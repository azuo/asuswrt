/**
   The following copyright and permission notice must be included in all
   copies, modified as well as unmodified, of this file.

   This file is free software: you may copy, redistribute and/or modify it
   under the terms of the GNU General Public License as published by the
   Free Software Foundation, either version 2 of the License, or (at your
   option) any later version.

   This file is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.

   This file incorporates work covered by the following copyright and
   permission notice:

   @copyright
   Copyright (c) 2010-2015, INSIDE Secure Oy. All rights reserved.

 */

/*
 * linux_procfs.c
 *
 * Linux interceptor proc filesystem interface for kernel to user space
 * messaging.
 *
 */

#include "linux_internal.h"
#include <linux/wait.h>

/************************ Module parameters *********************************/
extern SshInterceptor ssh_interceptor_context;

/************************ Internal utility functions ************************/

/* Use printk instead of SSH_DEBUG macros. */
#ifdef DEBUG_LIGHT
#define SSH_LINUX_PROCFS_DEBUG(x...) if (net_ratelimit()) printk(KERN_INFO x)
#define SSH_LINUX_PROCFS_WARN(x...) printk(KERN_EMERG x)
#endif /* DEBUG_LIGHT */

#ifndef SSH_LINUX_PROCFS_DEBUG
#define SSH_LINUX_PROCFS_DEBUG(x...)
#define SSH_LINUX_PROCFS_WARN(x...)
#endif /* SSH_LINUX_PROCFS_DEBUG */

/* Maximum number of bytes that can be written to the ipm proc entry
   in one call. This limits the time spent softirqs disabled by forcing
   the application to perform another write operation. */
#define SSH_LINUX_PROCFS_IPM_WRITE_MAX_LENGTH   \
  (4 * SSH_LINUX_IPM_RECV_BUFFER_SIZE)

static int
interceptor_proc_entry_fop_open(SshInterceptorProcEntry entry,
				struct file *file)
{
  /* Allow only one userspace application at a time. */
  write_lock(&entry->lock);
  if (entry->open == TRUE || entry->enabled == FALSE)
    {
      write_unlock(&entry->lock);
      return -EBUSY;
    }

  /* Increment module ref to prohibit module unloading. */
  if (!ssh_linux_module_inc_use_count())
    {
      write_unlock(&entry->lock);
      return -EBUSY;
    }

  file->private_data = ssh_interceptor_context;
  entry->buf_len = 0;
  entry->open = TRUE;

  write_unlock(&entry->lock);

  return 0;
}

static int
interceptor_proc_entry_fop_release(SshInterceptorProcEntry entry)
{
  write_lock(&entry->lock);

  /* Release the module reference. */
  ssh_linux_module_dec_use_count();

  /* Mark proc entry closed */
  entry->open = FALSE;

  write_unlock(&entry->lock);

  return 0;
}

/************************ Ipm proc entry ************************************/
#ifdef LINUX_USE_WAIT_EVENT
static Boolean
interceptor_ipm_proc_entry_read_allowed(SshInterceptor interceptor)
{
  Boolean ret = FALSE;

  local_bh_disable();
  write_lock(&interceptor->ipm.lock);

  if (interceptor->ipm.send_queue != NULL)
    ret = TRUE;

  write_unlock(&interceptor->ipm.lock);
  local_bh_enable();
  return ret;
}
#endif /* LINUX_USE_WAIT_EVENT */

