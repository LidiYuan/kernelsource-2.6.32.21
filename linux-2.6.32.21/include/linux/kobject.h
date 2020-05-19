/*
 * kobject.h - generic kernel object infrastructure.
 *
 * Copyright (c) 2002-2003 Patrick Mochel
 * Copyright (c) 2002-2003 Open Source Development Labs
 * Copyright (c) 2006-2008 Greg Kroah-Hartman <greg@kroah.com>
 * Copyright (c) 2006-2008 Novell Inc.
 *
 * This file is released under the GPLv2.
 *
 * Please read Documentation/kobject.txt before using the kobject
 * interface, ESPECIALLY the parts about reference counts and object
 * destructors.
 */

#ifndef _KOBJECT_H_
#define _KOBJECT_H_

#include <linux/types.h>
#include <linux/list.h>
#include <linux/sysfs.h>
#include <linux/compiler.h>
#include <linux/spinlock.h>
#include <linux/kref.h>
#include <linux/kernel.h>
#include <linux/wait.h>
#include <asm/atomic.h>

#define UEVENT_HELPER_PATH_LEN		256
#define UEVENT_NUM_ENVP			32	/* number of env pointers */
#define UEVENT_BUFFER_SIZE		2048	/* buffer for the variables */

/* path to the userspace helper executed on an event */
extern char uevent_helper[];

/* counter to tag the uevent, read only except for the kobject core */
extern u64 uevent_seqnum;

/*
 * The actions here must match the index to the string array
 * in lib/kobject_uevent.c
 *
 * Do not add new actions here without checking with the driver-core
 * maintainers. Action strings are not meant to express subsystem
 * or device specific properties. In most cases you want to send a
 * kobject_uevent_env(kobj, KOBJ_CHANGE, env) with additional event
 * specific variables added to the event environment.
 */
enum kobject_action {
	KOBJ_ADD, //��ϵͳ���һ��kset����
	KOBJ_REMOVE, //�ں˶����Ƴ�
	KOBJ_CHANGE, //�ں˶������仯
	KOBJ_MOVE,  //�ں˶����ƶ�
	KOBJ_ONLINE, //����cpu ����
	KOBJ_OFFLINE,//cpu�Ƴ�
	KOBJ_MAX
};

/*kobject_init()��ʼ��һ��kobect����

  kobject_add()  1: ����kobect�����Ĳ�ι�ϵ  2��sysfs�ļ�ϵͳ�н���һ��Ŀ¼
  kobect_del()  ���ļ�����ɾ��kobject
  
  kobject_init_and_add()  �൱��������������������
  kobject_create() ��������ʼ��һ��kobect����
  kobject_create_and_add() ���� ��ʼ�� ����ӵ�sysfs�ļ�ϵͳ��
  
*/
struct kobject {
	const char		*name; //��ʾkobject��������֣���Ӧsysfs�µ�һ��Ŀ¼
	                       //kobject_set_name()
	                       
	struct list_head	entry;  //��kobject���ӵ�kset�����Ӽ�
	struct kobject		*parent;//��ָ��ǰkobject�������ָ�룬������sys�ṹ�о��ǰ�����ǰkobject�����Ŀ¼����
	struct kset		*kset;      //��kobject�Ѿ����ӵ�kset���ô�ָ��ָ����
	struct kobj_type	*ktype;//���ں˶���һ��sysfs�ļ�ϵͳ��صĲ�������������
	struct sysfs_dirent	*sd;   //���ں˶�����sysfs�ļ�ϵͳ�ж�Ӧ��Ŀ¼��ʵ��
	struct kref		kref; //�Ƕ�kobject�����ü����������ü���Ϊ0ʱ���ͻص�֮ǰע���release�����ͷŸö���
	unsigned int state_initialized:1;// 1��ʾ�Ѿ���ʼ��  0��ʾδ����ʼ��
	unsigned int state_in_sysfs:1;//��ʾ�Ѿ���sysfsϵͳ�н���һ����ڵ�
	unsigned int state_add_uevent_sent:1;//����¼��Ƿ����û��ռ�
	unsigned int state_remove_uevent_sent:1;//ɾ���¼��Ƿ����û��ռ�
	unsigned int uevent_suppress:1;// 1��ʾ�ڸö���״̬�����仯ʱ ��������kset���û��ռ䷢��uevent��Ϣ
};

