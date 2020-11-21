/*
 * Copyright (C) 2001 Momchil Velikov
 * Portions Copyright (C) 2001 Christoph Hellwig
 * Copyright (C) 2005 SGI, Christoph Lameter
 * Copyright (C) 2006 Nick Piggin
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*
基数树的介绍:
1)RADIX-TREE，也称为Radix Trie或Compact Prefix Tree，中文翻译基数树，是一种空间 优化的字典树，
其每一个节点都是由子节点和它的父节点合并而成。其结果是，每个内部 节点的子节点数目至少是基数数的基数r，其中r是一个正数，且为 2x，其中x≥1。 
有别于其他的字典树，RADIX树的边可以标记为元素序列或单个元素，这使得小集合场景下 基数树更加有效

2)普通的树在对比时，总是进行完整的键值比对（比如红黑树、AVL树都是这样）。RADIX树在 进行比对时，键值在给定节点上进行位组比对,位组中位的数量是基数树的基数r,当r=2 时为二进制RADIX树

3)RADIX树不同于 其他树的最大特点是，它不在每个分支上比较整个键值，在进行搜索操作时，只需将键值的 一部分与节点中存储的键值进行比较


*/

#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/radix-tree.h>
#include <linux/percpu.h>
#include <linux/slab.h>
#include <linux/notifier.h>
#include <linux/cpu.h>
#include <linux/gfp.h>
#include <linux/string.h>
#include <linux/bitops.h>
#include <linux/rcupdate.h>


//定义基数r 内核通过CONFIG选项，可设置为4或6，默认为6
#ifdef __KERNEL__
#define RADIX_TREE_MAP_SHIFT	(CONFIG_BASE_SMALL ? 4 : 6)
#else
#define RADIX_TREE_MAP_SHIFT	3	/* For more stressful testing */
#endif

//定义为2^RADIX_TREE_MAP_SHIFT，等于64。该数值定义指针 数组 slot 数量
#define RADIX_TREE_MAP_SIZE	(1UL << RADIX_TREE_MAP_SHIFT)

//在提取特定位时使用，C语言通常使用移位与掩码操作实现位组 提取
#define RADIX_TREE_MAP_MASK	(RADIX_TREE_MAP_SIZE-1)

#define RADIX_TREE_TAG_LONGS	\
	((RADIX_TREE_MAP_SIZE + BITS_PER_LONG - 1) / BITS_PER_LONG)

//节点类型 文件缓存
struct radix_tree_node {
	unsigned int	height;		/* 底部到当前节点的高度 */
	unsigned int	count;
	struct rcu_head	rcu_head;
	void		*slots[RADIX_TREE_MAP_SIZE];//在非叶子节点slots[n]指向下一级radix_tree_node
	                                        //在叶子节点slots[n]指向一个page结构
	unsigned long	tags[RADIX_TREE_MAX_TAGS][RADIX_TREE_TAG_LONGS];
};

struct radix_tree_path 
{
	struct radix_tree_node *node;
	int offset;
};

//基数数key值为unsigned long，因此bit数合计 8*sizeof(unsigned log) ；
#define RADIX_TREE_INDEX_BITS  (8 /* CHAR_BIT */ * sizeof(unsigned long))

//根据RADIX定义，可知最大深度为 ?RADIX_TREE_INDEX_BITS/RADIX_TREE_MAP_SHIFT?{}，64位下为11。
#define RADIX_TREE_MAX_PATH (DIV_ROUND_UP(RADIX_TREE_INDEX_BITS,RADIX_TREE_MAP_SHIFT))
/*
#define RADIX_TREE_MAX_PATH  (8*8 + (6-1) ) / (8*8) = (64+5) / 6 = 11

*/

/*
 * The height_to_maxindex array needs to be one deeper than the maximum
 * path as height 0 holds only 1 entry.
 */
 //存储每层树高所能表示的最大索引数
static unsigned long height_to_maxindex[RADIX_TREE_MAX_PATH + 1]; //12
static unsigned long height_to_maxindex[RADIX_TREE_MAX_PATH + 1] __read_mostly;

/*
 * Radix tree node cache.
 */
static struct kmem_cache *radix_tree_node_cachep;

/*
 * Per-cpu pool of preloaded nodes
 */
struct radix_tree_preload {
	int nr;
	struct radix_tree_node *nodes[RADIX_TREE_MAX_PATH];
};
static DEFINE_PER_CPU(struct radix_tree_preload, radix_tree_preloads) = { 0, };

static inline gfp_t root_gfp_mask(struct radix_tree_root *root)
{
	return root->gfp_mask & __GFP_BITS_MASK;
}

static inline void tag_set(struct radix_tree_node *node, unsigned int tag,
		int offset)
{
	__set_bit(offset, node->tags[tag]);
}

static inline void tag_clear(struct radix_tree_node *node, unsigned int tag,
		int offset)
{
	__clear_bit(offset, node->tags[tag]);
}

static inline int tag_get(struct radix_tree_node *node, unsigned int tag,
		int offset)
{
	return test_bit(offset, node->tags[tag]);
}

static inline void root_tag_set(struct radix_tree_root *root, unsigned int tag)
{
	root->gfp_mask |= (__force gfp_t)(1 << (tag + __GFP_BITS_SHIFT));
}

static inline void root_tag_clear(struct radix_tree_root *root, unsigned int tag)
{
	root->gfp_mask &= (__force gfp_t)~(1 << (tag + __GFP_BITS_SHIFT));
}

