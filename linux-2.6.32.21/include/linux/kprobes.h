#ifndef _LINUX_KPROBES_H
#define _LINUX_KPROBES_H
/*
 *  Kernel Probes (KProbes)
 *  include/linux/kprobes.h
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Copyright (C) IBM Corporation, 2002, 2004
 *
 * 2002-Oct	Created by Vamsi Krishna S <vamsi_krishna@in.ibm.com> Kernel
 *		Probes initial implementation ( includes suggestions from
 *		Rusty Russell).
 * 2004-July	Suparna Bhattacharya <suparna@in.ibm.com> added jumper probes
 *		interface to access function arguments.
 * 2005-May	Hien Nguyen <hien@us.ibm.com> and Jim Keniston
 *		<jkenisto@us.ibm.com>  and Prasanna S Panchamukhi
 *		<prasanna@in.ibm.com> added function-return probes.
 */
#include <linux/linkage.h>
#include <linux/list.h>
#include <linux/notifier.h>
#include <linux/smp.h>
#include <linux/percpu.h>
#include <linux/spinlock.h>
#include <linux/rcupdate.h>
#include <linux/mutex.h>

#ifdef CONFIG_KPROBES
#include <asm/kprobes.h>

/* kprobe_status settings */
#define KPROBE_HIT_ACTIVE	0x00000001
#define KPROBE_HIT_SS		0x00000002
#define KPROBE_REENTER		0x00000004
#define KPROBE_HIT_SSDONE	0x00000008

/* Attach to insert probes on any functions which should be ignored*/
#define __kprobes	__attribute__((__section__(".kprobes.text")))
#else /* CONFIG_KPROBES */
typedef int kprobe_opcode_t;
struct arch_specific_insn {
	int dummy;
};
#define __kprobes
#endif /* CONFIG_KPROBES */

struct kprobe;
struct pt_regs;
struct kretprobe;

//被探测函数的返回地址保存在类型为 kretprobe_instance的变量中
struct kretprobe_instance;

typedef int (*kprobe_pre_handler_t) (struct kprobe *,  //是注册的struct kprobe探测实例
	                                      struct pt_regs *);//保存的触发断点前的寄存器状态

typedef int (*kprobe_break_handler_t) (struct kprobe *, struct pt_regs *);
typedef void (*kprobe_post_handler_t) (struct kprobe *, 
	                                         struct pt_regs *,
				                             unsigned long flags);
typedef int (*kprobe_fault_handler_t) (struct kprobe *, struct pt_regs *,
				       int trapnr);
typedef int (*kretprobe_handler_t) ( struct kretprobe_instance *,
				                           struct pt_regs *);

struct kprobe 
{	
	/*用于保存kprobe的全局hash表，以被探测的addr为key*/
	struct hlist_node hlist;

    /*当对同一个探测点存在多个探测函数时，所有的函数挂在这条链上*/
	//用于链接同一被探测点的不同探测kprobe
	/* list of kprobes for multi-handler support */
	struct list_head list;

	/*count the number of times this probe was temporarily disarmed */
	unsigned long nmissed;
	
	/*被探测的目标地址*/
	/* location of the probe point */
	kprobe_opcode_t *addr;

	 /*symblo_name的存在，允许用户指定函数名而非确定的地址*/
	/* Allow user to indicate symbol name of the probe point */
	const char *symbol_name;

	/*如果被探测点为函数内部某个指令，需要使用addr + offset的方式 被探测点在函数内部的偏移，用于探测函数内部的指令，如果该值为0表示函数的入口*/
	/* Offset into the symbol */
	unsigned int offset;

     /*探测函数，在目标探测点执行之前调用*/
	/* Called before addr is executed. */
	kprobe_pre_handler_t pre_handler;// pre_handler_kretprobe

     /*探测函数，在目标探测点执行之后调用*/
	/* Called after addr is executed, unless... */
	kprobe_post_handler_t post_handler;

