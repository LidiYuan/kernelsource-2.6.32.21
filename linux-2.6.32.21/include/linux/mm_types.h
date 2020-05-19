#ifndef _LINUX_MM_TYPES_H
#define _LINUX_MM_TYPES_H

#include <linux/auxvec.h>
#include <linux/types.h>
#include <linux/threads.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/prio_tree.h>
#include <linux/rbtree.h>
#include <linux/rwsem.h>
#include <linux/completion.h>
#include <linux/cpumask.h>
#include <linux/page-debug-flags.h>
#include <asm/page.h>
#include <asm/mmu.h>

#ifndef AT_VECTOR_SIZE_ARCH
#define AT_VECTOR_SIZE_ARCH 0
#endif
#define AT_VECTOR_SIZE (2*(AT_VECTOR_SIZE_ARCH + AT_VECTOR_SIZE_BASE + 1))

struct address_space;

#define USE_SPLIT_PTLOCKS	(NR_CPUS >= CONFIG_SPLIT_PTLOCK_CPUS)

#if USE_SPLIT_PTLOCKS
typedef atomic_long_t mm_counter_t;
#else  /* !USE_SPLIT_PTLOCKS */
typedef unsigned long mm_counter_t;
#endif /* !USE_SPLIT_PTLOCKS */

/*
 * Each physical page in the system has a struct page associated with
 * it to keep track of whatever it is we are using the page for at the
 * moment. Note that we have no way to track which tasks are using
 * a page, though if it is a pagecache page, rmap structures can tell us
 * who is mapping it.
 */

//page_zone()����page��ù�����
struct page {

	//ͨ��set_page_links()����ҳ�������ĸ��ڵ��ĸ���
	//�洢����ϵ�ṹ�޹صı�־,��������ҳ������  PG_error
	unsigned long flags;		

	//ʹ��page_count()���ñ���. 
	//�ú������� 0 ��ʾҳ���з��� ����0������ʾҳ�ڱ�ʹ��
	//���ü�������ʾ�ں������ø�page�Ĵ��������Ҫ������page�����ü�����+1���������-1������ֵΪ0ʱ����ʾû�����ø�page��λ�ã����Ը�page���Ա����ӳ�䣬���������ڴ����ʱ�����õ�
	atomic_t _count;		
					 
	union {
		atomic_t _mapcount;	/* Count of ptes mapped in mms,
					 * to show when page is mapped
					 * & limit reverse map searches.
					 */
					 /*
                         ��ʼֵΪ-1 �ڸ�ҳ�����뵽����ӳ�����ݽṹ ��������1
                         ����ӳ���Ǹ���һ��page���ҵ�����ʹ�ø�ҳ�Ľ���
					  */
					 /*��ҳ��ӳ��Ĵ�����Ҳ����˵��pageͬʱ�����ٸ����̹�����ʼֵΪ-1�����ֻ��һ�����̵�ҳ��ӳ���ˣ���ֵΪ0 �������page���ڻ��ϵͳ�У���ֵΪPAGE_BUDDY_MAPCOUNT_VALUE��-128�����ں�ͨ���жϸ�ֵ�Ƿ�ΪPAGE_BUDDY_MAPCOUNT_VALUE��ȷ����page�Ƿ����ڻ��ϵͳ��*/

		struct 	
		{	
			u16 inuse; //����SLUB���������������Ŀ
			u16 objects;
		};
	};
					 
	union {
	    struct 
		{
			/*
				���������PagePrivate ͨ������buffer_heads
				���������PageSwapCache ������swap_entry_t 
				���������PG_buddy �����ڻ��ϵͳ�Ľ�
			*/
			unsigned long private;

			/*
			    ������λΪ0 ��ָ��inode->address_pace,��ΪNULL
			    ������λ��λ����Ϊ����ӳ�䣬��ָ��ָ��anon_vma����
				anon_vma = (struct anon_vma *)(mapping - PAGE_MAPPING_ANON)
			*/
			struct address_space *mapping;//��page_lock_anon_vma()
	    };
		
#if USE_SPLIT_PTLOCKS
	    spinlock_t ptl;
#endif
	    struct kmem_cache *slab;	/* SLUB: Pointer to slab //����SLUB��������ָ��slab��ָ��*/
	    struct page *first_page;	/*�ں˿��Խ�������ڵ�ҳ�ϲ�Ϊ�ϴ�ĸ���ҳ�������еĵ�һ��ҳΪ��ҳ,
	                                  ������ҳ����βҳ */
	};

	
	union 
	{
		pgoff_t index;		/* Our offset within mapping.//��ӳ���ڵ�ƫ����   */
		void *freelist;		/* SLUB: freelist req. slab lock */
	};

