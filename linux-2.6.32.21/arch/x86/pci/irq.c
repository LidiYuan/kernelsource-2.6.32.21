/*
 *	Low-Level PCI Support for PC -- Routing of Interrupts
 *
 *	(c) 1999--2000 Martin Mares <mj@ucw.cz>
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/dmi.h>
#include <linux/io.h>
#include <linux/smp.h>
#include <asm/io_apic.h>
#include <linux/irq.h>
#include <linux/acpi.h>
#include <asm/pci_x86.h>

#define PIRQ_SIGNATURE	(('$' << 0) + ('P' << 8) + ('I' << 16) + ('R' << 24))
#define PIRQ_VERSION 0x0100

static int broken_hp_bios_irq9;
static int acer_tm360_irqrouting;

static struct irq_routing_table *pirq_table;//中断路由表

static int pirq_enable_irq(struct pci_dev *dev);

/*
 * Never use: 0, 1, 2 (timer, keyboard, and cascade)
 * Avoid using: 13, 14 and 15 (FP error and IDE).
 * Penalize: 3, 4, 6, 7, 12 (known ISA uses: serial, floppy, parallel and mouse)
 */
 //操作系统不允许使用0 1 2 这三个中段号
unsigned int pcibios_irq_mask = 0xfff8;

/*
pci分配irq号的时候使用 中断号有的可以被共享  有的专用，这种平衡机制是通过下面这个表来说明的
每个值对用一个irq 一共16个(8259A级联) 值越大 选择对应irq的机会越小 
定义为1000000的根本不会被pci使用
定义为 1000的使用几率较小
定义为0的最可能用到
*/
static int pirq_penalty[16] = 
{
	1000000, //0 
	1000000, // 1
	1000000, // 2
	1000,    // 3
	1000,    // 4
	0,       // 5
	1000,    // 6
	1000,    // 7
	0,       // 8
	0,       // 9
	0,       // 10
	0,       // 11
	1000,    // 12
	100000,  // 13
	100000,  // 14
	100000   // 15
};

//中断路由驱动
struct irq_router 
{
	char *name; //中断路由器的名字
	u16 vendor,//厂商id 
		 device;//设备id

	//为pci设备获取中断号	 
	/*
        router: 中断路由器对应的pci设备描述符
        dev:要设置或获取中断号的pci设备的描述符
        pirq:为链路值 
        函数返回1 表示成功  否则失败
	*/
	int (*get)(struct pci_dev *router, struct pci_dev *dev, int pirq);

    //为pci设备设置中断号
    //new:为irq编号
	int (*set)(struct pci_dev *router, struct pci_dev *dev, int pirq,int new);
};

//中断路由驱动句柄 为了查找中断路由驱动
//就是特定厂商开发的用于判断某个中断路由器是否由他们制造
struct irq_router_handler {
	u16 vendor; //厂商id

	//探测函数  返回1 表示找到中断路由器 填入名字 get和set函数 否则返回0
	/*
      r:中断路由驱动描述符
      router:中断路由器pci设备描述符
      device:中断路由器的设备id
	*/
	//具体的函数可以看pirq_routers表  不同厂商的设备有不同的函数
	int (*probe)(struct irq_router *r, struct pci_dev *router, u16 device);
};

int (*pcibios_enable_irq)(struct pci_dev *dev) = NULL;//pirq_enable_irq
void (*pcibios_disable_irq)(struct pci_dev *dev) = NULL;

/*
 *  Check passed address for the PCI IRQ Routing Table signature
 *  and perform checksum verification.
 */

//检查传入的地址是否包含一个有效的irq路由表
static inline struct irq_routing_table *pirq_check_routing_table(u8 *addr)
{
	struct irq_routing_table *rt;
	int i;
	u8 sum;

	rt = (struct irq_routing_table *) addr;
	if (rt->signature != PIRQ_SIGNATURE || //检查签名
	    rt->version != PIRQ_VERSION || //检查版本
	    rt->size % 16 || //size域 必须为16的倍数
	    rt->size < sizeof(struct irq_routing_table))
		return NULL;

    //对校验和进行计算
	sum = 0;
	for (i = 0; i < rt->size; i++)
		sum += addr[i];
	if (!sum) 
	{
		DBG(KERN_DEBUG "PCI: Interrupt Routing Table found at 0x%p\n",
			rt);
		return rt;
	}
	return NULL;
}



/*
 *  Search 0xf0000 -- 0xfffff for the PCI IRQ Routing Table.
 */
//在BIOS ROM空间中查找中断路由表 
static struct irq_routing_table * __init pirq_find_routing_table(void)
{
	u8 *addr;
	struct irq_routing_table *rt;

    //如果给出了中断路由表的地址
	if (pirq_table_addr) 
	{  
	    //直接在给定的地址中查找中断路由表
		rt = pirq_check_routing_table((u8 *) __va(pirq_table_addr));
		if (rt)
			return rt;
		printk(KERN_WARNING "PCI: PIRQ table NOT found at pirqaddr\n");
	}
	//搜表BIOS ROm映射区 来对中断路由表进行查找
	for (addr = (u8 *) __va(0xf0000); addr < (u8 *) __va(0x100000); addr += 16) {
		rt = pirq_check_routing_table(addr);
		if (rt)
			return rt;
	}
	return NULL;
}

/*
 *  If we have a IRQ routing table, use it to search for peer host
 *  bridges.  It's a gross hack, but since there are no other known
 *  ways how to get a list of buses, we have to go this way.
 */

static void __init pirq_peer_trick(void)
{
	struct irq_routing_table *rt = pirq_table;
	u8 busmap[256];
	int i;
	struct irq_info *e;

	memset(busmap, 0, sizeof(busmap));
	for (i = 0; i < (rt->size - sizeof(struct irq_routing_table)) / sizeof(struct irq_info); i++) 
	{
		e = &rt->slots[i];
#ifdef DEBUG
		{
			int j;
			DBG(KERN_DEBUG "%02x:%02x slot=%02x", e->bus, e->devfn/8, e->slot);
			for (j = 0; j < 4; j++)
				DBG(" %d:%02x/%04x", j, e->irq[j].link, e->irq[j].bitmap);
			DBG("\n");
		}
#endif
		busmap[e->bus] = 1;
	}
	for (i = 1; i < 256; i++) {
		int node;
		if (!busmap[i] || pci_find_bus(0, i))
			continue;
		node = get_mp_bus_to_node(i);
		if (pci_scan_bus_on_node(i, &pci_root_ops, node))
			printk(KERN_INFO "PCI: Discovered primary peer "
			       "bus %02x [IRQ]\n", i);
	}
	pcibios_last_bus = -1;
}