	/*
	 * ... called if executing addr causes a fault (eg. page fault).
	 * Return 1 if it handled fault, otherwise kernel will see it.
	 */
	/*
	在执行pre_handler、post_handler或单步执行被探测指令时出现内存异常则会调用该回调函数
	*/ 
	kprobe_fault_handler_t fault_handler;

	/*
	 * ... called if breakpoint trap occurs in probe handler.
	 * Return 1 if it handled break, otherwise kernel will see it.
	 */
	/*
	在执行某一kprobe过程中触发了断点指令后会调用该函数，用于实现jprobe
	*/ 
	kprobe_break_handler_t break_handler;

    /*opcode 以及 ainsn 用于保存被替换的指令码*/
	/* Saved opcode (which has been replaced with breakpoint) */
	kprobe_opcode_t opcode;//保存的被探测点原始指令；

	/* copy of the original instruction */
	struct arch_specific_insn ainsn;//被复制的被探测点的原始指令，用于单步执行

	/*
	 * Indicates various status flags.
	 * Protected by kprobe_mutex after this kprobe is registered.
	 */
	u32 flags;
};

/* Kprobe status flags */
#define KPROBE_FLAG_GONE	1 /* breakpoint has already gone */
#define KPROBE_FLAG_DISABLED	2 /* probe is temporarily disabled */

/* Has this kprobe gone ? */
static inline int kprobe_gone(struct kprobe *p)
{
	return p->flags & KPROBE_FLAG_GONE;
}

/* Is this kprobe disabled ? */
static inline int kprobe_disabled(struct kprobe *p)
{
	return p->flags & (KPROBE_FLAG_DISABLED | KPROBE_FLAG_GONE);
}
/*
 * Special probe type that uses setjmp-longjmp type tricks to resume
 * execution at a specified entry with a matching prototype corresponding
 * to the probed function - a trick to enable arguments to become
 * accessible seamlessly by probe handling logic.
 * Note:
 * Because of the way compilers allocate stack space for local variables
 * etc upfront, regardless of sub-scopes within a function, this mirroring
 * principle currently works only for probes placed on function entry points.
 */
struct jprobe {
	struct kprobe kp;
	void *entry;	/* probe handling code to jump to */
};

/* For backward compatibility with old code using JPROBE_ENTRY() */
#define JPROBE_ENTRY(handler)	(handler)

/*
 * Function-return probe -
 * Note:
 * User needs to provide a handler function, and initialize maxactive.
 * maxactive - The maximum number of instances of the probed function that
 * can be active concurrently.
 * nmissed - tracks the number of times the probed function's return was
 * ignored, due to maxactive being too low.
 *
 */
struct kretprobe {
	struct kprobe kp; //该成员是kretprobe内嵌的struct kprobe结构
	kretprobe_handler_t handler;//该成员是调试者定义的回调函数,handler在被探测函数返回后被调用
	kretprobe_handler_t entry_handler;//entry_handler会在被探测函数执行之前被调用

             /*******************
               maxactive表示同时支持并行探测的上限，因为kretprobe会跟踪一个函数从开始到结束，因此对于一些调用比较频繁的被探测函数，在探测的时间段内重入的概率比较高，这个maxactive字段值表示在重入情况发生时，
               支持同时检测的进程数（执行流数）的上限，若并行触发的数量超过了这个上限，
               则kretprobe不会进行跟踪探测，仅仅增加nmissed字段的值以作提示
	        ****************************/
	int maxactive;//指定了被探测函数可以被同时探测的实例数
	
	int nmissed;//missed字段将记录被丢失 的探测点执行数
	size_t data_size;//表示kretprobe私有数据的大小,在注册kretprobe时会根据该大小预留空间
	struct hlist_head free_instances;//用于链接未使用的返回地址实例，在注册时初始化,空闲的kretprobe运行实例链表,空闲实例struct kretprobe_instance结构体表示
	spinlock_t lock;
};

