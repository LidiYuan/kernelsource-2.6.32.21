/*
 *	PCI BIOS function numbering for conventional PCI BIOS 
 *	systems
 */
/*
所有PCI BIOS功能服务使用的功能号为B1h 下面为子功能号
*/
#define PCIBIOS_PCI_FUNCTION_ID 	0xb1XX 
#define PCIBIOS_PCI_BIOS_PRESENT 	0xb101 //确定pci bios 接口函数集是否存在 以及当前接口版本号 
#define PCIBIOS_FIND_PCI_DEVICE		0xb102 //返回给定设备ID/厂商ID的pci设备总线 插槽 功能编号
#define PCIBIOS_FIND_PCI_CLASS_CODE	0xb103 //返回给定类型编码的pci设备的设备总线 插槽 功能编号
#define PCIBIOS_GENERATE_SPECIAL_CYCLE	0xb106 //生成PCI特定周期
#define PCIBIOS_READ_CONFIG_BYTE	0xb108 //从给定设备的配置空间中读取一个字节
#define PCIBIOS_READ_CONFIG_WORD	0xb109 //从给定设备的配置空间中读取一个字
#define PCIBIOS_READ_CONFIG_DWORD	0xb10a //从给定设备的配置空间中读取一个双字
#define PCIBIOS_WRITE_CONFIG_BYTE	0xb10b //往给定设备的配置空间中写入一个字节
#define PCIBIOS_WRITE_CONFIG_WORD	0xb10c //往给定设备的配置空间中写入一个字
#define PCIBIOS_WRITE_CONFIG_DWORD	0xb10d //往给定设备的配置空间中写入一个双字
#define PCIBIOS_GET_ROUTING_OPTIONS	0xb10e //查找中断路由选项 
#define PCIBIOS_SET_PCI_HW_INT		0xb10f //设置硬件中断 (即从PCI中断引脚INT#到ISA中断号)

