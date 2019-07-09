#ifndef _LINUX_CDEV_H
#define _LINUX_CDEV_H

#include <linux/kobject.h>
#include <linux/kdev_t.h>
#include <linux/list.h>

struct file_operations;
struct inode;
struct module;

/*
��1��MKDEV(���豸�ţ����豸��)
��2��MAJOR(dev_t dev)
��3��MINOR(dev_t dev)

  alloc_chrdev_region() ��̬�������豸��
  unregister_chrdev_region() ע���豸��

  class_create();������/dev/xx�´����ڵ�
  class_destroy();
  device_create()
  device_destroy();
  
*/

struct cdev {
	struct kobject kobj;
	struct module *owner;
	const struct file_operations *ops; //�豸����������
	struct list_head list;
	dev_t dev; //�豸��
	unsigned int count; //�豸��
};

//��ʼ��һ���ַ��豸������cdev{}
void cdev_init(struct cdev *, const struct file_operations *);


struct cdev *cdev_alloc(void);

void cdev_put(struct cdev *p);


//ע��һ���ַ��豸
int cdev_add(struct cdev *, dev_t, unsigned);

//ע��һ���ַ��豸
void cdev_del(struct cdev *);

int cdev_index(struct inode *inode);

void cd_forget(struct inode *);

extern struct backing_dev_info directly_mappable_cdev_bdi;

#endif
