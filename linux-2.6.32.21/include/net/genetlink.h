#ifndef __NET_GENERIC_NETLINK_H
#define __NET_GENERIC_NETLINK_H

#include <linux/genetlink.h>
#include <net/netlink.h>
#include <net/net_namespace.h>

/**
 * struct genl_multicast_group - generic netlink multicast group
 * @name: name of the multicast group, names are per-family
 * @id: multicast group ID, assigned by the core, to use with
 *      genlmsg_multicast().
 * @list: list entry for linking
 * @family: pointer to family, need not be set before registering
 */
struct genl_multicast_group
{
	struct genl_family	*family;	/* private */
	struct list_head	list;		/* private */
	char			name[GENL_NAMSIZ];
	u32			id;
};

/**
 * struct genl_family - generic netlink family
 * @id: protocol family idenfitier
 * @hdrsize: length of user specific header in bytes
 * @name: name of family
 * @version: protocol version
 * @maxattr: maximum number of attributes supported
 * @netnsok: set to true if the family can handle network
 *	namespaces and should be presented in all of them
 * @attrbuf: buffer to store parsed attributes
 * @ops_list: list of all assigned operations
 * @family_list: family list
 * @mcast_groups: multicast groups list
 */
struct genl_family
{
	unsigned int		id;//Э�����־��
	unsigned int		hdrsize;//˽�б�ͷ�ĳ���
	char			    name[GENL_NAMSIZ];//��һ�޶�������
	unsigned int		version;//Э��汾
	unsigned int		maxattr;//֧�����Ե�������
	bool			    netnsok;//ָʾ��ǰ���Ƿ��ܹ��������������ռ�
	struct nlattr **	attrbuf;	/* private ���濽����attr���Ի���*/
	struct list_head	ops_list;	/* private */
	struct list_head	family_list;	/* ����ṹ�����ڽ���ǰ��ǰ������ȫ��family_htɢ�б���*/
	struct list_head	mcast_groups;	/* ���浱ǰ��ʹ�õ��鲥�鼰�鲥��ַ�ĸ���*/
};

/**
 * struct genl_info - receiving information
 * @snd_seq: sending sequence number
 * @snd_pid: netlink pid of sender
 * @nlhdr: netlink message header
 * @genlhdr: generic netlink message header
 * @userhdr: user specific header
 * @attrs: netlink attributes
 */
struct genl_info
{
	u32			snd_seq;//��Ϣ�ķ�����ţ���ǿ��ʹ�ã���
	u32			snd_pid;//��Ϣ���Ͷ�socket���󶨵�ID��
	struct nlmsghdr   *nlhdr;//netlink��Ϣͷ��
	struct genlmsghdr *genlhdr;//generic netlink��Ϣͷ��
	void *			userhdr;//�û�˽�б�ͷ��
	struct nlattr **	attrs;//netlink���ԣ���������Ϣ��ʵ���غɣ�
#ifdef CONFIG_NET_NS
	struct net *		_net;
#endif
};

#ifdef CONFIG_NET_NS
static inline struct net *genl_info_net(struct genl_info *info)
{
	return info->_net;
}

static inline void genl_info_net_set(struct genl_info *info, struct net *net)
{
	info->_net = net;
}

#else
static inline struct net *genl_info_net(struct genl_info *info)
{
	return &init_net;
}

static inline void genl_info_net_set(struct genl_info *info, struct net *net)
{
}
#endif

/*
 * struct genl_ops - generic netlink operations
 * @cmd: command identifier
 * @flags: flags��˽�б�ʶ�����ڽ���һЩ��֧�������Բ�ʹ�ã�
 * @policy: attribute validation policy
 * @doit: standard command callback
 * @dumpit: callback for dumpers
 * @done: completion callback for dumps
 * @ops_list: operations list
 */
struct genl_ops
{
	u8			cmd;//���������ͣ����û����и�����Ҫ����
	unsigned int flags;//��˽�б�ʶ��
	                          //GENL_ADMIN_PERM

    const struct nla_policy	*policy;//����attr��Ч�Բ��� �����ֶβ�Ϊ�գ���genlִ����Ϣ������ǰ�����Ϣ�е�attr���Խ���У�飬��������У�飻

	int		       (*doit)(struct sk_buff *skb,
				                struct genl_info *info);//��׼����ص��������ڵ�ǰ�����յ�����ʱ�������ã������ĵ�һ�����skb�б������û��·�����Ϣ����
	
	int		       (*dumpit)(struct sk_buff *skb,
					 struct netlink_callback *cb);//ת���ص���������genl_ops��flag��־�������NLM_F_DUMP�Ժ����øûص�������
					                              //����ĵ�һ�����skb�в������û��·���Ϣ���ݣ�����Ҫ�����ܹ��ڴ����skb��������Ϣ�غɲ������������ݳ��ȣ�
					 
	int		       (*done)(struct netlink_callback *cb);//ת��������ִ�еĻص�����
	struct list_head	ops_list;
};

extern int genl_register_family(struct genl_family *family);
extern int genl_register_family_with_ops(struct genl_family *family,
	       struct genl_ops *ops, size_t n_ops);
extern int genl_unregister_family(struct genl_family *family);
extern int genl_register_ops(struct genl_family *, struct genl_ops *ops);
extern int genl_unregister_ops(struct genl_family *, struct genl_ops *ops);
extern int genl_register_mc_group(struct genl_family *family,
				  struct genl_multicast_group *grp);
extern void genl_unregister_mc_group(struct genl_family *family,
				     struct genl_multicast_group *grp);