extern int kobject_set_name(struct kobject *kobj, const char *name, ...)
			    __attribute__((format(printf, 2, 3)));
extern int kobject_set_name_vargs(struct kobject *kobj, const char *fmt,
				  va_list vargs);

static inline const char *kobject_name(const struct kobject *kobj)
{
	return kobj->name;
}

extern void kobject_init(struct kobject *kobj, struct kobj_type *ktype);
extern int __must_check kobject_add(struct kobject *kobj,
				                           struct kobject *parent,
				                           const char *fmt, ...);
extern int __must_check kobject_init_and_add(struct kobject *kobj,
					     struct kobj_type *ktype,
					     struct kobject *parent,
					     const char *fmt, ...);

extern void kobject_del(struct kobject *kobj);

extern struct kobject * __must_check kobject_create(void);
extern struct kobject * __must_check kobject_create_and_add(const char *name,
						struct kobject *parent);

extern int __must_check kobject_rename(struct kobject *, const char *new_name);
extern int __must_check kobject_move(struct kobject *, struct kobject *);

extern struct kobject *kobject_get(struct kobject *kobj);
extern void kobject_put(struct kobject *kobj);

extern char *kobject_get_path(struct kobject *kobj, gfp_t flag);

//kobj_type��Ŀ�����Ϊ��ͬ���͵�kobject�ṩ��ͬ�������Լ����ٷ���
//get_ktype();
struct kobj_type 
{
	void (*release)(struct kobject *kobj);//��kobject_put()�л�����ͷ�kobect����
	struct sysfs_ops *sysfs_ops; //��attribute���в���
	struct attribute **default_attrs;//ÿ�����Դ���һ����Ŀ¼�µ��ļ�  
	                                 //������/sysfs/�¼�����cat ���kobject
	                                 //��ô����/sysfs/cat/ ���Ŀ¼  ��ô���kobect������ size color
	                                 //�Ǿͻ���� /sysfs/cat/size  /sysfs/cat/color �������ļ�
};

//Uevent�¼������Ի������������ַ���������ʽ���͵��û��ռ�
struct kobj_uevent_env {
	char *envp[UEVENT_NUM_ENVP]; /*��������ָ������  ָ�������buf  ��buf���зֶ� ÿ�α�ʾһ��key=value�ַ�����\0��β*/
	int envp_idx; /*�����±�*/
	char buf[UEVENT_BUFFER_SIZE]; //��������buffer
	int buflen;//buf�����ݳ���
};

struct kset_uevent_ops 
{
	//���˻ص����� ����0 ��ʾ����Ҫ���û��ռ䱨����¼�
	int (*filter)(struct kset *kset, struct kobject *kobj);

    //���ֻص����� ������ϵͳ���� ����������SUBSYSTEM������
	const char *(*name)(struct kset *kset, struct kobject *kobj);

    //�����ϵͳ�ض��Ļ�������
	int (*uevent)(struct kset *kset, struct kobject *kobj,struct kobj_uevent_env *env);
};

struct kobj_attribute 
{
	struct attribute attr;

	//��ȡ���Ե�����
	ssize_t (*show)(struct kobject *kobj, struct kobj_attribute *attr,
			char *buf);

	//��ȡ���Ե�����
	ssize_t (*store)(struct kobject *kobj, struct kobj_attribute *attr,
			 const char *buf, size_t count);
};

extern struct sysfs_ops kobj_sysfs_ops;

