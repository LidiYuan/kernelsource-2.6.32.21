/*
   Copyright (C) 2002 Richard Henderson
   Copyright (C) 2001 Rusty Russell, 2002 Rusty Russell IBM.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/
#include <linux/module.h>
#include <linux/moduleloader.h>
#include <linux/ftrace_event.h>
#include <linux/init.h>
#include <linux/kallsyms.h>
#include <linux/fs.h>
#include <linux/sysfs.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/elf.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/syscalls.h>
#include <linux/fcntl.h>
#include <linux/rcupdate.h>
#include <linux/capability.h>
#include <linux/cpu.h>
#include <linux/moduleparam.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/vermagic.h>
#include <linux/notifier.h>
#include <linux/sched.h>
#include <linux/stop_machine.h>
#include <linux/device.h>
#include <linux/string.h>
#include <linux/mutex.h>
#include <linux/rculist.h>
#include <asm/uaccess.h>
#include <asm/cacheflush.h>
#include <asm/mmu_context.h>
#include <linux/license.h>
#include <asm/sections.h>
#include <linux/tracepoint.h>
#include <linux/ftrace.h>
#include <linux/async.h>
#include <linux/percpu.h>
#include <linux/kmemleak.h>

#define CREATE_TRACE_POINTS
#include <trace/events/module.h>

EXPORT_TRACEPOINT_SYMBOL(module_get);

#if 0
#define DEBUGP printk
#else
#define DEBUGP(fmt , a...)
#endif

#ifndef ARCH_SHF_SMALL
#define ARCH_SHF_SMALL 0
#endif

/* If this is set, the section belongs in the init part of the module */
#define INIT_OFFSET_MASK (1UL << (BITS_PER_LONG-1))

/* List of modules, protected by module_mutex or preempt_disable
 * (delete uses stop_machine/add uses RCU list operations). */
DEFINE_MUTEX(module_mutex);
EXPORT_SYMBOL_GPL(module_mutex);
static LIST_HEAD(modules);

/* Block module loading/unloading? */
int modules_disabled = 0; //阻塞模块的加载和卸载

/* Waiting for a module to finish initializing? */
static DECLARE_WAIT_QUEUE_HEAD(module_wq);

//定义模块加载的通知连
static BLOCKING_NOTIFIER_HEAD(module_notify_list);

/* Bounds of module allocation, for speeding __module_address */
static unsigned long module_addr_min = -1UL, module_addr_max = 0;

//注册模块加载的通知连侦听点
int register_module_notifier(struct notifier_block * nb)
{
	return blocking_notifier_chain_register(&module_notify_list, nb);
}
EXPORT_SYMBOL(register_module_notifier);


//反注册模块加载的通知连侦听点
int unregister_module_notifier(struct notifier_block * nb)
{
	return blocking_notifier_chain_unregister(&module_notify_list, nb);
}

EXPORT_SYMBOL(unregister_module_notifier);

/* We require a truly strong try_module_get(): 0 means failure due to
   ongoing or failed initialization etc. */
   
static inline int strong_try_module_get(struct module *mod)
{
	if (mod && mod->state == MODULE_STATE_COMING)
		return -EBUSY;
	if (try_module_get(mod))
		return 0;
	else
		return -ENOENT;
}

static inline void add_taint_module(struct module *mod, unsigned flag)
{
	add_taint(flag);
	mod->taints |= (1U << flag);
}

/*
 * A thread that wants to hold a reference to a module only while it
 * is running can call this to safely exit.  nfsd and lockd use this.
 */
void __module_put_and_exit(struct module *mod, long code)
{
	module_put(mod);
	do_exit(code);
}
EXPORT_SYMBOL(__module_put_and_exit);

/* Find a module section: 0 means not found. */
//根据节名，查找一个节是否存在，并返回节表项的索引
static unsigned int find_sec(Elf_Ehdr *hdr,  //Elf header起始地址
			     Elf_Shdr *sechdrs,            //节表头的起始地址
			     const char *secstrings,       //节名表的起始地址
			     const char *name)             //要查找的节的名字
{
	unsigned int i;

	for (i = 1; i < hdr->e_shnum; i++)
	{
		/* Alloc bit cleared means "ignore it." */
		if ((sechdrs[i].sh_flags & SHF_ALLOC) &&  //表示此节在内存中是要分配内存的
			 strcmp(secstrings+sechdrs[i].sh_name, name) == 0)//进行名字比较
			return i;
	}

	//表示没有找到
	return 0;
}

/* Find a module section, or NULL. */
static void *section_addr(Elf_Ehdr *hdr, Elf_Shdr *shdrs,
			  const char *secstrings, const char *name)
{
	/* Section 0 has sh_addr 0. */
	return (void *)shdrs[find_sec(hdr, shdrs, secstrings, name)].sh_addr;
}

/* Find a module section, or NULL.  Fill in number of "objects" in section. */
static void *section_objs(Elf_Ehdr *hdr,
			  Elf_Shdr *sechdrs,
			  const char *secstrings,
			  const char *name,
			  size_t object_size,
			  unsigned int *num)
{
	unsigned int sec = find_sec(hdr, sechdrs, secstrings, name);

	/* Section 0 has sh_addr 0 and sh_size 0. */
	*num = sechdrs[sec].sh_size / object_size;
	return (void *)sechdrs[sec].sh_addr;
}

/* Provided by the linker */
extern const struct kernel_symbol __start___ksymtab[];
extern const struct kernel_symbol __stop___ksymtab[];
extern const struct kernel_symbol __start___ksymtab_gpl[];
extern const struct kernel_symbol __stop___ksymtab_gpl[];
extern const struct kernel_symbol __start___ksymtab_gpl_future[];
extern const struct kernel_symbol __stop___ksymtab_gpl_future[];
extern const struct kernel_symbol __start___ksymtab_gpl_future[];
extern const struct kernel_symbol __stop___ksymtab_gpl_future[];
extern const unsigned long __start___kcrctab[];
extern const unsigned long __start___kcrctab_gpl[];
extern const unsigned long __start___kcrctab_gpl_future[];
#ifdef CONFIG_UNUSED_SYMBOLS
extern const struct kernel_symbol __start___ksymtab_unused[];
extern const struct kernel_symbol __stop___ksymtab_unused[];
extern const struct kernel_symbol __start___ksymtab_unused_gpl[];
extern const struct kernel_symbol __stop___ksymtab_unused_gpl[];
extern const unsigned long __start___kcrctab_unused[];
extern const unsigned long __start___kcrctab_unused_gpl[];
#endif

#ifndef CONFIG_MODVERSIONS
#define symversion(base, idx) NULL
#else
#define symversion(base, idx) ((base != NULL) ? ((base) + (idx)) : NULL)
#endif

static bool each_symbol_in_section(const struct symsearch *arr,
				   unsigned int arrsize,
				   struct module *owner,
				   bool (*fn)(const struct symsearch *syms,
					      struct module *owner,
					      unsigned int symnum, void *data),
				   void *data)
{
	unsigned int i, j;

	for (j = 0; j < arrsize; j++) {
		for (i = 0; i < arr[j].stop - arr[j].start; i++)
			if (fn(&arr[j], owner, i, data))
				return true;
	}

	return false;
}

/* Returns true as soon as fn returns true, otherwise false. */
bool each_symbol(bool (*fn)(const struct symsearch *arr, struct module *owner,
			    unsigned int symnum, void *data), void *data)
{
	struct module *mod;
	const struct symsearch arr[] = {
		{ __start___ksymtab, __stop___ksymtab, __start___kcrctab,
		  NOT_GPL_ONLY, false },
		{ __start___ksymtab_gpl, __stop___ksymtab_gpl,
		  __start___kcrctab_gpl,
		  GPL_ONLY, false },
		{ __start___ksymtab_gpl_future, __stop___ksymtab_gpl_future,
		  __start___kcrctab_gpl_future,
		  WILL_BE_GPL_ONLY, false },
#ifdef CONFIG_UNUSED_SYMBOLS
		{ __start___ksymtab_unused, __stop___ksymtab_unused,
		  __start___kcrctab_unused,
		  NOT_GPL_ONLY, true },
		{ __start___ksymtab_unused_gpl, __stop___ksymtab_unused_gpl,
		  __start___kcrctab_unused_gpl,
		  GPL_ONLY, true },
#endif
	};

	if (each_symbol_in_section(arr, ARRAY_SIZE(arr), NULL, fn, data))
		return true;

	list_for_each_entry_rcu(mod, &modules, list) {
		struct symsearch arr[] = {
			{ mod->syms, mod->syms + mod->num_syms, mod->crcs,
			  NOT_GPL_ONLY, false },
			{ mod->gpl_syms, mod->gpl_syms + mod->num_gpl_syms,
			  mod->gpl_crcs,
			  GPL_ONLY, false },
			{ mod->gpl_future_syms,
			  mod->gpl_future_syms + mod->num_gpl_future_syms,
			  mod->gpl_future_crcs,
			  WILL_BE_GPL_ONLY, false },
#ifdef CONFIG_UNUSED_SYMBOLS
			{ mod->unused_syms,
			  mod->unused_syms + mod->num_unused_syms,
			  mod->unused_crcs,
			  NOT_GPL_ONLY, true },
			{ mod->unused_gpl_syms,
			  mod->unused_gpl_syms + mod->num_unused_gpl_syms,
			  mod->unused_gpl_crcs,
			  GPL_ONLY, true },
#endif
		};

		if (each_symbol_in_section(arr, ARRAY_SIZE(arr), mod, fn, data))
			return true;
	}
	return false;
}
EXPORT_SYMBOL_GPL(each_symbol);

struct find_symbol_arg {
	/* Input */
	const char *name;
	bool gplok;
	bool warn;

	/* Output */
	struct module *owner;
	const unsigned long *crc;
	const struct kernel_symbol *sym;
};

static bool find_symbol_in_section(const struct symsearch *syms,
				   struct module *owner,
				   unsigned int symnum, void *data)
{
	struct find_symbol_arg *fsa = data;

	if (strcmp(syms->start[symnum].name, fsa->name) != 0)
		return false;

	if (!fsa->gplok) {
		if (syms->licence == GPL_ONLY)
			return false;
		if (syms->licence == WILL_BE_GPL_ONLY && fsa->warn) {
			printk(KERN_WARNING "Symbol %s is being used "
			       "by a non-GPL module, which will not "
			       "be allowed in the future\n", fsa->name);
			printk(KERN_WARNING "Please see the file "
			       "Documentation/feature-removal-schedule.txt "
			       "in the kernel source tree for more details.\n");
		}
	}

#ifdef CONFIG_UNUSED_SYMBOLS
	if (syms->unused && fsa->warn) {
		printk(KERN_WARNING "Symbol %s is marked as UNUSED, "
		       "however this module is using it.\n", fsa->name);
		printk(KERN_WARNING
		       "This symbol will go away in the future.\n");
		printk(KERN_WARNING
		       "Please evalute if this is the right api to use and if "
		       "it really is, submit a report the linux kernel "
		       "mailinglist together with submitting your code for "
		       "inclusion.\n");
	}
#endif

	fsa->owner = owner;
	fsa->crc = symversion(syms->crcs, symnum);
	fsa->sym = &syms->start[symnum];
	return true;
}

/* Find a symbol and return it, along with, (optional) crc and
 * (optional) module which owns it */
const struct kernel_symbol *find_symbol(const char *name,
					                          struct module **owner,
					                          const unsigned long **crc,
					                          bool gplok,
					                          bool warn)
{
	struct find_symbol_arg fsa;

	fsa.name = name;
	fsa.gplok = gplok;
	fsa.warn = warn;

	if (each_symbol(find_symbol_in_section, &fsa)) 
	{
		if (owner)
			*owner = fsa.owner;
		if (crc)
			*crc = fsa.crc;
		return fsa.sym;
	}

	DEBUGP("Failed to find symbol %s\n", name);
	return NULL;
}
EXPORT_SYMBOL_GPL(find_symbol);

/* Search for module by name: must hold module_mutex. */
struct module *find_module(const char *name)
{
	struct module *mod;

	list_for_each_entry(mod, &modules, list) {
		if (strcmp(mod->name, name) == 0)
			return mod;
	}
	return NULL;
}
EXPORT_SYMBOL_GPL(find_module);

#ifdef CONFIG_SMP

#ifndef CONFIG_HAVE_LEGACY_PER_CPU_AREA

static void *percpu_modalloc(unsigned long size, unsigned long align,
			     const char *name)
{
	void *ptr;

	if (align > PAGE_SIZE) 
	{
		printk(KERN_WARNING "%s: per-cpu alignment %li > %li\n",
		       name, align, PAGE_SIZE);
		align = PAGE_SIZE;
	}

	ptr = __alloc_reserved_percpu(size, align);
	if (!ptr)
		printk(KERN_WARNING
		       "Could not allocate %lu bytes percpu data\n", size);
	return ptr;
}

static void percpu_modfree(void *freeme)
{
	free_percpu(freeme);
}

#else /* ... CONFIG_HAVE_LEGACY_PER_CPU_AREA */

/* Number of blocks used and allocated. */
static unsigned int pcpu_num_used, pcpu_num_allocated;
/* Size of each block.  -ve means used. */
static int *pcpu_size;

static int split_block(unsigned int i, unsigned short size)
{
	/* Reallocation required? */
	if (pcpu_num_used + 1 > pcpu_num_allocated) {
		int *new;

		new = krealloc(pcpu_size, sizeof(new[0])*pcpu_num_allocated*2,
			       GFP_KERNEL);
		if (!new)
			return 0;

		pcpu_num_allocated *= 2;
		pcpu_size = new;
	}

	/* Insert a new subblock */
	memmove(&pcpu_size[i+1], &pcpu_size[i],
		sizeof(pcpu_size[0]) * (pcpu_num_used - i));
	pcpu_num_used++;

	pcpu_size[i+1] -= size;
	pcpu_size[i] = size;
	return 1;
}

static inline unsigned int block_size(int val)
{
	if (val < 0)
		return -val;
	return val;
}

