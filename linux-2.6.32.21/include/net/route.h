/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET  is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Definitions for the IP router.
 *
 * Version:	@(#)route.h	1.0.4	05/27/93
 *
 * Authors:	Ross Biro
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 * Fixes:
 *		Alan Cox	:	Reformatted. Added ip_rt_local()
 *		Alan Cox	:	Support for TCP parameters.
 *		Alexey Kuznetsov:	Major changes for new routing code.
 *		Mike McLagan    :	Routing by source
 *		Robert Olsson   :	Added rt_cache statistics
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#ifndef _ROUTE_H
#define _ROUTE_H

#include <net/dst.h>
#include <net/inetpeer.h>
#include <net/flow.h>
#include <net/inet_sock.h>
#include <linux/in_route.h>
#include <linux/rtnetlink.h>
#include <linux/route.h>
#include <linux/ip.h>
#include <linux/cache.h>
#include <linux/security.h>

#ifndef __KERNEL__
#warning This file is not supposed to be used outside of kernel.
#endif

#define RTO_ONLINK	0x01

#define RTO_CONN	0
/* RTO_CONN is not used (being alias for 0), but preserved not to break
 * some modules referring to it. */

#define RT_CONN_FLAGS(sk)   (RT_TOS(inet_sk(sk)->tos) | sock_flag(sk, SOCK_LOCALROUTE))

struct fib_nh;
struct inet_peer;

//����ʹ�ù��Ĳ�ѯ·�ɵ�ַ�Ļ����ַ���ݱ���·�ɻ���
//·�ɻ���
struct rtable
{
	union
	{
	    //dst_entry���ְ����ھ�����ص���Ϣ
		struct dst_entry	dst; 
	} u;

	/* Cache lookup keys �ýṹ���д����·�ɻ���ƥ������в���*/
	struct flowi		fl;//ע����cache�еĲ�����Ҫ��ͨ��·�ɼ�ֵ���������Ϣ

	struct in_device	*idev; // �豸

	                          //��net->ipv4.rt_genid�� ��·�ɱ�ˢ�µ�ʱ������ipv4.rt_genid�е�ֵ, �����е�rt_genid��ipv4.rt_genid�е�ֵ����� ���ʾ·�ɱ������
	int			    rt_genid; // ·��id  ���������ռ䣨ip4���Ķ�̬·�ɱ�����ID�������Ϊ�汾�ţ���ʱ������壩���ǵ���̬·�ɱ��ʼ������ʱ���ض�����㷨���ɵ�
	unsigned		rt_flags;// ��ʶ RTCF_NOTIFY��RTCF_LOCAL��RTCF_BROADCAST��RTCF_MULTICAST��RTCF_REDIRECTED
	__u16			rt_type;// ·������ ���� �鲥�򱾵�·��RTN_UNSPEC��RTN_UNICAST��RTN_LOCAL��RTN_BROADCAST��RTN_MULTICAST

	__be32			rt_dst;	/* Path destination	*/// Ŀ�ĵ�ַ
	__be32			rt_src;	/* Path source		*/ // Դ��ַ 
	int			    rt_iif; // ��˿�

	/* Info on neighbour */
	__be32			rt_gateway;//�й��ھӵ���Ϣ ������һ����ַ

	/* Miscellaneous cached information */
	__be32			rt_spec_dst; /* RFC1122 specific destination */

	///*�洢ip peer��ص���Ϣ*/
	struct inet_peer	*peer; /* long-living peer info */
};

struct ip_rt_acct
{
	__u32 	o_bytes;
	__u32 	o_packets;
	__u32 	i_bytes;
	__u32 	i_packets;
};

struct rt_cache_stat 
{
        unsigned int in_hit;
        unsigned int in_slow_tot;
        unsigned int in_slow_mc;
        unsigned int in_no_route;
        unsigned int in_brd;
        unsigned int in_martian_dst;
        unsigned int in_martian_src;
        unsigned int out_hit;
        unsigned int out_slow_tot;
        unsigned int out_slow_mc;
        unsigned int gc_total;
        unsigned int gc_ignored;
        unsigned int gc_goal_miss;
        unsigned int gc_dst_overflow;
        unsigned int in_hlist_search;
        unsigned int out_hlist_search;
};

extern struct ip_rt_acct *ip_rt_acct;

struct in_device;
extern int		ip_rt_init(void);
extern void		ip_rt_redirect(__be32 old_gw, __be32 dst, __be32 new_gw,
				       __be32 src, struct net_device *dev);
extern void		rt_cache_flush(struct net *net, int how);
extern int		__ip_route_output_key(struct net *, struct rtable **, const struct flowi *flp);
extern int		ip_route_output_key(struct net *, struct rtable **, struct flowi *flp);
extern int		ip_route_output_flow(struct net *, struct rtable **rp, struct flowi *flp, struct sock *sk, int flags);
extern int		ip_route_input(struct sk_buff*, __be32 dst, __be32 src, u8 tos, struct net_device *devin);
extern unsigned short	ip_rt_frag_needed(struct net *net, struct iphdr *iph, unsigned short new_mtu, struct net_device *dev);
extern void		ip_rt_send_redirect(struct sk_buff *skb);

extern unsigned		inet_addr_type(struct net *net, __be32 addr);
extern unsigned		inet_dev_addr_type(struct net *net, const struct net_device *dev, __be32 addr);
extern void		ip_rt_multicast_event(struct in_device *);
extern int		ip_rt_ioctl(struct net *, unsigned int cmd, void __user *arg);
extern void		ip_rt_get_source(u8 *src, struct rtable *rt);
extern int		ip_rt_dump(struct sk_buff *skb,  struct netlink_callback *cb);

struct in_ifaddr;
extern void fib_add_ifaddr(struct in_ifaddr *);

static inline void ip_rt_put(struct rtable * rt)
{
	if (rt)
		dst_release(&rt->u.dst);
}

//����IPTOS_TOS_MASK(0x1E) ��3�ķ�����һ��λ�룬ʵ���Ͼ��������λ��0����˼
//0001 1110  & 1111 1100 = 0001 1100 = 0x1c =IPTOS_RT_MASK
#define IPTOS_RT_MASK	(IPTOS_TOS_MASK & ~3)

extern const __u8 ip_tos2prio[16];

static inline char rt_tos2priority(u8 tos)
{
	return ip_tos2prio[IPTOS_TOS(tos)>>1];
}

static inline int ip_route_connect(struct rtable **rp, //���·����
	                                         __be32 dst,//Զ�˵�ַ
                                             __be32 src, //���ص�ַ
                                             u32 tos,  //����
                                             int oif, //����ӿ�
                                             u8 protocol,//Э������
                                             __be16 sport, //Դ�˿�
                                             __be16 dport, //Ŀ�Ķ˿�
                                             struct sock *sk,//sock
                                             int flags)//��־
{
	struct flowi fl = { .oif = oif, //����豸�ӿ�
			    .mark = sk->sk_mark,
			    .nl_u = { .ip4_u = { .daddr = dst,//Ŀ�ĵ�ַ
						 .saddr = src, //Դ��ַ
						 .tos   = tos } }, //һ�����
			    .proto = protocol,//Э��
			    .uli_u = { .ports =
				       { .sport = sport,  //�Զ�ѡ��ĵ�һ��Դ�˿�
					 .dport = dport } } }; //Ŀ�Ķ˿�

	int err;
	struct net *net = sock_net(sk);

	if (inet_sk(sk)->transparent)
		fl.flags |= FLOWI_FLAG_ANYSRC;

	if (!dst || !src) {
		err = __ip_route_output_key(net, rp, &fl);
		if (err)
			return err;
		fl.fl4_dst = (*rp)->rt_dst;
		fl.fl4_src = (*rp)->rt_src;
		ip_rt_put(*rp);
		*rp = NULL;
	}
	security_sk_classify_flow(sk, &fl);
	return ip_route_output_flow(net, rp, &fl, sk, flags);
}

static inline int ip_route_newports(struct rtable **rp, u8 protocol,
				    __be16 sport, __be16 dport, struct sock *sk)
{
	if (sport != (*rp)->fl.fl_ip_sport ||
	    dport != (*rp)->fl.fl_ip_dport) {
		struct flowi fl;

		memcpy(&fl, &(*rp)->fl, sizeof(fl));
		fl.fl_ip_sport = sport;
		fl.fl_ip_dport = dport;
		fl.proto = protocol;
		ip_rt_put(*rp);
		*rp = NULL;
		security_sk_classify_flow(sk, &fl);
		return ip_route_output_flow(sock_net(sk), rp, &fl, sk, 0);
	}
	return 0;
}

extern void rt_bind_peer(struct rtable *rt, int create);

static inline struct inet_peer *rt_get_peer(struct rtable *rt)
{
	if (rt->peer)
		return rt->peer;

	rt_bind_peer(rt, 0);
	return rt->peer;
}

static inline int inet_iif(const struct sk_buff *skb)
{
	return skb_rtable(skb)->rt_iif;
}

#endif	/* _ROUTE_H */
