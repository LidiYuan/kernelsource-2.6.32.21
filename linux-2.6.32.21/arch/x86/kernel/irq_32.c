/*
 *	Copyright (C) 1992, 1998 Linus Torvalds, Ingo Molnar
 *
 * This file contains the lowest level x86-specific interrupt
 * entry, irq-stacks and irq statistics code. All the remaining
 * irq logic is done by the generic kernel/irq/ code and
 * by the x86-specific irq controller code. (e.g. i8259.c and
 * io_apic.c.)
 */

#include <linux/module.h>
#include <linux/seq_file.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>
#include <linux/notifier.h>
#include <linux/cpu.h>
#include <linux/delay.h>
#include <linux/uaccess.h>
#include <linux/percpu.h>

#include <asm/apic.h>

DEFINE_PER_CPU_SHARED_ALIGNED(irq_cpustat_t, irq_stat);
EXPORT_PER_CPU_SYMBOL(irq_stat);

DEFINE_PER_CPU(struct pt_regs *, irq_regs);
EXPORT_PER_CPU_SYMBOL(irq_regs);

#ifdef CONFIG_DEBUG_STACKOVERFLOW
/* Debugging check for stack overflow: is there less than 1KB free? */
static int check_stack_overflow(void)
{
	long sp;

	__asm__ __volatile__("andl %%esp,%0" :
			     "=r" (sp) : "0" (THREAD_SIZE - 1));

	return sp < (sizeof(struct thread_info) + STACK_WARN);
}

static void print_stack_overflow(void)
{
	printk(KERN_WARNING "low stack detected by irq handler\n");
	dump_stack();
}

#else
static inline int check_stack_overflow(void) { return 0; }
static inline void print_stack_overflow(void) { }
#endif

#ifdef CONFIG_4KSTACKS
/*
 * per-CPU IRQ handling contexts (thread information and stack)
 */
union irq_ctx {
	struct thread_info      tinfo;
	u32                     stack[THREAD_SIZE/sizeof(u32)];
} __attribute__((aligned(PAGE_SIZE)));

static DEFINE_PER_CPU(union irq_ctx *, hardirq_ctx);
static DEFINE_PER_CPU(union irq_ctx *, softirq_ctx);

static DEFINE_PER_CPU_PAGE_ALIGNED(union irq_ctx, hardirq_stack);
static DEFINE_PER_CPU_PAGE_ALIGNED(union irq_ctx, softirq_stack);

static void call_on_stack(void *func, void *stack)
{
	asm volatile("xchgl	%%ebx,%%esp	\n"
		     "call	*%%edi		\n"
		     "movl	%%ebx,%%esp	\n"
		     : "=b" (stack)
		     : "0" (stack),
		       "D"(func)
		     : "memory", "cc", "edx", "ecx", "eax");
}

static inline int
execute_on_irq_stack(int overflow, struct irq_desc *desc, int irq)
{
	union irq_ctx *curctx, *irqctx;
	u32 *isp, arg1, arg2;

	curctx = (union irq_ctx *) current_thread_info();
	irqctx = __get_cpu_var(hardirq_ctx);

	/*
	 * this is where we switch to the IRQ stack. However, if we are
	 * already using the IRQ stack (because we interrupted a hardirq
	 * handler) we can't do that and just have to keep using the
	 * current stack (which is the irq stack already after all)
	 */
	if (unlikely(curctx == irqctx))
		return 0;

	/* build the stack frame on the IRQ stack */
	isp = (u32 *) ((char *)irqctx + sizeof(*irqctx));
	irqctx->tinfo.task = curctx->tinfo.task;
	irqctx->tinfo.previous_esp = current_stack_pointer;

	/*
	 * Copy the softirq bits in preempt_count so that the
	 * softirq checks work in the hardirq context.
	 */
	irqctx->tinfo.preempt_count =
		(irqctx->tinfo.preempt_count & ~SOFTIRQ_MASK) |
		(curctx->tinfo.preempt_count & SOFTIRQ_MASK);

	if (unlikely(overflow))
		call_on_stack(print_stack_overflow, isp);

	asm volatile("xchgl	%%ebx,%%esp	\n"
		     "call	*%%edi		\n"
		     "movl	%%ebx,%%esp	\n"
		     : "=a" (arg1), "=d" (arg2), "=b" (isp)
		     :  "0" (irq),   "1" (desc),  "2" (isp),
			"D" (desc->handle_irq)
		     : "memory", "cc", "ecx");
	return 1;
}

/*
 * allocate per-cpu stacks for hardirq and for softirq processing
 */
