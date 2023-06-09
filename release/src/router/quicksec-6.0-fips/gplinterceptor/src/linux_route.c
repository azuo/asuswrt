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
 * linux_route.c
 *
 * Linux interceptor implementation of interceptor API routing functions.
 *
 */

#include "linux_internal.h"

/****************** Internal data types and declarations ********************/

typedef struct SshInterceptorRouteResultRec
{
  SshIpAddrStruct gw[1];
  SshInterceptorIfnum ifnum;
  SshUInt16 mtu;
} SshInterceptorRouteResultStruct, *SshInterceptorRouteResult;

/****************** Internal Utility Functions ******************************/

#ifdef SSH_IPSEC_IP_ONLY_INTERCEPTOR
#if (defined LINUX_FRAGMENTATION_AFTER_NF_POST_ROUTING) \
  || (defined LINUX_FRAGMENTATION_AFTER_NF6_POST_ROUTING)

/* Create a child dst_entry with locked interface MTU, and attach it to `dst'.
   This is needed on newer linux kernels and IP_ONLY_INTERCEPTOR builds,
   where the IP stack fragments packets to path MTU after ssh_interceptor_send.
*/
static struct dst_entry *
interceptor_route_create_child_dst(struct dst_entry *dst, Boolean ipv6)
{
  struct dst_entry *child;
#ifdef LINUX_DST_HAS_NEIGHBOUR
  struct neighbour *neigh;
#endif /* LINUX_DST_HAS_NEIGHBOUR */
#ifdef LINUX_DST_ALLOC_REQUIRES_ZEROING
  struct rt6_info *rt6, *rt6_child;
  struct rtable *rt, *rt_child;
#endif /* LINUX_DST_ALLOC_REQUIRES_ZEROING */
#ifdef LINUX_HAS_DST_METRICS_ACCESSORS
  SshUInt32 set;
#endif /* LINUX_HAS_DST_METRICS_ACCESSORS */

  /* Allocate a dst_entry and copy relevant fields from dst. */
  child = SSH_DST_ALLOC(dst);
  if (child == NULL)
    return NULL;

  child->input = dst->input;
  child->output = dst->output;

  /* Child is not added to dst hash, and linux native IPsec is disabled. */
  child->flags |= (DST_NOHASH | DST_NOPOLICY | DST_NOXFRM);

#ifdef LINUX_DST_ALLOC_REQUIRES_ZEROING
  /* Starting from kernel version 3.0 the dst entries are allocated
     using kmem_cache_alloc() instead of kmem_cache_zalloc(). Therefore
     the caller is responsible for initializing the rest of the data
     after the common dst_entry part. */
  if (ipv6 == TRUE) /* IPv6 */
    {
      rt6_child = (struct rt6_info *)child;
      rt6 = (struct rt6_info *)dst;
      memset(&rt6_child->rt6i_table, 0,
          sizeof(*rt6_child) - sizeof(struct dst_entry));
#ifdef LINUX_RT6_INFO_PEER_LONG
      rt6_child->_rt6i_peer = rt6->_rt6i_peer;
#endif /* LINUX_RT6_INFO_PEER_LONG */

      rt6_child->rt6i_table = rt6->rt6i_table;
      rt6_child->rt6i_gateway = rt6->rt6i_gateway;
      rt6_child->rt6i_idev = rt6->rt6i_idev;
      rt6_child->rt6i_flags = rt6->rt6i_flags;
      rt6_child->rt6i_protocol = rt6->rt6i_protocol;
    }
  else /* IPv4 */
    {
      rt_child = (struct rtable *)child;
      rt = (struct rtable *)dst;
      memset(&SSH_RTABLE_FIRST_MEMBER(rt_child), 0,
            sizeof(*rt_child) - sizeof(struct dst_entry));

#ifdef LINUX_RT_SLIM
      if (dst->dev != NULL)
        rt_child->rt_pmtu = dst->dev->mtu;
      INIT_LIST_HEAD(&rt_child->rt_uncached);
      rt_child->rt_is_input = rt->rt_is_input;
      rt_child->rt_uses_gateway = rt->rt_uses_gateway;
#endif /* LINUX_RT_SLIM */

      rt_child->rt_flags = rt->rt_flags;
      rt_child->rt_type = rt->rt_type;
      rt_child->rt_iif = rt->rt_iif;
      rt_child->rt_gateway = rt->rt_gateway;
    }
#endif /* LINUX_DST_ALLOC_REQUIRES_ZEROING */

  if (ipv6 == TRUE || SSH_LINUX_IPV4_DST_USES_METRICS == 1)
    {
  /* Copy route metrics and lock MTU to interface MTU. */
#ifdef LINUX_HAS_DST_METRICS_ACCESSORS
  dst_copy_metrics(child, dst);
  set = dst_metric(child, RTAX_LOCK);
  set |= 1 << RTAX_MTU;
  dst_metric_set(child, RTAX_LOCK, set);
  if (dst->dev != NULL)
    dst_metric_set(child, RTAX_MTU, dst->dev->mtu);
#else  /* LINUX_HAS_DST_METRICS_ACCESSORS */
  memcpy(child->metrics, dst->metrics, sizeof(child->metrics));
  child->metrics[RTAX_LOCK-1] |= 1 << RTAX_MTU;
  if (dst->dev != NULL)
    child->metrics[RTAX_MTU-1] = dst->dev->mtu;
#endif /* LINUX_HAS_DST_METRICS_ACCESSORS */
    }

  if (dst->expires > 0)
    child->expires = dst->expires;

#ifdef CONFIG_NET_CLS_ROUTE
  child->tclassid = dst->tclassid;
#endif /* CONFIG_NET_CLS_ROUTE */

#ifdef CONFIG_XFRM
  child->xfrm = NULL;
#endif /* CONFIG_XFRM */

#ifdef LINUX_HAS_HH_CACHE
  if (dst->hh)
    {
      atomic_inc(&dst->hh->hh_refcnt);
      child->hh = dst->hh;
    }
#endif /* LINUX_HAS_HH_CACHE */

#ifdef LINUX_DST_HAS_NEIGHBOUR
  SSH_DST_NEIGHBOUR_READ_LOCK();
  neigh = SSH_DST_GET_NEIGHBOUR(dst);

  if (neigh != NULL)
    {
      SSH_DST_SET_NEIGHBOUR(child, neigh_clone(neigh));
    }
  SSH_DST_NEIGHBOUR_READ_UNLOCK();
#endif /* LINUX_DST_HAS_NEIGHBOUR */

  if (dst->dev)
    {
      dev_hold(dst->dev);
      child->dev = dst->dev;
    }

  SSH_ASSERT(dst->child == NULL);
  dst->child = dst_clone(child);

  SSH_DEBUG(SSH_D_MIDOK, ("Allocated child %p dst_entry for dst %p mtu %d",
                          child, dst, dst_mtu(dst)));

  return child;
}

