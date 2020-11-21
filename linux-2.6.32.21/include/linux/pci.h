/*
 *	pci.h
 *
 *	PCI defines and function prototypes
 *	Copyright 1994, Drew Eckhardt
 *	Copyright 1997--1999 Martin Mares <mj@ucw.cz>
 *
 *	For more information, please consult the following manuals (look at
 *	http://www.pcisig.com/ for how to get them):
 *
 *	PCI BIOS Specification
 *	PCI Local Bus Specification
 *	PCI to PCI Bridge Specification
 *	PCI System Design Guide
 */

#ifndef LINUX_PCI_H
#define LINUX_PCI_H

#include <linux/pci_regs.h>	/* The pci register defines */

/*
 * The PCI interface treats multi-function devices as independent
 * devices.  The slot/function address of each device is encoded
 * in a single byte as follows:
 *
 *	7:3 = slot
 *	2:0 = function
 */
#define PCI_DEVFN(slot, func)	((((slot) & 0x1f) << 3) | ((func) & 0x07))
#define PCI_SLOT(devfn)		(((devfn) >> 3) & 0x1f)
#define PCI_FUNC(devfn)		((devfn) & 0x07)

/* Ioctls for /proc/bus/pci/X/Y nodes. */
#define PCIIOC_BASE		('P' << 24 | 'C' << 16 | 'I' << 8)
#define PCIIOC_CONTROLLER	(PCIIOC_BASE | 0x00)	/* Get controller for PCI device. */
#define PCIIOC_MMAP_IS_IO	(PCIIOC_BASE | 0x01)	/* Set mmap state to I/O space. */
#define PCIIOC_MMAP_IS_MEM	(PCIIOC_BASE | 0x02)	/* Set mmap state to MEM space. */
#define PCIIOC_WRITE_COMBINE	(PCIIOC_BASE | 0x03)	/* Enable/disable write-combining. */

#ifdef __KERNEL__

#include <linux/mod_devicetable.h>

#include <linux/types.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/list.h>
#include <linux/compiler.h>
#include <linux/errno.h>
#include <linux/kobject.h>
#include <asm/atomic.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/irqreturn.h>

/* Include the ID list */
#include <linux/pci_ids.h>

/* pci_slot represents a physical slot 
pci总线上的插槽 表示一个具体的物理设备  pci_dev是逻辑设备
*/
struct pci_slot {
	struct pci_bus *bus;		/* The bus this slot is on  指向属于的pci总线*/
	struct list_head list;		/* node in list of slots on this bus 用于和pci_bus{}.slots连接*/
	struct hotplug_slot *hotplug;	/* Hotplug info (migrate over time) 热插拔信息*/
	unsigned char number;		/* PCI_SLOT(pci_dev->devfn) 插槽编号即pci_dev{}.devfn的高5位*/
	struct kobject kobj;
};

static inline const char *pci_slot_name(const struct pci_slot *slot)
{
	return kobject_name(&slot->kobj);
}

/* File state for mmap()s on /proc/bus/pci/X/Y */
enum pci_mmap_state {
	pci_mmap_io,
	pci_mmap_mem
};

/* This defines the direction arg to the DMA mapping routines. */
#define PCI_DMA_BIDIRECTIONAL	0
#define PCI_DMA_TODEVICE	1
#define PCI_DMA_FROMDEVICE	2
#define PCI_DMA_NONE		3

/*
 *  For PCI devices, the region numbers are assigned this way:
 */
enum {
    //对应pci配置空间的BAR0-BAR5  桥设备之后BAR0-BAR1
	/* #0-5: standard PCI resources */
	PCI_STD_RESOURCES,
	PCI_STD_RESOURCE_END = 5,

	/* #6: expansion ROM resource 扩展ROM区间 对应PCI配置空间中的ROM基地址寄存器描述的ROM区域*/
	PCI_ROM_RESOURCE,

	/* device specific resources IO虚拟化资源区间 用于PCI设置的IO虚拟化*/
#ifdef CONFIG_PCI_IOV
	PCI_IOV_RESOURCES,
	PCI_IOV_RESOURCE_END = PCI_IOV_RESOURCES + PCI_SRIOV_NUM_BARS - 1,
#endif

	/* resources assigned to buses behind the bridge */
#define PCI_BRIDGE_RESOURCE_NUM 4

//过滤资源窗口 仅对桥设备有效 表示地址过滤的区间 每个地址过滤区间决定了一个地址窗口 
	PCI_BRIDGE_RESOURCES,
	PCI_BRIDGE_RESOURCE_END = PCI_BRIDGE_RESOURCES +PCI_BRIDGE_RESOURCE_NUM - 1,

	/* total resources associated with a PCI device */
	PCI_NUM_RESOURCES,

	/* preserve this for compatibility */
	DEVICE_COUNT_RESOURCE
};

typedef int __bitwise pci_power_t;

#define PCI_D0		((pci_power_t __force) 0)
#define PCI_D1		((pci_power_t __force) 1)
#define PCI_D2		((pci_power_t __force) 2)
#define PCI_D3hot	((pci_power_t __force) 3)
#define PCI_D3cold	((pci_power_t __force) 4)
#define PCI_UNKNOWN	((pci_power_t __force) 5)
#define PCI_POWER_ERROR	((pci_power_t __force) -1)

/* Remember to update this when the list above changes! */
extern const char *pci_power_names[];

static inline const char *pci_power_name(pci_power_t state)
{
	return pci_power_names[1 + (int) state];
}

#define PCI_PM_D2_DELAY	200
#define PCI_PM_D3_WAIT	10
#define PCI_PM_BUS_WAIT	50

/** The pci_channel state describes connectivity between the CPU and
 *  the pci device.  If some PCI bus between here and the pci device
 *  has crashed or locked up, this info is reflected here.
 */
typedef unsigned int __bitwise pci_channel_state_t;

enum pci_channel_state {
	/* I/O channel is in normal state */
	pci_channel_io_normal = (__force pci_channel_state_t) 1,

	/* I/O to channel is blocked */
	pci_channel_io_frozen = (__force pci_channel_state_t) 2,

	/* PCI card is dead */
	pci_channel_io_perm_failure = (__force pci_channel_state_t) 3,
};

typedef unsigned int __bitwise pcie_reset_state_t;

enum pcie_reset_state {
	/* Reset is NOT asserted (Use to deassert reset) */
	pcie_deassert_reset = (__force pcie_reset_state_t) 1,

	/* Use #PERST to reset PCI-E device */
	pcie_warm_reset = (__force pcie_reset_state_t) 2,

	/* Use PCI-E Hot Reset to reset device */
	pcie_hot_reset = (__force pcie_reset_state_t) 3
};

typedef unsigned short __bitwise pci_dev_flags_t;
enum pci_dev_flags {
	/* INTX_DISABLE in PCI_COMMAND register disables MSI
	 * generation too.
	 */
	PCI_DEV_FLAGS_MSI_INTX_DISABLE_BUG = (__force pci_dev_flags_t) 1,
	/* Device configuration is irrevocably lost if disabled into D3 */
	PCI_DEV_FLAGS_NO_D3 = (__force pci_dev_flags_t) 2,
};

enum pci_irq_reroute_variant {
	INTEL_IRQ_REROUTE_VARIANT = 1,
	MAX_IRQ_REROUTE_VARIANTS = 3
};

typedef unsigned short __bitwise pci_bus_flags_t;
enum pci_bus_flags {
	PCI_BUS_FLAGS_NO_MSI   = (__force pci_bus_flags_t) 1,
	PCI_BUS_FLAGS_NO_MMRBC = (__force pci_bus_flags_t) 2,
};

