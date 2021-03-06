/*
 *	Low-Level PCI Support for PC
 *
 *	(c) 1999--2000 Martin Mares <mj@ucw.cz>
 */

#include <linux/sched.h>
#include <linux/pci.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/dmi.h>

#include <asm/acpi.h>
#include <asm/segment.h>
#include <asm/io.h>
#include <asm/smp.h>
#include <asm/pci_x86.h>

unsigned int pci_probe = PCI_PROBE_BIOS | PCI_PROBE_CONF1 | PCI_PROBE_CONF2 | PCI_PROBE_MMCONF;

unsigned int pci_early_dump_regs;
static int pci_bf_sort;
int pci_routeirq;
int noioapicquirk;
#ifdef CONFIG_X86_REROUTE_FOR_BROKEN_BOOT_IRQS
int noioapicreroute = 0;
#else
int noioapicreroute = 1;
#endif
int pcibios_last_bus = -1;
unsigned long pirq_table_addr;//保存pci中断路由表的地址
struct pci_bus *pci_root_bus;

//负责原生pci配置空间的访问 赋值是在pci_arch_init()
struct pci_raw_ops *raw_pci_ops;

//用于扩展pci配置空间的访问 赋值是在pci_arch_init()
struct pci_raw_ops *raw_pci_ext_ops;

int raw_pci_read(unsigned int domain, //pci总线域 
                        unsigned int bus,  //总线编号
                        unsigned int devfn,//插槽号和功能号
						int reg, //寄存器编号
						int len, //读取的数据长度
						u32 *val)//数据缓冲区
{
    
	if (domain == 0 &&  //总线域为0 
		reg < 256 &&  //寄存器编号小于256 即在pci配置空间范围
		raw_pci_ops)  //raw_pci_ops不是空
		return raw_pci_ops->read(domain, bus, devfn, reg, len, val);
	
	if (raw_pci_ext_ops)
		return raw_pci_ext_ops->read(domain, bus, devfn, reg, len, val);
	return -EINVAL;
}

int raw_pci_write(unsigned int domain, unsigned int bus, unsigned int devfn,
						int reg, int len, u32 val)
{
	if (domain == 0 && reg < 256 && raw_pci_ops)
		return raw_pci_ops->write(domain, bus, devfn, reg, len, val);
	if (raw_pci_ext_ops)
		return raw_pci_ext_ops->write(domain, bus, devfn, reg, len, val);
	return -EINVAL;
}

static int pci_read(struct pci_bus *bus, unsigned int devfn, int where, int size, u32 *value)
{
	return raw_pci_read(pci_domain_nr(bus), bus->number,devfn, where, size, value);
}

static int pci_write(struct pci_bus *bus, unsigned int devfn, int where, int size, u32 value)
{
	return raw_pci_write(pci_domain_nr(bus), bus->number,devfn, where, size, value);
}

//用于pci_bus{}.ops域 对pci总线的配置空间访问操作表
struct pci_ops pci_root_ops = {
	.read = pci_read,
	.write = pci_write,
};

/*
 * legacy, numa, and acpi all want to call pcibios_scan_root
 * from their initcalls. This flag prevents that.
 */
int pcibios_scanned;

/*
 * This interrupt-safe spinlock protects all accesses to PCI
 * configuration space.
 */
DEFINE_SPINLOCK(pci_config_lock);

static int __devinit can_skip_ioresource_align(const struct dmi_system_id *d)
{
	pci_probe |= PCI_CAN_SKIP_ISA_ALIGN;
	printk(KERN_INFO "PCI: %s detected, can skip ISA alignment\n", d->ident);
	return 0;
}

static const struct dmi_system_id can_skip_pciprobe_dmi_table[] __devinitconst = {
/*
 * Systems where PCI IO resource ISA alignment can be skipped
 * when the ISA enable bit in the bridge control is not set
 */
	{
		.callback = can_skip_ioresource_align,
		.ident = "IBM System x3800",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "IBM"),
			DMI_MATCH(DMI_PRODUCT_NAME, "x3800"),
		},
	},
	{
		.callback = can_skip_ioresource_align,
		.ident = "IBM System x3850",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "IBM"),
			DMI_MATCH(DMI_PRODUCT_NAME, "x3850"),
		},
	},
	{
		.callback = can_skip_ioresource_align,
		.ident = "IBM System x3950",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "IBM"),
			DMI_MATCH(DMI_PRODUCT_NAME, "x3950"),
		},
	},
	{}
};

