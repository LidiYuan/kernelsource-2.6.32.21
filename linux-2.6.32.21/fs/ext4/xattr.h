/*
  File: fs/ext4/xattr.h

  On-disk format of extended attributes for the ext4 filesystem.

  (C) 2001 Andreas Gruenbacher, <a.gruenbacher@computer.org>
*/

#include <linux/xattr.h>

/* Magic value in attribute blocks */
#define EXT4_XATTR_MAGIC		0xEA020000

/* Maximum number of references to one attribute block */
#define EXT4_XATTR_REFCOUNT_MAX		1024

/* Name indexes */
#define EXT4_XATTR_INDEX_USER			1
#define EXT4_XATTR_INDEX_POSIX_ACL_ACCESS	2
#define EXT4_XATTR_INDEX_POSIX_ACL_DEFAULT	3
#define EXT4_XATTR_INDEX_TRUSTED		4
#define	EXT4_XATTR_INDEX_LUSTRE			5
#define EXT4_XATTR_INDEX_SECURITY	        6


/*
可以在两个地方找到扩展属性：
      一是在一个inode项结尾到下一个inode项开头的地方；
      二是inode.i_file_acl指向 的数据块之中
      
当扩展属性不存储在一个inode之后的时候，就会有一个头部struct ext4_xattr_header
当扩展属性存储在inode之后的时候  有一个头部ext4_xattr_ibody_header

这两个头部后面都会跟着多个ext4_xattr_entry扩展属性项
*/


//扩展属性的头部此扩展属性放在磁盘的block中
struct ext4_xattr_header {
	__le32	h_magic;	/* magic number for identification 用于标识的魔数 0xEA020000*/
	__le32	h_refcount;	/* reference count 引用计数 */
	__le32	h_blocks;	/* number of disk blocks used 使用的磁盘块数*/
	__le32	h_hash;		/* hash value of all attributes 所有属性的hash值*/
	__u32	h_reserved[4];	/* zero right now  暂时保留为0 */
};


//扩展属性头部  此扩展属性是放在inode之后的
struct ext4_xattr_ibody_header {
	__le32	h_magic;	/* magic number for identification 用于标识的魔数 0xEA020000 */
};

//紧跟在ext4_xattr_ibody_header或者ext4_xattr _header后面的是结构数组 struct ext4_xattr_entry
/*
扩展属性值可以紧跟在ext4_xattr_entry项表后面,

当扩展属性存储在额外的磁盘块的时候,ext4_xattr_entry必须进行排序存放,排序是按e_name_index,然后是e_name_len,在是e_name
存储在inode后的ext4_xattr_entry是不需要排序的
*/
struct ext4_xattr_entry {
	__u8	e_name_len;  	/* length of name 名字的长度*/
	__u8	e_name_index;	/* attribute name index  属性名索引如EXT4_XATTR_INDEX_USER
                                  为了减少属性名字符串对空间的浪费,将属性名的前缀换算为索引值进行存储,
                                  而名字只是除前缀意外的部分,前缀和索引的对应关系如下
									0	(no prefix)
									1	"user."
									2	"system.posix_acl_access"
									3	"system.posix_acl_default"
									4	"trusted."
									6	"security."
									7	"system." (inline_data only?)
									8	"system.richacl" (SuSE kernels only?)
	                        */
	
	__le16	e_value_offs;	/* offset in disk block of value 值在磁盘块中的索引
                               对于inode中的扩展属性此处是相对于的一项的偏移
                               对于在磁盘块中的属性此处是相对于开始块的位置(如header)
	                            */

	__le32	e_value_block;	/* disk block attribute is stored on (n/i) 属性存储的磁盘块号*/
	__le32	e_value_size;	/* size of attribute value 扩展属性值的长度*/
	__le32	e_hash;		    /* hash value of name and value 名字和值的hansh值*/
	char	e_name[0];	     /* attribute name 属性名*/
};

#define EXT4_XATTR_PAD_BITS		2
#define EXT4_XATTR_PAD		(1<<EXT4_XATTR_PAD_BITS)
#define EXT4_XATTR_ROUND		(EXT4_XATTR_PAD-1)

