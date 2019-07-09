/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		The IP fragmentation functionality.
 *
 * Authors:	Fred N. van Kempen <waltje@uWalt.NL.Mugnet.ORG>
 *		Alan Cox <alan@lxorguk.ukuu.org.uk>
 *
 * Fixes:
 *		Alan Cox	:	Split from ip.c , see ip_input.c for history.
 *		David S. Miller :	Begin massive cleanup...
 *		Andi Kleen	:	Add sysctls.
 *		xxxx		:	Overlapfrag bug.
 *		Ultima          :       ip_expire() kernel panic.
 *		Bill Hawes	:	Frag accounting and evictor fixes.
 *		John McDonald	:	0 length frag bug.
 *		Alexey Kuznetsov:	SMP races, threading, cleanup.
 *		Patrick McHardy :	LRU queue of frag heads for evictor.
 */

#include <linux/compiler.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/jiffies.h>
#include <linux/skbuff.h>
#include <linux/list.h>
#include <linux/ip.h>
#include <linux/icmp.h>
#include <linux/netdevice.h>
#include <linux/jhash.h>
#include <linux/random.h>
#include <net/sock.h>
#include <net/ip.h>
#include <net/icmp.h>
#include <net/checksum.h>
#include <net/inetpeer.h>
#include <net/inet_frag.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/inet.h>
#include <linux/netfilter_ipv4.h>

/* NOTE. Logic of IP defragmentation is parallel to corresponding IPv6
 * code now. If you change something here, _PLEASE_ update ipv6/reassembly.c
 * as well. Or notify me, at least. --ANK
 */

static int sysctl_ipfrag_max_dist __read_mostly = 64;

struct ipfrag_skb_cb
{
	struct inet_skb_parm	h;
	int			offset;
};
//������skb�������ipfrag_skb_cb
#define FRAG_CB(skb)	((struct ipfrag_skb_cb *)((skb)->cb))

/* Describe an entry in the "incomplete datagrams" queue. */
//ip����ķֶμ�
struct ipq {
	struct inet_frag_queue q;

	u32		user;
	__be32		saddr;
	__be32		daddr;
	__be16		id;
	u8		protocol;
	int             iif; //�������ݵ�������index
	unsigned int    rid;
	struct inet_peer *peer;
};

static struct inet_frags ip4_frags;

int ip_frag_nqueues(struct net *net)
{
	return net->ipv4.frags.nqueues;
}

int ip_frag_mem(struct net *net)
{
	return atomic_read(&net->ipv4.frags.mem);
}

static int ip_frag_reasm(struct ipq *qp, struct sk_buff *prev,
			 struct net_device *dev);

struct ip4_create_arg {
	struct iphdr *iph;
	u32 user;
};

static unsigned int ipqhashfn(__be16 id,  //���ݶ�ID 
	                               __be32 saddr, //Դ��ַ
	                               __be32 daddr, //Ŀ���ַ
	                               u8 prot)   //ipͷ�е�Э���ֶ�
{
	return jhash_3words((__force u32)id << 16 | prot,
			    (__force u32)saddr, (__force u32)daddr,
			    ip4_frags.rnd) & (INETFRAGS_HASHSZ - 1);
}

static unsigned int ip4_hashfn(struct inet_frag_queue *q)
{
	struct ipq *ipq;

	ipq = container_of(q, struct ipq, q);
	return ipqhashfn(ipq->id, ipq->saddr, ipq->daddr, ipq->protocol);
}

static int ip4_frag_match(struct inet_frag_queue *q, void *a)
{
	struct ipq *qp;
	struct ip4_create_arg *arg = a;

	qp = container_of(q, struct ipq, q);
	return (qp->id == arg->iph->id &&
			qp->saddr == arg->iph->saddr &&
			qp->daddr == arg->iph->daddr &&
			qp->protocol == arg->iph->protocol &&
			qp->user == arg->user);
}

