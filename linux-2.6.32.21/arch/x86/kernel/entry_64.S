/*
 *  linux/arch/x86_64/entry.S
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *  Copyright (C) 2000, 2001, 2002  Andi Kleen SuSE Labs
 *  Copyright (C) 2000  Pavel Machek <pavel@suse.cz>
 */

/*
 * entry.S contains the system-call and fault low-level handling routines.
 *
 * NOTE: This code handles signal-recognition, which happens every time
 * after an interrupt and after each system call.
 *
 * Normal syscalls and interrupts don't save a full stack frame, this is
 * only done for syscall tracing, signals or fork/exec et.al.
 *
 * A note on terminology:
 * - top of stack: Architecture defined interrupt frame from SS to RIP
 * at the top of the kernel process stack.
 * - partial stack frame: partially saved registers upto R11.
 * - full stack frame: Like partial stack frame, but all register saved.
 *
 * Some macro usage:
 * - CFI macros are used to generate dwarf2 unwind information for better
 * backtraces. They don't change any code.
 * - SAVE_ALL/RESTORE_ALL - Save/restore all registers
 * - SAVE_ARGS/RESTORE_ARGS - Save/restore registers that C functions modify.
 * There are unfortunately lots of special cases where some registers
 * not touched. The macro is a big mess that should be cleaned up.
 * - SAVE_REST/RESTORE_REST - Handle the registers not saved by SAVE_ARGS.
 * Gives a full stack frame.
 * - ENTRY/END Define functions in the symbol table.
 * - FIXUP_TOP_OF_STACK/RESTORE_TOP_OF_STACK - Fix up the hardware stack
 * frame that is otherwise undefined after a SYSCALL
 * - TRACE_IRQ_* - Trace hard interrupt state for lock debugging.
 * - errorentry/paranoidentry/zeroentry - Define exception entry points.
 */

#include <linux/linkage.h>
#include <asm/segment.h>
#include <asm/cache.h>
#include <asm/errno.h>
#include <asm/dwarf2.h>
#include <asm/calling.h>
#include <asm/asm-offsets.h>
#include <asm/msr.h>
#include <asm/unistd.h>
#include <asm/thread_info.h>
#include <asm/hw_irq.h>
#include <asm/page_types.h>
#include <asm/irqflags.h>
#include <asm/paravirt.h>
#include <asm/ftrace.h>
#include <asm/percpu.h>

/* Avoid __ASSEMBLER__'ifying <linux/audit.h> just for this.  */
#include <linux/elf-em.h>
#define AUDIT_ARCH_X86_64	(EM_X86_64|__AUDIT_ARCH_64BIT|__AUDIT_ARCH_LE)
#define __AUDIT_ARCH_64BIT 0x80000000
#define __AUDIT_ARCH_LE	   0x40000000

	.code64
#ifdef CONFIG_FUNCTION_TRACER
#ifdef CONFIG_DYNAMIC_FTRACE
ENTRY(mcount)
	retq
END(mcount)

ENTRY(ftrace_caller)
	cmpl $0, function_trace_stop
	jne  ftrace_stub

	MCOUNT_SAVE_FRAME

	movq 0x38(%rsp), %rdi
	movq 8(%rbp), %rsi
	subq $MCOUNT_INSN_SIZE, %rdi

GLOBAL(ftrace_call)
	call ftrace_stub

	MCOUNT_RESTORE_FRAME

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
GLOBAL(ftrace_graph_call)
	jmp ftrace_stub
#endif

GLOBAL(ftrace_stub)
	retq
END(ftrace_caller)

#else /* ! CONFIG_DYNAMIC_FTRACE */
ENTRY(mcount)
	cmpl $0, function_trace_stop
	jne  ftrace_stub

	cmpq $ftrace_stub, ftrace_trace_function
	jnz trace

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	cmpq $ftrace_stub, ftrace_graph_return
	jnz ftrace_graph_caller

	cmpq $ftrace_graph_entry_stub, ftrace_graph_entry
	jnz ftrace_graph_caller
#endif

GLOBAL(ftrace_stub)
	retq

trace:
	MCOUNT_SAVE_FRAME

	movq 0x38(%rsp), %rdi
	movq 8(%rbp), %rsi
	subq $MCOUNT_INSN_SIZE, %rdi

	call   *ftrace_trace_function

	MCOUNT_RESTORE_FRAME

	jmp ftrace_stub
END(mcount)
#endif /* CONFIG_DYNAMIC_FTRACE */
#endif /* CONFIG_FUNCTION_TRACER */

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
ENTRY(ftrace_graph_caller)
	cmpl $0, function_trace_stop
	jne ftrace_stub

	MCOUNT_SAVE_FRAME

	leaq 8(%rbp), %rdi
	movq 0x38(%rsp), %rsi
	movq (%rbp), %rdx
	subq $MCOUNT_INSN_SIZE, %rsi

	call	prepare_ftrace_return

	MCOUNT_RESTORE_FRAME

	retq
END(ftrace_graph_caller)

GLOBAL(return_to_handler)
	subq  $24, %rsp

	/* Save the return values */
	movq %rax, (%rsp)
	movq %rdx, 8(%rsp)
	movq %rbp, %rdi

	call ftrace_return_to_handler

	movq %rax, 16(%rsp)
	movq 8(%rsp), %rdx
	movq (%rsp), %rax
	addq $16, %rsp
	retq
#endif


#ifndef CONFIG_PREEMPT
#define retint_kernel retint_restore_args
#endif

#ifdef CONFIG_PARAVIRT
ENTRY(native_usergs_sysret64)
	swapgs
	sysretq
ENDPROC(native_usergs_sysret64)
#endif /* CONFIG_PARAVIRT */


.macro TRACE_IRQS_IRETQ offset=ARGOFFSET
#ifdef CONFIG_TRACE_IRQFLAGS
	bt   $9,EFLAGS-\offset(%rsp)	/* interrupts off? */
	jnc  1f
	TRACE_IRQS_ON
1:
#endif
.endm

/*
 * C code is not supposed to know about undefined top of stack. Every time
 * a C function with an pt_regs argument is called from the SYSCALL based
 * fast path FIXUP_TOP_OF_STACK is needed.
 * RESTORE_TOP_OF_STACK syncs the syscall state after any possible ptregs
 * manipulation.
 */

	/* %rsp:at FRAMEEND */
.macro FIXUP_TOP_OF_STACK tmp offset=0

    #将per-cpu变量old_rsp值 保存到tmp中  
	movq PER_CPU_VAR(old_rsp),\tmp

	#将用户的栈地址放到 堆栈中保存rsp的地方
	movq \tmp,RSP+\offset(%rsp)

	#将用户的ds cs放到栈中保存CS和ds的地方
	movq $__USER_DS,SS+\offset(%rsp)
	movq $__USER_CS,CS+\offset(%rsp)
  
	movq $-1,RCX+\offset(%rsp)
	
	movq R11+\offset(%rsp),\tmp  /* get eflags */

    #在栈中存放eflags
	movq \tmp,EFLAGS+\offset(%rsp)
.endm


.macro RESTORE_TOP_OF_STACK tmp offset=0
	movq RSP+\offset(%rsp),\tmp
	movq \tmp,PER_CPU_VAR(old_rsp)
	movq EFLAGS+\offset(%rsp),\tmp
	movq \tmp,R11+\offset(%rsp)
.endm

	.macro FAKE_STACK_FRAME child_rip
	/* push in order ss, rsp, eflags, cs, rip */
	xorl %eax, %eax
	pushq $__KERNEL_DS /* ss */
	CFI_ADJUST_CFA_OFFSET	8
	/*CFI_REL_OFFSET	ss,0*/
	pushq %rax /* rsp */
	CFI_ADJUST_CFA_OFFSET	8
	CFI_REL_OFFSET	rsp,0
	pushq $X86_EFLAGS_IF /* eflags - interrupts on */
	CFI_ADJUST_CFA_OFFSET	8
	/*CFI_REL_OFFSET	rflags,0*/
	pushq $__KERNEL_CS /* cs */
	CFI_ADJUST_CFA_OFFSET	8
	/*CFI_REL_OFFSET	cs,0*/
	pushq \child_rip /* rip */
	CFI_ADJUST_CFA_OFFSET	8
	CFI_REL_OFFSET	rip,0
	pushq	%rax /* orig rax */
	CFI_ADJUST_CFA_OFFSET	8
	.endm

	.macro UNFAKE_STACK_FRAME
	addq $8*6, %rsp
	CFI_ADJUST_CFA_OFFSET	-(6*8)
	.endm

/*
 * initial frame state for interrupts (and exceptions without error code)
 */
	.macro EMPTY_FRAME start=1 offset=0
	.if \start
	CFI_STARTPROC simple
	CFI_SIGNAL_FRAME
	CFI_DEF_CFA rsp,8+\offset
	.else
	CFI_DEF_CFA_OFFSET 8+\offset
	.endif
	.endm

