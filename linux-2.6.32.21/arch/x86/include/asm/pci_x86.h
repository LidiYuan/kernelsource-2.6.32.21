/*
 *	Low-Level PCI Access for i386 machines.
 *
 *	(c) 1999 Martin Mares <mj@ucw.cz>
 */

#undef DEBUG

#ifdef DEBUG
#define DBG(x...) printk(x)
#else
#define DBG(x...)
#endif

#define PCI_PROBE_BIOS		0x0001
#define PCI_PROBE_CONF1		0x0002
#define PCI_PROBE_CONF2		0x0004
#define PCI_PROBE_MMCONF	0x0008
#define PCI_PROBE_MASK		0x000f
#define PCI_PROBE_NOEARLY	0x0010

#define PCI_NO_CHECKS		0x0400
#define PCI_USE_PIRQ_MASK	0x0800
#define PCI_ASSIGN_ROMS		0x1000
#define PCI_BIOS_IRQ_SCAN	0x2000
#define PCI_ASSIGN_ALL_BUSSES	0x4000
#define PCI_CAN_SKIP_ISA_ALIGN	0x8000
#define PCI_USE__CRS		0x10000
#define PCI_CHECK_ENABLE_AMD_MMCONF	0x20000
#define PCI_HAS_IO_ECS		0x40000
#define PCI_NOASSIGN_ROMS	0x80000

extern unsigned int pci_probe;
extern unsigned long pirq_table_addr;

enum pci_bf_sort_state {
	pci_bf_sort_default,
	pci_force_nobf,
	pci_force_bf,
	pci_dmi_bf,
};

/* pci-i386.c */

extern unsigned int pcibios_max_latency;

void pcibios_resource_survey(void);

/* pci-pc.c */

extern int pcibios_last_bus;
extern struct pci_bus *pci_root_bus;
extern struct pci_ops pci_root_ops;

/* pci-irq.c */

struct irq_info {
	u8 bus,             //总线编号
	   devfn;			 /* Bus, device and function 中断路由器所在的插槽/功能号*/
	struct {
		u8 link;		 /* IRQ line ID, chipset dependent, 链路值 依赖于芯片组 0表示未路由
					        0 = not routed */
		u16 bitmap;		 /* Available IRQs 允许使用的irq位图*/
	} __attribute__((packed)) irq[4]; //每个插槽有INTA#-INTD#四个中断引脚
	u8 slot;			/* Slot number, 0=onboard 1表示母版上的一个物理插槽  0表示这个插槽是主板上的集成设备*/
	u8 rfu;        //保留
} __attribute__((packed));


/*中断路由表*/
struct irq_routing_table {
	u32 signature;			/* PIRQ_SIGNATURE should be here 签名*/
	u16 version;			/* PIRQ_VERSION 版本号 必须为1.0*/
	u16 size;			/* Table size in bytes 以字节为单位的表长度*/
	u8 rtr_bus,            //中断路由器所在的总线编号 
	   rtr_devfn;		/* Where the interrupt router lies 
                          //中断路由器所在的插槽/功能号
	                      */
	u16 exclusive_irqs;		/* IRQs devoted exclusively to PCI usage 排他性irq位图
                               一共16位  每位对应一个irq号(8259a级联 一共16个)  若有个位置1说明 该irq号被专用 不能共享 对应的惩罚量加100(看pirq_penalty)
                               对应的代码在pcibios_irq_init
						    
	u16 rtr_vendor,   //中断路由器的厂商id
		rtr_device;	/* Vendor and device ID of
					   interrupt router 中断路由器的设备id*/
	u32 miniport_data;		/* Crap 没有被用到*/
	u8 rfu[11];       //保留将来使用
	u8 checksum;			/* Modulo 256 checksum must give 0  校验和*/
	struct irq_info slots[0]; //中断路由表项  每个pci插槽占有一项
} __attribute__((packed));

extern unsigned int pcibios_irq_mask;

extern int pcibios_scanned;
extern spinlock_t pci_config_lock;

extern int (*pcibios_enable_irq)(struct pci_dev *dev);
extern void (*pcibios_disable_irq)(struct pci_dev *dev);

struct pci_raw_ops {
	int (*read)(unsigned int domain, unsigned int bus, unsigned int devfn,
						int reg, int len, u32 *val);
	int (*write)(unsigned int domain, unsigned int bus, unsigned int devfn,
						int reg, int len, u32 val);
};

extern struct pci_raw_ops *raw_pci_ops;
extern struct pci_raw_ops *raw_pci_ext_ops;

extern struct pci_raw_ops pci_direct_conf1;
extern bool port_cf9_safe;

/* arch_initcall level */
extern int pci_direct_probe(void);
extern void pci_direct_init(int type);
extern void pci_pcbios_init(void);
extern int pci_olpc_init(void);
extern void __init dmi_check_pciprobe(void);
extern void __init dmi_check_skip_isa_align(void);

/* some common used subsys_initcalls */
extern int __init pci_acpi_init(void);
extern int __init pcibios_irq_init(void);
extern int __init pci_visws_init(void);
extern int __init pci_numaq_init(void);
extern int __init pcibios_init(void);

/* pci-mmconfig.c */

extern int __init pci_mmcfg_arch_init(void);
extern void __init pci_mmcfg_arch_free(void);

extern struct acpi_mcfg_allocation *pci_mmcfg_config;
extern int pci_mmcfg_config_num;

/*
 * AMD Fam10h CPUs are buggy, and cannot access MMIO config space
 * on their northbrige except through the * %eax register. As such, you MUST
 * NOT use normal IOMEM accesses, you need to only use the magic mmio-config
 * accessor functions.
 * In fact just use pci_config_*, nothing else please.
 */
static inline unsigned char mmio_config_readb(void __iomem *pos)
{
	u8 val;
	asm volatile("movb (%1),%%al" : "=a" (val) : "r" (pos));
	return val;
}

static inline unsigned short mmio_config_readw(void __iomem *pos)
{
	u16 val;
	asm volatile("movw (%1),%%ax" : "=a" (val) : "r" (pos));
	return val;
}

static inline unsigned int mmio_config_readl(void __iomem *pos)
{
	u32 val;
	asm volatile("movl (%1),%%eax" : "=a" (val) : "r" (pos));
	return val;
}

static inline void mmio_config_writeb(void __iomem *pos, u8 val)
{
	asm volatile("movb %%al,(%1)" : : "a" (val), "r" (pos) : "memory");
}

static inline void mmio_config_writew(void __iomem *pos, u16 val)
{
	asm volatile("movw %%ax,(%1)" : : "a" (val), "r" (pos) : "memory");
}

static inline void mmio_config_writel(void __iomem *pos, u32 val)
{
	asm volatile("movl %%eax,(%1)" : : "a" (val), "r" (pos) : "memory");
}
