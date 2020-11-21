/*
  File: fs/xattr.c

  Extended attribute handling.

  Copyright (C) 2001 by Andreas Gruenbacher <a.gruenbacher@computer.org>
  Copyright (C) 2001 SGI - Silicon Graphics, Inc <linux-xfs@oss.sgi.com>
  Copyright (c) 2004 Red Hat, Inc., James Morris <jmorris@redhat.com>
 */
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/file.h>
#include <linux/xattr.h>
#include <linux/mount.h>
#include <linux/namei.h>
#include <linux/security.h>
#include <linux/syscalls.h>
#include <linux/module.h>
#include <linux/fsnotify.h>
#include <linux/audit.h>
#include <asm/uaccess.h>


/*
 * Check permissions for extended attribute access.  This is a bit complicated
 * because different namespaces have very different rules.
 */

/*
检查扩展属性的访问权限, 这有点复杂,因为不同的命名空间有不同的规则
*/
static int xattr_permission(struct inode *inode, const char *name, int mask)
{
	/*
	 * We can never set or remove an extended attribute on a read-only
	 * filesystem  or on an immutable / append-only inode.
	 */
	if (mask & MAY_WRITE) 
	{   
        //用chattr设置了 文件不可变属性, 或者设置了只能追加追加打开文件的属性
		if (IS_IMMUTABLE(inode) || IS_APPEND(inode))
			return -EPERM;
	}

	/*
	 * No restriction for security.* and system.* from the VFS.  Decision
	 * on these is left to the underlying filesystem / security module.
	 */
	//对于  security或者system命名空间的扩展属性 在此处不做决定, 
    //这些问题的决定权留给底层的文件系统/安全模块
	if (!strncmp(name, XATTR_SECURITY_PREFIX, XATTR_SECURITY_PREFIX_LEN) ||
	    !strncmp(name, XATTR_SYSTEM_PREFIX, XATTR_SYSTEM_PREFIX_LEN))
		return 0;

	/*
	 * The trusted.* namespace can only be accessed by a privileged user.
	 */
	 //对于trusted 命名空间 只能被有权限的用户访问 有(CAP_SYS_ADMIN)的权能
	if (!strncmp(name, XATTR_TRUSTED_PREFIX, XATTR_TRUSTED_PREFIX_LEN))
		return (capable(CAP_SYS_ADMIN) ? 0 : -EPERM);

	/* In user.* namespace, only regular files and directories can have
	 * extended attributes. For sticky directories, only the owner and
	 * privileged user can write attributes.
	 */
	//对于命名空间user,只有普通文件和目录可以有扩展属性, 对于sticky目录, 只有拥有者和
	//特权用户可以写入属性
	if (!strncmp(name, XATTR_USER_PREFIX, XATTR_USER_PREFIX_LEN)) 
	{
	    //判断是否为普通文件或者目录
		if (!S_ISREG(inode->i_mode) && !S_ISDIR(inode->i_mode))
			return -EPERM;

		//对于目录 设置了striky 那么 要写入属性，必须是拥有者或者特权用户
		if (S_ISDIR(inode->i_mode) && (inode->i_mode & S_ISVTX) &&
		    (mask & MAY_WRITE) && !is_owner_or_cap(inode))
			return -EPERM;
	}

	//继续对访问权限检测 此处是针对user的扩展属性, 检测文件rw属性
	return inode_permission(inode, mask);
}

/**
 *  __vfs_setxattr_noperm - perform setxattr operation without performing
 *  permission checks.
 *
 *  @dentry - object to perform setxattr on
 *  @name - xattr name to set
 *  @value - value to set @name to
 *  @size - size of @value
 *  @flags - flags to pass into filesystem operations
 *
 *  returns the result of the internal setxattr or setsecurity operations.
 *
 *  This function requires the caller to lock the inode's i_mutex before it
 *  is executed. It also assumes that the caller will make the appropriate
 *  permission checks.
 */