/* Memory Tracking Functions. */
static __inline__ void frag_kfree_skb(struct netns_frags *nf,
		struct sk_buff *skb, int *work)
{
	if (work)
		*work -= skb->truesize;
	atomic_sub(skb->truesize, &nf->mem);
	kfree_skb(skb);
}

static void ip4_frag_init(struct inet_frag_queue *q, void *a)
{
	struct ipq *qp = container_of(q, struct ipq, q);
	struct ip4_create_arg *arg = a;

	qp->protocol = arg->iph->protocol;
	qp->id = arg->iph->id;
	qp->saddr = arg->iph->saddr;
	qp->daddr = arg->iph->daddr;
	qp->user = arg->user;
	qp->peer = sysctl_ipfrag_max_dist ?
		inet_getpeer(arg->iph->saddr, 1) : NULL;
}

static __inline__ void ip4_frag_free(struct inet_frag_queue *q)
{
	struct ipq *qp;

	qp = container_of(q, struct ipq, q);
	if (qp->peer)
		inet_putpeer(qp->peer);
}


/* Destruction primitives. */
//�ݼ���������ipq�ṹ�����ü���ֵ
static __inline__ void ipq_put(struct ipq *ipq)
{
	inet_frag_put(&ipq->q, &ip4_frags);
}

/* Kill ipq entry. It is not destroyed immediately,
 * because caller (and someone more) holds reference count.
 */
 //��һ��ipq���Ϊ��ɾ��
static void ipq_kill(struct ipq *ipq)
{
	inet_frag_kill(&ipq->q, &ip4_frags);
}

/* Memory limiting on fragments.  Evictor trashes the oldest
 * fragment queue until we are back under the threshold.
 */
 //��һɾ�������������ipq�ṹ
//����ɵĿ�ʼ ֱ��Ƭ�����õ��ڴ潵��NET_IPV4_IPFRAG_LOW_THRESH��ֵ
static void ip_evictor(struct net *net)
{
	int evicted;

	evicted = inet_frag_evictor(&net->ipv4.frags, &ip4_frags);
	if (evicted)
		IP_ADD_STATS_BH(net, IPSTATS_MIB_REASMFAILS, evicted);
}

/*
 * Oops, a fragment queue timed out.  Kill it and send an ICMP reply.
 */
static void ip_expire(unsigned long arg)
{
	struct ipq *qp;
	struct net *net;

	qp = container_of((struct inet_frag_queue *) arg, struct ipq, q);
	net = container_of(qp->q.net, struct net, ipv4.frags);

	spin_lock(&qp->q.lock);

	if (qp->q.last_in & INET_FRAG_COMPLETE)
		goto out;

	ipq_kill(qp);

	IP_INC_STATS_BH(net, IPSTATS_MIB_REASMTIMEOUT);
	IP_INC_STATS_BH(net, IPSTATS_MIB_REASMFAILS);

	if ((qp->q.last_in & INET_FRAG_FIRST_IN) && qp->q.fragments != NULL) {
		struct sk_buff *head = qp->q.fragments;

		/* Send an ICMP "Fragment Reassembly Timeout" message. */
		if ((head->dev = dev_get_by_index(net, qp->iif)) != NULL) 
		{
		    //���ͷ�Ƭ��ʱ ĳ�����ݰ������з�Ƭ�ڹ涨ʱ��δ��ȫ����
			icmp_send(head, ICMP_TIME_EXCEEDED, ICMP_EXC_FRAGTIME, 0);
			dev_put(head->dev);
		}
	}
out:
	spin_unlock(&qp->q.lock);
	ipq_put(qp);
}

/* Find the correct entry in the "incomplete datagrams" queue for
 * this IP datagram, and create new one, if nothing is found.
 */
 //�ҵ������ڱ������Ƭ����صķ�� Ƭ������
static inline struct ipq *ip_find(struct net *net, struct iphdr *iph, u32 user)
{
	struct inet_frag_queue *q;
	struct ip4_create_arg arg;
	unsigned int hash;

	arg.iph = iph;
	arg.user = user;

	read_lock(&ip4_frags.lock);
	hash = ipqhashfn(iph->id, iph->saddr, iph->daddr, iph->protocol); //����ԴĿ��IP,id,Э��������ϣֵ

	q = inet_frag_find(&net->ipv4.frags, &ip4_frags, &arg, hash);
	if (q == NULL)
		goto out_nomem;

	return container_of(q, struct ipq, q);

out_nomem:
	LIMIT_NETDEBUG(KERN_ERR "ip_frag_create: no memory left !\n");
	return NULL;
}

/* Is the fragment too far ahead to be part of ipq? */
static inline int ip_frag_too_far(struct ipq *qp)
{
	struct inet_peer *peer = qp->peer;
	unsigned int max = sysctl_ipfrag_max_dist;
	unsigned int start, end;

	int rc;

	if (!peer || !max)
		return 0;

	start = qp->rid;
	end = atomic_inc_return(&peer->rid);
	qp->rid = end;

	rc = qp->q.fragments && (end - start) > max;

	if (rc) {
		struct net *net;

		net = container_of(qp->q.net, struct net, ipv4.frags);
		IP_INC_STATS_BH(net, IPSTATS_MIB_REASMFAILS);
	}

	return rc;
}

static int ip_frag_reinit(struct ipq *qp)
{
	struct sk_buff *fp;

	if (!mod_timer(&qp->q.timer, jiffies + qp->q.net->timeout)) {
		atomic_inc(&qp->q.refcnt);
		return -ETIMEDOUT;
	}

	fp = qp->q.fragments;
	do {
		struct sk_buff *xp = fp->next;
		frag_kfree_skb(qp->q.net, fp, NULL);
		fp = xp;
	} while (fp);

	qp->q.last_in = 0;
	qp->q.len = 0;
	qp->q.meat = 0;
	qp->q.fragments = NULL;
	qp->iif = 0;

	return 0;
}