/*
 * initial frame state for interrupts (and exceptions without error code)
 */
	.macro INTR_FRAME start=1 offset=0
	EMPTY_FRAME \start, SS+8+\offset-RIP
	/*CFI_REL_OFFSET ss, SS+\offset-RIP*/
	CFI_REL_OFFSET rsp, RSP+\offset-RIP
	/*CFI_REL_OFFSET rflags, EFLAGS+\offset-RIP*/
	/*CFI_REL_OFFSET cs, CS+\offset-RIP*/
	CFI_REL_OFFSET rip, RIP+\offset-RIP
	.endm

/*
 * initial frame state for exceptions with error code (and interrupts
 * with vector already pushed)
 */
	.macro XCPT_FRAME start=1 offset=0
	INTR_FRAME \start, RIP+\offset-ORIG_RAX
	/*CFI_REL_OFFSET orig_rax, ORIG_RAX-ORIG_RAX*/
	.endm

/*
 * frame that enables calling into C.
 */
	.macro PARTIAL_FRAME start=1 offset=0
	XCPT_FRAME \start, ORIG_RAX+\offset-ARGOFFSET
	CFI_REL_OFFSET rdi, RDI+\offset-ARGOFFSET
	CFI_REL_OFFSET rsi, RSI+\offset-ARGOFFSET
	CFI_REL_OFFSET rdx, RDX+\offset-ARGOFFSET
	CFI_REL_OFFSET rcx, RCX+\offset-ARGOFFSET
	CFI_REL_OFFSET rax, RAX+\offset-ARGOFFSET
	CFI_REL_OFFSET r8, R8+\offset-ARGOFFSET
	CFI_REL_OFFSET r9, R9+\offset-ARGOFFSET
	CFI_REL_OFFSET r10, R10+\offset-ARGOFFSET
	CFI_REL_OFFSET r11, R11+\offset-ARGOFFSET
	.endm

/*
 * frame that enables passing a complete pt_regs to a C function.
 */
	.macro DEFAULT_FRAME start=1 offset=0
	PARTIAL_FRAME \start, R11+\offset-R15
	CFI_REL_OFFSET rbx, RBX+\offset
	CFI_REL_OFFSET rbp, RBP+\offset
	CFI_REL_OFFSET r12, R12+\offset
	CFI_REL_OFFSET r13, R13+\offset
	CFI_REL_OFFSET r14, R14+\offset
	CFI_REL_OFFSET r15, R15+\offset
	.endm

/* save partial stack frame */
ENTRY(save_args)
	XCPT_FRAME
	cld
	movq_cfi rdi, RDI+16-ARGOFFSET
	movq_cfi rsi, RSI+16-ARGOFFSET
	movq_cfi rdx, RDX+16-ARGOFFSET
	movq_cfi rcx, RCX+16-ARGOFFSET
	movq_cfi rax, RAX+16-ARGOFFSET
	movq_cfi  r8,  R8+16-ARGOFFSET
	movq_cfi  r9,  R9+16-ARGOFFSET
	movq_cfi r10, R10+16-ARGOFFSET
	movq_cfi r11, R11+16-ARGOFFSET

	leaq -ARGOFFSET+16(%rsp),%rdi	/* arg1 for handler */
	movq_cfi rbp, 8		/* push %rbp */
	leaq 8(%rsp), %rbp		/* mov %rsp, %ebp */
	testl $3, CS(%rdi)
	je 1f
	SWAPGS
	/*
	 * irq_count is used to check if a CPU is already on an interrupt stack
	 * or not. While this is essentially redundant with preempt_count it is
	 * a little cheaper to use a separate counter in the PDA (short of
	 * moving irq_enter into assembly, which would be too much work)
	 */
1:	incl PER_CPU_VAR(irq_count)
	jne 2f
	popq_cfi %rax			/* move return address... */
	mov PER_CPU_VAR(irq_stack_ptr),%rsp
	EMPTY_FRAME 0
	pushq_cfi %rbp			/* backlink for unwinder */
	pushq_cfi %rax			/* ... to the new stack */
	/*
	 * We entered an interrupt context - irqs are off:
	 */
2:	TRACE_IRQS_OFF
	ret
	CFI_ENDPROC
END(save_args)

ENTRY(save_rest)
	PARTIAL_FRAME 1 REST_SKIP+8
	movq 5*8+16(%rsp), %r11	/* save return address */
	movq_cfi rbx, RBX+16
	movq_cfi rbp, RBP+16
	movq_cfi r12, R12+16
	movq_cfi r13, R13+16
	movq_cfi r14, R14+16
	movq_cfi r15, R15+16
	movq %r11, 8(%rsp)	/* return address */
	FIXUP_TOP_OF_STACK %r11, 16
	ret
	CFI_ENDPROC
END(save_rest)

/* save complete stack frame */
	.pushsection .kprobes.text, "ax"
ENTRY(save_paranoid)
	XCPT_FRAME 1 RDI+8
	cld
	movq_cfi rdi, RDI+8
	movq_cfi rsi, RSI+8
	movq_cfi rdx, RDX+8
	movq_cfi rcx, RCX+8
	movq_cfi rax, RAX+8
	movq_cfi r8, R8+8
	movq_cfi r9, R9+8
	movq_cfi r10, R10+8
	movq_cfi r11, R11+8
	movq_cfi rbx, RBX+8
	movq_cfi rbp, RBP+8
	movq_cfi r12, R12+8
	movq_cfi r13, R13+8
	movq_cfi r14, R14+8
	movq_cfi r15, R15+8
	movl $1,%ebx
	movl $MSR_GS_BASE,%ecx
	rdmsr
	testl %edx,%edx
	js 1f	/* negative -> in kernel */
	SWAPGS
	xorl %ebx,%ebx
1:	ret
	CFI_ENDPROC
END(save_paranoid)
	.popsection

/*
 * A newly forked process directly context switches into this address.
 *
 * rdi: prev task we switched from
 */
ENTRY(ret_from_fork)
	DEFAULT_FRAME

	LOCK ; btr $TIF_FORK,TI_flags(%r8)

	push kernel_eflags(%rip)
	CFI_ADJUST_CFA_OFFSET 8
	popf					# reset kernel eflags
	CFI_ADJUST_CFA_OFFSET -8

	call schedule_tail			# rdi: 'prev' task parameter

	GET_THREAD_INFO(%rcx)

	RESTORE_REST

	testl $3, CS-ARGOFFSET(%rsp)		# from kernel_thread?
	je   int_ret_from_sys_call

	testl $_TIF_IA32, TI_flags(%rcx)	# 32-bit compat task needs IRET
	jnz  int_ret_from_sys_call

	RESTORE_TOP_OF_STACK %rdi, -ARGOFFSET
	jmp ret_from_sys_call			# go to the SYSRET fastpath

	CFI_ENDPROC
END(ret_from_fork)

/*
 * System call entry. Upto 6 arguments in registers are supported.
 *
 * SYSCALL does not save anything on the stack and does not change the
 * stack pointer.
 */

/*
 * Register setup:
 * rax  system call number
 * rdi  arg0
 * rcx  return address for syscall/sysret, C arg3
 * rsi  arg1
 * rdx  arg2
 * r10  arg3 	(--> moved to rcx for C)
 * r8   arg4
 * r9   arg5
 * r11  eflags for syscall/sysret, temporary for C
 * r12-r15,rbp,rbx saved by C code, not touched.
 *
 * Interrupts are off on entry.
 * Only called from user space.
 *
 * XXX	if we had a free scratch register we could save the RSP into the stack frame
 *      and report it properly in ps. Unfortunately we haven't.
 *
 * When user can change the frames always force IRET. That is because
 * it deals with uncanonical addresses better. SYSRET has trouble
 * with them due to bugs in both AMD and Intel CPUs.
 */

/*
https://sourceware.org/binutils/docs/as/CFI-directives.html
Call Frame infromation

#define CFI_STARTPROC	.cfi_startproc
#define CFI_ENDPROC	    .cfi_endproc
#define CFI_DEF_CFA	    .cfi_def_cfa
#define CFI_REGISTER	.cfi_register  register1, register2   将register1中的值保存到register2中
#define CFI_REL_OFFSET	.cfi_rel_offset
#define CFI_UNDEFINED	.cfi_undefined
*/
###########################################################################################system_call() 系统调用开始######################################################
ENTRY(system_call)
	CFI_STARTPROC	simple    //    #define CFI_STARTPROC .cfi_startproc  用在每个函数的开始，用于初始化一些内部数据结构
	CFI_SIGNAL_FRAME
	CFI_DEF_CFA	rsp,KERNEL_STACK_OFFSET  ////定义计算CFA的规则
	CFI_REGISTER	rip,rcx   //#define CFI_REGISTER .cfi_register
	/*CFI_REGISTER	rflags,r11*/
	SWAPGS_UNSAFE_STACK  #swapgs 指令swapgs用于64位模式下的快速系统调用，允许在转换到内核模式时立即访问内核结构
	/*
	 * A hypervisor implementation might want to use a label
	 * after the swapgs, so that it can do the swapgs
	 * for the guest and jump here on syscall.
	 */
	 //上面的几行代码执行了swapgs指令。修改gs寄存器从用户态切换到内核态，其实就是修改运行级别
