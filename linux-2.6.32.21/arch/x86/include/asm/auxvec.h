#ifndef _ASM_X86_AUXVEC_H
#define _ASM_X86_AUXVEC_H
/*
 * Architecture-neutral AT_ values in 0-17, leave some room
 * for more of them, start the x86-specific ones at 32.
 */
#ifdef __i386__
#define AT_SYSINFO		32
#endif
#define AT_SYSINFO_EHDR		33 //这个值指向包含有Virtual Dynamic Shared Object (VDSO)的页，这是内核创建的以提供某些系统调用的快速实现

#endif /* _ASM_X86_AUXVEC_H */