struct pci_cap_saved_state {
	struct hlist_node next;
	char cap_nr;
	u32 data[0];
};

struct pcie_link_state;
struct pci_vpd;
struct pci_sriov;
struct pci_ats;

/*
 * The pci_dev structure is used to describe PCI devices.
 每个PCI逻辑设备都会被分配一个pci_dev变量，内核就用这个数据结构来表示一个PCI设备

 */
struct pci_dev {
	struct list_head bus_list;	/* node in per-bus list 挂入所在总线的链表 和pci_bus{}.devicesl连接*/
	
	struct pci_bus	*bus;		/* bus this device is on 指向这个设备所在的pci总线*/
	struct pci_bus	*subordinate;	/* bus this device bridges to 是针对桥设备的,如果为桥设备则指向下一级总线*/

	void		*sysdata;	/* hook for sys-specific extension 用来记录所在根总线特有的信息*/
	struct proc_dir_entry *procent;	/* device entry in /proc/bus/pci 在/proc/bus/pci下的目录项*/
	struct pci_slot	*slot;		/* Physical slot this device is in 指向此设备所在的物理插槽描述符的指针*/

	unsigned int	devfn;		/* encoded device & function index 槽号和功能号的组合  32个槽*8个功能号 */
	unsigned short	vendor; //从pci设备配置寄存器中的厂商id
	unsigned short	device;//从pci设备配置寄存器中的设备id
	unsigned short	subsystem_vendor;//子系统厂商id
	unsigned short	subsystem_device;//子系统设备id
	unsigned int	class;		/* 3 bytes: (base,sub,prog-if) pci设备配置寄存器中的class Code域*/
	u8		revision;	/* PCI revision, low byte of class word pci设备配置寄存器中的修正号*/
	u8		hdr_type;	/* PCI header type (`multi' flag masked out) pci配置寄存器中的头类型*/
	u8		pcie_type;	/* PCI-E device/port type PCI-E设备/端口类型*/
	u8		rom_base_reg;	/* which config register controls the ROM rom基地址寄存器在pci配置空间中的位置
                                  对于pci设备和pci桥设备有不同的值 0x30h  和38h
	                            */
	u8		pin;  		/* which interrupt pin this device uses pci设备配置中的中断引脚域*/

	struct pci_driver *driver;	/* which driver has allocated this device  指向驱动结构*/
	u64		dma_mask;	/* Mask of the bits of bus address this
					   device implements.  Normally this is
					   0xffffffff.  You only need to change
					   this if your device has broken DMA
					   or supports 64-bit transfers.  pci设备的DMA掩码 即它实现的总线地址位数*/

	struct device_dma_parameters dma_parms;//这个设备的DMA参数

	pci_power_t     current_state;  /* Current operating state. In ACPI-speak,
					   this is D0-D3, D0 being fully functional,
					   and D3 being off. 当前电源工作状态 */
	int		pm_cap;		/* PM capability offset in the
					   configuration space 电源管理能力在配置空间的偏移*/
	unsigned int	pme_support:5;	/* Bitmask of states from which PME#
					   can be generated 电源管理事件 状态掩码*/
	unsigned int	d1_support:1;	/* Low power state D1 is supported 为1 表示支持节能状态d1*/
	unsigned int	d2_support:1;	/* Low power state D2 is supported 为1 表示支持节能状态d2*/
	unsigned int	no_d1d2:1;	/* Only allow D0 and D3 为1表示在电源管理方面只允许节能状态d0或d3*/
	unsigned int	wakeup_prepared:1;//为1表示可以将该pci设备作为唤醒事件源

#ifdef CONFIG_PCIEASPM
	struct pcie_link_state	*link_state;	/* ASPM link state. 连接状态*/
#endif

	pci_channel_state_t error_state;	/* current connectivity state 当前连接状态*/
	struct	device	dev;		/* Generic device interface 内嵌的通用设备对象*/

	int		cfg_size;	/* Size of configuration space 配置空间长度*/

	/*
	 * Instead of touching interrupt line and base address registers
	 * directly, use the values stored here. They might be different!
	 */
	unsigned int	irq;//表示这个pci设备通过哪个irq输入线产生中断 一般为0~15之间的某个值
	struct resource resource[DEVICE_COUNT_RESOURCE]; /* I/O and memory regions + expansion ROMs 这个设备的资源数组*/

	/* These fields are used by common fixups */
	unsigned int	transparent:1;	/* Transparent PCI bridge 为1表示pci设备是透明的pci桥设备*/
	unsigned int	multifunction:1;/* Part of multi-function device 为1表示pci设备是多功能设备的一部分*/
	/* keep track of device state */
	unsigned int	is_added:1;//设备是否添加到了系统中
	unsigned int	is_busmaster:1; /* device is busmaster 为1表示pci设备是总线主控设备*/
	unsigned int	no_msi:1;	/* device may not use msi 为1表示设备不可以使用MSI*/
	unsigned int	block_ucfg_access:1;	/* userspace config space access is blocked 为1表示从用户空间对配置空间的访问被阻*/
	unsigned int	broken_parity_status:1;	/* Device generates false positive parity 为1表示pci设备有校验和的问题*/
	unsigned int	irq_reroute_variant:2;	/* device needs IRQ rerouting varian 某些芯片需要将linux内核的原始中断重新映射到它的引导中断上*/
	unsigned int 	msi_enabled:1//1表示启用msi中断;
	unsigned int	msix_enabled:1;//位1表示启动msix中断
	unsigned int	ari_enabled:1;	/* ARI forwarding 为1表示支持ari能力*/
	unsigned int	is_managed:1;//为1 表示pci设备是可管理的 即其设备资源释放将自动完成
	unsigned int	is_pcie:1; //为1 表示是pci-express设备
	unsigned int    needs_freset:1; /* Dev requires fundamental reset 为1表示 设备需要基本复位 */
	unsigned int	state_saved:1;//为1 表示pci设备的状态已经被保存
	unsigned int	is_physfn:1; //为1 表示是物理功能设备
	unsigned int	is_virtfn:1; //为1表示是虚拟功能设备
	unsigned int	reset_fn:1; //为1 表示设备可以被安全的复位
	unsigned int    is_hotplug_bridge:1; //为1表示支持热插拔
	pci_dev_flags_t dev_flags;//用来标记引出这个pci设备在设计上的一些缺陷
	atomic_t	enable_cnt;	/* pci_enable_device has been called pci_enable_device被调用的次数*/

	u32		saved_config_space[16]; /* config space saved at suspend time 在设备被挂起时候 保存其配置空间*/
	struct hlist_head saved_cap_space;//在设备被挂起时 保存其能力列表
	struct bin_attribute *rom_attr; /* attribute descriptor for sysfs ROM entry 用于在sysfs中为这个pci设备生成rom属性文件*/
	int rom_attr_enabled;		/* has display of the rom attribute been enabled? 为1表示允许显示rom属性*/
	struct bin_attribute *res_attr[DEVICE_COUNT_RESOURCE]; /* sysfs file for resources 在sysfs生成资源属性文件*/
	struct bin_attribute *res_attr_wc[DEVICE_COUNT_RESOURCE]; /* sysfs file for WC mapping of resources 用于在sysfs生成wc映射资源*/
#ifdef CONFIG_PCI_MSI
	struct list_pci_deviceshead msi_list;
#endif
	struct pci_vpd *vpd;//指向pci厂商产品数据描述符指针
#ifdef CONFIG_PCI_IOV
	union {
		struct pci_sriov *sriov;	/* SR-IOV capability related */
		struct pci_dev *physfn;	/* the PF this VF is associated with */
	};
	struct pci_ats	*ats;	/* Address Translation Service */
#endif
};