#endif /* LINUX_FRAGMENTATION_AFTER_NF_POST_ROUTING
          || LINUX_FRAGMENTATION_AFTER_NF6_POST_ROUTING */
#endif /* SSH_IPSEC_IP_ONLY_INTERCEPTOR */

static inline int interceptor_ip4_route_output(struct rtable **rt,
                                               struct flowi *fli)
{
  int rval = 0;
#ifdef LINUX_USE_NF_FOR_ROUTE_OUTPUT
  struct dst_entry *dst = NULL;
  const struct nf_afinfo *afinfo = NULL;
  unsigned short family = AF_INET;

  rcu_read_lock();
  afinfo = nf_get_afinfo(family);
  if (!afinfo)
    {
      rcu_read_unlock();
      SSH_DEBUG(SSH_D_FAIL, ("Failed to get nf afinfo"));
      return -1;
    }

  rval = afinfo->route(&init_net, &dst, fli, TRUE);
  rcu_read_unlock();

  *rt = (struct rtable *)dst;

#else /* LINUX_USE_NF_FOR_ROUTE_OUTPUT */

#ifdef LINUX_IP_ROUTE_OUTPUT_KEY_HAS_NET_ARGUMENT
  rval = ip_route_output_key(&init_net, rt, fli);
#else /* LINUX_IP_ROUTE_OUTPUT_KEY_HAS_NET_ARGUMENT */
  rval = ip_route_output_key(rt, fli);
#endif /* LINUX_IP_ROUTE_OUTPUT_KEY_HAS_NET_ARGUMENT */
#endif /* LINUX_USE_NF_FOR_ROUTE_OUTPUT */

  if (rval > 0)
    rval = -rval;

  return rval;
}

static inline struct dst_entry *interceptor_ip6_route_output(struct flowi *fli)
{
  struct dst_entry *dst = NULL;

  /* Perform route lookup */
  /* we do not need a socket, only fake flow */
#ifdef LINUX_USE_NF_FOR_ROUTE_OUTPUT
  const struct nf_afinfo *afinfo = NULL;
  unsigned short family = AF_INET6;
  int rval = 0;

  rcu_read_lock();
  afinfo = nf_get_afinfo(family);
  if (!afinfo)
    {
      rcu_read_unlock();
      SSH_DEBUG(SSH_D_FAIL, ("Failed to get nf afinfo"));
      return NULL;
    }

  rval = afinfo->route(&init_net, &dst, fli, FALSE);
  rcu_read_unlock();
  if (rval != 0)
    {
      SSH_DEBUG(SSH_D_FAIL, ("Failed to get route from IPv6 NF"));
      return NULL;
    }
#else /* LINUX_USE_NF_FOR_ROUTE_OUTPUT */

#ifdef LINUX_IP6_ROUTE_OUTPUT_KEY_HAS_NET_ARGUMENT
  dst = ip6_route_output(&init_net, NULL, fli);
#else /* LINUX_IP6_ROUTE_OUTPUT_KEY_HAS_NET_ARGUMENT */
  dst = ip6_route_output(NULL, fli);
#endif /* LINUX_IP6_ROUTE_OUTPUT_KEY_HAS_NET_ARGUMENT */
#endif /* LINUX_USE_NF_FOR_ROUTE_OUTPUT */

  return dst;
}

/****************** Implementation of ssh_interceptor_route *****************/

/* Perform route lookup using linux ip_route_output_key.

   The route lookup will use the following selectors:
   dst, src, outbound ifnum, ip protocol, tos, and fwmark.
   The source address is expected to be local or undefined.

   The following selectors are ignored:
   dst port, src port, icmp type, icmp code, ipsec spi. */