ENTRY(system_call_after_swapgs)

	movq	%rsp,PER_CPU_VAR(old_rsp)//保存原来的rsp
	movq	PER_CPU_VAR(kernel_stack),%rsp //切换到内核堆栈
	/*
	 * No need to follow this irqs off/on section - it's straight
	 * and short:
	 */
	ENABLE_INTERRUPTS(CLBR_NONE)  //sti  开中断
	
	SAVE_ARGS 8,1  #看calling.h中此宏的完全解析 主要为保存寄存器到堆栈中

	#将rax 和rcx保存在堆栈的某个位置  ORIG_RAX为120, ARGOFFSET为48
	#RIP为128      
	movq  %rax,ORIG_RAX - ARGOFFSET(%rsp) 
	movq  %rcx,RIP-ARGOFFSET(%rsp)
	
	CFI_REL_OFFSET rip,RIP-ARGOFFSET

	//此处获得thread_info地址
	GET_THREAD_INFO(%rcx)  


    #rcx现在保存thread_info的地址, TI_flags通过rcx获得thread_info中flags的偏移
	#_TIF_WORK_SYSCALL_ENTRY flags中设置的一些标志位,具体哪写标志看asm/thread_info.h
	#下面的意思是看thread_info{}->flags中是否设置了_TIF_WORK_SYSCALL_ENTRY标识的位
    #就是做一些执行系统调用之前的事前 比如审计
	testl $_TIF_WORK_SYSCALL_ENTRY, TI_flags(%rcx)

    //做一些系统调用追踪的事情 
	jnz tracesys     

	//进行快速系统调用
system_call_fastpath:
	cmpq $__NR_syscall_max,%rax   #比较系统调用号
	ja badsys                     #大于最大系统调用号 则跳到badsys

    #起初r10中保存arg3参数,现在将参数放到rcx中
	movq %r10,%rcx                 
	call *sys_call_table(,%rax,8)  # XXX:	 rip relative  //此处调用系统调用的处理例程 系统调用表8字节对齐

    #RAX=80  ARGOFFSET=48
	movq %rax,RAX-ARGOFFSET(%rsp)  #存储返回的值

/*
 * Syscall return path ending with SYSRET (fast path)
 * Has incomplete stack frame and undefined top of stack.
 */
#从系统调用返回
ret_from_sys_call:

    #返回用户空间前看是否还有工作要做  看asm/thread_info.h
    #_TIF_ALLWORK_MASK放着要做的工作的掩码
	movl $_TIF_ALLWORK_MASK,%edi
	/* edi:	flagmask */

sysret_check:

    #用于死锁检测 打印调试信息
	LOCKDEP_SYS_EXIT

	#获得thread_info结构
	GET_THREAD_INFO(%rcx)

	#关闭中断
	DISABLE_INTERRUPTS(CLBR_NONE)  

    #trace_hardirqs_off_caller 用于中断的状态跟踪 用于调试
	TRACE_IRQS_OFF

    #将thread_info{}->flags 放到edx中
    #rcx放着thread_info{}的地址
	movl TI_flags(%rcx),%edx

    /*检测是否还有其它工作需要完成*/ 
	andl %edi,%edx          

	/*在系统调用返回前还有待完成的工作 则跳转到sysret_careful */
	jnz  sysret_careful      

	CFI_REMEMBER_STATE
	/*
	 * sysretq will re-enable interrupts:
	 */
    #跟踪打开中断状态 下面将执行sysretq这个命令会重现打开中断
	TRACE_IRQS_ON

    #在上面RIP-ARGOFFSET(%rsp)这个位置保存了rcx的值，现在恢复rcx  
	movq RIP-ARGOFFSET(%rsp),%rcx
	
	CFI_REGISTER	rip,rcx

	#恢复寄存器的值  看calling.h
	RESTORE_ARGS 0,-ARG_SKIP,1

	/*CFI_REGISTER	rflags,r11*/
	
	#恢复堆栈到用户空间堆栈
	movq	PER_CPU_VAR(old_rsp), %rsp

    # 调用swapgs命令，切换gs寄存器的内核和用户态值，最后调用sysretq指令退出系统调用 sysretq会重新允许中断
    #到这里系统调用结束
	USERGS_SYSRET64                 

	CFI_RESTORE_STATE
	/* Handle reschedules */
	/* edx:	work, edi: workmask */

#前往用户空间前还有工作要做	
sysret_careful:

    #工作1 看是否要进行重新调度(如时间片用完被设置了重新调度标志,比如更高优先级的进程要运行),
    #edx中存放着要做的工作掩码
	bt $TIF_NEED_RESCHED,%edx  /*检测是否有设置TIF_NEED_RESCHED 标志*/

	jnc sysret_signal          #/*没有设置TIF_NEED_RESCHED标志，跳转到sysret_signal*/

    #走到此处 说面要进行重新调度
    #下面将开启中断 对打开中断状态进行跟踪，用于调试
	TRACE_IRQS_ON
	
	ENABLE_INTERRUPTS(CLBR_NONE)  //开启中断

    #rdi保存着工作的所有掩码 先保存
	pushq %rdi
	
	CFI_ADJUST_CFA_OFFSET 8

    #进行系统调度
	call schedule                

    #恢复rdi的值,
	popq  %rdi

	CFI_ADJUST_CFA_OFFSET -8

    //再次检测是否有工作
	jmp sysret_check              

	/* Handle a signal */

#跳转到此处 说明系统调度的工作已经没有了 要进行下一项工作检查
#信号的处理
sysret_signal:

    #下面将开启中断 对打开中断状态进行跟踪，用于调试
	TRACE_IRQS_ON
	#开启中断
	ENABLE_INTERRUPTS(CLBR_NONE) 

#配置了系统审计	
#ifdef CONFIG_AUDITSYSCALL
    #工作2 是否有审计的工作要做
	bt $TIF_SYSCALL_AUDIT,%edx

	#有审计工作要做  跳转到审计代码 进行审计的工作流程
	jc sysret_audit
#endif

	/*
	 * We have a signal, or exit tracing or single-step.
	 * These all wind up with the iret return path anyway,
	 * so just join that path right now.
	 */

	#下面的工作我们将会用iret中断返回 在这里先把堆栈调整好 
	/*iret将做下面的工作
    恢复IP
    恢复CS
    恢复中断前的PSW(program status word),即恢复中断前的标志寄存器的状态
    恢复ESP（返回权限发生变化）
    恢复SS（返回权限发生变化）
	*/
	FIXUP_TOP_OF_STACK %r11, -ARGOFFSET

	#继续执行系统调用返回前的工作,将走到中断处理部分
	#不管信号 还是 exit tracing 或者单步调试 都将从iret返回
	jmp int_check_syscall_exit_work

#系统调用号不对
badsys:
	movq $-ENOSYS,RAX-ARGOFFSET(%rsp)
	jmp ret_from_sys_call


#开启了审计特性
#ifdef CONFIG_AUDITSYSCALL
	/*
	 * Fast path for syscall audit without full syscall trace.
	 * We just call audit_syscall_entry() directly, and then
	 * jump back to the normal fast path.
	 */
	 
#对系统调用进行审计	 
auditsys:

   /*
   看下面的几条mov语句前先看下调用函数
   void audit_syscall_entry(int arch, int major, unsigned long a1,  unsigned long a2,unsigned long a3, unsigned long a4)
   一共有6个参数,很显然下面的6个mov执行是在构造参数呢  
   */

    #第六个参数,,系统调用的第四个参数
	movq %r10,%r9			/* 6th arg: 4th syscall arg */

    #第五数,,系统调用的第三个参数
	movq %rdx,%r8			/* 5th arg: 3rd syscall arg */

    #第四参数,,系统调用的第二个参数
	movq %rsi,%rcx			/* 4th arg: 2nd syscall arg */

    #第三个参数  ,   系统调用的第一个参数
	movq %rdi,%rdx			/* 3rd arg: 1st syscall arg */

    #第二个参数系统调用号
	movq %rax,%rsi			/* 2nd arg: syscall number */	

    #第一个参数 架构类型
	movl $AUDIT_ARCH_X86_64,%edi	/* 1st arg: audit arch */

    #调用audit_syscall_entry进行收集系统调用执行前的信息
	call audit_syscall_entry   

    #由于系统调用的寄存器数据遭到破坏, 下面的宏用于从栈中恢复寄存器的值, 寄存器的值在之前已经都保存到了栈中
    #这里就是从栈中在读出数据到寄存器中
	LOAD_ARGS 0		/* reload call-clobbered registers */

    #系统调用之前的审计信息已经获取完毕  跳到system_call_fastpath去执行系统调用
	jmp system_call_fastpath 
	/*
	 * Return fast path for syscall audit.  Call audit_syscall_exit()
	 * directly and then jump back to the fast path with TIF_SYSCALL_AUDIT
	 * masked off.
	 */