static inline void root_tag_clear_all(struct radix_tree_root *root)
{
	root->gfp_mask &= __GFP_BITS_MASK;
}

static inline int root_tag_get(struct radix_tree_root *root, unsigned int tag)
{
	return (__force unsigned)root->gfp_mask & (1 << (tag + __GFP_BITS_SHIFT));
}

/*
 * Returns 1 if any slot in the node has this tag set.
 * Otherwise returns 0.
 */
static inline int any_tag_set(struct radix_tree_node *node, unsigned int tag)
{
	int idx;
	for (idx = 0; idx < RADIX_TREE_TAG_LONGS; idx++) {
		if (node->tags[tag][idx])
			return 1;
	}
	return 0;
}
/*
 * This assumes that the caller has performed appropriate preallocation, and
 * that the caller has pinned this thread of control to the current CPU.
 */
static struct radix_tree_node * radix_tree_node_alloc(struct radix_tree_root *root)
{
	struct radix_tree_node *ret = NULL;
	gfp_t gfp_mask = root_gfp_mask(root);

	if (!(gfp_mask & __GFP_WAIT)) 
	{
		struct radix_tree_preload *rtp;

		/*
		 * Provided the caller has preloaded here, we will always
		 * succeed in getting a node here (and never reach
		 * kmem_cache_alloc)
		 */
		//进行了预先申请节点 
		rtp = &__get_cpu_var(radix_tree_preloads);
		if (rtp->nr) {
			ret = rtp->nodes[rtp->nr - 1];
			rtp->nodes[rtp->nr - 1] = NULL;
			rtp->nr--;
		}
	}
	if (ret == NULL)
		ret = kmem_cache_alloc(radix_tree_node_cachep, gfp_mask);

	BUG_ON(radix_tree_is_indirect_ptr(ret));
	return ret;
}

static void radix_tree_node_rcu_free(struct rcu_head *head)
{
	struct radix_tree_node *node =
			container_of(head, struct radix_tree_node, rcu_head);

	/*
	 * must only free zeroed nodes into the slab. radix_tree_shrink
	 * can leave us with a non-NULL entry in the first slot, so clear
	 * that here to make sure.
	 */
	tag_clear(node, 0, 0);
	tag_clear(node, 1, 0);
	node->slots[0] = NULL;
	node->count = 0;

	kmem_cache_free(radix_tree_node_cachep, node);
}

static inline void radix_tree_node_free(struct radix_tree_node *node)
{
	call_rcu(&node->rcu_head, radix_tree_node_rcu_free);
}

/*
 * Load up this CPU's radix_tree_node buffer with sufficient objects to
 * ensure that the addition of a single element in the tree cannot fail.  On
 * success, return zero, with preemption disabled.  On error, return -ENOMEM
 * with preemption not disabled.
 *
 * To make use of this facility, the radix tree must be initialised without
 * __GFP_WAIT being passed to INIT_RADIX_TREE().
 */
int radix_tree_preload(gfp_t gfp_mask)
{
	struct radix_tree_preload *rtp;
	struct radix_tree_node *node;
	int ret = -ENOMEM;

	preempt_disable();
	rtp = &__get_cpu_var(radix_tree_preloads);
	while (rtp->nr < ARRAY_SIZE(rtp->nodes)) {
		preempt_enable();
		node = kmem_cache_alloc(radix_tree_node_cachep, gfp_mask);
		if (node == NULL)
			goto out;
		preempt_disable();
		rtp = &__get_cpu_var(radix_tree_preloads);
		if (rtp->nr < ARRAY_SIZE(rtp->nodes))
			rtp->nodes[rtp->nr++] = node;
		else
			kmem_cache_free(radix_tree_node_cachep, node);
	}
	ret = 0;
out:
	return ret;
}
EXPORT_SYMBOL(radix_tree_preload);

/*  返回某个高度可以存储的最大索引值
 *	Return the maximum key which can be store into a
 *	radix tree with height HEIGHT.
 */
static inline unsigned long radix_tree_maxindex(unsigned int height)
{
	return height_to_maxindex[height];
}

/*  扩展一个radix树  ,将树进行拔高
 *	Extend a radix tree so it can store key @index.
 */
static int radix_tree_extend(struct radix_tree_root *root, unsigned long index)
{
	struct radix_tree_node *node;
	unsigned int height;
	int tag;

	/* Figure out what the height should be.  */
	height = root->height + 1;
	while (index > radix_tree_maxindex(height))
		height++;

	if (root->rnode == NULL) 
	{
		root->height = height;
		goto out;
	}

	do {
		unsigned int newheight;

	    //此函数可能会从预先申请的struct radix_tree_node 池中获得此结构体
	    //意思解释分配一个新的struct radix_tree_node结构体
		if (!(node = radix_tree_node_alloc(root)))
			return -ENOMEM;

		/* Increase the height.  */
		
		node->slots[0] = radix_tree_indirect_to_ptr(root->rnode);

		/* Propagate the aggregated tag info into the new root */
		for (tag = 0; tag < RADIX_TREE_MAX_TAGS; tag++) {
			if (root_tag_get(root, tag))
				tag_set(node, tag, 0);
		}

		newheight = root->height+1;
		node->height = newheight;
		node->count = 1;
		node = radix_tree_ptr_to_indirect(node);
		rcu_assign_pointer(root->rnode, node);
		root->height = newheight;
	} while (height > root->height);
out:
	return 0;
}

