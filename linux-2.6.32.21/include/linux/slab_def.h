#ifndef _LINUX_SLAB_DEF_H
#define	_LINUX_SLAB_DEF_H

/*
 * Definitions unique to the original Linux SLAB allocator.
 *
 * What we provide here is a way to optimize the frequent kmalloc
 * calls in the kernel by selecting the appropriate general cache
 * if kmalloc was called with a size that can be established at
 * compile time.
 */

#include <linux/init.h>
#include <asm/page.h>		/* kmalloc_sizes.h needs PAGE_SIZE */
#include <asm/cache.h>		/* kmalloc_sizes.h needs L1_CACHE_BYTES */
#include <linux/compiler.h>
#include <linux/kmemtrace.h>

/*
 * struct kmem_cache
 *
 * manages a cache.
 */

struct kmem_cache {
/* 1) per-cpu data, touched during every alloc/free */
	/*per-CPU数据，记录了本地高速缓存的信息，也用于跟踪最近释放的对象，每次分配和释放都要直接访问它*/
    /*
      array是一个指向数组的指针，每个数组项都对应于系统中的一个CPU。
      每个数组项都包含了另一个指针，指向下文讨论的array_cache结构的实例
   */
	struct array_cache *array[NR_CPUS];

/* 2) Cache tunables. Protected by cache_chain_mutex */
	unsigned int batchcount;/*本地高速缓存转入或转出的大批对象数量 指定了在每CPU列表为空的情况下，从缓存的slab中获取对象的数目。它还表示在缓存增长时分配的对象数目*/
	unsigned int limit; /*本地高速缓存中空闲对象的最大数目//limit指定了每CPU列表中保存的对象的最大数目，如果超出该值，内核会将batchcount个对象返回到slab*/
	unsigned int shared;

	unsigned int buffer_size;/*管理对象的大小*/
	u32 reciprocal_buffer_size;/*buffer_size的倒数值*/ 
/* 3) touched by every alloc & free from the backend */

	unsigned int flags;		/* constant flags */ /* 用于标记slab头得管理数据是在slab内还是外*/
	unsigned int num;		/* # of objs per slab */ /*每个slab包含对象的个数*/

/* 4) cache_grow/shrink */
	/* order of pgs per slab (2^n) */
	unsigned int gfporder;/*指定了slab包含的页数目以2为底得对数*/

	/* force GFP flags, e.g. GFP_DMA */
	gfp_t gfpflags;/*与伙伴系统交互时所提供的分配标识*/

	size_t colour;			/* cache colouring range *//* 颜色的个数*/
	unsigned int colour_off;	/* colour offset *//* 着色的偏移量  /是基本偏移量乘以颜色值获得的绝对偏移量*/ 
	struct kmem_cache *slabp_cache;/*/如果slab头部的管理数据存储在slab外部，则slabp_cache指向分配所需内存的一般性
                                      缓存；如果slab头部在slab上，则其为NULL*/
	unsigned int slab_size;/*slab管理区的大小*/
	unsigned int dflags;		/* dynamic flags */

	/* constructor func */
	void (*ctor)(void *obj);////创建高速缓存时的构造函数指针

/* 5) cache creation/removal */
	const char *name;//缓存的名称
	struct list_head next;//用于将kmem_cache的所有实例保存在全局链表cache_chain上

/* 6) statistics */
#ifdef CONFIG_DEBUG_SLAB
	unsigned long num_active;
	unsigned long num_allocations;
	unsigned long high_mark;
	unsigned long grown;
	unsigned long reaped;
	unsigned long errors;
	unsigned long max_freeable;
	unsigned long node_allocs;
	unsigned long node_frees;
	unsigned long node_overflow;
	atomic_t allochit;
	atomic_t allocmiss;
	atomic_t freehit;
	atomic_t freemiss;

	/*
	 * If debugging is enabled, then the allocator can add additional
	 * fields and/or padding to every object. buffer_size contains the total
	 * object size including these internal fields, the following two
	 * variables contain the offset to the user object and its size.
	 */
	int obj_offset;
	int obj_size;
#endif /* CONFIG_DEBUG_SLAB */