static ssize_t
interceptor_ipm_proc_entry_fop_read(struct file *file,
				    char __user *buf,
				    size_t len,
				    loff_t *pos)
{
  SshInterceptor interceptor = file->private_data;
  SshInterceptorIpmMsg msg = NULL;
  ssize_t msg_len;

  write_lock(&interceptor->ipm_proc_entry.lock);

  /* Allow only one read at a time. */
  if (interceptor->ipm_proc_entry.read_active)
    {
      write_unlock(&interceptor->ipm_proc_entry.lock);
      SSH_LINUX_PROCFS_DEBUG("interceptor_ipm_proc_entry_fop_read: -EBUSY\n");
      return -EBUSY;
    }

  interceptor->ipm_proc_entry.read_active = TRUE;

  /* Continue from the partial message. */
  if (interceptor->ipm_proc_entry.send_msg != NULL)
    msg = interceptor->ipm_proc_entry.send_msg;

  write_unlock(&interceptor->ipm_proc_entry.lock);

 retry:
  if (msg == NULL)
    {
      /* Get the next message from send queue. */
      local_bh_disable();
      write_lock(&interceptor->ipm.lock);

      /* Take next message from send queue. */
      if (interceptor->ipm.send_queue != NULL)
	{
	  msg = interceptor->ipm.send_queue;

	  interceptor->ipm.send_queue = msg->next;
	  if (msg->next)
	    msg->next->prev = NULL;
	  msg->next = NULL;

	  if (msg == interceptor->ipm.send_queue_tail)
	    interceptor->ipm.send_queue_tail = msg->next;

          if (msg->reliable == 0)
            {
              SSH_ASSERT(interceptor->ipm.send_queue_num_unreliable > 0);
              interceptor->ipm.send_queue_num_unreliable--;
            }
	}

      write_unlock(&interceptor->ipm.lock);
      local_bh_enable();
    }

  if (msg == NULL)
    {
      /* Non-blocking mode, fail read. */
      if (file->f_flags & O_NONBLOCK)
	{
	  write_lock(&interceptor->ipm_proc_entry.lock);
	  interceptor->ipm_proc_entry.read_active = FALSE;
	  write_unlock(&interceptor->ipm_proc_entry.lock);

	  return -EAGAIN;
	}

      /* Blocking mode, sleep until a message or a signal arrives. */
#ifdef LINUX_USE_WAIT_EVENT
      wait_event_interruptible(interceptor->ipm_proc_entry.wait_queue,
                               interceptor_ipm_proc_entry_read_allowed(
                                                            interceptor));
#else /* LINUX_USE_WAIT_EVENT */
      interruptible_sleep_on(&interceptor->ipm_proc_entry.wait_queue);
#endif /* LINUX_USE_WAIT_EVENT */

      if (signal_pending(current))
	{
	  SSH_LINUX_PROCFS_DEBUG("interceptor_ipm_proc_entry_fop_read: "
				 "-ERESTARTSYS\n");

	  write_lock(&interceptor->ipm_proc_entry.lock);
	  interceptor->ipm_proc_entry.read_active = FALSE;
	  write_unlock(&interceptor->ipm_proc_entry.lock);

	  return -ERESTARTSYS;
	}

      goto retry;
    }

  write_lock(&interceptor->ipm_proc_entry.lock);
  interceptor->ipm_proc_entry.send_msg = msg;
  write_unlock(&interceptor->ipm_proc_entry.lock);

  /* Copy message to userspace. */
  msg_len = msg->len - msg->offset;
  if (len < msg_len)
    msg_len = len;

  if (copy_to_user(buf, msg->buf + msg->offset, msg_len))
    {
      SSH_LINUX_PROCFS_WARN("interceptor_ipm_proc_entry_fop_read: "
			    "copy_to_user failed, dropping message\n");

      write_lock(&interceptor->ipm_proc_entry.lock);
      interceptor->ipm_proc_entry.send_msg = NULL;
      interceptor->ipm_proc_entry.read_active = FALSE;
      write_unlock(&interceptor->ipm_proc_entry.lock);

      interceptor_ipm_message_free(interceptor, msg);
      return -EFAULT;
    }

  msg->offset += msg_len;

  /* Whole message was sent. */
  if (msg->offset >= msg->len)
    {
      write_lock(&interceptor->ipm_proc_entry.lock);
      interceptor->ipm_proc_entry.send_msg = NULL;
      interceptor->ipm_proc_entry.read_active = FALSE;
      write_unlock(&interceptor->ipm_proc_entry.lock);

      interceptor_ipm_message_free(interceptor, msg);
    }
  else
    {
      write_lock(&interceptor->ipm_proc_entry.lock);
      interceptor->ipm_proc_entry.read_active = FALSE;
      write_unlock(&interceptor->ipm_proc_entry.lock);
    }

  return msg_len;
}

#ifdef LINUX_USE_WAIT_EVENT
static Boolean
interceptor_ipm_proc_entry_write_allowed(SshInterceptor interceptor)
{
  Boolean ret = TRUE;

  write_lock(&interceptor->ipm_proc_entry.lock);
  if (interceptor->ipm_proc_entry.write_active == TRUE)
    ret = FALSE;
  write_unlock(&interceptor->ipm_proc_entry.lock);

  return ret;
}
#endif /* LINUX_USE_WAIT_EVENT */