/**
 *	radix_tree_insert    -    insert into a radix tree
 *	@root:		radix tree root
 *	@index:		index key
 *	@item:		item to insert
 *  
    在树中加入一个树的节点
 *	Insert an item into the radix tree at position @index.

构造的radix树于越靠近树根 height层级越大
 */
int radix_tree_insert(struct radix_tree_root *root, //树根
			               unsigned long index,          //key 即一个长整数
			               void *item)                   //长整型对应的一个指针
{
	struct radix_tree_node *node = NULL, 
		                   *slot;
	unsigned int height, shift;
	int offset;
	int error;

    //意思为item地址的最后一位不能被标记为RADIX_TREE_INDIRECT_PTR,否则会和标记为冲突 出现bug
	BUG_ON(radix_tree_is_indirect_ptr(item));

	/* Make sure the tree is high enough.  */
	//确保当前树的高度能够存储index
	if (index > radix_tree_maxindex(root->height)) //获得当前高度下可以存储的最大key index
	{
	    //增加树的高度,调用函数radix_tree_extend扩展树的层数以满足index
	    //树的高度是从下往上增长的
		error = radix_tree_extend(root, index);
		if (error)
			return error;
	}


	//得到当前指向的槽
	slot = radix_tree_indirect_to_ptr(root->rnode);

	height = root->height;

	//获得在height层 长整型的偏移值
	shift = (height-1) * RADIX_TREE_MAP_SHIFT;

	offset = 0;			/* uninitialised var warning */

	//循环初始化各层的radix_tree_node对象
	//对节点进行填充 从下往上的填充, 
	while (height > 0) 
	{
		if (slot == NULL) 
		{
			/* Have to add a child node.  */
			if (!(slot = radix_tree_node_alloc(root)))
				return -ENOMEM;
			slot->height = height;
			if (node) 
			{
				rcu_assign_pointer(node->slots[offset], slot);
				node->count++;
			} 
			else
				rcu_assign_pointer(root->rnode,radix_tree_ptr_to_indirect(slot));
		}
        
		/* Go a level down */
		offset = (index >> shift) & RADIX_TREE_MAP_MASK;
		node = slot;
		slot = node->slots[offset];
		shift -= RADIX_TREE_MAP_SHIFT;
		height--;
	}

	if (slot != NULL)
		return -EEXIST;

	if (node) 
	{
		node->count++;
		rcu_assign_pointer(node->slots[offset], item);
		BUG_ON(tag_get(node, 0, offset));
		BUG_ON(tag_get(node, 1, offset));
	} 
	else  //如果第一次插入操作就传入的index为 0 则会走到此处
	{
		rcu_assign_pointer(root->rnode, item);
		BUG_ON(root_tag_get(root, 0));
		BUG_ON(root_tag_get(root, 1));
	}

	return 0;
}
EXPORT_SYMBOL(radix_tree_insert);

/*
 * is_slot == 1 寻找存放数据指针的槽,用于数据指针的替换.
 * is_slot == 0 寻找数据指针.
 */
static void *radix_tree_lookup_element(struct radix_tree_root *root,
				                              unsigned long index, int is_slot)
{
	unsigned int height, shift;
	struct radix_tree_node *node, **slot;

	node = rcu_dereference(root->rnode);
	if (node == NULL)
		return NULL;

    /*为了第一次就插入index 0,然后没有插入别的数据就再次要替换index 0的情况*/
	if (!radix_tree_is_indirect_ptr(node)) 
	{
		if (index > 0)  //此处index只能是 0,如果是别的值 则相应的数据指针还没有插入
			return NULL;
		
		return is_slot ? (void *)&root->rnode : node;
	}

	
	node = radix_tree_indirect_to_ptr(node);

	height = node->height;
	if (index > radix_tree_maxindex(height))
		return NULL;

	shift = (height-1) * RADIX_TREE_MAP_SHIFT;

    //下面是主要的遍历查找过程,在每层进行偏移查找 直到最底层
	do {
		slot = (struct radix_tree_node **)(node->slots + ((index>>shift) & RADIX_TREE_MAP_MASK));
		node = rcu_dereference(*slot);//获取真正的数据指针
		if (node == NULL) //如果为NULL 说明数据指针是空的
			return NULL;

		shift -= RADIX_TREE_MAP_SHIFT;
		height--;
	} while (height > 0);

	return is_slot ? (void *)slot:node;
}

/**
 *	radix_tree_lookup_slot    -    lookup a slot in a radix tree
 *	@root:		radix tree root
 *	@index:		index key
 *
 *	Returns:  the slot corresponding to the position @index in the
 *	radix tree @root. This is useful for update-if-exists operations.
 *
 *	This function can be called under rcu_read_lock iff the slot is not
 *	modified by radix_tree_replace_slot, otherwise it must be called
 *	exclusive from other writers. Any dereference of the slot must be done
 *	using radix_tree_deref_slot.
 */
void **radix_tree_lookup_slot(struct radix_tree_root *root, unsigned long index)
{
	return (void **)radix_tree_lookup_element(root, index, 1);
}
EXPORT_SYMBOL(radix_tree_lookup_slot);

/**
 *	radix_tree_lookup    -    perform lookup operation on a radix tree
 *	@root:		radix tree root
 *	@index:		index key
 *
 *	Lookup the item at the position @index in the radix tree @root.
 *
 *	This function can be called under rcu_read_lock, however the caller
 *	must manage lifetimes of leaf nodes (eg. RCU may also be used to free
 *	them safely). No RCU barriers are required to access or modify the
 *	returned item, however.
 */
