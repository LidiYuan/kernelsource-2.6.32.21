/*
 * BIOS32 and PCI BIOS handling.
 */

#include <linux/pci.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <asm/pci_x86.h>
#include <asm/pci-functions.h>

/* BIOS32 signature: "_32_" */
#define BIOS32_SIGNATURE	(('_' << 0) + ('3' << 8) + ('2' << 16) + ('_' << 24))

/* PCI signature: "PCI " */
#define PCI_SIGNATURE		(('P' << 0) + ('C' << 8) + ('I' << 16) + (' ' << 24))

/* PCI service signature: "$PCI" */
#define PCI_SERVICE		(('$' << 0) + ('P' << 8) + ('C' << 16) + ('I' << 24))

/* PCI BIOS hardware mechanism flags */
#define PCIBIOS_HW_TYPE1		0x01
#define PCIBIOS_HW_TYPE2		0x02
#define PCIBIOS_HW_TYPE1_SPEC		0x10
#define PCIBIOS_HW_TYPE2_SPEC		0x20

/*
 * This is the standard structure used to identify the entry point
 * to the BIOS32 Service Directory, as documented in
 * 	Standard BIOS 32-bit Service Directory Proposal
 * 	Revision 0.4 May 24, 1993
 * 	Phoenix Technologies Ltd.
 *	Norwood, MA
 * and the PCI BIOS specification.
 */

//bios32服务目录
union bios32 {
	struct {
		unsigned long signature;	/* _32_ */
		unsigned long entry;		/* 32 bit physical address BIOS32服务目录的入口地址*/
		unsigned char revision;		/* Revision level, 0 修正号 当前值为0 */
		unsigned char length;		/* Length in paragraphs should be 01 数据长度以16字节为单位*/
		unsigned char checksum;		/* All bytes must add up to zero 校验和*/
		unsigned char reserved[5]; 	/* Must be zero 保留 必须为0*/
	} fields;
	char chars[16];
};

/*
 * Physical address of the service directory.  I don't know if we're
 * allowed to have more than one of these or not, so just in case
 * we'll make pcibios_present() take a memory start parameter and store
 * the array there.
 */

static struct {
	unsigned long address;
	unsigned short segment;
} bios32_indirect = { 0, __KERNEL_CS };

/*
 * Returns the entry point for the given service, NULL on error
 */
//返回给定服务的入口点 找到返回地址  否则返回NULL
static unsigned long bios32_service(unsigned long service)
{
	unsigned char return_code;	/* %al */
	unsigned long address;		/* %ebx */
	unsigned long length;		/* %ecx */
	unsigned long entry;		/* %edx */
	unsigned long flags;

	local_irq_save(flags);
	__asm__("lcall *(%%edi); cld"  //bios32服务目录的功能比较单一 他只确定某个32为的BIOS服务是否被支持 
		: "=a" (return_code),
		  "=b" (address),
		  "=c" (length),
		  "=d" (entry)
		: "0" (service),
		  "1" (0),
		  "D" (&bios32_indirect));
	local_irq_restore(flags);

	switch (return_code) {
		case 0:
			return address + entry;
			
		case 0x80:	/* Not present */
			printk(KERN_WARNING "bios32_service(0x%lx): not present\n", service);
			return 0;
		default: /* Shouldn't happen */
			printk(KERN_WARNING "bios32_service(0x%lx): returned 0x%x -- BIOS bug!\n",
				service, return_code);
			return 0;
	}
}

static struct {
	unsigned long address;
	unsigned short segment;
} pci_indirect = { 0, __KERNEL_CS };

static int pci_bios_present; //是否可以通过pci bios获得pci配置空间