extern struct pci_dev *alloc_pci_dev(void);

#define pci_dev_b(n) list_entry(n, struct pci_dev, bus_list)
#define	to_pci_dev(n) container_of(n, struct pci_dev, dev)
#define for_each_pci_dev(d) while ((d = pci_get_device(PCI_ANY_ID, PCI_ANY_ID, d)) != NULL)

static inline int pci_channel_offline(struct pci_dev *pdev)
{
	return (pdev->error_state != pci_channel_io_normal);
}

static inline struct pci_cap_saved_state *pci_find_saved_cap(
	struct pci_dev *pci_dev, char cap)
{
	struct pci_cap_saved_state *tmp;
	struct hlist_node *pos;

	hlist_for_each_entry(tmp, pos, &pci_dev->saved_cap_space, next) {
		if (tmp->cap_nr == cap)
			return tmp;
	}
	return NULL;
}

static inline void pci_add_saved_cap(struct pci_dev *pci_dev,
	struct pci_cap_saved_state *new_cap)
{
	hlist_add_head(&new_cap->next, &pci_dev->saved_cap_space);
}

#ifndef PCI_BUS_NUM_RESOURCES
#define PCI_BUS_NUM_RESOURCES	16
#endif

#define PCI_REGION_FLAG_MASK	0x0fU	/* These bits of resource flags tell us the PCI region flags */

//描述pci总线的结构 无论根总线还是非根总线 都会对应一个pci_bus

/*
pci初始化过程
pcibus_class_init()   //在/sys/class下创建pci_bus目录
pci_driver_init()     //注册名字为pci的总线类型 在/sys/bus下创建目录pci
pci_arch_init()       //设置pci配置空间访问方式 
pci_subsys_init()     //进行PCI总线扫描(扫描pci设备 建立pci设备的数据结构 在sysfs构建pci目录树)
pci_slot_init()
pcibios_assign_resources()  //分配pci设备的io空间和资源空间
pci_apply_final_quirks()
pci_iommu_init()
pci_proc_init()        //初始化proc文件系统与pci有关的目录项
pci_mmcfg_late_insert_resources()
pci_resource_alignment_sysfs_init()
pci_sysfs_init()       //对扫描到的pci设备在sysfs中建立更多的属性文件
*/
/*
<0000:00:00.0> <PCI域:总线编号:插槽.功能号>

*/
struct pci_bus {
	struct list_head node;		/* node in list of buses
	             对于个根总线:
                     系统当前存在的所有根总线(因为可能存在不止一个Host/PCI桥，那么就可能存在多条根总线) 
                     都通过其pci_bus结构体中的node成员链接成一个全局的根总线链表，
                     其表头由struct list_head类型的全局变量pci_root_buses来描
                 对于非根总线:
                      连接到父总线的 children链
	          */
	struct pci_bus	*parent;	/* parent bus this bridge is on 指向父总线 即pci桥所在的那条总线*/
	struct list_head children;	/* list of child buses 
                                   而根总线下面的所有下级总线则都通过其pci_bus结构体中的node成员链接到其父总线的children链表中
	                             */
	                             
	struct list_head devices;	/* list of devices on this bus 在这个pci总线的设备列表 和pci_dev{}.bus_listl连接*/

	struct pci_dev	*self;		/* bridge device as seen by parent 
                                   对于非根总线 指向引出这条总线的桥设备的描述符
                                   对于根总线 指向NULL
	                             */
								 	
	struct list_head slots;		/* list of slots on this bus 这条总线的插槽链表的表头 pci_slot{}.list指向该域*/
								   
	struct resource	*resource[PCI_BUS_NUM_RESOURCES];
	                 /*
                      对于根总线 指向ioport_resource 或 iomem_resource
                      对于非根总线 指向引出这条总线的桥设备的代表资源窗口的resource数组
					 */ 
					 /* address space routed to this bus */

	struct pci_ops	*ops;		/* configuration access functions 这条总线使用的配置空间访问函数 对于每条总该都是相同的记录在pci_root_ops中*/
	void		*sysdata;	/* hook for sys-specific extension 用来记录所在根总线特有的信息 这些信息往往和硬件架构相关 如pci域NUMA节点和iommu*/
	struct proc_dir_entry *procdir;	/* directory entry in /proc/bus/pci 这条总线在/proc/bus/pci中的目录项*/

	unsigned char	number;		/* bus number 总线编号*/
	unsigned char	primary;	/* number of primary bridge 指向引出这条总线的桥设备的primary编号*/
	unsigned char	secondary;	/* number of secondary bridge 指向引出这条总线的桥设备的secondary编号*/
	unsigned char	subordinate;	/* max number of subordinate buses 这条总线下级最大的编号*/

	char		name[48];//总线的名字  PCI Bus #%02x形式

	unsigned short  bridge_ctl;	/* manage NO_ISA/FBB/et al behaviors引出这条总线的桥设备的桥控制寄存器  */
	pci_bus_flags_t bus_flags;	/* Inherited by child busses 总线标志 这个域用来标记引出这条总线的主桥设备在设计上的一些缺陷*/
	struct device		*bridge;/* 对于根pci总线 为指向新创建的"虚拟"devices指针 对于非根pci总线 为指向引出这条pci总线的桥设备内嵌device指针*/
	struct device		dev;//内嵌的类设备对象  pci总线通过这个域连入pci总线类pcibus_class的设备链表
	struct bin_attribute	*legacy_io; /* legacy I/O for this bus 用于在sysfs中为这条总线生成legacy io属性文件*/
	struct bin_attribute	*legacy_mem; /* legacy mem 用于早sysfs中为这条总线生成legacy mem属性文件*/
	unsigned int		is_added:1;//为1表示设备已经被添加到sysfs中
};

#define pci_bus_b(n)	list_entry(n, struct pci_bus, node)
#define to_pci_bus(n)	container_of(n, struct pci_bus, dev)

/*
 * Returns true if the pci bus is root (behind host-pci bridge),
 * false otherwise
 */
static inline bool pci_is_root_bus(struct pci_bus *pbus)
{
	return !(pbus->parent);
}

#ifdef CONFIG_PCI_MSI
static inline bool pci_dev_msi_enabled(struct pci_dev *pci_dev)
{
	return pci_dev->msi_enabled || pci_dev->msix_enabled;
}
#else
static inline bool pci_dev_msi_enabled(struct pci_dev *pci_dev) { return false; }
#endif

/*
 * Error values that may be returned by PCI functions.
 */
#define PCIBIOS_SUCCESSFUL		0x00
#define PCIBIOS_FUNC_NOT_SUPPORTED	0x81
#define PCIBIOS_BAD_VENDOR_ID		0x83
#define PCIBIOS_DEVICE_NOT_FOUND	0x86
#define PCIBIOS_BAD_REGISTER_NUMBER	0x87
#define PCIBIOS_SET_FAILED		0x88
#define PCIBIOS_BUFFER_TOO_SMALL	0x89

/* Low-level architecture-dependent routines */

struct pci_ops {
	int (*read)(struct pci_bus *bus, unsigned int devfn, int where, int size, u32 *val);
	int (*write)(struct pci_bus *bus, unsigned int devfn, int where, int size, u32 val);
};

/*
 * ACPI needs to be able to access PCI config space before we've done a
 * PCI bus scan and created pci_bus structures.
 */
extern int raw_pci_read(unsigned int domain, unsigned int bus,
			unsigned int devfn, int reg, int len, u32 *val);
