#ifndef __LINUX_NETLINK_H
#define __LINUX_NETLINK_H

#include <linux/socket.h> /* for sa_family_t */
#include <linux/types.h>


/*NETLINK_ROUTE��Ϊ�����Ϣ��
LINK(����ӿ�)
ADDR(�����ַ)
ROUTE(·��ѡ����Ϣ)
NEIGH(�ھ���ϵͳ��Ϣ)
RULE(����·�ɹ���)
QDISC(�Ŷӹ���)
TCLASS(��������)
ACTION(���ݰ�����API)
NEIGHTBL(�ڽӱ�)
ADDRLABEL(��ַ���)
*/
#define NETLINK_ROUTE		0	/* ·������ NETLINK_ROUTE �����ṩ�������ַ�����仯����Ϣ		
                                 �� rtnetlink_net_init ��ʹ�� */
                                                             
#define NETLINK_UNUSED		1	/* Unused number				*/
#define NETLINK_USERSOCK	2	/* Reserved for user mode socket protocols 	*/
#define NETLINK_FIREWALL	3	/* Firewalling hook				*/
#define NETLINK_INET_DIAG	4	/* INET socket monitoring			*/
#define NETLINK_NFLOG		5	/* netfilter/iptables ULOG */
#define NETLINK_XFRM		6	/* ipsec ��ʾIPsec��ϵͳ*/
#define NETLINK_SELINUX		7	/* SELinux event notifications */
#define NETLINK_ISCSI		8	/* Open-iSCSI */
#define NETLINK_AUDIT		9	/* auditing �����ϵͳ*/
#define NETLINK_FIB_LOOKUP	10	
#define NETLINK_CONNECTOR	11  
#define NETLINK_NETFILTER	12	/* netfilter subsystem */
#define NETLINK_IP6_FW		13  
#define NETLINK_DNRTMSG		14	/* DECnet routing messages */
#define NETLINK_KOBJECT_UEVENT	15	/* Kernel messages to userspace �����ں��¼����͵�Ӧ�ò�*/
#define NETLINK_GENERIC		16   //ͨ���׽�������
/* leave room for NETLINK_DM (DM Events) */
#define NETLINK_SCSITRANSPORT	18	/* SCSI Transports */
#define NETLINK_ECRYPTFS	19

#define MAX_LINKS 32		

struct net;

struct sockaddr_nl
{
	sa_family_t	nl_family;	/* AF_NETLINK	*/
	unsigned short	nl_pad;		/* zero		��*/
	__u32		nl_pid;		/* port ID	�˿ں� netlink�׽��ֵĵ�����ַ �����ں�netlink�׽��� ��ֵΪ0��
	                        */
    __u32		nl_groups;	/* multicast groups mask �鲥������*/
};

//netlink��Ϣͷ�����ݽ�����ͷ�ĺ���
struct nlmsghdr
{
	__u32		nlmsg_len;	/* ������Ϣͷ���ڵ���Ϣ�ܳ��� */
	__u16		nlmsg_type;	/*��Ϣ���� ������Ϣ������NLMSG_ERROR,��������Լ�����Ϣ����*/
	__u16		nlmsg_flags;	/* �����޸���Ϣ������Ϊ NLM_F_ACK*/
	__u32		nlmsg_seq;	/* ���к� ����������Ϣ ��ѡʹ��*/
	__u32		nlmsg_pid;	/* ���Ͷ˿�ID �����ں˷��͵���Ϣ ��Ϊ0,���ڴ�Ӧ�ò㷢�͵���Ϣ��ֵ������Ϊ����PID*/
};
/*
---------------------------
       ��Ϣ����           |
---------------------------
  ��Ϣ���� |   ��־       |
---------------------------
       ���к�             |
---------------------------
       �˿�ID             |
---------------------------
      �غ�����

*/

/* Flags values */

#define NLM_F_REQUEST	1   /* It is request message. 	��ϢΪ������Ϣ*/
#define NLM_F_MULTI		2	/* ��Ϣ�ɶಿ�����,��Ϣ�ĳ���ͨ��������һҳ ��Ϊ������Ϣ���ֳ����С����Ϣ,ÿ����Ϣ������NLM_F_MULTI,
                               �����һ������NLMSG_DONE */
#define NLM_F_ACK		4	/* ϣ�����շ�ʹ��ACK����Ϣ����Ӧ��,ACK��Ϣ���ɷ���netlink_ack���͵� */
#define NLM_F_ECHO		8	/* Echo this request 	��Ӧ��ǰ����	*/

/* Modifiers to GET request */
#define NLM_F_ROOT	0x100	/* specify tree	root	ָ������*/
#define NLM_F_MATCH	0x200	/* return all matching	��������ƥ�����Ŀ*/
#define NLM_F_ATOMIC	0x400	/* atomic GET	�Է����˱�־	*/
#define NLM_F_DUMP	(NLM_F_ROOT|NLM_F_MATCH) //�����йر�/��Ŀ����Ϣ

