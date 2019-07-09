/*
 *	NET3	PLIP tuning facilities for the new Niibe PLIP.
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 */
 //parallel(����) line Internet protocol  ����������Э��
 
#ifndef _LINUX_IF_PLIP_H
#define _LINUX_IF_PLIP_H

#include <linux/sockios.h>

#define	SIOCDEVPLIP	SIOCDEVPRIVATE

struct plipconf
{
	unsigned short pcmd;//����PLIP_SET_TIMEOUT ���ȡPLIP_GET_TIMEOUT
	unsigned long  nibble;//���ݴ��䳬ʱʱ��
	unsigned long  trigger;//ͬ���źŴ��䳬ʱʱ��
};

#define PLIP_GET_TIMEOUT	0x1
#define PLIP_SET_TIMEOUT	0x2

#endif
