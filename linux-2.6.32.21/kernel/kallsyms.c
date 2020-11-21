/*
 * kallsyms.c: in-kernel printing of symbolic oopses and stack traces.
 *
 * Rewritten and vastly simplified by Rusty Russell for in-kernel
 * module loader:
 *   Copyright 2002 Rusty Russell <rusty@rustcorp.com.au> IBM Corporation
 *
 * ChangeLog:
 *
 * (25/Aug/2004) Paulo Marques <pmarques@grupopie.com>
 *      Changed the compression method from stem compression to "table lookup"
 *      compression (see scripts/kallsyms.c for a more complete description)
 */
#include <linux/kallsyms.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/seq_file.h>
#include <linux/fs.h>
#include <linux/err.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>	/* for cond_resched */
#include <linux/mm.h>
#include <linux/ctype.h>

#include <asm/sections.h>

#ifdef CONFIG_KALLSYMS_ALL
#define all_var 1
#else
#define all_var 0
#endif

/*下面符号是在 由工具 scripts/kallsyms.c工具生成的汇编文件中
这些变量被嵌入在vmlinux中，所以在内核代码中直接extern就可以使用

kallsyms_addresses, 一个数组，存放所有符号的地址列表，按地址升序排列
kallsyms_num_syms, 符号的数量
kallsyms_names,   一个数组，存放所有符号的名称，和kallsyms_addresses一一对应
kallsyms_markers,

kallsyms_token_table, //压缩符号表


kallsyms_token_index,
*/

/*
 * These will be re-linked against their real values
 * during the second link stage.
 */
//保存符号的名和符号的地址  
extern const unsigned long kallsyms_addresses[] __attribute__((weak));

/*
存储的格式为<length1><string1><length2><string2>...即存储每个符号的长度和字符串 
*/
extern const u8 kallsyms_names[] __attribute__((weak));

/*
 * Tell the compiler that the count isn't in the small data section if the arch
 * has one (eg: FRV).
 */
 //内核中非模块的符号个数
extern const unsigned long kallsyms_num_syms


__attribute__((weak, section(".rodata")));

//压缩符号表  内容如下kallsyms_token_table表是如何生成的，可以阅读scripts/kallsyms.c的实现
/*
kallsyms_token_table:
   .asciz  "end"
   .asciz  "Tjffs2"
   .asciz  "map_"
   .asciz  "int"
   .asciz  "to_"
   .asciz  "Tn"
.asciz  "t__"
.asciz  "unregist"
... ...
.asciz  "a"
.asciz  "b"
.asciz  "c"
.asciz  "d"
.asciz  "e"
.asciz  "f"
.asciz  "g"

将常用的串存储在数组kallsyms_token_table数组中，而kallsyms_token_index则代表了该串的索引

*/
extern const u8 kallsyms_token_table[] __attribute__((weak));

//存放kallsyms_token_table 中的字符串索引
/*
kallsyms_token_index:
   .short  0
   .short  4     //Tjffs2第一个字符在kallsyms_token_table中的偏移
   .short  11
   .short  16
*/
extern const u16 kallsyms_token_index[] __attribute__((weak));

extern const unsigned long kallsyms_markers[] __attribute__((weak));

static inline int is_kernel_inittext(unsigned long addr)
{
	if (addr >= (unsigned long)_sinittext
	    && addr <= (unsigned long)_einittext)
		return 1;
	return 0;
}

static inline int is_kernel_text(unsigned long addr)
{
	if ((addr >= (unsigned long)_stext && addr <= (unsigned long)_etext) ||
	    arch_is_kernel_text(addr))
		return 1;
	return in_gate_area_no_task(addr);
}

static inline int is_kernel(unsigned long addr)
{
	if (addr >= (unsigned long)_stext && addr <= (unsigned long)_end)
		return 1;
	return in_gate_area_no_task(addr);
}

//
static int is_ksym_addr(unsigned long addr)
{
	if (all_var)//配置了CONFIG_KALLSYMS_ALL的化 则all_var为1
		return is_kernel(addr);

	return is_kernel_text(addr) || is_kernel_inittext(addr);
}