static ssize_t
interceptor_ipm_proc_entry_fop_write(struct file *file,
				     const char __user *buf,
				     size_t len,
				     loff_t *pos)
{
  SshInterceptor interceptor = file->private_data;
  size_t total_len, write_len, recv_len, consumed, msg_len;
  char *user_buf, *recv_buf;

  /* Limit the maximum write length to avoid running in softirq
     context for long periods of time. */
  if (len > SSH_LINUX_PROCFS_IPM_WRITE_MAX_LENGTH)
    write_len = SSH_LINUX_PROCFS_IPM_WRITE_MAX_LENGTH;
  else
    write_len = len;

  /* Refuse to receive any data if send queue is getting full.
     Note this here checks if the IPM message freelist is empty,
     which indicates that all IPM messages are in the send queue.

     Allowing a new write in such condition could cause a number
     of reply IPM messages to be queued for sending and this would
     cause either unreliable IPM messages to be discarded from the
     send queue or an emergency mallocation of a reliable IPM message.

     A better way to solve this problem is to refuse this write
     operation and force the application to read messages from the send
     queue before allowing another write. */
  local_bh_disable();
  read_lock(&interceptor->ipm.lock);
  if (interceptor->ipm.msg_freelist == NULL
      && interceptor->ipm.msg_allocated >= SSH_LINUX_MAX_IPM_MESSAGES)
    write_len = 0;
  read_unlock(&interceptor->ipm.lock);
  local_bh_enable();

  if (write_len == 0)
    return -EAGAIN;

  /* Check if there is another write going on. */
 retry:
  write_lock(&interceptor->ipm_proc_entry.lock);

  if (interceptor->ipm_proc_entry.write_active)
    {
      write_unlock(&interceptor->ipm_proc_entry.lock);

      /* Non-blocking mode, fail write. */
      if (file->f_flags & O_NONBLOCK)
	return -EAGAIN;

      /* Blocking mode, wait until other writes are done. */
#ifdef LINUX_USE_WAIT_EVENT
      wait_event_interruptible(interceptor->ipm_proc_entry.wait_queue,
                               interceptor_ipm_proc_entry_write_allowed(
                                                                interceptor));
#else /* LINUX_USE_WAIT_EVENT */
      interruptible_sleep_on(&interceptor->ipm_proc_entry.wait_queue);
#endif /* LINUX_USE_WAIT_EVENT */

      if (signal_pending(current))
	{
	  SSH_LINUX_PROCFS_DEBUG("interceptor_ipm_proc_entry_fop_write: "
				 "-ERESTARTSYS\n");
	  return -ERESTARTSYS;
	}

      goto retry;
    }

  interceptor->ipm_proc_entry.write_active = TRUE;
  write_unlock(&interceptor->ipm_proc_entry.lock);

  /* Receive data. */
  total_len = 0;
  user_buf = (char *) buf;
  while (user_buf < (buf + write_len))
    {
      /* Copy data from user to receive buffer up to the maximum
         allowed write size. */
      user_buf = (char *) (buf + total_len);

      recv_buf = (interceptor->ipm_proc_entry.recv_buf
		  + interceptor->ipm_proc_entry.recv_len);
      recv_len = (interceptor->ipm_proc_entry.recv_buf_size
		  - interceptor->ipm_proc_entry.recv_len);

      /* Break out of the loop if receive buffer is full. */
      if (recv_len == 0)
	break;

      if (recv_len > (write_len - total_len))
	recv_len = (write_len - total_len);

      if (copy_from_user(recv_buf, user_buf, recv_len))
	{
	  SSH_LINUX_PROCFS_WARN("interceptor_ipm_proc_entry_fop_write: "
				"copy_from_user failed, dropping message\n");

	  write_lock(&interceptor->ipm_proc_entry.lock);
	  interceptor->ipm_proc_entry.write_active = FALSE;
	  write_unlock(&interceptor->ipm_proc_entry.lock);

	  wake_up_interruptible(&interceptor->ipm_proc_entry.wait_queue);
	  return -EFAULT;
	}

      total_len += recv_len;
      interceptor->ipm_proc_entry.recv_len += recv_len;

      /* Parse ipm messages. */
      consumed = 0;
      while (consumed < interceptor->ipm_proc_entry.recv_len)
	{
	  msg_len =
	    ssh_interceptor_receive_from_ipm(interceptor->ipm_proc_entry.
					     recv_buf + consumed,
					     interceptor->ipm_proc_entry.
					     recv_len - consumed);

	  /* Need more data. */
	  if (msg_len == 0)
	    break;

	  /* Else continue parsing ipm messages. */
	  consumed += msg_len;
	}

      /* Move unparsed data to beginning of receive buffer. */
      if (consumed > 0)
	{
	  SSH_ASSERT(consumed <= interceptor->ipm_proc_entry.recv_len);

	  if (consumed < interceptor->ipm_proc_entry.recv_len)
	    memmove(interceptor->ipm_proc_entry.recv_buf,
		    interceptor->ipm_proc_entry.recv_buf + consumed,
		    interceptor->ipm_proc_entry.recv_len - consumed);

	  interceptor->ipm_proc_entry.recv_len -= consumed;
	}

      /* Continue receiving data from user. */
    }

  write_lock(&interceptor->ipm_proc_entry.lock);
  interceptor->ipm_proc_entry.write_active = FALSE;
  write_unlock(&interceptor->ipm_proc_entry.lock);

  if (total_len == 0)
    {
      SSH_LINUX_PROCFS_WARN("interceptor_ipm_proc_entry_fop_write: "
                            "Out of receive buffer space\n");
      return -ENOMEM;
    }

  SSH_ASSERT(total_len <= write_len);
  return total_len;
}