#此处是系统调用执行完后 要做的审计工作	 
sysret_audit:

    /*
    函数原型
    void audit_syscall_exit(int valid, long return_code)
    */

    #第二个参数, 系统调用返回值
	movq %rax,%rsi		/* second arg, syscall return value */

    #下面四个语句是设置第一个参数   成功设置1 失败返回2
    #比较返回值与0的大小
	cmpq $0,%rax		/* is it < 0? */

    #小于0(即系统调用失败了)的话 设置al为1，否则设置al为0 
	setl %al		/* 1 if so, 0 if not */

    #将第一个参数放到 edi中
	movzbl %al,%edi		/* zero-extend that into %edi */

    #edi中是0 或者1   我们的要的是  1成功 2是失败，所以先自加
	inc %edi /* first arg, 0->1(AUDITSC_SUCCESS), 1->2(AUDITSC_FAILURE) */

    #调用审计函数
	call audit_syscall_exit   

    #将审计的工作 从要待完成的工作中删除
	movl $(_TIF_ALLWORK_MASK & ~_TIF_SYSCALL_AUDIT),%edi

    #再次返回到sysret_check中，检查在返回用户前是否还有待完成的工作
	jmp sysret_check

#endif	/* CONFIG_AUDITSYSCALL */


/*做一些系统调用跟踪的事情 */
tracesys:  
#设置了审计配置
#ifdef CONFIG_AUDITSYSCALL

    #在进入系统调用前是否要进行审计
	testl $(_TIF_WORK_SYSCALL_ENTRY & ~_TIF_SYSCALL_AUDIT),TI_flags(%rcx)

    #如果了除了_TIF_SYSCALL_AUDIT以外其他工作都没有 则跳转到auditsys
    #因为要是设置了其他工作, 在其他工作流程中会进行审计 不需要再次调用审计了
	jz auditsys 
#endif

    #走到此处 说明设置了其他的工作
	SAVE_REST
	
	movq $-ENOSYS,RAX(%rsp) /* ptrace can change this for a bad syscall */
	FIXUP_TOP_OF_STACK %rdi

    #设置参数
	movq %rsp,%rdi
	
    #系统调用的跟踪  trace point技术
	call syscall_trace_enter

	/*
	 * Reload arg registers from stack in case ptrace changed them.
	 * We don't reload %rax because syscall_trace_enter() returned
	 * the value it wants us to use in the table lookup.
	 */
	LOAD_ARGS ARGOFFSET, 1
    #恢复寄存器值
	RESTORE_REST

    #比较系统调用的值是否合法
	cmpq $__NR_syscall_max,%rax
    #系统调用值大于 最大系统值 中断返回
	ja   int_ret_from_sys_call	/* RAX(%rsp) set to -ENOSYS above */

    #进行系统调用
	movq %r10,%rcx	/* fixup for C */
	call *sys_call_table(,%rax,8)
	movq %rax,RAX-ARGOFFSET(%rsp)
	/* Use IRET because user could have changed frame */

/*
 * Syscall return path ending with IRET.
 * Has correct top of stack, but partial stack frame.
 */
#从系统调用返回
GLOBAL(int_ret_from_sys_call)
	DISABLE_INTERRUPTS(CLBR_NONE)
	TRACE_IRQS_OFF

    #比较是否处于内核路径
	testl $3,CS-ARGOFFSET(%rsp)

	#如果是内核中断路径 则返回到内核
	je retint_restore_args

    #在次将工作掩码放到edi中
	movl $_TIF_ALLWORK_MASK,%edi
	/* edi:	mask to check */

#检测待完成的工作	
GLOBAL(int_with_check)

    #死锁检测
	LOCKDEP_SYS_EXIT_IRQ

    #获得thread_info结构地址 保存到ecx中
	GET_THREAD_INFO(%rcx)

	#获得thread_inf{}->flags
	movl TI_flags(%rcx),%edx

	#检测待完成的工作
	andl %edi,%edx

    #系统调用返回前 还有工作待完成
	jnz   int_careful

    #走到此处说明没有什么要待完成的工作了
	#!!!!TS_COMPAT看着像是在64位上使用了32位, 此处为什么去掉这个位  还不太清楚?
	andl    $~TS_COMPAT,TI_status(%rcx)

    #现在跳转到retint_swapgs准备返回
	jmp   retint_swapgs

	/* Either reschedule or signal or syscall exit tracking needed. */
	/* First do a reschedule test. */
	/* edx:	work, edi: workmask */

#进行待完成的工作	
int_careful:

    #检测是否要重新调度
	bt $TIF_NEED_RESCHED,%edx

	#不需要调度 则跳转到int_very_careful 继续检测
	jnc  int_very_careful

    #到此处说明要进行重新调度
    #跟踪开启中断状态,打开中断
	TRACE_IRQS_ON
	ENABLE_INTERRUPTS(CLBR_NONE)

    #保存待工作的掩码edi，并进行调度, 调度完后 恢复待完成的工作
	pushq %rdi
	CFI_ADJUST_CFA_OFFSET 8
	call schedule
	popq %rdi

	#禁止中断, 跟踪禁止中断的状态
	CFI_ADJUST_CFA_OFFSET -8
	DISABLE_INTERRUPTS(CLBR_NONE)
	TRACE_IRQS_OFF

	#再次去检测是否有待完成的工作
	jmp int_with_check

	/* handle signals and tracing -- both require a full stack frame */


int_very_careful:
    #跟踪开启中断状态,开启中断
	TRACE_IRQS_ON
	ENABLE_INTERRUPTS(CLBR_NONE)

#检测系统调用结束要返回用户前的工作	
int_check_syscall_exit_work:

    #宏在calling.h中 保存rbx,  rbp,r12,r13,r14,r15寄存器的值到堆栈中 
	SAVE_REST

	/* Check for syscall exit trace */
    #工作3   检测是否要进行系统调用结束跟踪工作
	testl $_TIF_WORK_SYSCALL_EXIT,%edx

    #没有此工作  则跳转到int_signal
	jz int_signal

    #走到此处 说明要进行系统调用跟踪工作 
    #先将要工作的位码保存
	pushq %rdi

	CFI_ADJUST_CFA_OFFSET 8

    #将ptregs的地址保存到rdi中    
	leaq 8(%rsp),%rdi	# &ptregs -> arg1

    #调用内核函数syscall_trace_leave进行跟踪信息统计
	call syscall_trace_leave

    #恢复工作位码
	popq %rdi
	
	CFI_ADJUST_CFA_OFFSET -8
	#将系统调用结束跟踪的工作从位码中去除
	andl $~(_TIF_WORK_SYSCALL_EXIT|_TIF_SYSCALL_EMU),%edi

    #跳转到int_restore_rest 
	jmp int_restore_rest

#进行信号的处理
int_signal:

    #是否要进行信号的处理
	testl $_TIF_DO_NOTIFY_MASK,%edx

    #如果不需要信号处理,则跳转到 1
	jz 1f

	#走到此处说明要调用do_notify_resume进行信号的处理
	movq %rsp,%rdi		# &ptregs -> arg1
	xorl %esi,%esi		# oldset -> arg2
	call do_notify_resume

	#将工作掩码保存到edi中
1:	movl $_TIF_WORK_MASK,%edi

   
int_restore_rest:

    #恢复寄存器的值 rbx,  rbp,r12,r13,r14,r15  ,宏在calling.h中  
	RESTORE_REST

    #禁止中断
	DISABLE_INTERRUPTS(CLBR_NONE)
    #对中断禁止状态跟踪 用于调试
	TRACE_IRQS_OFF

    #跳转到int_with_check 检测是否还有工作要处理
	jmp int_with_check

	CFI_ENDPROC   //#define CFI_ENDPROC .cfi_endproc   //在函数结束的时候使用与.cfi_startproc相配套使用


END(system_call)  
##########################################################################系统调用结束******#######################################################################

/*
 * Certain special system calls that need to save a complete full stack frame.
 */
	.macro PTREGSCALL label,func,arg