//计算指定的扩展属性项的长度
#define EXT4_XATTR_LEN(name_len) \
	(((name_len) + EXT4_XATTR_ROUND + \
	sizeof(struct ext4_xattr_entry)) & ~EXT4_XATTR_ROUND)

//计算相对当前指定的扩展属性项的下一个扩展属性项的地址
#define EXT4_XATTR_NEXT(entry) \
	((struct ext4_xattr_entry *)( \
	 (char *)(entry) + EXT4_XATTR_LEN((entry)->e_name_len)))


#define EXT4_XATTR_SIZE(size) \
	(((size) + EXT4_XATTR_ROUND) & ~EXT4_XATTR_ROUND)

//获取存放在索引节点空闲空间内的扩展属性列表的首部
#define IHDR(inode, raw_inode) \
	((struct ext4_xattr_ibody_header *) \
		((void *)raw_inode + \
		EXT4_GOOD_OLD_INODE_SIZE + \
		EXT4_I(inode)->i_extra_isize))

//获取索引节点空闲空间内存放的第一个扩展属性项
#define IFIRST(hdr) ((struct ext4_xattr_entry *)((hdr)+1))

# ifdef CONFIG_EXT4_FS_XATTR

extern struct xattr_handler ext4_xattr_user_handler;
extern struct xattr_handler ext4_xattr_trusted_handler;
extern struct xattr_handler ext4_xattr_acl_access_handler;
extern struct xattr_handler ext4_xattr_acl_default_handler;
extern struct xattr_handler ext4_xattr_security_handler;

extern ssize_t ext4_listxattr(struct dentry *, char *, size_t);

extern int ext4_xattr_get(struct inode *, int, const char *, void *, size_t);
extern int ext4_xattr_set(struct inode *, int, const char *, const void *, size_t, int);
extern int ext4_xattr_set_handle(handle_t *, struct inode *, int, const char *, const void *, size_t, int);

extern void ext4_xattr_delete_inode(handle_t *, struct inode *);
extern void ext4_xattr_put_super(struct super_block *);

extern int ext4_expand_extra_isize_ea(struct inode *inode, int new_extra_isize,
			    struct ext4_inode *raw_inode, handle_t *handle);

extern int init_ext4_xattr(void);
extern void exit_ext4_xattr(void);

extern struct xattr_handler *ext4_xattr_handlers[];

# else  /* CONFIG_EXT4_FS_XATTR */

static inline int
ext4_xattr_get(struct inode *inode, int name_index, const char *name,
	       void *buffer, size_t size, int flags)
{
	return -EOPNOTSUPP;
}

static inline int
ext4_xattr_set(struct inode *inode, int name_index, const char *name,
	       const void *value, size_t size, int flags)
{
	return -EOPNOTSUPP;
}

static inline int
ext4_xattr_set_handle(handle_t *handle, struct inode *inode, int name_index,
	       const char *name, const void *value, size_t size, int flags)
{
	return -EOPNOTSUPP;
}

static inline void
ext4_xattr_delete_inode(handle_t *handle, struct inode *inode)
{
}

static inline void
ext4_xattr_put_super(struct super_block *sb)
{
}

static inline int
init_ext4_xattr(void)
{
	return 0;
}

static inline void
exit_ext4_xattr(void)
{
}

static inline int
ext4_expand_extra_isize_ea(struct inode *inode, int new_extra_isize,
			    struct ext4_inode *raw_inode, handle_t *handle)
{
	return -EOPNOTSUPP;
}

#define ext4_xattr_handlers	NULL

# endif  /* CONFIG_EXT4_FS_XATTR */

#ifdef CONFIG_EXT4_FS_SECURITY
extern int ext4_init_security(handle_t *handle, struct inode *inode,
				struct inode *dir);
#else
static inline int ext4_init_security(handle_t *handle, struct inode *inode,
				struct inode *dir)
{
	return 0;
}
#endif