void __init dmi_check_skip_isa_align(void)
{
	dmi_check_system(can_skip_pciprobe_dmi_table);
}

static void __devinit pcibios_fixup_device_resources(struct pci_dev *dev)
{
	struct resource *rom_r = &dev->resource[PCI_ROM_RESOURCE];

	if (pci_probe & PCI_NOASSIGN_ROMS) {
		if (rom_r->parent)
			return;
		if (rom_r->start) {
			/* we deal with BIOS assigned ROM later */
			return;
		}
		rom_r->start = rom_r->end = rom_r->flags = 0;
	}
}

/*
 *  Called after each bus is probed, but before its children
 *  are examined.
 */

void __devinit pcibios_fixup_bus(struct pci_bus *b)
{
	struct pci_dev *dev;

	/* root bus? */
	if (!b->parent)
		x86_pci_root_bus_res_quirks(b);
	pci_read_bridge_bases(b);
	list_for_each_entry(dev, &b->devices, bus_list)
		pcibios_fixup_device_resources(dev);
}

/*
 * Only use DMI information to set this if nothing was passed
 * on the kernel command line (which was parsed earlier).
 */

static int __devinit set_bf_sort(const struct dmi_system_id *d)
{
	if (pci_bf_sort == pci_bf_sort_default) {
		pci_bf_sort = pci_dmi_bf;
		printk(KERN_INFO "PCI: %s detected, enabling pci=bfsort.\n", d->ident);
	}
	return 0;
}

/*
 * Enable renumbering of PCI bus# ranges to reach all PCI busses (Cardbus)
 */
#ifdef __i386__
static int __devinit assign_all_busses(const struct dmi_system_id *d)
{
	pci_probe |= PCI_ASSIGN_ALL_BUSSES;
	printk(KERN_INFO "%s detected: enabling PCI bus# renumbering"
			" (pci=assign-busses)\n", d->ident);
	return 0;
}
#endif

static const struct dmi_system_id __devinitconst pciprobe_dmi_table[] = {
#ifdef __i386__
/*
 * Laptops which need pci=assign-busses to see Cardbus cards
 */
	{
		.callback = assign_all_busses,
		.ident = "Samsung X20 Laptop",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Samsung Electronics"),
			DMI_MATCH(DMI_PRODUCT_NAME, "SX20S"),
		},
	},