/*
 *  Code for querying and setting of IRQ routes on various interrupt routers.
 */

void eisa_set_level_irq(unsigned int irq)
{
	unsigned char mask = 1 << (irq & 7);
	unsigned int port = 0x4d0 + (irq >> 3);
	unsigned char val;
	static u16 eisa_irq_mask;

	if (irq >= 16 || (1 << irq) & eisa_irq_mask)
		return;

	eisa_irq_mask |= (1 << irq);
	printk(KERN_DEBUG "PCI: setting IRQ %u as level-triggered\n", irq);
	val = inb(port);
	if (!(val & mask)) {
		DBG(KERN_DEBUG " -> edge");
		outb(val | mask, port);
	}
}

/*
 * Common IRQ routing practice: nibbles in config space,
 * offset by some magic constant.
 */
static unsigned int read_config_nybble(struct pci_dev *router, unsigned offset, unsigned nr)
{
	u8 x;
	unsigned reg = offset + (nr >> 1);

	pci_read_config_byte(router, reg, &x);
	return (nr & 1) ? (x >> 4) : (x & 0xf);
}

static void write_config_nybble(struct pci_dev *router, unsigned offset,
	unsigned nr, unsigned int val)
{
	u8 x;
	unsigned reg = offset + (nr >> 1);

	pci_read_config_byte(router, reg, &x);
	x = (nr & 1) ? ((x & 0x0f) | (val << 4)) : ((x & 0xf0) | val);
	pci_write_config_byte(router, reg, x);
}

/*
 * ALI pirq entries are damn ugly, and completely undocumented.
 * This has been figured out from pirq tables, and it's not a pretty
 * picture.
 */
static int pirq_ali_get(struct pci_dev *router, struct pci_dev *dev, int pirq)
{
	static const unsigned char irqmap[16] = { 0, 9, 3, 10, 4, 5, 7, 6, 1, 11, 0, 12, 0, 14, 0, 15 };

	WARN_ON_ONCE(pirq > 16);
	return irqmap[read_config_nybble(router, 0x48, pirq-1)];
}

static int pirq_ali_set(struct pci_dev *router, struct pci_dev *dev, int pirq, int irq)
{
	static const unsigned char irqmap[16] = { 0, 8, 0, 2, 4, 5, 7, 6, 0, 1, 3, 9, 11, 0, 13, 15 };
	unsigned int val = irqmap[irq];

	WARN_ON_ONCE(pirq > 16);
	if (val) {
		write_config_nybble(router, 0x48, pirq-1, val);
		return 1;
	}
	return 0;
}

/*
 * The Intel PIIX4 pirq rules are fairly simple: "pirq" is
 * just a pointer to the config space.
 */
static int pirq_piix_get(struct pci_dev *router, struct pci_dev *dev, int pirq)
{
	u8 x;

	pci_read_config_byte(router, pirq, &x);
	return (x < 16) ? x : 0;
}

static int pirq_piix_set(struct pci_dev *router, struct pci_dev *dev, int pirq, int irq)
{
	pci_write_config_byte(router, pirq, irq);
	return 1;
}

/*
 * The VIA pirq rules are nibble-based, like ALI,
 * but without the ugly irq number munging.
 * However, PIRQD is in the upper instead of lower 4 bits.
 */
static int pirq_via_get(struct pci_dev *router, struct pci_dev *dev, int pirq)
{
	return read_config_nybble(router, 0x55, pirq == 4 ? 5 : pirq);
}

static int pirq_via_set(struct pci_dev *router, struct pci_dev *dev, int pirq, int irq)
{
	write_config_nybble(router, 0x55, pirq == 4 ? 5 : pirq, irq);
	return 1;
}

/*
 * The VIA pirq rules are nibble-based, like ALI,
 * but without the ugly irq number munging.
 * However, for 82C586, nibble map is different .
 */
static int pirq_via586_get(struct pci_dev *router, struct pci_dev *dev, int pirq)
{
	static const unsigned int pirqmap[5] = { 3, 2, 5, 1, 1 };

	WARN_ON_ONCE(pirq > 5);
	return read_config_nybble(router, 0x55, pirqmap[pirq-1]);
}

static int pirq_via586_set(struct pci_dev *router, struct pci_dev *dev, int pirq, int irq)
{
	static const unsigned int pirqmap[5] = { 3, 2, 5, 1, 1 };

	WARN_ON_ONCE(pirq > 5);
	write_config_nybble(router, 0x55, pirqmap[pirq-1], irq);
	return 1;
}

/*
 * ITE 8330G pirq rules are nibble-based
 * FIXME: pirqmap may be { 1, 0, 3, 2 },
 * 	  2+3 are both mapped to irq 9 on my system
 */
static int pirq_ite_get(struct pci_dev *router, struct pci_dev *dev, int pirq)
{
	static const unsigned char pirqmap[4] = { 1, 0, 2, 3 };

	WARN_ON_ONCE(pirq > 4);
	return read_config_nybble(router, 0x43, pirqmap[pirq-1]);
}

static int pirq_ite_set(struct pci_dev *router, struct pci_dev *dev, int pirq, int irq)
{
	static const unsigned char pirqmap[4] = { 1, 0, 2, 3 };

	WARN_ON_ONCE(pirq > 4);
	write_config_nybble(router, 0x43, pirqmap[pirq-1], irq);
	return 1;
}

/*
 * OPTI: high four bits are nibble pointer..
 * I wonder what the low bits do?
 */
static int pirq_opti_get(struct pci_dev *router, struct pci_dev *dev, int pirq)
{
	return read_config_nybble(router, 0xb8, pirq >> 4);
}

static int pirq_opti_set(struct pci_dev *router, struct pci_dev *dev, int pirq, int irq)
{
	write_config_nybble(router, 0xb8, pirq >> 4, irq);
	return 1;
}

/*
 * Cyrix: nibble offset 0x5C
 * 0x5C bits 7:4 is INTB bits 3:0 is INTA
 * 0x5D bits 7:4 is INTD bits 3:0 is INTC
 */
static int pirq_cyrix_get(struct pci_dev *router, struct pci_dev *dev, int pirq)
{
	return read_config_nybble(router, 0x5C, (pirq-1)^1);
}

