/*
 * 	connector.h
 * 
 * 2004-2005 Copyright (c) Evgeniy Polyakov <johnpol@2ka.mipt.ru>
 * All rights reserved.
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef __CONNECTOR_H
#define __CONNECTOR_H

#include <linux/types.h>

/*
 * Process Events connector unique ids -- used for message routing
 */
#define CN_IDX_PROC			0x1
#define CN_VAL_PROC			0x1
#define CN_IDX_CIFS			0x2
#define CN_VAL_CIFS                     0x1
#define CN_W1_IDX			0x3	/* w1 communication */
#define CN_W1_VAL			0x1
#define CN_IDX_V86D			0x4
#define CN_VAL_V86D_UVESAFB		0x1
#define CN_IDX_BB			0x5	/* BlackBoard, from the TSP GPL sampling framework */
#define CN_DST_IDX			0x6
#define CN_DST_VAL			0x1
#define CN_IDX_DM			0x7	/* Device Mapper */
#define CN_VAL_DM_USERSPACE_LOG		0x1

#define CN_NETLINK_USERS		8

/*
 * Maximum connector's message size.
 */
#define CONNECTOR_MAX_MSG_SIZE		16384

/*
 * idx and val are unique identifiers which 
 * are used for message routing and 
 * must be registered in connector.h for in-kernel usage.
 */
//�ṹ cb_id ��������ʵ���ı�ʶ ID��������ȷ�� netlink ��Ϣ��ص������Ķ�Ӧ��ϵ�������������յ���ʶ ID Ϊ {idx��val} �� netlink ��Ϣʱ��ע��Ļص����� void (*callback) (void *) �������á��ûص������Ĳ���Ϊ�ṹ struct cn_msg ��ָ��
struct cb_id {
	__u32 idx;
	__u32 val;
};

//�������������Ϣͷ
struct cn_msg {
	struct cb_id id;

	//�ֶ� seq �� ack ����ȷ����Ϣ�Ŀɿ�����
	//netlink ���ڴ���ŵ�����¿��ܶ�ʧ��Ϣ����˸�ͷʹ��˳��ź���Ӧ��������Ҫ��ɿ������û�������
	//��������Ϣʱ���û���Ҫ���ö�һ�޶���˳��ź��������Ӧ�ţ�˳���ҲӦ�����õ� nlmsghdr->nlmsg_seq
	__u32 seq;
	__u32 ack;

	__u16 len;		/* Length of the following data */
	__u16 flags;
	__u8 data[0];
};

#ifdef __KERNEL__

#include <asm/atomic.h>

#include <linux/list.h>
#include <linux/workqueue.h>

#include <net/sock.h>

#define CN_CBQ_NAMELEN		32

struct cn_queue_dev {
	atomic_t refcnt;
	unsigned char name[CN_CBQ_NAMELEN];

	struct workqueue_struct *cn_queue;

	/* Sent to kevent to create cn_queue only when needed */
	struct work_struct wq_creation;

	/* Tell if the wq_creation job is pending/completed */
	atomic_t wq_requested;
	/* Wait for cn_queue to be created */
	wait_queue_head_t wq_created;

	struct list_head queue_list;
	spinlock_t queue_lock;

	struct sock *nls;
};

struct cn_callback_id {
	unsigned char name[CN_CBQ_NAMELEN];
	struct cb_id id;
};

struct cn_callback_data {
	struct sk_buff *skb;
	void (*callback) (struct cn_msg *, struct netlink_skb_parms *);

	void *free;
};

struct cn_callback_entry {
	struct list_head callback_entry;
	struct work_struct work;
	struct cn_queue_dev *pdev;

	struct cn_callback_id id;
	struct cn_callback_data data;

	u32 seq, group;
};

struct cn_dev {
	struct cb_id id;

	u32 seq, groups;

	//netlink sock
	struct sock *nls;

	//��Ϣ���ջص�����
	void (*input) (struct sk_buff *skb);

	
	struct cn_queue_dev *cbdev;
};

//������������ע���µ�������ʵ���Լ���Ӧ�Ļص����������� id ָ��ע��ı�ʶ ID������ name ָ���������ص������ķ����������� callback Ϊ�ص�����
int cn_add_callback(struct cb_id *, char *,  void (*callback) (struct cn_msg *, struct netlink_skb_parms *));

//����ж�ػص����������� id Ϊע�ắ�� cn_add_callback ע�����������ʶ ID
void cn_del_callback(struct cb_id *);

//������������鷢����Ϣ�����������κ������İ�ȫ�ص��á����ǣ�����ڴ治�㣬���ܻᷢ��ʧ�ܡ��ھ����������ʵ���У��ú����������û�̬���� netlink ��Ϣ
/*
���� msg Ϊ���͵� netlink ��Ϣ����Ϣͷ
���� __group Ϊ������Ϣ���飬�����Ϊ 0����ô����������������ע����������û������ս����͸��û� ID ���� msg �е� ID ��ͬ���飬����� __group ��Ϊ 0����Ϣ�����͸� __group ָ������
���� gfp_mask ָ��ҳ�����־
��ע���µĻص�����ʱ����������ָ��������Ϊ id.idx��
*/
int cn_netlink_send(struct cn_msg *msg, u32 __group, gfp_t gfp_mask);


int cn_queue_add_callback(struct cn_queue_dev *dev, char *name, struct cb_id *id, void (*callback)(struct cn_msg *, struct netlink_skb_parms *));

void cn_queue_del_callback(struct cn_queue_dev *dev, struct cb_id *id);

int queue_cn_work(struct cn_callback_entry *cbq, struct work_struct *work);

struct cn_queue_dev *cn_queue_alloc_dev(char *name, struct sock *);
void cn_queue_free_dev(struct cn_queue_dev *dev);

int cn_cb_equal(struct cb_id *, struct cb_id *);

void cn_queue_wrapper(struct work_struct *work);

#endif				/* __KERNEL__ */
#endif				/* __CONNECTOR_H */