static unsigned int
interceptor_ipm_proc_entry_fop_poll(struct file *file,
				    struct poll_table_struct *table)
{
  unsigned int mask = 0;
  SshInterceptor interceptor = file->private_data;

  poll_wait(file, &interceptor->ipm_proc_entry.wait_queue, table);

  /* Check if there are messages in the send queue. */
  read_lock(&interceptor->ipm_proc_entry.lock);
  if (interceptor->ipm_proc_entry.send_msg != NULL)
    mask |= (POLLIN | POLLRDNORM);
  read_unlock(&interceptor->ipm_proc_entry.lock);

  local_bh_disable();
  read_lock(&interceptor->ipm.lock);

  /* Check if there is a message pending for sending. */
  if (interceptor->ipm.send_queue != NULL)
    mask |= (POLLIN | POLLRDNORM);

  /* /proc entry is always writable, unless send queue is too long. */
  if (interceptor->ipm.msg_freelist != NULL
      || interceptor->ipm.msg_allocated < SSH_LINUX_MAX_IPM_MESSAGES)
    mask |= (POLLOUT | POLLWRNORM);

  read_unlock(&interceptor->ipm.lock);
  local_bh_enable();

  return mask;
}

static int
interceptor_ipm_proc_entry_fop_open(struct inode *inode,
				    struct file *file)
{
  write_lock(&ssh_interceptor_context->ipm_proc_entry.lock);

  /* Check that engine initialization has been completed. */
  if (ssh_interceptor_context->ipm_proc_entry.open)
    {
      write_unlock(&ssh_interceptor_context->ipm_proc_entry.lock);
      SSH_LINUX_PROCFS_DEBUG("interceptor_ipm_proc_entry_fop_open: -EBUSY\n");
      return -EBUSY;
    }

  /* Increment module ref to prohibit module unloading. */
  if (!ssh_linux_module_inc_use_count())
    {
      write_unlock(&ssh_interceptor_context->ipm_proc_entry.lock);
      SSH_LINUX_PROCFS_DEBUG("interceptor_ipm_proc_entry_fop_open: -EBUSY\n");
      return -EBUSY;
    }

  file->private_data = ssh_interceptor_context;

  ssh_interceptor_context->ipm_proc_entry.open = TRUE;

  /* Clear receive buffer. */
  ssh_interceptor_context->ipm_proc_entry.recv_len = 0;

  write_unlock(&ssh_interceptor_context->ipm_proc_entry.lock);

  interceptor_ipm_open(ssh_interceptor_context);
  ssh_interceptor_notify_ipm_open(ssh_interceptor_context);