static int pirq_cyrix_set(struct pci_dev *router, struct pci_dev *dev, int pirq, int irq)
{
	write_config_nybble(router, 0x5C, (pirq-1)^1, irq);
	return 1;
}

/*
 *	PIRQ routing for SiS 85C503 router used in several SiS chipsets.
 *	We have to deal with the following issues here:
 *	- vendors have different ideas about the meaning of link values
 *	- some onboard devices (integrated in the chipset) have special
 *	  links and are thus routed differently (i.e. not via PCI INTA-INTD)
 *	- different revision of the router have a different layout for
 *	  the routing registers, particularly for the onchip devices
 *
 *	For all routing registers the common thing is we have one byte
 *	per routeable link which is defined as:
 *		 bit 7      IRQ mapping enabled (0) or disabled (1)
 *		 bits [6:4] reserved (sometimes used for onchip devices)
 *		 bits [3:0] IRQ to map to
 *		     allowed: 3-7, 9-12, 14-15
 *		     reserved: 0, 1, 2, 8, 13
 *
 *	The config-space registers located at 0x41/0x42/0x43/0x44 are
 *	always used to route the normal PCI INT A/B/C/D respectively.
 *	Apparently there are systems implementing PCI routing table using
 *	link values 0x01-0x04 and others using 0x41-0x44 for PCI INTA..D.
 *	We try our best to handle both link mappings.
 *
 *	Currently (2003-05-21) it appears most SiS chipsets follow the
 *	definition of routing registers from the SiS-5595 southbridge.
 *	According to the SiS 5595 datasheets the revision id's of the
 *	router (ISA-bridge) should be 0x01 or 0xb0.
 *
 *	Furthermore we've also seen lspci dumps with revision 0x00 and 0xb1.
 *	Looks like these are used in a number of SiS 5xx/6xx/7xx chipsets.
 *	They seem to work with the current routing code. However there is
 *	some concern because of the two USB-OHCI HCs (original SiS 5595
 *	had only one). YMMV.
 *
 *	Onchip routing for router rev-id 0x01/0xb0 and probably 0x00/0xb1:
 *
 *	0x61:	IDEIRQ:
 *		bits [6:5] must be written 01
 *		bit 4 channel-select primary (0), secondary (1)
 *
 *	0x62:	USBIRQ:
 *		bit 6 OHCI function disabled (0), enabled (1)
 *
 *	0x6a:	ACPI/SCI IRQ: bits 4-6 reserved
 *
 *	0x7e:	Data Acq. Module IRQ - bits 4-6 reserved
 *
 *	We support USBIRQ (in addition to INTA-INTD) and keep the
 *	IDE, ACPI and DAQ routing untouched as set by the BIOS.
 *
 *	Currently the only reported exception is the new SiS 65x chipset
 *	which includes the SiS 69x southbridge. Here we have the 85C503
 *	router revision 0x04 and there are changes in the register layout
 *	mostly related to the different USB HCs with USB 2.0 support.
 *
 *	Onchip routing for router rev-id 0x04 (try-and-error observation)
 *
 *	0x60/0x61/0x62/0x63:	1xEHCI and 3xOHCI (companion) USB-HCs
 *				bit 6-4 are probably unused, not like 5595
 */

#define PIRQ_SIS_IRQ_MASK	0x0f
#define PIRQ_SIS_IRQ_DISABLE	0x80
#define PIRQ_SIS_USB_ENABLE	0x40

static int pirq_sis_get(struct pci_dev *router, struct pci_dev *dev, int pirq)
{
	u8 x;
	int reg;

	reg = pirq;
	if (reg >= 0x01 && reg <= 0x04)
		reg += 0x40;
	pci_read_config_byte(router, reg, &x);
	return (x & PIRQ_SIS_IRQ_DISABLE) ? 0 : (x & PIRQ_SIS_IRQ_MASK);
}

static int pirq_sis_set(struct pci_dev *router, struct pci_dev *dev, int pirq, int irq)
{
	u8 x;
	int reg;

	reg = pirq;
	if (reg >= 0x01 && reg <= 0x04)
		reg += 0x40;
	pci_read_config_byte(router, reg, &x);
	x &= ~(PIRQ_SIS_IRQ_MASK | PIRQ_SIS_IRQ_DISABLE);
	x |= irq ? irq: PIRQ_SIS_IRQ_DISABLE;
	pci_write_config_byte(router, reg, x);
	return 1;
}


/*
 * VLSI: nibble offset 0x74 - educated guess due to routing table and
 *       config space of VLSI 82C534 PCI-bridge/router (1004:0102)
 *       Tested on HP OmniBook 800 covering PIRQ 1, 2, 4, 8 for onboard
 *       devices, PIRQ 3 for non-pci(!) soundchip and (untested) PIRQ 6
 *       for the busbridge to the docking station.
 */

static int pirq_vlsi_get(struct pci_dev *router, struct pci_dev *dev, int pirq)
{
	WARN_ON_ONCE(pirq >= 9);
	if (pirq > 8) {
		dev_info(&dev->dev, "VLSI router PIRQ escape (%d)\n", pirq);
		return 0;
	}
	return read_config_nybble(router, 0x74, pirq-1);
}

static int pirq_vlsi_set(struct pci_dev *router, struct pci_dev *dev, int pirq, int irq)
{
	WARN_ON_ONCE(pirq >= 9);
	if (pirq > 8) {
		dev_info(&dev->dev, "VLSI router PIRQ escape (%d)\n", pirq);
		return 0;
	}
	write_config_nybble(router, 0x74, pirq-1, irq);
	return 1;
}

/*
 * ServerWorks: PCI interrupts mapped to system IRQ lines through Index
 * and Redirect I/O registers (0x0c00 and 0x0c01).  The Index register
 * format is (PCIIRQ## | 0x10), e.g.: PCIIRQ10=0x1a.  The Redirect
 * register is a straight binary coding of desired PIC IRQ (low nibble).
 *
 * The 'link' value in the PIRQ table is already in the correct format
 * for the Index register.  There are some special index values:
 * 0x00 for ACPI (SCI), 0x01 for USB, 0x02 for IDE0, 0x04 for IDE1,
 * and 0x03 for SMBus.
 */
static int pirq_serverworks_get(struct pci_dev *router, struct pci_dev *dev, int pirq)
{
	outb(pirq, 0xc00);
	return inb(0xc01) & 0xf;
}

static int pirq_serverworks_set(struct pci_dev *router, struct pci_dev *dev,
	int pirq, int irq)
{
	outb(pirq, 0xc00);
	outb(irq, 0xc01);
	return 1;
}