//该结构表示一个返回地址实例
/*
因为函数每次被调用的地方不同，这造成了返回地址不同，因此需要为每一次发生的调用记录在这样一个结构里面
*/
/********
这个结构体表示kretprobe的运行实例，前文说过被探测函数在跟踪期间可能存在并发执行的现象，因此kretprobe使用一个kretprobe_instance来跟踪一个执行流，支持的上限为maxactive
在没有触发探测时，所有的kretprobe_instance实例都保存在free_instances表中,每当有执行流触发一次kretprobe探测,都会从该表中取出一个空闲的kretprobe_instance实例用来跟踪
*/
struct kretprobe_instance {
	struct hlist_node hlist;  //该成员被链接入kretprobe的used_instances或是free_instances链表
	struct kretprobe *rp;     //该成员指向所属的kretprobe结构
	kprobe_opcode_t *ret_addr;//该成员用于记录被探测函数正真的返回地址
	struct task_struct *task; //该成员记录当时运行的进程
	char data[0];//最后data保存用户使用的kretprobe私有数据,它会在整个kretprobe探测运行期间在entry_handler和handler回调函数之间进行传递
};

struct kretprobe_blackpoint {
	const char *name; //其中name字段表示函数名
	void *addr;       //addr表示函数的运行地址
};

struct kprobe_blackpoint {
	const char *name;   //符号名 
	unsigned long start_addr; //符号的起始地址
	unsigned long range;  //符号的范围
};

#ifdef CONFIG_KPROBES
DECLARE_PER_CPU(struct kprobe *, current_kprobe);
DECLARE_PER_CPU(struct kprobe_ctlblk, kprobe_ctlblk);

/*
 * For #ifdef avoidance:
 */
static inline int kprobes_built_in(void)
{
	return 1;
}

#ifdef CONFIG_KRETPROBES
extern void arch_prepare_kretprobe(struct kretprobe_instance *ri,
				   struct pt_regs *regs);
extern int arch_trampoline_kprobe(struct kprobe *p);
#else /* CONFIG_KRETPROBES */
static inline void arch_prepare_kretprobe(struct kretprobe *rp,
					struct pt_regs *regs)
{
}
static inline int arch_trampoline_kprobe(struct kprobe *p)
{
	return 0;
}
#endif /* CONFIG_KRETPROBES */

extern struct kretprobe_blackpoint kretprobe_blacklist[];

static inline void kretprobe_assert(struct kretprobe_instance *ri,
	unsigned long orig_ret_address, unsigned long trampoline_address)
{
	if (!orig_ret_address || (orig_ret_address == trampoline_address)) {
		printk("kretprobe BUG!: Processing kretprobe %p @ %p\n",
				ri->rp, ri->rp->kp.addr);
		BUG();
	}
}

#ifdef CONFIG_KPROBES_SANITY_TEST
extern int init_test_probes(void);
#else
static inline int init_test_probes(void)
{
	return 0;
}
#endif /* CONFIG_KPROBES_SANITY_TEST */

extern int arch_prepare_kprobe(struct kprobe *p);
extern void arch_arm_kprobe(struct kprobe *p);
extern void arch_disarm_kprobe(struct kprobe *p);
extern int arch_init_kprobes(void);
extern void show_registers(struct pt_regs *regs);
extern kprobe_opcode_t *get_insn_slot(void);
extern void free_insn_slot(kprobe_opcode_t *slot, int dirty);
extern void kprobes_inc_nmissed_count(struct kprobe *p);

/* Get the kprobe at this addr (if any) - called with preemption disabled */
struct kprobe *get_kprobe(void *addr);
void kretprobe_hash_lock(struct task_struct *tsk,
			 struct hlist_head **head, unsigned long *flags);
void kretprobe_hash_unlock(struct task_struct *tsk, unsigned long *flags);
struct hlist_head * kretprobe_inst_table_head(struct task_struct *tsk);

