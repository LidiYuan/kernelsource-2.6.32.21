/*
 *	PCI BIOS function numbering for conventional PCI BIOS 
 *	systems
 */
/*
����PCI BIOS���ܷ���ʹ�õĹ��ܺ�ΪB1h ����Ϊ�ӹ��ܺ�
*/
#define PCIBIOS_PCI_FUNCTION_ID 	0xb1XX 
#define PCIBIOS_PCI_BIOS_PRESENT 	0xb101 //ȷ��pci bios �ӿں������Ƿ���� �Լ���ǰ�ӿڰ汾�� 
#define PCIBIOS_FIND_PCI_DEVICE		0xb102 //���ظ����豸ID/����ID��pci�豸���� ��� ���ܱ��
#define PCIBIOS_FIND_PCI_CLASS_CODE	0xb103 //���ظ������ͱ����pci�豸���豸���� ��� ���ܱ��
#define PCIBIOS_GENERATE_SPECIAL_CYCLE	0xb106 //����PCI�ض�����
#define PCIBIOS_READ_CONFIG_BYTE	0xb108 //�Ӹ����豸�����ÿռ��ж�ȡһ���ֽ�
#define PCIBIOS_READ_CONFIG_WORD	0xb109 //�Ӹ����豸�����ÿռ��ж�ȡһ����
#define PCIBIOS_READ_CONFIG_DWORD	0xb10a //�Ӹ����豸�����ÿռ��ж�ȡһ��˫��
#define PCIBIOS_WRITE_CONFIG_BYTE	0xb10b //�������豸�����ÿռ���д��һ���ֽ�
#define PCIBIOS_WRITE_CONFIG_WORD	0xb10c //�������豸�����ÿռ���д��һ����
#define PCIBIOS_WRITE_CONFIG_DWORD	0xb10d //�������豸�����ÿռ���д��һ��˫��
#define PCIBIOS_GET_ROUTING_OPTIONS	0xb10e //�����ж�·��ѡ�� 
#define PCIBIOS_SET_PCI_HW_INT		0xb10f //����Ӳ���ж� (����PCI�ж�����INT#��ISA�жϺ�)