ENTRY(\label)
	PARTIAL_FRAME 1 8		/* offset 8: return address */
	subq $REST_SKIP, %rsp
	CFI_ADJUST_CFA_OFFSET REST_SKIP
	call save_rest
	DEFAULT_FRAME 0 8		/* offset 8: return address */
	leaq 8(%rsp), \arg	/* pt_regs pointer */
	call \func
	jmp ptregscall_common
	CFI_ENDPROC
END(\label)
	.endm

	PTREGSCALL stub_clone, sys_clone, %r8
	PTREGSCALL stub_fork, sys_fork, %rdi
	PTREGSCALL stub_vfork, sys_vfork, %rdi
	PTREGSCALL stub_sigaltstack, sys_sigaltstack, %rdx
	PTREGSCALL stub_iopl, sys_iopl, %rsi

ENTRY(ptregscall_common)
	DEFAULT_FRAME 1 8	/* offset 8: return address */
	RESTORE_TOP_OF_STACK %r11, 8
	movq_cfi_restore R15+8, r15
	movq_cfi_restore R14+8, r14
	movq_cfi_restore R13+8, r13
	movq_cfi_restore R12+8, r12
	movq_cfi_restore RBP+8, rbp
	movq_cfi_restore RBX+8, rbx
	ret $REST_SKIP		/* pop extended registers */
	CFI_ENDPROC
END(ptregscall_common)

ENTRY(stub_execve)
	CFI_STARTPROC
	popq %r11
	CFI_ADJUST_CFA_OFFSET -8
	CFI_REGISTER rip, r11
	SAVE_REST
	FIXUP_TOP_OF_STACK %r11
	movq %rsp, %rcx
	call sys_execve
	RESTORE_TOP_OF_STACK %r11
	movq %rax,RAX(%rsp)
	RESTORE_REST
	jmp int_ret_from_sys_call
	CFI_ENDPROC
END(stub_execve)

/*
 * sigreturn is special because it needs to restore all registers on return.
 * This cannot be done with SYSRET, so use the IRET return path instead.
 */
ENTRY(stub_rt_sigreturn)
	CFI_STARTPROC
	addq $8, %rsp
	CFI_ADJUST_CFA_OFFSET	-8
	SAVE_REST
	movq %rsp,%rdi
	FIXUP_TOP_OF_STACK %r11
	call sys_rt_sigreturn
	movq %rax,RAX(%rsp) # fixme, this could be done at the higher layer
	RESTORE_REST
	jmp int_ret_from_sys_call
	CFI_ENDPROC
END(stub_rt_sigreturn)

/*
 * Build the entry stubs and pointer table with some assembler magic.
 * We pack 7 stubs into a single 32-byte chunk, which will fit in a
 * single cache line on all modern x86 implementations.
 */
	.section .init.rodata,"a"
ENTRY(interrupt)
	.text
	.p2align 5
	.p2align CONFIG_X86_L1_CACHE_SHIFT
ENTRY(irq_entries_start)
	INTR_FRAME
vector=FIRST_EXTERNAL_VECTOR
.rept (NR_VECTORS-FIRST_EXTERNAL_VECTOR+6)/7
	.balign 32
  .rept	7
    .if vector < NR_VECTORS
      .if vector <> FIRST_EXTERNAL_VECTOR
	CFI_ADJUST_CFA_OFFSET -8
      .endif
1:	pushq $(~vector+0x80)	/* Note: always in signed byte range */
	CFI_ADJUST_CFA_OFFSET 8
      .if ((vector-FIRST_EXTERNAL_VECTOR)%7) <> 6
	jmp 2f
      .endif
      .previous
	.quad 1b
      .text
vector=vector+1
    .endif
  .endr
2:	jmp common_interrupt
.endr
	CFI_ENDPROC
END(irq_entries_start)

.previous
END(interrupt)
.previous

/*
 * Interrupt entry/exit.
 *
 * Interrupt entry points save only callee clobbered registers in fast path.
 *
 * Entry runs with interrupts off.
 */

/* 0(%rsp): ~(interrupt number) */
	.macro interrupt func
	subq $10*8, %rsp
	CFI_ADJUST_CFA_OFFSET 10*8
	call save_args
	PARTIAL_FRAME 0
	call \func
	.endm

	/*
	 * The interrupt stubs push (~vector+0x80) onto the stack and
	 * then jump to common_interrupt.
	 */
	.p2align CONFIG_X86_L1_CACHE_SHIFT

###########################################################################################common_interrupt  start 中断入口###########################################################
#中断发生时候的公共函数
common_interrupt:
	XCPT_FRAME
	addq $-0x80,(%rsp)		/* Adjust vector to [-256,-1] range */
	interrupt do_IRQ        #调用do_IRQ()c语言 来处理中断
	/* 0(%rsp): old_rsp-ARGOFFSET */

/*******************************
#中断处理完后,从中断返回
#define CLBR_NONE 0
***************/
ret_from_intr:
	DISABLE_INTERRUPTS(CLBR_NONE)
	TRACE_IRQS_OFF
	decl PER_CPU_VAR(irq_count)
	leaveq
	CFI_DEF_CFA_REGISTER	rsp
	CFI_ADJUST_CFA_OFFSET	-8

exit_intr:
	GET_THREAD_INFO(%rcx)         #获得thread_info
	testl $3,CS-ARGOFFSET(%rsp)   #返回到内核态 还是用户态 
	je retint_kernel              #返回到内核态   如果没有设置CONFIG_PREEMPT 则此处跳转到retint_restore_args 恢复寄存器后 ireq结束

	/* Interrupt came from user space */
	/*
	 * Has a correct top of stack, but a partial stack frame
	 * %rcx: thread info. Interrupts off.
	 */
retint_with_reschedule:
	movl $_TIF_WORK_MASK,%edi

#中断返回前检测	
retint_check:
    #用于死锁的调试信息
	LOCKDEP_SYS_EXIT_IRQ

	#检测是否还有待完成的工作
	movl TI_flags(%rcx),%edx
	andl %edi,%edx

	
	CFI_REMEMBER_STATE

	#如果还有工作 则跳转到retint_careful
	jnz  retint_careful
    #执行到这 说明没有了工作 要中断返回了
	

#返回到用户空间
retint_swapgs:		/* return to user-space */
	/*
	 * The iretq could re-enable interrupts:
	 */
	#禁止中断 
	DISABLE_INTERRUPTS(CLBR_ANY)
    #对中断进行状态跟踪
	TRACE_IRQS_IRETQ

    #切换gs到用户空间
	SWAPGS

	#去恢复用户进到内核时候的寄存器内容
	jmp restore_args


#中断恢复寄存器内容 返回到内核空间
retint_restore_args:	/* return to kernel space */
	DISABLE_INTERRUPTS(CLBR_ANY) #禁止中断
	/*
	 * The iretq could re-enable interrupts:
	 */
	
	TRACE_IRQS_IRETQ
	
restore_args:
    #恢复寄存器 在calling.h中
	RESTORE_ARGS 0,8,0

#进行中断返回
irq_return:
    #执行了这 那么中断就返回(根据中断路径 此处可能会返回到内核空间 或者返回到用户空间)
	INTERRUPT_RETURN   #iretq  iret会恢复中断前的cs和ip和状态子psw 中断前的状态即开启中断

	.section __ex_table, "a"
	.quad irq_return, bad_iret
	.previous

#ifdef CONFIG_PARAVIRT
ENTRY(native_iret)
	iretq

	.section __ex_table,"a"
	.quad native_iret, bad_iret
	.previous
#endif

	.section .fixup,"ax"
bad_iret:
	/*
	 * The iret traps when the %cs or %ss being restored is bogus.
	 * We've lost the original trap vector and error code.
	 * #GPF is the most likely one to get for an invalid selector.
	 * So pretend we completed the iret and took the #GPF in user mode.
	 *
	 * We are now running with the kernel GS after exception recovery.
	 * But error_entry expects us to have user GS to match the user %cs,
	 * so swap back.
	 */
	pushq $0

	SWAPGS
	jmp general_protection

	.previous

	/* edi: workmask, edx: work */
retint_careful:
	CFI_RESTORE_STATE

    #检测是否要进行调度
	bt    $TIF_NEED_RESCHED,%edx

	#如果不需要调度 则进入retint_signal 检查是否有别的工作
	jnc   retint_signal

    #走到此处说明要进行重新调度
    #允许中断  ,并进行中断状态跟踪
	TRACE_IRQS_ON
	ENABLE_INTERRUPTS(CLBR_NONE)

    #将工作掩码保存
	pushq %rdi
	CFI_ADJUST_CFA_OFFSET	8

    #进行系统调度
	call  schedule

	#恢复工作掩码
	popq %rdi
	CFI_ADJUST_CFA_OFFSET	-8

    #获得thread_info{}->flags
	GET_THREAD_INFO(%rcx)

	#禁止中断并将 中断状态进行跟踪
	DISABLE_INTERRUPTS(CLBR_NONE)
	TRACE_IRQS_OFF

	#检测是否还有工作要做
	jmp retint_check

