/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Definitions for the ICMP protocol.
 *
 * Version:	@(#)icmp.h	1.0.3	04/28/93
 *
 * Author:	Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *

 ��ͷ�ļ�����ICMP�ײ��Լ�ICMP�Ĵ�������
 һ��ICMP�������:����(type)�ʹ���(code),�ֱ��ӦICMP�ײ��е�type��code�ֶ�
 
			   -------------------
			   |   ICMP���Ķ�	 | 
			   ------------------- 
	  ----------------------------
	  | IP�ײ� |	IP����		 |
	  ----------------------------
 ---------------------------------------
 | MAC| 	  ����֡			 | FCS |
 ---------------------------------------


 */
#ifndef _LINUX_ICMP_H
#define	_LINUX_ICMP_H

#include <linux/types.h>

//����ֵ����
#define ICMP_ECHOREPLY		0	/* Echo Reply			echo�ظ�Ӧ���� codeΪ0*/
#define ICMP_DEST_UNREACH	3	/* Destination Unreachable	Ŀ�Ĳ��ɴ�*/
#define ICMP_SOURCE_QUENCH	4	/* Source Quench		Դ�˽��� �����ͱ�����Դ��Ϊ�˻������ѹ�����͸�����֪ͨ,ʹ�仺�ⷢ���ٶ�*/
#define ICMP_REDIRECT		5	/* Redirect (change route)�ض�����	*/
#define ICMP_ECHO		    8	/* Echo Request			 echo�ظ����� codeΪ0*/
#define ICMP_TIME_EXCEEDED	11	/* Time Exceeded     ʱ�䳬ʱ*/
#define ICMP_PARAMETERPROB	12	/* Parameter Problem	��������	codeȡֵһ��Ϊ0 */
#define ICMP_TIMESTAMP		13	/* Timestamp Request	ʱ�������	codeȡֵΪ0*/
#define ICMP_TIMESTAMPREPLY	14	/* Timestamp Reply		ʱ���Ӧ���� codeȡֵΪ0*/
#define ICMP_INFO_REQUEST	15	/* Information Request	��Ϣ��ѯ����	*/
#define ICMP_INFO_REPLY		16	/* Information Reply	��Ϣ��ѯӦ����	*/
#define ICMP_ADDRESS		17	/* Address Mask Request		��ַ��������*/
#define ICMP_ADDRESSREPLY	18	/* Address Mask Reply		��ַ��������Ӧ��*/
#define NR_ICMP_TYPES		18


//Ŀ�Ĳ��ɴ����Ĵ���ֵ
/* Codes for UNREACH. */
#define ICMP_NET_UNREACH	0	/* Network Unreachable	���粻�ɴ�	*/
#define ICMP_HOST_UNREACH	1	/* Host Unreachable		�������ɴ�*/
#define ICMP_PROT_UNREACH	2	/* Protocol Unreachable	Э�鲻�ɴ�	*/
#define ICMP_PORT_UNREACH	3	/* Port Unreachable		�˿ڲ��ɴ�*/
#define ICMP_FRAG_NEEDED	4	/* Fragmentation Needed/DF set	��Ҫ��Ƭ��IP�ײ��е�DFλ��1*/
#define ICMP_SR_FAILED		5	/* Source Route failed		Դ·��ʧ��*/
#define ICMP_NET_UNKNOWN	6  //Ŀ������δ֪
#define ICMP_HOST_UNKNOWN	7  //Ŀ������δ֪
#define ICMP_HOST_ISOLATED	8  //Դ����������
#define ICMP_NET_ANO		9  //��Ŀ������ͨ�ű�ǿ�ƽ�ֹ
#define ICMP_HOST_ANO		10 //��Ŀ������ͨ�ű�ǿ�ƽ�ֹ
#define ICMP_NET_UNR_TOS	11 //��������ķ�������TOS ���粻�ɴ�
#define ICMP_HOST_UNR_TOS	12 //��������ķ�������TOS �������ɴ�
#define ICMP_PKT_FILTERED	13	/* Packet filtered ���ڹ��� ͨ�ű�ǿ�ƽ�ֹ*/
#define ICMP_PREC_VIOLATION	14	/* Precedence violation ����ԽȨ*/
#define ICMP_PREC_CUTOFF	15	/* Precedence cut off ����Ȩ��ֹ��Ч*/
#define NR_ICMP_UNREACH		15	/* instead of hardcoding immediate value */

//�ض�λ����ֵ
/* Codes for REDIRECT. */
#define ICMP_REDIR_NET		0	/* Redirect Net		����������ض�����	*/
#define ICMP_REDIR_HOST		1	/* Redirect Host	�����������ض�����	*/
#define ICMP_REDIR_NETTOS	2	/* Redirect Net for TOS	��������ͷ������͵��ض�����*/
#define ICMP_REDIR_HOSTTOS	3	/* Redirect Host for TOS���ڷ������ͺ��������ض�����	*/

//��ʱ����Ĵ���ֵ
/* Codes for TIME_EXCEEDED. */
#define ICMP_EXC_TTL		0	/* TTL count exceeded	������TTL��ʱ	*/
#define ICMP_EXC_FRAGTIME	1	/* Fragment Reass time exceeded	��Ƭ���ʱ�䳬ʱ(ĳ�����ݰ������з�Ƭ�ڹ涨ʱ��δ��ȫ����)
                                �����һ����Ƭ��δ�������ʱ������ʱ,�򲻷���ICMP������*/


struct icmphdr {
  __u8		type;//����
  __u8		code;//����
  __sum16	checksum;//У���

  //�������ͺʹ��� �ɱ�Ĳ���
  union {
	struct {
		__be16	id;
		__be16	sequence;
	} echo;
	__be32	gateway;
	struct {
		__be16	__unused;
		__be16	mtu;
	} frag;
  } un;
};

#ifdef __KERNEL__
#include <linux/skbuff.h>

static inline struct icmphdr *icmp_hdr(const struct sk_buff *skb)
{
	return (struct icmphdr *)skb_transport_header(skb);
}
#endif

/*
 *	constants for (set|get)sockopt
 */

#define ICMP_FILTER			1

struct icmp_filter {
	__u32		data;
};


#endif	/* _LINUX_ICMP_H */