static void *percpu_modalloc(unsigned long size, unsigned long align,
			     const char *name)
{
	unsigned long extra;
	unsigned int i;
	void *ptr;
	int cpu;

	if (align > PAGE_SIZE) {
		printk(KERN_WARNING "%s: per-cpu alignment %li > %li\n",
		       name, align, PAGE_SIZE);
		align = PAGE_SIZE;
	}

	ptr = __per_cpu_start;
	for (i = 0; i < pcpu_num_used; ptr += block_size(pcpu_size[i]), i++) {
		/* Extra for alignment requirement. */
		extra = ALIGN((unsigned long)ptr, align) - (unsigned long)ptr;
		BUG_ON(i == 0 && extra != 0);

		if (pcpu_size[i] < 0 || pcpu_size[i] < extra + size)
			continue;

		/* Transfer extra to previous block. */
		if (pcpu_size[i-1] < 0)
			pcpu_size[i-1] -= extra;
		else
			pcpu_size[i-1] += extra;
		pcpu_size[i] -= extra;
		ptr += extra;

		/* Split block if warranted */
		if (pcpu_size[i] - size > sizeof(unsigned long))
			if (!split_block(i, size))
				return NULL;

		/* add the per-cpu scanning areas */
		for_each_possible_cpu(cpu)
			kmemleak_alloc(ptr + per_cpu_offset(cpu), size, 0,
				       GFP_KERNEL);

		/* Mark allocated */
		pcpu_size[i] = -pcpu_size[i];
		return ptr;
	}

	printk(KERN_WARNING "Could not allocate %lu bytes percpu data\n",
	       size);
	return NULL;
}

static void percpu_modfree(void *freeme)
{
	unsigned int i;
	void *ptr = __per_cpu_start + block_size(pcpu_size[0]);
	int cpu;

	/* First entry is core kernel percpu data. */
	for (i = 1; i < pcpu_num_used; ptr += block_size(pcpu_size[i]), i++) {
		if (ptr == freeme) {
			pcpu_size[i] = -pcpu_size[i];
			goto free;
		}
	}
	BUG();

 free:
	/* remove the per-cpu scanning areas */
	for_each_possible_cpu(cpu)
		kmemleak_free(freeme + per_cpu_offset(cpu));

	/* Merge with previous? */
	if (pcpu_size[i-1] >= 0) {
		pcpu_size[i-1] += pcpu_size[i];
		pcpu_num_used--;
		memmove(&pcpu_size[i], &pcpu_size[i+1],
			(pcpu_num_used - i) * sizeof(pcpu_size[0]));
		i--;
	}
	/* Merge with next? */
	if (i+1 < pcpu_num_used && pcpu_size[i+1] >= 0) {
		pcpu_size[i] += pcpu_size[i+1];
		pcpu_num_used--;
		memmove(&pcpu_size[i+1], &pcpu_size[i+2],
			(pcpu_num_used - (i+1)) * sizeof(pcpu_size[0]));
	}
}

static int percpu_modinit(void)
{
	pcpu_num_used = 2;
	pcpu_num_allocated = 2;
	pcpu_size = kmalloc(sizeof(pcpu_size[0]) * pcpu_num_allocated,
			    GFP_KERNEL);
	/* Static in-kernel percpu data (used). */
	pcpu_size[0] = -(__per_cpu_end-__per_cpu_start);
	/* Free room. */
	pcpu_size[1] = PERCPU_ENOUGH_ROOM + pcpu_size[0];
	if (pcpu_size[1] < 0) {
		printk(KERN_ERR "No per-cpu room for modules.\n");
		pcpu_num_used = 1;
	}

	return 0;
}
__initcall(percpu_modinit);

#endif /* CONFIG_HAVE_LEGACY_PER_CPU_AREA */

static unsigned int find_pcpusec(Elf_Ehdr *hdr,
				 Elf_Shdr *sechdrs,
				 const char *secstrings)
{
	return find_sec(hdr, sechdrs, secstrings, ".data.percpu");
}

static void percpu_modcopy(void *pcpudest, const void *from, unsigned long size)
{
	int cpu;

	for_each_possible_cpu(cpu)
		memcpy(pcpudest + per_cpu_offset(cpu), from, size);
}

#else /* ... !CONFIG_SMP */

static inline void *percpu_modalloc(unsigned long size, unsigned long align,
				    const char *name)
{
	return NULL;
}
static inline void percpu_modfree(void *pcpuptr)
{
	BUG();
}
static inline unsigned int find_pcpusec(Elf_Ehdr *hdr,
					Elf_Shdr *sechdrs,
					const char *secstrings)
{
	return 0;
}
static inline void percpu_modcopy(void *pcpudst, const void *src,
				  unsigned long size)
{
	/* pcpusec should be 0, and size of that section should be 0. */
	BUG_ON(size != 0);
}

#endif /* CONFIG_SMP */

#define MODINFO_ATTR(field)	\
static void setup_modinfo_##field(struct module *mod, const char *s)  \
{                                                                     \
	mod->field = kstrdup(s, GFP_KERNEL);                          \
}                                                                     \
static ssize_t show_modinfo_##field(struct module_attribute *mattr,   \
	                struct module *mod, char *buffer)             \
{                                                                     \
	return sprintf(buffer, "%s\n", mod->field);                   \
}                                                                     \
static int modinfo_##field##_exists(struct module *mod)               \
{                                                                     \
	return mod->field != NULL;                                    \
}                                                                     \
static void free_modinfo_##field(struct module *mod)                  \
{                                                                     \
	kfree(mod->field);                                            \
	mod->field = NULL;                                            \
}                                                                     \
static struct module_attribute modinfo_##field = {                    \
	.attr = { .name = __stringify(field), .mode = 0444 },         \
	.show = show_modinfo_##field,                                 \
	.setup = setup_modinfo_##field,                               \
	.test = modinfo_##field##_exists,                             \
	.free = free_modinfo_##field,                                 \
};

MODINFO_ATTR(version);
MODINFO_ATTR(srcversion);

static char last_unloaded_module[MODULE_NAME_LEN+1];

#ifdef CONFIG_MODULE_UNLOAD
/* Init the unload section of the module. */
static void module_unload_init(struct module *mod)
{
	int cpu;

	INIT_LIST_HEAD(&mod->modules_which_use_me);
	for_each_possible_cpu(cpu)
	local_set(__module_ref_addr(mod, cpu), 0);
	/* Hold reference count during initialization. */
	local_set(__module_ref_addr(mod, raw_smp_processor_id()), 1);
	/* Backwards compatibility macros put refcount during init. */
	mod->waiter = current;
}

/* modules using other modules */
struct module_use
{
	struct list_head list;
	struct module *module_which_uses;
};

/* Does a already use b? */
static int already_uses(struct module *a, struct module *b)
{
	struct module_use *use;

	list_for_each_entry(use, &b->modules_which_use_me, list) {
		if (use->module_which_uses == a) {
			DEBUGP("%s uses %s!\n", a->name, b->name);
			return 1;
		}
	}
	DEBUGP("%s does not use %s!\n", a->name, b->name);
	return 0;
}

/* Module a uses b */
int use_module(struct module *a, struct module *b)
{
	struct module_use *use;
	int no_warn, err;

	if (b == NULL || already_uses(a, b)) return 1;

	/* If we're interrupted or time out, we fail. */
	if (wait_event_interruptible_timeout(
		    module_wq, (err = strong_try_module_get(b)) != -EBUSY,
		    30 * HZ) <= 0) {
		printk("%s: gave up waiting for init of module %s.\n",
		       a->name, b->name);
		return 0;
	}

	/* If strong_try_module_get() returned a different error, we fail. */
	if (err)
		return 0;

	DEBUGP("Allocating new usage for %s.\n", a->name);
	use = kmalloc(sizeof(*use), GFP_ATOMIC);
	if (!use) {
		printk("%s: out of memory loading\n", a->name);
		module_put(b);
		return 0;
	}

	use->module_which_uses = a;
	list_add(&use->list, &b->modules_which_use_me);
	no_warn = sysfs_create_link(b->holders_dir, &a->mkobj.kobj, a->name);
	return 1;
}
EXPORT_SYMBOL_GPL(use_module);

/* Clear the unload stuff of the module. */
static void module_unload_free(struct module *mod)
{
	struct module *i;

	list_for_each_entry(i, &modules, list) {
		struct module_use *use;

		list_for_each_entry(use, &i->modules_which_use_me, list) {
			if (use->module_which_uses == mod) {
				DEBUGP("%s unusing %s\n", mod->name, i->name);
				module_put(i);
				list_del(&use->list);
				kfree(use);
				sysfs_remove_link(i->holders_dir, mod->name);
				/* There can be at most one match. */
				break;
			}
		}
	}
}

#ifdef CONFIG_MODULE_FORCE_UNLOAD
static inline int try_force_unload(unsigned int flags)
{
	int ret = (flags & O_TRUNC);
	if (ret)
		add_taint(TAINT_FORCED_RMMOD);
	return ret;
}
#else
static inline int try_force_unload(unsigned int flags)
{
	return 0;
}
#endif /* CONFIG_MODULE_FORCE_UNLOAD */

struct stopref
{
	struct module *mod;
	int flags;
	int *forced;
};

/* Whole machine is stopped with interrupts off when this runs. */
static int __try_stop_module(void *_sref)
{
	struct stopref *sref = _sref;

	/* If it's not unused, quit unless we're forcing. */
	if (module_refcount(sref->mod) != 0) {
		if (!(*sref->forced = try_force_unload(sref->flags)))
			return -EWOULDBLOCK;
	}

	/* Mark it as dying. */
	sref->mod->state = MODULE_STATE_GOING;
	return 0;
}

static int try_stop_module(struct module *mod, int flags, int *forced)
{
	if (flags & O_NONBLOCK) {
		struct stopref sref = { mod, flags, forced };

		return stop_machine(__try_stop_module, &sref, NULL);
	} else {
		/* We don't need to stop the machine for this. */
		mod->state = MODULE_STATE_GOING;
		synchronize_sched();
		return 0;
	}
}

unsigned int module_refcount(struct module *mod)
{
	unsigned int total = 0;
	int cpu;

	for_each_possible_cpu(cpu)
		total += local_read(__module_ref_addr(mod, cpu));
	return total;
}
EXPORT_SYMBOL(module_refcount);

/* This exists whether we can unload or not */
static void free_module(struct module *mod);

static void wait_for_zero_refcount(struct module *mod)
{
	/* Since we might sleep for some time, release the mutex first */
	mutex_unlock(&module_mutex);
	for (;;) {
		DEBUGP("Looking at refcount...\n");
		set_current_state(TASK_UNINTERRUPTIBLE);
		if (module_refcount(mod) == 0)
			break;
		schedule();
	}
	current->state = TASK_RUNNING;
	mutex_lock(&module_mutex);
}

SYSCALL_DEFINE2(delete_module, const char __user *, name_user,
		unsigned int, flags)
{
	struct module *mod;
	char name[MODULE_NAME_LEN];
	int ret, forced = 0;

	if (!capable(CAP_SYS_MODULE) || modules_disabled)
		return -EPERM;

	if (strncpy_from_user(name, name_user, MODULE_NAME_LEN-1) < 0)
		return -EFAULT;
	name[MODULE_NAME_LEN-1] = '\0';

	/* Create stop_machine threads since free_module relies on
	 * a non-failing stop_machine call. */
	ret = stop_machine_create();
	if (ret)
		return ret;

	if (mutex_lock_interruptible(&module_mutex) != 0) {
		ret = -EINTR;
		goto out_stop;
	}

	mod = find_module(name);//根据模块名查找模块结构体
	if (!mod) {
		ret = -ENOENT;
		goto out;
	}

	/*
	*检查驱动是否可以卸载
	*/
	//其他模块依赖此模块，不删除
	if (!list_empty(&mod->modules_which_use_me)) {
		/* Other modules depend on us: get rid of them first. */
		ret = -EWOULDBLOCK;
		goto out;
	}

	//如果模块不是LIVE状态，那么就无法进行下去了，得到的结果是busy
	/* Doing init or already dying? */
	if (mod->state != MODULE_STATE_LIVE) {
		/* FIXME: if (force), slam module count and wake up
                   waiter --RR */
		DEBUGP("%s already dying\n", mod->name);
		ret = -EBUSY;
		goto out;
	}

	//  驱动模块必须有exit函数来退出
    //  如果没有则无法退出, 提示驱动busy
	/* If it has an init func, it must have an exit func to unload */
	if (mod->init && !mod->exit) 
	{
		forced = try_force_unload(flags);
		if (!forced) {
			/* This module can't be removed */
			ret = -EBUSY;
			goto out;
		}
	}

	/* Set this up before setting mod->state */
	mod->waiter = current;

	//  确认模块正在运行(是否有设备在使用当前驱动模块)
    //  否则内核无法停止一个正在使用的内核模块
	/* Stop the machine so refcounts can't move and disable module. */
	ret = try_stop_module(mod, flags, &forced);
	if (ret != 0)
		goto out;

	/* Never wait if forced. */
	if (!forced && module_refcount(mod) != 0)//如果引用计数不是0，则等待其为0
		wait_for_zero_refcount(mod);

	mutex_unlock(&module_mutex);
	/* Final destruction now noone is using it. */
	if (mod->exit != NULL)
		mod->exit();
	blocking_notifier_call_chain(&module_notify_list,
				     MODULE_STATE_GOING, mod);
	async_synchronize_full();
	mutex_lock(&module_mutex);
	/* Store the name of the last unloaded module for diagnostic purposes */
	strlcpy(last_unloaded_module, mod->name, sizeof(last_unloaded_module));
	free_module(mod);

 out:
	mutex_unlock(&module_mutex);
out_stop:
	stop_machine_destroy();
	return ret;
}

static inline void print_unload_info(struct seq_file *m, struct module *mod)
{
	struct module_use *use;
	int printed_something = 0;

	seq_printf(m, " %u ", module_refcount(mod));

	/* Always include a trailing , so userspace can differentiate
           between this and the old multi-field proc format. */

    //打印处所有依赖我的模块名
	list_for_each_entry(use, &mod->modules_which_use_me, list) 
    {
		printed_something = 1;
		seq_printf(m, "%s,", use->module_which_uses->name);
	}

	
	if (mod->init != NULL && mod->exit == NULL) 
	{
		printed_something = 1;
		seq_printf(m, "[permanent],");
	}

	if (!printed_something)
		seq_printf(m, "-");
}