/* Modifiers to NEW request */
#define NLM_F_REPLACE	0x100	/* Override existing	���Ǽ�����Ŀ	*/
#define NLM_F_EXCL	0x200	/* Do not touch, if it exists	����������Ŀ����*/
#define NLM_F_CREATE	0x400	/* Create, if it does not exist	������Ŀ*/
#define NLM_F_APPEND	0x800	/* Add to end of list	���б�ĩβ�����Ŀ	*/

/*
   4.4BSD ADD		NLM_F_CREATE|NLM_F_EXCL
   4.4BSD CHANGE	NLM_F_REPLACE

   True CHANGE		NLM_F_CREATE|NLM_F_REPLACE
   Append		NLM_F_CREATE
   Check		NLM_F_EXCL
 */

#define NLMSG_ALIGNTO	4
#define NLMSG_ALIGN(len) ( ((len)+NLMSG_ALIGNTO-1) & ~(NLMSG_ALIGNTO-1) )
#define NLMSG_HDRLEN	 ((int) NLMSG_ALIGN(sizeof(struct nlmsghdr)))
#define NLMSG_LENGTH(len) ((len)+NLMSG_ALIGN(NLMSG_HDRLEN))

#define NLMSG_SPACE(len) NLMSG_ALIGN(NLMSG_LENGTH(len))



#define NLMSG_NEXT(nlh,len)	 ((len) -= NLMSG_ALIGN((nlh)->nlmsg_len), \
				  (struct nlmsghdr*)(((char*)(nlh)) + NLMSG_ALIGN((nlh)->nlmsg_len)))
#define NLMSG_OK(nlh,len) ((len) >= (int)sizeof(struct nlmsghdr) && \
			   (nlh)->nlmsg_len >= sizeof(struct nlmsghdr) && \
			   (nlh)->nlmsg_len <= (len))


//��ָ��ָ������������
#define NLMSG_DATA(nlh)  ((void*)(((char*)nlh) + NLMSG_LENGTH(0)))
//������ݵĳ���			   
#define NLMSG_PAYLOAD(nlh,len) ((nlh)->nlmsg_len - NLMSG_SPACE((len)))


#define NLMSG_NOOP		0x1	/* Nothing.		��ִ���κβ���,���뽫��Ϣ����*/
#define NLMSG_ERROR		0x2	/* Error		�����˴���*/
#define NLMSG_DONE		0x3	/* End of a dump	�ɶಿ����ɵ���Ϣ��ĩβ*/
#define NLMSG_OVERRUN		0x4	/* Data lost		���������֪ͨ*/
//С�ڴ���Ϣ�������ڱ��� ���ڿ�����Ϣ
#define NLMSG_MIN_TYPE		0x10	/* < 0x10: reserved control messages */

//netlink������Ϣ��ʽ
struct nlmsgerr
{
	int		error;
	struct nlmsghdr msg;
};

#define NETLINK_ADD_MEMBERSHIP	1  //�����鲥��
#define NETLINK_DROP_MEMBERSHIP	2 //�˳��鲥��
#define NETLINK_PKTINFO		    3
#define NETLINK_BROADCAST_ERROR	4
#define NETLINK_NO_ENOBUFS	    5

struct nl_pktinfo
{
	__u32	group;
};

#define NET_MAJOR 36		/* Major 36 is reserved for networking 						*/

enum {
	NETLINK_UNCONNECTED = 0,
	NETLINK_CONNECTED,
};

/*
 *  <------- NLA_HDRLEN ------> <-- NLA_ALIGN(payload)-->
 * +---------------------+- - -+- - - - - - - - - -+- - -+
 * |        Header       | Pad |     Payload       | Pad |
 * |   (struct nlattr)   | ing |                   | ing |
 * +---------------------+- - -+- - - - - - - - - -+- - -+
 *  <-------------- nlattr->nla_len -------------->
 */
//netlink����ͷ
struct nlattr
{
	__u16           nla_len;//���Եĳ��� ��λΪ�ֽ�
	__u16           nla_type;//���Ե����� ���ܵ�ȡֵ���� NLA_U32(32λ�޷�������)
};

/*
 * nla_type (16 bits)
 * +---+---+-------------------------------+
 * | N | O | Attribute Type                |
 * +---+---+-------------------------------+
 * N := Carries nested attributes
 * O := Payload stored in network byte order
 *
 * Note: The N and O flag are mutually exclusive.
 */
#define NLA_F_NESTED		(1 << 15)
#define NLA_F_NET_BYTEORDER	(1 << 14)
#define NLA_TYPE_MASK		~(NLA_F_NESTED | NLA_F_NET_BYTEORDER)

#define NLA_ALIGNTO		4
#define NLA_ALIGN(len)		(((len) + NLA_ALIGNTO - 1) & ~(NLA_ALIGNTO - 1))
#define NLA_HDRLEN		((int) NLA_ALIGN(sizeof(struct nlattr)))

#ifdef __KERNEL__

#include <linux/capability.h>
#include <linux/skbuff.h>

static inline struct nlmsghdr *nlmsg_hdr(const struct sk_buff *skb)
{
	return (struct nlmsghdr *)skb->data;
}

