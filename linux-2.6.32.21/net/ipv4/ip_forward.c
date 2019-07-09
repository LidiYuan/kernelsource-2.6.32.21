/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		The IP forwarding functionality.
 *
 * Authors:	see ip.c
 *
 * Fixes:
 *		Many		:	Split from ip.c , see ip_input.c for
 *					history.
 *		Dave Gregorich	:	NULL ip_rt_put fix for multicast
 *					routing.
 *		Jos Vos		:	Add call_out_firewall before sending,
 *					use output device for accounting.
 *		Jos Vos		:	Call forward firewall after routing
 *					(always use output device).
 *		Mike McLagan	:	Routing by source
 */

#include <linux/types.h>
#include <linux/mm.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <linux/icmp.h>
#include <linux/netdevice.h>
#include <net/sock.h>
#include <net/ip.h>
#include <net/tcp.h>
#include <net/udp.h>
#include <net/icmp.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/netfilter_ipv4.h>
#include <net/checksum.h>
#include <linux/route.h>
#include <net/route.h>
#include <net/xfrm.h>

static int ip_forward_finish(struct sk_buff *skb)
{
	struct ip_options * opt	= &(IPCB(skb)->opt);

	IP_INC_STATS_BH(dev_net(skb_dst(skb)->dev), IPSTATS_MIB_OUTFORWDATAGRAMS);

    //��ipѡ����д���
	if (unlikely(opt->optlen))
		ip_forward_options(skb);

	return dst_output(skb);
}

//����ת��������������
int ip_forward(struct sk_buff *skb)
{
	struct iphdr *iph;	/* Our header */
	struct rtable *rt;	/* Route we use */
	struct ip_options * opt	= &(IPCB(skb)->opt);

	//��LRO(Large Received Offload)���ݰ�����,LRO��һ�������Ż����� ��������ݰ��ϲ��ɴ���SKB Ȼ�󴫸��ϲ�,
	//����CPU����,�������ܱ�ת�� ���Ĵ�С�����˳��ڵ�mtuֵ,GRO֧��ת�� ��LRO��֧��ת��
	if (skb_warn_if_lro(skb))
		goto drop;

	 //����ipSec���Լ��
	if (!xfrm4_policy_check(NULL, XFRM_POLICY_FWD, skb))
		goto drop;

    //���������router_alert(·�ɾ�����Ϣ)ѡ�� ��������ip_call_ra_chain���������ݰ�
    //��ԭʼ�׽��ֵ���setsockopts()ʹ��IP_ROUTER_ALERT ���׽��ֱ����뵽ip_ra_chain��,ip_call_ra_chain
    //�Ὣ���ݰ��������е�ԭʼ�׽���
	if (IPCB(skb)->opt.router_alert && ip_call_ra_chain(skb))
		return NET_RX_SUCCESS;

	//���ݰ�MAC��ַ���Ͳ��Ƿ������ص�����
	//pkt_type����eth_type_trans��ȷ����
	if (skb->pkt_type != PACKET_HOST)
		goto drop;

	//����������L3�� ���ص���L4���У�鹤�� ��CHECKSUM_NONEָ����ǰУ�������
	skb_forward_csum(skb);

	/*
	 *	According to the RFC, we must first decrease the TTL field. If
	 *	that reaches zero, we must reply an ICMP control message telling
	 *	that the packet's lifetime expired.
	 */
	 //��TTL����Ϊ0�� ����Ӧ�ý����ݱ�����
	if (ip_hdr(skb)->ttl <= 1)
		goto too_many_hops;

	if (!xfrm4_route_forward(skb))
		goto drop;

	//��skb->_skb_dst���·����
	rt = skb_rtable(skb);

	//�������ϸ�·�� ����Ŀ�ĵ�ַ�����ܵ�ַ��һ��
	if (opt->is_strictroute && rt->rt_dst != rt->rt_gateway)
		goto sr_failed;

	if (unlikely(skb->len > dst_mtu(&rt->u.dst) && //�����Ĵ�С �Ƿ��������豸��MTUֵ 
		!skb_is_gso(skb) && //����gso
		(ip_hdr(skb)->frag_off & htons(IP_DF))) &&//���ipͷ��DF��׼��Ƭ�Ƿ����� 
		!skb->local_df  //���ܽ��б�����Ƭ
	   ) 
    {
        //����ͳ����Ϣ
		IP_INC_STATS(dev_net(rt->u.dst.dev), IPSTATS_MIB_FRAGFAILS);

		//����icmp��Ϣ  Ŀ�Ĳ��ɴ�--->��Ҫ���з�Ƭ
		icmp_send(skb, ICMP_DEST_UNREACH, ICMP_FRAG_NEEDED,
			      htonl(dst_mtu(&rt->u.dst)));
		goto drop;
	}

	/* We are about to mangle packet. Copy it! */
	//���Ǽ����޸İ� �ȸ�����
	if (skb_cow(skb, LL_RESERVED_SPACE(rt->u.dst.dev)+rt->u.dst.header_len))
		goto drop;  

	//���ipͷ
	iph = ip_hdr(skb);

	/* Decrease ttl after skb cow done */
    //��Сipͷ�е�ttl
	ip_decrease_ttl(iph);

	/*
	 *	We now generate an ICMP HOST REDIRECT giving the route
	 *	we calculated.
	 */
	//����һ��icmp�ض�����Ϣ
	//����Ƿ�������RTCF_DOREDIRECT��־  ��__mkroute_input��������
	if (rt->rt_flags&RTCF_DOREDIRECT && !opt->srr && !skb_sec_path(skb))
		ip_rt_send_redirect(skb);

	//��ip_queue_xmit���������ȼ�Ϊ�׽��ֵ����ȼ� ��ת��ʱ��û���׽������ȼ�
	//������ip_tos2prio ���������ȼ�
	skb->priority = rt_tos2priority(iph->tos);

    //netfileter���� ִ����� ������ִ�� ��ִ��ip_forward_finish
	return NF_HOOK(PF_INET, NF_INET_FORWARD, skb, skb->dev, rt->u.dst.dev,
		       ip_forward_finish);

sr_failed:
	/*
	 *	Strict routing permits no gatewaying
	 */
     //����icmp��Ϣ Ŀ�Ĳ��ɴ�--Դ·��ʧ�� ��Ϣ
	 icmp_send(skb, ICMP_DEST_UNREACH, ICMP_SR_FAILED, 0);
	 goto drop;

too_many_hops:
	/* Tell the sender its packet died... */

	//����SNMP������InHdrErrors��ֵ
	IP_INC_STATS_BH(dev_net(skb_dst(skb)->dev), IPSTATS_MIB_INHDRERRORS);

	//���� ��ʱ---TTL��ʱ icmp��Ϣ
	icmp_send(skb, ICMP_TIME_EXCEEDED, ICMP_EXC_TTL, 0);
drop:
	kfree_skb(skb);
	return NET_RX_DROP;
}