     //����slab ��ֵ��������page_set_slab()��page_set_cache()
	//��һ����ͷ�������ڸ���������ά����ҳ��һ�齫ҳ�����������飬����Ҫ������ǻҳ�Ͳ��ҳ					             
	//����ҳ���滻���Կ��ܱ�������ҳ������active_list��inactive_list������
	struct list_head lru;		/* Pageout list, eg. active_list
					 			   *protected by zone->lru_lock !
									����ҳ�б�
					 			   */
	/*
	 * On machines where all RAM is mapped into kernel address space,
	 * we can simply calculate the virtual address. On machines with
	 * highmem some memory is mapped into kernel virtual memory
	 * dynamically, so we need a place to store that address.
	 * Note that this field could be 16 bits on x86 ... ;)
	 *
	 * Architectures with slow multiplication can define
	 * WANT_PAGE_VIRTUAL in asm/page.h
	 */
//�����ض��Ľṹ��Ħ������ ��������ϵ���Ҹ߶��ڴ���ɢ�б�
#if defined(WANT_PAGE_VIRTUAL)  //page_address()�ô˺����Դ��ֶν��з���
	void *virtual;			/* Kernel virtual address (NULL if
					           not kmapped, ie. highmem)
							   �ں������ַ ���û��ӳ����ΪNULL�����ڸ߶��ڴ� 	

					           */
#endif /* WANT_PAGE_VIRTUAL */

#ifdef CONFIG_WANT_PAGE_DEBUG_FLAGS
	unsigned long debug_flags;	/* Use atomic bitops on this */
#endif

#ifdef CONFIG_KMEMCHECK
	/*
	 * kmemcheck wants to track the status of each byte in a page; this
	 * is a pointer to such a status block. NULL if not tracked.
	 */
	void *shadow;
#endif

};

/*
 * A region containing a mapping of a non-memory backed file under NOMMU
 * conditions.  These are held in a global tree and are pinned by the VMAs that
 * map parts of them.
 */
struct vm_region {
	struct rb_node	vm_rb;		/* link in global region tree */
	unsigned long	vm_flags;	/* VMA vm_flags */
	unsigned long	vm_start;	/* start address of region */
	unsigned long	vm_end;		/* region initialised to here */
	unsigned long	vm_top;		/* region allocated to here */
	unsigned long	vm_pgoff;	/* the offset in vm_file corresponding to vm_start */
	struct file	*vm_file;	/* the backing file or NULL */

	atomic_t	vm_usage;	/* region usage count */
};

/*
 * This struct defines a memory VMM memory area. There is one of these
 * per VM-area/task.  A VM area is any part of the process virtual memory
 * space that has a special rule for the page-fault handlers (ie a shared
 * library, the executable area etc).
 */
 /*
vm_area_struct��������Ļ�����Ԫ������������һ�������ġ�������ͬ�������Ե����ռ䡣
һ������ʹ�õ��Ĳ�ͬ�����ռ�ķ������Կ��ܲ�ͬ�����Ծ���Ҫ���vm_area_struct�������������
*/
struct vm_area_struct {
	struct mm_struct * vm_mm;	/* The address space we belong to. ָ���ϼ��ṹmm_struct*/
	
	unsigned long vm_start;		/* Our start address within vm_mm. �����ڴ�����Ŀ�ʼ��ַ*/
	unsigned long vm_end;		/* The first byte after our end address
					   within vm_mm. �����ڴ�����Ľ�����ַ*/

	/* linked list of VM areas per task, sorted by address */
	struct vm_area_struct *vm_next, *vm_prev;  //˫������ �������е������ڴ�����

	pgprot_t vm_page_prot;		/* Access permissions of this VMA. �������ڴ�����ķ���Ȩ��*/
	unsigned long vm_flags;		/* Flags, VM_READ  see mm.h. ����ϵ�����޹ص��������� ��4λ����Ҳҳ����ĵ���λ*/

	struct rb_node vm_rb; //��������ӵ�  Ϊ�˿��ٶ�λ��ַ�����ĸ����� ���Բ��ú����

	/*
	 * For areas with an address space and backing store,
	 * linkage into the address_space->i_mmap prio tree, or
	 * linkage to the list of like vmas hanging off its node, or
	 * linkage of vma in the address_space->i_mmap_nonlinear list.
	 */
	/* shared���������ں�address space���� */
	union {
		struct {
			struct list_head list;/* �������������ӳ������� */
			void *parent;	/* aligns with prio_tree_node parent */
			struct vm_area_struct *head;
		} vm_set;

		struct raw_prio_tree_node prio_tree_node;/*����ӳ��������i_mmap������*/
	} shared;

	/*
	 * A file's MAP_PRIVATE vma can be in both i_mmap tree and anon_vma
	 * list, after a COW of one of the file pages.	A MAP_SHARED vma
	 * can only be in the i_mmap tree.  An anonymous MAP_PRIVATE, stack
	 * or brk vma (with NULL file) can only be in an anon_vma list.
	 */
	//���ڹ���Դ������ӳ��Ĺ���ҳ  ָ����ͬҳ��ӳ�䶼������һ��˫������anon_vma_node�䵱����Ԫ��
	struct list_head anon_vma_node;	/* Serialized by anon_vma->lock */
	struct anon_vma *anon_vma;	/* Serialized by page_table_lock ����vma����*/

	/* Function pointers to deal with this struct. */
	const struct vm_operations_struct *vm_ops; //���ļ�ϵͳfile�������ŵ��˴�

	/* Information about our backing store: */
	//�ļ�ӳ���ƫ����,��ֵֻ����ӳ�����ļ��Ĳ�������(�����ļ�ӳ��Ļ���ֵΪ0)
	unsigned long vm_pgoff;		/* Offset (within vm_file) in PAGE_SIZE
					   units, *not* PAGE_CACHE_SIZE ���ļ��е�ƫ��*/
				   	
	struct file * vm_file;		/* File we map to (can be NULL).  ��ӳ����ļ�*/
	void * vm_private_data;		/* was vm_pte (shared mem) ˽������*/
	unsigned long vm_truncate_count;/* truncate_count or restart_addr */

#ifndef CONFIG_MMU
	struct vm_region *vm_region;	/* NOMMU mapping region */
#endif
#ifdef CONFIG_NUMA
	struct mempolicy *vm_policy;	/* NUMA policy for the VMA */
#endif
};

struct core_thread {
	struct task_struct *task;
	struct core_thread *next;
};

struct core_state {
	atomic_t nr_threads;
	struct core_thread dumper;
	struct completion startup;
};

//�ڴ��������Ľṹ�� mm_struct�ṹ������һ�����̵����������ַ�ռ�
//vm_area_truct�����������ַ�ռ��һ�����䣨�����������
struct mm_struct {
	struct vm_area_struct * mmap;		/* list of VMAs *///ָ�������ڴ����������ͷ
											//��cat /proc/PID/maps ���Կ�����ͬ�������ڴ�ռ�
											
	struct rb_root mm_rb;////ָ�������ڴ�����ĺ����
	struct vm_area_struct * mmap_cache;	/* ��һ��find_vma�Ľ��*/

	//�����ڽ��̵�ַ�ռ���������Ч�Ľ��̵�ַ�ռ�ĺ���
	unsigned long (*get_unmapped_area) (struct file *filp,
				unsigned long addr, unsigned long len,
				unsigned long pgoff, unsigned long flags);

	//�ͷ�������ʱ���õķ����� 
	void (*unmap_area) (struct mm_struct *mm, unsigned long addr);
	
	unsigned long mmap_base;		/* base of mmap area �����ַ�ռ��������ڴ�ӳ�����ʼ��ַ
                                       ����get_unmapped_area()��mmap����Ϊ��ӳ���ҵ����ʵ�λ��
	                                   */

	unsigned long task_size;		/* �洢�˶�Ӧ���̵ĵ�ַ�ռ䳤�� ��ֵͨ��ΪTASK_SIZE
                                       ����64��ִ��32λ����  ��ֵΪ�ý��������ɼ��ĳ���
									   */
									   
	unsigned long cached_hole_size; 	/* if non-zero, the largest hole below free_area_cache */

	 //�ں˽����������̵�ַ�ռ������Ե�ַ�Ŀռ�
	unsigned long free_area_cache;		/* first hole of size cached_hole_size or larger */

	//ָ��ҳ���Ŀ¼ 
	//����ҳ��������ǽ�pgd����cr3�Ĵ���
	pgd_t * pgd;