Boolean
ssh_interceptor_route_output_ipv4(SshInterceptor interceptor,
                                  SshInterceptorRouteKey key,
                                  SshUInt32 selector,
                                  SshInterceptorRouteResult result)
{
  u32 daddr;
  struct rtable *rt = NULL;
  int rval = 0;
  struct flowi rt_key;
  u16 rt_type;
#ifdef DEBUG_LIGHT
  unsigned char *rt_type_str;
  u32 fwmark = 0;
  unsigned char dst_buf[SSH_IP_ADDR_STRING_SIZE];
  unsigned char src_buf[SSH_IP_ADDR_STRING_SIZE];
#endif /* DEBUG_LIGHT */

  SSH_IP4_ENCODE(&key->dst, (unsigned char *) &daddr);

  /* Initialize rt_key with zero values */
  memset(&rt_key, 0, sizeof(rt_key));

  rt_key.fl4_dst = daddr;
  if (selector & SSH_INTERCEPTOR_ROUTE_KEY_SRC)
    SSH_IP4_ENCODE(&key->src, (unsigned char *) &rt_key.fl4_src);
  if (selector & SSH_INTERCEPTOR_ROUTE_KEY_OUT_IFNUM)
    {
      SSH_LINUX_ASSERT_VALID_IFNUM(key->ifnum);
      rt_key.oif = key->ifnum;
    }
  if (selector & SSH_INTERCEPTOR_ROUTE_KEY_IPPROTO)
    rt_key.proto = key->ipproto;
  if (selector & SSH_INTERCEPTOR_ROUTE_KEY_IP4_TOS)
    rt_key.fl4_tos = RT_TOS(key->nh.ip4.tos);
  rt_key.fl4_scope = RT_SCOPE_UNIVERSE;

#if (SSH_INTERCEPTOR_NUM_EXTENSION_SELECTORS > 0)
#ifdef SSH_LINUX_FWMARK_EXTENSION_SELECTOR

  /* Use linux fw_mark in routing */
  if (selector & SSH_INTERCEPTOR_ROUTE_KEY_EXTENSION)
    {
#ifdef LINUX_HAS_SKB_MARK
      rt_key.mark = key->extension[SSH_LINUX_FWMARK_EXTENSION_SELECTOR];
#else /* LINUX_HAS_SKB_MARK */
#ifdef CONFIG_IP_ROUTE_FWMARK
      rt_key.fl4_fwmark =
        key->extension[SSH_LINUX_FWMARK_EXTENSION_SELECTOR];
#endif /* CONFIG_IP_ROUTE_FWMARK */
#endif /* LINUX_HAS_SKB_MARK */
#ifdef DEBUG_LIGHT
      fwmark = key->extension[SSH_LINUX_FWMARK_EXTENSION_SELECTOR];
#endif /* DEBUG_LIGHT */
    }

#endif /* SSH_LINUX_FWMARK_EXTENSION_SELECTOR */
#endif /* (SSH_INTERCEPTOR_NUM_EXTENSION_SELECTORS > 0) */

  SSH_DEBUG(SSH_D_LOWOK,
            ("Route lookup: "
             "dst %s src %s ifnum %d ipproto %d tos 0x%02x fwmark 0x%lx",
             ssh_ipaddr_print(&key->dst, dst_buf, sizeof(dst_buf)),
             ((selector & SSH_INTERCEPTOR_ROUTE_KEY_SRC) ?
              ssh_ipaddr_print(&key->src, src_buf, sizeof(src_buf)) : NULL),
             (int) ((selector & SSH_INTERCEPTOR_ROUTE_KEY_OUT_IFNUM)?
                    key->ifnum : -1),
             (int) ((selector & SSH_INTERCEPTOR_ROUTE_KEY_IPPROTO) ?
                    key->ipproto : -1),
             ((selector & SSH_INTERCEPTOR_ROUTE_KEY_IP4_TOS) ?
              key->nh.ip4.tos : 0),
             (unsigned long) ((selector & SSH_INTERCEPTOR_ROUTE_KEY_EXTENSION)?
              fwmark : 0)));

  /* Perform route lookup */

  rval = interceptor_ip4_route_output(&rt, &rt_key);
  if (rval < 0)
    {
      goto fail;
    }

  /* Get the gateway, mtu and ifnum */

  SSH_IP4_DECODE(result->gw, &rt->rt_gateway);
#ifdef LINUX_RT_USE_DST_AS_GW
  if (!rt->rt_gateway)
    {
      *result->gw = key->dst;
    }
#endif /* LINUX_RT_USE_DST_AS_GW */
  result->mtu = SSH_DST_MTU(&SSH_RT_DST(rt));
  result->ifnum = SSH_RT_DST(rt).dev->ifindex;
  rt_type = rt->rt_type;

#ifdef DEBUG_LIGHT
  switch (rt_type)
    {
    case RTN_UNSPEC:
      rt_type_str = "unspec";
      break;
    case RTN_UNICAST:
      rt_type_str = "unicast";
      break;
    case RTN_LOCAL:
      rt_type_str = "local";
      break;
    case RTN_BROADCAST:
      rt_type_str = "broadcast";
      break;
    case RTN_ANYCAST:
      rt_type_str = "anycast";
      break;
    case RTN_MULTICAST:
      rt_type_str = "multicast";
      break;
    case RTN_BLACKHOLE:
      rt_type_str = "blackhole";
      break;
    case RTN_UNREACHABLE:
      rt_type_str = "unreachable";
      break;
    case RTN_PROHIBIT:
      rt_type_str = "prohibit";
      break;
    case RTN_THROW:
      rt_type_str = "throw";
      break;
    case RTN_NAT:
      rt_type_str = "nat";
      break;
    case RTN_XRESOLVE:
      rt_type_str = "xresolve";
      break;
    default:
      rt_type_str = "unknown";
    }
#endif /* DEBUG_LIGHT */

  SSH_DEBUG(SSH_D_LOWOK,
            ("Route result: dst %s via %s ifnum %d[%s] mtu %d type %s [%d]",
             dst_buf, ssh_ipaddr_print(result->gw, src_buf, sizeof(src_buf)),
             (int) result->ifnum,
             (SSH_RT_DST(rt).dev->name ? SSH_RT_DST(rt).dev->name : "none"),
             result->mtu, rt_type_str, rt_type));

#ifdef SSH_IPSEC_IP_ONLY_INTERCEPTOR
#ifdef LINUX_FRAGMENTATION_AFTER_NF_POST_ROUTING
  /* Check if need to create a child dst_entry with interface MTU. */
  if ((selector & SSH_INTERCEPTOR_ROUTE_KEY_FLAG_TRANSFORM_APPLIED)
      && SSH_RT_DST(rt).child == NULL)
    {
      if (interceptor_route_create_child_dst(&SSH_RT_DST(rt), FALSE) == NULL)
        SSH_DEBUG(SSH_D_FAIL, ("Could not create child dst_entry for dst %p",
                               &SSH_RT_DST(rt)));
    }
#endif /* LINUX_FRAGMENTATION_AFTER_NF_POST_ROUTING */
#endif /* SSH_IPSEC_IP_ONLY_INTERCEPTOR */

  /* Release the routing table entry ; otherwise a memory leak occurs
     in the route entry table. */
  ip_rt_put(rt);

  /* Assert that ifnum fits into the SshInterceptorIfnum data type. */
  SSH_LINUX_ASSERT_IFNUM(result->ifnum);

  /* Check that ifnum does not collide with SSH_INTERCEPTOR_INVALID_IFNUM. */
  if (result->ifnum == SSH_INTERCEPTOR_INVALID_IFNUM)
    goto fail;

  /* Accept only unicast, broadcast, anycast,  multicast and local routes */
  if (rt_type == RTN_UNICAST
      || rt_type == RTN_BROADCAST
      || rt_type == RTN_ANYCAST
      || rt_type == RTN_MULTICAST
      || rt_type == RTN_LOCAL)
    {
      SSH_LINUX_ASSERT_VALID_IFNUM(result->ifnum);
      return TRUE;
    }

 fail:
  /* Fail route lookup for other route types */
  SSH_DEBUG(SSH_D_FAIL, ("Route lookup for %s failed with code %d",
                         dst_buf, rval));

  return FALSE;
}


/* Perform route lookup using linux ip_route_input.

   The route lookup will use the following selectors:
   dst, src, inbound ifnum, ip protocol, tos, and fwmark.
   The source address is expected to be non-local and it must be defined.

   The following selectors are ignored:
   dst port, src port, icmp type, icmp code, ipsec spi. */
