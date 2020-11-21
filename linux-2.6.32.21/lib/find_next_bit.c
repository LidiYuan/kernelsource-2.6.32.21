/* find_next_bit.c: fallback find next bit implementation
 *
 * Copyright (C) 2004 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/bitops.h>
#include <linux/module.h>
#include <asm/types.h>
#include <asm/byteorder.h>

#define BITOP_WORD(nr)		((nr) / BITS_PER_LONG)

#ifdef CONFIG_GENERIC_FIND_NEXT_BIT
/*
 * Find the next set bit in a memory region.
 * 在内存区域中寻从offset开始找位为0的位置
   寻找long型数据中 从offset开始 没有被置位1的位置
   比如:
      0x01   如果从偏移值offset=0开始查找, 则返回的是1  ,因为位0已经被置位了1  ，位1还没被置位1
      0x02   如果从偏移值offset=0开始查找, 则返回的是0,因为位1被置位1,但位0还没有置1 
      0x03   如果从偏移值offset=0开始查找, 则返回的是2  ,因为位0,位1已经被置位了1  ，位2还没被置位1 
 */
unsigned long find_next_bit(const unsigned long *addr,//区域的地址, 可以支持数组 
                                unsigned long size,   //区域的最大长度
			                    unsigned long offset) //从offset开始查找第一个未被设置1的位
{
	
	unsigned long tmp;
	const unsigned long *p = addr + BITOP_WORD(offset);
	unsigned long result = offset & ~(BITS_PER_LONG-1);  //在offset 小于BITS_PER_LONG的时候 result都会是0


	//如果位的偏移大于最大的位数,则返回最大位数
	if (offset >= size)
		return size;
	
	size -= result; 
	offset %= BITS_PER_LONG;

	if (offset) 
	{
		tmp = *(p++);  //取得第一个long的数据
		tmp &= (~0UL << offset);

		if (size < BITS_PER_LONG)
			goto found_first;
		
		if (tmp)
			goto found_middle;

		size -= BITS_PER_LONG;
		result += BITS_PER_LONG;
	}
	while (size & ~(BITS_PER_LONG-1)) 
	{
		if ((tmp = *(p++)))
			goto found_middle;
		result += BITS_PER_LONG;
		size -= BITS_PER_LONG;
	}
	if (!size)
		return result;
	tmp = *p;

found_first:
	tmp &= (~0UL >> (BITS_PER_LONG - size));
	if (tmp == 0UL)		/* Are any bits set? */
		return result + size;	/* Nope. */
	
found_middle:
	return result + __ffs(tmp);
}
EXPORT_SYMBOL(find_next_bit);

/*
 * This implementation of find_{first,next}_zero_bit was stolen from
 * Linus' asm-alpha/bitops.h.
 */
unsigned long find_next_zero_bit(const unsigned long *addr, unsigned long size,
				 unsigned long offset)
{
	const unsigned long *p = addr + BITOP_WORD(offset);
	unsigned long result = offset & ~(BITS_PER_LONG-1);
	unsigned long tmp;

	if (offset >= size)
		return size;
	size -= result;
	offset %= BITS_PER_LONG;
	if (offset) {
		tmp = *(p++);
		tmp |= ~0UL >> (BITS_PER_LONG - offset);
		if (size < BITS_PER_LONG)
			goto found_first;
		if (~tmp)
			goto found_middle;
		size -= BITS_PER_LONG;
		result += BITS_PER_LONG;
	}
	while (size & ~(BITS_PER_LONG-1)) {
		if (~(tmp = *(p++)))
			goto found_middle;
		result += BITS_PER_LONG;
		size -= BITS_PER_LONG;
	}
	if (!size)
		return result;
	tmp = *p;

found_first:
	tmp |= ~0UL << size;
	if (tmp == ~0UL)	/* Are any bits zero? */
		return result + size;	/* Nope. */
found_middle:
	return result + ffz(tmp);
}
EXPORT_SYMBOL(find_next_zero_bit);
#endif /* CONFIG_GENERIC_FIND_NEXT_BIT */

#ifdef CONFIG_GENERIC_FIND_FIRST_BIT
/*
 * Find the first set bit in a memory region.
 */
unsigned long find_first_bit(const unsigned long *addr, unsigned long size)
{
	const unsigned long *p = addr;
	unsigned long result = 0;
	unsigned long tmp;

	while (size & ~(BITS_PER_LONG-1)) {
		if ((tmp = *(p++)))
			goto found;
		result += BITS_PER_LONG;
		size -= BITS_PER_LONG;
	}
	if (!size)
		return result;

	tmp = (*p) & (~0UL >> (BITS_PER_LONG - size));
	if (tmp == 0UL)		/* Are any bits set? */
		return result + size;	/* Nope. */
found:
	return result + __ffs(tmp);
}
EXPORT_SYMBOL(find_first_bit);