extern int raw_pci_write(unsigned int domain, unsigned int bus,
			unsigned int devfn, int reg, int len, u32 val);

struct pci_bus_region {
	resource_size_t start;
	resource_size_t end;
};

//pci_dynid
struct pci_dynids {
	spinlock_t lock;            /* protects list, index 保护动态设备id的链表*/
	struct list_head list;      /* for IDs added at runtime在运行时添加的设备id链表表头*/
};

/* ---------------------------------------------------------------- */
/** PCI Error Recovery System (PCI-ERS).  If a PCI device driver provides
 *  a set of callbacks in struct pci_error_handlers, then that device driver
 *  will be notified of PCI bus errors, and will be driven to recovery
 *  when an error occurs.
 */

typedef unsigned int __bitwise pci_ers_result_t;

enum pci_ers_result {
	/* no result/none/not supported in device driver */
	PCI_ERS_RESULT_NONE = (__force pci_ers_result_t) 1,

	/* Device driver can recover without slot reset */
	PCI_ERS_RESULT_CAN_RECOVER = (__force pci_ers_result_t) 2,

	/* Device driver wants slot to be reset. */
	PCI_ERS_RESULT_NEED_RESET = (__force pci_ers_result_t) 3,

	/* Device has completely failed, is unrecoverable */
	PCI_ERS_RESULT_DISCONNECT = (__force pci_ers_result_t) 4,

	/* Device driver is fully recovered and operational */
	PCI_ERS_RESULT_RECOVERED = (__force pci_ers_result_t) 5,
};

/* PCI bus error event callbacks */
struct pci_error_handlers {
	/* PCI bus error detected on this device */
	pci_ers_result_t (*error_detected)(struct pci_dev *dev,
					   enum pci_channel_state error);

	/* MMIO has been re-enabled, but not DMA */
	pci_ers_result_t (*mmio_enabled)(struct pci_dev *dev);

	/* PCI Express link has been reset */
	pci_ers_result_t (*link_reset)(struct pci_dev *dev);

	/* PCI slot has been reset */
	pci_ers_result_t (*slot_reset)(struct pci_dev *dev);

	/* Device driver may resume normal operations */
	void (*resume)(struct pci_dev *dev);
};

/* ---------------------------------------------------------------- */

struct module;


//定义pci层和设备驱动程序之间的接口
/*
所有的PCI驱动程序都必须定义一个pci_driver结构变量，
在该变量中包含了这个PCI驱动程序所提供的不同功能的函数，
同时，在这个结构中也包含了一个device_driver结构，这个结构定义了PCI子系统与PCI设备之间的接口。
在注册PCI驱动程序时，这个结构将被初始化，同时这个pci_driver变量会被链接到pci_bus_type中的驱动链上去
pci_enable_device()

*/
struct pci_driver {
	struct list_head node;/*用于连接入pci驱动列表*/
	
	char *name; /*驱动模块的名字*/
	
	const struct pci_device_id *id_table;	/*用户把一些设备关联到此驱动程序*/

    /*
     当调用此函数时候 驱动和设备匹配已经成功
     dev:指向pci设备描述符
     id:pci设备id结构指针
     此函数需要需要对设备进一步初始化 成功返回0  否则返回错误码
	*/
	int  (*probe)  (struct pci_dev *dev, const struct pci_device_id *id);	/*当pci层发现正在搜索驱动程序的设备ID与前面提到的id_table匹配 就会调用此函数 创建新的设备*/


	void (*remove) (struct pci_dev *dev);	/* 当驱动程序移除 或热插拔设备被删除,网络设备使用此函数来释放分配的io端口和io内存*/

    //设备挂起   下面几个和电源管理相关
	int  (*suspend) (struct pci_dev *dev, pm_message_t state);	/* Device suspended */
	int  (*suspend_late) (struct pci_dev *dev, pm_message_t state);
	int  (*resume_early) (struct pci_dev *dev);
    //设备继续运行
	int  (*resume) (struct pci_dev *dev);	                /* Device woken up */

	void (*shutdown) (struct pci_dev *dev);

	//实现pci错误恢复
	struct pci_error_handlers *err_handler;

	//内嵌的device_driver
	struct device_driver	driver;

	//动态id
	struct pci_dynids dynids;
};

#define	to_pci_driver(drv) container_of(drv, struct pci_driver, driver)

/**
 * DEFINE_PCI_DEVICE_TABLE - macro used to describe a pci device table
 * @_table: device table name
 *
 * This macro is used to create a struct pci_device_id array (a device table)
 * in a generic manner.
 */
#define DEFINE_PCI_DEVICE_TABLE(_table) \
	const struct pci_device_id _table[] __devinitconst

/**
 * PCI_DEVICE - macro used to describe a specific pci device
 * @vend: the 16 bit PCI Vendor ID
 * @dev: the 16 bit PCI Device ID
 *
 * This macro is used to create a struct pci_device_id that matches a
 * specific device.  The subvendor and subdevice fields will be set to
 * PCI_ANY_ID.
 */
#define PCI_DEVICE(vend,dev) \
	.vendor = (vend), .device = (dev), \
	.subvendor = PCI_ANY_ID, .subdevice = PCI_ANY_ID

/**
 * PCI_DEVICE_CLASS - macro used to describe a specific pci device class
 * @dev_class: the class, subclass, prog-if triple for this device
 * @dev_class_mask: the class mask for this device
 *
 * This macro is used to create a struct pci_device_id that matches a
 * specific PCI class.  The vendor, device, subvendor, and subdevice
 * fields will be set to PCI_ANY_ID.
 */
#define PCI_DEVICE_CLASS(dev_class,dev_class_mask) \
	.class = (dev_class), .class_mask = (dev_class_mask), \
	.vendor = PCI_ANY_ID, .device = PCI_ANY_ID, \
	.subvendor = PCI_ANY_ID, .subdevice = PCI_ANY_ID

/**
 * PCI_VDEVICE - macro used to describe a specific pci device in short form
 * @vendor: the vendor name
 * @device: the 16 bit PCI Device ID
 *
 * This macro is used to create a struct pci_device_id that matches a
 * specific PCI device.  The subvendor, and subdevice fields will be set
 * to PCI_ANY_ID. The macro allows the next field to follow as the device
 * private data.
 */

#define PCI_VDEVICE(vendor, device)		\
	PCI_VENDOR_ID_##vendor, (device),	\
	PCI_ANY_ID, PCI_ANY_ID, 0, 0

/* these external functions are only available when PCI support is enabled */
#ifdef CONFIG_PCI

extern struct bus_type pci_bus_type;

/* Do NOT directly access these two variables, unless you are arch specific pci
 * code, or pci core code. */
//连接所有的pci总线 根总线的number是0     ,pci_bus{node}成员连接到此链表中
extern struct list_head pci_root_buses;	/* list of all known PCI buses */

/* Some device drivers need know if pci is initiated */
extern int no_pci_devices(void);

void pcibios_fixup_bus(struct pci_bus *);
int __must_check pcibios_enable_device(struct pci_dev *, int mask);
char *pcibios_setup(char *str);

/* Used only when drivers/pci/setup.c is used */
void pcibios_align_resource(void *, struct resource *, resource_size_t,
				resource_size_t);
void pcibios_update_irq(struct pci_dev *, int irq);

/* Weak but can be overriden by arch */
void pci_fixup_cardbus(struct pci_bus *);

/* Generic PCI functions used internally */