Boolean
ssh_interceptor_route_input_ipv4(SshInterceptor interceptor,
                                 SshInterceptorRouteKey key,
                                 SshUInt32 selector,
                                 SshInterceptorRouteResult result)
{
  u32 daddr, saddr;
  u8 ipproto;
  u8 tos;
  u32 fwmark;
  struct sk_buff *skbp;
  struct net_device *dev;
  struct rtable *rt;
  int rval = 0;
  u16 rt_type;
  struct iphdr *iph = NULL;
#ifdef DEBUG_LIGHT
  unsigned char *rt_type_str;
  unsigned char dst_buf[SSH_IP_ADDR_STRING_SIZE];
  unsigned char src_buf[SSH_IP_ADDR_STRING_SIZE];
#endif /* DEBUG_LIGHT */

  SSH_IP4_ENCODE(&key->dst, (unsigned char *) &daddr);

  /* Initialize */
  saddr = 0;
  ipproto = 0;
  tos = 0;
  fwmark = 0;
  dev = NULL;

  if (selector & SSH_INTERCEPTOR_ROUTE_KEY_SRC)
    SSH_IP4_ENCODE(&key->src, (unsigned char *) &saddr);
  if (selector & SSH_INTERCEPTOR_ROUTE_KEY_IN_IFNUM)
    {
      SSH_LINUX_ASSERT_VALID_IFNUM(key->ifnum);
      dev = ssh_interceptor_ifnum_to_netdev(interceptor, key->ifnum);
    }

  if (dev == NULL)
    return FALSE;

  if (selector & SSH_INTERCEPTOR_ROUTE_KEY_IPPROTO)
    ipproto = key->ipproto;
  if (selector & SSH_INTERCEPTOR_ROUTE_KEY_IP4_TOS)
    tos = RT_TOS(key->nh.ip4.tos);

#if (SSH_INTERCEPTOR_NUM_EXTENSION_SELECTORS > 0)
#ifdef SSH_LINUX_FWMARK_EXTENSION_SELECTOR
  /* Use linux fw_mark in routing */
  if (selector & SSH_INTERCEPTOR_ROUTE_KEY_EXTENSION)
    fwmark = key->extension[SSH_LINUX_FWMARK_EXTENSION_SELECTOR];
#endif /* SSH_LINUX_FWMARK_EXTENSION_SELECTOR */
#endif /* (SSH_INTERCEPTOR_NUM_EXTENSION_SELECTORS > 0) */

  /* Build dummy skb */
  skbp = alloc_skb(SSH_IPH4_HDRLEN, GFP_ATOMIC);
  if (skbp == NULL)
    goto fail;

  /* Set the device to the skb. */
  if (skbp->dev == NULL)
    {
      SSH_DEBUG(SSH_D_NICETOKNOW,
                ("Set temporarily dev to skb (and routing instance)."));
      skbp->dev = dev;
    }

  SSH_SKB_RESET_MACHDR(skbp);
  iph = (struct iphdr *) skb_put(skbp, SSH_IPH4_HDRLEN);
  if (iph == NULL)
    {
      dev_kfree_skb(skbp);
      goto fail;
    }
  SSH_SKB_SET_NETHDR(skbp, (unsigned char *) iph);

  SSH_SKB_DST_SET(skbp, NULL);
  skbp->protocol = __constant_htons(ETH_P_IP);
  SSH_SKB_MARK(skbp) = fwmark;
  iph->protocol = ipproto;

  SSH_DEBUG(SSH_D_LOWOK,
            ("Route lookup: "
             "dst %s src %s ifnum %d[%s] ipproto %d tos 0x%02x fwmark 0x%x",
             ssh_ipaddr_print(&key->dst, dst_buf, sizeof(dst_buf)),
             ((selector & SSH_INTERCEPTOR_ROUTE_KEY_SRC) ?
              ssh_ipaddr_print(&key->src, src_buf, sizeof(src_buf)) : NULL),
             dev->ifindex, dev->name,
             ipproto, tos, fwmark));

  /* Perform route lookup */

  rval = ip_route_input(skbp, daddr, saddr, tos, dev);
  if (rval < 0 || SSH_SKB_DST(skbp) == NULL)
    {
      dev_kfree_skb(skbp);
      goto fail;
    }

  /* Get the gateway, mtu and ifnum */
  rt = (struct rtable *) SSH_SKB_DST(skbp);
  SSH_IP4_DECODE(result->gw, &rt->rt_gateway);
#ifdef LINUX_RT_USE_DST_AS_GW
  if (!rt->rt_gateway)
    {
      *result->gw = key->dst;
    }
#endif /* LINUX_RT_USE_DST_AS_GW */
  result->mtu = SSH_DST_MTU(SSH_SKB_DST(skbp));
  result->ifnum = SSH_SKB_DST(skbp)->dev->ifindex;
  rt_type = rt->rt_type;

#ifdef DEBUG_LIGHT
  switch (rt_type)
    {
    case RTN_UNSPEC:
      rt_type_str = "unspec";
      break;
    case RTN_UNICAST:
      rt_type_str = "unicast";
      break;
    case RTN_LOCAL:
      rt_type_str = "local";
      break;
    case RTN_BROADCAST:
      rt_type_str = "broadcast";
      break;
    case RTN_ANYCAST:
      rt_type_str = "anycast";
      break;
    case RTN_MULTICAST:
      rt_type_str = "multicast";
      break;
    case RTN_BLACKHOLE:
      rt_type_str = "blackhole";
      break;
    case RTN_UNREACHABLE:
      rt_type_str = "unreachable";
      break;
    case RTN_PROHIBIT:
      rt_type_str = "prohibit";
      break;
    case RTN_THROW:
      rt_type_str = "throw";
      break;
    case RTN_NAT:
      rt_type_str = "nat";
      break;
    case RTN_XRESOLVE:
      rt_type_str = "xresolve";
      break;
    default:
      rt_type_str = "unknown";
    }
#endif /* DEBUG_LIGHT */

  SSH_DEBUG(SSH_D_LOWOK,
            ("Route result: dst %s via %s ifnum %d[%s] mtu %d type %s [%d]",
             dst_buf, ssh_ipaddr_print(result->gw, src_buf, sizeof(src_buf)),
             (int) result->ifnum,
             (SSH_RT_DST(rt).dev->name ? SSH_RT_DST(rt).dev->name : "none"),
             result->mtu, rt_type_str, rt_type));

#ifdef SSH_IPSEC_IP_ONLY_INTERCEPTOR
#ifdef LINUX_FRAGMENTATION_AFTER_NF_POST_ROUTING
  /* Check if need to create a child dst_entry with interface MTU. */
  if ((selector & SSH_INTERCEPTOR_ROUTE_KEY_FLAG_TRANSFORM_APPLIED)
      && SSH_SKB_DST(skbp)->child == NULL)
    {
      if (interceptor_route_create_child_dst(SSH_SKB_DST(skbp), FALSE) == NULL)
        SSH_DEBUG(SSH_D_FAIL, ("Could not create child dst_entry for dst %p",
                               SSH_SKB_DST(skbp)));
    }
#endif /* LINUX_FRAGMENTATION_AFTER_NF_POST_ROUTING */
#endif /* SSH_IPSEC_IP_ONLY_INTERCEPTOR */

  /* Release the routing table entry ; otherwise a memory leak occurs
     in the route entry table. */
  SSH_SKB_DST_DROP(skbp);

  dev_kfree_skb(skbp);

  /* Assert that ifnum fits into the SshInterceptorIfnum data type. */
  SSH_LINUX_ASSERT_IFNUM(result->ifnum);

  /* Check that ifnum does not collide with SSH_INTERCEPTOR_INVALID_IFNUM. */
  if (result->ifnum == SSH_INTERCEPTOR_INVALID_IFNUM)
    goto fail;

  /* Accept only unicast, broadcast, anycast, multicast and local routes. */
  if (rt_type == RTN_UNICAST
      || rt_type == RTN_BROADCAST
      || rt_type == RTN_ANYCAST
      || rt_type == RTN_MULTICAST
      || rt_type == RTN_LOCAL)
    {
      ssh_interceptor_release_netdev(dev);
      SSH_LINUX_ASSERT_VALID_IFNUM(result->ifnum);
      return TRUE;
    }

  /* Fail route lookup for other route types. */
 fail:
  if (dev)
    ssh_interceptor_release_netdev(dev);

  SSH_DEBUG(SSH_D_FAIL,
            ("Route lookup for %s failed with code %d",
             ssh_ipaddr_print(&key->dst, dst_buf, sizeof(dst_buf)), rval));

  return FALSE;
}