void __symbol_put(const char *symbol)
{
	struct module *owner;

	preempt_disable();
	if (!find_symbol(symbol, &owner, NULL, true, false))
		BUG();
	module_put(owner);
	preempt_enable();
}
EXPORT_SYMBOL(__symbol_put);

/* Note this assumes addr is a function, which it currently always is. */
void symbol_put_addr(void *addr)
{
	struct module *modaddr;
	unsigned long a = (unsigned long)dereference_function_descriptor(addr);

	if (core_kernel_text(a))
		return;

	/* module_text_address is safe here: we're supposed to have reference
	 * to module from symbol_get, so it can't go away. */
	modaddr = __module_text_address(a);
	BUG_ON(!modaddr);
	module_put(modaddr);
}
EXPORT_SYMBOL_GPL(symbol_put_addr);

static ssize_t show_refcnt(struct module_attribute *mattr,
			   struct module *mod, char *buffer)
{
	return sprintf(buffer, "%u\n", module_refcount(mod));
}

static struct module_attribute refcnt = {
	.attr = { .name = "refcnt", .mode = 0444 },
	.show = show_refcnt,
};

void module_put(struct module *module)
{
	if (module) {
		unsigned int cpu = get_cpu();
		local_dec(__module_ref_addr(module, cpu));
		trace_module_put(module, _RET_IP_,
				 local_read(__module_ref_addr(module, cpu)));
		/* Maybe they're waiting for us to drop reference? */
		if (unlikely(!module_is_live(module)))
			wake_up_process(module->waiter);
		put_cpu();
	}
}
EXPORT_SYMBOL(module_put);

#else /* !CONFIG_MODULE_UNLOAD */
static inline void print_unload_info(struct seq_file *m, struct module *mod)
{
	/* We don't know the usage count, or what modules are using. */
	seq_printf(m, " - -");
}

static inline void module_unload_free(struct module *mod)
{
}

int use_module(struct module *a, struct module *b)
{
	return strong_try_module_get(b) == 0;
}
EXPORT_SYMBOL_GPL(use_module);

static inline void module_unload_init(struct module *mod)
{
}
#endif /* CONFIG_MODULE_UNLOAD */

static ssize_t show_initstate(struct module_attribute *mattr,
			   struct module *mod, char *buffer)
{
	const char *state = "unknown";

	switch (mod->state) {
	case MODULE_STATE_LIVE:
		state = "live";
		break;
	case MODULE_STATE_COMING:
		state = "coming";
		break;
	case MODULE_STATE_GOING:
		state = "going";
		break;
	}
	return sprintf(buffer, "%s\n", state);
}

static struct module_attribute initstate = {
	.attr = { .name = "initstate", .mode = 0444 },
	.show = show_initstate,
};

static struct module_attribute *modinfo_attrs[] = {
	&modinfo_version,
	&modinfo_srcversion,
	&initstate,
#ifdef CONFIG_MODULE_UNLOAD
	&refcnt,
#endif
	NULL,
};

static const char vermagic[] = VERMAGIC_STRING;

/*尝试强制加载模块 */
static int try_to_force_load(struct module *mod, const char *reason)
{
#ifdef CONFIG_MODULE_FORCE_LOAD
	if (!test_taint(TAINT_FORCED_MODULE))
		printk(KERN_WARNING "%s: %s: kernel tainted.\n",
		       mod->name, reason);
	add_taint_module(mod, TAINT_FORCED_MODULE);
	return 0;
#else
	return -ENOEXEC;
#endif
}

#ifdef CONFIG_MODVERSIONS
/* If the arch applies (non-zero) relocations to kernel kcrctab, unapply it. */
static unsigned long maybe_relocated(unsigned long crc,
				     const struct module *crc_owner)
{
#ifdef ARCH_RELOCATES_KCRCTAB
	if (crc_owner == NULL)
		return crc - (unsigned long)reloc_start;
#endif
	return crc;
}

static int check_version(Elf_Shdr *sechdrs,
			 unsigned int versindex,
			 const char *symname,
			 struct module *mod, 
			 const unsigned long *crc,
			 const struct module *crc_owner)
{
	unsigned int i, num_versions;
	struct modversion_info *versions;

	/* Exporting module didn't supply crcs?  OK, we're already tainted. */
	if (!crc)
		return 1;

	/* No versions at all?  modprobe --force does this. */
	if (versindex == 0)
		return try_to_force_load(mod, symname) == 0;

	versions = (void *) sechdrs[versindex].sh_addr;
	num_versions = sechdrs[versindex].sh_size / sizeof(struct modversion_info);

	for (i = 0; i < num_versions; i++) {
		if (strcmp(versions[i].name, symname) != 0)
			continue;

		if (versions[i].crc == maybe_relocated(*crc, crc_owner))
			return 1;
		DEBUGP("Found checksum %lX vs module %lX\n",
		       maybe_relocated(*crc, crc_owner), versions[i].crc);
		goto bad_version;
	}

	printk(KERN_WARNING "%s: no symbol version for %s\n",
	       mod->name, symname);
	return 0;

bad_version:
	printk("%s: disagrees about version of symbol %s\n",
	       mod->name, symname);
	return 0;
}

static inline int check_modstruct_version(Elf_Shdr *sechdrs,
					  unsigned int versindex,
					  struct module *mod)
{
	const unsigned long *crc;

	if (!find_symbol(MODULE_SYMBOL_PREFIX "module_layout", NULL,
			 &crc, true, false))
		BUG();
	return check_version(sechdrs, versindex, "module_layout", mod, crc,
			     NULL);
}

/* First part is kernel version, which we ignore if module has crcs. */
static inline int same_magic(const char *amagic, const char *bmagic,
			     bool has_crcs)
{
	if (has_crcs) {
		amagic += strcspn(amagic, " ");
		bmagic += strcspn(bmagic, " ");
	}
	return strcmp(amagic, bmagic) == 0;
}
#else
static inline int check_version(Elf_Shdr *sechdrs,
				unsigned int versindex,
				const char *symname,
				struct module *mod, 
				const unsigned long *crc,
				const struct module *crc_owner)
{
	return 1;
}

static inline int check_modstruct_version(Elf_Shdr *sechdrs,
					  unsigned int versindex,
					  struct module *mod)
{
	return 1;
}

static inline int same_magic(const char *amagic, const char *bmagic,
			     bool has_crcs)
{
	return strcmp(amagic, bmagic) == 0;
}
#endif /* CONFIG_MODVERSIONS */

/* Resolve a symbol for this module.  I.e. if we find one, record usage.
   Must be holding module_mutex. */
static const struct kernel_symbol *resolve_symbol(Elf_Shdr *sechdrs,
						  unsigned int versindex,
						  const char *name,
						  struct module *mod)
{
	struct module *owner;
	const struct kernel_symbol *sym;
	const unsigned long *crc;

	 ////返回符号对应的符号表，所属模块，crc
	sym = find_symbol(name, &owner, &crc,
			  !(mod->taints & (1 << TAINT_PROPRIETARY_MODULE)), true);
	/* use_module can fail due to OOM,
	   or module initialization or unloading */
	if (sym) 
	{   
	 //检测内核中当前符号的crc和新模块中的记录的crc是否相符
		if (!check_version(sechdrs, versindex, name, mod, crc, owner)
		    || !use_module(mod, owner))
			sym = NULL;
	}
	return sym;
}

/*
 * /sys/module/foo/sections stuff
 * J. Corbet <corbet@lwn.net>
 */
#if defined(CONFIG_KALLSYMS) && defined(CONFIG_SYSFS)

static inline bool sect_empty(const Elf_Shdr *sect)
{
	return !(sect->sh_flags & SHF_ALLOC) || sect->sh_size == 0;
}

struct module_sect_attr
{
	struct module_attribute mattr;
	char *name;
	unsigned long address;
};

struct module_sect_attrs
{
	struct attribute_group grp;
	unsigned int nsections;
	struct module_sect_attr attrs[0];
};

static ssize_t module_sect_show(struct module_attribute *mattr,
				struct module *mod, char *buf)
{
	struct module_sect_attr *sattr =
		container_of(mattr, struct module_sect_attr, mattr);
	return sprintf(buf, "0x%lx\n", sattr->address);
}

static void free_sect_attrs(struct module_sect_attrs *sect_attrs)
{
	unsigned int section;

	for (section = 0; section < sect_attrs->nsections; section++)
		kfree(sect_attrs->attrs[section].name);
	kfree(sect_attrs);
}

static void add_sect_attrs(struct module *mod, unsigned int nsect,
		char *secstrings, Elf_Shdr *sechdrs)
{
	unsigned int nloaded = 0, i, size[2];
	struct module_sect_attrs *sect_attrs;
	struct module_sect_attr *sattr;
	struct attribute **gattr;

	/* Count loaded sections and allocate structures */
	for (i = 0; i < nsect; i++)
		if (!sect_empty(&sechdrs[i]))
			nloaded++;
	size[0] = ALIGN(sizeof(*sect_attrs)
			+ nloaded * sizeof(sect_attrs->attrs[0]),
			sizeof(sect_attrs->grp.attrs[0]));
	size[1] = (nloaded + 1) * sizeof(sect_attrs->grp.attrs[0]);
	sect_attrs = kzalloc(size[0] + size[1], GFP_KERNEL);
	if (sect_attrs == NULL)
		return;

	/* Setup section attributes. */
	sect_attrs->grp.name = "sections";
	sect_attrs->grp.attrs = (void *)sect_attrs + size[0];

	sect_attrs->nsections = 0;
	sattr = &sect_attrs->attrs[0];
	gattr = &sect_attrs->grp.attrs[0];
	for (i = 0; i < nsect; i++) {
		if (sect_empty(&sechdrs[i]))
			continue;
		sattr->address = sechdrs[i].sh_addr;
		sattr->name = kstrdup(secstrings + sechdrs[i].sh_name,
					GFP_KERNEL);
		if (sattr->name == NULL)
			goto out;
		sect_attrs->nsections++;
		sattr->mattr.show = module_sect_show;
		sattr->mattr.store = NULL;
		sattr->mattr.attr.name = sattr->name;
		sattr->mattr.attr.mode = S_IRUGO;
		*(gattr++) = &(sattr++)->mattr.attr;
	}
	*gattr = NULL;

	if (sysfs_create_group(&mod->mkobj.kobj, &sect_attrs->grp))
		goto out;

	mod->sect_attrs = sect_attrs;
	return;
  out:
	free_sect_attrs(sect_attrs);
}

static void remove_sect_attrs(struct module *mod)
{
	if (mod->sect_attrs) {
		sysfs_remove_group(&mod->mkobj.kobj,
				   &mod->sect_attrs->grp);
		/* We are positive that no one is using any sect attrs
		 * at this point.  Deallocate immediately. */
		free_sect_attrs(mod->sect_attrs);
		mod->sect_attrs = NULL;
	}
}

/*
 * /sys/module/foo/notes/.section.name gives contents of SHT_NOTE sections.
 */

struct module_notes_attrs {
	struct kobject *dir;
	unsigned int notes;
	struct bin_attribute attrs[0];
};

static ssize_t module_notes_read(struct kobject *kobj,
				 struct bin_attribute *bin_attr,
				 char *buf, loff_t pos, size_t count)
{
	/*
	 * The caller checked the pos and count against our size.
	 */
	memcpy(buf, bin_attr->private + pos, count);
	return count;
}

static void free_notes_attrs(struct module_notes_attrs *notes_attrs,
			     unsigned int i)
{
	if (notes_attrs->dir) {
		while (i-- > 0)
			sysfs_remove_bin_file(notes_attrs->dir,
					      &notes_attrs->attrs[i]);
		kobject_put(notes_attrs->dir);
	}
	kfree(notes_attrs);
}

static void add_notes_attrs(struct module *mod, unsigned int nsect,
			    char *secstrings, Elf_Shdr *sechdrs)
{
	unsigned int notes, loaded, i;
	struct module_notes_attrs *notes_attrs;
	struct bin_attribute *nattr;

	/* failed to create section attributes, so can't create notes */
	if (!mod->sect_attrs)
		return;

	/* Count notes sections and allocate structures.  */
	notes = 0;
	for (i = 0; i < nsect; i++)
		if (!sect_empty(&sechdrs[i]) &&
		    (sechdrs[i].sh_type == SHT_NOTE))
			++notes;

	if (notes == 0)
		return;

	notes_attrs = kzalloc(sizeof(*notes_attrs)
			      + notes * sizeof(notes_attrs->attrs[0]),
			      GFP_KERNEL);
	if (notes_attrs == NULL)
		return;

	notes_attrs->notes = notes;
	nattr = &notes_attrs->attrs[0];
	for (loaded = i = 0; i < nsect; ++i) {
		if (sect_empty(&sechdrs[i]))
			continue;
		if (sechdrs[i].sh_type == SHT_NOTE) {
			nattr->attr.name = mod->sect_attrs->attrs[loaded].name;
			nattr->attr.mode = S_IRUGO;
			nattr->size = sechdrs[i].sh_size;
			nattr->private = (void *) sechdrs[i].sh_addr;
			nattr->read = module_notes_read;
			++nattr;
		}
		++loaded;
	}

	notes_attrs->dir = kobject_create_and_add("notes", &mod->mkobj.kobj);
	if (!notes_attrs->dir)
		goto out;

	for (i = 0; i < notes; ++i)
		if (sysfs_create_bin_file(notes_attrs->dir,
					  &notes_attrs->attrs[i]))
			goto out;

	mod->notes_attrs = notes_attrs;
	return;

  out:
	free_notes_attrs(notes_attrs, i);
}

static void remove_notes_attrs(struct module *mod)
{
	if (mod->notes_attrs)
		free_notes_attrs(mod->notes_attrs, mod->notes_attrs->notes);
}