/*
 * Find the first cleared bit in a memory region.
 */
unsigned long find_first_zero_bit(const unsigned long *addr, unsigned long size)
{
	const unsigned long *p = addr;
	unsigned long result = 0;
	unsigned long tmp;

	while (size & ~(BITS_PER_LONG-1)) {
		if (~(tmp = *(p++)))
			goto found;
		result += BITS_PER_LONG;
		size -= BITS_PER_LONG;
	}
	if (!size)
		return result;

	tmp = (*p) | (~0UL << size);
	if (tmp == ~0UL)	/* Are any bits zero? */
		return result + size;	/* Nope. */
found:
	return result + ffz(tmp);
}
EXPORT_SYMBOL(find_first_zero_bit);
#endif /* CONFIG_GENERIC_FIND_FIRST_BIT */

#ifdef __BIG_ENDIAN

/* include/linux/byteorder does not support "unsigned long" type */
static inline unsigned long ext2_swabp(const unsigned long * x)
{
#if BITS_PER_LONG == 64
	return (unsigned long) __swab64p((u64 *) x);
#elif BITS_PER_LONG == 32
	return (unsigned long) __swab32p((u32 *) x);
#else
#error BITS_PER_LONG not defined
#endif
}

/* include/linux/byteorder doesn't support "unsigned long" type */
static inline unsigned long ext2_swab(const unsigned long y)
{
#if BITS_PER_LONG == 64
	return (unsigned long) __swab64((u64) y);
#elif BITS_PER_LONG == 32
	return (unsigned long) __swab32((u32) y);
#else
#error BITS_PER_LONG not defined
#endif
}

unsigned long generic_find_next_zero_le_bit(const unsigned long *addr, unsigned
		long size, unsigned long offset)
{
	const unsigned long *p = addr + BITOP_WORD(offset);
	unsigned long result = offset & ~(BITS_PER_LONG - 1);
	unsigned long tmp;

	if (offset >= size)
		return size;
	size -= result;
	offset &= (BITS_PER_LONG - 1UL);
	if (offset) {
		tmp = ext2_swabp(p++);
		tmp |= (~0UL >> (BITS_PER_LONG - offset));
		if (size < BITS_PER_LONG)
			goto found_first;
		if (~tmp)
			goto found_middle;
		size -= BITS_PER_LONG;
		result += BITS_PER_LONG;
	}

	while (size & ~(BITS_PER_LONG - 1)) {
		if (~(tmp = *(p++)))
			goto found_middle_swap;
		result += BITS_PER_LONG;
		size -= BITS_PER_LONG;
	}
	if (!size)
		return result;
	tmp = ext2_swabp(p);
found_first:
	tmp |= ~0UL << size;
	if (tmp == ~0UL)	/* Are any bits zero? */
		return result + size; /* Nope. Skip ffz */
found_middle:
	return result + ffz(tmp);

found_middle_swap:
	return result + ffz(ext2_swab(tmp));
}

EXPORT_SYMBOL(generic_find_next_zero_le_bit);

unsigned long generic_find_next_le_bit(const unsigned long *addr, unsigned
		long size, unsigned long offset)
{
	const unsigned long *p = addr + BITOP_WORD(offset);
	unsigned long result = offset & ~(BITS_PER_LONG - 1);
	unsigned long tmp;

	if (offset >= size)
		return size;
	size -= result;
	offset &= (BITS_PER_LONG - 1UL);
	if (offset) {
		tmp = ext2_swabp(p++);
		tmp &= (~0UL << offset);
		if (size < BITS_PER_LONG)
			goto found_first;
		if (tmp)
			goto found_middle;
		size -= BITS_PER_LONG;
		result += BITS_PER_LONG;
	}

	while (size & ~(BITS_PER_LONG - 1)) {
		tmp = *(p++);
		if (tmp)
			goto found_middle_swap;
		result += BITS_PER_LONG;
		size -= BITS_PER_LONG;
	}
	if (!size)
		return result;
	tmp = ext2_swabp(p);
found_first:
	tmp &= (~0UL >> (BITS_PER_LONG - size));
	if (tmp == 0UL)		/* Are any bits set? */
		return result + size; /* Nope. */
found_middle:
	return result + __ffs(tmp);

found_middle_swap:
	return result + __ffs(ext2_swab(tmp));
}
EXPORT_SYMBOL(generic_find_next_le_bit);
#endif /* __BIG_ENDIAN */