#ifdef SSH_LINUX_INTERCEPTOR_IPV6

/* Perform route lookup using linux ip6_route_output.

   The route lookup will use the following selectors:
   dst, src, outbound ifnum.

   The following selectors are ignored:
   ipv6 priority, flowlabel, ip protocol, dst port, src port,
   icmp type, icmp code, ipsec spi, and fwmark. */
Boolean
ssh_interceptor_route_output_ipv6(SshInterceptor interceptor,
                                  SshInterceptorRouteKey key,
                                  SshUInt32 selector,
                                  SshInterceptorRouteResult result)
{
  struct flowi rt_key;
  struct dst_entry *dst;
  struct rt6_info *rt;
  u32 rt6i_flags;
  int error = 0;
  struct neighbour *neigh = NULL;
#ifdef DEBUG_LIGHT
  unsigned char dst_buf[SSH_IP_ADDR_STRING_SIZE];
  unsigned char src_buf[SSH_IP_ADDR_STRING_SIZE];
#endif /* DEBUG_LIGHT */

  memset(&rt_key, 0, sizeof(rt_key));

  SSH_IP6_ENCODE(&key->dst, rt_key.fl6_dst.s6_addr);

  if (selector & SSH_INTERCEPTOR_ROUTE_KEY_SRC)
      SSH_IP6_ENCODE(&key->src, rt_key.fl6_src.s6_addr);

  if (selector & SSH_INTERCEPTOR_ROUTE_KEY_OUT_IFNUM)
    {
      SSH_LINUX_ASSERT_VALID_IFNUM(key->ifnum);
      rt_key.oif = key->ifnum;
    }

  SSH_DEBUG(SSH_D_LOWOK,
            ("Route lookup: dst %s src %s ifnum %d",
             ssh_ipaddr_print(&key->dst, dst_buf, sizeof(dst_buf)),
             ((selector & SSH_INTERCEPTOR_ROUTE_KEY_SRC) ?
              ssh_ipaddr_print(&key->src, src_buf, sizeof(src_buf)) : NULL),
             (int) ((selector & SSH_INTERCEPTOR_ROUTE_KEY_OUT_IFNUM) ?
                    key->ifnum : -1)));

  dst = interceptor_ip6_route_output(&rt_key);
  if (dst == NULL)
    {
      goto fail;
    }
  else if (dst->error != 0)
    {
      error = dst->error;
      /* Release dst_entry */
      dst_release(dst);
      goto fail;
    }

  rt = (struct rt6_info *) dst;

  /* Get the gateway, mtu and ifnum */

  /* For an example of retrieving routing information for IPv6
     within Linux kernel (2.4.19) see inet6_rtm_getroute()
     in /usr/src/linux/net/ipv6/route.c */

#ifdef LINUX_DST_HAS_NEIGHBOUR
  SSH_DST_NEIGHBOUR_READ_LOCK();
  neigh = SSH_DST_GET_NEIGHBOUR(dst);
  if (neigh != NULL)
    SSH_IP6_DECODE(result->gw, &neigh->primary_key);
  else
    SSH_IP6_DECODE(result->gw, &rt_key.fl6_dst.s6_addr);
  SSH_DST_NEIGHBOUR_READ_UNLOCK();
#else /* LINUX_DST_HAS_NEIGHBOUR */
#ifdef LINUX_RT6_INFO_HAS_NEIGHBOUR
  neigh = rt->n;
#endif /* LINUX_RT6_INFO_HAS_NEIGHBOUR */
  if (neigh != NULL)
    SSH_IP6_DECODE(result->gw, &neigh->primary_key);
  else if (rt->rt6i_flags & RTF_GATEWAY)
    SSH_IP6_DECODE(result->gw, &rt->rt6i_gateway);
  else
    {
#ifdef LINUX_RT_USE_DST_AS_GW
      /* Ok, RT does not have gateway, let's try to send this directly to
        destination. */
      *result->gw = key->dst;
#else /* LINUX_RT_USE_DST_AS_GW */
      goto fail;
#endif /* LINUX_RT_USE_DST_AS_GW */
    }
#endif /* LINUX_DST_HAS_NEIGHBOUR */

  result->mtu = SSH_DST_MTU(dst);

  /* The interface number might not be ok, but that is a problem
     for the recipient of the routing information. */
  result->ifnum = dst->dev->ifindex;

  rt6i_flags = rt->rt6i_flags;

  SSH_DEBUG(SSH_D_LOWOK,
            ("Route result: %s via %s ifnum %d[%s] mtu %d flags 0x%08x[%s%s]",
             dst_buf, ssh_ipaddr_print(result->gw, src_buf, sizeof(src_buf)),
             (int) result->ifnum, (dst->dev ? dst->dev->name : "none"),
             result->mtu, rt6i_flags, ((rt6i_flags & RTF_UP) ? "up " : ""),
             ((rt6i_flags & RTF_REJECT) ? "reject" : "")));


#ifdef SSH_IPSEC_IP_ONLY_INTERCEPTOR
#ifdef LINUX_FRAGMENTATION_AFTER_NF6_POST_ROUTING
  /* Check if need to create a child dst_entry with interface MTU. */
  if (selector & SSH_INTERCEPTOR_ROUTE_KEY_FLAG_TRANSFORM_APPLIED)
    {
      if (dst->child == NULL)
        if (interceptor_route_create_child_dst(dst, TRUE) == NULL)
          SSH_DEBUG(SSH_D_FAIL, ("Could not create child dst_entry for dst %p",
                                 dst));
    }
#endif /* LINUX_FRAGMENTATION_AFTER_NF6_POST_ROUTING */
#endif /* SSH_IPSEC_IP_ONLY_INTERCEPTOR */

  /* Release dst_entry */
  dst_release(dst);

  /* Assert that ifnum fits into the SshInterceptorIfnum data type. */
  SSH_LINUX_ASSERT_IFNUM(result->ifnum);

  /* Check that ifnum does not collide with SSH_INTERCEPTOR_INVALID_IFNUM. */
  if (result->ifnum == SSH_INTERCEPTOR_INVALID_IFNUM)
    goto fail;

  /* Accept only valid routes */
  if ((rt6i_flags & RTF_UP)
      && (rt6i_flags & RTF_REJECT) == 0)
    {
      SSH_LINUX_ASSERT_VALID_IFNUM(result->ifnum);
      return TRUE;
    }

  /* Fail route lookup for reject and unknown routes */
 fail:
  SSH_DEBUG(SSH_D_FAIL, ("Route lookup for %s failed with code %d",
                         dst_buf, error));

  return FALSE;
}
#endif /* SSH_LINUX_INTERCEPTOR_IPV6 */