int __vfs_setxattr_noperm(struct dentry *dentry, //要设置扩展属性的对象
                                const char *name,   //属性名字
		                        const void *value,  //属性的值
		                        size_t size,        //属性值的长度
		                        int flags)          //标志位  
{
	struct inode *inode = dentry->d_inode;
	int error = -EOPNOTSUPP;


	//调用具体文件系统对此函数的实现方式, 
	if (inode->i_op->setxattr) 
	{
        //如ext4执行的是 generic_setxattr
		error = inode->i_op->setxattr(dentry, name, value, size, flags);
		if (!error) 
		{
		    //设置正常
		    //通知扩展属性事件
			fsnotify_xattr(dentry);

			//和lsm有关的安全机制
			security_inode_post_setxattr(dentry, name, value,size, flags);
		}
	} 
	else if (!strncmp(name, XATTR_SECURITY_PREFIX,XATTR_SECURITY_PREFIX_LEN)) 
	{
		const char *suffix = name + XATTR_SECURITY_PREFIX_LEN;
		error = security_inode_setsecurity(inode, suffix, value,size, flags);
		if (!error)
			fsnotify_xattr(dentry);
	}

	return error;
}


int vfs_setxattr(struct dentry *dentry, const char *name, const void *value,
		size_t size, int flags)
{
	struct inode *inode = dentry->d_inode;
	int error;

	//检查扩展属性是否有写权限
	error = xattr_permission(inode, name, MAY_WRITE);
	if (error)
		return error;

    //将文件的inode锁定,要对inode进行修改
	mutex_lock(&inode->i_mutex);

	//和selinux有关的回调函数
	error = security_inode_setxattr(dentry, name, value, size, flags);
	if (error)
		goto out;

	//当前进程能够修改文件的扩展属性,此函数内部不会做权限检查
	error = __vfs_setxattr_noperm(dentry, name, value, size, flags);

out:

	//将文件的inode解锁
	mutex_unlock(&inode->i_mutex);
	return error;
}
EXPORT_SYMBOL_GPL(vfs_setxattr);

ssize_t
xattr_getsecurity(struct inode *inode, const char *name, void *value,
			size_t size)
{
	void *buffer = NULL;
	ssize_t len;

	if (!value || !size) {
		len = security_inode_getsecurity(inode, name, &buffer, false);
		goto out_noalloc;
	}

	len = security_inode_getsecurity(inode, name, &buffer, true);
	if (len < 0)
		return len;
	if (size < len) {
		len = -ERANGE;
		goto out;
	}
	memcpy(value, buffer, len);
out:
	security_release_secctx(buffer, len);
out_noalloc:
	return len;
}
EXPORT_SYMBOL_GPL(xattr_getsecurity);

ssize_t
vfs_getxattr(struct dentry *dentry, const char *name, void *value, size_t size)
{
	struct inode *inode = dentry->d_inode;
	int error;

	error = xattr_permission(inode, name, MAY_READ);
	if (error)
		return error;

	error = security_inode_getxattr(dentry, name);
	if (error)
		return error;

	if (!strncmp(name, XATTR_SECURITY_PREFIX,
				XATTR_SECURITY_PREFIX_LEN)) {
		const char *suffix = name + XATTR_SECURITY_PREFIX_LEN;
		int ret = xattr_getsecurity(inode, suffix, value, size);
		/*
		 * Only overwrite the return value if a security module
		 * is actually active.
		 */
		if (ret == -EOPNOTSUPP)
			goto nolsm;
		return ret;
	}
nolsm:
	if (inode->i_op->getxattr)
		error = inode->i_op->getxattr(dentry, name, value, size);
	else
		error = -EOPNOTSUPP;

	return error;
}
EXPORT_SYMBOL_GPL(vfs_getxattr);

ssize_t
vfs_listxattr(struct dentry *d, char *list, size_t size)
{
	ssize_t error;

	error = security_inode_listxattr(d);
	if (error)
		return error;
	error = -EOPNOTSUPP;
	if (d->d_inode->i_op->listxattr) {
		error = d->d_inode->i_op->listxattr(d, list, size);
	} else {
		error = security_inode_listsecurity(d->d_inode, list, size);
		if (size && error > size)
			error = -ERANGE;
	}
	return error;
}
EXPORT_SYMBOL_GPL(vfs_listxattr);

int
vfs_removexattr(struct dentry *dentry, const char *name)
{
	struct inode *inode = dentry->d_inode;
	int error;

	if (!inode->i_op->removexattr)
		return -EOPNOTSUPP;

	error = xattr_permission(inode, name, MAY_WRITE);
	if (error)
		return error;

	error = security_inode_removexattr(dentry, name);
	if (error)
		return error;

	mutex_lock(&inode->i_mutex);
	error = inode->i_op->removexattr(dentry, name);
	mutex_unlock(&inode->i_mutex);

	if (!error)
		fsnotify_xattr(dentry);
	return error;
}
EXPORT_SYMBOL_GPL(vfs_removexattr);