  return 0;
}

static int
interceptor_ipm_proc_entry_fop_release(struct inode *inode,
				       struct file *file)
{
  SshInterceptor interceptor = file->private_data;
  SshInterceptorIpmMsg msg;

  ssh_interceptor_notify_ipm_close(interceptor);
  interceptor_ipm_close(interceptor);

  write_lock(&interceptor->ipm_proc_entry.lock);

  interceptor->ipm_proc_entry.open = FALSE;

  /* Release the module reference. */
  ssh_linux_module_dec_use_count();

  /* Clear receive buffer. */
  interceptor->ipm_proc_entry.recv_len = 0;

  /* Free partial output message */
  msg = interceptor->ipm_proc_entry.send_msg;
  interceptor->ipm_proc_entry.send_msg = NULL;

  write_unlock(&interceptor->ipm_proc_entry.lock);

  if (msg)
    interceptor_ipm_message_free(interceptor, msg);

  return 0;
}

/* Creates and initializes the procfs entry */
static struct proc_dir_entry*
interceptor_create_procfs_entry (const char* name, umode_t mode,
                                 struct proc_dir_entry *parent,
                                 struct file_operations *fops)
{
  struct proc_dir_entry *pde = NULL;

#ifdef LINUX_HAS_PROC_CREATE_DATA
  pde = proc_create_data(name, mode, parent, fops, NULL);
  /* fops cannot be null for this case */
#else /* LINUX_HAS_PROC_CREATE_DATA */
  pde = create_proc_entry(name, mode, parent);
  if (pde == NULL)
    return NULL;
  /* Set ops to NULL so that the entries cannot be used until later, when
  ssh_interceptor_proc_enable() is called */
  pde->proc_fops = NULL;
  pde->proc_iops = NULL;
#endif /* LINUX_HAS_PROC_CREATE_DATA */

  if (pde == NULL)
    return NULL;

#ifdef LINUX_HAS_PROC_DIR_ENTRY_OWNER
  pde->owner = THIS_MODULE;
#endif

#ifdef LINUX_HAS_PROC_SET_FUNCTIONS
  proc_set_size(pde, 0);
  proc_set_user(pde, KUIDT_INIT(0), KGIDT_INIT(0));
#else  /* LINUX_HAS_PROC_SET_FUNCTIONS */
  pde->uid = 0;
  pde->gid = 0;
  pde->size = 0;
#endif /* LINUX_HAS_PROC_SET_FUNCTIONS */

  return pde;
}

static struct file_operations ipm_proc_entry_fops =
{
  .read = interceptor_ipm_proc_entry_fop_read,
  .write = interceptor_ipm_proc_entry_fop_write,
  .poll = interceptor_ipm_proc_entry_fop_poll,
  .open = interceptor_ipm_proc_entry_fop_open,
  .release = interceptor_ipm_proc_entry_fop_release
};

static Boolean
interceptor_ipm_proc_entry_init(SshInterceptor interceptor)
{
  /* Assert that parent dir exists. */
  SSH_ASSERT(interceptor->proc_dir != NULL);

  /* Initialize proc entry structure. */
  init_waitqueue_head(&interceptor->ipm_proc_entry.wait_queue);
  rwlock_init(&interceptor->ipm_proc_entry.lock);

  write_lock(&interceptor->ipm_proc_entry.lock);
  interceptor->ipm_proc_entry.open = TRUE;
  interceptor->ipm_proc_entry.write_active = TRUE;
  interceptor->ipm_proc_entry.read_active = TRUE;
  interceptor->ipm_proc_entry.enabled = TRUE;
  write_unlock(&interceptor->ipm_proc_entry.lock);

  interceptor->ipm_proc_entry.recv_buf_size = SSH_LINUX_IPM_RECV_BUFFER_SIZE;
  interceptor->ipm_proc_entry.recv_buf =
    ssh_malloc(interceptor->ipm_proc_entry.recv_buf_size);
  if (interceptor->ipm_proc_entry.recv_buf == NULL)
    return FALSE;

  /* Create entry to procfs. */
  interceptor->ipm_proc_entry.entry =
    interceptor_create_procfs_entry(SSH_PROC_ENGINE,
                                    S_IFREG | S_IRUSR | S_IWUSR,
                                    interceptor->proc_dir,
                                    &ipm_proc_entry_fops);

  if (interceptor->ipm_proc_entry.entry == NULL)
    return FALSE;

  /* Mark proc entry ready for use. */
  write_lock(&interceptor->ipm_proc_entry.lock);
  interceptor->ipm_proc_entry.open = FALSE;
  interceptor->ipm_proc_entry.write_active = FALSE;
  interceptor->ipm_proc_entry.read_active = FALSE;
  write_unlock(&interceptor->ipm_proc_entry.lock);

  return TRUE;
}