void __cpuinit irq_ctx_init(int cpu)
{
	union irq_ctx *irqctx;

	if (per_cpu(hardirq_ctx, cpu))
		return;

	irqctx = &per_cpu(hardirq_stack, cpu);
	irqctx->tinfo.task		= NULL;
	irqctx->tinfo.exec_domain	= NULL;
	irqctx->tinfo.cpu		= cpu;
	irqctx->tinfo.preempt_count	= HARDIRQ_OFFSET;
	irqctx->tinfo.addr_limit	= MAKE_MM_SEG(0);

	per_cpu(hardirq_ctx, cpu) = irqctx;

	irqctx = &per_cpu(softirq_stack, cpu);
	irqctx->tinfo.task		= NULL;
	irqctx->tinfo.exec_domain	= NULL;
	irqctx->tinfo.cpu		= cpu;
	irqctx->tinfo.preempt_count	= 0;
	irqctx->tinfo.addr_limit	= MAKE_MM_SEG(0);

	per_cpu(softirq_ctx, cpu) = irqctx;

	printk(KERN_DEBUG "CPU %u irqstacks, hard=%p soft=%p\n",cpu, per_cpu(hardirq_ctx, cpu),  per_cpu(softirq_ctx, cpu));
}

void irq_ctx_exit(int cpu)
{
	per_cpu(hardirq_ctx, cpu) = NULL;
}

asmlinkage void do_softirq(void)
{
	unsigned long flags;
	struct thread_info *curctx;
	union irq_ctx *irqctx;
	u32 *isp;
    
	//检查是否处于硬件或者软中断中  此处一看 貌似执行软中断的实收 硬中断是关闭的..  看后面
	//__do_softirq中会开启硬中断
	if (in_interrupt())
		return;

	local_irq_save(flags);

	if (local_softirq_pending()) 
	{
		curctx = current_thread_info();
		irqctx = __get_cpu_var(softirq_ctx);
		irqctx->tinfo.task = curctx->task;
		irqctx->tinfo.previous_esp = current_stack_pointer;

		/* build the stack frame on the softirq stack */
		isp = (u32 *) ((char *)irqctx + sizeof(*irqctx));

		call_on_stack(__do_softirq, isp);
		/*
		 * Shouldnt happen, we returned above if in_interrupt():
		 */
		WARN_ON_ONCE(softirq_count());
	}

	local_irq_restore(flags);
}

#else
static inline int
execute_on_irq_stack(int overflow, struct irq_desc *desc, int irq) { return 0; }
#endif

bool handle_irq(unsigned irq, struct pt_regs *regs)
{
	struct irq_desc *desc;
	int overflow;

	overflow = check_stack_overflow();

	desc = irq_to_desc(irq);// //根据中断号在数组irq_desc中找到一项
	if (unlikely(!desc))
		return false;

	if (!execute_on_irq_stack(overflow, desc, irq)) {
		if (unlikely(overflow))
			print_stack_overflow();
		desc->handle_irq(irq, desc);
		//此处是在init_IRQ中->native_init_IRQ->init_ISA_irqs 设置desc->handle_irq=handle_level_irq
	}

	return true;
}

#ifdef CONFIG_HOTPLUG_CPU

/* A cpu has been removed from cpu_online_mask.  Reset irq affinities. */
void fixup_irqs(void)
{
	unsigned int irq;
	struct irq_desc *desc;

	for_each_irq_desc(irq, desc) {
		const struct cpumask *affinity;

		if (!desc)
			continue;
		if (irq == 2)
			continue;

		affinity = desc->affinity;
		if (cpumask_any_and(affinity, cpu_online_mask) >= nr_cpu_ids) {
			printk("Breaking affinity for irq %i\n", irq);
			affinity = cpu_all_mask;
		}
		if (desc->chip->set_affinity)
			desc->chip->set_affinity(irq, affinity);
		else if (desc->action)
			printk_once("Cannot set affinity for irq %i\n", irq);
	}

#if 0
	barrier();
	/* Ingo Molnar says: "after the IO-APIC masks have been redirected
	   [note the nop - the interrupt-enable boundary on x86 is two
	   instructions from sti] - to flush out pending hardirqs and
	   IPIs. After this point nothing is supposed to reach this CPU." */
	__asm__ __volatile__("sti; nop; cli");
	barrier();
#else
	/* That doesn't seem sufficient.  Give it 1ms. */
	local_irq_enable();
	mdelay(1);
	local_irq_disable();
#endif
}
#endif