/* Add new segment to existing queue. */
//��ָ��Ƭ�β����ͬһ�������ص�Ƭ��������
static int ip_frag_queue(struct ipq *qp, struct sk_buff *skb)
{
	struct sk_buff *prev, *next;
	struct net_device *dev;
	int flags, offset;
	int ihl, end;
	int err = -ENOENT;

	//��Ƭ�����Ѿ����
	if (qp->q.last_in & INET_FRAG_COMPLETE)
		goto err;

   /*
    IPCB(skb)->flagsֻ���ڱ�������IPv4��Ƭʱ����λ����ô����ļ��Ӧ����Ԥ���յ������Լ�������IP��Ƭ�� 
   */	
	if (!(IPCB(skb)->flags & IPSKB_FRAG_COMPLETE) &&//���skb�еķ�Ƭ��־ 
	
    //����ip_frag_too_far���ú�����Ҫ��֤������ͬһ��peer����ͬ��Դ��ַ������ռ�ù����IP��Ƭ����
    unlikely(ip_frag_too_far(qp)) &&

    //���³�ʼ���ö���
    unlikely(err = ip_frag_reinit(qp))) 
    {
	     ipq_kill(qp);
	     goto err;
    } 
	
    //������Ƭ��ƫ�����ͱ�־
	offset = ntohs(ip_hdr(skb)->frag_off);
	flags = offset & ~IP_OFFSET;
	offset &= IP_OFFSET;
	offset <<= 3;		/* offset is in 8-byte chunks */

	//���ipͷ�ĳ���
	ihl = ip_hdrlen(skb);

	/* Determine the position of this fragment. */
	//��ñ����ܵĳ���
	end = offset + skb->len - ihl;
	err = -EINVAL;

	//��������һ����Ƭ
	if ((flags & IP_MF) == 0) 
	{
		/* If we already have some bits beyond end
		 * or have different end, the segment is corrrupted.
		 */
		//ĩ��С��֮ǰ��õ��ܳ���  ��������� 
		//֮ǰ�Ѿ��յ���һ������Ƭ��������жϵ�ĩ�˲�����֮ǰ��õ�ֵ �����
		if (end < qp->q.len ||
		    ((qp->q.last_in & INET_FRAG_LAST_IN) && end != qp->q.len))
			goto err;

		qp->q.last_in |= INET_FRAG_LAST_IN; //��־�������һ����Ƭ
		qp->q.len = end;//�����ܳ���
	} 
	else //�������һ����Ƭ
    {
        //����Ƿ�8�ֽڶ���
		if (end&7) 
		{
		    //�����һ����Ƭ�� �����Ƭ��Ϊ8�ֽڶ���
			end &= ~7;
			if (skb->ip_summed != CHECKSUM_UNNECESSARY)//û����������У��ͼ���
				skb->ip_summed = CHECKSUM_NONE;//������Լ�����У���
		}

		//�˷�Ƭβ���Ƿ񳬹�֮ǰ��õı����ܳ���
		if (end > qp->q.len) 
		{
			/* Some bits beyond end -> corruption. */
			if (qp->q.last_in & INET_FRAG_LAST_IN)//���һ����Ƭ�Ѿ����ܵ� �����ķ�Ƭ�Ǵ����
				goto err;
			qp->q.len = end;//���±��ĵ��ܳ���
		}
	}
	//��ʾ�յ�ip��Ƭ
	/*
      ����ipЭ�鶨�� ip��ͷ���ܱ��ֶΣ����һ��������ֶ� ��Ч�غ�һ����Ϊ��,���û����Ч�غ�
      ��Ƭ�ξ�û������
	*/
	if (end == offset)
		goto err;

	err = -ENOMEM;
	//��skb->data�Ƶ�ip�ײ��Ժ�
	if (pskb_pull(skb, ihl) == NULL)
		goto err;

	err = pskb_trim_rcsum(skb, end - offset);
	if (err)
		goto err;

	/* Find out which fragments are in front and at the back of us
	 * in the chain of fragments so far.  We must know where to put
	 * this fragment, right?
	 */
	//����ipq�е�fragments����ÿ��skb��cb��Ա��¼�ŵ�ǰskb����ip�����
    //ʱ������Ҫ��ƫ�ƣ�fragments�е�skb���ǰ���offset�����źõģ����ԣ��ҵ�
    //��һ��offset���ڵ�ǰIP��ƫ�Ƶ����ݰ��Ϳ�����
	prev = NULL;
	for (next = qp->q.fragments; next != NULL; next = next->next) 
	{
		if (FRAG_CB(next)->offset >= offset)
			break;	/* bingo! */
		prev = next;
	}

	/* We found where to put this one.  Check for overlap with
	 * preceding fragment, and, if needed, align things so that
	 * any overlaps are eliminated.
	 */
	//�������ѭ�����ҵ��˲����λ�� �����ܷ����ص�,
	if (prev) 
	{
	    //��û�����ص� ����Ȼoffset�Ĵ�С Ӧ����FRAG_CB(prev)->offset + prev->len ֮��Ҳ����iֵ��С��0
	    /*���������ص� offset�Ĵ�С ��FRAG_CB(prev)->offset��FRAG_CB(prev)->offset + prev->len֮��
          ��i��ֵ�����0
	    
         �������ѭ����֪,�˷�ƬӦ�ò��뵽prevָ���λ�ú������ prev��offset=100 len=150 
         ���˷�Ƭ��offset��ֵ ��100+150�ĺ���  iֵ�ͻ�С��0 û���ص�����
         ���˷�Ƭ��offset��ֵ ��100,350֮�� i��ֵ�����0  �������ص�
                                             ����λ��   ǰ�ص�
                                             \|/ 
       ------------------------------------------------------
         ��Ƭ1   |  (���Ƭ2�����ص�)��Ƭ2    |   ��Ƭ3     |
       ------------------------------------------------------
		*/
		int i = (FRAG_CB(prev)->offset + prev->len) - offset;

		if (i > 0)//�������ص� 
		{
			offset += i; //��ƫ������ƶ�i �ܵ�prev������������ұ�
			err = -EINVAL;
			if (end <= offset) //�˷�Ƭ�ƶ����offset�Ƿ񳬹��˴˷�Ƭ����󳤶�
				goto err;//ƫ�Ƴ����˷�Ƭ����󳤶� ����
			err = -ENOMEM;
			
			if (!pskb_pull(skb, i))//��skb������ָ�����i���ֽ� �ı�ƫ��ֵ
				goto err;
			if (skb->ip_summed != CHECKSUM_UNNECESSARY)//û����������У��ͼ���
				skb->ip_summed = CHECKSUM_NONE;//������Լ�����У���
		}
	}

	err = -ENOMEM;
    //�˷�Ƭ������д��ڵķ�Ƭ����������ص�  ���ص�
    /*                      ����λ��
                            \|/
       ------------------------------------------------------------------------
         ��Ƭ1   |  ��Ƭ2    |  (�ص����Ƭ3��������ķ�Ƭ�����ص�) ��Ƭ3     |
       ------------------------------------------------------------------------     
     */
    //�˷�Ƭ������ĳ��� ���ں����Ƭ����ʼλ�� �������ص�
	while (next && FRAG_CB(next)->offset < end) 
	{
	    //�����ص����ֽ���
		int i = end - FRAG_CB(next)->offset; /* overlap is 'i' bytes */
	 
		if (i < next->len)//�Ƿ�ֻ�ص��˴�next��Ƭ 
		{
			/* Eat head of the next overlapped fragment
			 * and leave the loop. The next ones cannot overlap.
			 */
			//�����ǵ�i���ֽ���next��Ƭ���޳�
			//��dataָ�����i���ֽ�
			if (!pskb_pull(next, i))
				goto err;
			FRAG_CB(next)->offset += i;//����next�µ�ƫ�� �����ƶ�i���ֽ�
			qp->q.meat -= i;
			if (next->ip_summed != CHECKSUM_UNNECESSARY)//û����������У��ͼ���
				next->ip_summed = CHECKSUM_NONE;//������Լ�����У���
			break;
		} 
		else 
	    {
	        //�����˺����������Ƭ �򽫸��ǵķ�Ƭ�ͷŵ�
			struct sk_buff *free_it = next;

			/* Old fragment is completely overridden with
			 * new one drop it.
			 */
			next = next->next;

			if (prev)
				prev->next = next;
			else
				qp->q.fragments = next;

			qp->q.meat -= free_it->len;
            //�ͷŸ��ǵ�������Ƭ
			frag_kfree_skb(qp->q.net, free_it, NULL);
		}
	}

	//�����µ�ƫ��ֵ
	FRAG_CB(skb)->offset = offset;

	/* Insert this fragment in the chain of fragments. */
    //��skb���뵽������
	skb->next = next;
	if (prev)
		prev->next = skb;
	else
		qp->q.fragments = skb;

	dev = skb->dev;
	if (dev) 
	{
		qp->iif = dev->ifindex;//�����豸��
		skb->dev = NULL;
	}
	qp->q.stamp = skb->tstamp;//��¼��������յ�ʱ���
	qp->q.meat += skb->len; //�����Ѿ���õı��ĵĳ���
	atomic_add(skb->truesize, &qp->q.net->mem);

	//˵���ǵ�һ����Ƭ ���õ�һ����Ƭ�ı�־
	if (offset == 0)
		qp->q.last_in |= INET_FRAG_FIRST_IN;

	//����յ��˵�һ����Ƭ�����һ����Ƭ ���ұ��ĳ���ҲҲ����ԭʼ��ip���ĳ���
	//������еķ�Ƭ������װ
	if (qp->q.last_in == (INET_FRAG_FIRST_IN | INET_FRAG_LAST_IN) &&
	    qp->q.meat == qp->q.len)//qp->q.len ȫ���ֶε��ܳ���
		return ip_frag_reasm(qp, prev, dev);//�������еķֶδ���һ���µ����ݰ�

	//ip��Ƭ��δ��ȫ����
	write_lock(&ip4_frags.lock);
	list_move_tail(&qp->q.lru_list, &qp->q.net->lru_list);
	write_unlock(&ip4_frags.lock);
	return -EINPROGRESS;

err:
	kfree_skb(skb);
	return err;
}