void *radix_tree_lookup(struct radix_tree_root *root, unsigned long index)
{
	return radix_tree_lookup_element(root, index, 0);
}
EXPORT_SYMBOL(radix_tree_lookup);

/**
 *	radix_tree_tag_set - set a tag on a radix tree node
 *	@root:		radix tree root
 *	@index:		index key
 *	@tag: 		tag index
 *
 *	Set the search tag (which must be < RADIX_TREE_MAX_TAGS)
 *	corresponding to @index in the radix tree.  From
 *	the root all the way down to the leaf node.
 *
 *	Returns the address of the tagged item.   Setting a tag on a not-present
 *	item is a bug.
 */
void *radix_tree_tag_set(struct radix_tree_root *root,
			                  unsigned long index, 
			                  unsigned int tag)
{
	unsigned int height, shift;
	struct radix_tree_node *slot;

	height = root->height;
	BUG_ON(index > radix_tree_maxindex(height));

	slot = radix_tree_indirect_to_ptr(root->rnode);
	shift = (height - 1) * RADIX_TREE_MAP_SHIFT;

	while (height > 0) {
		int offset;

		offset = (index >> shift) & RADIX_TREE_MAP_MASK;
		if (!tag_get(slot, tag, offset))
			tag_set(slot, tag, offset);
		slot = slot->slots[offset];
		BUG_ON(slot == NULL);
		shift -= RADIX_TREE_MAP_SHIFT;
		height--;
	}

	/* set the root's tag bit */
	if (slot && !root_tag_get(root, tag))
		root_tag_set(root, tag);

	return slot;
}
EXPORT_SYMBOL(radix_tree_tag_set);

/**
 *	radix_tree_tag_clear - clear a tag on a radix tree node
 *	@root:		radix tree root
 *	@index:		index key
 *	@tag: 		tag index
 *
 *	Clear the search tag (which must be < RADIX_TREE_MAX_TAGS)
 *	corresponding to @index in the radix tree.  If
 *	this causes the leaf node to have no tags set then clear the tag in the
 *	next-to-leaf node, etc.
 *
 *	Returns the address of the tagged item on success, else NULL.  ie:
 *	has the same return value and semantics as radix_tree_lookup().
 */
void *radix_tree_tag_clear(struct radix_tree_root *root,
			unsigned long index, unsigned int tag)
{
	/*
	 * The radix tree path needs to be one longer than the maximum path
	 * since the "list" is null terminated.
	 */
	struct radix_tree_path path[RADIX_TREE_MAX_PATH + 1], *pathp = path;
	struct radix_tree_node *slot = NULL;
	unsigned int height, shift;

	height = root->height;
	if (index > radix_tree_maxindex(height))
		goto out;

	shift = (height - 1) * RADIX_TREE_MAP_SHIFT;
	pathp->node = NULL;
	slot = radix_tree_indirect_to_ptr(root->rnode);

	while (height > 0) {
		int offset;

		if (slot == NULL)
			goto out;

		offset = (index >> shift) & RADIX_TREE_MAP_MASK;
		pathp[1].offset = offset;
		pathp[1].node = slot;
		slot = slot->slots[offset];
		pathp++;
		shift -= RADIX_TREE_MAP_SHIFT;
		height--;
	}

	if (slot == NULL)
		goto out;

	while (pathp->node) {
		if (!tag_get(pathp->node, tag, pathp->offset))
			goto out;
		tag_clear(pathp->node, tag, pathp->offset);
		if (any_tag_set(pathp->node, tag))
			goto out;
		pathp--;
	}

	/* clear the root's tag bit */
	if (root_tag_get(root, tag))
		root_tag_clear(root, tag);

out:
	return slot;
}
EXPORT_SYMBOL(radix_tree_tag_clear);

/**
 * radix_tree_tag_get - get a tag on a radix tree node
 * @root:		radix tree root
 * @index:		index key
 * @tag: 		tag index (< RADIX_TREE_MAX_TAGS)
 *
 * Return values:
 *
 *  0: tag not present or not set
 *  1: tag set
 */
int radix_tree_tag_get(struct radix_tree_root *root,
			unsigned long index, unsigned int tag)
{
	unsigned int height, shift;
	struct radix_tree_node *node;
	int saw_unset_tag = 0;

	/* check the root's tag bit */
	if (!root_tag_get(root, tag))
		return 0;

	node = rcu_dereference(root->rnode);
	if (node == NULL)
		return 0;

	if (!radix_tree_is_indirect_ptr(node))
		return (index == 0);
	node = radix_tree_indirect_to_ptr(node);

	height = node->height;
	if (index > radix_tree_maxindex(height))
		return 0;

	shift = (height - 1) * RADIX_TREE_MAP_SHIFT;

	for ( ; ; ) {
		int offset;

		if (node == NULL)
			return 0;

		offset = (index >> shift) & RADIX_TREE_MAP_MASK;

		/*
		 * This is just a debug check.  Later, we can bale as soon as
		 * we see an unset tag.
		 */
		if (!tag_get(node, tag, offset))
			saw_unset_tag = 1;
		if (height == 1) {
			int ret = tag_get(node, tag, offset);

			BUG_ON(ret && saw_unset_tag);
			return !!ret;
		}
		node = rcu_dereference(node->slots[offset]);
		shift -= RADIX_TREE_MAP_SHIFT;
		height--;
	}
}
EXPORT_SYMBOL(radix_tree_tag_get);

