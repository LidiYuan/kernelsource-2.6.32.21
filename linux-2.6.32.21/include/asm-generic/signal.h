#ifndef __ASM_GENERIC_SIGNAL_H
#define __ASM_GENERIC_SIGNAL_H

#include <linux/types.h>

#define _NSIG		64
#define _NSIG_BPW	__BITS_PER_LONG
#define _NSIG_WORDS	(_NSIG / _NSIG_BPW)

#define SIGHUP		 1 //�ҶϿ����ն˻����
#define SIGINT		 2//���Լ��̵��ж�
#define SIGQUIT		 3 //���Լ��̵��˳�
#define SIGILL		 4//�Ƿ�ָ��
#define SIGTRAP		 5//���ٶϵ�
#define SIGABRT		 6//�쳣����
#define SIGIOT		 6//�ȼ�SIGABRT
#define SIGBUS		 7//z���ߴ���
#define SIGFPE		 8//�����쳣
#define SIGKILL		 9//ǿ����ֹ����
#define SIGUSR1		10//���̿���ʹ��
#define SIGSEGV		11 //��������һ����Ч��ҳ
#define SIGUSR2		12//
#define SIGPIPE		13//���޶��ߵĹܵ�д
#define SIGALRM		14//��ʱ��ʱ��
#define SIGTERM		15//������ֹ
#define SIGSTKFLT	16//Э������ջ����
#define SIGCHLD		17 //��ĳһ���ӽ���ֹͣ����ֹ��ʱ�� ���͸�������
#define SIGCONT		18//���ֹͣ��ظ�ִ��
#define SIGSTOP		19//ֹͣ����ִ��
#define SIGTSTP		20//��tty����ֹͣ����
#define SIGTTIN		21//��̨������������
#define SIGTTOU		22//��̨�����������
#define SIGURG		23//�׽��ֽ���ָ��
#define SIGXCPU		24//����CPUʱ��
#define SIGXFSZ		25//�����ļ���С����
#define SIGVTALRM	26//���ⶨʱ��ʱ��
#define SIGPROF		27//�ſ�������ʱ��
#define SIGWINCH	28//���ڵ�����С
#define SIGIO		29//IO���ڿ��ܷ���
#define SIGPOLL		SIGIO
/*
#define SIGLOST		29
*/
#define SIGPWR		30//��Դ����ʧЧ
#define SIGSYS		31
#define	SIGUNUSED	31//û��ʹ��

/* These should not be considered constants from userland.  */
#define SIGRTMIN	32
#ifndef SIGRTMAX
#define SIGRTMAX	_NSIG
#endif

/*
 * SA_FLAGS values:
 *
 * SA_ONSTACK indicates that a registered stack_t will be used.
 * SA_RESTART flag to get restarting signals (which were the default long ago)
 * SA_NOCLDSTOP flag to turn off SIGCHLD when children stop.
 * SA_RESETHAND clears the handler when the signal is delivered.
 * SA_NOCLDWAIT flag on SIGCHLD to inhibit zombies.
 * SA_NODEFER prevents the current signal from being masked in the handler.
 *
 * SA_ONESHOT and SA_NOMASK are the historical Linux names for the Single
 * Unix names RESETHAND and NODEFER respectively.
 */
#define SA_NOCLDSTOP	0x00000001//����λ����ʱ�����ӽ���stopʱ������SIGCHLD�ź�
#define SA_NOCLDWAIT	0x00000002
#define SA_SIGINFO	0x00000004
#define SA_ONSTACK	0x08000000//����Ҫʹ���Ѿ�ע�����ջ��������ʹ�ý��������ջ
#define SA_RESTART	0x10000000//�������źű��жϺ�����
#define SA_NODEFER	0x40000000//һ������£� ���źŴ���������ʱ���ں˽������ø����źš�������������� SA_NODEFER��ǣ� ��ô�ڸ��źŴ���������ʱ���ں˽������������ź�
#define SA_RESETHAND	0x80000000//�������źŴ�����ʱ�����źŵĴ���������ΪȱʡֵSIG_DFL

#define SA_NOMASK	SA_NODEFER
#define SA_ONESHOT	SA_RESETHAND

/*
 * New architectures should not define the obsolete
 *	SA_RESTORER	0x04000000
 */

/*
 * sigaltstack controls
 */
#define SS_ONSTACK	1
#define SS_DISABLE	2

#define MINSIGSTKSZ	2048
#define SIGSTKSZ	8192

#ifndef __ASSEMBLY__
typedef struct {
	unsigned long sig[_NSIG_WORDS];
} sigset_t;

/* not actually used, but required for linux/syscalls.h */
typedef unsigned long old_sigset_t;

#include <asm-generic/signal-defs.h>

struct sigaction {
	__sighandler_t sa_handler;//sa_handlerָ��һ��������
	unsigned long sa_flags;//��¼�źŴ���ʱ��һЩ���� ��:SA_RESTART���������źű��жϺ�����
#ifdef SA_RESTORER
	__sigrestore_t sa_restorer;
#endif
	sigset_t sa_mask;//sa_mask�ʹ����ڴ���ǰ�ź�ʱ������ѡ���Ե�����һЩ�źš��൱�ڼ�ʱ��Ч��		/* mask last for extensibility */
};

struct k_sigaction {
	struct sigaction sa;
};

typedef struct sigaltstack {
	void __user *ss_sp;
	int ss_flags;
	size_t ss_size;
} stack_t;

#ifdef __KERNEL__

#include <asm/sigcontext.h>
#undef __HAVE_ARCH_SIG_BITOPS

#define ptrace_signal_deliver(regs, cookie) do { } while (0)

#endif /* __KERNEL__ */
#endif /* __ASSEMBLY__ */

#endif /* _ASM_GENERIC_SIGNAL_H */