/*
 * Extended attribute SET operations
 */
//扩展属性 设置操作 
static long  setxattr(struct dentry *d, 
                        const char __user *name, const void __user *value,
	                    size_t size, int flags)
{
	int error;
	void *kvalue = NULL;
	char kname[XATTR_NAME_MAX + 1];


	//falgs设置的参数不正确,只能设置XATTR_CREATE或者XATTR_REPLACE
	if (flags & ~(XATTR_CREATE|XATTR_REPLACE))
		return -EINVAL;

	//检查传入的name参数是否空
	error = strncpy_from_user(kname, name, sizeof(kname));
	if (error == 0 || error == sizeof(kname))
		error = -ERANGE;
	
	if (error < 0)
		return error;

    //如果value的长不为0
	if (size) 
	{
	     //扩展属性值的最大长度
		if (size > XATTR_SIZE_MAX)
			return -E2BIG;

		//将用户层传递过来的value复制到内核层
		kvalue = memdup_user(value, size);

		//复制错误
		if (IS_ERR(kvalue))
			return PTR_ERR(kvalue);
	}

	error = vfs_setxattr(d, kname, kvalue, size, flags);

	kfree(kvalue);
	return error;
}


SYSCALL_DEFINE5(setxattr, const char __user *, pathname,
		            const char __user *, name, 
		            const void __user *, value,
		            size_t, size, 
		            int, flags)
{
	struct path path;
	int error;

	//根据用户层提供的文件名获得内核path结构
	error = user_path(pathname, &path);
	if (error)
		return error;

	//获得mnt的写权限
	error = mnt_want_write(path.mnt);
	if (!error) 
	{
	    //获得成功 ,设置扩展属性
		error = setxattr(path.dentry, name, value, size, flags);

		//将mnt写的引用计数减一
		mnt_drop_write(path.mnt);
	}
	
	path_put(&path);


	return error;
}

SYSCALL_DEFINE5(lsetxattr, const char __user *, pathname,
		const char __user *, name, const void __user *, value,
		size_t, size, int, flags)
{
	struct path path;
	int error;

	error = user_lpath(pathname, &path);
	if (error)
		return error;
	error = mnt_want_write(path.mnt);
	if (!error) {
		error = setxattr(path.dentry, name, value, size, flags);
		mnt_drop_write(path.mnt);
	}
	path_put(&path);
	return error;
}

SYSCALL_DEFINE5(fsetxattr, int, fd, const char __user *, name,
		const void __user *,value, size_t, size, int, flags)
{
	struct file *f;
	struct dentry *dentry;
	int error = -EBADF;

	f = fget(fd);
	if (!f)
		return error;
	dentry = f->f_path.dentry;
	audit_inode(NULL, dentry);
	error = mnt_want_write_file(f);
	if (!error) {
		error = setxattr(dentry, name, value, size, flags);
		mnt_drop_write(f->f_path.mnt);
	}
	fput(f);
	return error;
}

/*
 * Extended attribute GET operations
 */
static ssize_t
getxattr(struct dentry *d, const char __user *name, void __user *value,
	 size_t size)
{
	ssize_t error;
	void *kvalue = NULL;
	char kname[XATTR_NAME_MAX + 1];

	error = strncpy_from_user(kname, name, sizeof(kname));
	if (error == 0 || error == sizeof(kname))
		error = -ERANGE;
	if (error < 0)
		return error;

	if (size) {
		if (size > XATTR_SIZE_MAX)
			size = XATTR_SIZE_MAX;
		kvalue = kzalloc(size, GFP_KERNEL);
		if (!kvalue)
			return -ENOMEM;
	}

	error = vfs_getxattr(d, kname, kvalue, size);
	if (error > 0) {
		if (size && copy_to_user(value, kvalue, error))
			error = -EFAULT;
	} else if (error == -ERANGE && size >= XATTR_SIZE_MAX) {
		/* The file system tried to returned a value bigger
		   than XATTR_SIZE_MAX bytes. Not possible. */
		error = -E2BIG;
	}
	kfree(kvalue);
	return error;
}