/**
 *	radix_tree_next_hole    -    find the next hole (not-present entry)
 *	@root:		tree root
 *	@index:		index key
 *	@max_scan:	maximum range to search
 *
 *	Search the set [index, min(index+max_scan-1, MAX_INDEX)] for the lowest
 *	indexed hole.
 *
 *	Returns: the index of the hole if found, otherwise returns an index
 *	outside of the set specified (in which case 'return - index >= max_scan'
 *	will be true). In rare cases of index wrap-around, 0 will be returned.
 *
 *	radix_tree_next_hole may be called under rcu_read_lock. However, like
 *	radix_tree_gang_lookup, this will not atomically search a snapshot of
 *	the tree at a single point in time. For example, if a hole is created
 *	at index 5, then subsequently a hole is created at index 10,
 *	radix_tree_next_hole covering both indexes may return 10 if called
 *	under rcu_read_lock.
 */
unsigned long radix_tree_next_hole(struct radix_tree_root *root,
				unsigned long index, unsigned long max_scan)
{
	unsigned long i;

	for (i = 0; i < max_scan; i++) {
		if (!radix_tree_lookup(root, index))
			break;
		index++;
		if (index == 0)
			break;
	}

	return index;
}
EXPORT_SYMBOL(radix_tree_next_hole);

/**
 *	radix_tree_prev_hole    -    find the prev hole (not-present entry)
 *	@root:		tree root
 *	@index:		index key
 *	@max_scan:	maximum range to search
 *
 *	Search backwards in the range [max(index-max_scan+1, 0), index]
 *	for the first hole.
 *
 *	Returns: the index of the hole if found, otherwise returns an index
 *	outside of the set specified (in which case 'index - return >= max_scan'
 *	will be true). In rare cases of wrap-around, LONG_MAX will be returned.
 *
 *	radix_tree_next_hole may be called under rcu_read_lock. However, like
 *	radix_tree_gang_lookup, this will not atomically search a snapshot of
 *	the tree at a single point in time. For example, if a hole is created
 *	at index 10, then subsequently a hole is created at index 5,
 *	radix_tree_prev_hole covering both indexes may return 5 if called under
 *	rcu_read_lock.
 */
unsigned long radix_tree_prev_hole(struct radix_tree_root *root,
				   unsigned long index, unsigned long max_scan)
{
	unsigned long i;

	for (i = 0; i < max_scan; i++) {
		if (!radix_tree_lookup(root, index))
			break;
		index--;
		if (index == LONG_MAX)
			break;
	}

	return index;
}
EXPORT_SYMBOL(radix_tree_prev_hole);

static unsigned int
__lookup(struct radix_tree_node *slot, void ***results, unsigned long index,
	unsigned int max_items, unsigned long *next_index)
{
	unsigned int nr_found = 0;
	unsigned int shift, height;
	unsigned long i;

	height = slot->height;
	if (height == 0)
		goto out;
	shift = (height-1) * RADIX_TREE_MAP_SHIFT;

	for ( ; height > 1; height--) {
		i = (index >> shift) & RADIX_TREE_MAP_MASK;
		for (;;) {
			if (slot->slots[i] != NULL)
				break;
			index &= ~((1UL << shift) - 1);
			index += 1UL << shift;
			if (index == 0)
				goto out;	/* 32-bit wraparound */
			i++;
			if (i == RADIX_TREE_MAP_SIZE)
				goto out;
		}

		shift -= RADIX_TREE_MAP_SHIFT;
		slot = rcu_dereference(slot->slots[i]);
		if (slot == NULL)
			goto out;
	}

	/* Bottom level: grab some items */
	for (i = index & RADIX_TREE_MAP_MASK; i < RADIX_TREE_MAP_SIZE; i++) {
		index++;
		if (slot->slots[i]) {
			results[nr_found++] = &(slot->slots[i]);
			if (nr_found == max_items)
				goto out;
		}
	}
out:
	*next_index = index;
	return nr_found;
}

/**
 *	radix_tree_gang_lookup - perform multiple lookup on a radix tree
 *	@root:		radix tree root
 *	@results:	where the results of the lookup are placed
 *	@first_index:	start the lookup from this key
 *	@max_items:	place up to this many items at *results
 *
 *	Performs an index-ascending scan of the tree for present items.  Places
 *	them at *@results and returns the number of items which were placed at
 *	*@results.
 *
 *	The implementation is naive.
 *
 *	Like radix_tree_lookup, radix_tree_gang_lookup may be called under
 *	rcu_read_lock. In this case, rather than the returned results being
 *	an atomic snapshot of the tree at a single point in time, the semantics
 *	of an RCU protected gang lookup are as though multiple radix_tree_lookups
 *	have been issued in individual locks, and results stored in 'results'.
 */
 //循环调用函数__lookup，查找radix_tree_root中最底层的非空页
unsigned int radix_tree_gang_lookup(struct radix_tree_root *root, void **results,
			unsigned long first_index, unsigned int max_items)
{
	unsigned long max_index;
	struct radix_tree_node *node;
	unsigned long cur_index = first_index;
	unsigned int ret;

	node = rcu_dereference(root->rnode);
	if (!node)
		return 0;

	if (!radix_tree_is_indirect_ptr(node)) {
		if (first_index > 0)
			return 0;
		results[0] = node;
		return 1;
	}
	node = radix_tree_indirect_to_ptr(node);

	max_index = radix_tree_maxindex(node->height);

	ret = 0;
	while (ret < max_items) {
		unsigned int nr_found, slots_found, i;
		unsigned long next_index;	/* Index of next search */

		if (cur_index > max_index)
			break;
		slots_found = __lookup(node, (void ***)results + ret, cur_index,
					max_items - ret, &next_index);
		nr_found = 0;
		for (i = 0; i < slots_found; i++) {
			struct radix_tree_node *slot;
			slot = *(((void ***)results)[ret + i]);
			if (!slot)
				continue;
			results[ret + nr_found] = rcu_dereference(slot);
			nr_found++;
		}
		ret += nr_found;
		if (next_index == 0)
			break;
		cur_index = next_index;
	}

	return ret;
}
EXPORT_SYMBOL(radix_tree_gang_lookup);