static void
interceptor_ipm_proc_entry_uninit(SshInterceptor interceptor)
{
  if (interceptor->ipm_proc_entry.recv_buf != NULL)
    ssh_free(interceptor->ipm_proc_entry.recv_buf);
  interceptor->ipm_proc_entry.recv_buf = NULL;

  /* This should be safe to do without locking as interceptor code
     does not refer `interceptor->ipm_proc_entry.entry' except in
     init/uninit. */
  if (interceptor->ipm_proc_entry.entry != NULL)
    PROC_REMOVE(interceptor->ipm_proc_entry.entry, interceptor->proc_dir);
  write_lock(&interceptor->ipm_proc_entry.lock);
  interceptor->ipm_proc_entry.entry = NULL;
  interceptor->ipm_proc_entry.enabled = FALSE;
  write_unlock(&interceptor->ipm_proc_entry.lock);
}

/************************ Version proc_entry ********************************/

static ssize_t
interceptor_version_proc_entry_fop_read(struct file *file,
					char __user *buf,
					size_t len,
					loff_t *pos)
{
  SshInterceptor interceptor = file->private_data;
  ssize_t version_len;

  /* Check if another read / write is ongoing */
  write_lock(&interceptor->version_proc_entry.lock);
  if (interceptor->version_proc_entry.active == TRUE)
    {
      write_unlock(&interceptor->version_proc_entry.lock);
      return -EBUSY;
    }
  interceptor->version_proc_entry.active = TRUE;
  write_unlock(&interceptor->version_proc_entry.lock);

  if (*pos == 0)
    {
      version_len =
	snprintf(interceptor->version_proc_entry.buf,
		 sizeof(interceptor->version_proc_entry.buf),
		 "INSIDE Secure %s built on " __DATE__ " " __TIME__ "\n",
                  ssh_engine_version);
      interceptor->version_proc_entry.buf_len = version_len;
    }

  else if (*pos >= interceptor->version_proc_entry.buf_len)
    {
      interceptor->version_proc_entry.buf_len = 0;
      version_len = 0;
      goto out;
    }

  else
    {
      version_len = interceptor->version_proc_entry.buf_len - *pos;
    }

  if (len < version_len)
    version_len = len;

  if (copy_to_user(buf, interceptor->version_proc_entry.buf + *pos,
		   version_len))
    {
      interceptor->version_proc_entry.buf_len = 0;
      version_len = -EFAULT;
      goto out;
    }

  *pos += version_len;

 out:
  write_lock(&interceptor->version_proc_entry.lock);
  interceptor->version_proc_entry.active = FALSE;
  write_unlock(&interceptor->version_proc_entry.lock);

  return version_len;
}

static int
interceptor_version_proc_entry_fop_open(struct inode *inode,
					struct file *file)
{
  return interceptor_proc_entry_fop_open(&ssh_interceptor_context->
                                         version_proc_entry,
                                         file);
}

static int
interceptor_version_proc_entry_fop_release(struct inode *inode,
					   struct file *file)
{
  SshInterceptor interceptor = file->private_data;
  return interceptor_proc_entry_fop_release(&interceptor->version_proc_entry);
}

static struct file_operations version_proc_entry_fops =
{
  .read = interceptor_version_proc_entry_fop_read,
  .open = interceptor_version_proc_entry_fop_open,
  .release = interceptor_version_proc_entry_fop_release
};