retint_signal:

    #检测是否要进行信号 工作
	testl $_TIF_DO_NOTIFY_MASK,%edx

    #不需要的话 就跳转到retint_swapgs
	jz    retint_swapgs

    #走到此处说明要进行一些信号处理工作
    #开启中断 并且进行跟踪
	TRACE_IRQS_ON
	ENABLE_INTERRUPTS(CLBR_NONE)

    #保存一些寄存器的值
	SAVE_REST

	#调用 do_notify_resume(struct pt_regs *regs, void *unused, __u32 thread_info_flags)
	#并进行参数的设置
	movq $-1,ORIG_RAX(%rsp)
	xorl %esi,%esi		# oldset
	movq %rsp,%rdi		# &pt_regs
	call do_notify_resume


    #恢复寄存器的值
	RESTORE_REST
	
	#禁止中断并跟踪中断状态
	DISABLE_INTERRUPTS(CLBR_NONE)
	TRACE_IRQS_OFF

	#获得thread_info{}->flags
	GET_THREAD_INFO(%rcx)

	#进行重新系统调用
	jmp retint_with_reschedule


#/*返回内核空间,先检查preempt_count,再检查need_resched*/
#ifdef CONFIG_PREEMPT
	/* Returning to kernel space. Check if we need preemption */
	/* rcx:	 threadinfo. interrupts off. */
ENTRY(retint_kernel)
	cmpl $0,TI_preempt_count(%rcx) # /*是否可以抢占,即preempt_count是否为0*/
	jnz  retint_restore_args         #不能抢占,则恢复被中断时处理器状态
	bt  $TIF_NEED_RESCHED,TI_flags(%rcx) #是否需要重新调度
	jnc  retint_restore_args             #不需要调度 恢复寄存器

	bt   $9,EFLAGS-ARGOFFSET(%rsp)	/* interrupts off? */
	jnc  retint_restore_args
	call preempt_schedule_irq   #进行调度
	jmp exit_intr
#endif

	CFI_ENDPROC
END(common_interrupt)

##############################################################################################common_interrupt中断处理结束

/*
 * APIC interrupts.
 */
.macro apicinterrupt num sym do_sym
ENTRY(\sym)
	INTR_FRAME
	pushq $~(\num)
	CFI_ADJUST_CFA_OFFSET 8
	interrupt \do_sym
	jmp ret_from_intr
	CFI_ENDPROC
END(\sym)
.endm

#ifdef CONFIG_SMP
apicinterrupt IRQ_MOVE_CLEANUP_VECTOR \
	irq_move_cleanup_interrupt smp_irq_move_cleanup_interrupt
apicinterrupt REBOOT_VECTOR \
	reboot_interrupt smp_reboot_interrupt
#endif

#ifdef CONFIG_X86_UV
apicinterrupt UV_BAU_MESSAGE \
	uv_bau_message_intr1 uv_bau_message_interrupt
#endif
apicinterrupt LOCAL_TIMER_VECTOR \
	apic_timer_interrupt smp_apic_timer_interrupt
apicinterrupt GENERIC_INTERRUPT_VECTOR \
	generic_interrupt smp_generic_interrupt

#ifdef CONFIG_SMP
apicinterrupt INVALIDATE_TLB_VECTOR_START+0 \
	invalidate_interrupt0 smp_invalidate_interrupt
apicinterrupt INVALIDATE_TLB_VECTOR_START+1 \
	invalidate_interrupt1 smp_invalidate_interrupt
apicinterrupt INVALIDATE_TLB_VECTOR_START+2 \
	invalidate_interrupt2 smp_invalidate_interrupt
apicinterrupt INVALIDATE_TLB_VECTOR_START+3 \
	invalidate_interrupt3 smp_invalidate_interrupt
apicinterrupt INVALIDATE_TLB_VECTOR_START+4 \
	invalidate_interrupt4 smp_invalidate_interrupt
apicinterrupt INVALIDATE_TLB_VECTOR_START+5 \
	invalidate_interrupt5 smp_invalidate_interrupt
apicinterrupt INVALIDATE_TLB_VECTOR_START+6 \
	invalidate_interrupt6 smp_invalidate_interrupt
apicinterrupt INVALIDATE_TLB_VECTOR_START+7 \
	invalidate_interrupt7 smp_invalidate_interrupt
#endif

apicinterrupt THRESHOLD_APIC_VECTOR \
	threshold_interrupt smp_threshold_interrupt
apicinterrupt THERMAL_APIC_VECTOR \
	thermal_interrupt smp_thermal_interrupt

#ifdef CONFIG_X86_MCE
apicinterrupt MCE_SELF_VECTOR \
	mce_self_interrupt smp_mce_self_interrupt
#endif

#ifdef CONFIG_SMP
apicinterrupt CALL_FUNCTION_SINGLE_VECTOR \
	call_function_single_interrupt smp_call_function_single_interrupt
apicinterrupt CALL_FUNCTION_VECTOR \
	call_function_interrupt smp_call_function_interrupt
apicinterrupt RESCHEDULE_VECTOR \
	reschedule_interrupt smp_reschedule_interrupt
#endif

apicinterrupt ERROR_APIC_VECTOR \
	error_interrupt smp_error_interrupt
apicinterrupt SPURIOUS_APIC_VECTOR \
	spurious_interrupt smp_spurious_interrupt

#ifdef CONFIG_PERF_EVENTS
apicinterrupt LOCAL_PENDING_VECTOR \
	perf_pending_interrupt smp_perf_pending_interrupt
#endif

/*
 * Exception entry points.
 */
.macro zeroentry sym do_sym
ENTRY(\sym)
	INTR_FRAME
	PARAVIRT_ADJUST_EXCEPTION_FRAME
	pushq_cfi $-1		/* ORIG_RAX: no syscall to restart */
	subq $15*8,%rsp
	CFI_ADJUST_CFA_OFFSET 15*8
	call error_entry
	DEFAULT_FRAME 0
	movq %rsp,%rdi		/* pt_regs pointer */
	xorl %esi,%esi		/* no error code */
	call \do_sym
	jmp error_exit		/* %ebx: no swapgs flag */
	CFI_ENDPROC
END(\sym)
.endm

.macro paranoidzeroentry sym do_sym
ENTRY(\sym)
	INTR_FRAME
	PARAVIRT_ADJUST_EXCEPTION_FRAME
	pushq $-1		/* ORIG_RAX: no syscall to restart */
	CFI_ADJUST_CFA_OFFSET 8
	subq $15*8, %rsp
	call save_paranoid
	TRACE_IRQS_OFF
	movq %rsp,%rdi		/* pt_regs pointer */
	xorl %esi,%esi		/* no error code */
	call \do_sym
	jmp paranoid_exit	/* %ebx: no swapgs flag */
	CFI_ENDPROC
END(\sym)
.endm

.macro paranoidzeroentry_ist sym do_sym ist
ENTRY(\sym)
	INTR_FRAME
	PARAVIRT_ADJUST_EXCEPTION_FRAME
	pushq $-1		/* ORIG_RAX: no syscall to restart */
	CFI_ADJUST_CFA_OFFSET 8
	subq $15*8, %rsp
	call save_paranoid
	TRACE_IRQS_OFF
	movq %rsp,%rdi		/* pt_regs pointer */
	xorl %esi,%esi		/* no error code */
	PER_CPU(init_tss, %rbp)
	subq $EXCEPTION_STKSZ, TSS_ist + (\ist - 1) * 8(%rbp)
	call \do_sym
	addq $EXCEPTION_STKSZ, TSS_ist + (\ist - 1) * 8(%rbp)
	jmp paranoid_exit	/* %ebx: no swapgs flag */
	CFI_ENDPROC
END(\sym)
.endm

.macro errorentry sym do_sym
ENTRY(\sym)
	XCPT_FRAME
	PARAVIRT_ADJUST_EXCEPTION_FRAME
	subq $15*8,%rsp
	CFI_ADJUST_CFA_OFFSET 15*8
	call error_entry
	DEFAULT_FRAME 0
	movq %rsp,%rdi			/* pt_regs pointer */
	movq ORIG_RAX(%rsp),%rsi	/* get error code */
	movq $-1,ORIG_RAX(%rsp)		/* no syscall to restart */
	call \do_sym
	jmp error_exit			/* %ebx: no swapgs flag */
	CFI_ENDPROC
END(\sym)
.endm

	/* error code is on the stack already */