extern struct pci_bus *pci_find_bus(int domain, int busnr);
void pci_bus_add_devices(const struct pci_bus *bus);
struct pci_bus *pci_scan_bus_parented(struct device *parent, int bus,
				      struct pci_ops *ops, void *sysdata);
static inline struct pci_bus * __devinit pci_scan_bus(int bus, struct pci_ops *ops,
					   void *sysdata)
{
	struct pci_bus *root_bus;
	root_bus = pci_scan_bus_parented(NULL, bus, ops, sysdata);
	if (root_bus)
		pci_bus_add_devices(root_bus);
	return root_bus;
}
struct pci_bus *pci_create_bus(struct device *parent, int bus,
			       struct pci_ops *ops, void *sysdata);
struct pci_bus *pci_add_new_bus(struct pci_bus *parent, struct pci_dev *dev,
				int busnr);
struct pci_slot *pci_create_slot(struct pci_bus *parent, int slot_nr,
				 const char *name,
				 struct hotplug_slot *hotplug);
void pci_destroy_slot(struct pci_slot *slot);
void pci_renumber_slot(struct pci_slot *slot, int slot_nr);
int pci_scan_slot(struct pci_bus *bus, int devfn);
struct pci_dev *pci_scan_single_device(struct pci_bus *bus, int devfn);
void pci_device_add(struct pci_dev *dev, struct pci_bus *bus);
unsigned int pci_scan_child_bus(struct pci_bus *bus);
int __must_check pci_bus_add_device(struct pci_dev *dev);
void pci_read_bridge_bases(struct pci_bus *child);
struct resource *pci_find_parent_resource(const struct pci_dev *dev,
					  struct resource *res);
u8 pci_swizzle_interrupt_pin(struct pci_dev *dev, u8 pin);
int pci_get_interrupt_pin(struct pci_dev *dev, struct pci_dev **bridge);
u8 pci_common_swizzle(struct pci_dev *dev, u8 *pinp);
extern struct pci_dev *pci_dev_get(struct pci_dev *dev);
extern void pci_dev_put(struct pci_dev *dev);
extern void pci_remove_bus(struct pci_bus *b);
extern void pci_remove_bus_device(struct pci_dev *dev);
extern void pci_stop_bus_device(struct pci_dev *dev);
void pci_setup_cardbus(struct pci_bus *bus);
extern void pci_sort_breadthfirst(void);

/* Generic PCI functions exported to card drivers */

#ifdef CONFIG_PCI_LEGACY
struct pci_dev __deprecated *pci_find_device(unsigned int vendor,
					     unsigned int device,
					     struct pci_dev *from);
#endif /* CONFIG_PCI_LEGACY */

enum pci_lost_interrupt_reason {
	PCI_LOST_IRQ_NO_INFORMATION = 0,
	PCI_LOST_IRQ_DISABLE_MSI,
	PCI_LOST_IRQ_DISABLE_MSIX,
	PCI_LOST_IRQ_DISABLE_ACPI,
};
enum pci_lost_interrupt_reason pci_lost_interrupt(struct pci_dev *dev);
int pci_find_capability(struct pci_dev *dev, int cap);
int pci_find_next_capability(struct pci_dev *dev, u8 pos, int cap);
int pci_find_ext_capability(struct pci_dev *dev, int cap);
int pci_find_ht_capability(struct pci_dev *dev, int ht_cap);
int pci_find_next_ht_capability(struct pci_dev *dev, int pos, int ht_cap);
struct pci_bus *pci_find_next_bus(const struct pci_bus *from);

struct pci_dev *pci_get_device(unsigned int vendor, unsigned int device,
				struct pci_dev *from);
struct pci_dev *pci_get_subsys(unsigned int vendor, unsigned int device,
				unsigned int ss_vendor, unsigned int ss_device,
				struct pci_dev *from);
struct pci_dev *pci_get_slot(struct pci_bus *bus, unsigned int devfn);
struct pci_dev *pci_get_bus_and_slot(unsigned int bus, unsigned int devfn);
struct pci_dev *pci_get_class(unsigned int class, struct pci_dev *from);
int pci_dev_present(const struct pci_device_id *ids);



//下面函数在dirvers/pci/access.h
/*下面的6个函数参数含义
bus 指向pci bus总线
devfn: 设备/功能号
where:要访问的配置空间位置 
val:要读些的值
下面的函数最终会调用raw_pci_ops中相应函数 完成配置空间的访问,raw_pci_ops的初始化看pci_arch_init()
*/
int pci_bus_read_config_byte(struct pci_bus *bus, unsigned int devfn,int where, u8 *val);
int pci_bus_read_config_word(struct pci_bus *bus, unsigned int devfn,int where, u16 *val);
int pci_bus_read_config_dword(struct pci_bus *bus, unsigned int devfn,int where, u32 *val);
int pci_bus_write_config_byte(struct pci_bus *bus, unsigned int devfn,int where, u8 val);
int pci_bus_write_config_word(struct pci_bus *bus, unsigned int devfn,int where, u16 val);
int pci_bus_write_config_dword(struct pci_bus *bus, unsigned int devfn,int where, u32 val);
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



struct pci_ops *pci_bus_set_ops(struct pci_bus *bus, struct pci_ops *ops);

/*
驱动可以使用下面的函数访问指定pci设备的配置空间
dev: pci 逻辑设备指针z
where: 要访问的配置空间位置
val:要读写的数据
*/
static inline int pci_read_config_byte(struct pci_dev *dev, int where, u8 *val)
{
	return pci_bus_read_config_byte(dev->bus, dev->devfn, where, val);
}
static inline int pci_read_config_word(struct pci_dev *dev, int where, u16 *val)
{
	return pci_bus_read_config_word(dev->bus, dev->devfn, where, val);
}
static inline int pci_read_config_dword(struct pci_dev *dev, int where,u32 *val)
{
	return pci_bus_read_config_dword(dev->bus, dev->devfn, where, val);
}
static inline int pci_write_config_byte(struct pci_dev *dev, int where, u8 val)
{
	return pci_bus_write_config_byte(dev->bus, dev->devfn, where, val);
}
static inline int pci_write_config_word(struct pci_dev *dev, int where, u16 val)
{
	return pci_bus_write_config_word(dev->bus, dev->devfn, where, val);
}
static inline int pci_write_config_dword(struct pci_dev *dev, int where,u32 val)
{
	return pci_bus_write_config_dword(dev->bus, dev->devfn, where, val);
}

int __must_check pci_enable_device(struct pci_dev *dev);
int __must_check pci_enable_device_io(struct pci_dev *dev);
int __must_check pci_enable_device_mem(struct pci_dev *dev);
int __must_check pci_reenable_device(struct pci_dev *);
int __must_check pcim_enable_device(struct pci_dev *pdev);
void pcim_pin_device(struct pci_dev *pdev);

static inline int pci_is_enabled(struct pci_dev *pdev)
{
	return (atomic_read(&pdev->enable_cnt) > 0);
}

static inline int pci_is_managed(struct pci_dev *pdev)
{
	return pdev->is_managed;
}