static Boolean
interceptor_version_proc_entry_init(SshInterceptor interceptor)
{
  /* Assert that parent dir exists. */
  SSH_ASSERT(interceptor->proc_dir != NULL);

  /* Initialize proc entry structure. */
  rwlock_init(&interceptor->version_proc_entry.lock);
  interceptor->version_proc_entry.buf_len = 0;

  /* Take lock to use same variables always under lock to not confuse
     static analysis. */
  write_lock(&interceptor->version_proc_entry.lock);
  interceptor->version_proc_entry.active = TRUE;
  interceptor->version_proc_entry.open = TRUE;
  interceptor->version_proc_entry.enabled = TRUE;
  write_unlock(&interceptor->version_proc_entry.lock);

  /* Create entry to procfs. */
  interceptor->version_proc_entry.entry =
    interceptor_create_procfs_entry(SSH_PROC_VERSION,
                                    S_IFREG | S_IRUSR,
                                    interceptor->proc_dir,
                                    &version_proc_entry_fops);

  if (interceptor->version_proc_entry.entry == NULL)
    return FALSE;

  /* Mark proc entry ready for use. */
  write_lock(&interceptor->version_proc_entry.lock);
  interceptor->version_proc_entry.active = FALSE;
  interceptor->version_proc_entry.open = FALSE;
  write_unlock(&interceptor->version_proc_entry.lock);

  return TRUE;
}

static void
interceptor_version_proc_entry_uninit(SshInterceptor interceptor)
{
  if (interceptor->version_proc_entry.entry == NULL)
    return;

  /* This should be safe to do without locking as interceptor code
     does not refer `interceptor->version_proc_entry.entry' except in
     init/uninit. */
  PROC_REMOVE(interceptor->version_proc_entry.entry, interceptor->proc_dir);
  write_lock(&interceptor->version_proc_entry.lock);
  interceptor->version_proc_entry.entry = NULL;
  interceptor->version_proc_entry.enabled = FALSE;
  write_unlock(&interceptor->version_proc_entry.lock);
}


/************************ Proc init / uninit ********************************/

void ssh_interceptor_proc_enable(SshInterceptor interceptor)
{
#ifndef LINUX_HAS_PROC_CREATE_DATA
  /* Attach file operations to IPM proc entry. After this the connection
     attempts from PM succeed. */
  interceptor->ipm_proc_entry.entry->proc_fops = &ipm_proc_entry_fops;
#endif /* LINUX_HAS_PROC_CREATE_DATA */
  write_lock(&interceptor->ipm_proc_entry.lock);
  interceptor->ipm_proc_entry.enabled = TRUE;
  write_unlock(&interceptor->ipm_proc_entry.lock);

#ifndef LINUX_HAS_PROC_CREATE_DATA
  /* Attach file operations to version proc entry. */
  interceptor->version_proc_entry.entry->proc_fops = &version_proc_entry_fops;
#endif /* LINUX_HAS_PROC_CREATE_DATA */
  write_lock(&interceptor->version_proc_entry.lock);
  interceptor->version_proc_entry.enabled = TRUE;
  write_unlock(&interceptor->version_proc_entry.lock);
}

Boolean ssh_interceptor_proc_init(SshInterceptor interceptor)
{
  char name[128];

  /* Create a directory under /proc/ */
  snprintf(name, sizeof(name), "%s%s", SSH_PROC_ROOT, ssh_device_suffix);

  interceptor->proc_dir = proc_mkdir(name, NULL);

  if (interceptor->proc_dir == NULL)
    goto error;

  if (interceptor_ipm_proc_entry_init(interceptor) == FALSE)
    goto error;

  if (interceptor_version_proc_entry_init(interceptor) == FALSE)
    goto error;

  return TRUE;

 error:
  SSH_DEBUG(SSH_D_ERROR, ("Could not create /proc/%s", name));

  interceptor_ipm_proc_entry_uninit(interceptor);
  interceptor_version_proc_entry_uninit(interceptor);

  if (interceptor->proc_dir)
    PROC_REMOVE(interceptor->proc_dir, NULL);
  interceptor->proc_dir = NULL;

  return FALSE;
}

void ssh_interceptor_proc_uninit(SshInterceptor interceptor)
{
  /* Enable softirqs. */
  local_bh_enable();

  interceptor_ipm_proc_entry_uninit(interceptor);
  interceptor_version_proc_entry_uninit(interceptor);

  if (interceptor->proc_dir)
    PROC_REMOVE(interceptor->proc_dir, NULL);
  interceptor->proc_dir = NULL;

  /* Disable softirqs. */
  local_bh_disable();
}