.macro paranoiderrorentry sym do_sym
ENTRY(\sym)
	XCPT_FRAME
	PARAVIRT_ADJUST_EXCEPTION_FRAME
	subq $15*8,%rsp
	CFI_ADJUST_CFA_OFFSET 15*8
	call save_paranoid
	DEFAULT_FRAME 0
	TRACE_IRQS_OFF
	movq %rsp,%rdi			/* pt_regs pointer */
	movq ORIG_RAX(%rsp),%rsi	/* get error code */
	movq $-1,ORIG_RAX(%rsp)		/* no syscall to restart */
	call \do_sym
	jmp paranoid_exit		/* %ebx: no swapgs flag */
	CFI_ENDPROC
END(\sym)
.endm

zeroentry divide_error do_divide_error
zeroentry overflow do_overflow
zeroentry bounds do_bounds
zeroentry invalid_op do_invalid_op
zeroentry device_not_available do_device_not_available
paranoiderrorentry double_fault do_double_fault
zeroentry coprocessor_segment_overrun do_coprocessor_segment_overrun
errorentry invalid_TSS do_invalid_TSS
errorentry segment_not_present do_segment_not_present
zeroentry spurious_interrupt_bug do_spurious_interrupt_bug
zeroentry coprocessor_error do_coprocessor_error
errorentry alignment_check do_alignment_check
zeroentry simd_coprocessor_error do_simd_coprocessor_error

	/* Reload gs selector with exception handling */
	/* edi:  new selector */
ENTRY(native_load_gs_index)
	CFI_STARTPROC
	pushf
	CFI_ADJUST_CFA_OFFSET 8
	DISABLE_INTERRUPTS(CLBR_ANY & ~CLBR_RDI)
	SWAPGS
gs_change:
	movl %edi,%gs
2:	mfence		/* workaround */
	SWAPGS
	popf
	CFI_ADJUST_CFA_OFFSET -8
	ret
	CFI_ENDPROC
END(native_load_gs_index)

	.section __ex_table,"a"
	.align 8
	.quad gs_change,bad_gs
	.previous
	.section .fixup,"ax"
	/* running with kernelgs */
bad_gs:
	SWAPGS			/* switch back to user gs */
	xorl %eax,%eax
	movl %eax,%gs
	jmp  2b
	.previous

/*
 * Create a kernel thread.
 *
 * C extern interface:
 *	extern long kernel_thread(int (*fn)(void *), void * arg, unsigned long flags)
 *
 * asm input arguments:
 *	rdi: fn, rsi: arg, rdx: flags
 */
ENTRY(kernel_thread)
	CFI_STARTPROC
	FAKE_STACK_FRAME $child_rip
	SAVE_ALL

	# rdi: flags, rsi: usp, rdx: will be &pt_regs
	movq %rdx,%rdi
	orq  kernel_thread_flags(%rip),%rdi
	movq $-1, %rsi
	movq %rsp, %rdx

	xorl %r8d,%r8d
	xorl %r9d,%r9d

	# clone now
	call do_fork
	movq %rax,RAX(%rsp)
	xorl %edi,%edi

	/*
	 * It isn't worth to check for reschedule here,
	 * so internally to the x86_64 port you can rely on kernel_thread()
	 * not to reschedule the child before returning, this avoids the need
	 * of hacks for example to fork off the per-CPU idle tasks.
	 * [Hopefully no generic code relies on the reschedule -AK]
	 */
	RESTORE_ALL
	UNFAKE_STACK_FRAME
	ret
	CFI_ENDPROC
END(kernel_thread)

ENTRY(child_rip)
	pushq $0		# fake return address
	CFI_STARTPROC
	/*
	 * Here we are in the child and the registers are set as they were
	 * at kernel_thread() invocation in the parent.
	 */
	movq %rdi, %rax
	movq %rsi, %rdi
	call *%rax
	# exit
	mov %eax, %edi
	call do_exit
	ud2			# padding for call trace
	CFI_ENDPROC
END(child_rip)

/*
 * execve(). This function needs to use IRET, not SYSRET, to set up all state properly.
 *
 * C extern interface:
 *	 extern long execve(char *name, char **argv, char **envp)
 *
 * asm input arguments:
 *	rdi: name, rsi: argv, rdx: envp
 *
 * We want to fallback into:
 *	extern long sys_execve(char *name, char **argv,char **envp, struct pt_regs *regs)
 *
 * do_sys_execve asm fallback arguments:
 *	rdi: name, rsi: argv, rdx: envp, rcx: fake frame on the stack
 */
ENTRY(kernel_execve)
	CFI_STARTPROC
	FAKE_STACK_FRAME $0
	SAVE_ALL
	movq %rsp,%rcx
	call sys_execve
	movq %rax, RAX(%rsp)
	RESTORE_REST
	testq %rax,%rax
	je int_ret_from_sys_call
	RESTORE_ARGS
	UNFAKE_STACK_FRAME
	ret
	CFI_ENDPROC
END(kernel_execve)

/* Call softirq on interrupt stack. Interrupts are off. */
ENTRY(call_softirq)
	CFI_STARTPROC
	push %rbp
	CFI_ADJUST_CFA_OFFSET	8
	CFI_REL_OFFSET rbp,0
	mov  %rsp,%rbp
	CFI_DEF_CFA_REGISTER rbp
	incl PER_CPU_VAR(irq_count)
	cmove PER_CPU_VAR(irq_stack_ptr),%rsp
	push  %rbp			# backlink for old unwinder
	call __do_softirq
	leaveq
	CFI_DEF_CFA_REGISTER	rsp
	CFI_ADJUST_CFA_OFFSET   -8
	decl PER_CPU_VAR(irq_count)
	ret
	CFI_ENDPROC
END(call_softirq)

#ifdef CONFIG_XEN
zeroentry xen_hypervisor_callback xen_do_hypervisor_callback

/*
 * A note on the "critical region" in our callback handler.
 * We want to avoid stacking callback handlers due to events occurring
 * during handling of the last event. To do this, we keep events disabled
 * until we've done all processing. HOWEVER, we must enable events before
 * popping the stack frame (can't be done atomically) and so it would still
 * be possible to get enough handler activations to overflow the stack.
 * Although unlikely, bugs of that kind are hard to track down, so we'd
 * like to avoid the possibility.
 * So, on entry to the handler we detect whether we interrupted an
 * existing activation in its critical region -- if so, we pop the current
 * activation and restart the handler using the previous one.
 */
ENTRY(xen_do_hypervisor_callback)   # do_hypervisor_callback(struct *pt_regs)
	CFI_STARTPROC
/*
 * Since we don't modify %rdi, evtchn_do_upall(struct *pt_regs) will
 * see the correct pointer to the pt_regs
 */
	movq %rdi, %rsp            # we don't return, adjust the stack frame
	CFI_ENDPROC
	DEFAULT_FRAME
11:	incl PER_CPU_VAR(irq_count)
	movq %rsp,%rbp
	CFI_DEF_CFA_REGISTER rbp
	cmovzq PER_CPU_VAR(irq_stack_ptr),%rsp
	pushq %rbp			# backlink for old unwinder
	call xen_evtchn_do_upcall
	popq %rsp
	CFI_DEF_CFA_REGISTER rsp
	decl PER_CPU_VAR(irq_count)
	jmp  error_exit
	CFI_ENDPROC
END(do_hypervisor_callback)

/*
 * Hypervisor uses this for application faults while it executes.
 * We get here for two reasons:
 *  1. Fault while reloading DS, ES, FS or GS
 *  2. Fault while executing IRET
 * Category 1 we do not need to fix up as Xen has already reloaded all segment
 * registers that could be reloaded and zeroed the others.
 * Category 2 we fix up by killing the current process. We cannot use the
 * normal Linux return path in this case because if we use the IRET hypercall
 * to pop the stack frame we end up in an infinite loop of failsafe callbacks.
 * We distinguish between categories by comparing each saved segment register
 * with its current contents: any discrepancy means we in category 1.
 */
ENTRY(xen_failsafe_callback)
	INTR_FRAME 1 (6*8)
	/*CFI_REL_OFFSET gs,GS*/
	/*CFI_REL_OFFSET fs,FS*/
	/*CFI_REL_OFFSET es,ES*/
	/*CFI_REL_OFFSET ds,DS*/
	CFI_REL_OFFSET r11,8
	CFI_REL_OFFSET rcx,0
	movw %ds,%cx
	cmpw %cx,0x10(%rsp)
	CFI_REMEMBER_STATE
	jne 1f
	movw %es,%cx
	cmpw %cx,0x18(%rsp)
	jne 1f
	movw %fs,%cx
	cmpw %cx,0x20(%rsp)
	jne 1f
	movw %gs,%cx
	cmpw %cx,0x28(%rsp)
	jne 1f
	/* All segments match their saved values => Category 2 (Bad IRET). */
	movq (%rsp),%rcx
	CFI_RESTORE rcx
	movq 8(%rsp),%r11
	CFI_RESTORE r11
	addq $0x30,%rsp
	CFI_ADJUST_CFA_OFFSET -0x30
	pushq_cfi $0	/* RIP */
	pushq_cfi %r11
	pushq_cfi %rcx
	jmp general_protection
	CFI_RESTORE_STATE
