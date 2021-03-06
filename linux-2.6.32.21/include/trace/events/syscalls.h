#undef TRACE_SYSTEM
#define TRACE_SYSTEM syscalls

#if !defined(_TRACE_EVENTS_SYSCALLS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_EVENTS_SYSCALLS_H

#include <linux/tracepoint.h>

#include <asm/ptrace.h>
#include <asm/syscall.h>


#ifdef CONFIG_HAVE_SYSCALL_TRACEPOINTS

extern void syscall_regfunc(void);
extern void syscall_unregfunc(void);


/*
注意此处TRACE_EVENT_FN 会变化
在第一次包含此头文件的时候  TRACE_EVENT_FN的定义为DECLARE_TRACE
然后在最下面一条的#include <trace/define_trace.h>还会再次包含此头文件,由于定义了TRACE_HEADER_MULTI_READ所以能进入这里
第二次将TRACE_EVENT_FN的定义改为了DEFINE_TRACE_FN
*/

TRACE_EVENT_FN(sys_enter,
	TP_PROTO(struct pt_regs *regs, long id),
	TP_ARGS(regs, id),
	TP_STRUCT__entry(
		__field(	long,		id		)
		__array(	unsigned long,	args,	6	)
	),

	TP_fast_assign(
		__entry->id	= id;
		syscall_get_arguments(current, regs, 0, 6, __entry->args);
	),

	TP_printk("NR %ld (%lx, %lx, %lx, %lx, %lx, %lx)",
		  __entry->id,
		  __entry->args[0], __entry->args[1], __entry->args[2],
		  __entry->args[3], __entry->args[4], __entry->args[5]),

	syscall_regfunc, syscall_unregfunc
);

TRACE_EVENT_FN(sys_exit,

	TP_PROTO(struct pt_regs *regs, long ret),

	TP_ARGS(regs, ret),

	TP_STRUCT__entry(
		__field(	long,	id	)
		__field(	long,	ret	)
	),

	TP_fast_assign(
		__entry->id	= syscall_get_nr(current, regs);
		__entry->ret	= ret;
	),

	TP_printk("NR %ld = %ld",
		  __entry->id, __entry->ret),

	syscall_regfunc, syscall_unregfunc
);

#endif /* CONFIG_HAVE_SYSCALL_TRACEPOINTS */

#endif /* _TRACE_EVENTS_SYSCALLS_H */

/* This part must be outside protection */
#include <trace/define_trace.h>

