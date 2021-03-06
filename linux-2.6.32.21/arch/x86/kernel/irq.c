/*
 * Common interrupt code for 32 and 64 bit
 */
#include <linux/cpu.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>
#include <linux/seq_file.h>
#include <linux/smp.h>
#include <linux/ftrace.h>

#include <asm/apic.h>
#include <asm/io_apic.h>
#include <asm/irq.h>
#include <asm/idle.h>
#include <asm/mce.h>
#include <asm/hw_irq.h>

atomic_t irq_err_count;

/* Function pointer for generic interrupt vector handling */
void (*generic_interrupt_extension)(void) = NULL;

/*
 * 'what should we do if we get a hw irq event on an illegal vector'.
 * each architecture has to answer this themselves.
 */
void ack_bad_irq(unsigned int irq)
{
	if (printk_ratelimit())
		pr_err("unexpected IRQ trap at vector %02x\n", irq);

	/*
	 * Currently unexpected vectors happen only on SMP and APIC.
	 * We _must_ ack these because every local APIC has only N
	 * irq slots per priority level, and a 'hanging, unacked' IRQ
	 * holds up an irq slot - in excessive cases (when multiple
	 * unexpected vectors occur) that might lock up the APIC
	 * completely.
	 * But only ack when the APIC is enabled -AK
	 */
	ack_APIC_irq();
}

#define irq_stats(x)		(&per_cpu(irq_stat, x))
/*
 * /proc/interrupts printing:
 */
static int show_other_interrupts(struct seq_file *p, int prec)
{
	int j;

	seq_printf(p, "%*s: ", prec, "NMI");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", irq_stats(j)->__nmi_count);
	seq_printf(p, "  Non-maskable interrupts\n");
#ifdef CONFIG_X86_LOCAL_APIC
	seq_printf(p, "%*s: ", prec, "LOC");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", irq_stats(j)->apic_timer_irqs);
	seq_printf(p, "  Local timer interrupts\n");

	seq_printf(p, "%*s: ", prec, "SPU");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", irq_stats(j)->irq_spurious_count);
	seq_printf(p, "  Spurious interrupts\n");
	seq_printf(p, "%*s: ", prec, "PMI");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", irq_stats(j)->apic_perf_irqs);
	seq_printf(p, "  Performance monitoring interrupts\n");
	seq_printf(p, "%*s: ", prec, "PND");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", irq_stats(j)->apic_pending_irqs);
	seq_printf(p, "  Performance pending work\n");
#endif
	if (generic_interrupt_extension) {
		seq_printf(p, "%*s: ", prec, "PLT");
		for_each_online_cpu(j)
			seq_printf(p, "%10u ", irq_stats(j)->generic_irqs);
		seq_printf(p, "  Platform interrupts\n");
	}
#ifdef CONFIG_SMP
	seq_printf(p, "%*s: ", prec, "RES");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", irq_stats(j)->irq_resched_count);
	seq_printf(p, "  Rescheduling interrupts\n");
	seq_printf(p, "%*s: ", prec, "CAL");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", irq_stats(j)->irq_call_count);
	seq_printf(p, "  Function call interrupts\n");
	seq_printf(p, "%*s: ", prec, "TLB");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", irq_stats(j)->irq_tlb_count);
	seq_printf(p, "  TLB shootdowns\n");
#endif
#ifdef CONFIG_X86_MCE
	seq_printf(p, "%*s: ", prec, "TRM");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", irq_stats(j)->irq_thermal_count);
	seq_printf(p, "  Thermal event interrupts\n");
# ifdef CONFIG_X86_MCE_THRESHOLD
	seq_printf(p, "%*s: ", prec, "THR");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", irq_stats(j)->irq_threshold_count);
	seq_printf(p, "  Threshold APIC interrupts\n");
# endif
#endif
#ifdef CONFIG_X86_MCE
	seq_printf(p, "%*s: ", prec, "MCE");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", per_cpu(mce_exception_count, j));
	seq_printf(p, "  Machine check exceptions\n");
	seq_printf(p, "%*s: ", prec, "MCP");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", per_cpu(mce_poll_count, j));
	seq_printf(p, "  Machine check polls\n");
#endif
	seq_printf(p, "%*s: %10u\n", prec, "ERR", atomic_read(&irq_err_count));
#if defined(CONFIG_X86_IO_APIC)
	seq_printf(p, "%*s: %10u\n", prec, "MIS", atomic_read(&irq_mis_count));
#endif
	return 0;
}

