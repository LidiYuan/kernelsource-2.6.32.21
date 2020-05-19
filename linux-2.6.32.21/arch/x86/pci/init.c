#include <linux/pci.h>
#include <linux/init.h>
#include <asm/pci_x86.h>

/* arch_initcall has too random ordering, so call the initializers
   in the right sequence from here. */

/*
pci�������ÿռ��ж��ַ���,����ѡ������Ҫ���ں˱���ѡ������,��linux���������в���ָ��
1) ͨ������#1 ����
    ��raw_pci_ops����Ϊpci_direct_conf1�����������ÿռ�
    ͨ�������ض���32λI/O�ռ� ��0xcf8��0xcfc,�������ռ��Ӧ���ŵ������Ĵ���,�����ſ���cpu�ھֲ����߶�������I/O�ռ����˫�Ӳ���,�ͽ���IO����ת��Ϊpci���ߵ����ò���
    0xcf8���ڲ������ÿռ�ĵ�ַ(CONFIG_ADDRESS),0xcfc���ڱ������ÿռ�Ķ�д����
2) ͨ������#2 ����
    ��raw_pci_ops����Ϊpci_direct_conf2�����������ÿռ�
3) ͨ��MMCONF����
    ��raw_pci_ext_ops����Ϊpci_mmcfg�����������ÿռ�
4) ͨ��BIOS����
    ��raw_pci_ext_ops����Ϊpci_bios_access���������ÿռ�
*/
static __init int pci_arch_init(void)
{

   /*1)�ں˱���������CONFIG_PCI_DIRECTʹ�û���#1����pci���ÿռ�
       
   */
#ifdef CONFIG_PCI_DIRECT
	int type = 0;
	type = pci_direct_probe(); //���û���1 �����2�������ÿռ�
#endif

	if (!(pci_probe & PCI_PROBE_NOEARLY))
		pci_mmcfg_early_init(); //���ó�ͨ��mmconf�������ÿռ�

#ifdef CONFIG_PCI_OLPC
	if (!pci_olpc_init())
		return 0;	/* skip additional checks if it's an XO */
#endif

#ifdef CONFIG_PCI_BIOS
	pci_pcbios_init(); //����ͨ��bios�������ÿռ�
#endif
	/*
	 * don't check for raw_pci_ops here because we want pcbios as last
	 * fallback, yet it's needed to run first to set pcibios_last_bus
	 * in case legacy PCI probing is used. otherwise detecting peer busses
	 * fails.
	 */

#ifdef CONFIG_PCI_DIRECT
	pci_direct_init(type);//�����������������ÿռ��������
#endif

	if (!raw_pci_ops && !raw_pci_ext_ops)
		printk(KERN_ERR"PCI: Fatal: No config space access function found\n");

	dmi_check_pciprobe();

	dmi_check_skip_isa_align();

	return 0;
}
arch_initcall(pci_arch_init);
