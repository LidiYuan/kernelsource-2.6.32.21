/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Definitions for the IP protocol.
 *
 * Version:	@(#)ip.h	1.0.2	04/28/93
 *
 * Authors:	Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#ifndef _LINUX_IP_H
#define _LINUX_IP_H
#include <linux/types.h>
#include <asm/byteorder.h>

#define IPTOS_TOS_MASK		0x1E  //0001 1110
#define IPTOS_TOS(tos)		((tos)&IPTOS_TOS_MASK)
#define	IPTOS_LOWDELAY		0x10
#define	IPTOS_THROUGHPUT	0x08
#define	IPTOS_RELIABILITY	0x04
#define	IPTOS_MINCOST		0x02

#define IPTOS_PREC_MASK		0xE0
#define IPTOS_PREC(tos)		((tos)&IPTOS_PREC_MASK)
#define IPTOS_PREC_NETCONTROL           0xe0
#define IPTOS_PREC_INTERNETCONTROL      0xc0
#define IPTOS_PREC_CRITIC_ECP           0xa0
#define IPTOS_PREC_FLASHOVERRIDE        0x80
#define IPTOS_PREC_FLASH                0x60
#define IPTOS_PREC_IMMEDIATE            0x40
#define IPTOS_PREC_PRIORITY             0x20
#define IPTOS_PREC_ROUTINE              0x00


/*
0    1        3            8                               31
------------------------------------------------------------
|����|ѡ�����|  ѡ���    |                               | 
|��־|        |            |                           ~~  |
------------------------------------------------------------
���Ʊ�־:
   0���ڵ�һ����Ƭ�и���,  1���Ƶ����з�Ƭ,ͨ��IPOPT_COPIED()�����ȡ��ֵ
ѡ�����: ͨ��IPOPT_CLASS()�����ȡ��ֵ
   00:���ݱ�����  IPOPT_CONTROL
   01:������������IPOPT_RESERVED1��
   10:���Բ���    IPOPT_MEASUREMENT 
   11:������������IPOPT_RESERVED2�� ��
ѡ���:ͨ��IPOPT_NUMBER()�����ȡ��ֵ
   00000:0  ѡ�����                  IPOPT_END
   00001:1  �޲���                    IPOPT_NOOP
   00010:2  ��ȫ                      IPOPT_SEC
   00011:3  ���ϸ��Դ·��            IPOPT_LSRR
   00100:4  ʱ���                    IPOPT_TIMESTAMP
   00110:6  ����InternetЭ�鰲ȫѡ��  IPOPT_CIPSO        
   00111:7  ��¼·��                  IPOPT_RR 
   01000:8  ��ID                      IPOPT_SID
   01001:9  �ϸ�Դ·��                IPOPT_SSRR
   10100:20 ·��������                IPOPT_RA
*/
/* IP options */
#define IPOPT_COPY		0x80 //���Ƶ����з�Ƭ
#define IPOPT_CLASS_MASK	0x60
#define IPOPT_NUMBER_MASK	0x1f

#define	IPOPT_COPIED(o)		((o)&IPOPT_COPY) //ȡ��copy�ֶ� ��8bit�еĵ�һλ
#define	IPOPT_CLASS(o)		((o)&IPOPT_CLASS_MASK)//ȡ���ڶ�λ�͵���λ(���)
#define	IPOPT_NUMBER(o)		((o)&IPOPT_NUMBER_MASK)//ȡ������λ���ڰ�λ���

#define	IPOPT_CONTROL		0x00 //0 00 00000���ݱ�����
#define	IPOPT_RESERVED1		0x20 //0 01 00000�����ֶ�
#define	IPOPT_MEASUREMENT	0x40// 0 10 00000 ���Բ���ʹ��
#define	IPOPT_RESERVED2		0x60// 0 11 00000 �����ֶ�


/*ѡ�����Ҳ��1�ֽ�ѡ��,����ѡ���ֶν���ʱ�����,ֻ���������һ��ѡ��,����ֻ����һ��*/
#define IPOPT_END	(0 |IPOPT_CONTROL)//0 00 00000  ѡ�����  //���ֽ�ѡ��  ����Ҫ���Ⱥ�ֵ


/*�޲�����1�ֽڵ�ѡ��,��Ҫ����ѡ���ѡ��֮�������,ʹ��һ��ѡ����16λ��32λ�߽��϶���*/
#define IPOPT_NOOP	(1 |IPOPT_CONTROL)//0 00 00001  �޲���    //���ӽ�ѡ��   ����Ҫ���Ⱥ�ֵ

/*��ȫ����ѡ��*/
#define IPOPT_SEC	(2 |IPOPT_CONTROL|IPOPT_COPY) // 1 00 00010 ���Ƶ����з�Ƭ ���ݱ�����   [��ȫ����]   ���ֽ�ѡ��������Ⱥ�ֵ

/*�ڴ��͹����У�һ̨�м�·�ɿ���ʹ����һ̨·�����������б��У�����Ϊͨ���б�����һ��·������·��*/
#define IPOPT_LSRR	(3 |IPOPT_CONTROL|IPOPT_COPY)//  1 00 00011 ���Ƶ����з�Ƭ ���ݱ�����  [��ɢԴ·��] ���ֽ�ѡ��������Ⱥ�ֵ

/*                                        7 6 5 4 3 2 1 0 
---------------------------------------------------------- 
| <����,class,number> |   ����   |  ֵ    | oflw  |  flg |
---------------------------------------------------------- 
                         ip��ַ  |                        
---------------------------------------------------------- 
                         ʱ���  |                        
---------------------------------------------------------- 
oflw�ֶα�ʾ����ȱ�ٿռ���޷���¼ʱ�����������Ŀ ����ʾ15������������
flg:������ѡ��Ĺ�����ʽ 
 0 ֻ��¼ʱ��� ÿ��ʱ�����4�ֽ������ŷ� ���37/4=9��
 1 ͬʱ��¼ip��ַ��ʱ���  37/8 = 4 ��������Ϣ
 3 ���ip��ַ��ʱ���  ip��ַ��Դ��Ԥ������� �����ص�ַ��Ԥ������ĵ�ַ��ͬʱ����������ת��ʱ���
*/
#define IPOPT_TIMESTAMP	(4 |IPOPT_MEASUREMENT)//     0 10 00100 �����Ƶ����з�Ƭ �Ŵ����  [ʱ���]         ���ֽ�ѡ��������Ⱥ�ֵ
#define IPOPT_CIPSO	(6 |IPOPT_CONTROL|IPOPT_COPY)//  1 00 00110 ���Ƶ����з�Ƭ ���ݱ�����  

/*��¼·��ѡ��������¼�������ݱ���������·����.�������г����Ÿ�·����IP��ַ
-------------------------------------------------
|   <����,class,number> |   ����    |     ֵ    |
|         T             |    L      |      v    |
-------------------------------------------------
|��һ��ip                                       | 
-------------------------------------------------
|��������                                       |
-------------------------------------------------
|���һ��ip                                     |
-------------------------------------------------
9*4+1+1+1=39�ֽ�
*/
#define IPOPT_RR	(7 |IPOPT_CONTROL)           //  0 00 00111 �����Ƶ����з�Ƭ ��¼·�� [ ]               ���ֽ�ѡ��������Ⱥ�ֵ



//���ڲ�֧����������ʽ�����磬��ѡ���ṩһ�ֻ���ͨ������16bit����ʶ����ģ����������ʽ
#define IPOPT_SID	(8 |IPOPT_CONTROL|IPOPT_COPY)//  1 00 01000 ���Ƶ����з�Ƭ ���ݱ����� [�����·� ]               ���ֽ�ѡ��������Ⱥ�ֵ

/*�����߱����г�·����ÿ̨·������IP��ַ��������;�������޸ģ����밴���б��ϵ�·�ɵ�ַ���д���*/
#define IPOPT_SSRR	(9 |IPOPT_CONTROL|IPOPT_COPY)//  1 00 01001 ���Ƶ����з�Ƭ ���ݱ����� [�ϸ�·��]        ���ֽ�ѡ��������Ⱥ�ֵ

////�ѷ����ǳ���Ҫ���⴦������ͼΪ����������������õ�QoS��
//·��������  ����֪ͨ·������IP���ݽ��и��ϸ�����ݼ��
#define IPOPT_RA	(20|IPOPT_CONTROL|IPOPT_COPY)//  1 01 00000 ���Ƶ����з�Ƭ ���ݱ����� [�����ֶ�]        ���ֽ�ѡ��������Ⱥ�ֵ

#define IPVERSION	4
#define MAXTTL		255
#define IPDEFTTL	64

#define IPOPT_OPTVAL 0
#define IPOPT_OLEN   1
#define IPOPT_OFFSET 2
#define IPOPT_MINOFF 4
#define MAX_IPOPTLEN 40
#define IPOPT_NOP IPOPT_NOOP
#define IPOPT_EOL IPOPT_END
#define IPOPT_TS  IPOPT_TIMESTAMP

//����ʱ��� 
#define	IPOPT_TS_TSONLY		0		/* timestamps only ֻ����ʱ���*/
#define	IPOPT_TS_TSANDADDR	1		/* timestamps and addresses ����ʱ����͵�ַ*/
#define	IPOPT_TS_PRESPEC	3		/* specified modules only ֻ����ָ������ʱ���*/

#define IPV4_BEET_PHMAXLEN 8

struct iphdr {
#if defined(__LITTLE_ENDIAN_BITFIELD)
	__u8	ihl:4,
		version:4;
#elif defined (__BIG_ENDIAN_BITFIELD)
	__u8	version:4, //�汾��
  		ihl:4;//�ײ�����  //��4�ֽ�Ϊ��λ  �ײ��: 15*4=60,�̶�Ϊ20�ֽ� �����ֽ�Ϊipѡ��
#else
#error	"Please fix <asm/byteorder.h>"
#endif
/*
     ---------------------------------
     |   |   |   | D | R | T | C |   |
     ---------------------------------
        ���ȼ�         TOSλ
        
     D:��Сʱ��
     R:��߿ɿ���
     T:���������
     C:��С����

     ���ȼ�:
     111 Network contorl
     110 Internetwork COntrol
     101 CRITIC /ECP	
     100 FLASH Override
     011 FLASH
     010 immediate
     001 priority
     000 routine
     ��·��������ӵ�������붪��һЩ���ݱ�ʱ�����е����ȼ��Ľ����ȱ�����
     */
     __u8	tos;//��������

	
	__be16	tot_len;//�ܳ���IP�ײ�+ip���� RFC791�涨 ��̲�������576�ֽ�
	__be16	id;//��־��   �������ݰ���Ƭ �����ݱ�����MTUʱ�� ���ݱ�����Ƭ��������ͬ��־λ�ֶεķ�Ƭ����һ�����ݱ�

/*************************************************************
    -----------------
	|  CE | DF | MF |
    -----------------
        ���Ʊ��� 3λ
    bit 0:Ϊ1 ��ʾӵ��
    bit 1:Ϊ0 ��ʾ���ݱ����ȴ��ҳ���MTU ��������з�Ƭ�� Ϊ1 ��ʾ���ɽ��з�Ƭ
    bit 2:Ϊ0 ��ʾ�������һ����Ƭ Ϊ1��ʾ���к�����Ƭ

    ��Ƭƫ��:13λ
    100:��ʾӵ��
    010:��ʾ����Ƭ
    001:��ʾ��������������Ƭ �����һ����Ƭ ���඼Ӧ�����ô�λ
****************************************************************************/
	__be16	frag_off;//��־λ��Ƭƫ�� ��λΪ8�ֽ�
	__u8	ttl; //����ʱ�� ÿһ��·����������Ҫת�������ݱ�ʱ���Ƚ����ֶμ�1 �����ֶα�Ϊ0���������������ݱ�����ʼ�˷���һ��ICMP����
	__u8	protocol;//IPPROTO_TCPЭ������
	__sum16	check;//�ײ�У��� ��ΪIP�ײ�����
	__be32	saddr;//Դ��ַ
	__be32	daddr;//Ŀ�ĵ�ַ
	/*The options start here. */
};
	
#ifdef __KERNEL__
#include <linux/skbuff.h>

static inline struct iphdr *ip_hdr(const struct sk_buff *skb)
{
	return (struct iphdr *)skb_network_header(skb);
}

static inline struct iphdr *ipip_hdr(const struct sk_buff *skb)
{
	return (struct iphdr *)skb_transport_header(skb);
}
#endif

struct ip_auth_hdr {
	__u8  nexthdr;
	__u8  hdrlen;		/* This one is measured in 32 bit units! */
	__be16 reserved;
	__be32 spi;
	__be32 seq_no;		/* Sequence number */
	__u8  auth_data[0];	/* Variable len but >=4. Mind the 64 bit alignment! */
};

struct ip_esp_hdr {
	__be32 spi;
	__be32 seq_no;		/* Sequence number */
	__u8  enc_data[0];	/* Variable len but >=8. Mind the 64 bit alignment! */
};

struct ip_comp_hdr {
	__u8 nexthdr;
	__u8 flags;
	__be16 cpi;
};

struct ip_beet_phdr {
	__u8 nexthdr;
	__u8 hdrlen;
	__u8 padlen;
	__u8 reserved;
};

#endif	/* _LINUX_IP_H */