/**
 * genlmsg_put - Add generic netlink header to netlink message
 * @skb: socket buffer holding the message
 * @pid: netlink pid the message is addressed to
 * @seq: sequence number (usually the one of the sender)
 * @family: generic netlink family
 * @flags netlink message flags
 * @cmd: generic netlink command
 *
 * Returns pointer to user specific header
 */
static inline void *genlmsg_put(struct sk_buff *skb, u32 pid, u32 seq,
				                       struct genl_family *family, int flags, u8 cmd)
{
	struct nlmsghdr *nlh;
	struct genlmsghdr *hdr;

	nlh = nlmsg_put(skb, pid, seq, family->id, GENL_HDRLEN +
			family->hdrsize, flags);
	if (nlh == NULL)
		return NULL;

	hdr = nlmsg_data(nlh);
	hdr->cmd = cmd;
	hdr->version = family->version;
	hdr->reserved = 0;

	return (char *) hdr + GENL_HDRLEN;
}

/**
 * genlmsg_put_reply - Add generic netlink header to a reply message
 * @skb: socket buffer holding the message
 * @info: receiver info
 * @family: generic netlink family
 * @flags: netlink message flags
 * @cmd: generic netlink command
 *
 * Returns pointer to user specific header
 */
static inline void *genlmsg_put_reply(struct sk_buff *skb,
				      struct genl_info *info,
				      struct genl_family *family,
				      int flags, u8 cmd)
{
	return genlmsg_put(skb, info->snd_pid, info->snd_seq, family,
			   flags, cmd);
}

/**
 * genlmsg_end - Finalize a generic netlink message
 * @skb: socket buffer the message is stored in
 * @hdr: user specific header
 */
static inline int genlmsg_end(struct sk_buff *skb, void *hdr)
{
	return nlmsg_end(skb, hdr - GENL_HDRLEN - NLMSG_HDRLEN);
}

/**
 * genlmsg_cancel - Cancel construction of a generic netlink message
 * @skb: socket buffer the message is stored in
 * @hdr: generic netlink message header
 */
static inline void genlmsg_cancel(struct sk_buff *skb, void *hdr)
{
	nlmsg_cancel(skb, hdr - GENL_HDRLEN - NLMSG_HDRLEN);
}

/**
 * genlmsg_multicast_netns - multicast a netlink message to a specific netns
 * @net: the net namespace
 * @skb: netlink message as socket buffer
 * @pid: own netlink pid to avoid sending to yourself
 * @group: multicast group id
 * @flags: allocation flags
 */
static inline int genlmsg_multicast_netns(struct net *net, struct sk_buff *skb,
					  u32 pid, unsigned int group, gfp_t flags)
{
	return nlmsg_multicast(net->genl_sock, skb, pid, group, flags);
}

/**
 * genlmsg_multicast - multicast a netlink message to the default netns
 * @skb: netlink message as socket buffer
 * @pid: own netlink pid to avoid sending to yourself
 * @group: multicast group id
 * @flags: allocation flags
 */
static inline int genlmsg_multicast(struct sk_buff *skb, u32 pid,
				                              unsigned int group, gfp_t flags)
{
	return genlmsg_multicast_netns(&init_net, skb, pid, group, flags);
}

/**
 * genlmsg_multicast_allns - multicast a netlink message to all net namespaces
 * @skb: netlink message as socket buffer
 * @pid: own netlink pid to avoid sending to yourself
 * @group: multicast group id
 * @flags: allocation flags
 *
 * This function must hold the RTNL or rcu_read_lock().
 */
int genlmsg_multicast_allns(struct sk_buff *skb, u32 pid,
			                              unsigned int group, gfp_t flags);

/**
 * genlmsg_unicast - unicast a netlink message
 * @skb: netlink message as socket buffer
 * @pid: netlink pid of the destination socket
 */
static inline int genlmsg_unicast(struct net *net, struct sk_buff *skb, u32 pid)
{
	return nlmsg_unicast(net->genl_sock, skb, pid);
}

/**
 * genlmsg_reply - reply to a request
 * @skb: netlink message to be sent back
 * @info: receiver information
 */
static inline int genlmsg_reply(struct sk_buff *skb, struct genl_info *info)
{
	return genlmsg_unicast(genl_info_net(info), skb, info->snd_pid);
}

/**
 * gennlmsg_data - head of message payload
 * @gnlh: genetlink messsage header
 */
static inline void *genlmsg_data(const struct genlmsghdr *gnlh)
{
	return ((unsigned char *) gnlh + GENL_HDRLEN);
}

/**
 * genlmsg_len - length of message payload
 * @gnlh: genetlink message header
 */
static inline int genlmsg_len(const struct genlmsghdr *gnlh)
{
	struct nlmsghdr *nlh = (struct nlmsghdr *)((unsigned char *)gnlh -
							NLMSG_HDRLEN);
	return (nlh->nlmsg_len - GENL_HDRLEN - NLMSG_HDRLEN);
}

/**
 * genlmsg_msg_size - length of genetlink message not including padding
 * @payload: length of message payload
 */
static inline int genlmsg_msg_size(int payload)
{
	return GENL_HDRLEN + payload;
}

/**
 * genlmsg_total_size - length of genetlink message including padding
 * @payload: length of message payload
 */
static inline int genlmsg_total_size(int payload)
{
	return NLMSG_ALIGN(genlmsg_msg_size(payload));
}

/**
 * genlmsg_new - Allocate a new generic netlink message
 * @payload: size of the message payload
 * @flags: the type of memory to allocate.
 */
static inline struct sk_buff *genlmsg_new(size_t payload, gfp_t flags)
{
	return nlmsg_new(genlmsg_total_size(payload), flags);
}


#endif	/* __NET_GENERIC_NETLINK_H */