/* Performs a route lookup for the given destination address.
   This also always calls a callback function. */

void
ssh_interceptor_route(SshInterceptor interceptor,
                      SshInterceptorRouteKey key,
                      SshInterceptorRouteCompletion callback,
                      void *cb_context)
{
  SshInterceptorRouteResultStruct result;
  struct net_device *dev;

  /* It is a fatal error to call ssh_interceptor_route with
     a routing key that does not specify the destination address. */
  SSH_ASSERT(SSH_IP_DEFINED(&key->dst));

  if (SSH_IP_IS4(&key->dst))
    {
      /* Key specifies non-local src address and inbound ifnum
         -> use ssh_interceptor_route_input_ipv4 */
      if ((key->selector & SSH_INTERCEPTOR_ROUTE_KEY_SRC)
          && (key->selector & SSH_INTERCEPTOR_ROUTE_KEY_FLAG_LOCAL_SRC) == 0
          && (key->selector & SSH_INTERCEPTOR_ROUTE_KEY_IN_IFNUM))
        {
          /* Assert that all mandatory selectors are present. */
          SSH_ASSERT(key->selector & SSH_INTERCEPTOR_ROUTE_KEY_SRC);
          SSH_ASSERT(SSH_IP_IS4(&key->src));
          SSH_ASSERT(key->selector & SSH_INTERCEPTOR_ROUTE_KEY_IN_IFNUM);

          if (!ssh_interceptor_route_input_ipv4(interceptor, key,
                                                key->selector, &result))
            goto fail;
        }

      /* Otherwise use ssh_interceptor_route_output_ipv4 */
      else
        {
          SshUInt32 selector = key->selector;

          /* Assert that all mandatory selectors are present. */
          SSH_ASSERT((key->selector & SSH_INTERCEPTOR_ROUTE_KEY_SRC) == 0
                     || SSH_IP_IS4(&key->src));

          /* Key specifies non-local src address.
             Linux ip_route_output_key will fail such route lookups,
             so we must clear the src address selector and do the
             route lookup as if src was one of local addresses.
             For example, locally generated TCP RST packets are
             such packets. */
          if ((key->selector & SSH_INTERCEPTOR_ROUTE_KEY_FLAG_LOCAL_SRC) == 0)
            selector &= ~SSH_INTERCEPTOR_ROUTE_KEY_SRC;

          if (!ssh_interceptor_route_output_ipv4(interceptor, key, selector,
                                                 &result))
            goto fail;
        }

      dev = ssh_interceptor_ifnum_to_netdev(interceptor, result.ifnum);
      if (dev == NULL)
        {
          SSH_DEBUG(SSH_D_FAIL, ("Route lookup resulted into unknown ifnum %d",
                                 (int) result.ifnum));
          goto fail;
        }
      ssh_interceptor_release_netdev(dev);

      (*callback)(TRUE, result.gw, result.ifnum, 0,
                  result.mtu, cb_context);
      return;
    }

#ifdef SSH_LINUX_INTERCEPTOR_IPV6
  if (SSH_IP_IS6(&key->dst))
    {
      /* Assert that all mandatory selectors are present. */
      SSH_ASSERT((key->selector & SSH_INTERCEPTOR_ROUTE_KEY_SRC) == 0
                 || SSH_IP_IS6(&key->src));

      /* Always use ip6_route_output for IPv6 */
      if (!ssh_interceptor_route_output_ipv6(interceptor, key, key->selector,
                                             &result))
        goto fail;

      dev = ssh_interceptor_ifnum_to_netdev(interceptor, result.ifnum);
      if (dev == NULL)
        {
          SSH_DEBUG(SSH_D_FAIL, ("Route lookup resulted into unknown ifnum %d",
                                 (int) result.ifnum));
          goto fail;
        }
      ssh_interceptor_release_netdev(dev);

      (*callback)(TRUE, result.gw, result.ifnum,  0,
                  result.mtu, cb_context);
      return;
    }
#endif /* SSH_LINUX_INTERCEPTOR_IPV6 */

  /* Fallback to error */
 fail:
  SSH_DEBUG(SSH_D_FAIL, ("Route lookup failed, unknown dst address type"));
  (*callback) (FALSE, NULL, 0, -1, 0, cb_context);
}


/**************************** Rerouting of Packets **************************/


/* Route IPv4 packet 'skbp', using the route key selectors in
   'route_selector' and the interface number 'ifnum_in'. */