/* Support for AMD756 PCI IRQ Routing
 * Jhon H. Caicedo <jhcaiced@osso.org.co>
 * Jun/21/2001 0.2.0 Release, fixed to use "nybble" functions... (jhcaiced)
 * Jun/19/2001 Alpha Release 0.1.0 (jhcaiced)
 * The AMD756 pirq rules are nibble-based
 * offset 0x56 0-3 PIRQA  4-7  PIRQB
 * offset 0x57 0-3 PIRQC  4-7  PIRQD
 */
static int pirq_amd756_get(struct pci_dev *router, struct pci_dev *dev, int pirq)
{
	u8 irq;
	irq = 0;
	if (pirq <= 4)
		irq = read_config_nybble(router, 0x56, pirq - 1);
	dev_info(&dev->dev,
		 "AMD756: dev [%04x:%04x], router PIRQ %d get IRQ %d\n",
		 dev->vendor, dev->device, pirq, irq);
	return irq;
}

static int pirq_amd756_set(struct pci_dev *router, struct pci_dev *dev, int pirq, int irq)
{
	dev_info(&dev->dev,
		 "AMD756: dev [%04x:%04x], router PIRQ %d set IRQ %d\n",
		 dev->vendor, dev->device, pirq, irq);
	if (pirq <= 4)
		write_config_nybble(router, 0x56, pirq - 1, irq);
	return 1;
}

/*
 * PicoPower PT86C523
 */
static int pirq_pico_get(struct pci_dev *router, struct pci_dev *dev, int pirq)
{
	outb(0x10 + ((pirq - 1) >> 1), 0x24);
	return ((pirq - 1) & 1) ? (inb(0x26) >> 4) : (inb(0x26) & 0xf);
}

static int pirq_pico_set(struct pci_dev *router, struct pci_dev *dev, int pirq,
			int irq)
{
	unsigned int x;
	outb(0x10 + ((pirq - 1) >> 1), 0x24);
	x = inb(0x26);
	x = ((pirq - 1) & 1) ? ((x & 0x0f) | (irq << 4)) : ((x & 0xf0) | (irq));
	outb(x, 0x26);
	return 1;
}

#ifdef CONFIG_PCI_BIOS

static int pirq_bios_set(struct pci_dev *router, struct pci_dev *dev, int pirq, int irq)
{
	struct pci_dev *bridge;
	int pin = pci_get_interrupt_pin(dev, &bridge);
	return pcibios_set_irq_routing(bridge, pin - 1, irq);
}

#endif

//该函数基本上罗列了intel中断路由器的各种可能设备id
static __init int intel_router_probe(struct irq_router *r, struct pci_dev *router, u16 device)
{
	static struct pci_device_id __initdata pirq_440gx[] = {
		{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82443GX_0) },
		{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82443GX_2) },
		{ },
	};

	/* 440GX has a proprietary PIRQ router -- don't use it */
	if (pci_dev_present(pirq_440gx))
		return 0;

	switch (device) {
	case PCI_DEVICE_ID_INTEL_82371FB_0:
	case PCI_DEVICE_ID_INTEL_82371SB_0:
	case PCI_DEVICE_ID_INTEL_82371AB_0:
	case PCI_DEVICE_ID_INTEL_82371MX:
	case PCI_DEVICE_ID_INTEL_82443MX_0:
	case PCI_DEVICE_ID_INTEL_82801AA_0:
	case PCI_DEVICE_ID_INTEL_82801AB_0:
	case PCI_DEVICE_ID_INTEL_82801BA_0:
	case PCI_DEVICE_ID_INTEL_82801BA_10:
	case PCI_DEVICE_ID_INTEL_82801CA_0:
	case PCI_DEVICE_ID_INTEL_82801CA_12:
	case PCI_DEVICE_ID_INTEL_82801DB_0:
	case PCI_DEVICE_ID_INTEL_82801E_0:
	case PCI_DEVICE_ID_INTEL_82801EB_0:
	case PCI_DEVICE_ID_INTEL_ESB_1:
	case PCI_DEVICE_ID_INTEL_ICH6_0:
	case PCI_DEVICE_ID_INTEL_ICH6_1:
	case PCI_DEVICE_ID_INTEL_ICH7_0:
	case PCI_DEVICE_ID_INTEL_ICH7_1:
	case PCI_DEVICE_ID_INTEL_ICH7_30:
	case PCI_DEVICE_ID_INTEL_ICH7_31:
	case PCI_DEVICE_ID_INTEL_TGP_LPC:
	case PCI_DEVICE_ID_INTEL_ESB2_0:
	case PCI_DEVICE_ID_INTEL_ICH8_0:
	case PCI_DEVICE_ID_INTEL_ICH8_1:
	case PCI_DEVICE_ID_INTEL_ICH8_2:
	case PCI_DEVICE_ID_INTEL_ICH8_3:
	case PCI_DEVICE_ID_INTEL_ICH8_4:
	case PCI_DEVICE_ID_INTEL_ICH9_0:
	case PCI_DEVICE_ID_INTEL_ICH9_1:
	case PCI_DEVICE_ID_INTEL_ICH9_2:
	case PCI_DEVICE_ID_INTEL_ICH9_3:
	case PCI_DEVICE_ID_INTEL_ICH9_4:
	case PCI_DEVICE_ID_INTEL_ICH9_5:
	case PCI_DEVICE_ID_INTEL_TOLAPAI_0:
	case PCI_DEVICE_ID_INTEL_ICH10_0:
	case PCI_DEVICE_ID_INTEL_ICH10_1:
	case PCI_DEVICE_ID_INTEL_ICH10_2:
	case PCI_DEVICE_ID_INTEL_ICH10_3:
	case PCI_DEVICE_ID_INTEL_CPT_LPC1:
	case PCI_DEVICE_ID_INTEL_CPT_LPC2:
		r->name = "PIIX/ICH";
		r->get = pirq_piix_get;
		r->set = pirq_piix_set;
		return 1;
	}

	if ((device >= PCI_DEVICE_ID_INTEL_PCH_LPC_MIN) && 
		(device <= PCI_DEVICE_ID_INTEL_PCH_LPC_MAX)) 
	{
		r->name = "PIIX/ICH";
		r->get = pirq_piix_get;
		r->set = pirq_piix_set;
		return 1;
	}

	return 0;
}

static __init int via_router_probe(struct irq_router *r,
				struct pci_dev *router, u16 device)
{
	/* FIXME: We should move some of the quirk fixup stuff here */