/**
 * struct kset - a set of kobjects of a specific type, belonging to a specific subsystem.
 *
 * A kset defines a group of kobjects.  They can be individually
 * different "types" but overall these kobjects all want to be grouped
 * together and operated on in the same manner.  ksets are used to
 * define the attribute callbacks and other common events that happen to
 * a kobject.
 *
 * @list: the list of all kobjects for this kset
 * @list_lock: a lock for iterating over the kobjects
 * @kobj: the embedded kobject for this kset (recursion, isn't it fun...)
 * @uevent_ops: the set of uevent operations for this kset.  These are
 * called whenever a kobject has something happen to it so that the kset
 * can add new environment variables, or filter out the uevents if so
 * desired.
 */
 //һ��kobject�ļ���
 /*
kset_init();
kset_add();
kset_register();
kset_unregister();
kset_get()
kset_put();
*/
struct kset {
	struct list_head list;//���kset������kobject������
	spinlock_t list_lock;//����listʱ���������
	struct kobject kobj;//����ǰkset��kobj�ں˶���
	struct kset_uevent_ops *uevent_ops; //һ�麯��ָ�� ��kset�е�kobject����״̬�仯��Ҫ֪ͨ�û��ռ� �������еĺ�����ʵ��
};

//��ʼ��һ��kset����
extern void kset_init(struct kset *kset);

//������ʼ����ע��һ��kset����
extern int __must_check kset_register(struct kset *kset);

//ע��һ��kset����
extern void kset_unregister(struct kset *kset);

//��̬����һ��kset���������sysfsϵͳ��
//name:Ϊkset����
//u:kset�������������û��ռ�event��Ϣ�Ĳ�����
//parent_kobj:���ں˶���ָ��
extern struct kset * __must_check kset_create_and_add(const char *name,
						struct kset_uevent_ops *u,
						struct kobject *parent_kobj);

static inline struct kset *to_kset(struct kobject *kobj)
{
	return kobj ? container_of(kobj, struct kset, kobj) : NULL;
}

static inline struct kset *kset_get(struct kset *k)
{
	return k ? to_kset(kobject_get(&k->kobj)) : NULL;
}

static inline void kset_put(struct kset *k)
{
	kobject_put(&k->kobj);
}

static inline struct kobj_type *get_ktype(struct kobject *kobj)
{
	return kobj->ktype;
}

extern struct kobject *kset_find_obj(struct kset *, const char *);

/* The global /sys/kernel/ kobject for people to chain off of */
extern struct kobject *kernel_kobj;
/* The global /sys/kernel/mm/ kobject for people to chain off of */
extern struct kobject *mm_kobj;
/* The global /sys/hypervisor/ kobject for people to chain off of */
extern struct kobject *hypervisor_kobj;
/* The global /sys/power/ kobject for people to chain off of */
extern struct kobject *power_kobj;
/* The global /sys/firmware/ kobject for people to chain off of */
extern struct kobject *firmware_kobj;

#if defined(CONFIG_HOTPLUG)
int kobject_uevent(struct kobject *kobj, enum kobject_action action);
int kobject_uevent_env(struct kobject *kobj, enum kobject_action action,
			char *envp[]);

int add_uevent_var(struct kobj_uevent_env *env, const char *format, ...)
	__attribute__((format (printf, 2, 3)));

int kobject_action_type(const char *buf, size_t count,
			enum kobject_action *type);
#else
static inline int kobject_uevent(struct kobject *kobj,
				 enum kobject_action action)
{ return 0; }
static inline int kobject_uevent_env(struct kobject *kobj,
				      enum kobject_action action,
				      char *envp[])
{ return 0; }

static inline int add_uevent_var(struct kobj_uevent_env *env,
				 const char *format, ...)
{ return 0; }

static inline int kobject_action_type(const char *buf, size_t count,
				      enum kobject_action *type)
{ return -EINVAL; }
#endif

#endif /* _KOBJECT_H_ */