#endif		/* __i386__ */
	{
		.callback = set_bf_sort,
		.ident = "Dell PowerEdge 1950",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell"),
			DMI_MATCH(DMI_PRODUCT_NAME, "PowerEdge 1950"),
		},
	},
	{
		.callback = set_bf_sort,
		.ident = "Dell PowerEdge 1955",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell"),
			DMI_MATCH(DMI_PRODUCT_NAME, "PowerEdge 1955"),
		},
	},
	{
		.callback = set_bf_sort,
		.ident = "Dell PowerEdge 2900",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell"),
			DMI_MATCH(DMI_PRODUCT_NAME, "PowerEdge 2900"),
		},
	},
	{
		.callback = set_bf_sort,
		.ident = "Dell PowerEdge 2950",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell"),
			DMI_MATCH(DMI_PRODUCT_NAME, "PowerEdge 2950"),
		},
	},
	{
		.callback = set_bf_sort,
		.ident = "Dell PowerEdge R900",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell"),
			DMI_MATCH(DMI_PRODUCT_NAME, "PowerEdge R900"),
		},
	},
	{
		.callback = set_bf_sort,
		.ident = "HP ProLiant BL20p G3",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "HP"),
			DMI_MATCH(DMI_PRODUCT_NAME, "ProLiant BL20p G3"),
		},
	},
	{
		.callback = set_bf_sort,
		.ident = "HP ProLiant BL20p G4",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "HP"),
			DMI_MATCH(DMI_PRODUCT_NAME, "ProLiant BL20p G4"),
		},
	},
	{
		.callback = set_bf_sort,
		.ident = "HP ProLiant BL30p G1",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "HP"),
			DMI_MATCH(DMI_PRODUCT_NAME, "ProLiant BL30p G1"),
		},
	},
	{
		.callback = set_bf_sort,
		.ident = "HP ProLiant BL25p G1",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "HP"),
			DMI_MATCH(DMI_PRODUCT_NAME, "ProLiant BL25p G1"),
		},
	},
	{
		.callback = set_bf_sort,
		.ident = "HP ProLiant BL35p G1",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "HP"),
			DMI_MATCH(DMI_PRODUCT_NAME, "ProLiant BL35p G1"),
		},
	},
	{
		.callback = set_bf_sort,
		.ident = "HP ProLiant BL45p G1",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "HP"),
			DMI_MATCH(DMI_PRODUCT_NAME, "ProLiant BL45p G1"),
		},
	},
	{
		.callback = set_bf_sort,
		.ident = "HP ProLiant BL45p G2",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "HP"),
			DMI_MATCH(DMI_PRODUCT_NAME, "ProLiant BL45p G2"),
		},
	},
	{
		.callback = set_bf_sort,
		.ident = "HP ProLiant BL460c G1",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "HP"),
			DMI_MATCH(DMI_PRODUCT_NAME, "ProLiant BL460c G1"),
		},
	},
	{
		.callback = set_bf_sort,
		.ident = "HP ProLiant BL465c G1",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "HP"),
			DMI_MATCH(DMI_PRODUCT_NAME, "ProLiant BL465c G1"),
		},
	},
	{
		.callback = set_bf_sort,
		.ident = "HP ProLiant BL480c G1",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "HP"),
			DMI_MATCH(DMI_PRODUCT_NAME, "ProLiant BL480c G1"),
		},
	},
	{
		.callback = set_bf_sort,
		.ident = "HP ProLiant BL685c G1",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "HP"),
			DMI_MATCH(DMI_PRODUCT_NAME, "ProLiant BL685c G1"),
		},
	},
	{
		.callback = set_bf_sort,
		.ident = "HP ProLiant DL360",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "HP"),
			DMI_MATCH(DMI_PRODUCT_NAME, "ProLiant DL360"),
		},
	},
	{
		.callback = set_bf_sort,
		.ident = "HP ProLiant DL380",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "HP"),
			DMI_MATCH(DMI_PRODUCT_NAME, "ProLiant DL380"),
		},
	},
#ifdef __i386__
	{
		.callback = assign_all_busses,
		.ident = "Compaq EVO N800c",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Compaq"),
			DMI_MATCH(DMI_PRODUCT_NAME, "EVO N800c"),
		},
	},
#endif
	{
		.callback = set_bf_sort,
		.ident = "HP ProLiant DL385 G2",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "HP"),
			DMI_MATCH(DMI_PRODUCT_NAME, "ProLiant DL385 G2"),
		},
	},
	{
		.callback = set_bf_sort,
		.ident = "HP ProLiant DL585 G2",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "HP"),
			DMI_MATCH(DMI_PRODUCT_NAME, "ProLiant DL585 G2"),
		},
	},
	{}
};

void __init dmi_check_pciprobe(void)
{
	dmi_check_system(pciprobe_dmi_table);
}

//扫描总线 参数busnum:根总线编号 返回这跟总线的pci_bus
//若扫描正常完成的话 这条根总线的所有下级PCI总线以及PCI设备的数据结构在内存中都会构建好 
struct pci_bus * __devinit pcibios_scan_root(int busnum)
{
	struct pci_bus *bus = NULL;
	struct pci_sysdata *sd;

	//遍历所有的PCI总线，检查指定的总线是否扫秒过  是的话 直接返回
	//内核扫描过的总线将连入pci_root_buses为表头的链表中
	while ((bus = pci_find_next_bus(bus)) != NULL) {
		if (bus->number == busnum) 
		{
			/* Already scanned */
			return bus;
		}
	}

	/* Allocate per-root-bus (not per bus) arch-specific data.
	 * TODO: leak; this memory is never freed.
	 * It's arguable whether it's worth the trouble to care.
	 */
	sd = kzalloc(sizeof(*sd), GFP_KERNEL);
	if (!sd) {
		printk(KERN_ERR "PCI: OOM, not probing PCI bus %02x\n", busnum);
		return NULL;
	}