/**
 *	radix_tree_gang_lookup_slot - perform multiple slot lookup on radix tree
 *	@root:		radix tree root
 *	@results:	where the results of the lookup are placed
 *	@first_index:	start the lookup from this key
 *	@max_items:	place up to this many items at *results
 *
 *	Performs an index-ascending scan of the tree for present items.  Places
 *	their slots at *@results and returns the number of items which were
 *	placed at *@results.
 *
 *	The implementation is naive.
 *
 *	Like radix_tree_gang_lookup as far as RCU and locking goes. Slots must
 *	be dereferenced with radix_tree_deref_slot, and if using only RCU
 *	protection, radix_tree_deref_slot may fail requiring a retry.
 */
unsigned int
radix_tree_gang_lookup_slot(struct radix_tree_root *root, void ***results,
			unsigned long first_index, unsigned int max_items)
{
	unsigned long max_index;
	struct radix_tree_node *node;
	unsigned long cur_index = first_index;
	unsigned int ret;

	node = rcu_dereference(root->rnode);
	if (!node)
		return 0;

	if (!radix_tree_is_indirect_ptr(node)) {
		if (first_index > 0)
			return 0;
		results[0] = (void **)&root->rnode;
		return 1;
	}
	node = radix_tree_indirect_to_ptr(node);

	max_index = radix_tree_maxindex(node->height);

	ret = 0;
	while (ret < max_items) {
		unsigned int slots_found;
		unsigned long next_index;	/* Index of next search */

		if (cur_index > max_index)
			break;
		slots_found = __lookup(node, results + ret, cur_index,
					max_items - ret, &next_index);
		ret += slots_found;
		if (next_index == 0)
			break;
		cur_index = next_index;
	}

	return ret;
}
EXPORT_SYMBOL(radix_tree_gang_lookup_slot);

/*
 * FIXME: the two tag_get()s here should use find_next_bit() instead of
 * open-coding the search.
 */
static unsigned int
__lookup_tag(struct radix_tree_node *slot, void ***results, unsigned long index,
	unsigned int max_items, unsigned long *next_index, unsigned int tag)
{
	unsigned int nr_found = 0;
	unsigned int shift, height;

	height = slot->height;
	if (height == 0)
		goto out;
	shift = (height-1) * RADIX_TREE_MAP_SHIFT;

	while (height > 0) {
		unsigned long i = (index >> shift) & RADIX_TREE_MAP_MASK ;

		for (;;) {
			if (tag_get(slot, tag, i))
				break;
			index &= ~((1UL << shift) - 1);
			index += 1UL << shift;
			if (index == 0)
				goto out;	/* 32-bit wraparound */
			i++;
			if (i == RADIX_TREE_MAP_SIZE)
				goto out;
		}
		height--;
		if (height == 0) {	/* Bottom level: grab some items */
			unsigned long j = index & RADIX_TREE_MAP_MASK;

			for ( ; j < RADIX_TREE_MAP_SIZE; j++) {
				index++;
				if (!tag_get(slot, tag, j))
					continue;
				/*
				 * Even though the tag was found set, we need to
				 * recheck that we have a non-NULL node, because
				 * if this lookup is lockless, it may have been
				 * subsequently deleted.
				 *
				 * Similar care must be taken in any place that
				 * lookup ->slots[x] without a lock (ie. can't
				 * rely on its value remaining the same).
				 */
				if (slot->slots[j]) {
					results[nr_found++] = &(slot->slots[j]);
					if (nr_found == max_items)
						goto out;
				}
			}
		}
		shift -= RADIX_TREE_MAP_SHIFT;
		slot = rcu_dereference(slot->slots[i]);
		if (slot == NULL)
			break;
	}
out:
	*next_index = index;
	return nr_found;
}

/**
 *	radix_tree_gang_lookup_tag - perform multiple lookup on a radix tree
 *	                             based on a tag
 *	@root:		radix tree root
 *	@results:	where the results of the lookup are placed
 *	@first_index:	start the lookup from this key
 *	@max_items:	place up to this many items at *results
 *	@tag:		the tag index (< RADIX_TREE_MAX_TAGS)
 *
 *	Performs an index-ascending scan of the tree for present items which
 *	have the tag indexed by @tag set.  Places the items at *@results and
 *	returns the number of items which were placed at *@results.
 */