/* Build a new IP datagram from all its fragments. */
/*�������еķֶδ���һ���µ����ݰ�
�˴�����һ����ʱ�� �ڶ�ʱ�����ں� ���еİ�δ��װ��ɻᷢ��ICMP��װ��ʱ�Ĵ�����ϢICMP_EXC_FRAGTIME
�Գ�ʱ��������������ipfrag_init�н��г�ʼ��  ��ʱ����Ϊip_expire Ĭ��ʱ��Ϊ30�����ͨ��/proc/sys/net/ipv4/ipfrag_time 
������*/
static int ip_frag_reasm(struct ipq *qp, 
                                 struct sk_buff *prev,
			                     struct net_device *dev)
{
    struct iphdr *iph;
	struct net *net = container_of(qp->q.net, struct net, ipv4.frags);
	struct sk_buff *fp, *head = qp->q.fragments;
	int len;
	int ihlen;
	int err;

	//һ���ֹ�������ֹͣ��װ��ʱ�Ķ�ʱ��
	ipq_kill(qp);

	/* Make the one we just received the head. */
	if (prev) 
	{
		head = prev->next;
		fp = skb_clone(head, GFP_ATOMIC);
		if (!fp)
			goto out_nomem;

		fp->next = head->next;
		prev->next = fp;

		skb_morph(head, qp->q.fragments);
		head->next = qp->q.fragments->next;

		kfree_skb(qp->q.fragments);
		qp->q.fragments = head;
	}