//检测pci bios服务是否可以使用
static int __devinit check_pcibios(void)
{
	u32 signature, eax, ebx, ecx;
	u8 status, major_ver, minor_ver, hw_mech;
	unsigned long flags, pcibios_entry;

	//看是否存在PCI服务 
	if ((pcibios_entry = bios32_service(PCI_SERVICE))) 
	{
	    //保存pci服务的入口地址
		pci_indirect.address = pcibios_entry + PAGE_OFFSET;

		local_irq_save(flags);


		/*
          功能入口: AH 寄存器值为B1h AL寄存器为 0x01 即PCIBIOS_PCI_BIOS_PRESENT
          功能出口: edx 存放PCI签名即"pci"
                    AH存放状态  00表示存在  否则不存在
                    AL存放硬件支持状态 位0设置表示支持机制#1 
                                       位1设置表示支持机制#2 
                                       位4设置表示机制#1支持Special Cycle
                                       位5设置表示机制#2支持 Special Cycle
                                       
                    BX为接口版本号
                    CL:表示系统中最后一个PCI总线编号
                    CF:为进位寄存器 如果设备说明有错误 被当成pci bios不指出存在
		*/
		__asm__(
			"lcall *(%%edi); cld\n\t" //调用pci bios服务 传入功能号为PCIBIOS_PCI_BIOS_PRESENT,pci bios 接口函数集是否存在 
			"jc 1f\n\t"
			"xor %%ah, %%ah\n"
			"1:"
			: "=d" (signature), //edx 赋值到signature
			  "=a" (eax),  //
			  "=b" (ebx),
			  "=c" (ecx)
			: "1" (PCIBIOS_PCI_BIOS_PRESENT),//PCIBIOS_PCI_BIOS_PRESENT常量输入到eax
			  "D" (&pci_indirect)
			: "memory");
		local_irq_restore(flags);

		status = (eax >> 8) & 0xff;//AH存放的状态  	
		hw_mech = eax & 0xff; //AL存放硬件支持状态
		
		major_ver = (ebx >> 8) & 0xff;//得到接口的主版本号
		minor_ver = ebx & 0xff; //得到接口的次版本号
		
		if (pcibios_last_bus < 0)
			pcibios_last_bus = ecx & 0xff; //获得系统中最后一个总线编号
		
		DBG("PCI: BIOS probe returned s=%02x hw=%02x ver=%02x.%02x l=%02x\n",
			status, hw_mech, major_ver, minor_ver, pcibios_last_bus);

		
		if (status ||                  //检测硬件状态是否不为0   00表示存在
			signature != PCI_SIGNATURE) //上面汇编返回的签名是否正确
		{
			printk (KERN_ERR "PCI: BIOS BUG #%x[%08x] found\n",
				status, signature);
			return 0;
		}
		
		printk(KERN_INFO "PCI: PCI BIOS revision %x.%02x entry at 0x%lx, last bus=%d\n",
			major_ver, minor_ver, pcibios_entry, pcibios_last_bus);

#ifdef CONFIG_PCI_DIRECT
		if (!(hw_mech & PCIBIOS_HW_TYPE1))
			pci_probe &= ~PCI_PROBE_CONF1;//硬件不支持机制#1 禁止使用机制#1来配置pci空间
		if (!(hw_mech & PCIBIOS_HW_TYPE2))
			pci_probe &= ~PCI_PROBE_CONF2;//硬件不支持机制#2 禁止使用机制#2来配置pci空间
#endif
		return 1;
	}
	return 0;
}

static int pci_bios_read(unsigned int seg, unsigned int bus,
			 unsigned int devfn, int reg, int len, u32 *value)
{
	unsigned long result = 0;
	unsigned long flags;
	unsigned long bx = (bus << 8) | devfn;

	if (!value || (bus > 255) || (devfn > 255) || (reg > 255))
		return -EINVAL;

	spin_lock_irqsave(&pci_config_lock, flags);

	switch (len) {
	case 1:
		__asm__("lcall *(%%esi); cld\n\t"
			"jc 1f\n\t"
			"xor %%ah, %%ah\n"
			"1:"
			: "=c" (*value),
			  "=a" (result)
			: "1" (PCIBIOS_READ_CONFIG_BYTE),
			  "b" (bx),
			  "D" ((long)reg),
			  "S" (&pci_indirect));
		/*
		 * Zero-extend the result beyond 8 bits, do not trust the
		 * BIOS having done it:
		 */
		*value &= 0xff;
		break;
	case 2:
		__asm__("lcall *(%%esi); cld\n\t"
			"jc 1f\n\t"
			"xor %%ah, %%ah\n"
			"1:"
			: "=c" (*value),
			  "=a" (result)
			: "1" (PCIBIOS_READ_CONFIG_WORD),
			  "b" (bx),
			  "D" ((long)reg),
			  "S" (&pci_indirect));
		/*
		 * Zero-extend the result beyond 16 bits, do not trust the
		 * BIOS having done it:
		 */
		*value &= 0xffff;
		break;
	case 4:
		__asm__("lcall *(%%esi); cld\n\t"
			"jc 1f\n\t"
			"xor %%ah, %%ah\n"
			"1:"
			: "=c" (*value),
			  "=a" (result)
			: "1" (PCIBIOS_READ_CONFIG_DWORD),
			  "b" (bx),
			  "D" ((long)reg),
			  "S" (&pci_indirect));
		break;
	}

	spin_unlock_irqrestore(&pci_config_lock, flags);

	return (int)((result & 0xff00) >> 8);
}