SYSCALL_DEFINE4(getxattr, const char __user *, pathname,
		const char __user *, name, void __user *, value, size_t, size)
{
	struct path path;
	ssize_t error;

	error = user_path(pathname, &path);
	if (error)
		return error;
	error = getxattr(path.dentry, name, value, size);
	path_put(&path);
	return error;
}

SYSCALL_DEFINE4(lgetxattr, const char __user *, pathname,
		const char __user *, name, void __user *, value, size_t, size)
{
	struct path path;
	ssize_t error;

	error = user_lpath(pathname, &path);
	if (error)
		return error;
	error = getxattr(path.dentry, name, value, size);
	path_put(&path);
	return error;
}

SYSCALL_DEFINE4(fgetxattr, int, fd, const char __user *, name,
		void __user *, value, size_t, size)
{
	struct file *f;
	ssize_t error = -EBADF;

	f = fget(fd);
	if (!f)
		return error;
	audit_inode(NULL, f->f_path.dentry);
	error = getxattr(f->f_path.dentry, name, value, size);
	fput(f);
	return error;
}

/*
 * Extended attribute LIST operations
 */
static ssize_t
listxattr(struct dentry *d, char __user *list, size_t size)
{
	ssize_t error;
	char *klist = NULL;

	if (size) {
		if (size > XATTR_LIST_MAX)
			size = XATTR_LIST_MAX;
		klist = kmalloc(size, GFP_KERNEL);
		if (!klist)
			return -ENOMEM;
	}

	error = vfs_listxattr(d, klist, size);
	if (error > 0) {
		if (size && copy_to_user(list, klist, error))
			error = -EFAULT;
	} else if (error == -ERANGE && size >= XATTR_LIST_MAX) {
		/* The file system tried to returned a list bigger
		   than XATTR_LIST_MAX bytes. Not possible. */
		error = -E2BIG;
	}
	kfree(klist);
	return error;
}

SYSCALL_DEFINE3(listxattr, const char __user *, pathname, char __user *, list,
		size_t, size)
{
	struct path path;
	ssize_t error;

	error = user_path(pathname, &path);
	if (error)
		return error;
	error = listxattr(path.dentry, list, size);
	path_put(&path);
	return error;
}

SYSCALL_DEFINE3(llistxattr, const char __user *, pathname, char __user *, list,
		size_t, size)
{
	struct path path;
	ssize_t error;

	error = user_lpath(pathname, &path);
	if (error)
		return error;
	error = listxattr(path.dentry, list, size);
	path_put(&path);
	return error;
}

SYSCALL_DEFINE3(flistxattr, int, fd, char __user *, list, size_t, size)
{
	struct file *f;
	ssize_t error = -EBADF;

	f = fget(fd);
	if (!f)
		return error;
	audit_inode(NULL, f->f_path.dentry);
	error = listxattr(f->f_path.dentry, list, size);
	fput(f);
	return error;
}

/*
 * Extended attribute REMOVE operations
 */
static long
removexattr(struct dentry *d, const char __user *name)
{
	int error;
	char kname[XATTR_NAME_MAX + 1];

	error = strncpy_from_user(kname, name, sizeof(kname));
	if (error == 0 || error == sizeof(kname))
		error = -ERANGE;
	if (error < 0)
		return error;

	return vfs_removexattr(d, kname);
}

SYSCALL_DEFINE2(removexattr, const char __user *, pathname,
		const char __user *, name)
{
	struct path path;
	int error;

	error = user_path(pathname, &path);
	if (error)
		return error;
	error = mnt_want_write(path.mnt);
	if (!error) {
		error = removexattr(path.dentry, name);
		mnt_drop_write(path.mnt);
	}
	path_put(&path);
	return error;
}

SYSCALL_DEFINE2(lremovexattr, const char __user *, pathname,
		const char __user *, name)
{
	struct path path;
	int error;

	error = user_lpath(pathname, &path);
	if (error)
		return error;
	error = mnt_want_write(path.mnt);
	if (!error) {
		error = removexattr(path.dentry, name);
		mnt_drop_write(path.mnt);
	}
	path_put(&path);
	return error;
}