void pci_disable_device(struct pci_dev *dev);
void pci_set_master(struct pci_dev *dev);
void pci_clear_master(struct pci_dev *dev);
int pci_set_pcie_reset_state(struct pci_dev *dev, enum pcie_reset_state state);
#define HAVE_PCI_SET_MWI
int __must_check pci_set_mwi(struct pci_dev *dev);
int pci_try_set_mwi(struct pci_dev *dev);
void pci_clear_mwi(struct pci_dev *dev);
void pci_intx(struct pci_dev *dev, int enable);
void pci_msi_off(struct pci_dev *dev);
int pci_set_dma_mask(struct pci_dev *dev, u64 mask);
int pci_set_consistent_dma_mask(struct pci_dev *dev, u64 mask);
int pci_set_dma_max_seg_size(struct pci_dev *dev, unsigned int size);
int pci_set_dma_seg_boundary(struct pci_dev *dev, unsigned long mask);
int pcix_get_max_mmrbc(struct pci_dev *dev);
int pcix_get_mmrbc(struct pci_dev *dev);
int pcix_set_mmrbc(struct pci_dev *dev, int mmrbc);
int pcie_get_readrq(struct pci_dev *dev);
int pcie_set_readrq(struct pci_dev *dev, int rq);
int __pci_reset_function(struct pci_dev *dev);
int pci_reset_function(struct pci_dev *dev);
void pci_update_resource(struct pci_dev *dev, int resno);
int __must_check pci_assign_resource(struct pci_dev *dev, int i);
int pci_select_bars(struct pci_dev *dev, unsigned long flags);

/* ROM control related routines */
int pci_enable_rom(struct pci_dev *pdev);
void pci_disable_rom(struct pci_dev *pdev);
void __iomem __must_check *pci_map_rom(struct pci_dev *pdev, size_t *size);
void pci_unmap_rom(struct pci_dev *pdev, void __iomem *rom);
size_t pci_get_rom_size(struct pci_dev *pdev, void __iomem *rom, size_t size);

/* Power management related routines */
int pci_save_state(struct pci_dev *dev);
int pci_restore_state(struct pci_dev *dev);
int __pci_complete_power_transition(struct pci_dev *dev, pci_power_t state);
int pci_set_power_state(struct pci_dev *dev, pci_power_t state);
pci_power_t pci_choose_state(struct pci_dev *dev, pm_message_t state);
bool pci_pme_capable(struct pci_dev *dev, pci_power_t state);
void pci_pme_active(struct pci_dev *dev, bool enable);
int pci_enable_wake(struct pci_dev *dev, pci_power_t state, bool enable);
int pci_wake_from_d3(struct pci_dev *dev, bool enable);
pci_power_t pci_target_state(struct pci_dev *dev);
int pci_prepare_to_sleep(struct pci_dev *dev);
int pci_back_from_sleep(struct pci_dev *dev);

/* Functions for PCI Hotplug drivers to use */
int pci_bus_find_capability(struct pci_bus *bus, unsigned int devfn, int cap);
#ifdef CONFIG_HOTPLUG
unsigned int pci_rescan_bus(struct pci_bus *bus);
#endif

/* Vital product data routines */
ssize_t pci_read_vpd(struct pci_dev *dev, loff_t pos, size_t count, void *buf);
ssize_t pci_write_vpd(struct pci_dev *dev, loff_t pos, size_t count, const void *buf);
int pci_vpd_truncate(struct pci_dev *dev, size_t size);

/* Helper functions for low-level code (drivers/pci/setup-[bus,res].c) */
void pci_bus_assign_resources(const struct pci_bus *bus);
void pci_bus_size_bridges(struct pci_bus *bus);
int pci_claim_resource(struct pci_dev *, int);
void pci_assign_unassigned_resources(void);
void pdev_enable_device(struct pci_dev *);
void pdev_sort_resources(struct pci_dev *, struct resource_list *);
int pci_enable_resources(struct pci_dev *, int mask);
void pci_fixup_irqs(u8 (*)(struct pci_dev *, u8 *),
		    int (*)(struct pci_dev *, u8, u8));
#define HAVE_PCI_REQ_REGIONS	2
int __must_check pci_request_regions(struct pci_dev *, const char *);
int __must_check pci_request_regions_exclusive(struct pci_dev *, const char *);
void pci_release_regions(struct pci_dev *);
int __must_check pci_request_region(struct pci_dev *, int, const char *);
int __must_check pci_request_region_exclusive(struct pci_dev *, int, const char *);
void pci_release_region(struct pci_dev *, int);
int pci_request_selected_regions(struct pci_dev *, int, const char *);
int pci_request_selected_regions_exclusive(struct pci_dev *, int, const char *);
void pci_release_selected_regions(struct pci_dev *, int);

/* drivers/pci/bus.c */
int __must_check pci_bus_alloc_resource(struct pci_bus *bus,
			struct resource *res, resource_size_t size,
			resource_size_t align, resource_size_t min,
			unsigned int type_mask,
			void (*alignf)(void *, struct resource *,
				resource_size_t, resource_size_t),
			void *alignf_data);
void pci_enable_bridges(struct pci_bus *bus);

/* Proper probing supporting hot-pluggable devices */
int __must_check __pci_register_driver(struct pci_driver *, struct module *,
				       const char *mod_name);

/*
 * pci_register_driver must be a macro so that KBUILD_MODNAME can be expanded
 */
 //注册PCI驱动，参数为要注册的pci驱动的结构体
 //每个pci驱动调用pci_register_driver传入一个pci_driver结构
#define pci_register_driver(driver)		\
	__pci_register_driver(driver, THIS_MODULE, KBUILD_MODNAME)

void pci_unregister_driver(struct pci_driver *dev);
void pci_remove_behind_bridge(struct pci_dev *dev);
struct pci_driver *pci_dev_driver(const struct pci_dev *dev);e3
int pci_add_dynid(struct pci_driver *drv,
		  unsigned int vendor, unsigned int device,
		  unsigned int subvendor, unsigned int subdevice,
		  unsigned int class, unsigned int class_mask,
		  unsigned long driver_data);
const struct pci_device_id *pci_match_id(const struct pci_device_id *ids,
					 struct pci_dev *dev);
int pci_scan_bridge(struct pci_bus *bus, struct pci_dev *dev, int max,
		    int pass);

void pci_walk_bus(struct pci_bus *top, int (*cb)(struct pci_dev *, void *),
		  void *userdata);
int pci_cfg_space_size_ext(struct pci_dev *dev);
int pci_cfg_space_size(struct pci_dev *dev);
unsigned char pci_bus_max_busnr(struct pci_bus *bus);

int pci_set_vga_state(struct pci_dev *pdev, bool decode,
		      unsigned int command_bits, bool change_bridge);
/* kmem_cache style wrapper around pci_alloc_consistent() */

#include <linux/dmapool.h>

#define	pci_pool dma_pool
#define pci_pool_create(name, pdev, size, align, allocation) \
		dma_pool_create(name, &pdev->dev, size, align, allocation)
#define	pci_pool_destroy(pool) dma_pool_destroy(pool)
#define	pci_pool_alloc(pool, flags, handle) dma_pool_alloc(pool, flags, handle)
#define	pci_pool_free(pool, vaddr, addr) dma_pool_free(pool, vaddr, addr)

enum pci_dma_burst_strategy {
	PCI_DMA_BURST_INFINITY,	/* make bursts as large as possible,
				   strategy_parameter is N/A */
	PCI_DMA_BURST_BOUNDARY, /* disconnect at every strategy_parameter
				   byte boundaries */
	PCI_DMA_BURST_MULTIPLE, /* disconnect at some multiple of
				   strategy_parameter byte boundaries */
};

struct msix_entry {
	u32	vector;	/* kernel uses to write allocated vector */
	u16	entry;	/* driver uses to specify entry, OS writes */
};


#ifndef CONFIG_PCI_MSI
static inline int pci_enable_msi_block(struct pci_dev *dev, unsigned int nvec)
{
	return -1;
}

static inline void pci_msi_shutdown(struct pci_dev *dev)
{ }
static inline void pci_disable_msi(struct pci_dev *dev)
{ }