	/*
	 * workarounds for some buggy BIOSes
	 */
	if (device == PCI_DEVICE_ID_VIA_82C586_0) {
		switch (router->device) {
		case PCI_DEVICE_ID_VIA_82C686:
			/*
			 * Asus k7m bios wrongly reports 82C686A
			 * as 586-compatible
			 */
			device = PCI_DEVICE_ID_VIA_82C686;
			break;
		case PCI_DEVICE_ID_VIA_8235:
			/**
			 * Asus a7v-x bios wrongly reports 8235
			 * as 586-compatible
			 */
			device = PCI_DEVICE_ID_VIA_8235;
			break;
		case PCI_DEVICE_ID_VIA_8237:
			/**
			 * Asus a7v600 bios wrongly reports 8237
			 * as 586-compatible
			 */
			device = PCI_DEVICE_ID_VIA_8237;
			break;
		}
	}

	switch (device) {
	case PCI_DEVICE_ID_VIA_82C586_0:
		r->name = "VIA";
		r->get = pirq_via586_get;
		r->set = pirq_via586_set;
		return 1;
	case PCI_DEVICE_ID_VIA_82C596:
	case PCI_DEVICE_ID_VIA_82C686:
	case PCI_DEVICE_ID_VIA_8231:
	case PCI_DEVICE_ID_VIA_8233A:
	case PCI_DEVICE_ID_VIA_8235:
	case PCI_DEVICE_ID_VIA_8237:
		/* FIXME: add new ones for 8233/5 */
		r->name = "VIA";
		r->get = pirq_via_get;
		r->set = pirq_via_set;
		return 1;
	}
	return 0;
}

static __init int vlsi_router_probe(struct irq_router *r, struct pci_dev *router, u16 device)
{
	switch (device) {
	case PCI_DEVICE_ID_VLSI_82C534:
		r->name = "VLSI 82C534";
		r->get = pirq_vlsi_get;
		r->set = pirq_vlsi_set;
		return 1;
	}
	return 0;
}


static __init int serverworks_router_probe(struct irq_router *r,
		struct pci_dev *router, u16 device)
{
	switch (device) {
	case PCI_DEVICE_ID_SERVERWORKS_OSB4:
	case PCI_DEVICE_ID_SERVERWORKS_CSB5:
		r->name = "ServerWorks";
		r->get = pirq_serverworks_get;
		r->set = pirq_serverworks_set;
		return 1;
	}
	return 0;
}

static __init int sis_router_probe(struct irq_router *r, struct pci_dev *router, u16 device)
{
	if (device != PCI_DEVICE_ID_SI_503)
		return 0;

	r->name = "SIS";
	r->get = pirq_sis_get;
	r->set = pirq_sis_set;
	return 1;
}

static __init int cyrix_router_probe(struct irq_router *r, struct pci_dev *router, u16 device)
{
	switch (device) {
	case PCI_DEVICE_ID_CYRIX_5520:
		r->name = "NatSemi";
		r->get = pirq_cyrix_get;
		r->set = pirq_cyrix_set;
		return 1;
	}
	return 0;
}

static __init int opti_router_probe(struct irq_router *r, struct pci_dev *router, u16 device)
{
	switch (device) {
	case PCI_DEVICE_ID_OPTI_82C700:
		r->name = "OPTI";
		r->get = pirq_opti_get;
		r->set = pirq_opti_set;
		return 1;
	}
	return 0;
}

static __init int ite_router_probe(struct irq_router *r, struct pci_dev *router, u16 device)
{
	switch (device) {
	case PCI_DEVICE_ID_ITE_IT8330G_0:
		r->name = "ITE";
		r->get = pirq_ite_get;
		r->set = pirq_ite_set;
		return 1;
	}
	return 0;
}

static __init int ali_router_probe(struct irq_router *r, struct pci_dev *router, u16 device)
{
	switch (device) {
	case PCI_DEVICE_ID_AL_M1533:
	case PCI_DEVICE_ID_AL_M1563:
		r->name = "ALI";
		r->get = pirq_ali_get;
		r->set = pirq_ali_set;
		return 1;
	}
	return 0;
}

static __init int amd_router_probe(struct irq_router *r, struct pci_dev *router, u16 device)
{
	switch (device) {
	case PCI_DEVICE_ID_AMD_VIPER_740B:
		r->name = "AMD756";
		break;
	case PCI_DEVICE_ID_AMD_VIPER_7413:
		r->name = "AMD766";
		break;
	case PCI_DEVICE_ID_AMD_VIPER_7443:
		r->name = "AMD768";
		break;
	default:
		return 0;
	}
	r->get = pirq_amd756_get;
	r->set = pirq_amd756_set;
	return 1;
}

static __init int pico_router_probe(struct irq_router *r, struct pci_dev *router, u16 device)
{
	switch (device) {
	case PCI_DEVICE_ID_PICOPOWER_PT86C523:
		r->name = "PicoPower PT86C523";
		r->get = pirq_pico_get;
		r->set = pirq_pico_set;
		return 1;

	case PCI_DEVICE_ID_PICOPOWER_PT86C523BBP:
		r->name = "PicoPower PT86C523 rev. BB+";
		r->get = pirq_pico_get;
		r->set = pirq_pico_set;
		return 1;
	}
	return 0;
}

//系统中可能存在不同的中断路由器 从而有多种驱动,每个厂商会有自己的中断路由器句柄
// linux将支持的所有路由器句柄放到全局数组pirq_routers中
static __initdata struct irq_router_handler pirq_routers[] = {
	{ PCI_VENDOR_ID_INTEL, intel_router_probe },//intel厂商
	{ PCI_VENDOR_ID_AL, ali_router_probe },
	{ PCI_VENDOR_ID_ITE, ite_router_probe },
	{ PCI_VENDOR_ID_VIA, via_router_probe },
	{ PCI_VENDOR_ID_OPTI, opti_router_probe },
	{ PCI_VENDOR_ID_SI, sis_router_probe },
	{ PCI_VENDOR_ID_CYRIX, cyrix_router_probe },
	{ PCI_VENDOR_ID_VLSI, vlsi_router_probe },
	{ PCI_VENDOR_ID_SERVERWORKS, serverworks_router_probe },
	{ PCI_VENDOR_ID_AMD, amd_router_probe },
	{ PCI_VENDOR_ID_PICOPOWER, pico_router_probe },
	/* Someone with docs needs to add the ATI Radeon IGP */
	{ 0, NULL }
};
static struct irq_router pirq_router;//中断路由器驱动
static struct pci_dev *pirq_router_dev;//中断路由器pci设备驱动