/*
 * Expand a compressed symbol data into the resulting uncompressed string,
 * given the offset to where the symbol is in the compressed stream.
 */
 /*
将符号解压并存储到result字符串中,并且返回下一个符号在压缩流中的偏移位置
*/
static unsigned int kallsyms_expand_symbol(unsigned int off, char *result)
{
	int len, skipped_first = 0;
	const u8 *tptr, *data;

	/* Get the compressed symbol length from the first symbol byte. */
    /*获得压缩符号的长度 符号的第一个字节存放的是符号字符串的长度*/
	data = &kallsyms_names[off];

    //将长度临时存储到len中,并且data指向真正的符号起始地址
	len = *data;
	data++;

	/*
	 * Update the offset to return the offset for the next symbol on
	 * the compressed stream.
	 */
	//更新偏移量,返回下一个符号在压缩流中的偏移位置 
	off += len + 1;

	/*
	 * For every byte on the compressed symbol data, copy the table
	 * entry for that byte.
	 */
	while (len) 
	{
	/*
    前面我们介绍过，为了提高查询速度，我们将常用的字符串存储在kallsyms_token_table中，kallsyms_token_index记录每个ascii字符的替代串在kallsyms_token_table中的偏移
比如我们需要查找的符号信息是 0x03, 0xbe, 0xbc, 0x71其中3代表了符号的长度，而后面紧跟的三个字节就是符号的内容了。我们需要知道be究竟代表的是什么串。我们首先需要通过
kallsyms_token_index[0xbe]查到0xbe所对应的串在kallsyms_token_table中的索引，然后将该串的首地址赋给tptr。从表1中我们查到0xbe（190）所对应的串为 "t.text.lock."其中第一个字母t代表了符号的类型
然后data指向下一个要解析的字节0xbc
	*/
		tptr = &kallsyms_token_table[kallsyms_token_index[*data]];
		data++;
		len--;

		while (*tptr) 
		{
		    //跳过字符串的第一个字节 第一个自己存放的是字符串的类型, 类型+符号
		    //并将符号存储到result中 返回给用户
			if (skipped_first) 
			{
				*result = *tptr;
				result++;
			} 
			else
				skipped_first = 1;
			tptr++;
		}
	}

	*result = '\0';

	/* Return to offset to the next symbol. */
	return off;
}

/*
 * Get symbol type information. This is encoded as a single char at the
 * beginning of the symbol name.
 */
static char kallsyms_get_symbol_type(unsigned int off)
{
	/*
	 * Get just the first code, look it up in the token table,
	 * and return the first char from this token.
	 */
	return kallsyms_token_table[kallsyms_token_index[kallsyms_names[off + 1]]];
}


/*
 * Find the offset on the compressed stream given and index in the
 * kallsyms array.
 */
 //获得符号在压缩流中的偏移
static unsigned int get_symbol_offset(unsigned long pos)
{
	const u8 *name;
	int i;

	/*
	 * Use the closest marker we have. We have markers every 256 positions,
	 * so that should be close enough.
	 */
	/*linux内核为了便于查询，将kallsyms_names将符号每256个分为一组，而这个pos前面我们已经分析过了，就是符号地址*/ 
	//为了防止大量的变量 每256个位置 将偏移就保存到 kallsyms_markers中
	/*
     在kallsyms_address中的索引。那么显然pos>>8就代表了该符号位于kallsyms_names中的哪一组了
     而kallsyms_markers这个数组中的每个元素分别代表了该组的字符串在kallsyms_names中的偏移量
     我们通过pos查到该符号所属的组；然后通过kallsyms_markers数组查询到该组的字符串在kallsyms_names中的索引。
	*/
	name = &kallsyms_names[kallsyms_markers[pos >> 8]];

	/*
	 * Sequentially scan all the symbols up to the point we're searching
	 * for. Every symbol is stored in a [<len>][<len> bytes of data] format,
	 * so we just need to add the len to the current pointer for every
	 * symbol we wish to skip.
	 */
	 //显然pos并不一定就是改组的第一个符号，那么接下来，我们就要从第一个符号起，开始查找，直到找到pos这个符号为止
	for (i = 0; i < (pos & 0xFF); i++)
		name = name + (*name) + 1;

	//返回偏移值
	return name - kallsyms_names;
}

