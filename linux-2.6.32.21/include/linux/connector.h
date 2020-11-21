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
//结构 cb_id 是连接器实例的标识 ID，它用于确定 netlink 消息与回调函数的对应关系。当连接器接收到标识 ID 为 {idx，val} 的 netlink 消息时，注册的回调函数 void (*callback) (void *) 将被调用。该回调函数的参数为结构 struct cn_msg 的指针
struct cb_id {
	__u32 idx;
	__u32 val;
};

//连接器定义的消息头
struct cn_msg {
	struct cb_id id;

	//字段 seq 和 ack 用于确保消息的可靠传输
	//netlink 在内存紧张的情况下可能丢失消息，因此该头使用顺序号和响应号来满足要求可靠传输用户的需求
	//当发送消息时，用户需要设置独一无二的顺序号和随机的响应号，顺序号也应当设置到 nlmsghdr->nlmsg_seq
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

	//消息接收回调函数
	void (*input) (struct sk_buff *skb);

	
	struct cn_queue_dev *cbdev;
};

//用于向连接器注册新的连接器实例以及相应的回调函数，参数 id 指定注册的标识 ID，参数 name 指定连接器回调函数的符号名，参数 callback 为回调函数
int cn_add_callback(struct cb_id *, char *,  void (*callback) (struct cn_msg *, struct netlink_skb_parms *));

//用于卸载回调函数，参数 id 为注册函数 cn_add_callback 注册的连接器标识 ID
void cn_del_callback(struct cb_id *);

//用于向给定的组发送消息，它可以在任何上下文安全地调用。但是，如果内存不足，可能会发送失败。在具体的连接器实例中，该函数用于向用户态发送 netlink 消息
/*
参数 msg 为发送的 netlink 消息的消息头
参数 __group 为接收消息的组，如果它为 0，那么连接器将搜索所有注册的连接器用户，最终将发送给用户 ID 与在 msg 中的 ID 相同的组，但如果 __group 不为 0，消息将发送给 __group 指定的组
参数 gfp_mask 指定页分配标志
当注册新的回调函数时，连接器将指定它的组为 id.idx。
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