Boolean
ssh_interceptor_reroute_skb_ipv4(SshInterceptor interceptor,
                                 struct sk_buff *skbp,
                                 SshUInt32 route_selector,
                                 SshUInt32 ifnum_in)
{
  struct iphdr *iph;
  int rval = 0;
  int dev_taken = 0;

  /* Recalculate the route info as the engine might have touched the
     destination address. This can happen for example if we are in
     tunnel mode. */

  iph = (struct iphdr *) SSH_SKB_GET_NETHDR(skbp);
  if (iph == NULL)
    {
      SSH_DEBUG(SSH_D_ERROR, ("Could not access IP header"));
      return FALSE;
    }

  /* Release old dst_entry */
  if (SSH_SKB_DST(skbp))
    SSH_SKB_DST_DROP(skbp);

  if ((route_selector & SSH_INTERCEPTOR_ROUTE_KEY_SRC)
      && (route_selector & SSH_INTERCEPTOR_ROUTE_KEY_FLAG_LOCAL_SRC) == 0
      && (route_selector & SSH_INTERCEPTOR_ROUTE_KEY_IN_IFNUM))
    {
      u32 saddr = 0;
      u8 ipproto = 0;
      u8 tos = 0;
#if (SSH_INTERCEPTOR_NUM_EXTENSION_SELECTORS > 0)
#ifdef SSH_LINUX_FWMARK_EXTENSION_SELECTOR
      u32 fwmark = 0;
#endif /* SSH_LINUX_FWMARK_EXTENSION_SELECTOR */
#endif /* (SSH_INTERCEPTOR_NUM_EXTENSION_SELECTORS > 0) */
      struct net_device *dev;

      SSH_ASSERT(skbp->protocol == __constant_htons(ETH_P_IP));

      if (route_selector & SSH_INTERCEPTOR_ROUTE_KEY_SRC)
        saddr = iph->saddr;

      /* Map 'ifnum_in' to a net_device. */
      SSH_LINUX_ASSERT_VALID_IFNUM(ifnum_in);
      dev = ssh_interceptor_ifnum_to_netdev(interceptor, ifnum_in);

      /* Clear the IP protocol, if selector does not define it. */
      if ((route_selector & SSH_INTERCEPTOR_ROUTE_KEY_IPPROTO) == 0)
        {
          ipproto = iph->protocol;
          iph->protocol = 0;
        }

      if (route_selector & SSH_INTERCEPTOR_ROUTE_KEY_IP4_TOS)
        tos = RT_TOS(iph->tos);

#if (SSH_INTERCEPTOR_NUM_EXTENSION_SELECTORS > 0)
#ifdef SSH_LINUX_FWMARK_EXTENSION_SELECTOR
      /* Clear the nfmark, if selector does not define it. */
      if ((route_selector & SSH_INTERCEPTOR_ROUTE_KEY_EXTENSION) == 0)
        {
          fwmark = SSH_SKB_MARK(skbp);
          SSH_SKB_MARK(skbp) = 0;
        }
#endif /* SSH_LINUX_FWMARK_EXTENSION_SELECTOR */
#endif /* (SSH_INTERCEPTOR_NUM_EXTENSION_SELECTORS > 0) */

      /* Call ip_route_input */
      if (
#ifdef LINUX_USE_SKB_DST_NOREF
          ip_route_input_noref(skbp, iph->daddr, saddr, tos, dev)
#else /* LINUX_USE_SKB_DST_NOREF */
          ip_route_input(skbp, iph->daddr, saddr, tos, dev)
#endif /* LINUX_USE_SKB_DST_NOREF */
          < 0)
        {
          SSH_DEBUG(SSH_D_FAIL,
                    ("ip_route_input failed. (0x%08x -> 0x%08x)",
                     iph->saddr, iph->daddr));

          SSH_DEBUG(SSH_D_NICETOKNOW,
                    ("dst 0x%08x src 0x%08x iif %d[%s] proto %d tos 0x%02x "
                     "fwmark 0x%x",
                     iph->daddr, saddr, (dev ? dev->ifindex : -1),
                     (dev ? dev->name : "none"), iph->protocol, tos,
                     SSH_SKB_MARK(skbp)));

          /* Release netdev reference */
          if (dev)
            ssh_interceptor_release_netdev(dev);

          if (dev_taken != 0)
            skbp->dev = NULL;

          /* Note, skb modifications are not un-done as the caller frees the
             skb. If this is changed then the modifications should be un-done
             here before returning. */

          return FALSE;
        }

      if (dev_taken != 0)
        skbp->dev = NULL;

      /* Write original IP protocol back to skb */
      if (ipproto)
        iph->protocol = ipproto;

#if (SSH_INTERCEPTOR_NUM_EXTENSION_SELECTORS > 0)
#ifdef SSH_LINUX_FWMARK_EXTENSION_SELECTOR
      /* Write original fwmark back to skb */
      if (fwmark)
        SSH_SKB_MARK(skbp) = fwmark;
#endif /* SSH_LINUX_FWMARK_EXTENSION_SELECTOR */
#endif /* (SSH_INTERCEPTOR_NUM_EXTENSION_SELECTORS > 0) */

      /* Release netdev reference */
      if (dev)
        ssh_interceptor_release_netdev(dev);
    }

  else
    {
      struct rtable *rt = NULL;
      struct flowi rt_key;

      if ((route_selector & SSH_INTERCEPTOR_ROUTE_KEY_FLAG_LOCAL_SRC) == 0)
        route_selector &= ~SSH_INTERCEPTOR_ROUTE_KEY_SRC;

      memset(&rt_key, 0, sizeof(rt_key));

      rt_key.fl4_dst = iph->daddr;
      if (route_selector & SSH_INTERCEPTOR_ROUTE_KEY_SRC)
        rt_key.fl4_src = iph->saddr;
      if (route_selector & SSH_INTERCEPTOR_ROUTE_KEY_OUT_IFNUM)
        rt_key.oif = (skbp->dev ? skbp->dev->ifindex : 0);
      if (route_selector & SSH_INTERCEPTOR_ROUTE_KEY_IPPROTO)
        rt_key.proto = iph->protocol;
      if (route_selector & SSH_INTERCEPTOR_ROUTE_KEY_IP4_TOS)
        rt_key.fl4_tos = RT_TOS(iph->tos);
      rt_key.fl4_scope = RT_SCOPE_UNIVERSE;

#if (SSH_INTERCEPTOR_NUM_EXTENSION_SELECTORS > 0)
#ifdef SSH_LINUX_FWMARK_EXTENSION_SELECTOR
      if (route_selector & SSH_INTERCEPTOR_ROUTE_KEY_EXTENSION)
        {
#ifdef LINUX_HAS_SKB_MARK
          rt_key.mark = SSH_SKB_MARK(skbp);
#else /* LINUX_HAS_SKB_MARK */
#ifdef CONFIG_IP_ROUTE_FWMARK
          rt_key.fl4_fwmark = SSH_SKB_MARK(skbp);
#endif /* CONFIG_IP_ROUTE_FWMARK */
#endif /* LINUX_HAS_SKB_MARK */
        }
#endif /* SSH_LINUX_FWMARK_EXTENSION_SELECTOR */
#endif /* (SSH_INTERCEPTOR_NUM_EXTENSION_SELECTORS > 0) */

      rval = interceptor_ip4_route_output(&rt, &rt_key);
      if (rval < 0)
        {
          SSH_DEBUG(SSH_D_FAIL,
                    ("ip_route_output_key failed (0x%08x -> 0x%08x): %d",
                     iph->saddr, iph->daddr, rval));

          SSH_DEBUG(SSH_D_NICETOKNOW,
                    ("dst 0x%08x src 0x%08x oif %d[%s] proto %d tos 0x%02x"
                     "fwmark 0x%x",
                     iph->daddr,
                     ((route_selector & SSH_INTERCEPTOR_ROUTE_KEY_SRC) ?
                      iph->saddr : 0),
                     ((route_selector & SSH_INTERCEPTOR_ROUTE_KEY_OUT_IFNUM) ?
                      rt_key.oif : -1),
                     ((route_selector & SSH_INTERCEPTOR_ROUTE_KEY_OUT_IFNUM) ?
                      (skbp->dev ? skbp->dev->name : "none") : "none"),
                     ((route_selector & SSH_INTERCEPTOR_ROUTE_KEY_IPPROTO) ?
                      iph->protocol : -1),
                     ((route_selector & SSH_INTERCEPTOR_ROUTE_KEY_IP4_TOS) ?
                      iph->tos : 0),
                     ((route_selector & SSH_INTERCEPTOR_ROUTE_KEY_EXTENSION) ?
                      SSH_SKB_MARK(skbp) : 0)));

          return FALSE;
        }

      /* Make a new dst because we just rechecked the route. */
#ifdef LINUX_USE_SKB_DST_NOREF
      skb_dst_set_noref(skbp, &SSH_RT_DST(rt));
#else /* LINUX_USE_SKB_DST_NOREF */
      skb_dst_set(skbp, dst_clone(&SSH_RT_DST(rt)));
#endif /* LINUX_USE_SKB_DST_NOREF */

      /* Release the routing table entry ; otherwise a memory leak occurs
         in the route entry table. */
      ip_rt_put(rt);
    }

  SSH_ASSERT(SSH_SKB_DST(skbp) != NULL);

#ifdef SSH_IPSEC_IP_ONLY_INTERCEPTOR
#ifdef LINUX_FRAGMENTATION_AFTER_NF_POST_ROUTING
  if (route_selector & SSH_INTERCEPTOR_ROUTE_KEY_FLAG_TRANSFORM_APPLIED)
    {
      struct dst_entry *dst;

      /* Check if need to create a child dst_entry with interface MTU. */
      dst = SSH_SKB_DST(skbp);
      if (dst->child == NULL)
        {
          if (interceptor_route_create_child_dst(dst, FALSE) == NULL)
            {
              SSH_DEBUG(SSH_D_ERROR,
                        ("Could not create child dst_entry for dst %p",
                         dst));
              return FALSE;
            }
        }
      if (dst->child->dev != NULL
          && dst_mtu(dst->child) < dst->child->dev->mtu)
        {
          SSH_DEBUG(SSH_D_LOWOK,
                    ("Adjusting child dst_entry mtu from %d to %d",
                     dst_mtu(dst->child), dst->child->dev->mtu));
#ifdef LINUX_HAS_DST_METRICS_ACCESSORS
          dst_metric_set(dst->child, RTAX_MTU, dst->child->dev->mtu);
#else /* LINUX_HAS_DST_METRICS_ACCESSORS */
          dst->child->metrics[RTAX_MTU-1] = dst->child->dev->mtu;
#endif /* LINUX_HAS_DST_METRICS_ACCESSORS */
        }

      /* Pop dst stack and use the child entry with interface MTU
         for sending the packet. */
      dst = dst->child;
      dst_clone(dst);
      SSH_SKB_DST_DROP(skbp);
      skb_dst_set(skbp, dst);
    }
#endif /* LINUX_FRAGMENTATION_AFTER_NF_POST_ROUTING */
#endif /* SSH_IPSEC_IP_ONLY_INTERCEPTOR */

  return TRUE;
}


