#ifndef __ASM_GENERIC_MMAN_H
#define __ASM_GENERIC_MMAN_H

#include <asm-generic/mman-common.h>

#define MAP_GROWSDOWN	0x0100		/* stack-like segment */
#define MAP_DENYWRITE	0x0800		/* ETXTBSY ֻ�����ӳ�������д��������������ļ�ֱ��д��Ĳ������ᱻ�ܾ�*/
#define MAP_EXECUTABLE	0x1000		/* mark it as an executable */
#define MAP_LOCKED	0x2000		/* pages are locked  ��ӳ����������ס�����ʾ�����򲻻ᱻ�û�*/
#define MAP_NORESERVE	0x4000		/* don't check for reservations ����ҪΪӳ�䱣���ռ�*/
#define MAP_POPULATE	0x8000		/* populate (prefault) pagetables ���ҳ��*/
#define MAP_NONBLOCK	0x10000		/* do not block on IO ��IO�ϲ���������*/
#define MAP_STACK	0x20000		/* give out an address that is best suited for process/thread stacks */
#define MAP_HUGETLB	0x40000		/* create a huge page mapping */

#define MCL_CURRENT	1		/* lock all current mappings */
#define MCL_FUTURE	2		/* lock all future mappings */

#endif /* __ASM_GENERIC_MMAN_H */
