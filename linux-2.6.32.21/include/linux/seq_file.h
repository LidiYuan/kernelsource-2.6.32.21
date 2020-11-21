#ifndef _LINUX_SEQ_FILE_H
#define _LINUX_SEQ_FILE_H

#include <linux/types.h>
#include <linux/string.h>
#include <linux/mutex.h>
#include <linux/cpumask.h>
#include <linux/nodemask.h>

struct seq_operations;
struct file;
struct path;
struct inode;
struct dentry;

/*
������seq����Ļ���������������buf�Ƕ�̬����ģ�
��С��С��PAGE_SIZE��ͨ������ṹ��ͨ��struct file�ṹ�е�private_data��ָ���
*/
struct seq_file {
	char *buf;//seq���Ļ�����
	size_t size;//��������С
	size_t from;//��ʾ�ӻ�������ʼ����λ��Ϊƫ���� 
	size_t count;//�������д��ڵ����ݵĳ���
	loff_t index;//���ݼ�¼����ֵ
	loff_t read_pos;//��seq�������·����Ϣ��·���ַ���ת��Ϊ8��������
	u64 version;//�汾�ţ���struct file�İ汾�ŵĿ���
	struct mutex lock;//seq��
	const struct seq_operations *op;//seq�����ṹ������������ʾ�Ĳ�������
	void *private;//˽������
};

struct seq_operations {
	/*��һ������λ��ֵ��ָʾ��ʼ��ȡ��λ�á��������λ�õ�������ȫȡ���ڵײ�ʵ�֣���һ�����ֽ�Ϊ��λ��λ�ã�������һ��Ԫ�ص����к�*/
	void * (*start) (struct seq_file *m, loff_t *pos);//start ���������ȱ����ã����������������÷��ʵ���ʼ��

	void (*stop) (struct seq_file *m, 
		          void *v   //�����Լ���˽������,��start�з��ص�
		          ); 

	/*����˺�������NULL ����ֹ������ȡ����*/
	void * (*next) (struct seq_file *m, 
	                void *v,     //�û��Լ���˽������ 
	                loff_t *pos);//��¼������,���ڴ�����һ��Ҫ��ȡ�ļ�¼����λ��

    /*����˺�������С��0��ֵ����ֹ������ȡ����
	  ����˺������ش���0��ֵ������ջ������е�����  
      ����0���ʾ�ɹ�ִ��
	 */
	int (*show) (struct seq_file *m,   //��������䵽������
		         void *v);             //�û��Լ��Ŀ�������,�����Լ���ȡ��¼��λ�� 

};                                    

#define SEQ_SKIP 1

/**
 * seq_get_buf - get buffer to write arbitrary data to
 * @m: the seq_file handle
 * @bufp: the beginning of the buffer is stored here
 *
 * Return the number of bytes available in the buffer, or zero if
 * there's no space.
 */
static inline size_t seq_get_buf(struct seq_file *m, char **bufp)
{
	BUG_ON(m->count > m->size);
	if (m->count < m->size)
		*bufp = m->buf + m->count;
	else
		*bufp = NULL;

	return m->size - m->count;
}

/**
 * seq_commit - commit data to the buffer
 * @m: the seq_file handle
 * @num: the number of bytes to commit
 *
 * Commit @num bytes of data written to a buffer previously acquired
 * by seq_buf_get.  To signal an error condition, or that the data
 * didn't fit in the available space, pass a negative @num value.
 */
static inline void seq_commit(struct seq_file *m, int num)
{
	if (num < 0) {
		m->count = m->size;
	} else {
		BUG_ON(m->count + num > m->size);
		m->count += num;
	}
}

char *mangle_path(char *s, char *p, char *esc);
int seq_open(struct file *, const struct seq_operations *);
ssize_t seq_read(struct file *, char __user *, size_t, loff_t *);
loff_t seq_lseek(struct file *, loff_t, int);
int seq_release(struct inode *, struct file *);
int seq_escape(struct seq_file *, const char *, const char *);
int seq_putc(struct seq_file *m, char c);
int seq_puts(struct seq_file *m, const char *s);
int seq_write(struct seq_file *seq, const void *data, size_t len);

int seq_printf(struct seq_file *, const char *, ...)
	__attribute__ ((format (printf,2,3)));

int seq_path(struct seq_file *, struct path *, char *);
int seq_dentry(struct seq_file *, struct dentry *, char *);
int seq_path_root(struct seq_file *m, struct path *path, struct path *root,
		  char *esc);
int seq_bitmap(struct seq_file *m, const unsigned long *bits,
				   unsigned int nr_bits);
static inline int seq_cpumask(struct seq_file *m, const struct cpumask *mask)
{
	return seq_bitmap(m, cpumask_bits(mask), nr_cpu_ids);
}

static inline int seq_nodemask(struct seq_file *m, nodemask_t *mask)
{
	return seq_bitmap(m, mask->bits, MAX_NUMNODES);
}

int seq_bitmap_list(struct seq_file *m, const unsigned long *bits,
		unsigned int nr_bits);

static inline int seq_cpumask_list(struct seq_file *m,
				   const struct cpumask *mask)
{
	return seq_bitmap_list(m, cpumask_bits(mask), nr_cpu_ids);
}

static inline int seq_nodemask_list(struct seq_file *m, nodemask_t *mask)
{
	return seq_bitmap_list(m, mask->bits, MAX_NUMNODES);
}

int single_open(struct file *, int (*)(struct seq_file *, void *), void *);
int single_release(struct inode *, struct file *);
void *__seq_open_private(struct file *, const struct seq_operations *, int);
int seq_open_private(struct file *, const struct seq_operations *, int);
int seq_release_private(struct inode *, struct file *);

#define SEQ_START_TOKEN ((void *)1)

/*
 * Helpers for iteration over list_head-s in seq_files
 */

extern struct list_head *seq_list_start(struct list_head *head,
		loff_t pos);
extern struct list_head *seq_list_start_head(struct list_head *head,
		loff_t pos);
extern struct list_head *seq_list_next(void *v, struct list_head *head,
		loff_t *ppos);

#endif