#ifdef SSH_LINUX_INTERCEPTOR_IPV6

/* Route IPv6 packet 'skbp', using the route key selectors in
   'route_selector' and the interface number 'ifnum_in'. */
Boolean
ssh_interceptor_reroute_skb_ipv6(SshInterceptor interceptor,
                                 struct sk_buff *skbp,
                                 SshUInt32 route_selector,
                                 SshUInt32 ifnum_in)
{
  /* we do not need a socket, only fake flow */
  struct flowi rt_key;
  struct dst_entry *dst;
  struct ipv6hdr *iph6;

  iph6 = (struct ipv6hdr *) SSH_SKB_GET_NETHDR(skbp);
  if (iph6 == NULL)
    {
      SSH_DEBUG(SSH_D_ERROR, ("Could not access IPv6 header"));
      return FALSE;
    }

  memset(&rt_key, 0, sizeof(rt_key));

  rt_key.fl6_dst = iph6->daddr;

  if (route_selector & SSH_INTERCEPTOR_ROUTE_KEY_SRC)
    rt_key.fl6_src = iph6->saddr;

  if (route_selector & SSH_INTERCEPTOR_ROUTE_KEY_OUT_IFNUM)
    {
      rt_key.oif = (skbp->dev ? skbp->dev->ifindex : 0);
      SSH_LINUX_ASSERT_IFNUM(rt_key.oif);
    }

  dst = interceptor_ip6_route_output(&rt_key);
  if (dst == NULL || dst->error != 0)
    {
      if (dst != NULL)
        dst_release(dst);

      SSH_DEBUG(SSH_D_FAIL,
                ("ip6_route_output failed."));

      SSH_DEBUG_HEXDUMP(SSH_D_NICETOKNOW,
                        ("dst "),
                        (unsigned char *) &iph6->daddr, sizeof(iph6->daddr));
      SSH_DEBUG_HEXDUMP(SSH_D_NICETOKNOW,
                        ("src "),
                        (unsigned char *) &iph6->saddr, sizeof(iph6->saddr));
      SSH_DEBUG(SSH_D_NICETOKNOW,
                ("oif %d[%s]",
                 (skbp->dev ? skbp->dev->ifindex : -1),
                 (skbp->dev ? skbp->dev->name : "none")));
      return FALSE;
    }
  if (SSH_SKB_DST(skbp))
    SSH_SKB_DST_DROP(skbp);

#ifdef SSH_IPSEC_IP_ONLY_INTERCEPTOR
#ifdef LINUX_FRAGMENTATION_AFTER_NF6_POST_ROUTING
  if (route_selector & SSH_INTERCEPTOR_ROUTE_KEY_FLAG_TRANSFORM_APPLIED)
    {
      /* Check if need to create a child dst_entry with interface MTU. */
      if (dst->child == NULL)
        {
          if (interceptor_route_create_child_dst(dst, TRUE) == NULL)
            {
              SSH_DEBUG(SSH_D_ERROR,
                        ("Could not create child dst_entry for dst %p",
                         dst));

              dst_release(dst);
              return FALSE;
            }
        }
      if (dst->child->dev != NULL
          && dst_mtu(dst->child) < dst->child->dev->mtu)
        {
          SSH_DEBUG(SSH_D_LOWOK,
                    ("Adjusting child dst_entry mtu from %d to %d",
                     dst_mtu(dst->child), dst->child->dev->mtu));
#ifdef LINUX_HAS_DST_METRICS_ACCESSORS
          dst_metric_set(dst->child, RTAX_MTU, dst->child->dev->mtu);
#else /* LINUX_HAS_DST_METRICS_ACCESSORS */
          dst->child->metrics[RTAX_MTU-1] = dst->child->dev->mtu;
#endif /* LINUX_HAS_DST_METRICS_ACCESSORS */
        }

      /* Pop dst stack and use the child entry with interface MTU
         for sending the packet. */
      skb_dst_set(skbp, dst_clone(dst->child));
      dst_release(dst);
    }
  else
#endif /* LINUX_FRAGMENTATION_AFTER_NF6_POST_ROUTING */
#endif /* SSH_IPSEC_IP_ONLY_INTERCEPTOR */
    {
      /* No need to clone the dst as ip6_route_output has already taken
         one for us. */
      SSH_SKB_DST_SET(skbp, dst);
    }

  return TRUE;
}
#endif /* SSH_LINUX_INTERCEPTOR_IPV6 */
