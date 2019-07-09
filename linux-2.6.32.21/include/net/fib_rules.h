#ifndef __NET_FIB_RULES_H
#define __NET_FIB_RULES_H

#include <linux/types.h>
#include <linux/netdevice.h>
#include <linux/fib_rules.h>
#include <net/flow.h>
#include <net/rtnetlink.h>

//���Թ����Ӧ�����ݽṹ
//����ipv4��Ϊfib4_rule
struct fib_rule
{
	struct list_head	list; // ��������·�ɹ�����������(fib_rules_ops,�������)  
	atomic_t		refcnt;   // ������ 
	int			ifindex;      // �����豸�ӿ�����
	char			ifname[IFNAMSIZ];// �豸���� 
	u32			mark;         // ���ڹ�������
	u32			mark_mask;    // ���� 
	u32			pref;         // ���ȼ�(������������зֱ���0,0x7FEE��0x7FFF) 
	u32			flags;        // ��ʶλ 
	u32			table;        // ·�ɺ�����id(���籾��LOCAL����·��MAIN...)  

	u8			action;       // ����������ôȥ����������ݰ� FR_ACT_TO_TBL  
	                          //��ƥ���Ĺ��򣬼�����뵽��Ӧ��·�ɱ���������·�����ƥ��  
	u32			target;
	struct fib_rule *	ctarget;// ��ǰ����
	struct rcu_head		rcu;
	struct net *		fr_net; // ����ռ�ṹָ�� 
};

struct fib_lookup_arg
{
	void			*lookup_ptr;
	void			*result;
	struct fib_rule		*rule;
};

struct fib_rules_ops
{
	int			family; // Э����ID  
	struct list_head	list; /*��Ҫ�ǽ�ע�ᵽϵͳ��fib_rules_ops���ӵ�����rules_ops��*/
	int			rule_size;  /*һ�����Թ�����ռ�õ��ڴ��С*/
	int			addr_size;  /*Э����صĵ�ַ�ĳ���*/
	int			unresolved_rules;
	int			nr_goto_rules;

    /*Э����ص�action���������ǲ��Թ���ƥ��������õ�action������ִ�к����Ĳ�����
    һ���ǻ�ȡ����Ӧ��·�ɱ����ҷ���Ҫ���·����*/
	int			(*action)(struct fib_rule *,
					  struct flowi *, int,
					  struct fib_lookup_arg *);
	
    /*Э����صĹ���ƥ�亯�������ڲ��Թ����ƥ�䣬������ͨ��ƥ�䣬
    ��ͨ��ƥ����ɺ������øú���������Э����ز�����Դ��Ŀ�ĵ�ַ�ȣ���ƥ��*/  	
	int			(*match)(struct fib_rule *,
					 struct flowi *, int);

	/*Э����ص����ú���*/
	int			(*configure)(struct fib_rule *,
					     struct sk_buff *,
					     struct fib_rule_hdr *,
					     struct nlattr **);
	
	int			(*compare)(struct fib_rule *,
					   struct fib_rule_hdr *,
					   struct nlattr **);// �ԱȺ���ָ�� 
	int			(*fill)(struct fib_rule *, struct sk_buff *,
					struct fib_rule_hdr *); // ��д����ָ��  
	u32			(*default_pref)(struct fib_rules_ops *ops);// �������ȼ�����ָ��
	size_t			(*nlmsg_payload)(struct fib_rule *);// ͳ�Ƹ���������������ָ�� 

	/* Called after modifications to the rules set, must flush
	 * the route cache if one exists. */
	void			(*flush_cache)(struct fib_rules_ops *ops); // �޸Ĺ���֮��ˢ�»��溯��ָ��

	int			nlgroup; // ·��netlink�黮�ֱ�ʶ
	const struct nla_policy	*policy; // netlink�������ȼ� 
	struct list_head	rules_list;  // ·�ɹ������ 
	struct module		*owner;
	struct net		*fro_net;// ����ռ�ṹָ��
};

#define FRA_GENERIC_POLICY \
	[FRA_IFNAME]	= { .type = NLA_STRING, .len = IFNAMSIZ - 1 }, \
	[FRA_PRIORITY]	= { .type = NLA_U32 }, \
	[FRA_FWMARK]	= { .type = NLA_U32 }, \
	[FRA_FWMASK]	= { .type = NLA_U32 }, \
	[FRA_TABLE]     = { .type = NLA_U32 }, \
	[FRA_GOTO]	= { .type = NLA_U32 }

static inline void fib_rule_get(struct fib_rule *rule)
{
	atomic_inc(&rule->refcnt);
}

static inline void fib_rule_put_rcu(struct rcu_head *head)
{
	struct fib_rule *rule = container_of(head, struct fib_rule, rcu);
	release_net(rule->fr_net);
	kfree(rule);
}

static inline void fib_rule_put(struct fib_rule *rule)
{
	if (atomic_dec_and_test(&rule->refcnt))
		call_rcu(&rule->rcu, fib_rule_put_rcu);
}

static inline u32 frh_get_table(struct fib_rule_hdr *frh, struct nlattr **nla)
{
	if (nla[FRA_TABLE])
		return nla_get_u32(nla[FRA_TABLE]);
	return frh->table;
}

extern int fib_rules_register(struct fib_rules_ops *);
extern void fib_rules_unregister(struct fib_rules_ops *);
extern void fib_rules_cleanup_ops(struct fib_rules_ops *);

extern int	fib_rules_lookup(struct fib_rules_ops *,
						               struct flowi *, int flags,
						               struct fib_lookup_arg *);
extern int  fib_default_rule_add(struct fib_rules_ops *,
						                     u32 pref, u32 table,
						                     u32 flags);
#endif