#else

static inline void add_sect_attrs(struct module *mod, unsigned int nsect,
		char *sectstrings, Elf_Shdr *sechdrs)
{
}

static inline void remove_sect_attrs(struct module *mod)
{
}

static inline void add_notes_attrs(struct module *mod, unsigned int nsect,
				   char *sectstrings, Elf_Shdr *sechdrs)
{
}

static inline void remove_notes_attrs(struct module *mod)
{
}
#endif

#ifdef CONFIG_SYSFS
int module_add_modinfo_attrs(struct module *mod)
{
	struct module_attribute *attr;
	struct module_attribute *temp_attr;
	int error = 0;
	int i;

	mod->modinfo_attrs = kzalloc((sizeof(struct module_attribute) *
					(ARRAY_SIZE(modinfo_attrs) + 1)),
					GFP_KERNEL);
	if (!mod->modinfo_attrs)
		return -ENOMEM;

	temp_attr = mod->modinfo_attrs;
	for (i = 0; (attr = modinfo_attrs[i]) && !error; i++) {
		if (!attr->test ||
		    (attr->test && attr->test(mod))) {
			memcpy(temp_attr, attr, sizeof(*temp_attr));
			error = sysfs_create_file(&mod->mkobj.kobj,&temp_attr->attr);
			++temp_attr;
		}
	}
	return error;
}

void module_remove_modinfo_attrs(struct module *mod)
{
	struct module_attribute *attr;
	int i;

	for (i = 0; (attr = &mod->modinfo_attrs[i]); i++) {
		/* pick a field to test for end of list */
		if (!attr->attr.name)
			break;
		sysfs_remove_file(&mod->mkobj.kobj,&attr->attr);
		if (attr->free)
			attr->free(mod);
	}
	kfree(mod->modinfo_attrs);
}

int mod_sysfs_init(struct module *mod)
{
	int err;
	struct kobject *kobj;

	if (!module_sysfs_initialized) {
		printk(KERN_ERR "%s: module sysfs not initialized\n",
		       mod->name);
		err = -EINVAL;
		goto out;
	}

	kobj = kset_find_obj(module_kset, mod->name);
	if (kobj) 
	{
		printk(KERN_ERR "%s: module is already loaded\n", mod->name);
		kobject_put(kobj);
		err = -EINVAL;
		goto out;
	}

	mod->mkobj.mod = mod;

	memset(&mod->mkobj.kobj, 0, sizeof(mod->mkobj.kobj));
	mod->mkobj.kobj.kset = module_kset;
	err = kobject_init_and_add(&mod->mkobj.kobj, &module_ktype, NULL,
				   "%s", mod->name);
	if (err)
		kobject_put(&mod->mkobj.kobj);

	/* delay uevent until full sysfs population */
out:
	return err;
}

int mod_sysfs_setup(struct module *mod,
			   struct kernel_param *kparam,
			   unsigned int num_params)
{
	int err;

	mod->holders_dir = kobject_create_and_add("holders", &mod->mkobj.kobj);
	if (!mod->holders_dir) {
		err = -ENOMEM;
		goto out_unreg;
	}

	err = module_param_sysfs_setup(mod, kparam, num_params);
	if (err)
		goto out_unreg_holders;

	err = module_add_modinfo_attrs(mod);
	if (err)
		goto out_unreg_param;

	kobject_uevent(&mod->mkobj.kobj, KOBJ_ADD);
	return 0;

out_unreg_param:
	module_param_sysfs_remove(mod);
out_unreg_holders:
	kobject_put(mod->holders_dir);
out_unreg:
	kobject_put(&mod->mkobj.kobj);
	return err;
}

static void mod_sysfs_fini(struct module *mod)
{
	kobject_put(&mod->mkobj.kobj);
}

#else /* CONFIG_SYSFS */

static void mod_sysfs_fini(struct module *mod)
{
}

#endif /* CONFIG_SYSFS */

static void mod_kobject_remove(struct module *mod)
{
	module_remove_modinfo_attrs(mod);
	module_param_sysfs_remove(mod);
	kobject_put(mod->mkobj.drivers_dir);
	kobject_put(mod->holders_dir);
	mod_sysfs_fini(mod);
}

/*
 * unlink the module with the whole machine is stopped with interrupts off
 * - this defends against kallsyms not taking locks
 */
static int __unlink_module(void *_mod)
{
	struct module *mod = _mod;
	list_del(&mod->list);
	return 0;
}

/* Free a module, remove from lists, etc (must hold module_mutex). */
static void free_module(struct module *mod)
{
	trace_module_free(mod);

	/* Delete from various lists */
	stop_machine(__unlink_module, mod, NULL);
	remove_notes_attrs(mod);
	remove_sect_attrs(mod);
	mod_kobject_remove(mod);

	/* Remove dynamic debug info */
	ddebug_remove_module(mod->name);

	/* Arch-specific cleanup. */
	module_arch_cleanup(mod);

	/* Module unload stuff */
	module_unload_free(mod);

	/* Free any allocated parameters. */
	destroy_params(mod->kp, mod->num_kp);

	/* This may be NULL, but that's OK */
	module_free(mod, mod->module_init);
	kfree(mod->args);
	if (mod->percpu)
		percpu_modfree(mod->percpu);
#if defined(CONFIG_MODULE_UNLOAD) && defined(CONFIG_SMP)
	if (mod->refptr)
		percpu_modfree(mod->refptr);
#endif
	/* Free lock-classes: */
	lockdep_free_key_range(mod->module_core, mod->core_size);

	/* Finally, free the core (containing the module structure) */
	module_free(mod, mod->module_core);

#ifdef CONFIG_MPU
	update_protections(current->mm);
#endif
}

void *__symbol_get(const char *symbol)
{
	struct module *owner;
	const struct kernel_symbol *sym;

	preempt_disable();
	sym = find_symbol(symbol, &owner, NULL, true, true);
	if (sym && strong_try_module_get(owner))
		sym = NULL;
	preempt_enable();

	return sym ? (void *)sym->value : NULL;
}
EXPORT_SYMBOL_GPL(__symbol_get);

/*
 * Ensure that an exported symbol [global namespace] does not already exist
 * in the kernel or in some other module's exported symbol table.
 */
static int verify_export_symbols(struct module *mod)
{
	unsigned int i;
	struct module *owner;
	const struct kernel_symbol *s;
	struct {
		const struct kernel_symbol *sym;
		unsigned int num;
	} arr[] = {
		{ mod->syms, mod->num_syms },
		{ mod->gpl_syms, mod->num_gpl_syms },
		{ mod->gpl_future_syms, mod->num_gpl_future_syms },
#ifdef CONFIG_UNUSED_SYMBOLS
		{ mod->unused_syms, mod->num_unused_syms },
		{ mod->unused_gpl_syms, mod->num_unused_gpl_syms },
#endif
	};

	for (i = 0; i < ARRAY_SIZE(arr); i++) {
		for (s = arr[i].sym; s < arr[i].sym + arr[i].num; s++) {
			if (find_symbol(s->name, &owner, NULL, true, false)) {
				printk(KERN_ERR
				       "%s: exports duplicate symbol %s"
				       " (owned by %s)\n",
				       mod->name, s->name, module_name(owner));
				return -ENOEXEC;
			}
		}
	}
	return 0;
}

/* Change all symbols so that st_value encodes the pointer directly. */
static int simplify_symbols(Elf_Shdr *sechdrs,
			                     unsigned int symindex,
			                     const char *strtab,
			                     unsigned int versindex,
			                     unsigned int pcpuindex,
			                     struct module *mod)
{
	Elf_Sym *sym = (void *)sechdrs[symindex].sh_addr;
	unsigned long secbase;
	
	unsigned int i, n = sechdrs[symindex].sh_size / sizeof(Elf_Sym);
	int ret = 0;
	const struct kernel_symbol *ksym;

	//循环所有符号 
	for (i = 1; i < n; i++) 
	{
		switch (sym[i].st_shndx) 
		{
		case SHN_COMMON:
			  //一般来说，未初始化的全局符号是这种类型，在ko文件中
            //不应该出现这种类型的符号,出现则直接返回错误了 
			/* We compiled with -fno-common.  These are not
			   supposed to happen.  */
			DEBUGP("Common symbol: %s\n", strtab + sym[i].st_name);
			printk("%s: please compile with -fno-common\n",
			       mod->name);
			ret = -ENOEXEC;
			break;

		case SHN_ABS:
			//包含一个绝对值，不受重定位的影响，可能是节索引编号
			/* Don't need to do anything */
			DEBUGP("Absolute symbol: 0x%08lx\n",
			       (long)sym[i].st_value);
			break;

		case SHN_UNDEF:
			 //这是一个未定义符号，需要内核来找这个符号的定义
            //内核会查找自身+所有模块，如果找到，返回ksym，
            //指向这个符号。
			ksym = resolve_symbol(sechdrs, versindex,
					      strtab + sym[i].st_name, mod);
			/* Ok if resolved.  */
			if (ksym) 
			{
			     //符号表的这个st_value，指向这个符号的内存地址
				sym[i].st_value = ksym->value;
				break;
			}

			/* Ok if weak.  */
			if (ELF_ST_BIND(sym[i].st_info) == STB_WEAK)
				break;

			printk(KERN_WARNING "%s: Unknown symbol %s\n",
			       mod->name, strtab + sym[i].st_name);
			ret = -ENOENT;
			break;

		default:
			/* Divert to percpu allocation if a percpu var. */
		    //其他的st_shndex，都认为是指向节区索引，则找到节区起始地址（内存映像中的）
			if (sym[i].st_shndx == pcpuindex)
				secbase = (unsigned long)mod->percpu;
			else
				secbase = sechdrs[sym[i].st_shndx].sh_addr;
			  //st_value是偏移，这里加上节区起始地址（内存映像中的），为符号真正的地址。
			sym[i].st_value += secbase;
			break;
		}
	}

	return ret;
}

/* Additional bytes needed by arch in front of individual sections */
unsigned int __weak arch_mod_section_prepend(struct module *mod,
					     unsigned int section)
{
	/* default implementation just returns zero */
	return 0;
}

/* Update size with this section: return offset. */
static long get_offset(struct module *mod, unsigned int *size,
		       Elf_Shdr *sechdr, unsigned int section)
{
	long ret;

	*size += arch_mod_section_prepend(mod, section);
	ret = ALIGN(*size, sechdr->sh_addralign ?: 1);
	*size = ret + sechdr->sh_size;
	return ret;
}

/* Lay out the SHF_ALLOC sections in a way not dissimilar to how ld
   might -- code, read-only data, read-write data, small data.  Tally
   sizes, and place the offsets into sh_entsize fields: high bit means it
   belongs in init. */
static void layout_sections(struct module *mod,
			                     const Elf_Ehdr *hdr,
			                     Elf_Shdr *sechdrs,
			                     const char *secstrings)
{
     //这个数组中的所有项，都有SHF_ALLOC属性，但只有第一行这种是可执行的
	static unsigned long const masks[][2] = 
	{
		/* NOTE: all executable code must be the first section
		 * in this array; otherwise modify the text_size
		 * finder in the two loops below */
		{ SHF_EXECINSTR | SHF_ALLOC,  ARCH_SHF_SMALL },
		{ SHF_ALLOC                ,  SHF_WRITE | ARCH_SHF_SMALL },
		{ SHF_WRITE | SHF_ALLOC,      ARCH_SHF_SMALL },
		{ ARCH_SHF_SMALL | SHF_ALLOC, 0 }
	};
	unsigned int m, i;

     //e_shnum为节区头部表中的表项数
	for (i = 0; i < hdr->e_shnum; i++)
 		sechdrs[i].sh_entsize = ~0UL; //将每个节区的元素长度设为0xffff 后面会重新设置

	DEBUGP("Core section allocation order:\n");

	//对masks数组中的每种节属性组合，执行以下操作
	for (m = 0; m < ARRAY_SIZE(masks); ++m) 
	{
		for (i = 0; i < hdr->e_shnum; ++i) 
		{
		    //获取第i个节表的头部表
			Elf_Shdr *s = &sechdrs[i];

			if ((s->sh_flags & masks[m][0]) != masks[m][0] //如果节区与masks掩码不匹配
			    || (s->sh_flags & masks[m][1]) //节区flag不匹配
			    || s->sh_entsize != ~0UL       //或者节区元素长度不匹配
			    || strstarts(secstrings + s->sh_name, ".init")) //者是初始化段
				continue;
			
			//重新得到表项的大小
			//这里剩下的主要是有SHF_ALLOC的非初始化段了，这样的段最终会被放到module.module_core,也就是内存映像中去，这里重用s->sh_entsize，来记录当前段在内存映像中的偏移。core_size为最终内存映像的大小，get_offset的时候会加上当前这个节的大小
			s->sh_entsize = get_offset(mod, &mod->core_size, s, i); 
			DEBUGP("\t%s\n", secstrings + s->sh_name);
		}
		if (m == 0)
			mod->core_text_size = mod->core_size;
	}

	DEBUGP("Init section allocation order:\n");

	for (m = 0; m < ARRAY_SIZE(masks); ++m) 
	{
		for (i = 0; i < hdr->e_shnum; ++i) {
			Elf_Shdr *s = &sechdrs[i];

			if ((s->sh_flags & masks[m][0]) != masks[m][0]
			    || (s->sh_flags & masks[m][1])
			    || s->sh_entsize != ~0UL   //上面设置过属于init的在此处就会continue  不会执行下面的
			    || !strstarts(secstrings + s->sh_name, ".init")) //寻找.init中开头的节
				continue;
			s->sh_entsize = (get_offset(mod, &mod->init_size, s, i) | INIT_OFFSET_MASK);
			DEBUGP("\t%s\n", secstrings + s->sh_name);
		}
		if (m == 0)
			mod->init_text_size = mod->init_size;
	}
}

