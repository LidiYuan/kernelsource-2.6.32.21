/*
 * descriptor table internals; you almost certainly want file.h instead.
 */

#ifndef __LINUX_FDTABLE_H
#define __LINUX_FDTABLE_H

#include <linux/posix_types.h>
#include <linux/compiler.h>
#include <linux/spinlock.h>
#include <linux/rcupdate.h>
#include <linux/types.h>
#include <linux/init.h>

#include <asm/atomic.h>

/*
 * The default fd array needs to be at least BITS_PER_LONG,
 * as this is the granularity returned by copy_fdset().
 */
#define NR_OPEN_DEFAULT BITS_PER_LONG

/*
 * The embedded_fd_set is a small fd_set,
 * suitable for most tasks (which open <= BITS_PER_LONG files)
 */
struct embedded_fd_set {
	unsigned long fds_bits[1];
};

struct fdtable {
	unsigned int max_fds;
	struct file ** fd;      /* current fd array */// ָ��򿪵��ļ��������б���ָ�룬��ʼ��ʱ��ָ��fd_array
	fd_set *close_on_exec;// ִ��exec��Ҫ�رյ��ļ�������λͼ(fork��exec�������ӽ��̼̳е��ļ�������)
	fd_set *open_fds;//�򿪵��ļ�������λͼ
	struct rcu_head rcu;
	struct fdtable *next;
};

/*
 * Open file table structure
 *///����ÿ�����̣�����һ��files_struct�ṹ��������¼�ļ���������ʹ�����
struct files_struct {
  /*
   * read mostly part
   */
	atomic_t count;//ʹ�øñ��Ľ�����
	struct fdtable *fdt;
	struct fdtable fdtab;
  /*
   * written part on a separate cache line in SMP
   */
	spinlock_t file_lock ____cacheline_aligned_in_smp;
	int next_fd; //��ֵ��С������ر��ļ����ļ�������,��һ�����õ��ļ�������
	struct embedded_fd_set close_on_exec_init;;// ִ��execʱ��Ҫ�رյ��ļ���������ֵ����
	struct embedded_fd_set open_fds_init;// �ļ��������������ֳ�ֵ����
	struct file * fd_array[NR_OPEN_DEFAULT];//Ĭ�ϴ򿪵�fd����
};

#define files_fdtable(files) (rcu_dereference((files)->fdt))

struct file_operations;
struct vfsmount;
struct dentry;

extern int expand_files(struct files_struct *, int nr);
extern void free_fdtable_rcu(struct rcu_head *rcu);
extern void __init files_defer_init(void);

static inline void free_fdtable(struct fdtable *fdt)
{
	call_rcu(&fdt->rcu, free_fdtable_rcu);
}

static inline struct file * fcheck_files(struct files_struct *files, unsigned int fd)
{
	struct file * file = NULL;
	struct fdtable *fdt = files_fdtable(files);

	if (fd < fdt->max_fds)
		file = rcu_dereference(fdt->fd[fd]);
	return file;
}

/*
 * Check whether the specified fd has an open file.
 */
#define fcheck(fd)	fcheck_files(current->files, fd)

struct task_struct;

struct files_struct *get_files_struct(struct task_struct *);
void put_files_struct(struct files_struct *fs);
void reset_files_struct(struct files_struct *);
int unshare_files(struct files_struct **);
struct files_struct *dup_fd(struct files_struct *, int *);

extern struct kmem_cache *files_cachep;

#endif /* __LINUX_FDTABLE_H */