	WARN_ON(head == NULL);
	WARN_ON(FRAG_CB(head)->offset != 0);

	/* Allocate a new buffer for the datagram. */
	ihlen = ip_hdrlen(head);
	len = ihlen + qp->q.len;

	err = -E2BIG;
	if (len > 65535)
		goto out_oversize;

	/* Head of list must not be cloned. */
	if (skb_cloned(head) && pskb_expand_head(head, 0, 0, GFP_ATOMIC))
		goto out_nomem;

	/* If the first fragment is fragmented itself, we split
	 * it to two chunks: the first with data and paged part
	 * and the second, holding only fragments. */
	if (skb_has_frags(head)) {
		struct sk_buff *clone;
		int i, plen = 0;

		if ((clone = alloc_skb(0, GFP_ATOMIC)) == NULL)
			goto out_nomem;
		clone->next = head->next;
		head->next = clone;
		skb_shinfo(clone)->frag_list = skb_shinfo(head)->frag_list;
		skb_frag_list_init(head);
		for (i=0; i<skb_shinfo(head)->nr_frags; i++)
			plen += skb_shinfo(head)->frags[i].size;
		clone->len = clone->data_len = head->data_len - plen;
		head->data_len -= clone->len;
		head->len -= clone->len;
		clone->csum = 0;
		clone->ip_summed = head->ip_summed;
		atomic_add(clone->truesize, &qp->q.net->mem);
	}

	skb_shinfo(head)->frag_list = head->next;
	skb_push(head, head->data - skb_network_header(head));
	atomic_sub(head->truesize, &qp->q.net->mem);