	sd->node = get_mp_bus_to_node(busnum);

	printk(KERN_DEBUG "PCI: Probing PCI hardware (bus %02x)\n", busnum);

	//没有扫描过指定的PCI总线 则调用下面函数  扫描总线上可能的256个设备
	//如果发现是桥设备还要递归的去扫描
	bus = pci_scan_bus_parented(NULL, busnum, &pci_root_ops, sd);
	if (!bus)
		kfree(sd);

	return bus;
}

extern u8 pci_cache_line_size;


int __init pcibios_init(void)
{
	struct cpuinfo_x86 *c = &boot_cpu_data;

	//确保raw_pci_ops不为空   负责配置空间的访问
	if (!raw_pci_ops) {
		printk(KERN_WARNING "PCI: System does not support PCI\n");
		return 0;
	}

	/*
	 * Assume PCI cacheline size of 32 bytes for all x86s except K7/K8
	 * and P4. It's also good for 386/486s (which actually have 16)
	 * as quite a few PCI devices do not support smaller values.
	 */
	//设置pci缓存线 
	pci_cache_line_size = 32 >> 2;
	if (c->x86 >= 6 && c->x86_vendor == X86_VENDOR_AMD)
		pci_cache_line_size = 64 >> 2;	/* K7 & K8 */
	else if (c->x86 > 6 && c->x86_vendor == X86_VENDOR_INTEL)
		pci_cache_line_size = 128 >> 2;	/* P4 */

    //保留已有的资源分配  若配置空间分配好了资源  在资源空间不冲突的情况下 我们尽量使用分配的资源空间
	pcibios_resource_survey();

	//linux2.6是默认深度优先算法进行总线枚举  但2.4是广度优先, 在这里用户可疑选择是否和2.4一样的广度优先算法枚举总线
	//若采用广度优先  则进行重新排序
	if (pci_bf_sort >= pci_force_bf)
		pci_sort_breadthfirst();
	return 0;
}