SYSCALL_DEFINE2(fremovexattr, int, fd, const char __user *, name)
{
	struct file *f;
	struct dentry *dentry;
	int error = -EBADF;

	f = fget(fd);
	if (!f)
		return error;
	dentry = f->f_path.dentry;
	audit_inode(NULL, dentry);
	error = mnt_want_write_file(f);
	if (!error) {
		error = removexattr(dentry, name);
		mnt_drop_write(f->f_path.mnt);
	}
	fput(f);
	return error;
}


static const char *
strcmp_prefix(const char *a, const char *a_prefix)
{
	while (*a_prefix && *a == *a_prefix) {
		a++;
		a_prefix++;
	}
	return *a_prefix ? NULL : a;
}

/*
 * In order to implement different sets of xattr operations for each xattr
 * prefix with the generic xattr API, a filesystem should create a
 * null-terminated array of struct xattr_handler (one for each prefix) and
 * hang a pointer to it off of the s_xattr field of the superblock.
 *
 * The generic_fooxattr() functions will use this list to dispatch xattr
 * operations to the correct xattr_handler.
 */
#define for_each_xattr_handler(handlers, handler)		\
		for ((handler) = *(handlers)++;			\
			(handler) != NULL;			\
			(handler) = *(handlers)++)

/*
 * Find the xattr_handler with the matching prefix.
 */
static struct xattr_handler *
xattr_resolve_name(struct xattr_handler **handlers, const char **name)
{
	struct xattr_handler *handler;

	if (!*name)
		return NULL;

	// 表中 ext4_xattr_handlers 根据命名空间前缀来得到 扩展属性操作结构体
	for_each_xattr_handler(handlers, handler) 
   {
		const char *n = strcmp_prefix(*name, handler->prefix);
		if (n) {
			*name = n;
			break;
		}
	}
	return handler;
}

/*
 * Find the handler for the prefix and dispatch its get() operation.
 */
ssize_t
generic_getxattr(struct dentry *dentry, const char *name, void *buffer, size_t size)
{
	struct xattr_handler *handler;
	struct inode *inode = dentry->d_inode;

	handler = xattr_resolve_name(inode->i_sb->s_xattr, &name);
	if (!handler)
		return -EOPNOTSUPP;
	return handler->get(inode, name, buffer, size);
}

/*
 * Combine the results of the list() operation from every xattr_handler in the
 * list.
 */
ssize_t
generic_listxattr(struct dentry *dentry, char *buffer, size_t buffer_size)
{
	struct inode *inode = dentry->d_inode;
	struct xattr_handler *handler, **handlers = inode->i_sb->s_xattr;
	unsigned int size = 0;

	if (!buffer) {
		for_each_xattr_handler(handlers, handler)
			size += handler->list(inode, NULL, 0, NULL, 0);
	} else {
		char *buf = buffer;

		for_each_xattr_handler(handlers, handler) {
			size = handler->list(inode, buf, buffer_size, NULL, 0);
			if (size > buffer_size)
				return -ERANGE;
			buf += size;
			buffer_size -= size;
		}
		size = buf - buffer;
	}
	return size;
}

/*
 * Find the handler for the prefix and dispatch its set() operation.
 */
int generic_setxattr(struct dentry *dentry, const char *name, const void *value, size_t size, int flags)
{
	struct xattr_handler *handler;
	struct inode *inode = dentry->d_inode;

	if (size == 0)
		value = "";  /* empty EA, do not remove */

	/* 查找 ext4_xattr_handlers 指定命名空间中扩展属性的处理函数 */
	handler = xattr_resolve_name(inode->i_sb->s_xattr, &name);
	if (!handler)
		return -EOPNOTSUPP;

	//在表   中的某一个
	return handler->set(inode, name, value, size, flags);
}

/*
 * Find the handler for the prefix and dispatch its set() operation to remove
 * any associated extended attribute.
 */
int
generic_removexattr(struct dentry *dentry, const char *name)
{
	struct xattr_handler *handler;
	struct inode *inode = dentry->d_inode;

	handler = xattr_resolve_name(inode->i_sb->s_xattr, &name);
	if (!handler)
		return -EOPNOTSUPP;
	return handler->set(inode, name, NULL, 0, XATTR_REPLACE);
}

EXPORT_SYMBOL(generic_getxattr);
EXPORT_SYMBOL(generic_listxattr);
EXPORT_SYMBOL(generic_setxattr);
EXPORT_SYMBOL(generic_removexattr);
