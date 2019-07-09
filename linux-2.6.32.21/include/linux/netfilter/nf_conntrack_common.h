#ifndef _NF_CONNTRACK_COMMON_H
#define _NF_CONNTRACK_COMMON_H
/* Connection state tracking for netfilter.  This is separated from,
   but required by, the NAT layer; it can also be used by an iptables
   extension. */
  //struct nf_conn ���Ӹ��ٽṹ��
  //���Ӹ��ٶԸ������ϵ�ÿ�����ݰ�����Ϊ���¼���״̬֮һ
enum ip_conntrack_info
{
	/*��ʾ������ݰ���Ӧ���������������������ݰ�ͨ����
	��������ORIGINAL��ʼ�������ݰ���������TCP��UDP��ICMP���ݰ���
	ֻҪ�ڸ����ӵ������������������ݰ�ͨ�����ͻὫ����������ΪIP_CT_ESTABLISHED״̬��
	�������Э���еı�־λ�����жϣ�����TCP��SYN�ȣ���
	������ʾ�������ǵڼ������ݰ���Ҳ˵���������CT�Ƿ��������ӡ�*/
	/* Part of an established connection (either direction). */
	IP_CT_ESTABLISHED,//Packet��һ���ѽ����ӵ�һ���֣������ʼ����
	
	 /* ��ʾ������ݰ���Ӧ�����ӻ�û��REPLY�������ݰ�����ǰ���ݰ���ORIGINAL�������ݰ���
	 ����������ӹ���һ�����е����ӣ��Ǹ��������ӵ������ӣ�
	 ����status��־���Ѿ�������IPS_EXPECTED��־���ñ�־��init_conntrack()���������ã���
	 ���޷��ж��ǵڼ������ݰ�����һ���ǵ�һ����*/
	/* Like NEW, but related to an existing connection, or ICMP error
	   (in either direction). */
	IP_CT_RELATED,//Packet����һ���ѽ����ӵ�������ӣ������ʼ����

	 /* ��ʾ������ݰ���Ӧ�����ӻ�û��REPLY�������ݰ�����ǰ���ݰ���ORIGINAL�������ݰ���
	 �����Ӳ��������ӡ����޷��ж��ǵڼ������ݰ�����һ���ǵ�һ����*/
	/* Started a new connection to track (only
           IP_CT_DIR_ORIGINAL); may be a retransmission. */
	IP_CT_NEW,//Packet��ͼ�����µ�����


	 /* ���״̬һ�㲻����ʹ�ã�ͨ�����������ַ�ʽʹ�� 
		IP_CT_ESTABLISHED + IP_CT_IS_REPLY  ��ʾ������ݰ���Ӧ���������������������ݰ�ͨ����
											��������REPLYӦ�������ݰ���������ʾ�������ǵڼ������ݰ���
											Ҳ˵���������CT�Ƿ���������
		IP_CT_RELATED + IP_CT_IS_REPLY	 ���״̬����nf_conntrack_attach()���������ã�
										���ڱ�������REJECT�����緵��һ��ICMPĿ�Ĳ��ɴﱨ�ģ� 
										�򷵻�һ��reset���ġ�����ʾ�������ǵڼ������ݰ�

	*/
	/* >= this indicates reply direction */
	IP_CT_IS_REPLY,//Packet��һ���ѽ����ӵ�һ���֣�������Ӧ����

	/* Number of distinct IP_CT types (no NEW in reply dirn). */
	IP_CT_NUMBER = IP_CT_IS_REPLY * 2 - 1
};

/* Bitset representing status of connection. */
enum ip_conntrack_status {
	/* It's an expected connection: bit 0 set.  This bit never changed */
	IPS_EXPECTED_BIT = 0,///* ��ʾ�������Ǹ������� */
	IPS_EXPECTED = (1 << IPS_EXPECTED_BIT),

	/* We've seen packets both ways: bit 1 set.  Can be set, not unset. */
	IPS_SEEN_REPLY_BIT = 1, /* ��ʾ��������˫�����϶������ݰ��� */
	IPS_SEEN_REPLY = (1 << IPS_SEEN_REPLY_BIT),