	//ʹ�õ�ַ�ռ���û��� ���������̹߳��״˽ṹ���ֵΪ2
	atomic_t mm_users;			/* How many users with user space? */

	//�ڴ�����������ʹ�ü��������������ü�����ԭ����Ϊ0ʱ�������û��ٴ�ʹ��
	atomic_t mm_count;			/* How many references to "struct mm_struct" (users count as 1) */

	 //�������ĸ���
	int map_count;				/* number of VMAs */
	struct rw_semaphore mmap_sem;

	  //��������ҳ������ü�������
	spinlock_t page_table_lock;		/* Protects page tables and some counters */

    //����mm_struct�γɵ����� �������Ԫ��Ϊinit_mm ������������Ҫ��mmlist_lock
	struct list_head mmlist;		/* List of maybe swapped mm's.	These are globally strung
						 * together off init_mm.mmlist, and are protected
						 * by mmlist_lock
						 */

	/* Special counters, in some configurations protected by the
	 * page_table_lock, in other configurations by being atomic.
	 */
	mm_counter_t _file_rss;
	mm_counter_t _anon_rss;

	//����ӵ�е����ҳ����Ŀ
	unsigned long hiwater_rss;	/* High-watermark of RSS usage */

	//���������������ҳ����Ŀ
	unsigned long hiwater_vm;	/* High-water virtual memory usage */

	 //���̵�ַ�ռ�Ĵ�С����ס�޷���ҳ�ĸ����������ļ��ڴ�ӳ���ҳ������ִ���ڴ�ӳ���е�ҳ��
	unsigned long total_vm, locked_vm, shared_vm, exec_vm;
	unsigned long stack_vm, 
		          reserved_vm, 
		          def_flags, //Ϊ0��VM_LOCKED  VM_LOCKED��־ӳ���ҳ�޷������� �˱�־��Ҫ��mlockallϵͳ����������VM_LOCKED
		          nr_ptes;

	              //ά��code��
	unsigned long start_code,
		          end_code,

				  //ά��data��
		          start_data, 
		          end_data;
	
	unsigned long start_brk, //�ѵ���ʼ��ַ
		          brk, //������ǰ�Ľ�����ַ
		          
		          start_stack;//������ά��stack�οռ䷶Χ
		          
	unsigned long arg_start,//������ʼ��ַ  
		          arg_end,  //����������ַ
		           
		          env_start,//����������ʼ��ַ 
		          env_end;//��������������ַ

	unsigned long saved_auxv[AT_VECTOR_SIZE]; /* for /proc/PID/auxv */

	struct linux_binfmt *binfmt;

	cpumask_t cpu_vm_mask;

	/* Architecture-specific MM context */
	mm_context_t context;//��ϵ�ṹ��������

	/* Swap token stuff */
	/*
	 * Last value of global fault stamp as seen by this process.
	 * In other words, this value gives an indication of how long
	 * it has been since this task got the token.
	 * Look at mm/thrash.c
	 */
	unsigned int faultstamp;
	unsigned int token_priority;
	unsigned int last_interval;

	unsigned long flags; /* ״̬��־  ����λ��ʾ�Ƿ����coredump MMF_DUMPABLE_MASK SUID_DUMP_USER*/

	struct core_state *core_state; /* coredumping support ����ת����֧��*/
#ifdef CONFIG_AIO
	spinlock_t		ioctx_lock;//AIO IO������
	struct hlist_head	ioctx_list; //AIO IO����
#endif
#ifdef CONFIG_MM_OWNER
	/*
	 * "owner" points to a task that is regarded as the canonical
	 * user/owner of this mm. All of the following must be true in
	 * order for it to be changed:
	 *
	 * current == mm->owner
	 * current->mm != mm
	 * new_owner->mm == mm
	 * new_owner->alloc_lock is held
	 */
	struct task_struct *owner;
#endif

#ifdef CONFIG_PROC_FS
	/* store ref to file /proc/<pid>/exe symlink points to */
	struct file *exe_file;
	unsigned long num_exe_file_vmas;
#endif
#ifdef CONFIG_MMU_NOTIFIER
	struct mmu_notifier_mm *mmu_notifier_mm;
#endif
};

/* Future-safe accessor for struct mm_struct's cpu_vm_mask. */
#define mm_cpumask(mm) (&(mm)->cpu_vm_mask)

#endif /* _LINUX_MM_TYPES_H */