unsigned int
radix_tree_gang_lookup_tag(struct radix_tree_root *root, void **results,
		unsigned long first_index, unsigned int max_items,
		unsigned int tag)
{
	struct radix_tree_node *node;
	unsigned long max_index;
	unsigned long cur_index = first_index;
	unsigned int ret;

	/* check the root's tag bit */
	if (!root_tag_get(root, tag))
		return 0;

	node = rcu_dereference(root->rnode);
	if (!node)
		return 0;

	if (!radix_tree_is_indirect_ptr(node)) {
		if (first_index > 0)
			return 0;
		results[0] = node;
		return 1;
	}
	node = radix_tree_indirect_to_ptr(node);

	max_index = radix_tree_maxindex(node->height);

	ret = 0;
	while (ret < max_items) {
		unsigned int nr_found, slots_found, i;
		unsigned long next_index;	/* Index of next search */

		if (cur_index > max_index)
			break;
		slots_found = __lookup_tag(node, (void ***)results + ret,
				cur_index, max_items - ret, &next_index, tag);
		nr_found = 0;
		for (i = 0; i < slots_found; i++) {
			struct radix_tree_node *slot;
			slot = *(((void ***)results)[ret + i]);
			if (!slot)
				continue;
			results[ret + nr_found] = rcu_dereference(slot);
			nr_found++;
		}
		ret += nr_found;
		if (next_index == 0)
			break;
		cur_index = next_index;
	}

	return ret;
}
EXPORT_SYMBOL(radix_tree_gang_lookup_tag);

/**
 *	radix_tree_gang_lookup_tag_slot - perform multiple slot lookup on a
 *					  radix tree based on a tag
 *	@root:		radix tree root
 *	@results:	where the results of the lookup are placed
 *	@first_index:	start the lookup from this key
 *	@max_items:	place up to this many items at *results
 *	@tag:		the tag index (< RADIX_TREE_MAX_TAGS)
 *
 *	Performs an index-ascending scan of the tree for present items which
 *	have the tag indexed by @tag set.  Places the slots at *@results and
 *	returns the number of slots which were placed at *@results.
 */
unsigned int
radix_tree_gang_lookup_tag_slot(struct radix_tree_root *root, void ***results,
		unsigned long first_index, unsigned int max_items,
		unsigned int tag)
{
	struct radix_tree_node *node;
	unsigned long max_index;
	unsigned long cur_index = first_index;
	unsigned int ret;

	/* check the root's tag bit */
	if (!root_tag_get(root, tag))
		return 0;

	node = rcu_dereference(root->rnode);
	if (!node)
		return 0;

	if (!radix_tree_is_indirect_ptr(node)) {
		if (first_index > 0)
			return 0;
		results[0] = (void **)&root->rnode;
		return 1;
	}
	node = radix_tree_indirect_to_ptr(node);

	max_index = radix_tree_maxindex(node->height);

	ret = 0;
	while (ret < max_items) {
		unsigned int slots_found;
		unsigned long next_index;	/* Index of next search */

		if (cur_index > max_index)
			break;
		slots_found = __lookup_tag(node, results + ret,
				cur_index, max_items - ret, &next_index, tag);
		ret += slots_found;
		if (next_index == 0)
			break;
		cur_index = next_index;
	}

	return ret;
}
EXPORT_SYMBOL(radix_tree_gang_lookup_tag_slot);


/**
 *	radix_tree_shrink    -    shrink height of a radix tree to minimal
 *	@root		radix tree root
 */
 /*将基数树的高度缩减到最小*/
static inline void radix_tree_shrink(struct radix_tree_root *root)
{
	/* try to shrink tree height */
	while (root->height > 0) 
	{
		struct radix_tree_node *to_free = root->rnode;
		void *newptr;

		BUG_ON(!radix_tree_is_indirect_ptr(to_free));
		to_free = radix_tree_indirect_to_ptr(to_free);

		/*
		 * The candidate node has more than one child, or its child
		 * is not at the leftmost slot, we cannot shrink.
		 */
		 //当发现 当前层只有一个孩子，并且是最左边的槽保存的孩子的时候 才能将此层给去掉,如果不是孩子不是在最左边 此节点就不能不删除掉	
		 /*
         比如树只有两层的化,  因为树的高度是从下往上长的, 所以如果要删除掉heigh=2层,则必须它下面只有最左边的槽也就是范围为0~(2^6-1)范围内的节点可以直接成为root的子节点
         ,heigh=2层的其他子节点比如64~127范围内的第二个孩子  是不能直接成为root的子节点的
		*/
		if (to_free->count != 1)
			break;

		//缩减的化 只能是最左边的孩子成为上一层节点
		if (!to_free->slots[0])
			break;

		/*
		 * We don't need rcu_assign_pointer(), since we are simply
		 * moving the node from one part of the tree to another. If
		 * it was safe to dereference the old pointer to it
		 * (to_free->slots[0]), it will be safe to dereference the new
		 * one (root->rnode).
		 */

		//得到下一个节点的地址
		newptr = to_free->slots[0];

		if (root->height > 1)
			newptr = radix_tree_ptr_to_indirect(newptr);

		root->rnode = newptr;
		root->height--;

		radix_tree_node_free(to_free);
	}
}

/**
 *	radix_tree_delete    -    delete an item from a radix tree
 *	@root:		radix tree root
 *	@index:		index key
 *
 *	Remove the item at @index from the radix tree rooted at @root.
 *
 *	Returns the address of the deleted item, or NULL if it was not present.
 */