static void set_license(struct module *mod, const char *license)
{
	if (!license)
		license = "unspecified";
	
    //比较gpl  如果不在这预定义的几个值之内 则打印警告污染内核，并设置内核标志TAINT_PROPRIETARY_MODULE
	if (!license_is_gpl_compatible(license)) 
	{
		if (!test_taint(TAINT_PROPRIETARY_MODULE))
			printk(KERN_WARNING "%s: module license '%s' taints ""kernel.\n", mod->name, license);
		add_taint_module(mod, TAINT_PROPRIETARY_MODULE);
	}
}

/* Parse tag=value strings from .modinfo section 
从.modinfo节中解析 \0分隔的key=value的类型
*/
static char *next_string(char *string, unsigned long *secsize)
{
	/* Skip non-zero chars */
   
	while (string[0]) 
	{
		string++;
		if ((*secsize)-- <= 1)
			return NULL;
	}

	/* Skip any zero padding. */
	while (!string[0]) {
		string++;
		if ((*secsize)-- <= 1)
			return NULL;
	}
	return string;
}

static char *get_modinfo(Elf_Shdr *sechdrs,  //节表头地址
			                unsigned int info,  //节的索引 
			                const char *tag)    //查找的标记
{
	char *p;
	unsigned int taglen = strlen(tag); //查找符号的长度
	unsigned long size = sechdrs[info].sh_size; //下标为info 节的大小
	                                            //
	//.modinfo 节的内容为 key=value格式
	//在.modinfo节的起始处 查找key是tag的值
	for (p = (char *)sechdrs[info].sh_addr; p; p = next_string(p, &size)) 
	{
		if (strncmp(p, tag, taglen) == 0 && p[taglen] == '=')
			return p + taglen + 1;
	}
	
	//没有找到
	return NULL;
}

static void setup_modinfo(struct module *mod, Elf_Shdr *sechdrs,
			  unsigned int infoindex)
{
	struct module_attribute *attr;
	int i;

	for (i = 0; (attr = modinfo_attrs[i]); i++) {
		if (attr->setup)
			attr->setup(mod,
				    get_modinfo(sechdrs,
						infoindex,
						attr->attr.name));
	}
}

static void free_modinfo(struct module *mod)
{
	struct module_attribute *attr;
	int i;

	for (i = 0; (attr = modinfo_attrs[i]); i++) {
		if (attr->free)
			attr->free(mod);
	}
}

#ifdef CONFIG_KALLSYMS

/* lookup symbol in given range of kernel_symbols */
/*在给定的一个地址空间中查找符号*/
static const struct kernel_symbol *lookup_symbol(const char *name,   //符号名
	                                                 const struct kernel_symbol *start, //查找的起始地址
	                                                 const struct kernel_symbol *stop)  //查找的结束地址
{
	const struct kernel_symbol *ks = start;
    //每个符号是以kernel_symbol结构保存的
	for (; ks < stop; ks++)
		if (strcmp(ks->name, name) == 0)//比较符号名是否相等
			return ks;
		
	return NULL;
}

/*
判断符号是否导出的
*/
static int is_exported(const char *name, unsigned long value,
		       const struct module *mod)
{
	const struct kernel_symbol *ks;

    //可见kernel的导出符号保存放在两个地方，如果不是模块的话，保存在__start___ksymtab ~ __stop___ksymtab之间
    //如果是模块的化  则地址保存在模块的内部,为mod->syms ~ (mod->syms + mod->num_syms) 之间
	if (!mod)
		ks = lookup_symbol(name, __start___ksymtab, __stop___ksymtab);
	else
		ks = lookup_symbol(name, mod->syms, mod->syms + mod->num_syms);


	//看是否在导出的符号中找到了对应的符号和值
	return ks != NULL && ks->value == value;
}

/* As per nm */
static char elf_type(const Elf_Sym *sym,
		     Elf_Shdr *sechdrs,
		     const char *secstrings,
		     struct module *mod)
{
	if (ELF_ST_BIND(sym->st_info) == STB_WEAK) {
		if (ELF_ST_TYPE(sym->st_info) == STT_OBJECT)
			return 'v';
		else
			return 'w';
	}
	if (sym->st_shndx == SHN_UNDEF)
		return 'U';
	if (sym->st_shndx == SHN_ABS)
		return 'a';
	if (sym->st_shndx >= SHN_LORESERVE)
		return '?';
	if (sechdrs[sym->st_shndx].sh_flags & SHF_EXECINSTR)
		return 't';
	if (sechdrs[sym->st_shndx].sh_flags & SHF_ALLOC
	    && sechdrs[sym->st_shndx].sh_type != SHT_NOBITS) {
		if (!(sechdrs[sym->st_shndx].sh_flags & SHF_WRITE))
			return 'r';
		else if (sechdrs[sym->st_shndx].sh_flags & ARCH_SHF_SMALL)
			return 'g';
		else
			return 'd';
	}
	if (sechdrs[sym->st_shndx].sh_type == SHT_NOBITS) {
		if (sechdrs[sym->st_shndx].sh_flags & ARCH_SHF_SMALL)
			return 's';
		else
			return 'b';
	}
	if (strstarts(secstrings + sechdrs[sym->st_shndx].sh_name, ".debug"))
		return 'n';
	return '?';
}

static bool is_core_symbol(const Elf_Sym *src, const Elf_Shdr *sechdrs,
                           unsigned int shnum)
{
	const Elf_Shdr *sec;

	if (src->st_shndx == SHN_UNDEF
	    || src->st_shndx >= shnum
	    || !src->st_name)
		return false;

	sec = sechdrs + src->st_shndx;
	if (!(sec->sh_flags & SHF_ALLOC)
#ifndef CONFIG_KALLSYMS_ALL
	    || !(sec->sh_flags & SHF_EXECINSTR)
#endif
	    || (sec->sh_entsize & INIT_OFFSET_MASK))
		return false;

	return true;
}

static unsigned long layout_symtab(struct module *mod,
				   Elf_Shdr *sechdrs,
				   unsigned int symindex,
				   unsigned int strindex,
				   const Elf_Ehdr *hdr,
				   const char *secstrings,
				   unsigned long *pstroffs,
				   unsigned long *strmap)
{
	unsigned long symoffs;
	Elf_Shdr *symsect = sechdrs + symindex;
	Elf_Shdr *strsect = sechdrs + strindex;
	const Elf_Sym *src;
	const char *strtab;
	unsigned int i, nsrc, ndst;

	/* Put symbol section at end of init part of module. */
	//给符号表加上SHF_ALLOC属性
	symsect->sh_flags |= SHF_ALLOC;
	
	symsect->sh_entsize = get_offset(mod, &mod->init_size, symsect,symindex) | INIT_OFFSET_MASK;
	DEBUGP("\t%s\n", secstrings + symsect->sh_name);

	src = (void *)hdr + symsect->sh_offset;
	nsrc = symsect->sh_size / sizeof(*src);
	strtab = (void *)hdr + strsect->sh_offset;
	//计算所有核心符号字符串的总大小
	for (ndst = i = 1; i < nsrc; ++i, ++src)
		if (is_core_symbol(src, sechdrs, hdr->e_shnum)) 
		{
			unsigned int j = src->st_name;

			while(!__test_and_set_bit(j, strmap) && strtab[j])
				++j;
			++ndst;
		}

	/* Append room for core symbols at end of core part. */
	symoffs = ALIGN(mod->core_size, symsect->sh_addralign ?: 1);

	 //在core_size的末尾增加这两个表的空间,符号表和其字符串表
	mod->core_size = symoffs + ndst * sizeof(Elf_Sym);

	/* Put string table section at end of init part of module. */
    //给字符串表加上SHF_ALLOC属性，并算到module_init中。
	strsect->sh_flags |= SHF_ALLOC;
	strsect->sh_entsize = get_offset(mod, &mod->init_size, strsect,
					 strindex) | INIT_OFFSET_MASK;
	DEBUGP("\t%s\n", secstrings + strsect->sh_name);

	/* Append room for core symbols' strings at end of core part. */
	*pstroffs = mod->core_size;
	__set_bit(0, strmap);
	mod->core_size += bitmap_weight(strmap, strsect->sh_size);

	return symoffs;
}

static void add_kallsyms(struct module *mod,
			 Elf_Shdr *sechdrs,
			 unsigned int shnum,
			 unsigned int symindex,
			 unsigned int strindex,
			 unsigned long symoffs,
			 unsigned long stroffs,
			 const char *secstrings,
			 unsigned long *strmap)
{
	unsigned int i, ndst;
	const Elf_Sym *src;
	Elf_Sym *dst;
	char *s;

	mod->symtab = (void *)sechdrs[symindex].sh_addr;
	mod->num_symtab = sechdrs[symindex].sh_size / sizeof(Elf_Sym);
	mod->strtab = (void *)sechdrs[strindex].sh_addr;

	/* Set types up while we still have access to sections. */
	for (i = 0; i < mod->num_symtab; i++)
		mod->symtab[i].st_info
			= elf_type(&mod->symtab[i], sechdrs, secstrings, mod);

	mod->core_symtab = dst = mod->module_core + symoffs;
	src = mod->symtab;
	*dst = *src;
	for (ndst = i = 1; i < mod->num_symtab; ++i, ++src) {
		if (!is_core_symbol(src, sechdrs, shnum))
			continue;
		dst[ndst] = *src;
		dst[ndst].st_name = bitmap_weight(strmap, dst[ndst].st_name);
		++ndst;
	}
	mod->core_num_syms = ndst;

	mod->core_strtab = s = mod->module_core + stroffs;
	for (*s = 0, i = 1; i < sechdrs[strindex].sh_size; ++i)
		if (test_bit(i, strmap))
			*++s = mod->strtab[i];
}
#else
static inline unsigned long layout_symtab(struct module *mod,
					  Elf_Shdr *sechdrs,
					  unsigned int symindex,
					  unsigned int strindex,
					  const Elf_Ehdr *hdr,
					  const char *secstrings,
					  unsigned long *pstroffs,
					  unsigned long *strmap)
{
	return 0;
}

static inline void add_kallsyms(struct module *mod,
				Elf_Shdr *sechdrs,
				unsigned int shnum,
				unsigned int symindex,
				unsigned int strindex,
				unsigned long symoffs,
				unsigned long stroffs,
				const char *secstrings,
				const unsigned long *strmap)
{
}
#endif /* CONFIG_KALLSYMS */

static void dynamic_debug_setup(struct _ddebug *debug, unsigned int num)
{
#ifdef CONFIG_DYNAMIC_DEBUG
	if (ddebug_add_module(debug, num, debug->modname))
		printk(KERN_ERR "dynamic debug error adding module: %s\n",
					debug->modname);
#endif
}

static void *module_alloc_update_bounds(unsigned long size)
{
	void *ret = module_alloc(size);

	if (ret) {
		/* Update module bounds. */
		if ((unsigned long)ret < module_addr_min)
			module_addr_min = (unsigned long)ret;
		if ((unsigned long)ret + size > module_addr_max)
			module_addr_max = (unsigned long)ret + size;
	}
	return ret;
}

#ifdef CONFIG_DEBUG_KMEMLEAK
static void kmemleak_load_module(struct module *mod, Elf_Ehdr *hdr,
				 Elf_Shdr *sechdrs, char *secstrings)
{
	unsigned int i;

	/* only scan the sections containing data */
	kmemleak_scan_area(mod->module_core, (unsigned long)mod -
			   (unsigned long)mod->module_core,
			   sizeof(struct module), GFP_KERNEL);

	for (i = 1; i < hdr->e_shnum; i++) {
		if (!(sechdrs[i].sh_flags & SHF_ALLOC))
			continue;
		if (strncmp(secstrings + sechdrs[i].sh_name, ".data", 5) != 0
		    && strncmp(secstrings + sechdrs[i].sh_name, ".bss", 4) != 0)
			continue;

		kmemleak_scan_area(mod->module_core, sechdrs[i].sh_addr -
				   (unsigned long)mod->module_core,
				   sechdrs[i].sh_size, GFP_KERNEL);
	}
}
#else
static inline void kmemleak_load_module(struct module *mod, Elf_Ehdr *hdr,
					Elf_Shdr *sechdrs, char *secstrings)
{
}
#endif

/* Allocate and load the module: note that size of section 0 is always
   zero, and we rely on this for optional sections. 
分配加载内核模块
 */