static inline int pci_msix_table_size(struct pci_dev *dev)
{
	return 0;
}
static inline int pci_enable_msix(struct pci_dev *dev,
				  struct msix_entry *entries, int nvec)
{
	return -1;
}

static inline void pci_msix_shutdown(struct pci_dev *dev)
{ }
static inline void pci_disable_msix(struct pci_dev *dev)
{ }

static inline void msi_remove_pci_irq_vectors(struct pci_dev *dev)
{ }

static inline void pci_restore_msi_state(struct pci_dev *dev)
{ }
static inline int pci_msi_enabled(void)
{
	return 0;
}
#else
extern int pci_enable_msi_block(struct pci_dev *dev, unsigned int nvec);
extern void pci_msi_shutdown(struct pci_dev *dev);
extern void pci_disable_msi(struct pci_dev *dev);
extern int pci_msix_table_size(struct pci_dev *dev);
extern int pci_enable_msix(struct pci_dev *dev,
	struct msix_entry *entries, int nvec);
extern void pci_msix_shutdown(struct pci_dev *dev);
extern void pci_disable_msix(struct pci_dev *dev);
extern void msi_remove_pci_irq_vectors(struct pci_dev *dev);
extern void pci_restore_msi_state(struct pci_dev *dev);
extern int pci_msi_enabled(void);
#endif

#ifndef CONFIG_PCIEASPM
static inline int pcie_aspm_enabled(void)
{
	return 0;
}
#else
extern int pcie_aspm_enabled(void);
#endif

#ifndef CONFIG_PCIE_ECRC
static inline void pcie_set_ecrc_checking(struct pci_dev *dev)
{
	return;
}
static inline void pcie_ecrc_get_policy(char *str) {};
#else
extern void pcie_set_ecrc_checking(struct pci_dev *dev);
extern void pcie_ecrc_get_policy(char *str);
#endif

#define pci_enable_msi(pdev)	pci_enable_msi_block(pdev, 1)

#ifdef CONFIG_HT_IRQ
/* The functions a driver should call */
int  ht_create_irq(struct pci_dev *dev, int idx);
void ht_destroy_irq(unsigned int irq);
#endif /* CONFIG_HT_IRQ */

extern void pci_block_user_cfg_access(struct pci_dev *dev);
extern void pci_unblock_user_cfg_access(struct pci_dev *dev);

/*
 * PCI domain support.  Sometimes called PCI segment (eg by ACPI),
 * a PCI domain is defined to be a set of PCI busses which share
 * configuration space.
 */
#ifdef CONFIG_PCI_DOMAINS
extern int pci_domains_supported;
#else
enum { pci_domains_supported = 0 };
static inline int pci_domain_nr(struct pci_bus *bus)
{
	return 0;
}

static inline int pci_proc_domain(struct pci_bus *bus)
{
	return 0;
}
#endif /* CONFIG_PCI_DOMAINS */

/* some architectures require additional setup to direct VGA traffic */
typedef int (*arch_set_vga_state_t)(struct pci_dev *pdev, bool decode,
		      unsigned int command_bits, bool change_bridge);
extern void pci_register_set_vga_state(arch_set_vga_state_t func);

#else /* CONFIG_PCI is not enabled */

/*
 *  If the system does not have PCI, clearly these return errors.  Define
 *  these as simple inline functions to avoid hair in drivers.
 */

#define _PCI_NOP(o, s, t) \
	static inline int pci_##o##_config_##s(struct pci_dev *dev, \
						int where, t val) \
		{ return PCIBIOS_FUNC_NOT_SUPPORTED; }

#define _PCI_NOP_ALL(o, x)	_PCI_NOP(o, byte, u8 x) \
				_PCI_NOP(o, word, u16 x) \
				_PCI_NOP(o, dword, u32 x)
_PCI_NOP_ALL(read, *)
_PCI_NOP_ALL(write,)

static inline struct pci_dev *pci_find_device(unsigned int vendor,
					      unsigned int device,
					      struct pci_dev *from)
{
	return NULL;
}

static inline struct pci_dev *pci_get_device(unsigned int vendor,
					     unsigned int device,
					     struct pci_dev *from)
{
	return NULL;
}

static inline struct pci_dev *pci_get_subsys(unsigned int vendor,
					     unsigned int device,
					     unsigned int ss_vendor,
					     unsigned int ss_device,
					     struct pci_dev *from)
{
	return NULL;
}

static inline struct pci_dev *pci_get_class(unsigned int class,
					    struct pci_dev *from)
{
	return NULL;
}

#define pci_dev_present(ids)	(0)
#define no_pci_devices()	(1)
#define pci_dev_put(dev)	do { } while (0)

static inline void pci_set_master(struct pci_dev *dev)
{ }

static inline int pci_enable_device(struct pci_dev *dev)
{
	return -EIO;
}

static inline void pci_disable_device(struct pci_dev *dev)
{ }

static inline int pci_set_dma_mask(struct pci_dev *dev, u64 mask)
{
	return -EIO;
}

static inline int pci_set_consistent_dma_mask(struct pci_dev *dev, u64 mask)
{
	return -EIO;
}

static inline int pci_set_dma_max_seg_size(struct pci_dev *dev,
					unsigned int size)
{
	return -EIO;
}

static inline int pci_set_dma_seg_boundary(struct pci_dev *dev,
					unsigned long mask)
{
	return -EIO;
}

static inline int pci_assign_resource(struct pci_dev *dev, int i)
{
	return -EBUSY;
}

static inline int __pci_register_driver(struct pci_driver *drv,
					struct module *owner)
{
	return 0;
}

static inline int pci_register_driver(struct pci_driver *drv)
{
	return 0;
}

static inline void pci_unregister_driver(struct pci_driver *drv)
{ }

static inline int pci_find_capability(struct pci_dev *dev, int cap)
{
	return 0;
}

static inline int pci_find_next_capability(struct pci_dev *dev, u8 post,
					   int cap)
{
	return 0;
}

static inline int pci_find_ext_capability(struct pci_dev *dev, int cap)
{
	return 0;
}

/* Power management related routines */
static inline int pci_save_state(struct pci_dev *dev)
{
	return 0;
}

static inline int pci_restore_state(struct pci_dev *dev)
{
	return 0;
}

static inline int pci_set_power_state(struct pci_dev *dev, pci_power_t state)
{
	return 0;
}

static inline pci_power_t pci_choose_state(struct pci_dev *dev,
					   pm_message_t state)
{
	return PCI_D0;
}

static inline int pci_enable_wake(struct pci_dev *dev, pci_power_t state,
				  int enable)
{
	return 0;
}

static inline int pci_request_regions(struct pci_dev *dev, const char *res_name)
{
	return -EIO;
}

static inline void pci_release_regions(struct pci_dev *dev)
{ }

#define pci_dma_burst_advice(pdev, strat, strategy_parameter) do { } while (0)

static inline void pci_block_user_cfg_access(struct pci_dev *dev)
{ }

static inline void pci_unblock_user_cfg_access(struct pci_dev *dev)
{ }

static inline struct pci_bus *pci_find_next_bus(const struct pci_bus *from)
{ return NULL; }

static inline struct pci_dev *pci_get_slot(struct pci_bus *bus,
						unsigned int devfn)
{ return NULL; }

static inline struct pci_dev *pci_get_bus_and_slot(unsigned int bus,
						unsigned int devfn)
{ return NULL; }

#endif /* CONFIG_PCI */

/* Include architecture-dependent settings and functions */

#include <asm/pci.h>

#ifndef PCIBIOS_MAX_MEM_32
#define PCIBIOS_MAX_MEM_32 (-1)
#endif

