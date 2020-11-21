#ifndef _LINUX_AUXVEC_H
#define _LINUX_AUXVEC_H

#include <asm/auxvec.h>

/* Symbolic values for the entries in the auxiliary table
   put on the initial stack 
在 create_elf_tables() 对辅助向量进行填充
 */
#define AT_NULL   0	/* end of vector 辅助信息数组结束*/
#define AT_IGNORE 1	/* entry should be ignored */
#define AT_EXECFD 2	/* file descriptor of program 可执行文件的文件句柄*/
#define AT_PHDR   3	/* program headers for program  ELF program Header的地址*/
#define AT_PHENT  4	/* size of program header entry ELF program Header项的大小*/
#define AT_PHNUM  5	/* number of program headers  ELF program Header项的个数*/
#define AT_PAGESZ 6	/* system page size 页的尺寸大小*/
#define AT_BASE   7	/* base address of interpreter 解释器的装载地址 (动态连接器在连接阶段使用这个)*/
#define AT_FLAGS  8	/* flags */
#define AT_ENTRY  9	/* entry point of program 程序的入口点*/
#define AT_NOTELF 10	/* program is not ELF */
#define AT_UID    11	/* real uid 真正的UID*/
#define AT_EUID   12	/* effective uid 有效ID*/
#define AT_GID    13	/* real gid 真实的gid*/
#define AT_EGID   14	/* effective gid 有效的gid*/
#define AT_PLATFORM 15  /* string identifying CPU for optimizations 指向一个字符串 用于识别程序运行在哪个硬件平台*/
#define AT_HWCAP  16    /* arch dependent hints at CPU capabilities 这个值指向一个多字节位掩码，设置指示详细的处理器能力。这个信息可以被用来提供某些库函数优化的行为 */
#define AT_CLKTCK 17	/* frequency at which times() increments */
/* AT_* values 18 through 22 are reserved */
#define AT_SECURE 23   /* secure mode boolean */
#define AT_BASE_PLATFORM 24	/* string identifying real platform, may
				 * differ from AT_PLATFORM. */
#define AT_RANDOM 25	/* address of 16 random bytes */

#define AT_EXECFN  31	/* filename of program */

#ifdef __KERNEL__
#define AT_VECTOR_SIZE_BASE 19 /* NEW_AUX_ENT entries in auxiliary table */
  /* number of "#define AT_.*" above, minus {AT_NULL, AT_IGNORE, AT_NOTELF} */
#endif

#endif /* _LINUX_AUXVEC_H */