static noinline struct module *load_module(void __user *umod,//文件映像的用户态指针
				                              unsigned long len,//文件映像长度
				                              const char __user *uargs)//加载参数
{
	Elf_Ehdr *hdr;
	Elf_Shdr *sechdrs;
	char *secstrings, *args, *modmagic, *strtab = NULL;
	char *staging;
	unsigned int i;
	unsigned int symindex = 0;
	unsigned int strindex = 0;
	unsigned int modindex, 
		         versindex, 
		         infoindex, 
		         pcpuindex;
	
	struct module *mod;
	long err = 0;
	void *percpu = NULL, 
		 *ptr = NULL; /* Stops spurious gcc warning */
	
	unsigned long symoffs, 
		          stroffs, 
		          *strmap;

	mm_segment_t old_fs;

	DEBUGP("load_module: umod=%p, len=%lu, uargs=%p\n",umod, len, uargs);

	//被加载elf的长度不正确,非elf格式
	if (len < sizeof(*hdr))
		return ERR_PTR(-ENOEXEC);

	/* Suck in entire file: we'll want most of it. */
	/* vmalloc barfs on "unusual" numbers.  Check here */
	if (len > 64 * 1024 * 1024 || (hdr = vmalloc(len)) == NULL)
		return ERR_PTR(-ENOMEM);

    //将影响拷贝到内核中
	if (copy_from_user(hdr, umod, len) != 0) 
	{
		err = -EFAULT;
		goto free_hdr;
	}

	//主要用来检查是否为ELF格式
	/* Sanity checks against insmoding binaries or wrong arch,weird elf version */
	if (memcmp(hdr->e_ident, ELFMAG, SELFMAG) != 0  //检查elf魔数
	    || hdr->e_type != ET_REL                    //检测elf类型是否为重定向类型 
	    || !elf_check_arch(hdr)                     //检查体系类型 
	    || hdr->e_shentsize != sizeof(*sechdrs))    //检查节表项的大小
	{
		err = -ENOEXEC;  
		goto free_hdr;
	}

    //判断总长度 是否小于 节表的偏移 + 节表的大小,  即判断节表是否完整的在文件中
    //hdr->e_shnum * sizeof(Elf_Shdr) 计算节表的总大小
	if (len <  hdr->e_shoff + hdr->e_shnum * sizeof(Elf_Shdr))
		goto truncated;

	/* Convenience variables */
	//计算节表的偏移位置
	sechdrs = (void *)hdr + hdr->e_shoff;

	//节名字表的表项在节区头部表的索引此节的名字为shstrtab 注意与strtab的区别
	secstrings = (void *)hdr + sechdrs[hdr->e_shstrndx].sh_offset;

    //将第一个节在进程地址空间中的虚拟地址地址设置为0
	sechdrs[0].sh_addr = 0;

	for (i = 1; i < hdr->e_shnum; i++) 
	{
	
		if (sechdrs[i].sh_type != SHT_NOBITS &&               //节的类型不是SHT_NOBITS(文件中不保存内容如bss)
			len < sechdrs[i].sh_offset + sechdrs[i].sh_size)  //节的数据偏移+节的总大小  超过读取的总长度,  即该节不完整
			goto truncated;                                  //该文件被截断了 

		/* Mark all sections sh_addr with their address in the
		   temporary image. */
		//先设置所有节的虚拟地址为 相对于现在镜像内存的地址  内存基地址+文件中偏移 
		sechdrs[i].sh_addr = (size_t)hdr + sechdrs[i].sh_offset;

		/* Internal symbols and strings. */
		//如果此节表头是一个符号表
		if (sechdrs[i].sh_type == SHT_SYMTAB) 
		{
			symindex = i;

			//得到字符串表咋节表项中的索引
			strindex = sechdrs[i].sh_link;

			//得到字符串表的偏移
			strtab = (char *)hdr + sechdrs[strindex].sh_offset;
		}
#ifndef CONFIG_MODULE_UNLOAD
		/* Don't load .exit sections 
              不加载.exit节  
        */
		if (strstarts(secstrings+sechdrs[i].sh_name, ".exit"))
			sechdrs[i].sh_flags &= ~(unsigned long)SHF_ALLOC; //设置节名为.exit开头的节 不分配内存 
#endif
	}

	//查找是否存在一个节名为.gnu.linkonce.this_module的表项.并返回节表项的索引
	//在scripts/mod/modpost.c 会生成xx.mod.c  在这个文件中有如下定义
	/*
	 __visible struct module __this_module  __section(.gnu.linkonce.this_module) = {
	    .name = KBUILD_MODNAME,
	    .init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	    .exit = cleanup_module,
#endif
	    .arch = MODULE_ARCH_INIT,
	};
   其中定义了一个__this_module变量 并将其放到.gnu.linkonce.this_module节中
   其中对一些字段进行了初始化
	*/
	modindex = find_sec(hdr, sechdrs, secstrings,".gnu.linkonce.this_module");
	if (!modindex) 
	{   
	    //如果没有此节 说明不是一个内核模块
		printk(KERN_WARNING "No module found in object\n");
		err = -ENOEXEC;
		goto free_hdr;
	}
	/* This is temporary: point mod into copy of data. 
        .gnu.linkonce.this_module节描述的是一个module结构体?  
	*/
	mod = (void *)sechdrs[modindex].sh_addr;//得到.gnu.linkonce.this_module的地址

	//如果没有符号表
	if (symindex == 0) 
	{
		printk(KERN_WARNING "%s: module has no symbols (stripped?)\n",mod->name);
		err = -ENOEXEC;
		goto free_hdr;
	}

	//查找节__versions
	versindex = find_sec(hdr, sechdrs, secstrings, "__versions");

	//查找节.modinfo
	infoindex = find_sec(hdr, sechdrs, secstrings, ".modinfo");
	pcpuindex = find_pcpusec(hdr, sechdrs, secstrings);

	/* Don't keep modinfo and version sections. */
	//infoindex 和version信息不必留在内存中
	sechdrs[infoindex].sh_flags &= ~(unsigned long)SHF_ALLOC;
	sechdrs[versindex].sh_flags &= ~(unsigned long)SHF_ALLOC; 

	/* Check module struct version now, before we try to use module. */
	//如果没有设置CONFIG_MODVERSIONS  此处返回是0
	if (!check_modstruct_version(sechdrs, versindex, mod)) 
	{
		err = -ENOEXEC;
		goto free_hdr;
	}

    //在.modinfo节区查找vermagic = 开头的字符串(相当于作为key查找)，返回的是vermagic=后面的字符串
    //如vermagic=5.4.0-42-generic SMP mod_unload
    //返回"5.4.0-42-generic SMP mod_unload"
	modmagic = get_modinfo(sechdrs, infoindex, "vermagic");
	/* This is allowed: modprobe --force will invalidate it. */
	if (!modmagic) 
	{   
	    //没找到版本信息
        //如果配置了CONFIG_MODULE_FORCE_LOAD，则强制加载，标记taints，返回true，否则返回错误(-ENOEXEC)
		err = try_to_force_load(mod, "bad vermagic");
		if (err)
			goto free_hdr;
	} 
	else if (!same_magic(modmagic, vermagic, versindex)) 
	{
	    //字符串比较，如果有vers，则加上vers比较
        //vermagic 是个全局变量，在最终导出的vmlinux中可以找到。
		printk(KERN_ERR "%s: version magic '%s' should be '%s'\n",
		       mod->name, modmagic, vermagic);
		err = -ENOEXEC;
		goto free_hdr;
	}

    /*
    /*
  * 从2.6.28版本起，内核代码的drivers下增加了一个staging目录，
  * 这个目录也是用来存放驱动程序，只是这里的驱动程序
  * 和上层目录不同，加载的时候内核日志会打印如下的语句:
  * MODULE_NAME: module is from the staging directory, the quality is unknown, you have been warned.
  * Greg KH于2008年6月10号在Linux内核邮件列表里发出一封信，宣布建
  * 立了另外一棵kernel tree，这就是Linux staging tree。Greg解释到，staging tree
  * 建立之目的是用来放置一些未充分测试或者因为一些其他原因
  * 未能进入内核的新增驱动程序和新增文件系统。
  */
	
	staging = get_modinfo(sechdrs, infoindex, "staging");
	if (staging) 
	{
	    
		add_taint_module(mod, TAINT_CRAP);
		printk(KERN_WARNING "%s: module is from the staging directory,"" the quality is unknown, you have been warned.\n",
		       mod->name);
	}

	/* Now copy in args */
	//拷贝用户的参数
	args = strndup_user(uargs, ~0UL >> 1);
	if (IS_ERR(args)) {
		err = PTR_ERR(args);
		goto free_hdr;
	}

	/*
  * 为与符号表相关的字符串表段在内存中分配用于映射的空间。
  * sechdrs[strindex].sh_size是与符号表相关的字符串表段的大小。
  * 这里分配的是一个位图，用于符号表中的符号名称的
  * 映射。
  */
	strmap = kzalloc(BITS_TO_LONGS(sechdrs[strindex].sh_size)* sizeof(long), GFP_KERNEL);
	if (!strmap) {
		err = -ENOMEM;
		goto free_mod;
	}

	//根据模块名检查此模块是否加载过
	if (find_module(mod->name)) 
	{
		err = -EEXIST;
		goto free_mod;
	}

    //将模块状态设置为正在加载
	mod->state = MODULE_STATE_COMING;

	/*err总是为0*/
	/* Allow arches to frob section contents and sizes.  */
	err = module_frob_arch_sections(hdr, sechdrs, secstrings, mod);
	if (err < 0)
		goto free_mod;

	 /*
  * 如果存在.data.percpu段，则为该段在内存中分配空间。
  * 分配成功后，移除SHF_ALLOC标志，并且初始化module实例
  * 的percpu成员。
  */
	if (pcpuindex) 
	{
		/* We have a special allocation for this section. */
		percpu = percpu_modalloc(sechdrs[pcpuindex].sh_size,sechdrs[pcpuindex].sh_addralign,mod->name);
		if (!percpu) {
			err = -ENOMEM;
			goto free_mod;
		}
		sechdrs[pcpuindex].sh_flags &= ~(unsigned long)SHF_ALLOC;
		mod->percpu = percpu;
	}

	/* Determine total sizes, and put offsets in sh_entsize.  For now
	   this is done generically; there doesn't appear to be any
	   special cases for the architectures. */
    /*
	 * 对core section和init section中的大小及代码段的信息进行
	 * 统计
	 */
	layout_sections(mod, hdr, sechdrs, secstrings);

    /*
	 * 处理符号表中的符号，返回值是core section尾部的
	 * 符号表的偏移。
	 */
	symoffs = layout_symtab(mod, sechdrs, symindex, strindex, hdr,secstrings, &stroffs, strmap);

	/* Do the allocs. */
	/*
	 * 为core section分配内存，初始化后存储在module实例
	 * 的module_core成员中。
	 */
	//为模块分配core_size大小的内存
	ptr = module_alloc_update_bounds(mod->core_size);
	/*
	 * The pointer to this block is stored in the module structure
	 * which is inside the block. Just mark it as not being a
	 * leak.
	 */
	//标记到内存泄露检测里 
	kmemleak_not_leak(ptr);
	if (!ptr) {
		err = -ENOMEM;
		goto free_percpu;
	}
	//初始化
	memset(ptr, 0, mod->core_size);
	mod->module_core = ptr; //设置module.module_core指针

    //为init分配空间
    /*
	 * 为init section分配内存，初始化后存储在module实例
	 * 的module_init成员中。
	 */
	ptr = module_alloc_update_bounds(mod->init_size);
	/*
	 * The pointer to this block is stored in the module structure
	 * which is inside the block. This block doesn't need to be
	 * scanned as it contains data and code that will be freed
	 * after the module is initialized.
	 */
	// //当泄漏时不扫描或报告对象
	kmemleak_ignore(ptr);
	if (!ptr && mod->init_size) {
		err = -ENOMEM;
		goto free_core;
	}
	memset(ptr, 0, mod->init_size);
	//设置module.module_init
	mod->module_init = ptr;

	/* Transfer each section which specifies SHF_ALLOC */
	DEBUGP("final section addresses:\n");
     //复制所有有SHF_ALLOC属性的节到新内核空间
     /*
	 * 遍历段首部表，拷贝需要占用内存的段到
	 * init section 或core section，并且调整各个段的运行
	 * 时地址。
	 */
	for (i = 0; i < hdr->e_shnum; i++) 
	{
		void *dest;

		if (!(sechdrs[i].sh_flags & SHF_ALLOC))
			continue;

		//如果这个节区属于init的节区
		if (sechdrs[i].sh_entsize & INIT_OFFSET_MASK)
			dest = mod->module_init + (sechdrs[i].sh_entsize & ~INIT_OFFSET_MASK);
		else//如果这个节区属于core的节区
			dest = mod->module_core + sechdrs[i].sh_entsize;

		/*
		 * 将当前段的内容从ELF文件头拷贝到指定的
		 * 段(init section或core section)中
		 */
		if (sechdrs[i].sh_type != SHT_NOBITS)
			memcpy(dest, (void *)sechdrs[i].sh_addr,sechdrs[i].sh_size);
		
		/* Update sh_addr to point to copy in image. */
		sechdrs[i].sh_addr = (unsigned long)dest;
		DEBUGP("\t0x%lx %s\n", sechdrs[i].sh_addr, secstrings + sechdrs[i].sh_name);
	}
	/* Module has been moved. */
	mod = (void *)sechdrs[modindex].sh_addr;
	kmemleak_load_module(mod, hdr, sechdrs, secstrings);

#if defined(CONFIG_MODULE_UNLOAD) && defined(CONFIG_SMP)
	mod->refptr = percpu_modalloc(sizeof(local_t), __alignof__(local_t),
				      mod->name);
	if (!mod->refptr) {
		err = -ENOMEM;
		goto free_init;
	}
#endif
	/* Now we've moved module, initialize linked lists, etc. */
	module_unload_init(mod);

	/* add kobject, so we can reference it. */
	//在/sys 下创建一些文件
	err = mod_sysfs_init(mod);
	if (err)
		goto free_unload;

	//检测MODULE_LICENSE("GPL") 是不是设置的GPL或之类的值  ,若没设置或随意设置则打印污染内核的警告
	/* Set up license info based on the info section */
	set_license(mod, get_modinfo(sechdrs, infoindex, "license"));

	/*
	 * ndiswrapper is under GPL by itself, but loads proprietary modules.
	 * Don't use add_taint_module(), as it would prevent ndiswrapper from
	 * using GPL-only symbols it needs.
	 */
	if (strcmp(mod->name, "ndiswrapper") == 0)
		add_taint(TAINT_PROPRIETARY_MODULE);

	/* driverloader was caught wrongly pretending to be under GPL */
	if (strcmp(mod->name, "driverloader") == 0)
		add_taint_module(mod, TAINT_PROPRIETARY_MODULE);

	/* Set up MODINFO_ATTR fields */
	/*
	 * 根据.modinfo段设置模块信息。
	 */
	setup_modinfo(mod, sechdrs, infoindex);

	/* Fix up syms, so that st_value is a pointer to location. */
	/*
	 * 解决当前模块对其他模块的符号引用问题，
	 * 并找到符号对应的值的地址
	 */
	err = simplify_symbols(sechdrs, symindex, strtab, versindex, pcpuindex,
			       mod);
	if (err < 0)
		goto cleanup;

	/* Now we've got everything in the final locations, we can
	 * find optional sections. */
	 /*
	 * 获取__param段的运行时地址，及其存储的
	 * 对象的个数。
	 */
	mod->kp = section_objs(hdr, sechdrs, secstrings, "__param",sizeof(*mod->kp), &mod->num_kp);

	/*
		 * 获取__ksymtab段的运行时地址，及其存储的
		 * 对象的个数。
   */
	mod->syms = section_objs(hdr, sechdrs, secstrings, "__ksymtab",
				 sizeof(*mod->syms), &mod->num_syms);

	/*
	 * 获取__kcrctab段的运行时地址。
	 */			 
	mod->crcs = section_addr(hdr, sechdrs, secstrings, "__kcrctab");

	/*
	 * 获取__ksymtab_gpl段的运行时地址，及其存储的
	 * 对象的个数。
	 */
	mod->gpl_syms = section_objs(hdr, sechdrs, secstrings, "__ksymtab_gpl",
				     sizeof(*mod->gpl_syms),
				     &mod->num_gpl_syms);

	/*
	 * 获取__kcrctab_gpl段的运行时地址。
	 */				 
	mod->gpl_crcs = section_addr(hdr, sechdrs, secstrings, "__kcrctab_gpl");
	mod->gpl_future_syms = section_objs(hdr, sechdrs, secstrings,
					    "__ksymtab_gpl_future",
					    sizeof(*mod->gpl_future_syms),
					    &mod->num_gpl_future_syms);
	/*
	 * 获取__ksymtab_gpl_future段的运行时地址，及其存储的
	 * 对象的个数。
	 */
	mod->gpl_future_crcs = section_addr(hdr, sechdrs, secstrings,
					    "__kcrctab_gpl_future");

#ifdef CONFIG_UNUSED_SYMBOLS
	/*
		* 获取__ksymtab_unused段的运行时地址，及其存储的
		* 对象的个数。
		*/
	mod->unused_syms = section_objs(hdr, sechdrs, secstrings,
					"__ksymtab_unused",
					sizeof(*mod->unused_syms),
					&mod->num_unused_syms);
	mod->unused_crcs = section_addr(hdr, sechdrs, secstrings,
					"__kcrctab_unused");
	mod->unused_gpl_syms = section_objs(hdr, sechdrs, secstrings,
					    "__ksymtab_unused_gpl",
					    sizeof(*mod->unused_gpl_syms),
					    &mod->num_unused_gpl_syms);
	mod->unused_gpl_crcs = section_addr(hdr, sechdrs, secstrings,
					    "__kcrctab_unused_gpl");
#endif
#ifdef CONFIG_CONSTRUCTORS //得到.ctors中的构造函数
	mod->ctors = section_objs(hdr, sechdrs, secstrings, ".ctors",sizeof(*mod->ctors), &mod->num_ctors);
#endif

#ifdef CONFIG_TRACEPOINTS
	mod->tracepoints = section_objs(hdr, sechdrs, secstrings,"__tracepoints",sizeof(*mod->tracepoints),&mod->num_tracepoints);
#endif
#ifdef CONFIG_EVENT_TRACING
	mod->trace_events = section_objs(hdr, sechdrs, secstrings,
					 "_ftrace_events",
					 sizeof(*mod->trace_events),
					 &mod->num_trace_events);
#endif
#ifdef CONFIG_FTRACE_MCOUNT_RECORD
	/* sechdrs[0].sh_size is always zero */
	mod->ftrace_callsites = section_objs(hdr, sechdrs, secstrings,
					     "__mcount_loc",
					     sizeof(*mod->ftrace_callsites),
					     &mod->num_ftrace_callsites);
#endif
#ifdef CONFIG_MODVERSIONS
	if ((mod->num_syms && !mod->crcs)
	    || (mod->num_gpl_syms && !mod->gpl_crcs)
	    || (mod->num_gpl_future_syms && !mod->gpl_future_crcs)
#ifdef CONFIG_UNUSED_SYMBOLS
	    || (mod->num_unused_syms && !mod->unused_crcs)
	    || (mod->num_unused_gpl_syms && !mod->unused_gpl_crcs)
#endif
		) {
		err = try_to_force_load(mod,
					"no versions for exported symbols");
		if (err)
			goto cleanup;
	}
#endif

	/* Now do relocations. 
     现在进行重定向的操作
    */
	for (i = 1; i < hdr->e_shnum; i++) 
	{
		const char *strtab = (char *)sechdrs[strindex].sh_addr;
		unsigned int info = sechdrs[i].sh_info;

		/* Not a valid relocation section? */
		if (info >= hdr->e_shnum)
			continue;

		/* Don't bother with non-allocated sections */
		if (!(sechdrs[info].sh_flags & SHF_ALLOC))
			continue;

		if (sechdrs[i].sh_type == SHT_REL)
			err = apply_relocate(sechdrs, strtab, symindex, i,mod);
		else if (sechdrs[i].sh_type == SHT_RELA)
			err = apply_relocate_add(sechdrs, strtab, symindex, i,
						 mod);
		if (err < 0)
			goto cleanup;
	}

        /* Find duplicate symbols */
	/*
	 * 检查模块导出的符号在内核导出的或其他模块
	 * 导出的符号是否有重复的。
	 */
	err = verify_export_symbols(mod);
	if (err < 0)
		goto cleanup;

    /*
	 * 获取__ex_table段的运行时地址，及其存储的
	 * 对象的个数。
	 */
  	/* Set up and sort exception table */
	mod->extable = section_objs(hdr, sechdrs, secstrings, "__ex_table",
				    sizeof(*mod->extable), &mod->num_exentries);
	sort_extable(mod->extable, mod->extable + mod->num_exentries);

	/* Finally, copy percpu area over. */
	percpu_modcopy(mod->percpu, (void *)sechdrs[pcpuindex].sh_addr,
		       sechdrs[pcpuindex].sh_size);
	/*
		 * 初始化模块中字符串表、符号表相关的成员，
			* 初始化core section中的字符串表和符号表。
		 */
	add_kallsyms(mod, sechdrs, hdr->e_shnum, symindex, strindex,
		     symoffs, stroffs, secstrings, strmap);
	kfree(strmap);
	strmap = NULL;

	if (!mod->taints) 
	{
		struct _ddebug *debug;
		unsigned int num_debug;

		debug = section_objs(hdr, sechdrs, secstrings, "__verbose",
				     sizeof(*debug), &num_debug);
		if (debug)
			dynamic_debug_setup(debug, num_debug);
	}

	err = module_finalize(hdr, sechdrs, mod);
	if (err < 0)
		goto cleanup;

	/* flush the icache in correct context */
	old_fs = get_fs();
	set_fs(KERNEL_DS);

	/*
	 * Flush the instruction cache, since we've played with text.
	 * Do it before processing of module parameters, so the module
	 * can provide parameter accessor functions of its own.
	 */
	if (mod->module_init)
		flush_icache_range((unsigned long)mod->module_init,
				   (unsigned long)mod->module_init
				   + mod->init_size);
	flush_icache_range((unsigned long)mod->module_core,
			   (unsigned long)mod->module_core + mod->core_size);

    /*
	 * get_fs是用来获取当前进程的地址限制，当当前的限制是
	 * KERNEL_DS时，内核不会检查参数中的地址类型
	 */
	set_fs(old_fs);

	mod->args = args;
	if (section_addr(hdr, sechdrs, secstrings, "__obsparm"))
		printk(KERN_WARNING "%s: Ignoring obsolete parameters\n",
		       mod->name);

	/* Now sew it into the lists so we can get lockdep and oops
	 * info during argument parsing.  Noone should access us, since
	 * strong_try_module_get() will fail.
	 * lockdep/oops can run asynchronous, so use the RCU list insertion
	 * function to insert in a way safe to concurrent readers.
	 * The mutex protects against concurrent writers.
	 */
	list_add_rcu(&mod->list, &modules);

	 //解析参数到mod->kp 和mod->num_kp
	err = parse_args(mod->name, mod->args, mod->kp, mod->num_kp, NULL);
	if (err < 0)
		goto unlink;


	 //模块信息加入到sysfs中
	err = mod_sysfs_setup(mod, mod->kp, mod->num_kp);
	if (err < 0)
		goto unlink;
	
	add_sect_attrs(mod, hdr->e_shnum, secstrings, sechdrs);
	add_notes_attrs(mod, hdr->e_shnum, secstrings, sechdrs);

	/* Get rid of temporary copy */
	vfree(hdr);

	trace_module_load(mod);

	/* Done! */
	return mod;

 unlink:
	/* Unlink carefully: kallsyms could be walking list. */
	list_del_rcu(&mod->list);
	synchronize_sched();
	module_arch_cleanup(mod);
 cleanup:
	free_modinfo(mod);
	kobject_del(&mod->mkobj.kobj);
	kobject_put(&mod->mkobj.kobj);
 free_unload:
	module_unload_free(mod);
#if defined(CONFIG_MODULE_UNLOAD) && defined(CONFIG_SMP)
	percpu_modfree(mod->refptr);
 free_init:
#endif
	module_free(mod, mod->module_init);
 free_core:
	module_free(mod, mod->module_core);
	/* mod will be freed with core. Don't access it beyond this line! */
 free_percpu:
	if (percpu)
		percpu_modfree(percpu);
 free_mod:
	kfree(args);
	kfree(strmap);
 free_hdr:
	vfree(hdr);
	return ERR_PTR(err);

 truncated:
	printk(KERN_ERR "Module len %lu truncated\n", len);
	err = -ENOEXEC;
	goto free_hdr;
}