/* Lookup the address for this symbol. Returns 0 if not found. 
   查找符号的地址, 如果没有发现则返回0
*/
unsigned long kallsyms_lookup_name(const char *name)
{
	char namebuf[KSYM_NAME_LEN];
	unsigned long i;
	unsigned int off;

    //遍历所有的符号
	for (i = 0, off = 0; i < kallsyms_num_syms; i++) 
	{
	    //我们从上面知道kallsyms_expand_symbol函数是解压获得一个符号,并且从压缩流中返回下一个符号的偏移
		off = kallsyms_expand_symbol(off, namebuf);

        //比较符号是否是要查找的符号,是的化则返回符号地址
		if (strcmp(namebuf, name) == 0)
			return kallsyms_addresses[i];
	}

	//如果纯内核符号中没有找到,则到模块中查找
	return module_kallsyms_lookup_name(name);
}

int kallsyms_on_each_symbol(int (*fn)(void *, const char *, struct module *,unsigned long),
			                              void *data)
{
	char namebuf[KSYM_NAME_LEN];
	unsigned long i;
	unsigned int off;
	int ret;

	for (i = 0, off = 0; i < kallsyms_num_syms; i++) {
		off = kallsyms_expand_symbol(off, namebuf);
		ret = fn(data, namebuf, NULL, kallsyms_addresses[i]);
		if (ret != 0)
			return ret;
	}
	return module_kallsyms_on_each_symbol(fn, data);
}
EXPORT_SYMBOL_GPL(kallsyms_on_each_symbol);

//返回符号在kallsyms_addresses中的索引
static unsigned long get_symbol_pos(unsigned long addr,      //要查找的符号地址
				                       unsigned long *symbolsize,//返回符号的长度
				                       unsigned long *offset)    //返回符号的偏移
{
	unsigned long symbol_start = 0, symbol_end = 0;
	unsigned long i, low, high, mid;

	/* This kernel should never had been booted. */
	BUG_ON(!kallsyms_addresses);

	/* Do a binary search on the sorted kallsyms_addresses array. */
	low = 0;
	high = kallsyms_num_syms;

    //进行二分查找 addr所属于哪个符号的范围内
    //31245748
	while (high - low > 1) 
	{
		mid = low + (high - low) / 2;
		if (kallsyms_addresses[mid] <= addr)
			low = mid;
		else
			high = mid;
	}

	/*
	 * Search for the first aliased symbol. Aliased
	 * symbols are symbols with the same address.
	 *查找第一个匿名的符号, 匿名的符号有相同的地址
	 */
	while (low && kallsyms_addresses[low-1] == kallsyms_addresses[low])
		--low;

    //将查找到的符号临时放到symbol_start中
	symbol_start = kallsyms_addresses[low];

	/* Search for next non-aliased symbol. 
       查找下一个非匿名的符号地址
	*/
	for (i = low + 1; i < kallsyms_num_syms; i++) 
	{
		if (kallsyms_addresses[i] > symbol_start) 
		{
			symbol_end = kallsyms_addresses[i];
			break;
		}
	}

	/* If we found no next symbol, we use the end of the section. */
    //如果发现没有下一个符号 我们使用节的结束地址
	if (!symbol_end) 
	{
		if (is_kernel_inittext(addr))
			symbol_end = (unsigned long)_einittext;
		else if (all_var)
			symbol_end = (unsigned long)_end;
		else
			symbol_end = (unsigned long)_etext;
	}

	//传出符号的大小
	if (symbolsize)
		*symbolsize = symbol_end - symbol_start;

	//传出符号的偏移
	if (offset)
		*offset = addr - symbol_start;

	//返回符号在kallsyms_addresses中的索引
	return low;
}

/*
 * Lookup an address but don't bother to find any names.
 */