//对内核启动时候的参数进行解析
char * __devinit  pcibios_setup(char *str)
{  
    //off选项意味着内核启动时就不会再有总线枚举这个过程了，也就是说你机子里挂在PCI总线上的那些设备就都游离于内核之外了，什么后果自己想想吧，所以使用起来要慎重，没事儿的时候最好不要设置它
	if (!strcmp(str, "off")) 
	{
		pci_probe = 0; //方式pci配置空间的方式清0  即不使用任何方式访问pci配置空间
		return NULL;
	} 
	else if (!strcmp(str, "bfsort")) //如果用户指定了bfsort，则枚举最后得到的PCI设备链表里就是按照广度优先的顺序排列
	{
		pci_bf_sort = pci_force_bf;
		return NULL;
	} 
	else if (!strcmp(str, "nobfsort")) 
	{
		pci_bf_sort = pci_force_nobf;
		return NULL;
	}

	//内核配置了可以通过BIOS访问PCI设备
#ifdef CONFIG_PCI_BIOS
	else if (!strcmp(str, "bios"))  //内核参数指定了通过BIOS方式访问PCI配置空间
	{
		pci_probe = PCI_PROBE_BIOS;
		return NULL;
		
	} else if (!strcmp(str, "nobios")) //内核参数指定了不能通过BIOS方式访问PCI配置空间
	{
		pci_probe &= ~PCI_PROBE_BIOS;
		return NULL;
		
	} else if (!strcmp(str, "biosirq")) { //牵涉到PCI的中断机制,那么这个biosirq就是告诉内核通过PCI BIOS去获取一个名叫中断路由表（PCI Interrupt routing table）的东东，从而达到对中断路由情况的了然于胸。不得不说的是，中断路由器只有在中断控制器为PIC的时候才用得着，如果为APIC，就不用麻烦他老人家了
		pci_probe |= PCI_BIOS_IRQ_SCAN;
		return NULL;
	} 
    else if (!strncmp(str, "pirqaddr=", 9)) //pirqaddr允许你去告诉内核中断路由表保存在哪个地方，这样的话，内核可以直接检测这个地址来获得中断路由表，而不用再去在0xF0000h~0xFFFFF之间挖地三尺进行大范围的搜索
	{
		pirq_table_addr = simple_strtoul(str+9, NULL, 0);
		return NULL;
	}
#endif

//内核配置了直接访问pci 配置空间的方式
#ifdef CONFIG_PCI_DIRECT
	else if (!strcmp(str, "conf1"))  //明确告诉内核使用conf1方式 
	{
		pci_probe = PCI_PROBE_CONF1 | PCI_NO_CHECKS;
		return NULL;
	}
	else if (!strcmp(str, "conf2")) {//明确告诉内核使用conf2方式
		pci_probe = PCI_PROBE_CONF2 | PCI_NO_CHECKS;
		return NULL;
	}
#endif

//内核配置了mmconfig访问pci配置空间方式
#ifdef CONFIG_PCI_MMCONFIG
	else if (!strcmp(str, "nommconf")) {//告诉内核不要使用MMConfig方式访问设备
		pci_probe &= ~PCI_PROBE_MMCONF;
		return NULL;
	}
	else if (!strcmp(str, "check_enable_amd_mmconf")) {
		pci_probe |= PCI_CHECK_ENABLE_AMD_MMCONF;
		return NULL;
	}
#endif
	else if (!strcmp(str, "noacpi")) {//用来禁止使用ACPI处理任何PCI相关的内容,包括PCI总线的枚举和PCI设备中断路由
	                                  /*
	                                   咱们已经习惯于将ACPI和电源管理牢牢的联系在一起，所以很难会理解它和PCI这边儿的关系。其实仔细瞅瞅ACPI的全称the Advanced Configuration & Power Interface，里边儿不仅有Power，还有Configuration，你就应该能够悟出点儿道了。ACPI出现的目的是在咱们的OS和硬件平台之间隔出个抽象层，这样OS和平台就可以各自发展各自的，新的OS可以控制老的平台，老的OS也可以控制新的平台而不需要额外的修改。ACPI这个抽象层里包含了很多寄存器和配置信息，绝大部分OS需要从BIOS得到的信息都可以从ACPI得到，并且趋势是未来的任何新的特性相关的信息都只能从ACPI得到，这些信息里当然也包括PCI设备的中断路由情况等
	                                  */
		acpi_noirq_set();
		return NULL;
	}
	else if (!strcmp(str, "noearly")) {//你可以使用noearly选项来禁止这个早期的扫描
		pci_probe |= PCI_PROBE_NOEARLY;
		return NULL;
	}
#ifndef CONFIG_X86_VISWS
	else if (!strcmp(str, "usepirqmask")) {
		pci_probe |= PCI_USE_PIRQ_MASK;
		return NULL;
	} else if (!strncmp(str, "irqmask=", 8)) {
		pcibios_irq_mask = simple_strtol(str+8, NULL, 0);
		return NULL;
	} else if (!strncmp(str, "lastbus=", 8)) {
		pcibios_last_bus = simple_strtol(str+8, NULL, 0);
		return NULL;
	}
#endif
	else if (!strcmp(str, "rom")) {//PCI设备可以携带一个扩展ROM，并将与自己有关的初始化代码放到它里边儿，内核通执行这些代码来完成与设备有关的初始化。同时，PCI Spec还规定了，这些代码不能在设备的ROM里执行，必须得拷贝到系统的RAM里再执行，于是ROM与RAM这两个本来不搭嘎的东东就产生了联系，这个联系就是行话所谓的映射。这里的选项rom就是告诉内核将设备的ROM映射到系统的RAM里
		pci_probe |= PCI_ASSIGN_ROMS;
		return NULL;
	} else if (!strcmp(str, "norom")) {
		pci_probe |= PCI_NOASSIGN_ROMS;
		return NULL;
	} else if (!strcmp(str, "assign-busses")) {//表示内核将无视PCI BIOS分配的总线号，自己重新分配
		pci_probe |= PCI_ASSIGN_ALL_BUSSES;
		return NULL;
	} else if (!strcmp(str, "use_crs")) {
		pci_probe |= PCI_USE__CRS;
		return NULL;
	} else if (!strcmp(str, "earlydump")) {
		pci_early_dump_regs = 1;
		return NULL;
	} else if (!strcmp(str, "routeirq")) {//routeirq，它清楚明白的告诉内核不要信任那些写PCI驱动的，得自己为所有的PCI设备做中断路由
		pci_routeirq = 1;
		return NULL;
	} else if (!strcmp(str, "skip_isa_align")) {
		pci_probe |= PCI_CAN_SKIP_ISA_ALIGN;
		return NULL;
	} else if (!strcmp(str, "noioapicquirk")) {
		noioapicquirk = 1;
		return NULL;
	} else if (!strcmp(str, "ioapicreroute")) {
		if (noioapicreroute != -1)
			noioapicreroute = 0;
		return NULL;
	} else if (!strcmp(str, "noioapicreroute")) {
		if (noioapicreroute != -1)
			noioapicreroute = 1;
		return NULL;
	}
	return str;
}