1:	/* Segment mismatch => Category 1 (Bad segment). Retry the IRET. */
	movq (%rsp),%rcx
	CFI_RESTORE rcx
	movq 8(%rsp),%r11
	CFI_RESTORE r11
	addq $0x30,%rsp
	CFI_ADJUST_CFA_OFFSET -0x30
	pushq_cfi $0
	SAVE_ALL
	jmp error_exit
	CFI_ENDPROC
END(xen_failsafe_callback)

#endif /* CONFIG_XEN */

/*
 * Some functions should be protected against kprobes
 */
	.pushsection .kprobes.text, "ax"

paranoidzeroentry_ist debug do_debug DEBUG_STACK
paranoidzeroentry_ist int3 do_int3 DEBUG_STACK
paranoiderrorentry stack_segment do_stack_segment
#ifdef CONFIG_XEN
zeroentry xen_debug do_debug
zeroentry xen_int3 do_int3
errorentry xen_stack_segment do_stack_segment
#endif
errorentry general_protection do_general_protection
errorentry page_fault do_page_fault
#ifdef CONFIG_X86_MCE
paranoidzeroentry machine_check *machine_check_vector(%rip)
#endif

	/*
	 * "Paranoid" exit path from exception stack.
	 * Paranoid because this is used by NMIs and cannot take
	 * any kernel state for granted.
	 * We don't do kernel preemption checks here, because only
	 * NMI should be common and it does not enable IRQs and
	 * cannot get reschedule ticks.
	 *
	 * "trace" is 0 for the NMI handler only, because irq-tracing
	 * is fundamentally NMI-unsafe. (we cannot change the soft and
	 * hard flags at once, atomically)
	 */

	/* ebx:	no swapgs flag */
ENTRY(paranoid_exit)
	INTR_FRAME
	DISABLE_INTERRUPTS(CLBR_NONE)
	TRACE_IRQS_OFF
	testl %ebx,%ebx				/* swapgs needed? */
	jnz paranoid_restore
	testl $3,CS(%rsp)
	jnz   paranoid_userspace
paranoid_swapgs:
	TRACE_IRQS_IRETQ 0
	SWAPGS_UNSAFE_STACK
	RESTORE_ALL 8
	jmp irq_return
paranoid_restore:
	TRACE_IRQS_IRETQ 0
	RESTORE_ALL 8
	jmp irq_return
paranoid_userspace:
	GET_THREAD_INFO(%rcx)
	movl TI_flags(%rcx),%ebx
	andl $_TIF_WORK_MASK,%ebx
	jz paranoid_swapgs
	movq %rsp,%rdi			/* &pt_regs */
	call sync_regs
	movq %rax,%rsp			/* switch stack for scheduling */
	testl $_TIF_NEED_RESCHED,%ebx
	jnz paranoid_schedule
	movl %ebx,%edx			/* arg3: thread flags */
	TRACE_IRQS_ON
	ENABLE_INTERRUPTS(CLBR_NONE)
	xorl %esi,%esi 			/* arg2: oldset */
	movq %rsp,%rdi 			/* arg1: &pt_regs */
	call do_notify_resume
	DISABLE_INTERRUPTS(CLBR_NONE)
	TRACE_IRQS_OFF
	jmp paranoid_userspace
paranoid_schedule:
	TRACE_IRQS_ON
	ENABLE_INTERRUPTS(CLBR_ANY)
	call schedule
	DISABLE_INTERRUPTS(CLBR_ANY)
	TRACE_IRQS_OFF
	jmp paranoid_userspace
	CFI_ENDPROC
END(paranoid_exit)

/*
 * Exception entry point. This expects an error code/orig_rax on the stack.
 * returns in "no swapgs flag" in %ebx.
 */
ENTRY(error_entry)
	XCPT_FRAME
	CFI_ADJUST_CFA_OFFSET 15*8
	/* oldrax contains error code */
	cld
	movq_cfi rdi, RDI+8
	movq_cfi rsi, RSI+8
	movq_cfi rdx, RDX+8
	movq_cfi rcx, RCX+8
	movq_cfi rax, RAX+8
	movq_cfi  r8,  R8+8
	movq_cfi  r9,  R9+8
	movq_cfi r10, R10+8
	movq_cfi r11, R11+8
	movq_cfi rbx, RBX+8
	movq_cfi rbp, RBP+8
	movq_cfi r12, R12+8
	movq_cfi r13, R13+8
	movq_cfi r14, R14+8
	movq_cfi r15, R15+8
	xorl %ebx,%ebx
	testl $3,CS+8(%rsp)
	je error_kernelspace
error_swapgs:
	SWAPGS
error_sti:
	TRACE_IRQS_OFF
	ret
	CFI_ENDPROC

/*
 * There are two places in the kernel that can potentially fault with
 * usergs. Handle them here. The exception handlers after iret run with
 * kernel gs again, so don't set the user space flag. B stepping K8s
 * sometimes report an truncated RIP for IRET exceptions returning to
 * compat mode. Check for these here too.
 */
error_kernelspace:
	incl %ebx
	leaq irq_return(%rip),%rcx
	cmpq %rcx,RIP+8(%rsp)
	je error_swapgs
	movl %ecx,%ecx	/* zero extend */
	cmpq %rcx,RIP+8(%rsp)
	je error_swapgs
	cmpq $gs_change,RIP+8(%rsp)
	je error_swapgs
	jmp error_sti
END(error_entry)


/* ebx:	no swapgs flag (1: don't need swapgs, 0: need it) */
ENTRY(error_exit)
	DEFAULT_FRAME
	movl %ebx,%eax
	RESTORE_REST
	DISABLE_INTERRUPTS(CLBR_NONE)
	TRACE_IRQS_OFF
	GET_THREAD_INFO(%rcx)
	testl %eax,%eax
	jne retint_kernel
	LOCKDEP_SYS_EXIT_IRQ
	movl TI_flags(%rcx),%edx
	movl $_TIF_WORK_MASK,%edi
	andl %edi,%edx
	jnz retint_careful
	jmp retint_swapgs
	CFI_ENDPROC
END(error_exit)


	/* runs on exception stack */
ENTRY(nmi)
	INTR_FRAME
	PARAVIRT_ADJUST_EXCEPTION_FRAME
	pushq_cfi $-1
	subq $15*8, %rsp
	CFI_ADJUST_CFA_OFFSET 15*8
	call save_paranoid
	DEFAULT_FRAME 0
	/* paranoidentry do_nmi, 0; without TRACE_IRQS_OFF */
	movq %rsp,%rdi
	movq $-1,%rsi
	call do_nmi
#ifdef CONFIG_TRACE_IRQFLAGS
	/* paranoidexit; without TRACE_IRQS_OFF */
	/* ebx:	no swapgs flag */
	DISABLE_INTERRUPTS(CLBR_NONE)
	testl %ebx,%ebx				/* swapgs needed? */
	jnz nmi_restore
	testl $3,CS(%rsp)
	jnz nmi_userspace
nmi_swapgs:
	SWAPGS_UNSAFE_STACK
nmi_restore:
	RESTORE_ALL 8
	jmp irq_return
nmi_userspace:
	GET_THREAD_INFO(%rcx)
	movl TI_flags(%rcx),%ebx
	andl $_TIF_WORK_MASK,%ebx
	jz nmi_swapgs
	movq %rsp,%rdi			/* &pt_regs */
	call sync_regs
	movq %rax,%rsp			/* switch stack for scheduling */
	testl $_TIF_NEED_RESCHED,%ebx
	jnz nmi_schedule
	movl %ebx,%edx			/* arg3: thread flags */
	ENABLE_INTERRUPTS(CLBR_NONE)
	xorl %esi,%esi 			/* arg2: oldset */
	movq %rsp,%rdi 			/* arg1: &pt_regs */
	call do_notify_resume
	DISABLE_INTERRUPTS(CLBR_NONE)
	jmp nmi_userspace
nmi_schedule:
	ENABLE_INTERRUPTS(CLBR_ANY)
	call schedule
	DISABLE_INTERRUPTS(CLBR_ANY)
	jmp nmi_userspace
	CFI_ENDPROC
#else
	jmp paranoid_exit
	CFI_ENDPROC
#endif
END(nmi)

ENTRY(ignore_sysret)
	CFI_STARTPROC
	mov $-ENOSYS,%eax
	sysret
	CFI_ENDPROC
END(ignore_sysret)

/*
 * End of kprobes section
 */
	.popsection