int kallsyms_lookup_size_offset(unsigned long addr, unsigned long *symbolsize,
				unsigned long *offset)
{
	char namebuf[KSYM_NAME_LEN];
	if (is_ksym_addr(addr))
		return !!get_symbol_pos(addr, symbolsize, offset);

	return !!module_address_lookup(addr, symbolsize, offset, NULL, namebuf);
}

/*
 * Lookup an address
 * - modname is set to NULL if it's in the kernel.
 * - We guarantee that the returned name is valid until we reschedule even if.
 *   It resides in a module.
 * - We also guarantee that modname will be valid until rescheduled.
 *
    如果符号在内核,不在module中 则返回的modname置位空
    
 */
const char *kallsyms_lookup(unsigned long addr,                    //输入值 要查找的符号地址
			                         unsigned long *symbolsize,    //输出值, 返回符号的大小
			                         unsigned long *offset,        //输出值,返回符号的偏移量
			                         char **modname, 
			                         char *namebuf)
{
	namebuf[KSYM_NAME_LEN - 1] = 0;
	namebuf[0] = 0;

    //检查地址属于内核
	if (is_ksym_addr(addr)) 
	{
		unsigned long pos;

		//返回符号在kallsyms_addresses的索引
		pos = get_symbol_pos(addr, symbolsize, offset);

		/* Grab name 获得符号名*/
		kallsyms_expand_symbol(get_symbol_offset(pos), namebuf);
		if (modname)
			*modname = NULL;
		return namebuf;
	}

	/* See if it's in a module. 
       在模块中查找
	*/
	return module_address_lookup(addr, 
	                             symbolsize, offset, modname,
				                 namebuf);
}

int lookup_symbol_name(unsigned long addr, char *symname)
{
	symname[0] = '\0';
	symname[KSYM_NAME_LEN - 1] = '\0';

	if (is_ksym_addr(addr)) //地址有效性检查
	{
		unsigned long pos;

		pos = get_symbol_pos(addr, NULL, NULL);
		/* Grab name */
		kallsyms_expand_symbol(get_symbol_offset(pos), symname);//获取不好名称到symname
		return 0;
	}
	/* See if it's in a module. */
	return lookup_module_symbol_name(addr, symname);//从module符号表中查找
}

int lookup_symbol_attrs(unsigned long addr, unsigned long *size,
			unsigned long *offset, char *modname, char *name)
{
	name[0] = '\0';
	name[KSYM_NAME_LEN - 1] = '\0';

	if (is_ksym_addr(addr)) {
		unsigned long pos;

		pos = get_symbol_pos(addr, size, offset);
		/* Grab name */
		kallsyms_expand_symbol(get_symbol_offset(pos), name);
		modname[0] = '\0';
		return 0;
	}
	/* See if it's in a module. */
	return lookup_module_symbol_attrs(addr, size, offset, modname, name);
}

/* Look up a kernel symbol and return it in a text buffer. */
int sprint_symbol(char *buffer, unsigned long address)
{
	char *modname;
	const char *name;
	unsigned long offset, size;
	int len;

	name = kallsyms_lookup(address, &size, &offset, &modname, buffer);
	if (!name)
		return sprintf(buffer, "0x%lx", address);

	if (name != buffer)
		strcpy(buffer, name);
	len = strlen(buffer);
	buffer += len;

	if (modname)
		len += sprintf(buffer, "+%#lx/%#lx [%s]",
						offset, size, modname);
	else
		len += sprintf(buffer, "+%#lx/%#lx", offset, size);

	return len;
}

EXPORT_SYMBOL_GPL(sprint_symbol);

/* Look up a kernel symbol and print it to the kernel messages. */
void __print_symbol(const char *fmt, unsigned long address)
{
	char buffer[KSYM_SYMBOL_LEN];

	sprint_symbol(buffer, address);

	printk(fmt, buffer);
}
EXPORT_SYMBOL(__print_symbol);

/* To avoid using get_symbol_offset for every symbol, we carry prefix along. */
struct kallsym_iter {
	loff_t pos;
	unsigned long value;
	unsigned int nameoff; /* If iterating in core kernel symbols. */
	char type;
	char name[KSYM_NAME_LEN];
	char module_name[MODULE_NAME_LEN];
	int exported;
};