/* Call module constructors. */
static void do_mod_ctors(struct module *mod)
{
//支持构造函数
#ifdef CONFIG_CONSTRUCTORS
	unsigned long i;

	//分别调用每个狗仔函数 即.ctor节中的函数
	for (i = 0; i < mod->num_ctors; i++)
		mod->ctors[i]();
#endif
}

/* This is where the real work happens */
/*
系统调用函数  insmod会调用此系统调用
sys_init_module()
模块的加载首先会在用户空间将模块的文件映射到内存(后面称之为文件映像)，然后调用sys_init_module,内核会将文件映像复制到内核态，然后根据各个节的属性，再重新分配模块的空间，重新分配的空间我们后面称之为内存映像
*/
SYSCALL_DEFINE3(init_module, 
                    void __user *, umod,  //一个指向用户地址空间的指针，模块的二进制代码位于其中
		            unsigned long, len,  //用户地址空间的长度
		            const char __user *, uargs)//模块的参数
{
	struct module *mod;//内核用struct module来表示一个模块
	int ret = 0;

	/* Must have permission 
    权能检查 是否有权限加载模块
	*/
	if (!capable(CAP_SYS_MODULE) || modules_disabled)
		return -EPERM;

	/* Only one module load at a time, please 
       在同一时刻只能有一个模块可以被加载  即不支持并发加载 
	*/
	if (mutex_lock_interruptible(&module_mutex) != 0)
		return -EINTR;

	/*载入elf模块，返回初始化好的module结构体*/
	mod = load_module(umod, len, uargs);
	if (IS_ERR(mod)) 
	{
	    //加载出现错误
		mutex_unlock(&module_mutex);
		return PTR_ERR(mod);
	}

	/* Drop lock so they can recurse */
	mutex_unlock(&module_mutex);

    //利用内核通知链，告诉内核其他子系统，有模块将要装入
	blocking_notifier_call_chain(&module_notify_list,MODULE_STATE_COMING, mod);

    //调用mod中的所有构造函数 即.ctors节中的构造函数
	do_mod_ctors(mod);
	
	/* Start the module  //调用模块的init函数*/
	if (mod->init != NULL)
		ret = do_one_initcall(mod->init);//基本上等于直接调用mod->init

	if (ret < 0) 
	{
		/* Init routine failed: abort.  Try to protect us from
                   buggy refcounters. */
        //init函数运行失败，模块状态改为正在卸载中
		mod->state = MODULE_STATE_GOING;
		synchronize_sched();
		
		module_put(mod); //减小模块的使用计数

		//调用内核通知连，通知模块正在卸载中
		blocking_notifier_call_chain(&module_notify_list,MODULE_STATE_GOING, mod);

		mutex_lock(&module_mutex);
		//释放模块
		free_module(mod);
		mutex_unlock(&module_mutex);

		//唤醒module_wq队列中的所有元素，由于内核的整个modules
        //列表需要原子操作，如果有多个进程同时对modules列表进行
        //操作，后来的就会进入这个module_wq队列
		wake_up(&module_wq);
		return ret;
	}

	if (ret > 0) 
	{
		printk(KERN_WARNING
"%s: '%s'->init suspiciously returned %d, it should follow 0/-E convention\n"
"%s: loading module anyway...\n",
		       __func__, mod->name, ret,
		       __func__);
		dump_stack(); //此函数可以打印当前cpu的堆栈信息
	}

	/* Now it's a first class citizen!  Wake up anyone waiting for it. */
    //设置模块状态为加载成功，唤醒所有等待此模块加载完成的进程
	mod->state = MODULE_STATE_LIVE;

	wake_up(&module_wq);

	//调用内核通知连，通知模块已加载
	blocking_notifier_call_chain(&module_notify_list,
				     MODULE_STATE_LIVE, mod);

     //等待所有的异步函数调用完成  
	/* We need to finish all async code before the module init sequence is done */
	async_synchronize_full();

	mutex_lock(&module_mutex);
	/* Drop initial reference. */
	module_put(mod);//减少引用计数
	trim_init_extable(mod);
	
#ifdef CONFIG_KALLSYMS
    //导出符号表，在/proc/kallsyms中能看到的应该是mod->symtab
	mod->num_symtab = mod->core_num_syms;

	//符号表的位置
	mod->symtab = mod->core_symtab;
    //字符串表的位置
	mod->strtab = mod->core_strtab;
#endif

    //对init的空间进行释放
	module_free(mod, mod->module_init);

	mod->module_init = NULL;
	mod->init_size = 0;
	mod->init_text_size = 0;
	mutex_unlock(&module_mutex);

	return 0;
}

static inline int within(unsigned long addr, void *start, unsigned long size)
{
	return ((void *)addr >= start && (void *)addr < start + size);
}

#ifdef CONFIG_KALLSYMS
/*
 * This ignores the intensely annoying "mapping symbols" found
 * in ARM ELF files: $a, $t and $d.
 */
static inline int is_arm_mapping_symbol(const char *str)
{
	return str[0] == '$' && strchr("atd", str[1])
	       && (str[2] == '\0' || str[2] == '.');
}