	/* Conntrack should never be early-expired. */
	IPS_ASSURED_BIT = 2,/* TCP�����������ֽ��������Ӻ��趨�ñ�־��UDP������ڸ������ϵ��������������ݰ�ͨ����
                            ���������ݰ��ڸ�������ͨ��ʱ�����趨�ñ�־��ICMP�������øñ�־ */
	IPS_ASSURED = (1 << IPS_ASSURED_BIT),

	/* Connection is confirmed: originating packet has left box */
	IPS_CONFIRMED_BIT = 3, /* ��ʾ�������ѱ���ӵ�net->ct.hash���� */
	IPS_CONFIRMED = (1 << IPS_CONFIRMED_BIT),

	/* Connection needs src nat in orig dir.  This bit never changed. */
	IPS_SRC_NAT_BIT = 4,  /*��POSTROUTING�������滻reply tuple���ʱ, ���øñ�� */
	IPS_SRC_NAT = (1 << IPS_SRC_NAT_BIT),

	/* Connection needs dst nat in orig dir.  This bit never changed. */
	IPS_DST_NAT_BIT = 5,  /* ��PREROUTING�������滻reply tuple���ʱ, ���øñ�� */
	IPS_DST_NAT = (1 << IPS_DST_NAT_BIT),

	/* Both together. */
	IPS_NAT_MASK = (IPS_DST_NAT | IPS_SRC_NAT),

	/* Connection needs TCP sequence adjusted. */
	IPS_SEQ_ADJUST_BIT = 6,
	IPS_SEQ_ADJUST = (1 << IPS_SEQ_ADJUST_BIT),

	/* NAT initialization bits. */
	IPS_SRC_NAT_DONE_BIT = 7, /* ��POSTROUTING�����ѱ�SNAT�����������뵽bysource���У����øñ�� */
	IPS_SRC_NAT_DONE = (1 << IPS_SRC_NAT_DONE_BIT),

	IPS_DST_NAT_DONE_BIT = 8,/* ��PREROUTING�����ѱ�DNAT�����������뵽bysource���У����øñ�� */
	IPS_DST_NAT_DONE = (1 << IPS_DST_NAT_DONE_BIT),

	/* Both together */
	IPS_NAT_DONE_MASK = (IPS_DST_NAT_DONE | IPS_SRC_NAT_DONE),

	/* Connection is dying (removed from lists), can not be unset. */
	IPS_DYING_BIT = 9,   /* ��ʾ���������ڱ��ͷţ��ں�ͨ���ñ�־��֤���ڱ��ͷŵ�ct���ᱻ�����ط��ٴ����á�
						���������־����ĳ������Ҫ��ɾ��ʱ����ʹ������net->ct.hash�У�Ҳ�����ٴα����á�*/
	IPS_DYING = (1 << IPS_DYING_BIT),

	/* Connection has fixed timeout. */
	IPS_FIXED_TIMEOUT_BIT = 10,/* �̶����ӳ�ʱʱ�䣬�⽫������״̬�޸����ӳ�ʱʱ�䡣
								ͨ������nf_ct_refresh_acct()�޸ĳ�ʱʱ��ʱ���ñ�־�� */
	IPS_FIXED_TIMEOUT = (1 << IPS_FIXED_TIMEOUT_BIT),
};

#ifdef __KERNEL__
struct ip_conntrack_stat
{
	unsigned int searched;
	unsigned int found;
	unsigned int new;
	unsigned int invalid;
	unsigned int ignore;
	unsigned int delete;
	unsigned int delete_list;
	unsigned int insert;
	unsigned int insert_failed;
	unsigned int drop;
	unsigned int early_drop;
	unsigned int error;
	unsigned int expect_new;
	unsigned int expect_create;
	unsigned int expect_delete;
};

/* call to create an explicit dependency on nf_conntrack. */
extern void need_conntrack(void);

#endif /* __KERNEL__ */

#endif /* _NF_CONNTRACK_COMMON_H */