/* these helpers provide future and backwards compatibility
 * for accessing popular PCI BAR info */
#define pci_resource_start(dev, bar)	((dev)->resource[(bar)].start)
#define pci_resource_end(dev, bar)	((dev)->resource[(bar)].end)
#define pci_resource_flags(dev, bar)	((dev)->resource[(bar)].flags)
#define pci_resource_len(dev,bar) \
	((pci_resource_start((dev), (bar)) == 0 &&	\
	  pci_resource_end((dev), (bar)) ==		\
	  pci_resource_start((dev), (bar))) ? 0 :	\
							\
	 (pci_resource_end((dev), (bar)) -		\
	  pci_resource_start((dev), (bar)) + 1))

/* Similar to the helpers above, these manipulate per-pci_dev
 * driver-specific data.  They are really just a wrapper around
 * the generic device structure functions of these calls.
 */
static inline void *pci_get_drvdata(struct pci_dev *pdev)
{
	return dev_get_drvdata(&pdev->dev);
}

static inline void pci_set_drvdata(struct pci_dev *pdev, void *data)
{
	dev_set_drvdata(&pdev->dev, data);
}

/* If you want to know what to call your pci_dev, ask this function.
 * Again, it's a wrapper around the generic device.
 */
static inline const char *pci_name(const struct pci_dev *pdev)
{
	return dev_name(&pdev->dev);
}


/* Some archs don't want to expose struct resource to userland as-is
 * in sysfs and /proc
 */
#ifndef HAVE_ARCH_PCI_RESOURCE_TO_USER
static inline void pci_resource_to_user(const struct pci_dev *dev, int bar,
		const struct resource *rsrc, resource_size_t *start,
		resource_size_t *end)
{
	*start = rsrc->start;
	*end = rsrc->end;
}
#endif /* HAVE_ARCH_PCI_RESOURCE_TO_USER */


/*
 *  The world is not perfect and supplies us with broken PCI devices.
 *  For at least a part of these bugs we need a work-around, so both
 *  generic (drivers/pci/quirks.c) and per-architecture code can define
 *  fixup hooks to be called for particular buggy devices.
 */

struct pci_fixup {
	u16 vendor, device;	/* You can use PCI_ANY_ID here of course */
	void (*hook)(struct pci_dev *dev);
};

enum pci_fixup_pass {
	pci_fixup_early,	/* Before probing BARs */
	pci_fixup_header,	/* After reading configuration header */
	pci_fixup_final,	/* Final phase of device fixups */
	pci_fixup_enable,	/* pci_enable_device() time */
	pci_fixup_resume,	/* pci_device_resume() */
	pci_fixup_suspend,	/* pci_device_suspend */
	pci_fixup_resume_early, /* pci_device_resume_early() */
};

/* Anonymous variables would be nice... */
#define DECLARE_PCI_FIXUP_SECTION(section, name, vendor, device, hook)	\
	static const struct pci_fixup __pci_fixup_##name __used		\
	__attribute__((__section__(#section))) = { vendor, device, hook };
#define DECLARE_PCI_FIXUP_EARLY(vendor, device, hook)			\
	DECLARE_PCI_FIXUP_SECTION(.pci_fixup_early,			\
			vendor##device##hook, vendor, device, hook)
#define DECLARE_PCI_FIXUP_HEADER(vendor, device, hook)			\
	DECLARE_PCI_FIXUP_SECTION(.pci_fixup_header,			\
			vendor##device##hook, vendor, device, hook)
#define DECLARE_PCI_FIXUP_FINAL(vendor, device, hook)			\
	DECLARE_PCI_FIXUP_SECTION(.pci_fixup_final,			\
			vendor##device##hook, vendor, device, hook)
#define DECLARE_PCI_FIXUP_ENABLE(vendor, device, hook)			\
	DECLARE_PCI_FIXUP_SECTION(.pci_fixup_enable,			\
			vendor##device##hook, vendor, device, hook)
#define DECLARE_PCI_FIXUP_RESUME(vendor, device, hook)			\
	DECLARE_PCI_FIXUP_SECTION(.pci_fixup_resume,			\
			resume##vendor##device##hook, vendor, device, hook)
#define DECLARE_PCI_FIXUP_RESUME_EARLY(vendor, device, hook)		\
	DECLARE_PCI_FIXUP_SECTION(.pci_fixup_resume_early,		\
			resume_early##vendor##device##hook, vendor, device, hook)
#define DECLARE_PCI_FIXUP_SUSPEND(vendor, device, hook)			\
	DECLARE_PCI_FIXUP_SECTION(.pci_fixup_suspend,			\
			suspend##vendor##device##hook, vendor, device, hook)


void pci_fixup_device(enum pci_fixup_pass pass, struct pci_dev *dev);

void __iomem *pcim_iomap(struct pci_dev *pdev, int bar, unsigned long maxlen);
void pcim_iounmap(struct pci_dev *pdev, void __iomem *addr);
void __iomem * const *pcim_iomap_table(struct pci_dev *pdev);
int pcim_iomap_regions(struct pci_dev *pdev, u16 mask, const char *name);
int pcim_iomap_regions_request_all(struct pci_dev *pdev, u16 mask,
				   const char *name);
void pcim_iounmap_regions(struct pci_dev *pdev, u16 mask);

extern int pci_pci_problems;
#define PCIPCI_FAIL		1	/* No PCI PCI DMA */
#define PCIPCI_TRITON		2
#define PCIPCI_NATOMA		4
#define PCIPCI_VIAETBF		8
#define PCIPCI_VSFX		16
#define PCIPCI_ALIMAGIK		32	/* Need low latency setting */
#define PCIAGP_FAIL		64	/* No PCI to AGP DMA */

extern unsigned long pci_cardbus_io_size;
extern unsigned long pci_cardbus_mem_size;

extern unsigned long pci_hotplug_io_size;
extern unsigned long pci_hotplug_mem_size;

int pcibios_add_platform_entries(struct pci_dev *dev);
void pcibios_disable_device(struct pci_dev *dev);
int pcibios_set_pcie_reset_state(struct pci_dev *dev,
				 enum pcie_reset_state state);

#ifdef CONFIG_PCI_MMCONFIG
extern void __init pci_mmcfg_early_init(void);
extern void __init pci_mmcfg_late_init(void);
#else
static inline void pci_mmcfg_early_init(void) { }
static inline void pci_mmcfg_late_init(void) { }
#endif

int pci_ext_cfg_avail(struct pci_dev *dev);

void __iomem *pci_ioremap_bar(struct pci_dev *pdev, int bar);

#ifdef CONFIG_PCI_IOV
extern int pci_enable_sriov(struct pci_dev *dev, int nr_virtfn);
extern void pci_disable_sriov(struct pci_dev *dev);
extern irqreturn_t pci_sriov_migration(struct pci_dev *dev);
#else
static inline int pci_enable_sriov(struct pci_dev *dev, int nr_virtfn)
{
	return -ENODEV;
}
static inline void pci_disable_sriov(struct pci_dev *dev)
{
}
static inline irqreturn_t pci_sriov_migration(struct pci_dev *dev)
{
	return IRQ_NONE;
}
#endif

#if defined(CONFIG_HOTPLUG_PCI) || defined(CONFIG_HOTPLUG_PCI_MODULE)
extern void pci_hp_create_module_link(struct pci_slot *pci_slot);
extern void pci_hp_remove_module_link(struct pci_slot *pci_slot);
#endif

#endif /* __KERNEL__ */
#endif /* LINUX_PCI_H */