/*
 *	FIXME: should we have an option to say "generic for
 *	chipset" ?
 */
/*
查找中断路由驱动
*/
static void __init pirq_find_router(struct irq_router *r)
{
	struct irq_routing_table *rt = pirq_table;//中断路由表
	struct irq_router_handler *h;//中断路由句柄

#ifdef CONFIG_PCI_BIOS
	if (!rt->signature) //检查签名是否为空
	{ 
	    //通过bios 服务API来设置中断号
		printk(KERN_INFO "PCI: Using BIOS for IRQ routing\n");
		r->set = pirq_bios_set;
		r->name = "BIOS";
		return;
	}
#endif

	/* Default unless a driver reloads it */
	r->name = "default";
	r->get = NULL;
	r->set = NULL;

	DBG(KERN_DEBUG "PCI: Attempting to find IRQ router for [%04x:%04x]\n",
	    rt->rtr_vendor, rt->rtr_device);

    //根据pci总线编号和槽/功能号 获得中断路由器pci设备
	pirq_router_dev = pci_get_bus_and_slot(rt->rtr_bus, rt->rtr_devfn);
	if (!pirq_router_dev) 
	{
		DBG(KERN_DEBUG "PCI: Interrupt router not found at "
			"%02x:%02x\n", rt->rtr_bus, rt->rtr_devfn);
		return;
	}

    //查找中断路由器句柄表  看使用的哪种厂商的中断路由器
	for (h = pirq_routers; h->vendor; h++) {

		/* First look for a router match */
		if (rt->rtr_vendor == h->vendor &&
			h->probe(r, pirq_router_dev, rt->rtr_device))
			break;
		/* Fall back to a device match */
		if (pirq_router_dev->vendor == h->vendor &&
			h->probe(r, pirq_router_dev, pirq_router_dev->device))
			break;
	}
	dev_info(&pirq_router_dev->dev, "%s IRQ router [%04x:%04x]\n",
		 pirq_router.name,
		 pirq_router_dev->vendor, pirq_router_dev->device);

	/* The device remains referenced for the kernel lifetime */
}

static struct irq_info *pirq_get_info(struct pci_dev *dev)
{
	struct irq_routing_table *rt = pirq_table;
	//获得中断路由表项个数
	int entries = (rt->size - sizeof(struct irq_routing_table)) /sizeof(struct irq_info);
	struct irq_info *info;

    //遍历所有的中断路由表项  查找此设备对应的路由表项
	for (info = rt->slots; entries--; info++)
		if (info->bus == dev->bus->number &&
			PCI_SLOT(info->devfn) == PCI_SLOT(dev->devfn))
			return info;
	return NULL;
}