// 在/proc/interrupts中显示中断信息
//系统可用的中断数量主要由架构决定，x86 的具体数量可以参考以下定义
int show_interrupts(struct seq_file *p, void *v)
{
	unsigned long flags, any_count = 0;
	int i = *(loff_t *) v, j, prec;
	struct irqaction *action;
	struct irq_desc *desc;

	if (i > nr_irqs)
		return 0;

	//计算第一列的宽度prec
	//如果最大中断号小于1000，则 prec 为3，如果最大中断号大于1000小于10000，则 prec 为4，以此类推,最大宽度为10
	for (prec = 3, j = 1000; prec < 10 && j <= nr_irqs; ++prec)
		j *= 10;

	//如果外部中断都打印完毕
	if (i == nr_irqs)
		return show_other_interrupts(p, prec);

	/* print header */
	//打印头 主要是打印出 cpuX,有几个cpu打印几个
	if (i == 0) 
	{
	    //前面加 prec+8个空格
		seq_printf(p, "%*s", prec + 8, "");

       
		for_each_online_cpu(j)
			seq_printf(p, "CPU%-8d", j);//数字的右边保持8位 不足的在右边用空格填充

		seq_putc(p, '\n');
	}

	//根据中断号来获得中断描述符
	desc = irq_to_desc(i);
	if (!desc)
		return 0;
    
	spin_lock_irqsave(&desc->lock, flags);

	for_each_online_cpu(j)
		any_count |= kstat_irqs_cpu(i, j);//打印每个 CPU 对应的统计数量 .

	action = desc->action;
	
	if (!action && !any_count)
		goto out;
    
	//逻辑中断号
	//<逻辑中断号>
	seq_printf(p, "%*d: ", prec, i);

	//打印每个各个cpu的统计信息,中断在各CPU发生的次数
	//<中断次数percpu>
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", kstat_irqs_cpu(i, j));

    //<中断控制器名称>
	seq_printf(p, " %8s", desc->chip->name);

	//<中断描述符的名>
	seq_printf(p, "-%-8s", desc->name);

    //[中断源1,中断源2]
	//该中断号上的所有用户注册的中断名
	if (action) 
	{
		seq_printf(p, "  %s", action->name);
		while ((action = action->next) != NULL)
			seq_printf(p, ", %s", action->name);
	}

	seq_putc(p, '\n');
out:
	spin_unlock_irqrestore(&desc->lock, flags);
	
	return 0;
}

/*
 * /proc/stat helpers
 */
u64 arch_irq_stat_cpu(unsigned int cpu)
{
	u64 sum = irq_stats(cpu)->__nmi_count;

#ifdef CONFIG_X86_LOCAL_APIC
	sum += irq_stats(cpu)->apic_timer_irqs;
	sum += irq_stats(cpu)->irq_spurious_count;
	sum += irq_stats(cpu)->apic_perf_irqs;
	sum += irq_stats(cpu)->apic_pending_irqs;
#endif
	if (generic_interrupt_extension)
		sum += irq_stats(cpu)->generic_irqs;
#ifdef CONFIG_SMP
	sum += irq_stats(cpu)->irq_resched_count;
	sum += irq_stats(cpu)->irq_call_count;
	sum += irq_stats(cpu)->irq_tlb_count;
#endif
#ifdef CONFIG_X86_MCE
	sum += irq_stats(cpu)->irq_thermal_count;
# ifdef CONFIG_X86_MCE_THRESHOLD
	sum += irq_stats(cpu)->irq_threshold_count;
# endif
#endif
#ifdef CONFIG_X86_MCE
	sum += per_cpu(mce_exception_count, cpu);
	sum += per_cpu(mce_poll_count, cpu);
#endif
	return sum;
}

u64 arch_irq_stat(void)
{
	u64 sum = atomic_read(&irq_err_count);

#ifdef CONFIG_X86_IO_APIC
	sum += atomic_read(&irq_mis_count);
#endif
	return sum;
}


/*
 * do_IRQ handles all normal device IRQ's (the special
 * SMP cross-CPU interrupts have their own specific
 * handlers).
 */
 /*在arch/x86/kernel/entry_32.s
common_interrupt:
	addl $-0x80,(%esp) //Adjust vector into the [-256,-1] range 
	SAVE_ALL
	TRACE_IRQS_OFF
	movl %esp,%eax
	call do_IRQ
	jmp ret_from_intr
    ENDPROC(common_interrupt)
	CFI_ENDPROC
***********************************************/
unsigned int __irq_entry do_IRQ(struct pt_regs *regs)
{
	struct pt_regs *old_regs = set_irq_regs(regs);

	//获取中断号  orig_ax存放的是中断号的相反数
	unsigned vector = ~regs->orig_ax;
	unsigned irq;

	exit_idle();
	irq_enter();

	//获取中断号
	irq = __get_cpu_var(vector_irq)[vector];

	if (!handle_irq(irq, regs))
	{
		
		ack_APIC_irq();

		if (printk_ratelimit())
			pr_emerg("%s: %d.%d No irq handler for vector (irq %d)\n",
				__func__, smp_processor_id(), vector, irq);
	}

	irq_exit();//在里面会执行软中断

	set_irq_regs(old_regs);
	return 1;
}

/*
 * Handler for GENERIC_INTERRUPT_VECTOR.
 */
void smp_generic_interrupt(struct pt_regs *regs)
{
	struct pt_regs *old_regs = set_irq_regs(regs);

	ack_APIC_irq();

	exit_idle();

	irq_enter();

	inc_irq_stat(generic_irqs);

	if (generic_interrupt_extension)
		generic_interrupt_extension();

	irq_exit();

	set_irq_regs(old_regs);
}

EXPORT_SYMBOL_GPL(vector_used_by_percpu_irq);
