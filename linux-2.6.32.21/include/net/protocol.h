/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Definitions for the protocol dispatcher.
 *
 * Version:	@(#)protocol.h	1.0.2	05/07/93
 *
 * Author:	Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 *	Changes:
 *		Alan Cox	:	Added a name field and a frag handler
 *					field for later.
 *		Alan Cox	:	Cleaned up, and sorted types.
 *		Pedro Roque	:	inet6 protocols
 */
 
#ifndef _PROTOCOL_H
#define _PROTOCOL_H

#include <linux/in6.h>
#if defined(CONFIG_IPV6) || defined (CONFIG_IPV6_MODULE)
#include <linux/ipv6.h>
#endif

#define MAX_INET_PROTOS	256		/* Must be a power of 2		*/

/*
net_protocol��һ���ǳ���Ҫ�Ľṹ��������Э������֧�ֵĴ����Э���Լ������ı��Ľ������̡�
�˽ṹ�������ʹ����֮ǰ�����������������ݱ��Ĵ�������������ʱ��
����ô˽ṹ�еĴ����Э�����ݱ����մ�����
*/
/* This is used to register protocols. */
struct net_protocol {

    //��������ipЭ���ֶεĲ�ͬ�����ò�ͬ��handler������
    //����Э���ֶ�ΪIPPROTO_ICMP ��handler��Ӧicmp_rcv
	int			(*handler)(struct sk_buff *skb);

	//��ICMPģ���н��յ�����ĺ󣬻��������ģ������ݲ������ԭʼ��IP�ײ���
	//���ö�Ӧ�������쳣������err_handler
	void			(*err_handler)(struct sk_buff *skb, u32 info);

	/*GSO�������豸֧�ִ�����һ������*/
	int			(*gso_send_check)(struct sk_buff *skb);
	
	struct sk_buff	       *(*gso_segment)(struct sk_buff *skb,
					       int features);

	struct sk_buff	      **(*gro_receive)(struct sk_buff **head,
					       struct sk_buff *skb);
	int			          (*gro_complete)(struct sk_buff *skb);
	
	unsigned int		no_policy:1,//��ֵΪ1 ��ʾ����Ҫ����IPsec���Լ��(ip_local_deliver_finish��)
				        netns_ok:1; //��Э���Ƿ�֧�����������ռ�
};

#if defined(CONFIG_IPV6) || defined (CONFIG_IPV6_MODULE)
struct inet6_protocol 
{
	int	(*handler)(struct sk_buff *skb);

	void	(*err_handler)(struct sk_buff *skb,
			       struct inet6_skb_parm *opt,
			       u8 type, u8 code, int offset,
			       __be32 info);

	int	(*gso_send_check)(struct sk_buff *skb);
	struct sk_buff *(*gso_segment)(struct sk_buff *skb,
				       int features);
	struct sk_buff **(*gro_receive)(struct sk_buff **head,
					struct sk_buff *skb);
	int	(*gro_complete)(struct sk_buff *skb);

	unsigned int	flags;	/* INET6_PROTO_xxx */
};

#define INET6_PROTO_NOPOLICY	0x1
#define INET6_PROTO_FINAL	0x2
/* This should be set for any extension header which is compatible with GSO. */
#define INET6_PROTO_GSO_EXTHDR	0x4
#endif

/* This is used to register socket interfaces for IP protocols.  */
//��Э����������;���Э��������������� inetsw_array
struct inet_protosw {
	struct list_head list;

        /* These two fields form the lookup key.  */
    /*
    ��ʶ�׽ӿڵ����ͣ�����InternetЭ���干����������SOCK_STREAM��SOCK_DGRAM��SOCK_RAW��
    ��Ӧ�ó���㴴���׽ӿں���socket()�ĵڶ�������typeȡֵ��Ӧ
	*/
	unsigned short	 type;	   /* This is the 2nd argument to socket(2). */

   /*��ʶЭ�������Ĳ�Э��ţ�InternetЭ�����е�ֵ����IPPROTO_TCP��IPPROTO_UDP
   */ 		
	unsigned short	 protocol; /* This is the L4 protocol number.  */

    //����㺯�����ýӿڲ�����TCPΪtcp_prot;UDPΪudp_prot;ԭʼ�׽ӿ�raw_prot��
	struct proto	 *prot; //�����Э�������


	//inet��������� TCPΪinet_stream_ops;UDPΪinet_dgram_ops;ԭʼ�׽ӿ�inet_sockraw_ops
	const struct proto_ops *ops; //Э���������

    //��������ʱ����Ҫ���鵱ǰ�����׽ӿڵĽ����Ƿ�����������
	int              capability; /* Which (if any) capability do
				      * we need to use this socket
				      * interface?

                                    */
     //���ͻ���ձ���ʱ���Ƿ���ҪУ���
	char             no_check;   /* checksum on rcv/xmit/none? */
	unsigned char	 flags;      /* See INET_PROTOSW_* below.  */
};
#define INET_PROTOSW_REUSE 0x01	     /* Are ports automatically reusable?  �˿��Ƿ�������*/
#define INET_PROTOSW_PERMANENT 0x02  /* Permanent protocols are unremovable. ��ʶ�˿��Ƿ��ܱ����ñ�ʶ��Э�鲻�ܱ��滻��ж��*/
#define INET_PROTOSW_ICSK      0x04  /* Is this an inet_connection_sock? ��ʶ�ǲ������ӵ��׽ӿ�*/

extern const struct net_protocol *inet_protos[MAX_INET_PROTOS];

#if defined(CONFIG_IPV6) || defined (CONFIG_IPV6_MODULE)
extern const struct inet6_protocol *inet6_protos[MAX_INET_PROTOS];
#endif

extern int	inet_add_protocol(const struct net_protocol *prot, unsigned char num);
extern int	inet_del_protocol(const struct net_protocol *prot, unsigned char num);
extern void	inet_register_protosw(struct inet_protosw *p);
extern void	inet_unregister_protosw(struct inet_protosw *p);

#if defined(CONFIG_IPV6) || defined (CONFIG_IPV6_MODULE)
extern int	inet6_add_protocol(const struct inet6_protocol *prot, unsigned char num);
extern int	inet6_del_protocol(const struct inet6_protocol *prot, unsigned char num);
extern int	inet6_register_protosw(struct inet_protosw *p);
extern void	inet6_unregister_protosw(struct inet_protosw *p);
#endif

#endif	/* _PROTOCOL_H */