void *radix_tree_delete(struct radix_tree_root *root, unsigned long index)
{
	/*
	 * The radix tree path needs to be one longer than the maximum path
	 * since the "list" is null terminated.
	 */
	struct radix_tree_path path[RADIX_TREE_MAX_PATH + 1], *pathp = path;
	struct radix_tree_node *slot = NULL;
	struct radix_tree_node *to_free;
	unsigned int height, shift;
	int tag;
	int offset;

    //获得当前树的总高度
	height = root->height;

	//判断索引范围是否不存在与树中
	if (index > radix_tree_maxindex(height))
		goto out;

    
	slot = root->rnode;

	//如果高度为0  ,则认为树还没有数据
	if (height == 0) 
	{
		root_tag_clear_all(root);
		root->rnode = NULL;
		goto out;
	}
	
	slot = radix_tree_indirect_to_ptr(slot);

	//计算height index的偏移
	shift = (height - 1) * RADIX_TREE_MAP_SHIFT;
	pathp->node = NULL;

    ////对删除路径进行记录
	do {
		if (slot == NULL)
			goto out;

		pathp++;
		offset = (index >> shift) & RADIX_TREE_MAP_MASK;
		pathp->offset = offset;
		pathp->node = slot;
		slot = slot->slots[offset];
		shift -= RADIX_TREE_MAP_SHIFT;
		height--;
	} while (height > 0);

	if (slot == NULL)
		goto out;

	/*
	 * Clear all tags associated with the just-deleted item
	 */
	for (tag = 0; tag < RADIX_TREE_MAX_TAGS; tag++) {
		if (tag_get(pathp->node, tag, pathp->offset))
			radix_tree_tag_clear(root, index, tag);
	}

	to_free = NULL;
	//现在开始释放不需要的节点
	/* Now free the nodes we do not need anymore */
    //当前pathp->node记录的是保存数据槽的地址,  真实的数据现在保存在slot变量中
    //从下往上进行释放,发现node中槽的使用计数不为0 则退出，删除完毕
	while (pathp->node) 
	{
	    //将槽中的数据设置为空,槽数组的使用计数减一
		pathp->node->slots[pathp->offset] = NULL;
		pathp->node->count--;

		/*
		 * Queue the node for deferred freeing after the
		 * last reference to it disappears (set NULL, above).
		 */
		if (to_free)
			radix_tree_node_free(to_free);

		//如果此节点的其他slot还被使用 则不执行下面的节点释放
		if (pathp->node->count) 
		{ 
		    
			if (pathp->node == radix_tree_indirect_to_ptr(root->rnode))
				radix_tree_shrink(root);//对树的高度进行调整
			goto out;
		}

		/* Node with zero slots in use so free it */
		to_free = pathp->node;
		pathp--;

	}

	//走到此处 说明整个树都被释放了
	root_tag_clear_all(root);
	root->height = 0;
	root->rnode = NULL;
	if (to_free)
		radix_tree_node_free(to_free);

out:
	return slot;
}
EXPORT_SYMBOL(radix_tree_delete);

/**
 *	radix_tree_tagged - test whether any items in the tree are tagged
 *	@root:		radix tree root
 *	@tag:		tag to test
 */
int radix_tree_tagged(struct radix_tree_root *root, unsigned int tag)
{
	return root_tag_get(root, tag);
}
EXPORT_SYMBOL(radix_tree_tagged);


//每次申请一个radix_tree_node结构后的初始化回调
static void radix_tree_node_ctor(void *node)
{
	memset(node, 0, sizeof(struct radix_tree_node));
}


static __init unsigned long __maxindex(unsigned int height)
{
    //每一层的数据宽度
    /*
      如0层: 整数范围  width=0*6   即                shift=64 - 0=64   
        1层: 整数范围  width=1*6即 0~    (2^6 - 1)  shift=64 - 6=58 
        2层: 整数范围  width=2*6即 0~(2^12 -1)      shift=64 - 12=52  
	*/
	unsigned int width = height * RADIX_TREE_MAP_SHIFT;	
	int shift = RADIX_TREE_INDEX_BITS - width;

	if (shift < 0)
		return ~0UL;
	
	if (shift >= BITS_PER_LONG)
		return 0UL;

	//获得每层的具体表示范围
	return ~0UL >> shift;
}

static __init void radix_tree_init_maxindex(void)
{
	unsigned int i;

	/*计算每层树高可以存储的最大索引  下面是将radix做成一个2^6为一组的多叉树
	0
	63
	4095
	262143
	16777215
	1073741823
	68719476735
	4398046511103
	281474976710655
	18014398509481983
	1152921504606846975
	-1 
	*/
	for (i = 0; i < ARRAY_SIZE(height_to_maxindex); i++)
		height_to_maxindex[i] = __maxindex(i);
}


//cpu热插拔的回调函数,拔掉cou后释放一些和此cpu相关的资源
static int radix_tree_callback(struct notifier_block *nfb,
                                     unsigned long action,
                                     void *hcpu)
{
       
       int cpu = (long)hcpu;
       struct radix_tree_preload *rtp;

       /* Free per-cpu pool of perloaded nodes */
       if (action == CPU_DEAD || action == CPU_DEAD_FROZEN) 
	   {
               rtp = &per_cpu(radix_tree_preloads, cpu);
               while (rtp->nr)
			   {
                       kmem_cache_free(radix_tree_node_cachep,rtp->nodes[rtp->nr-1]);
                       rtp->nodes[rtp->nr-1] = NULL;
                       rtp->nr--;
               }
       }
       return NOTIFY_OK;
}


//在 start_kernel() 中进行初始化
void __init radix_tree_init(void)
{

    //分配一个slab高速缓存,以后可以从radix_tree_node_cachep快速分配struct radix_tree_node结构体
	radix_tree_node_cachep = kmem_cache_create("radix_tree_node",
		                                        sizeof(struct radix_tree_node), 0,
			                                    SLAB_PANIC | SLAB_RECLAIM_ACCOUNT,
			                                    radix_tree_node_ctor);
    //初始化书中各层的最大索引
	radix_tree_init_maxindex();

	//设置热插拔cpu时的回调函数
	hotcpu_notifier(radix_tree_callback, 0);
}