	/*
	 * We put nodelists[] at the end of kmem_cache, because we want to size
	 * this array to nr_node_ids slots instead of MAX_NUMNODES
	 * (see kmem_cache_init())
	 * We still use [MAX_NUMNODES] and not [1] or [0] because cache_cache
	 * is statically defined, so we reserve the max number of nodes.
	 */
	 
	struct kmem_list3 *nodelists[MAX_NUMNODES];
	/*
	 * Do not add fields after nodelists[]
	 */
};

/* Size description struct for general caches. */
struct cache_sizes {
	size_t		 	cs_size;
	struct kmem_cache	*cs_cachep;
#ifdef CONFIG_ZONE_DMA
	struct kmem_cache	*cs_dmacachep;
#endif
};
extern struct cache_sizes malloc_sizes[];

void *kmem_cache_alloc(struct kmem_cache *, gfp_t);
void *__kmalloc(size_t size, gfp_t flags);

#ifdef CONFIG_KMEMTRACE
extern void *kmem_cache_alloc_notrace(struct kmem_cache *cachep, gfp_t flags);
extern size_t slab_buffer_size(struct kmem_cache *cachep);
#else
static __always_inline void *
kmem_cache_alloc_notrace(struct kmem_cache *cachep, gfp_t flags)
{
	return kmem_cache_alloc(cachep, flags);
}
static inline size_t slab_buffer_size(struct kmem_cache *cachep)
{
	return 0;
}
#endif

static __always_inline void *kmalloc(size_t size, gfp_t flags)
{
	struct kmem_cache *cachep;
	void *ret;

	if (__builtin_constant_p(size)) {
		int i = 0;

		if (!size)
			return ZERO_SIZE_PTR;

#define CACHE(x) \
		if (size <= x) \
			goto found; \
		else \
			i++;
#include <linux/kmalloc_sizes.h>
#undef CACHE
		return NULL;
found:
#ifdef CONFIG_ZONE_DMA
		if (flags & GFP_DMA)
			cachep = malloc_sizes[i].cs_dmacachep;
		else
#endif
			cachep = malloc_sizes[i].cs_cachep;

		ret = kmem_cache_alloc_notrace(cachep, flags);

		trace_kmalloc(_THIS_IP_, ret,
			      size, slab_buffer_size(cachep), flags);

		return ret;
	}
	return __kmalloc(size, flags);
}

#ifdef CONFIG_NUMA
extern void *__kmalloc_node(size_t size, gfp_t flags, int node);
extern void *kmem_cache_alloc_node(struct kmem_cache *, gfp_t flags, int node);

#ifdef CONFIG_KMEMTRACE
extern void *kmem_cache_alloc_node_notrace(struct kmem_cache *cachep,
					   gfp_t flags,
					   int nodeid);
#else
static __always_inline void *
kmem_cache_alloc_node_notrace(struct kmem_cache *cachep,
			      gfp_t flags,
			      int nodeid)
{
	return kmem_cache_alloc_node(cachep, flags, nodeid);
}
#endif

static __always_inline void *kmalloc_node(size_t size, gfp_t flags, int node)
{
	struct kmem_cache *cachep;
	void *ret;

	if (__builtin_constant_p(size)) {
		int i = 0;

		if (!size)
			return ZERO_SIZE_PTR;

#define CACHE(x) \
		if (size <= x) \
			goto found; \
		else \
			i++;
#include <linux/kmalloc_sizes.h>
#undef CACHE
		return NULL;
found:
#ifdef CONFIG_ZONE_DMA
		if (flags & GFP_DMA)
			cachep = malloc_sizes[i].cs_dmacachep;
		else
#endif
			cachep = malloc_sizes[i].cs_cachep;

		ret = kmem_cache_alloc_node_notrace(cachep, flags, node);

		trace_kmalloc_node(_THIS_IP_, ret,
				   size, slab_buffer_size(cachep),
				   flags, node);

		return ret;
	}
	return __kmalloc_node(size, flags, node);
}

#endif	/* CONFIG_NUMA */

#endif	/* _LINUX_SLAB_DEF_H */
