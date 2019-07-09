#ifndef _IPX_H_
#define _IPX_H_
#include <linux/types.h>
#include <linux/sockios.h>
#include <linux/socket.h>
#define IPX_NODE_LEN	6
#define IPX_MTU		576

//IPXЭ��ӿڵ�ַ���� ������sockaddr_in sockaddr_un
//�˽ṹΪ16�ֽ�
struct sockaddr_ipx {
	sa_family_t	sipx_family; //AF_IPX
	__be16		sipx_port; //�˿ں�
	__be32		sipx_network;//�����ַ
	unsigned char 	sipx_node[IPX_NODE_LEN];//�ڵ��ַ һ��ΪӲ����ַ
	__u8		sipx_type;
	unsigned char	sipx_zero;	/* 16 byte fill */
};

/*
 * So we can fit the extra info for SIOCSIFADDR into the address nicely
 */
#define sipx_special	sipx_port
#define sipx_action	sipx_zero
#define IPX_DLTITF	0
#define IPX_CRTITF	1

//����ipx��·�ɱ��·�ɱ���Ǵ˽ṹ��ʾ����ipx_route 
//ipx_route_definition���ڴ���һ��ipx_route ����һ����Ϣ�����м�ṹ��
struct ipx_route_definition {
	__be32        ipx_network;
	__be32        ipx_router_network;
	unsigned char ipx_router_node[IPX_NODE_LEN];
};


//���û��ȡ�����ӿ��豸��Ϣ,��ʹ��ipxͬ�еĻ�����ÿ������ӿڶ���һ��ipx_interface �ṹ��ʾ
struct ipx_interface_definition {
	__be32        ipx_network;
	unsigned char ipx_device[16];

//ipx��·���װЭ�� ȡֵ���³���	
	unsigned char ipx_dlink_type;
#define IPX_FRAME_NONE		0

/*
------------------------------------------------------------------------------------
|����Ӳ����ַ   | ����Ӳ����ַ  | ����   | IEEE802.2�ײ�  | Code | ����  | IPX���� |
------------------------------------------------------------------------------------
*/
#define IPX_FRAME_SNAP		1 //ethnet SNAP


/*
------------------------------------------------------------------------
|����Ӳ����ַ   | ����Ӳ����ַ | ����  |DSAP | SSAP  | CTRL  | IPX���� |
------------------------------------------------------------------------
*/
#define IPX_FRAME_8022		2 //IEEE802.2

/*
--------------------------------------------------
|����Ӳ����ַ  | ����Ӳ����ַ  | ����  | ipx���� |
--------------------------------------------------
*/
#define IPX_FRAME_ETHERII	3  //Eternet II

/*
---------------------------------------------------------
|���Ͷ�Ӳ����ַ  |  ���ն�Ӳ����ַ  | ����   |  ipx���� |
---------------------------------------------------------
*/
#define IPX_FRAME_8023		4  //802.3
#define IPX_FRAME_TR_8022       5 /* obsolete */
    
	unsigned char ipx_special;//���ֶ�ȡֵ������������
#define IPX_SPECIAL_NONE	0
#define IPX_PRIMARY		1
#define IPX_INTERNAL		2
	unsigned char ipx_node[IPX_NODE_LEN];
};

struct ipx_config_data {
	unsigned char	ipxcfg_auto_select_primary;//���������ں˱���ipxcfg_auto_select_primary
	unsigned char	ipxcfg_auto_create_interfaces;//���������ں˱���ipxcfg_auto_create_interfaces
};

/*
 * OLD Route Definition for backward compatibility.
 */

//�ṹ����ͬipx_route_definition  �ýṹΪ�����ݱ�����
struct ipx_route_def {
	__be32		ipx_network;
	__be32		ipx_router_network;
#define IPX_ROUTE_NO_ROUTER	0
	unsigned char	ipx_router_node[IPX_NODE_LEN];
	unsigned char	ipx_device[16];
	unsigned short	ipx_flags;
#define IPX_RT_SNAP		8
#define IPX_RT_8022		4
#define IPX_RT_BLUEBOOK		2
#define IPX_RT_ROUTED		1
};

//����ipx_ioctrl �����������Ϣ���û��ȡ 
#define SIOCAIPXITFCRT		(SIOCPROTOPRIVATE)
#define SIOCAIPXPRISLT		(SIOCPROTOPRIVATE + 1)
#define SIOCIPXCFGDATA		(SIOCPROTOPRIVATE + 2)
#define SIOCIPXNCPCONN		(SIOCPROTOPRIVATE + 3)
#endif /* _IPX_H_ */