static int pci_bios_write(unsigned int seg, unsigned int bus,
			  unsigned int devfn, int reg, int len, u32 value)
{
	unsigned long result = 0;
	unsigned long flags;
	unsigned long bx = (bus << 8) | devfn;

	if ((bus > 255) || (devfn > 255) || (reg > 255)) 
		return -EINVAL;

	spin_lock_irqsave(&pci_config_lock, flags);

	switch (len) {
	case 1:
		__asm__("lcall *(%%esi); cld\n\t"
			"jc 1f\n\t"
			"xor %%ah, %%ah\n"
			"1:"
			: "=a" (result)
			: "0" (PCIBIOS_WRITE_CONFIG_BYTE),
			  "c" (value),
			  "b" (bx),
			  "D" ((long)reg),
			  "S" (&pci_indirect));
		break;
	case 2:
		__asm__("lcall *(%%esi); cld\n\t"
			"jc 1f\n\t"
			"xor %%ah, %%ah\n"
			"1:"
			: "=a" (result)
			: "0" (PCIBIOS_WRITE_CONFIG_WORD),
			  "c" (value),
			  "b" (bx),
			  "D" ((long)reg),
			  "S" (&pci_indirect));
		break;
	case 4:
		__asm__("lcall *(%%esi); cld\n\t"
			"jc 1f\n\t"
			"xor %%ah, %%ah\n"
			"1:"
			: "=a" (result)
			: "0" (PCIBIOS_WRITE_CONFIG_DWORD),
			  "c" (value),
			  "b" (bx),
			  "D" ((long)reg),
			  "S" (&pci_indirect));
		break;
	}

	spin_unlock_irqrestore(&pci_config_lock, flags);

	return (int)((result & 0xff00) >> 8);
}


/*
 * Function table for BIOS32 access
 */

static struct pci_raw_ops pci_bios_access = {
	.read =		pci_bios_read,
	.write =	pci_bios_write
};

/*
 * Try to find PCI BIOS.
 */

/*
返回值：
   NULL 说明pcibios不可行
   返回 &pci_bios_access 
*/
static struct pci_raw_ops * __devinit pci_find_bios(void)
{
	union bios32 *check;
	unsigned char sum;
	int i, length;

	/*
	 * Follow the standard procedure for locating the BIOS32 Service
	 * directory by scanning the permissible address range from
	 * 0xe0000 through 0xfffff for a valid BIOS32 structure.
	 */
    //在BIOS中查找BIOS32服务目录 
    //遵循标准的查找BISO32服务目录的过程 在BIOS ROM空间的可允许地址范围(0xe0000 ~ 0xffff0)内扫面有效的bios32结构 步长为16字节
	for (check = (union bios32 *) __va(0xe0000);check <= (union bios32 *) __va(0xffff0); ++check) 
    {
		long sig;
		if (probe_kernel_address(&check->fields.signature, sig))
			continue;

		//对签名进行检查
		if (check->fields.signature != BIOS32_SIGNATURE)
			continue;

		//获得数据长度  以16字节为单位
		length = check->fields.length * 16;
		if (!length)
			continue;

		//进行校验和计算
		sum = 0;
		for (i = 0; i < length ; ++i)
			sum += check->chars[i];
		if (sum != 0)
			continue;

       //暂时修改号为0 
		if (check->fields.revision != 0) 
		{
			printk("PCI: unsupported BIOS32 revision %d at 0x%p\n",
				check->fields.revision, check);
			continue;
		}
		DBG("PCI: BIOS32 Service Directory structure at 0x%p\n", check);
        //bios32服务目录的入口点  此入口地址不能处于高端地址
		if (check->fields.entry >= 0x100000) 
		{
			printk("PCI: BIOS32 entry (0x%p) in high memory, "
					"cannot use.\n", check);
			return NULL;
		} 
		else 
		{
			unsigned long bios32_entry = check->fields.entry;
			DBG("PCI: BIOS32 Service Directory entry at 0x%lx\n",
					bios32_entry);
			bios32_indirect.address = bios32_entry + PAGE_OFFSET;//将入口点保存到bios32_indirect中
			if (check_pcibios()) //检查pci bios是否存在
				return &pci_bios_access;
		}
		break;	/* Hopefully more than one BIOS32 cannot happen... */
	}