struct netlink_skb_parms
{
	struct ucred		creds;		/* Skb credentials	*/
	__u32			pid;
	__u32			dst_group;
	kernel_cap_t		eff_cap;
	__u32			loginuid;	/* Login (audit) uid */
	__u32			sessionid;	/* Session id (audit) */
	__u32			sid;		/* SELinux security id */
};

#define NETLINK_CB(skb)		(*(struct netlink_skb_parms*)&((skb)->cb))
#define NETLINK_CREDS(skb)	(&NETLINK_CB((skb)).creds)


extern void netlink_table_grab(void);
extern void netlink_table_ungrab(void);

extern struct sock *netlink_kernel_create(struct net *net,
					  int unit,unsigned int groups,
					  void (*input)(struct sk_buff *skb),
					  struct mutex *cb_mutex,
					  struct module *module);
extern void netlink_kernel_release(struct sock *sk);
extern int __netlink_change_ngroups(struct sock *sk, unsigned int groups);
extern int netlink_change_ngroups(struct sock *sk, unsigned int groups);
extern void __netlink_clear_multicast_users(struct sock *sk, unsigned int group);
extern void netlink_clear_multicast_users(struct sock *sk, unsigned int group);
extern void netlink_ack(struct sk_buff *in_skb, struct nlmsghdr *nlh, int err);
extern int netlink_has_listeners(struct sock *sk, unsigned int group);
extern int netlink_unicast(struct sock *ssk, struct sk_buff *skb, __u32 pid, int nonblock);
extern int netlink_broadcast(struct sock *ssk, struct sk_buff *skb, __u32 pid,
			     __u32 group, gfp_t allocation);
extern void netlink_set_err(struct sock *ssk, __u32 pid, __u32 group, int code);
extern int netlink_register_notifier(struct notifier_block *nb);
extern int netlink_unregister_notifier(struct notifier_block *nb);

/* finegrained unicast helpers: */
struct sock *netlink_getsockbyfilp(struct file *filp);
int netlink_attachskb(struct sock *sk, struct sk_buff *skb,
		      long *timeo, struct sock *ssk);
void netlink_detachskb(struct sock *sk, struct sk_buff *skb);
int netlink_sendskb(struct sock *sk, struct sk_buff *skb);

/*
 *	skb should fit one page. This choice is good for headerless malloc.
 *	But we should limit to 8K so that userspace does not have to
 *	use enormous buffer sizes on recvmsg() calls just to avoid
 *	MSG_TRUNC when PAGE_SIZE is very large.
 */
#if PAGE_SIZE < 8192UL
#define NLMSG_GOODSIZE	SKB_WITH_OVERHEAD(PAGE_SIZE)
#else
#define NLMSG_GOODSIZE	SKB_WITH_OVERHEAD(8192UL)
#endif

#define NLMSG_DEFAULT_SIZE (NLMSG_GOODSIZE - NLMSG_HDRLEN)


struct netlink_callback
{
	struct sk_buff		*skb;
	const struct nlmsghdr	*nlh;
	int			(*dump)(struct sk_buff * skb,
					struct netlink_callback *cb);
	int			(*done)(struct netlink_callback *cb);
	int			family;
	long			args[6];
};

struct netlink_notify
{
	struct net *net;
	int pid;
	int protocol;
};

static __inline__ struct nlmsghdr *
__nlmsg_put(struct sk_buff *skb, u32 pid, u32 seq, int type, int len, int flags)
{
	struct nlmsghdr *nlh;
	int size = NLMSG_LENGTH(len);

	nlh = (struct nlmsghdr*)skb_put(skb, NLMSG_ALIGN(size));
	nlh->nlmsg_type = type;
	nlh->nlmsg_len = size;
	nlh->nlmsg_flags = flags;
	nlh->nlmsg_pid = pid;
	nlh->nlmsg_seq = seq;
	if (!__builtin_constant_p(size) || NLMSG_ALIGN(size) - size != 0)
		memset(NLMSG_DATA(nlh) + len, 0, NLMSG_ALIGN(size) - size);
	return nlh;
}

#define NLMSG_NEW(skb, pid, seq, type, len, flags) \
({	if (unlikely(skb_tailroom(skb) < (int)NLMSG_SPACE(len))) \
		goto nlmsg_failure; \
	__nlmsg_put(skb, pid, seq, type, len, flags); })

#define NLMSG_PUT(skb, pid, seq, type, len) \
	NLMSG_NEW(skb, pid, seq, type, len, 0)

extern int netlink_dump_start(struct sock *ssk, struct sk_buff *skb,
			      const struct nlmsghdr *nlh,
			      int (*dump)(struct sk_buff *skb, struct netlink_callback*),
			      int (*done)(struct netlink_callback*));


#define NL_NONROOT_RECV 0x1
#define NL_NONROOT_SEND 0x2
extern void netlink_set_nonroot(int protocol, unsigned flag);

#endif /* __KERNEL__ */

#endif	/* __LINUX_NETLINK_H */