static int get_ksymbol_mod(struct kallsym_iter *iter)
{
	if (module_get_kallsym(iter->pos - kallsyms_num_syms, 
		                   &iter->value,
		                   &iter->type, 
		                   iter->name, 
		                   iter->module_name,
				           &iter->exported) < 0
				          )
		return 0;
	return 1;
}

/* Returns space to next name. */
static unsigned long get_ksymbol_core(struct kallsym_iter *iter)
{
	unsigned off = iter->nameoff;

	iter->module_name[0] = '\0';
	iter->value = kallsyms_addresses[iter->pos];

	//获得符号的类型
	iter->type = kallsyms_get_symbol_type(off);

	off = kallsyms_expand_symbol(off, iter->name);

	return off - iter->nameoff;
}

static void reset_iter(struct kallsym_iter *iter, loff_t new_pos)
{
	iter->name[0] = '\0';
	iter->nameoff = get_symbol_offset(new_pos);
	iter->pos = new_pos;
}

/* Returns false if pos at or past end of file. */
static int update_iter(struct kallsym_iter *iter, loff_t pos)
{
	/* Module symbols can be accessed randomly. */
    //大于非模块内核符号的个数,则开始查找模块中的符号
	if (pos >= kallsyms_num_syms) 
	{
		iter->pos = pos;
		return get_ksymbol_mod(iter);
	}

	/* If we're not on the desired position, reset to new position. */
	if (pos != iter->pos)
		reset_iter(iter, pos);

	iter->nameoff += get_ksymbol_core(iter);
	iter->pos++;

	return 1;
}

static void *s_next(struct seq_file *m, void *p, loff_t *pos)
{
	(*pos)++;

	if (!update_iter(m->private, *pos))
		return NULL;
	return p;
}

static void *s_start(struct seq_file *m, loff_t *pos)
{
	if (!update_iter(m->private, *pos))
		return NULL;
	return m->private;
}

static void s_stop(struct seq_file *m, void *p)
{
}

//显示单个符号的信息
static int s_show(struct seq_file *m, void *p)
{
	struct kallsym_iter *iter = m->private;

	//一些调试用的符号没有名字 忽略他们
	/* Some debugging symbols have no name.  Ignore them. */
	if (!iter->name[0])
		return 0;

    //如果此符号是模块中的符号名
	if (iter->module_name[0]) 
	{
		char type;

		/*
		 * Label it "global" if it is exported,
		 * "local" if not exported.
		 */
		type = iter->exported ? toupper(iter->type) :tolower(iter->type);
		seq_printf(m, "%0*lx  %c %s\t[%s]\n",(int)(2 * sizeof(void *)),iter->value, type, iter->name, iter->module_name);
	} 
	else
		seq_printf(m, "%0*lx %c %s\n",(int)(2 * sizeof(void *)),iter->value, iter->type, iter->name);

	return 0;
}

static const struct seq_operations kallsyms_op = {
	.start = s_start,
	.next = s_next,
	.stop = s_stop,
	.show = s_show
};

static int kallsyms_open(struct inode *inode, struct file *file)
{
	/*
	 * We keep iterator in m->private, since normal case is to
	 * s_start from where we left off, so we avoid doing
	 * using get_symbol_offset for every symbol.
	 */
	struct kallsym_iter *iter;
	int ret;

	iter = kmalloc(sizeof(*iter), GFP_KERNEL);
	if (!iter)
		return -ENOMEM;
	reset_iter(iter, 0);

	ret = seq_open(file, &kallsyms_op);
	if (ret == 0)
		((struct seq_file *)file->private_data)->private = iter;
	else
		kfree(iter);
	return ret;
}

static const struct file_operations kallsyms_operations = {
	.open = kallsyms_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release_private,
};

static int __init kallsyms_init(void)
{
	proc_create("kallsyms", 0444, NULL, &kallsyms_operations);
	return 0;
}
device_initcall(kallsyms_init);