//给某个设备查找有效的irq号
static int pcibios_lookup_irq(struct pci_dev *dev, //pci设备描述符 
                                         int assign)
{
	u8 pin;
	struct irq_info *info;
	int i, pirq, newirq;
	int irq = 0;
	u32 mask;
	struct irq_router *r = &pirq_router;
	struct pci_dev *dev2 = NULL;
	char *msg = NULL;

	/* Find IRQ pin */
	//读取设备的中断引脚
	//00 表示没有中断引脚  01表示INTA# 0x02表示INTB#  0X03表示INTC# 0x04表示INTD#
	pci_read_config_byte(dev, PCI_INTERRUPT_PIN, &pin);
	if (!pin) {
		dev_dbg(&dev->dev, "no interrupt pin\n");
		return 0;
	}

	if (io_apic_assign_pci_irqs)
		return 0;

	/* Find IRQ routing entry */
    //看中断路由表是否为空
	if (!pirq_table)
		return 0;

	//获得该设备所属的插槽对应的irq_info中断路由表项
	info = pirq_get_info(dev);
	if (!info) {
		dev_dbg(&dev->dev, "PCI INT %c not found in routing table\n",
			'A' + pin - 1);
		return 0;
	}
	//获得pci设备中断引脚的链路值
	pirq = info->irq[pin - 1].link;
	//允许的irq位图 这个限制是硬件上的限制 在下面还要加上操作系统的限制
	mask = info->irq[pin - 1].bitmap;

	if (!pirq) {
		dev_dbg(&dev->dev, "PCI INT %c not routed\n", 'A' + pin - 1);
		return 0;
	}
	dev_dbg(&dev->dev, "PCI INT %c -> PIRQ %02x, mask %04x, excl %04x",
		'A' + pin - 1, pirq, mask, pirq_table->exclusive_irqs);

	//对位图的使用加上操作系统的限制
	mask &= pcibios_irq_mask;

	/* Work around broken HP Pavilion Notebooks which assign USB to
	   IRQ 9 even though it is actually wired to IRQ 11 */

	//这个是针对惠普的特殊设置
	if (broken_hp_bios_irq9 && pirq == 0x59 && dev->irq == 9) 
	{
		dev->irq = 11;
		pci_write_config_byte(dev, PCI_INTERRUPT_LINE, 11);
		r->set(pirq_router_dev, dev, pirq, 11);
	}

    //针对宏基的特殊设置
	/* same for Acer Travelmate 360, but with CB and irq 11 -> 10 */
	if (acer_tm360_irqrouting && dev->irq == 11 &&
		dev->vendor == PCI_VENDOR_ID_O2) {
		pirq = 0x68;
		mask = 0x400;
		dev->irq = r->get(pirq_router_dev, dev, pirq);
		pci_write_config_byte(dev, PCI_INTERRUPT_LINE, dev->irq);
	}


	/*
	 * Find the best IRQ to assign: use the one
	 * reported by the device if possible.
	 */
	//如果可能先使用设备中记录的irq  最后不一定用这个
	newirq = dev->irq;
    //如果irq存在 并且此irq不在允许的mask中
	if (newirq && !((1 << newirq) & mask)) 
	{
	    //如果编译选项设置了PCI_USE_PIRQ_MASK 则在和mask冲突的情况下将 irq设置为0重新分配
		if (pci_probe & PCI_USE_PIRQ_MASK)
			newirq = 0;
		else  //编译选项没有设置PCI_USE_PIRQ_MASK 则忽略和mask冲突的问题  只是打出一个警告
			dev_warn(&dev->dev, "IRQ %d doesn't match PIRQ mask "
				 "%#x; try pci=usepirqmask\n", newirq, mask);
	}

	//如果irq为0  并且参数中指定要进行分配  则进行分配新的irq
	if (!newirq && assign) 
	{
		for (i = 0; i < 16; i++) 
		{
		   //检查掩码中是否允许此中断号
			if (!(mask & (1 << i)))
				continue;

			//寻找惩罚量最小的irq号 进行分配 并将分配号的irq存放到newirq中
			if (pirq_penalty[i] < pirq_penalty[newirq] &&can_request_irq(i, IRQF_SHARED))
				newirq = i;
		}
	}
	dev_dbg(&dev->dev, "PCI INT %c -> newirq %d", 'A' + pin - 1, newirq);

    
	/* Check if it is hardcoded */
	//如果链路值的高四位为1 则说明它是个硬链接  也就是直接连在了8259A的引脚上
	//引脚编号即是低四位
	if ((pirq & 0xf0) == 0xf0) 
	{
		irq = pirq & 0xf;
		msg = "hardcoded";
	} //调用中断路由器回调获得irq  回调看pirq_routers
	else if (r->get && (irq = r->get(pirq_router_dev, dev, pirq)) && \
	((!(pci_probe & PCI_USE_PIRQ_MASK)) || ((1 << irq) & mask))) 
	{
		msg = "found";
		eisa_set_level_irq(irq);
	} 
	else if (newirq && r->set &&
		    (dev->class >> 8) != PCI_CLASS_DISPLAY_VGA) 
    {    
        //调用中断路由驱动设置irq  回调看pirq_routers
		if (r->set(pirq_router_dev, dev, pirq, newirq)) 
		{
			eisa_set_level_irq(newirq);
			msg = "assigned";
			irq = newirq;
		}
	}

	if (!irq) 
	{
		if (newirq && mask == (1 << newirq)) 
		{
			msg = "guessed";
			irq = newirq;
		} else {
			dev_dbg(&dev->dev, "can't route interrupt\n");
			return 0;
		}
	}
	dev_info(&dev->dev, "%s PCI INT %c -> IRQ %d\n", msg, 'A' + pin - 1, irq);

	/* Update IRQ for all devices with the same pirq value */
    //遍历所有的pci设备
	while ((dev2 = pci_get_device(PCI_ANY_ID, PCI_ANY_ID, dev2)) != NULL) 
	{
	    
		pci_read_config_byte(dev2, PCI_INTERRUPT_PIN, &pin);
		if (!pin)
			continue;

		info = pirq_get_info(dev2);
		if (!info)
			continue;

		//更根据上面获得的设备链路值  看是否和pirq相同
		if (info->irq[pin - 1].link == pirq) 
		{
			/*
			 * We refuse to override the dev->irq
			 * information. Give a warning!
			 */
			 //设备有自己有效的irq
			if (dev2->irq && dev2->irq != irq && \
			(!(pci_probe & PCI_USE_PIRQ_MASK) || \
			((1 << dev2->irq) & mask))) {
#ifndef CONFIG_PCI_MSI
				dev_info(&dev2->dev, "IRQ routing conflict: "
					 "have IRQ %d, want IRQ %d\n",
					 dev2->irq, irq);
#endif
				continue;
			}
			
			//更新设备的irq
			dev2->irq = irq;
			pirq_penalty[irq]++;
			if (dev != dev2)
				dev_info(&dev->dev, "sharing IRQ %d with %s\n",
					 irq, pci_name(dev2));
		}
	}
	return 1;
}
/*
给pci设备分配irq中断号
*/
static void __init pcibios_fixup_irqs(void)
{
	struct pci_dev *dev = NULL;
	u8 pin;

	DBG(KERN_DEBUG "PCI: IRQ fixup\n");
	//遍历所有的pci设备
	while ((dev = pci_get_device(PCI_ANY_ID, PCI_ANY_ID, dev)) != NULL) 
	{
		/*
		 * If the BIOS has set an out of range IRQ number, just
		 * ignore it.  Also keep track of which IRQ's are
		 * already in use.
		 */
		//该设备使用的irq号是否超出限制
		if (dev->irq >= 16) 
		{
			dev_dbg(&dev->dev, "ignoring bogus IRQ %d\n", dev->irq);
			dev->irq = 0;
		}
		/*
		 * If the IRQ is already assigned to a PCI device,
		 * ignore its ISA use penalty
		 */
		//如果不希望被设备使用的irq已经被被使用则干脆就消掉对它的惩罚 让他正常参与到下面的irq分配中 
		if (pirq_penalty[dev->irq] >= 100 && pirq_penalty[dev->irq] < 100000)
			pirq_penalty[dev->irq] = 0;
		
		//将被使用的irq对应的惩罚值加1
		pirq_penalty[dev->irq]++;
	}

	if (io_apic_assign_pci_irqs)
		return;

    //下面对irq进行分配
	dev = NULL;
	
    //遍历所有的pci设备
	while ((dev = pci_get_device(PCI_ANY_ID, PCI_ANY_ID, dev)) != NULL) 
	{ 
	    //读取pci设备的配置空间  读取该设备使用的中断引脚
		pci_read_config_byte(dev, PCI_INTERRUPT_PIN, &pin);
		if (!pin)
			continue;

		/*
		 * Still no IRQ? Try to lookup one...
		 */
		//它当前是否有一个有效的irq 如果有中断引脚 但没有对应的irq号 则进行分配
		if (!dev->irq)
			pcibios_lookup_irq(dev, 0);
	}
}

/*
 * Work around broken HP Pavilion Notebooks which assign USB to
 * IRQ 9 even though it is actually wired to IRQ 11
 */
static int __init fix_broken_hp_bios_irq9(const struct dmi_system_id *d)
{
	if (!broken_hp_bios_irq9) {
		broken_hp_bios_irq9 = 1;
		printk(KERN_INFO "%s detected - fixing broken IRQ routing\n",
			d->ident);
	}
	return 0;
}

/*
 * Work around broken Acer TravelMate 360 Notebooks which assign
 * Cardbus to IRQ 11 even though it is actually wired to IRQ 10
 */
static int __init fix_acer_tm360_irqrouting(const struct dmi_system_id *d)
{
	if (!acer_tm360_irqrouting) {
		acer_tm360_irqrouting = 1;
		printk(KERN_INFO "%s detected - fixing broken IRQ routing\n",
			d->ident);
	}
	return 0;
}