	for (fp=head->next; fp; fp = fp->next) {
		head->data_len += fp->len;
		head->len += fp->len;
		if (head->ip_summed != fp->ip_summed)
			head->ip_summed = CHECKSUM_NONE;
		else if (head->ip_summed == CHECKSUM_COMPLETE)
			head->csum = csum_add(head->csum, fp->csum);
		head->truesize += fp->truesize;
		atomic_sub(fp->truesize, &qp->q.net->mem);
	}

	head->next = NULL;
	head->dev = dev;
	head->tstamp = qp->q.stamp;

	iph = ip_hdr(head);
	iph->frag_off = 0;
	iph->tot_len = htons(len);
	IP_INC_STATS_BH(net, IPSTATS_MIB_REASMOKS);
	qp->q.fragments = NULL;
	return 0;

out_nomem:
	LIMIT_NETDEBUG(KERN_ERR "IP: queue_glue: no memory for gluing "
			      "queue %p\n", qp);
	err = -ENOMEM;
	goto out_fail;
out_oversize:
	if (net_ratelimit())
		printk(KERN_INFO "Oversized IP packet from %pI4.\n",
			&qp->saddr);
out_fail:
	IP_INC_STATS_BH(net, IPSTATS_MIB_REASMFAILS);
	return err;
}

/* Process an incoming IP datagram fragment. */
//���麯���ǻ���ipq����ɢ�б� ɢ�к���ipqhash()
int ip_defrag(struct sk_buff *skb, u32 user)
{
	struct ipq *qp;
	struct net *net;

	//��������ڵ����������ռ�
	net = skb->dev ? dev_net(skb->dev) : dev_net(skb_dst(skb)->dev);

	IP_INC_STATS_BH(net, IPSTATS_MIB_REASMREQDS);

	
    /*�鿴�Ѿ�Ϊ��Ƭ������ڴ��Ƿ��Ѿ����������ȶ�������ޣ�
      sysctl_ipfrag_high_thresh��ʼ�����£�
      int sysctl_ipfrag_high_thresh __read_mostly = 256*1024,װ����Ƭ���ڴ�������256K*/
	if (atomic_read(&net->ipv4.frags.mem) > net->ipv4.frags.high_thresh)
		ip_evictor(net); //���ڴ��������

	//���һ򴴽����Ӧ�Ķ�ͷ
	if ((qp = ip_find(net, ip_hdr(skb), user)) != NULL) 
	{
		int ret;
		spin_lock(&qp->q.lock);

		//���ֶμ��뵽һ���ֶ�������qp->q.fragments,��������Ǹ���ƫ�����������
		ret = ip_frag_queue(qp, skb);

		spin_unlock(&qp->q.lock);
		
		ipq_put(qp);
		return ret;
	}

	IP_INC_STATS_BH(net, IPSTATS_MIB_REASMFAILS);
	kfree_skb(skb);
	return -ENOMEM;
}

#ifdef CONFIG_SYSCTL
static int zero;

static struct ctl_table ip4_frags_ns_ctl_table[] = {
	{
		.ctl_name	= NET_IPV4_IPFRAG_HIGH_THRESH,
		.procname	= "ipfrag_high_thresh",
		.data		= &init_net.ipv4.frags.high_thresh,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.ctl_name	= NET_IPV4_IPFRAG_LOW_THRESH,
		.procname	= "ipfrag_low_thresh",
		.data		= &init_net.ipv4.frags.low_thresh,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.ctl_name	= NET_IPV4_IPFRAG_TIME,
		.procname	= "ipfrag_time",
		.data		= &init_net.ipv4.frags.timeout,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_jiffies,
		.strategy	= sysctl_jiffies
	},
	{ }
};