/* kprobe_running() will just return the current_kprobe on this CPU */
static inline struct kprobe *kprobe_running(void)
{
	return (__get_cpu_var(current_kprobe));
}

static inline void reset_current_kprobe(void)
{
	__get_cpu_var(current_kprobe) = NULL;
}

static inline struct kprobe_ctlblk *get_kprobe_ctlblk(void)
{
	return (&__get_cpu_var(kprobe_ctlblk));
}

int register_kprobe(struct kprobe *p);
void unregister_kprobe(struct kprobe *p);
int register_kprobes(struct kprobe **kps, int num);
void unregister_kprobes(struct kprobe **kps, int num);
int setjmp_pre_handler(struct kprobe *, struct pt_regs *);
int longjmp_break_handler(struct kprobe *, struct pt_regs *);
int register_jprobe(struct jprobe *p);
void unregister_jprobe(struct jprobe *p);
int register_jprobes(struct jprobe **jps, int num);
void unregister_jprobes(struct jprobe **jps, int num);
void jprobe_return(void);
unsigned long arch_deref_entry_point(void *);

int register_kretprobe(struct kretprobe *rp);
void unregister_kretprobe(struct kretprobe *rp);
int register_kretprobes(struct kretprobe **rps, int num);
void unregister_kretprobes(struct kretprobe **rps, int num);

void kprobe_flush_task(struct task_struct *tk);
void recycle_rp_inst(struct kretprobe_instance *ri, struct hlist_head *head);

int disable_kprobe(struct kprobe *kp);
int enable_kprobe(struct kprobe *kp);

#else /* !CONFIG_KPROBES: */

static inline int kprobes_built_in(void)
{
	return 0;
}
static inline int kprobe_fault_handler(struct pt_regs *regs, int trapnr)
{
	return 0;
}
static inline struct kprobe *get_kprobe(void *addr)
{
	return NULL;
}
static inline struct kprobe *kprobe_running(void)
{
	return NULL;
}
static inline int register_kprobe(struct kprobe *p)
{
	return -ENOSYS;
}
static inline int register_kprobes(struct kprobe **kps, int num)
{
	return -ENOSYS;
}
static inline void unregister_kprobe(struct kprobe *p)
{
}
static inline void unregister_kprobes(struct kprobe **kps, int num)
{
}
static inline int register_jprobe(struct jprobe *p)
{
	return -ENOSYS;
}
static inline int register_jprobes(struct jprobe **jps, int num)
{
	return -ENOSYS;
}
static inline void unregister_jprobe(struct jprobe *p)
{
}
static inline void unregister_jprobes(struct jprobe **jps, int num)
{
}
static inline void jprobe_return(void)
{
}
static inline int register_kretprobe(struct kretprobe *rp)
{
	return -ENOSYS;
}
static inline int register_kretprobes(struct kretprobe **rps, int num)
{
	return -ENOSYS;
}
static inline void unregister_kretprobe(struct kretprobe *rp)
{
}
static inline void unregister_kretprobes(struct kretprobe **rps, int num)
{
}
static inline void kprobe_flush_task(struct task_struct *tk)
{
}
static inline int disable_kprobe(struct kprobe *kp)
{
	return -ENOSYS;
}
static inline int enable_kprobe(struct kprobe *kp)
{
	return -ENOSYS;
}
#endif /* CONFIG_KPROBES */
static inline int disable_kretprobe(struct kretprobe *rp)
{
	return disable_kprobe(&rp->kp);
}
static inline int enable_kretprobe(struct kretprobe *rp)
{
	return enable_kprobe(&rp->kp);
}
static inline int disable_jprobe(struct jprobe *jp)
{
	return disable_kprobe(&jp->kp);
}
static inline int enable_jprobe(struct jprobe *jp)
{
	return enable_kprobe(&jp->kp);
}

#endif /* _LINUX_KPROBES_H */
