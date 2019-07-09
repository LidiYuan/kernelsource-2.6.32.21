/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Definitions for the TCP protocol.
 *
 * Version:	@(#)tcp.h	1.0.2	04/28/93
 *
 * Author:	Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#ifndef _LINUX_TCP_H
#define _LINUX_TCP_H

#include <linux/types.h>
#include <asm/byteorder.h>
#include <linux/socket.h>

//TCP_SYN_SENT״̬ת��
struct tcphdr {
	__be16	source; //���Ͷ˶˿ں�  
	__be16	dest; //���ն� �˿ں�
	__be32	seq; //���к� ���η������ݰ����������ĵ�һ���ֽڵ����к�
	__be32	ack_seq; //һ�����ʾ�������к�  ��һ�����ʾӦ�����к�
	                /*��Զ�˿��� ���ֶκ���Ϊ�����������һ���ֽڵ����кţ�Ҳ�����ڴ����к�֮ǰ�Ķ��Ѿ�������*/
#if defined(__LITTLE_ENDIAN_BITFIELD)//С�˱�ʾ
	__u16	res1:4,
		doff:4,
		fin:1,
		syn:1,
		rst:1,
		psh:1,
		ack:1,
		urg:1,
		ece:1,
		cwr:1;
#elif defined(__BIG_ENDIAN_BITFIELD)//��˱�ʾ
	__u16	doff:4, //tcp�ײ�������4�ֽ�Ϊ��λ(����TCPѡ�� �������) Ҳ�����û����ݵĿ�ʼ���
		res1:4, //����
        //���������ֶκ�ECN�����й�
		cwr:1,//ӵ��������С��־		     
		ece:1,//Ecn-echo��־  
		  
		urg:1,//����ָ���ֶ�  ���������ǽ����յ���������������Ӧ�ó���
		ack:1,//Ϊ1��ʾ���Ǹ�Ӧ���� Ӧ����һ�㶼����������һ���͵�
		psh:1,//����ͬURG ��ʾ������Ҫ���̽���Ӧ�ó��� ��������Ǹ�Ӧ�ó�����SIGURG�ź�
		rst:1,//��λ���  ��ʾ�Է�Ҫ�󱾵����½�����Է�������
		syn:1,//Ϊ1 ��������ʱ���ͬ������
		fin:1;//�������ӱ���
#else
#error	"Adjust your <asm/byteorder.h> defines"
#endif	
	__be16	window;//���ڴ�С �������� ��ʾ��ǰ�Է��������̵�������ݽ�����
	__sum16	check;//У���
	__be16	urg_ptr;//�������ݲ��� �Ǹ�ƫ��ֵ ��ƫ�����ǴӰ��е�һ�������ֽ�����ֻ��urgΪ1��ʱ�����ָ���ֶβ���Ч
};

/****
(1)ѡ����� ����0
  --------
  00000000
  --------
(2)�޲���ѡ�� ����1  ��Ϊ���
  ---------
  00000001
  ---------
(3)����ĳ��� ����2
  ---------------------------------------------------------------------------
  00000010  |  00000100        |       ����ĳ���(MSS)
  --------------------------------------------------------------------------
  ����2     ����4(��������1�ֽڱ���1�ֽںͺ�������ݲ���)
  
����ĳ���ָ�û�����һ�η��͵�������󳤶�
MSS = MTU-(TCP�ײ�����+IP�ײ�����)
*/


/*
 *	The union cast uses a gcc extension to avoid aliasing problems
 *  (union is compatible to any of its members)
 *  This means this part of the code is -fstrict-aliasing safe now.
 */
union tcp_word_hdr { 
	struct tcphdr hdr;
	__be32 		  words[5];
}; 

#define tcp_flag_word(tp) ( ((union tcp_word_hdr *)(tp))->words [3]) 

enum { 
	TCP_FLAG_CWR = __cpu_to_be32(0x00800000),
	TCP_FLAG_ECE = __cpu_to_be32(0x00400000),
	TCP_FLAG_URG = __cpu_to_be32(0x00200000),
	TCP_FLAG_ACK = __cpu_to_be32(0x00100000),
	TCP_FLAG_PSH = __cpu_to_be32(0x00080000),
	TCP_FLAG_RST = __cpu_to_be32(0x00040000),
	TCP_FLAG_SYN = __cpu_to_be32(0x00020000),
	TCP_FLAG_FIN = __cpu_to_be32(0x00010000),
	TCP_RESERVED_BITS = __cpu_to_be32(0x0F000000),
	TCP_DATA_OFFSET = __cpu_to_be32(0xF0000000)
}; 

/* TCP socket options */
/*��ʹ��һЩЭ��ͨѶ��ʱ�򣬱���Telnet������һ���ֽ��ֽڵķ��͵��龰��
ÿ�η���һ���ֽڵ��������ݣ��ͻ����41���ֽڳ��ķ��飬20���ֽڵ�IP Header �� 20���ֽڵ�TCP Header��
��͵�����1���ֽڵ�������ϢҪ�˷ѵ�40���ֽڵ�ͷ����Ϣ������һ�ʾ޴���ֽڿ�����
��������Small packet�ڹ������ϻ�����ӵ���ĳ��֡�
�������������� Nagle�������һ��ͨ��������Ҫͨ�����緢�Ͱ������������TCP/IP�����Ч�ʣ������Nagle�㷨*/
/*�ر�Nagle's�㷨,Nagle�㷨��Ҫ�Ǳ��ⷢ��С�����ݰ���Ҫ��TCP���������ֻ����һ��δ��ȷ�ϵ�С���飬
�ڸ÷����ȷ�ϵ���֮ǰ���ܷ���������С����

setsockopt(sockfd,IPPROTO_TCP,TCP_NODELAY,&on,sizeof(on))
*/
#define TCP_NODELAY		1	/* Turn off Nagle's algorithm. (int)
                               nagle �㷨��Ŀ���Ǽ��ٹ�������С�������Ŀ       

                             */
#define TCP_MAXSEG		2	/* Limit MSS   �������ֽڴ�С (int)*/ 
#define TCP_CORK		3	/* Never send partially complete segments */
#define TCP_KEEPIDLE		4	/* Start keeplives after this period */
#define TCP_KEEPINTVL		5	/* Interval between keepalives */
#define TCP_KEEPCNT		6	/* Number of keepalives before death */
#define TCP_SYNCNT		7	/* Number of SYN retransmits */
#define TCP_LINGER2		8	/* Life time of orphaned FIN-WAIT-2 state */
#define TCP_DEFER_ACCEPT	9	/* Wake up listener only when data arrive */
#define TCP_WINDOW_CLAMP	10	/* Bound advertised window */
#define TCP_INFO		11	/* Information about this connection. */
#define TCP_QUICKACK		12	/* Block/reenable quick acks */
#define TCP_CONGESTION		13	/* Congestion control algorithm */
#define TCP_MD5SIG		14	/* TCP MD5 Signature (RFC2385) */

#define TCPI_OPT_TIMESTAMPS	1
#define TCPI_OPT_SACK		2
#define TCPI_OPT_WSCALE		4
#define TCPI_OPT_ECN		8

enum tcp_ca_state
{
	TCP_CA_Open = 0,
#define TCPF_CA_Open	(1<<TCP_CA_Open)
	TCP_CA_Disorder = 1,
#define TCPF_CA_Disorder (1<<TCP_CA_Disorder)
	TCP_CA_CWR = 2,
#define TCPF_CA_CWR	(1<<TCP_CA_CWR)
	TCP_CA_Recovery = 3,
#define TCPF_CA_Recovery (1<<TCP_CA_Recovery)
	TCP_CA_Loss = 4
#define TCPF_CA_Loss	(1<<TCP_CA_Loss)
};

struct tcp_info
{
	__u8	tcpi_state;
	__u8	tcpi_ca_state;
	__u8	tcpi_retransmits;
	__u8	tcpi_probes;
	__u8	tcpi_backoff;
	__u8	tcpi_options;
	__u8	tcpi_snd_wscale : 4, tcpi_rcv_wscale : 4;

	__u32	tcpi_rto;
	__u32	tcpi_ato;
	__u32	tcpi_snd_mss;
	__u32	tcpi_rcv_mss;

	__u32	tcpi_unacked;
	__u32	tcpi_sacked;
	__u32	tcpi_lost;
	__u32	tcpi_retrans;
	__u32	tcpi_fackets;

	/* Times. */
	__u32	tcpi_last_data_sent;
	__u32	tcpi_last_ack_sent;     /* Not remembered, sorry. */
	__u32	tcpi_last_data_recv;
	__u32	tcpi_last_ack_recv;

	/* Metrics. */
	__u32	tcpi_pmtu;
	__u32	tcpi_rcv_ssthresh;
	__u32	tcpi_rtt;
	__u32	tcpi_rttvar;
	__u32	tcpi_snd_ssthresh;
	__u32	tcpi_snd_cwnd;
	__u32	tcpi_advmss;
	__u32	tcpi_reordering;

	__u32	tcpi_rcv_rtt;
	__u32	tcpi_rcv_space;

	__u32	tcpi_total_retrans;
};

/* for TCP_MD5SIG socket option */
#define TCP_MD5SIG_MAXKEYLEN	80

struct tcp_md5sig {
	struct __kernel_sockaddr_storage tcpm_addr;	/* address associated */
	__u16	__tcpm_pad1;				/* zero */
	__u16	tcpm_keylen;				/* key length */
	__u32	__tcpm_pad2;				/* zero */
	__u8	tcpm_key[TCP_MD5SIG_MAXKEYLEN];		/* key (binary) */
};

#ifdef __KERNEL__

#include <linux/skbuff.h>
#include <linux/dmaengine.h>
#include <net/sock.h>
#include <net/inet_connection_sock.h>
#include <net/inet_timewait_sock.h>

static inline struct tcphdr *tcp_hdr(const struct sk_buff *skb)
{
	return (struct tcphdr *)skb_transport_header(skb);
}

static inline unsigned int tcp_hdrlen(const struct sk_buff *skb)
{
	return tcp_hdr(skb)->doff * 4;
}

static inline unsigned int tcp_optlen(const struct sk_buff *skb)
{
	return (tcp_hdr(skb)->doff - 5) * 4;
}

/* This defines a selective acknowledgement block. */
struct tcp_sack_block_wire {
	__be32	start_seq;
	__be32	end_seq;
};

struct tcp_sack_block {
	u32	start_seq;
	u32	end_seq;
};

struct tcp_options_received {
/*	PAWS/RTTM data	*/
	long	ts_recent_stamp;/* Time we stored ts_recent (for aging) */
	u32	ts_recent;	/* Time stamp to echo next		*/
	u32	rcv_tsval;	/* Time stamp value             	*/
	u32	rcv_tsecr;	/* Time stamp echo reply        	*/
	u16 	saw_tstamp : 1,	/* Saw TIMESTAMP on last packet		*/
		tstamp_ok : 1,	/* TIMESTAMP seen on SYN packet		*/
		dsack : 1,	/* D-SACK is scheduled			*/
		wscale_ok : 1,	/* Wscale seen on SYN packet		*/
		sack_ok : 4,	/* SACK seen on SYN packet		*/
		snd_wscale : 4,	/* Window scaling received from sender	���ԶԶ�ͨ��Ļ���������������,�����ԶԶ˵ĵ�һ��SYN�л�ȡ*/
		rcv_wscale : 4;	/* Window scaling to send to receiver	���ؽ��ջ������ڵ���������*/
/*	SACKs data	*/
	u8	num_sacks;	/* Number of SACK blocks		*/
	u16	user_mss;  	/* mss requested by user in ioctl */
	u16	mss_clamp;	/* Maximal mss, negotiated at connection setup */
};

/* This is the max number of SACKS that we'll generate and process. It's safe
 * to increse this, although since:
 *   size = TCPOLEN_SACK_BASE_ALIGNED (4) + n * TCPOLEN_SACK_PERBLOCK (8)
 * only four options will fit in a standard TCP header */
#define TCP_NUM_SACKS 4

struct tcp_request_sock 
{
	struct inet_request_sock 	req;

#ifdef CONFIG_TCP_MD5SIG
	/* Only used by TCP MD5 Signature so far. */
	const struct tcp_request_sock_ops *af_specific;
#endif

    //�ͻ���SYN����Я����seq�����ͻ��˵ĳ�ʼ���к�
	u32			 	rcv_isn;

    //SYN+ACK��Я����seq�����������˵ĳ�ʼ���к�
	u32			 	snt_isn;
};

static inline struct tcp_request_sock *tcp_rsk(const struct request_sock *req)
{
	return (struct tcp_request_sock *)req;
}


/******************
Sender������:
        --------------->snd_wnd<-------
---------------------------------------
Acked   |   NotAcked    |    Credit   |
---------------------------------------
     snd_una         snd_nxt         snd_una+snd_wnd

Receiver������:

--------------------------------------------
Acked    |     NotAcked     |    Credit    |
--------------------------------------------
      rcv_wup             rcv_nxt      rcv_wup+rcv_wnd 
*/

struct tcp_sock {
	/* inet_connection_sock has to be the first member of tcp_sock */
	struct inet_connection_sock	inet_conn;
	
	u16	tcp_header_len;	     /* Bytes of tcp header to send	tcpͷ������*/
	u16	xmit_size_goal_segs; /* Goal for segmenting output packets �ֶ����ݰ�������*/

/*
 *	Header prediction flags
 *	0x5?10 << 16 + snd_wnd in net byte order
 */
    //ͷ��Ԥ��λ�����ڼ��ͷ����ʶλ����ACK��PUSH֮�⻹��û������λ���Ӷ��ж��ǲ��ǿ���ʹ�ÿ���·����������
	__be32	pred_flags;

/*
 *	RFC793 variables by their proper names. This means you can
 *	read the code and the spec side by side (and laugh ...)
 *	See RFC793 and RFC1122. The RFC writes these in capitals.
 */
 	u32	rcv_nxt;	/* What we want to receive next 	ϣ�����յ���һ�����к�*/
	u32	copied_seq;	/* Head of yet unread data		Ӧ�ó����´δ����︴������*/
	u32	rcv_wup;	/* rcv_nxt on last window update sent	��¼�������ڵ������,���ڻ��������е���С��һ�����*/
    
	u32	snd_nxt;	/* Next sequence we send	��һ�������͵����к�	*/

 	u32	snd_una;	/* First byte we want an ack for	��һ����ȷ�ϵ��ֽ�*/
 	u32	snd_sml;	/* �Ѿ�����ȥ�������һ��С�������һ���ֽ�*/
	                 
	u32	rcv_tstamp;	/*���һ�ν��յ�ACK��ʱ��� timestamp of last received ACK (for keepalives) */
	u32	lsndtime;	/* ���һ�η������ݰ�ʱ��� timestamp of last sent data packet (for restart window) */

	/* Data for direct copy to user */
	// ע���������ucopy�����ǽ��û����ݴ�skb���ó����Ž�ȥ��Ȼ�󴫸�Ӧ�ý��̣�����
	struct {
		struct sk_buff_head	prequeue;// Ԥ�������
		struct task_struct	*task;// Ԥ�������
		struct iovec		*iov;// �û�����(Ӧ�ó���)�������ݵĻ�����
		int			memory; // ����Ԥ�������
		int			len;/// Ԥ������ 
#ifdef CONFIG_NET_DMA
		/* members for async copy */
		struct dma_chan		*dma_chan;
		int			wakeup;
		struct dma_pinned_list	*pinned_list;
		dma_cookie_t		dma_cookie;
#endif
	} ucopy;

	u32	snd_wl1;	/* Sequence for window update		*/
	u32	snd_wnd;	/* The window we expect to receive	*/

	//��¼�Է����ķ��� window ����ֵ 
	u32	max_window;	/* Maximal window ever seen from peer	*/

	// ����Ĵ�СĬ��536, ��Է�Э��һ��Ϊ1460 = 1500-20-20
	u32	mss_cache;	/* Cached effective mss, not including SACKS */

    //// ���⹫�������Ĵ���
	u32	window_clamp;	/* Maximal window to advertise	��ʾ�������ڵ����ֵ,�������ڵĴ�С�ڱ仯�Ĺ����в��ܳ������ֵ*/

    // ��ǰ����ֵ 
	u32	rcv_ssthresh;	/* Current window clamp		�ǵ�ǰ�Ľ��մ��ڴ�С��һ����ֵ	*/

	u32	frto_highmark;	/* snd_nxt when RTO occurred */
	u16	advmss;		/* Advertised MSS		���ն�ͨ���MSS	*/
	u8	frto_counter;	/* Number of new acks after RTO */
	u8	nonagle;	/* Disable Nagle algorithm?             */

/* RTT measurement */
	u32	srtt;		/* smoothed round trip time << 3	*/
	u32	mdev;		/* medium deviation			*/
	u32	mdev_max;	/* maximal mdev for the last rtt period	*/
	u32	rttvar;		/* smoothed mdev_max			*/
	u32	rtt_seq;	/* sequence number to update rttvar	*/

	u32	packets_out;	/* Packets which are "in flight"	*/
	u32	retrans_out;	/* Retransmitted packets out		*/

	u16	urg_data;	/* Saved octet of OOB data and control flags */
	u8	ecn_flags;	/* ECN status bits.			*/
	u8	reordering;	/* Packet reordering metric.		*/
	u32	snd_up;		/* Urgent pointer		*/

	u8	keepalive_probes; /* num of allowed keep alive probes	*/
/*
 *      Options received (usually on last packet, some only on SYN packets).
 */
	struct tcp_options_received rx_opt;

/*������ӵ������
 *	Slow start and congestion control (see also Nagle, and Karn & Partridge)
 */
 	u32	snd_ssthresh;	/* Slow start size threshold	��������ֵ ��ӵ������С�ڴ�ֵ ���ǽ����������׶� tcp_slow_start*/
 	u32	snd_cwnd;	   /* Sending congestion window     ӵ�����ڵĴ�С		*/
	u32	snd_cwnd_cnt;	/* Linear increase counter ������������ֵ�� ���������ĸ���	*/
	u32	snd_cwnd_clamp; /* Do not allow snd_cwnd to grow above this ����ӵ�����ڿ������ӵ��������ֵ*/
	u32	snd_cwnd_used;
	u32	snd_cwnd_stamp; //ӵ���������һ����Ч��ʱ���

 	u32	rcv_wnd;	/* Current receiver window		��ǰ���մ��ڵĴ�С*/
	u32	write_seq;	/* Tail(+1) of data held in tcp send buffer */
	u32	pushed_seq;	/* Last pushed seq, required to talk to windows */
	u32	lost_out;	/* Lost packets			*/
	u32	sacked_out;	/* SACK'd packets			*/
	u32	fackets_out;	/* FACK'd packets			*/
	u32	tso_deferred;
	u32	bytes_acked;	/* Appropriate Byte Counting - RFC3465 */

	/* from STCP, retrans queue hinting */
	struct sk_buff* lost_skb_hint;
	struct sk_buff *scoreboard_skb_hint;
	struct sk_buff *retransmit_skb_hint;

                                             //��������Ķ���  ���յ�������ı�����ʱ�洢�ڴ˶����� 
	struct sk_buff_head	out_of_order_queue; /* Out of order segments go here */

	/* SACKs data, these 2 need to be together (see tcp_build_and_update_options) */
	struct tcp_sack_block duplicate_sack[1]; /* D-SACK block */
	struct tcp_sack_block selective_acks[4]; /* The SACKS themselves*/

	struct tcp_sack_block recv_sack_cache[4];

	struct sk_buff *highest_sack;   /* highest skb with SACK received
					 * (validity guaranteed only if
					 * sacked_out > 0)
					 */

	int     lost_cnt_hint;
	u32     retransmit_high;	/* L-bits may be on up to this seqno */

	u32	lost_retrans_low;	/* Sent seq after any rxmit (lowest) */

	u32	prior_ssthresh; /* ssthresh saved at recovery start	*/
	u32	high_seq;	/* snd_nxt at onset of congestion	*/

	u32	retrans_stamp;	/* Timestamp of the last retransmit,
				 * also used in SYN-SENT to remember stamp of
				 * the first SYN. */
	u32	undo_marker;	/* tracking retrans started here. */
	int	undo_retrans;	/* number of undoable retransmissions. */
	u32	total_retrans;	/* Total retransmits for entire connection */

	u32	urg_seq;	/* Seq of received urgent pointer */
	unsigned int		keepalive_time;	  /* time before keep alive takes place */
	unsigned int		keepalive_intvl;  /* time interval between keep alive probes */

	int			linger2;

/* Receiver side RTT estimation */
	struct {
		u32	rtt;
		u32	seq;
		u32	time;
	} rcv_rtt_est;

/* Receiver queue space */
	struct {
		int	space;//��ʾ��ǰ���ջ���Ĵ�С��ֻ����Ӧ�ò����ݣ���λΪ�ֽڣ���
		u32	seq;
		u32	time;
	} rcvq_space;

/* TCP-specific MTU probe information. */
	struct {
		u32		  probe_seq_start;
		u32		  probe_seq_end;
	} mtu_probe;

#ifdef CONFIG_TCP_MD5SIG
/* TCP AF-Specific parts; only used by MD5 Signature support so far */
	const struct tcp_sock_af_ops	*af_specific;

/* TCP MD5 Signature Option information */
	struct tcp_md5sig_info	*md5sig_info;
#endif
};

static inline struct tcp_sock *tcp_sk(const struct sock *sk)
{
	return (struct tcp_sock *)sk;
}

struct tcp_timewait_sock {
	struct inet_timewait_sock tw_sk;
	u32			  tw_rcv_nxt;
	u32			  tw_snd_nxt;
	u32			  tw_rcv_wnd;
	u32			  tw_ts_recent;
	long			  tw_ts_recent_stamp;
#ifdef CONFIG_TCP_MD5SIG
	u16			  tw_md5_keylen;
	u8			  tw_md5_key[TCP_MD5SIG_MAXKEYLEN];
#endif
};

static inline struct tcp_timewait_sock *tcp_twsk(const struct sock *sk)
{
	return (struct tcp_timewait_sock *)sk;
}

#endif

#endif	/* _LINUX_TCP_H */
