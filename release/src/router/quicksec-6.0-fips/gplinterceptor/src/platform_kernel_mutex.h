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
   Copyright (c) 2010-2012, AuthenTec Inc, all rights reserved. 
  
 */ 

/*
 * platform_kernel_mutex.h
 *
 * Linux interceptor specific defines for the kernel mutex API.
 *
 */

#ifndef PLATFORM_KERNEL_MUTEX_H
#define PLATFORM_KERNEL_MUTEX_H 1

/* Sanity check to make sure that optimizations get done correcly
   on a UP machine. The following block can be safely removed. */
#ifndef CONFIG_SMP
#ifdef __ASM_SPINLOCK_H
#error "asm/spinlock.h" should not be included explicitly.
#endif /* __ASM_SPINLOCK_H */
#endif /* !CONFIG_SMP */

#include "linux_params.h"
#include "linux_mutex_internal.h"

/* Directly map linux mutexes to macros. This causes significantly
   less overhead in the non-preemptive UP case, where these macros 
   are empty. */
#ifndef KERNEL_MUTEX_USE_FUNCTIONS

/* This code should not be used unless DEBUG_LIGHT is disabled. */

#define ssh_kernel_mutex_lock(a) spin_lock(&((a)->lock))
#define ssh_kernel_mutex_unlock(b) spin_unlock(&((b)->lock))

#else /* KERNEL_MUTEX_USE_FUNCTIONS */

void ssh_kernel_mutex_lock_i(SshKernelMutex mutex);
void ssh_kernel_mutex_unlock_i(SshKernelMutex mutex);

#define ssh_kernel_mutex_lock(a) \
  ssh_kernel_mutex_lock_i((a))
#define ssh_kernel_mutex_unlock(a) \
  ssh_kernel_mutex_unlock_i((a))

#endif /* KERNEL_MUTEX_USE_FUNCTIONS */

#endif /* PLATFORM_KERNEL_MUTEX_H */