static struct dmi_system_id __initdata pciirq_dmi_table[] = {
	{
		.callback = fix_broken_hp_bios_irq9,
		.ident = "HP Pavilion N5400 Series Laptop",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Hewlett-Packard"),
			DMI_MATCH(DMI_BIOS_VERSION, "GE.M1.03"),
			DMI_MATCH(DMI_PRODUCT_VERSION,
				"HP Pavilion Notebook Model GE"),
			DMI_MATCH(DMI_BOARD_VERSION, "OmniBook N32N-736"),
		},
	},
	{
		.callback = fix_acer_tm360_irqrouting,
		.ident = "Acer TravelMate 36x Laptop",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "TravelMate 360"),
		},
	},
	{ }
};

//pci中断路由路由的入口函数
int __init pcibios_irq_init(void)
{
	DBG(KERN_DEBUG "PCI: IRQ init\n");

	if (pcibios_enable_irq || raw_pci_ops == NULL)
		return 0;

	dmi_check_system(pciirq_dmi_table);

	//查找中断路由表 通过遍历BIOS ROM空间的方式
	pirq_table = pirq_find_routing_table();
    
#ifdef CONFIG_PCI_BIOS
    //如果上面遍历 BIOS rom 空间获得中断路由表失败 ，则在配置CONFIG_PCI_BIOS的情况下
    //尝试通过BIOS服务目录的 API获得路由表
	if (!pirq_table && (pci_probe & PCI_BIOS_IRQ_SCAN))
		pirq_table = pcibios_get_irq_routing_table();
#endif

	if (pirq_table) 
	{
	    //路由表中列出了所有的pci插槽,检查这些插槽所在的pci总线是否都已经扫描到了
	    //如果没有扫描到 则进行扫描
		pirq_peer_trick();

		//查找可编程中断路由器的驱动 将他保存到pirq_router全局变量中
		//查找中断路由器驱动的目的是用于获得为PCI设备中断设置IRQ的回调函数
		pirq_find_router(&pirq_router);


	    //如果中断路由表给出了排它irq位图 则需要为各个irq的惩罚量进行修改 它在操作系统为链路值选择irq的算法中用到
	    //将位图设置为1所对应的irq的惩罚量增加100 使得在选择中断路由的时候 不太会选择对应的irq
		if (pirq_table->exclusive_irqs) 
		{
			int i;
			for (i = 0; i < 16; i++)
				//如果某位设置了1 说明该irq号不想被共享  将惩罚量加100 
				if (!(pirq_table->exclusive_irqs & (1 << i)))
					pirq_penalty[i] += 100;
		}
		/*
		 * If we're using the I/O APIC, avoid using the PCI IRQ
		 * routing table
		 */
		if (io_apic_assign_pci_irqs)
			pirq_table = NULL;
	}

	pcibios_enable_irq = pirq_enable_irq;

    //给pci设备分配irq中断号
	pcibios_fixup_irqs();

	if (io_apic_assign_pci_irqs && pci_routeirq) {
		struct pci_dev *dev = NULL;
		/*
		 * PCI IRQ routing is set up by pci_enable_device(), but we
		 * also do it here in case there are still broken drivers that
		 * don't use pci_enable_device().
		 */
		printk(KERN_INFO "PCI: Routing PCI interrupts for all devices because \"pci=routeirq\" specified\n");
		for_each_pci_dev(dev)
			pirq_enable_irq(dev);
	}

	return 0;
}

static void pirq_penalize_isa_irq(int irq, int active)
{
	/*
	 *  If any ISAPnP device reports an IRQ in its list of possible
	 *  IRQ's, we try to avoid assigning it to PCI devices.
	 */
	if (irq < 16) {
		if (active)
			pirq_penalty[irq] += 1000;
		else
			pirq_penalty[irq] += 100;
	}
}

void pcibios_penalize_isa_irq(int irq, int active)
{
#ifdef CONFIG_ACPI
	if (!acpi_noirq)
		acpi_penalize_isa_irq(irq, active);
	else
#endif
		pirq_penalize_isa_irq(irq, active);
}

static int pirq_enable_irq(struct pci_dev *dev)
{
	u8 pin;

	pci_read_config_byte(dev, PCI_INTERRUPT_PIN, &pin);
	if (pin && !pcibios_lookup_irq(dev, 1)) {
		char *msg = "";

		if (!io_apic_assign_pci_irqs && dev->irq)
			return 0;

		if (io_apic_assign_pci_irqs) {
#ifdef CONFIG_X86_IO_APIC
			struct pci_dev *temp_dev;
			int irq;
			struct io_apic_irq_attr irq_attr;

			irq = IO_APIC_get_PCI_irq_vector(dev->bus->number,
						PCI_SLOT(dev->devfn),
						pin - 1, &irq_attr);
			/*
			 * Busses behind bridges are typically not listed in the MP-table.
			 * In this case we have to look up the IRQ based on the parent bus,
			 * parent slot, and pin number. The SMP code detects such bridged
			 * busses itself so we should get into this branch reliably.
			 */
			temp_dev = dev;
			while (irq < 0 && dev->bus->parent) { /* go back to the bridge */
				struct pci_dev *bridge = dev->bus->self;

				pin = pci_swizzle_interrupt_pin(dev, pin);
				irq = IO_APIC_get_PCI_irq_vector(bridge->bus->number,
						PCI_SLOT(bridge->devfn),
						pin - 1, &irq_attr);
				if (irq >= 0)
					dev_warn(&dev->dev, "using bridge %s "
						 "INT %c to get IRQ %d\n",
						 pci_name(bridge), 'A' + pin - 1,
						 irq);
				dev = bridge;
			}
			dev = temp_dev;
			if (irq >= 0) {
				io_apic_set_pci_routing(&dev->dev, irq,
							 &irq_attr);
				dev->irq = irq;
				dev_info(&dev->dev, "PCI->APIC IRQ transform: "
					 "INT %c -> IRQ %d\n", 'A' + pin - 1, irq);
				return 0;
			} else
				msg = "; probably buggy MP table";
#endif
		} else if (pci_probe & PCI_BIOS_IRQ_SCAN)
			msg = "";
		else
			msg = "; please try using pci=biosirq";

		/*
		 * With IDE legacy devices the IRQ lookup failure is not
		 * a problem..
		 */
		if (dev->class >> 8 == PCI_CLASS_STORAGE_IDE &&
				!(dev->class & 0x5))
			return 0;

		dev_warn(&dev->dev, "can't find IRQ for PCI INT %c%s\n",
			 'A' + pin - 1, msg);
	}
	return 0;
}