	return NULL;
}

/*
 *  BIOS Functions for IRQ Routing
 */

struct irq_routing_options {
	u16 size;
	struct irq_info *table;
	u16 segment;
} __attribute__((packed));

//调用BIOS服务目录的API 来获得IRQ路由表
struct irq_routing_table * pcibios_get_irq_routing_table(void)
{
	struct irq_routing_options opt;
	struct irq_routing_table *rt = NULL;
	int ret, map;
	unsigned long page;

	if (!pci_bios_present)
		return NULL;
	page = __get_free_page(GFP_KERNEL);
	if (!page)
		return NULL;
	opt.table = (struct irq_info *) page;
	opt.size = PAGE_SIZE;
	opt.segment = __KERNEL_DS;

	DBG("PCI: Fetching IRQ routing table... ");
	__asm__("push %%es\n\t"
		"push %%ds\n\t"
		"pop  %%es\n\t"
		"lcall *(%%esi); cld\n\t"
		"pop %%es\n\t"
		"jc 1f\n\t"
		"xor %%ah, %%ah\n"
		"1:"
		: "=a" (ret),
		  "=b" (map),
		  "=m" (opt)
		: "0" (PCIBIOS_GET_ROUTING_OPTIONS),
		  "1" (0),
		  "D" ((long) &opt),
		  "S" (&pci_indirect),
		  "m" (opt)
		: "memory");
	DBG("OK  ret=%d, size=%d, map=%x\n", ret, opt.size, map);
	if (ret & 0xff00)
		printk(KERN_ERR "PCI: Error %02x when fetching IRQ routing table.\n", (ret >> 8) & 0xff);
	else if (opt.size) {
		rt = kmalloc(sizeof(struct irq_routing_table) + opt.size, GFP_KERNEL);
		if (rt) {
			memset(rt, 0, sizeof(struct irq_routing_table));
			rt->size = opt.size + sizeof(struct irq_routing_table);
			rt->exclusive_irqs = map;
			memcpy(rt->slots, (void *) page, opt.size);
			printk(KERN_INFO "PCI: Using BIOS Interrupt Routing Table\n");
		}
	}
	free_page(page);
	return rt;
}
EXPORT_SYMBOL(pcibios_get_irq_routing_table);

int pcibios_set_irq_routing(struct pci_dev *dev, int pin, int irq)
{
	int ret;

	__asm__("lcall *(%%esi); cld\n\t"
		"jc 1f\n\t"
		"xor %%ah, %%ah\n"
		"1:"
		: "=a" (ret)
		: "0" (PCIBIOS_SET_PCI_HW_INT),
		  "b" ((dev->bus->number << 8) | dev->devfn),
		  "c" ((irq << 8) | (pin + 10)),
		  "S" (&pci_indirect));
	return !(ret & 0xff00);
}
EXPORT_SYMBOL(pcibios_set_irq_routing);

void __init pci_pcbios_init(void)
{
	if ((pci_probe & PCI_PROBE_BIOS) && //没有从命令行i年至从 
		((raw_pci_ops = pci_find_bios()))) 
	{
		pci_bios_present = 1;//设置此值 标识pci bios可以使用
	}
}