unsigned int pcibios_assign_all_busses(void)
{
	return (pci_probe & PCI_ASSIGN_ALL_BUSSES) ? 1 : 0;
}

int pcibios_enable_device(struct pci_dev *dev, int mask)
{
	int err;

	if ((err = pci_enable_resources(dev, mask)) < 0)
		return err;

	if (!pci_dev_msi_enabled(dev))
		return pcibios_enable_irq(dev);
	return 0;
}

void pcibios_disable_device (struct pci_dev *dev)
{
	if (!pci_dev_msi_enabled(dev) && pcibios_disable_irq)
		pcibios_disable_irq(dev);
}

int pci_ext_cfg_avail(struct pci_dev *dev)
{
	if (raw_pci_ext_ops)
		return 1;
	else
		return 0;
}

struct pci_bus * __devinit pci_scan_bus_on_node(int busno, struct pci_ops *ops, int node)
{
	struct pci_bus *bus = NULL;
	struct pci_sysdata *sd;

	/*
	 * Allocate per-root-bus (not per bus) arch-specific data.
	 * TODO: leak; this memory is never freed.
	 * It's arguable whether it's worth the trouble to care.
	 */
	sd = kzalloc(sizeof(*sd), GFP_KERNEL);
	if (!sd) {
		printk(KERN_ERR "PCI: OOM, skipping PCI bus %02x\n", busno);
		return NULL;
	}
	sd->node = node;
	bus = pci_scan_bus(busno, ops, sd);
	if (!bus)
		kfree(sd);

	return bus;
}

struct pci_bus * __devinit pci_scan_bus_with_sysdata(int busno)
{
	return pci_scan_bus_on_node(busno, &pci_root_ops, -1);
}

/*
 * NUMA info for PCI busses
 *
 * Early arch code is responsible for filling in reasonable values here.
 * A node id of "-1" means "use current node".  In other words, if a bus
 * has a -1 node id, it's not tightly coupled to any particular chunk
 * of memory (as is the case on some Nehalem systems).
 */
#ifdef CONFIG_NUMA

#define BUS_NR 256

#ifdef CONFIG_X86_64

static int mp_bus_to_node[BUS_NR] = {
	[0 ... BUS_NR - 1] = -1
};

void set_mp_bus_to_node(int busnum, int node)
{
	if (busnum >= 0 &&  busnum < BUS_NR)
		mp_bus_to_node[busnum] = node;
}

int get_mp_bus_to_node(int busnum)
{
	int node = -1;

	if (busnum < 0 || busnum > (BUS_NR - 1))
		return node;

	node = mp_bus_to_node[busnum];

	/*
	 * let numa_node_id to decide it later in dma_alloc_pages
	 * if there is no ram on that node
	 */
	if (node != -1 && !node_online(node))
		node = -1;

	return node;
}

#else /* CONFIG_X86_32 */

static int mp_bus_to_node[BUS_NR] = {
	[0 ... BUS_NR - 1] = -1
};

void set_mp_bus_to_node(int busnum, int node)
{
	if (busnum >= 0 &&  busnum < BUS_NR)
	mp_bus_to_node[busnum] = (unsigned char) node;
}

int get_mp_bus_to_node(int busnum)
{
	int node;

	if (busnum < 0 || busnum > (BUS_NR - 1))
		return 0;
	node = mp_bus_to_node[busnum];
	return node;
}

#endif /* CONFIG_X86_32 */

#endif /* CONFIG_NUMA */
