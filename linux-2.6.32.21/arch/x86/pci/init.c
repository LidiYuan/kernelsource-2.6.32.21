#include <linux/pci.h>
#include <linux/init.h>
#include <asm/pci_x86.h>

/* arch_initcall has too random ordering, so call the initializers
   in the right sequence from here. */

/*
pci访问配置空间有多种方法,具体选择哪种要看内核编译选择哪种,或linux启动命令行参数指定
1) 通过机制#1 访问
    将raw_pci_ops设置为pci_direct_conf1用来访问配置空间
    通过两个特定的32位I/O空间 即0xcf8和0xcfc,这两个空间对应主桥的两个寄存器,当主桥看到cpu在局部总线对这两个I/O空间进行双子操作,就将该IO操作转变为pci总线的配置操作
    0xcf8用于产生配置空间的地址(CONFIG_ADDRESS),0xcfc用于保存配置空间的读写数据
2) 通过机制#2 访问
    将raw_pci_ops设置为pci_direct_conf2用来访问配置空间
3) 通过MMCONF访问
    将raw_pci_ext_ops设置为pci_mmcfg用来访问配置空间
4) 通过BIOS访问
    将raw_pci_ext_ops设置为pci_bios_access来访问配置空间
*/
static __init int pci_arch_init(void)
{

   /*1)内核编译配置了CONFIG_PCI_DIRECT使用机制#1访问pci配置空间
       
   */
#ifdef CONFIG_PCI_DIRECT
	int type = 0;
	type = pci_direct_probe(); //设置机制1 或机制2访问配置空间
#endif

	if (!(pci_probe & PCI_PROBE_NOEARLY))
		pci_mmcfg_early_init(); //设置成通过mmconf访问配置空间

#ifdef CONFIG_PCI_OLPC
	if (!pci_olpc_init())
		return 0;	/* skip additional checks if it's an XO */
#endif

#ifdef CONFIG_PCI_BIOS
	pci_pcbios_init(); //设置通过bios访问配置空间
#endif
	/*
	 * don't check for raw_pci_ops here because we want pcbios as last
	 * fallback, yet it's needed to run first to set pcibios_last_bus
	 * in case legacy PCI probing is used. otherwise detecting peer busses
	 * fails.
	 */

#ifdef CONFIG_PCI_DIRECT
	pci_direct_init(type);//根据类型来设置配置空间操作函数
#endif

	if (!raw_pci_ops && !raw_pci_ext_ops)
		printk(KERN_ERR"PCI: Fatal: No config space access function found\n");

	dmi_check_pciprobe();

	dmi_check_skip_isa_align();

	return 0;
}
arch_initcall(pci_arch_init);