static const char *get_ksymbol(struct module *mod,
			       unsigned long addr,
			       unsigned long *size,
			       unsigned long *offset)
{
	unsigned int i, best = 0;
	unsigned long nextval;

	/* At worse, next value is at end of module */
	if (within_module_init(addr, mod))
		nextval = (unsigned long)mod->module_init+mod->init_text_size;
	else
		nextval = (unsigned long)mod->module_core+mod->core_text_size;

	/* Scan for closest preceeding symbol, and next symbol. (ELF
	   starts real symbols at 1). */
	for (i = 1; i < mod->num_symtab; i++) {
		if (mod->symtab[i].st_shndx == SHN_UNDEF)
			continue;

		/* We ignore unnamed symbols: they're uninformative
		 * and inserted at a whim. */
		if (mod->symtab[i].st_value <= addr
		    && mod->symtab[i].st_value > mod->symtab[best].st_value
		    && *(mod->strtab + mod->symtab[i].st_name) != '\0'
		    && !is_arm_mapping_symbol(mod->strtab + mod->symtab[i].st_name))
			best = i;
		if (mod->symtab[i].st_value > addr
		    && mod->symtab[i].st_value < nextval
		    && *(mod->strtab + mod->symtab[i].st_name) != '\0'
		    && !is_arm_mapping_symbol(mod->strtab + mod->symtab[i].st_name))
			nextval = mod->symtab[i].st_value;
	}

	if (!best)
		return NULL;

	if (size)
		*size = nextval - mod->symtab[best].st_value;
	if (offset)
		*offset = addr - mod->symtab[best].st_value;
	return mod->strtab + mod->symtab[best].st_name;
}

/* For kallsyms to ask for address resolution.  NULL means not found.  Careful
 * not to lock to avoid deadlock on oopses, simply disable preemption. */
const char *module_address_lookup(unsigned long addr,
			    unsigned long *size,
			    unsigned long *offset,
			    char **modname,
			    char *namebuf)
{
	struct module *mod;
	const char *ret = NULL;

	preempt_disable();
	list_for_each_entry_rcu(mod, &modules, list) {
		if (within_module_init(addr, mod) ||
		    within_module_core(addr, mod)) {
			if (modname)
				*modname = mod->name;
			ret = get_ksymbol(mod, addr, size, offset);
			break;
		}
	}
	/* Make a copy in here where it's safe */
	if (ret) {
		strncpy(namebuf, ret, KSYM_NAME_LEN - 1);
		ret = namebuf;
	}
	preempt_enable();
	return ret;
}

int lookup_module_symbol_name(unsigned long addr, char *symname)
{
	struct module *mod;

	preempt_disable();
	list_for_each_entry_rcu(mod, &modules, list) {
		if (within_module_init(addr, mod) ||
		    within_module_core(addr, mod)) {
			const char *sym;

			sym = get_ksymbol(mod, addr, NULL, NULL);
			if (!sym)
				goto out;
			strlcpy(symname, sym, KSYM_NAME_LEN);
			preempt_enable();
			return 0;
		}
	}
out:
	preempt_enable();
	return -ERANGE;
}

int lookup_module_symbol_attrs(unsigned long addr, unsigned long *size,
			unsigned long *offset, char *modname, char *name)
{
	struct module *mod;

	preempt_disable();
	list_for_each_entry_rcu(mod, &modules, list) {
		if (within_module_init(addr, mod) ||
		    within_module_core(addr, mod)) {
			const char *sym;

			sym = get_ksymbol(mod, addr, size, offset);
			if (!sym)
				goto out;
			if (modname)
				strlcpy(modname, mod->name, MODULE_NAME_LEN);
			if (name)
				strlcpy(name, sym, KSYM_NAME_LEN);
			preempt_enable();
			return 0;
		}
	}
out:
	preempt_enable();
	return -ERANGE;
}

int module_get_kallsym(unsigned int symnum, 
                            unsigned long *value, 
                            char *type,
			                char *name, 
			                char *module_name, 
			                int *exported)
{
	struct module *mod;

	preempt_disable();

	//遍历所有的模块 modules是个模块的链表头
	list_for_each_entry_rcu(mod, &modules, list) 
	{

	    ////如果当前模块的num_symtab 大于给定的symnum,则返回symnum 位置symbol的值value和type
		if (symnum < mod->num_symtab) 
		{
			*value = mod->symtab[symnum].st_value; //符号值
			*type = mod->symtab[symnum].st_info; //符号类型

			//从字符串基址加上名字的偏移 获得符号的名
			strlcpy(name, mod->strtab + mod->symtab[symnum].st_name,KSYM_NAME_LEN);

			//获得模块的名
			strlcpy(module_name, mod->name, MODULE_NAME_LEN);

			//检查这个模块中symbol为name的字符串是否导出 
			*exported = is_exported(name, *value, mod);


			preempt_enable();
			return 0;
		}
		symnum -= mod->num_symtab;
	}
	preempt_enable();
	return -ERANGE;
}

static unsigned long mod_find_symname(struct module *mod, const char *name)
{
	unsigned int i;

	for (i = 0; i < mod->num_symtab; i++)
		if (strcmp(name, mod->strtab+mod->symtab[i].st_name) == 0 &&
		    mod->symtab[i].st_info != 'U')
			return mod->symtab[i].st_value;
	return 0;
}

/* Look for this name: can be of form module:name. */
unsigned long module_kallsyms_lookup_name(const char *name)
{
	struct module *mod;
	char *colon;
	unsigned long ret = 0;

	/* Don't lock: we're in enough trouble already. */
	preempt_disable();
	if ((colon = strchr(name, ':')) != NULL) {
		*colon = '\0';
		if ((mod = find_module(name)) != NULL)
			ret = mod_find_symname(mod, colon+1);
		*colon = ':';
	} else {
		list_for_each_entry_rcu(mod, &modules, list)
			if ((ret = mod_find_symname(mod, name)) != 0)
				break;
	}
	preempt_enable();
	return ret;
}

int module_kallsyms_on_each_symbol(int (*fn)(void *, const char *,
					     struct module *, unsigned long),
				   void *data)
{
	struct module *mod;
	unsigned int i;
	int ret;

	list_for_each_entry(mod, &modules, list) {
		for (i = 0; i < mod->num_symtab; i++) {
			ret = fn(data, mod->strtab + mod->symtab[i].st_name,
				 mod, mod->symtab[i].st_value);
			if (ret != 0)
				return ret;
		}
	}
	return 0;
}
#endif /* CONFIG_KALLSYMS */

static char *module_flags(struct module *mod, char *buf)
{
	int bx = 0;

	if (mod->taints ||
	    mod->state == MODULE_STATE_GOING ||
	    mod->state == MODULE_STATE_COMING) {
		buf[bx++] = '(';
		if (mod->taints & (1 << TAINT_PROPRIETARY_MODULE))
			buf[bx++] = 'P';
		if (mod->taints & (1 << TAINT_FORCED_MODULE))
			buf[bx++] = 'F';
		if (mod->taints & (1 << TAINT_CRAP))
			buf[bx++] = 'C';
		/*
		 * TAINT_FORCED_RMMOD: could be added.
		 * TAINT_UNSAFE_SMP, TAINT_MACHINE_CHECK, TAINT_BAD_PAGE don't
		 * apply to modules.
		 */

		/* Show a - for module-is-being-unloaded */
		if (mod->state == MODULE_STATE_GOING)
			buf[bx++] = '-';
		/* Show a + for module-is-being-loaded */
		if (mod->state == MODULE_STATE_COMING)
			buf[bx++] = '+';
		buf[bx++] = ')';
	}
	buf[bx] = '\0';

	return buf;
}

#ifdef CONFIG_PROC_FS
/* Called by the /proc file system to return a list of modules. */
static void *m_start(struct seq_file *m, loff_t *pos)
{
	mutex_lock(&module_mutex);
	return seq_list_start(&modules, *pos);
}

static void *m_next(struct seq_file *m, void *p, loff_t *pos)
{
	return seq_list_next(p, &modules, pos);
}

static void m_stop(struct seq_file *m, void *p)
{
	mutex_unlock(&module_mutex);
}

static int m_show(struct seq_file *m, void *p)
{
	struct module *mod = list_entry(p, struct module, list);
	char buf[8];

    //模块名 占用内核空间的大小 
	seq_printf(m, "%s %u",mod->name, mod->init_size + mod->core_size);

	//被引用次数      依赖此模块的模块名          是否永久的 
	print_unload_info(m, mod);

	/* Informative for users. */
	//模块状态
	seq_printf(m, " %s",mod->state == MODULE_STATE_GOING ? "Unloading":mod->state == MODULE_STATE_COMING ? "Loading":"Live");

    //模块占用内存的地址 
	/* Used by oprofile and other similar tools. */
	seq_printf(m, " 0x%p", mod->module_core);

    //[污染标志]
	/* Taints info */
	if (mod->taints)
		seq_printf(m, " %s", module_flags(mod, buf));

	seq_printf(m, "\n");
	return 0;
}

/* Format: modulename size refcount deps address

   Where refcount is a number or -, and deps is a comma-separated list
   of depends or -.
*/
static const struct seq_operations modules_op = {
	.start	= m_start,
	.next	= m_next,
	.stop	= m_stop,
	.show	= m_show
};

static int modules_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &modules_op);
}

static const struct file_operations proc_modules_operations = {
	.open		= modules_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

//创建/proc/modules 文件 用于存放所有的内核加载的模块信息
static int __init proc_modules_init(void)
{
	proc_create("modules", 0, NULL, &proc_modules_operations);
	return 0;
}
module_init(proc_modules_init);

#endif

/* Given an address, look for it in the module exception tables. */
const struct exception_table_entry *search_module_extables(unsigned long addr)
{
	const struct exception_table_entry *e = NULL;
	struct module *mod;

	preempt_disable();
	list_for_each_entry_rcu(mod, &modules, list) {
		if (mod->num_exentries == 0)
			continue;

		e = search_extable(mod->extable,
				   mod->extable + mod->num_exentries - 1,
				   addr);
		if (e)
			break;
	}
	preempt_enable();

	/* Now, if we found one, we are running inside it now, hence
	   we cannot unload the module, hence no refcnt needed. */
	return e;
}

/*
 * is_module_address - is this address inside a module?
 * @addr: the address to check.
 *
 * See is_module_text_address() if you simply want to see if the address
 * is code (not data).
 */
bool is_module_address(unsigned long addr)
{
	bool ret;

	preempt_disable();
	ret = __module_address(addr) != NULL;
	preempt_enable();

	return ret;
}

/*
 * __module_address - get the module which contains an address.
 * @addr: the address.
 *
 * Must be called with preempt disabled or module mutex held so that
 * module doesn't get freed during this.
 */
struct module *__module_address(unsigned long addr)
{
	struct module *mod;

	if (addr < module_addr_min || addr > module_addr_max)
		return NULL;

	list_for_each_entry_rcu(mod, &modules, list)
		if (within_module_core(addr, mod)
		    || within_module_init(addr, mod))
			return mod;
	return NULL;
}
EXPORT_SYMBOL_GPL(__module_address);

/*
 * is_module_text_address - is this address inside module code?
 * @addr: the address to check.
 *
 * See is_module_address() if you simply want to see if the address is
 * anywhere in a module.  See kernel_text_address() for testing if an
 * address corresponds to kernel or module code.
 */
bool is_module_text_address(unsigned long addr)
{
	bool ret;

	preempt_disable();
	ret = __module_text_address(addr) != NULL;
	preempt_enable();

	return ret;
}

/*
 * __module_text_address - get the module whose code contains an address.
 * @addr: the address.
 *
 * Must be called with preempt disabled or module mutex held so that
 * module doesn't get freed during this.
 */
struct module *__module_text_address(unsigned long addr)
{
	struct module *mod = __module_address(addr);
	if (mod) {
		/* Make sure it's within the text section. */
		if (!within(addr, mod->module_init, mod->init_text_size)
		    && !within(addr, mod->module_core, mod->core_text_size))
			mod = NULL;
	}
	return mod;
}
EXPORT_SYMBOL_GPL(__module_text_address);

/* Don't grab lock, we're oopsing. */
void print_modules(void)
{
	struct module *mod;
	char buf[8];

	printk(KERN_DEFAULT "Modules linked in:");
	/* Most callers should already have preempt disabled, but make sure */
	preempt_disable();
	list_for_each_entry_rcu(mod, &modules, list)
		printk(" %s%s", mod->name, module_flags(mod, buf));
	preempt_enable();
	if (last_unloaded_module[0])
		printk(" [last unloaded: %s]", last_unloaded_module);
	printk("\n");
}

#ifdef CONFIG_MODVERSIONS
/* Generate the signature for all relevant module structures here.
 * If these change, we don't want to try to parse the module. */
void module_layout(struct module *mod,
		   struct modversion_info *ver,
		   struct kernel_param *kp,
		   struct kernel_symbol *ks,
		   struct tracepoint *tp)
{
}
EXPORT_SYMBOL(module_layout);
#endif

#ifdef CONFIG_TRACEPOINTS
void module_update_tracepoints(void)
{
	struct module *mod;

	mutex_lock(&module_mutex);
	list_for_each_entry(mod, &modules, list)
		if (!mod->taints)
			tracepoint_update_probe_range(mod->tracepoints,
				mod->tracepoints + mod->num_tracepoints);
	mutex_unlock(&module_mutex);
}

/*
 * Returns 0 if current not found.
 * Returns 1 if current found.
 */
int module_get_iter_tracepoints(struct tracepoint_iter *iter)
{
	struct module *iter_mod;
	int found = 0;

	mutex_lock(&module_mutex);
	list_for_each_entry(iter_mod, &modules, list) {
		if (!iter_mod->taints) {
			/*
			 * Sorted module list
			 */
			if (iter_mod < iter->module)
				continue;
			else if (iter_mod > iter->module)
				iter->tracepoint = NULL;
			found = tracepoint_get_iter_range(&iter->tracepoint,
				iter_mod->tracepoints,
				iter_mod->tracepoints
					+ iter_mod->num_tracepoints);
			if (found) {
				iter->module = iter_mod;
				break;
			}
		}
	}
	mutex_unlock(&module_mutex);
	return found;
}
#endif