static struct ctl_table ip4_frags_ctl_table[] = {
	{
		.ctl_name	= NET_IPV4_IPFRAG_SECRET_INTERVAL,
		.procname	= "ipfrag_secret_interval",
		.data		= &ip4_frags.secret_interval,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_jiffies,
		.strategy	= sysctl_jiffies
	},
	{
		.procname	= "ipfrag_max_dist",
		.data		= &sysctl_ipfrag_max_dist,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &zero
	},
	{ }
};

static int ip4_frags_ns_ctl_register(struct net *net)
{
	struct ctl_table *table;
	struct ctl_table_header *hdr;

	table = ip4_frags_ns_ctl_table;
	if (net != &init_net) {
		table = kmemdup(table, sizeof(ip4_frags_ns_ctl_table), GFP_KERNEL);
		if (table == NULL)
			goto err_alloc;

		table[0].data = &net->ipv4.frags.high_thresh;
		table[1].data = &net->ipv4.frags.low_thresh;
		table[2].data = &net->ipv4.frags.timeout;
	}

	hdr = register_net_sysctl_table(net, net_ipv4_ctl_path, table);
	if (hdr == NULL)
		goto err_reg;

	net->ipv4.frags_hdr = hdr;
	return 0;

err_reg:
	if (net != &init_net)
		kfree(table);
err_alloc:
	return -ENOMEM;
}

static void ip4_frags_ns_ctl_unregister(struct net *net)
{
	struct ctl_table *table;

	table = net->ipv4.frags_hdr->ctl_table_arg;
	unregister_net_sysctl_table(net->ipv4.frags_hdr);
	kfree(table);
}

static void ip4_frags_ctl_register(void)
{
	register_net_sysctl_rotable(net_ipv4_ctl_path, ip4_frags_ctl_table);
}
#else
static inline int ip4_frags_ns_ctl_register(struct net *net)
{
	return 0;
}

static inline void ip4_frags_ns_ctl_unregister(struct net *net)
{
}

static inline void ip4_frags_ctl_register(void)
{
}
#endif

static int ipv4_frags_init_net(struct net *net)
{
	/*
	 * Fragment cache limits. We will commit 256K at one time. Should we
	 * cross that limit we will prune down to 192K. This should cope with
	 * even the most extreme cases without allowing an attacker to
	 * measurably harm machine performance.
	 */
	net->ipv4.frags.high_thresh = 256 * 1024;
	net->ipv4.frags.low_thresh = 192 * 1024;
	/*
	 * Important NOTE! Fragment queue must be destroyed before MSL expires.
	 * RFC791 is wrong proposing to prolongate timer each fragment arrival
	 * by TTL.
	 */
	net->ipv4.frags.timeout = IP_FRAG_TIME;

	inet_frags_init_net(&net->ipv4.frags);

	return ip4_frags_ns_ctl_register(net);
}

static void ipv4_frags_exit_net(struct net *net)
{
	ip4_frags_ns_ctl_unregister(net);
	inet_frags_exit_net(&net->ipv4.frags, &ip4_frags);
}

static struct pernet_operations ip4_frags_ops = {
	.init = ipv4_frags_init_net,
	.exit = ipv4_frags_exit_net,
};

//�ֶ�������ϵͳ��ʼ��
//��ʼ��һ����ʱ�� �� ���ֵ���� Ϊ�˷�ֹDos����
void __init ipfrag_init(void)
{
	ip4_frags_ctl_register();
	register_pernet_subsys(&ip4_frags_ops);
	ip4_frags.hashfn = ip4_hashfn;
	ip4_frags.constructor = ip4_frag_init;
	ip4_frags.destructor = ip4_frag_free;
	ip4_frags.skb_free = NULL;
	ip4_frags.qsize = sizeof(struct ipq);
	ip4_frags.match = ip4_frag_match;
	ip4_frags.frag_expire = ip_expire;
	ip4_frags.secret_interval = 10 * 60 * HZ;
	inet_frags_init(&ip4_frags);
}

EXPORT_SYMBOL(ip_defrag);
