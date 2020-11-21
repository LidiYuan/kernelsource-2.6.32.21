/*
 * linux/fs/seq_file.c
 *
 * helper functions for making synthetic files from sequences of records.
 * initial implementation -- AV, Oct 2001.
 */

#include <linux/fs.h>
#include <linux/module.h>
#include <linux/seq_file.h>
#include <linux/slab.h>

#include <asm/uaccess.h>
#include <asm/page.h>

/**
 *	seq_open -	initialize sequential file
 *	@file: file we initialize
 *	@op: method table describing the sequence
 *
 *	seq_open() sets @file, associating it with a sequence described
 *	by @op.  @op->start() sets the iterator up and returns the first
 *	element of sequence. @op->stop() shuts it down.  @op->next()
 *	returns the next element of sequence.  @op->show() prints element
 *	into the buffer.  In case of error ->start() and ->next() return
 *	ERR_PTR(error).  In the end of sequence they return %NULL. ->show()
 *	returns 0 in case of success and negative number in case of error.
 *	Returning SEQ_SKIP means "discard this element and move on".
 */
 //打开seq流，为struct file分配struct seq_file结构，并定义seq_file的操作
 /*

*/
int seq_open(struct file *file, const struct seq_operations *op)
{
    
	struct seq_file *p = file->private_data;

	
	if (!p) 
	{
	    //分配一个用于此文件的缓存流
		p = kmalloc(sizeof(*p), GFP_KERNEL);
		if (!p)
			return -ENOMEM;
		file->private_data = p;
	}
	
	memset(p, 0, sizeof(*p));
	mutex_init(&p->lock);

	//保存此缓存流的操作集合
	p->op = op;

	/*
	 * Wrappers around seq_open(e.g. swaps_open) need to be
	 * aware of this. If they set f_version themselves, they
	 * should call seq_open first and then set f_version.
	 */
	file->f_version = 0;

	/*
	 * seq_files support lseek() and pread().  They do not implement
	 * write() at all, but we clear FMODE_PWRITE here for historical
	 * reasons.
	 *
	 * If a client of seq_files a) implements file.write() and b) wishes to
	 * support pwrite() then that client will need to implement its own
	 * file.open() which calls seq_open() and then sets FMODE_PWRITE.
	 */
	file->f_mode &= ~FMODE_PWRITE;
	return 0;
}

EXPORT_SYMBOL(seq_open);

static int traverse(struct seq_file *m, loff_t offset)
{
	loff_t pos = 0, index;
	int error = 0;
	void *p;

	m->version = 0;
	index = 0;
	
	m->count = m->from = 0;
	if (!offset) //用户从头开始读取
	{
		m->index = index;//将数据的索引值设置为0
		return 0;
	}

	//数据缓冲区还未分配
	if (!m->buf) 
	{
	    //分配内核的缓冲区 用于缓存要读取的数据
		m->buf = kmalloc(m->size = PAGE_SIZE, GFP_KERNEL);
		if (!m->buf)
			return -ENOMEM;
	}

	//此处调用的在seq_open()时候传入的第二个参数中的操作结合start
	p = m->op->start(m, &index);
	
	while (p) 
	{
		error = PTR_ERR(p);
		if (IS_ERR(p))
			break;

		
		error = m->op->show(m, p);
		if (error < 0)
			break;
		
		if (unlikely(error)) 
		{
			error = 0;
			m->count = 0;
		}

		if (m->count == m->size)
			goto Eoverflow;

		if (pos + m->count > offset) 
		{
			m->from = offset - pos;
			m->count -= m->from;
			m->index = index;
			break;
		}
		
		pos += m->count;
		m->count = 0;

		if (pos == offset) 
		{
			index++;
			m->index = index;
			break;
		}
		p = m->op->next(m, p, &index);
	}
	m->op->stop(m, p);
	m->index = index;
	return error;

Eoverflow:
	m->op->stop(m, p);
	kfree(m->buf);
	m->buf = kmalloc(m->size <<= 1, GFP_KERNEL);
	return !m->buf ? -ENOMEM : -EAGAIN;
}

/**
 *	seq_read -->read() method for sequential files.
 *	@file: the file to read from
 *	@buf:  the buffer to read to
 *	@size: the maximum number of bytes to read
 *	@ppos: the current position in the file
 *
 *	Ready-made ->f_op->read()
 *///从seq流中读数据到用户空间，其中循环调用了struct seq_file中的各个函数来读数据
ssize_t seq_read(struct file *file, //文件描述符
                   char __user *buf,  //用户缓存，用于输出信息   
                   size_t size,       //读取的大小
                   loff_t *ppos)      //文件指针此时的位置 
{
    //取出和此文件相关联缓存流
	struct seq_file *m = (struct seq_file *)file->private_data;
	size_t copied = 0;
	loff_t pos;
	size_t n;
	void *p;
	int err = 0;

	mutex_lock(&m->lock);

	/* Don't assume *ppos is where we left it */
	//不要认为 *ppos的值是就在我们离开的地方,用户可能自己设置了此值
	//如果用户已经读取内容和seq_file中不一致，要将seq_file部分内容丢弃
	if (unlikely(*ppos != m->read_pos)) 
	{
		m->read_pos = *ppos;  //设置成当前要读取的位置
		while ((err = traverse(m, *ppos)) == -EAGAIN)
		if (err) 
		{
			/* With prejudice... */
			m->read_pos = 0;
			m->version = 0;
			m->index = 0;
			m->count = 0;
			goto Done;
		}
	}

	/*
	 * seq_file->op->..m_start/m_stop/m_next may do special actions
	 * or optimisations based on the file->f_version, so we want to
	 * pass the file->f_version to those methods.
	 *
	 * seq_file->version is just copy of f_version, and seq_file
	 * methods can treat it simply as file version.
	 * It is copied in first and copied out after all operations.
	 * It is convenient to have it as  part of structure to avoid the
	 * need of passing another argument to all the seq_file methods.
	 */
	m->version = file->f_version;
	
	//如果struct seq_file结构中的缓冲区没有分配的话
	/// 分配缓冲，大小为PAGE_SIZE
	/* grab buffer if we didn't have one */
	if (!m->buf) 
	{
		m->buf = kmalloc(m->size = PAGE_SIZE, GFP_KERNEL);
		if (!m->buf)
			goto Enomem;
	}
	/* if not empty - flush it first */
	//count表示当时有多少数据还没有传给用户空间
	if (m->count) 
	{
		n = min(m->count, size);
		err = copy_to_user(buf, m->buf + m->from, n);
		if (err)
			goto Efault;
		m->count -= n;
		m->from += n;
		size -= n;
		buf += n;
		copied += n;
		if (!m->count)
			m->index++;
		
		if (!size)
			goto Done;
	}
	
	/* we need at least one record in buffer */
	pos = m->index;

	p = m->op->start(m, &pos);
	//进行主要传数据过程，缓冲区中至少要有一个记录单位的数据
	while (1) 
	{
		err = PTR_ERR(p);
		if (!p || IS_ERR(p))//是否读到了结尾
			break;

		//将一条记录加如到缓存中
		//在show中返回小于0的值 将终止继续显示数据
		err = m->op->show(m, p);
		if (err < 0)
			break;

		//返回值大于0,   将缓冲区中的数据进行清空
		if (unlikely(err))
			m->count = 0;

		//缓冲区中的数据为0,调用next将指针指向下一条记录
		if (unlikely(!m->count)) 
		{
			p = m->op->next(m, p, &pos);
			m->index = pos; //更新当前的记录所有位置
			continue;
		}
		
		// 当前缓冲区中的实际数据小于缓冲区大小，转到填数据部分
		//count是在show函数中通过 seq_printf seq_putc等函数进行增长的
		if (m->count < m->size)
			goto Fill;

		//走到此处说明一条记录的长度超出了缓冲区可以表示的最大值，stop传入用户自己的私有数据v 让用户来做一些改变,基本此函数不用处理
		m->op->stop(m, p);

		//释放之前的缓冲区,然后分配一个更大的缓冲区
		kfree(m->buf);
		m->buf = kmalloc(m->size <<= 1, GFP_KERNEL);
		if (!m->buf)
			goto Enomem;
		
		m->count = 0;
		m->version = 0;
		pos = m->index;

		//在当前位置进行重新开始读取
		p = m->op->start(m, &pos);
	}
	m->op->stop(m, p);
	m->count = 0;
	goto Done;
	
Fill:
	/* they want more? let's try to get some more */
    //是不是当前读取到的数据小于用户自己的缓冲区
	while (m->count < size) 
	{  
	    //读取更多的记录
		size_t offs = m->count;
		loff_t next = pos;

		//将数据指针指向下一条记录
		p = m->op->next(m, p, &next);

		//如果发生错误比如没有更多数据了
		if (!p || IS_ERR(p)) 
		{
			err = PTR_ERR(p);
			break;
		}
		
		//将数据继续加如到缓冲区
		err = m->op->show(m, p);

		//读取的数据有可能不完整 ,恢复上一次的偏移值
		if (m->count == m->size || err) 
		{
			m->count = offs;
			if (likely(err <= 0))
				break;
		}
		
		pos = next;
	}
	
	m->op->stop(m, p);
	
	n = min(m->count, size);
	
    //将数据拷贝到用户空间
	err = copy_to_user(buf, m->buf, n);
	if (err)
		goto Efault;
	
	copied += n;
	m->count -= n;
	if (m->count)
		m->from = n;
	else
		pos++;
	m->index = pos;
Done:
	if (!copied)
		copied = err;
	else {
		*ppos += copied;
		m->read_pos += copied;
	}
	file->f_version = m->version;
	mutex_unlock(&m->lock);
	return copied;
Enomem:
	err = -ENOMEM;
	goto Done;
Efault:
	err = -EFAULT;
	goto Done;
}
EXPORT_SYMBOL(seq_read);

/**
 *	seq_lseek -	->llseek() method for sequential files.
 *	@file: the file in question
 *	@offset: new position
 *	@origin: 0 for absolute, 1 for relative position
 *
 *	Ready-made ->f_op->llseek()
 */
 //定位seq流当前指针偏移
loff_t seq_lseek(struct file *file, loff_t offset, int origin)
{
	struct seq_file *m = (struct seq_file *)file->private_data;
	loff_t retval = -EINVAL;

	mutex_lock(&m->lock);
	m->version = file->f_version;
	switch (origin) {
		case 1:
			offset += file->f_pos;
		case 0:
			if (offset < 0)
				break;
			retval = offset;
			if (offset != m->read_pos) {
				while ((retval=traverse(m, offset)) == -EAGAIN)
					;
				if (retval) {
					/* with extreme prejudice... */
					file->f_pos = 0;
					m->read_pos = 0;
					m->version = 0;
					m->index = 0;
					m->count = 0;
				} else {
					m->read_pos = offset;
					retval = file->f_pos = offset;
				}
			}
	}
	file->f_version = m->version;
	mutex_unlock(&m->lock);
	return retval;
}
EXPORT_SYMBOL(seq_lseek);

/**
 *	seq_release -	free the structures associated with sequential file.
 *	@file: file in question
 *	@inode: file->f_path.dentry->d_inode
 *
 *	Frees the structures associated with sequential file; can be used
 *	as ->f_op->release() if you don't have private data to destroy.
 */
 //释放seq流所分配的动态内存空间，即struct seq_file的buf及其本身
int seq_release(struct inode *inode, struct file *file)
{
	struct seq_file *m = (struct seq_file *)file->private_data;
	kfree(m->buf);
	kfree(m);
	return 0;
}
EXPORT_SYMBOL(seq_release);

/**
 *	seq_escape -	print string into buffer, escaping some characters
 *	@m:	target buffer
 *	@s:	string
 *	@esc:	set of characters that need escaping
 *
 *	Puts string into buffer, replacing each occurrence of character from
 *	@esc with usual octal escape.  Returns 0 in case of success, -1 - in
 *	case of overflow.
 */
 //将seq流中需要进行转义的字符转换为8进制数字
int seq_escape(struct seq_file *m, const char *s, const char *esc)
{
	char *end = m->buf + m->size;
        char *p;
	char c;

        for (p = m->buf + m->count; (c = *s) != '\0' && p < end; s++) {
		if (!strchr(esc, c)) {
			*p++ = c;
			continue;
		}
		if (p + 3 < end) {
			*p++ = '\\';
			*p++ = '0' + ((c & 0300) >> 6);
			*p++ = '0' + ((c & 070) >> 3);
			*p++ = '0' + (c & 07);
			continue;
		}
		m->count = m->size;
		return -1;
        }
	m->count = p - m->buf;
        return 0;
}
EXPORT_SYMBOL(seq_escape);

//向seq流方式写格式化信息
int seq_printf(struct seq_file *m, const char *f, ...)
{
	va_list args;
	int len;

	if (m->count < m->size) 
	{
		va_start(args, f);
		len = vsnprintf(m->buf + m->count, m->size - m->count, f, args);
		va_end(args);
		//如果填充后的数据长度小于缓冲区的总大小
		if (m->count + len < m->size) 
		{
			m->count += len;
			return 0;
		}
	}

	//走到处处说明填充的数据已经超出了缓冲区可以容纳的数量
	m->count = m->size;
	return -1;
}
EXPORT_SYMBOL(seq_printf);

/**
 *	mangle_path -	mangle and copy path to buffer beginning
 *	@s: buffer start
 *	@p: beginning of path in above buffer
 *	@esc: set of characters that need escaping
 *
 *      Copy the path from @p to @s, replacing each occurrence of character from
 *      @esc with usual octal escape.
 *      Returns pointer past last written character in @s, or NULL in case of
 *      failure.
 */
char *mangle_path(char *s, char *p, char *esc)
{
	while (s <= p) {
		char c = *p++;
		if (!c) {
			return s;
		} else if (!strchr(esc, c)) {
			*s++ = c;
		} else if (s + 4 > p) {
			break;
		} else {
			*s++ = '\\';
			*s++ = '0' + ((c & 0300) >> 6);
			*s++ = '0' + ((c & 070) >> 3);
			*s++ = '0' + (c & 07);
		}
	}
	return NULL;
}
EXPORT_SYMBOL(mangle_path);

/**
 * seq_path - seq_file interface to print a pathname
 * @m: the seq_file handle
 * @path: the struct path to print
 * @esc: set of characters to escape in the output
 *
 * return the absolute path of 'path', as represented by the
 * dentry / mnt pair in the path parameter.
 */
 //在seq流中添加路径信息，路径字符都转换为8进制数。
int seq_path(struct seq_file *m, struct path *path, char *esc)
{
	char *buf;
	size_t size = seq_get_buf(m, &buf);
	int res = -1;

	if (size) {
		char *p = d_path(path, buf, size);
		if (!IS_ERR(p)) {
			char *end = mangle_path(buf, p, esc);
			if (end)
				res = end - buf;
		}
	}
	seq_commit(m, res);

	return res;
}
EXPORT_SYMBOL(seq_path);

/*
 * Same as seq_path, but relative to supplied root.
 *
 * root may be changed, see __d_path().
 */
int seq_path_root(struct seq_file *m, struct path *path, struct path *root,
		  char *esc)
{
	char *buf;
	size_t size = seq_get_buf(m, &buf);
	int res = -ENAMETOOLONG;

	if (size) {
		char *p;

		spin_lock(&dcache_lock);
		p = __d_path(path, root, buf, size);
		spin_unlock(&dcache_lock);
		res = PTR_ERR(p);
		if (!IS_ERR(p)) {
			char *end = mangle_path(buf, p, esc);
			if (end)
				res = end - buf;
			else
				res = -ENAMETOOLONG;
		}
	}
	seq_commit(m, res);

	return res < 0 ? res : 0;
}

/*
 * returns the path of the 'dentry' from the root of its filesystem.
 */
int seq_dentry(struct seq_file *m, struct dentry *dentry, char *esc)
{
	char *buf;
	size_t size = seq_get_buf(m, &buf);
	int res = -1;

	if (size) {
		char *p = dentry_path(dentry, buf, size);
		if (!IS_ERR(p)) {
			char *end = mangle_path(buf, p, esc);
			if (end)
				res = end - buf;
		}
	}
	seq_commit(m, res);

	return res;
}

int seq_bitmap(struct seq_file *m, const unsigned long *bits,
				   unsigned int nr_bits)
{
	if (m->count < m->size) {
		int len = bitmap_scnprintf(m->buf + m->count,
				m->size - m->count, bits, nr_bits);
		if (m->count + len < m->size) {
			m->count += len;
			return 0;
		}
	}
	m->count = m->size;
	return -1;
}
EXPORT_SYMBOL(seq_bitmap);

int seq_bitmap_list(struct seq_file *m, const unsigned long *bits,
		unsigned int nr_bits)
{
	if (m->count < m->size) {
		int len = bitmap_scnlistprintf(m->buf + m->count,
				m->size - m->count, bits, nr_bits);
		if (m->count + len < m->size) {
			m->count += len;
			return 0;
		}
	}
	m->count = m->size;
	return -1;
}
EXPORT_SYMBOL(seq_bitmap_list);

static void *single_start(struct seq_file *p, loff_t *pos)
{
	return NULL + (*pos == 0);
}

static void *single_next(struct seq_file *p, void *v, loff_t *pos)
{
	++*pos;
	return NULL;
}

static void single_stop(struct seq_file *p, void *v)
{
}

int single_open(struct file *file, int (*show)(struct seq_file *, void *),
		void *data)
{
	struct seq_operations *op = kmalloc(sizeof(*op), GFP_KERNEL);
	int res = -ENOMEM;

	if (op) {
		op->start = single_start;
		op->next = single_next;
		op->stop = single_stop;
		op->show = show;
		res = seq_open(file, op);
		if (!res)
			((struct seq_file *)file->private_data)->private = data;
		else
			kfree(op);
	}
	return res;
}
EXPORT_SYMBOL(single_open);

int single_release(struct inode *inode, struct file *file)
{
	const struct seq_operations *op = ((struct seq_file *)file->private_data)->op;
	int res = seq_release(inode, file);
	kfree(op);
	return res;
}
EXPORT_SYMBOL(single_release);

int seq_release_private(struct inode *inode, struct file *file)
{
	struct seq_file *seq = file->private_data;

	kfree(seq->private);
	seq->private = NULL;
	return seq_release(inode, file);
}
EXPORT_SYMBOL(seq_release_private);

void *__seq_open_private(struct file *f, const struct seq_operations *ops,
		int psize)
{
	int rc;
	void *private;
	struct seq_file *seq;

	private = kzalloc(psize, GFP_KERNEL);
	if (private == NULL)
		goto out;

	//将seq_file{}结构与file{}->private_data进行绑定
	rc = seq_open(f, ops);
	if (rc < 0)
		goto out_free;

	seq = f->private_data;
	seq->private = private;
	return private;

out_free:
	kfree(private);
out:
	return NULL;
}
EXPORT_SYMBOL(__seq_open_private);

int seq_open_private(struct file *filp, const struct seq_operations *ops,
		int psize)
{
	return __seq_open_private(filp, ops, psize) ? 0 : -ENOMEM;
}
EXPORT_SYMBOL(seq_open_private);

//往缓冲区填入一个字符的数据
int seq_putc(struct seq_file *m, char c)
{
    //当前缓冲区数据个数 小于缓冲区的大小
	if (m->count < m->size) 
	{
		m->buf[m->count++] = c;
		return 0;
	}
	return -1;
}
EXPORT_SYMBOL(seq_putc);

int seq_puts(struct seq_file *m, const char *s)
{
	int len = strlen(s);
	if (m->count + len < m->size) {
		memcpy(m->buf + m->count, s, len);
		m->count += len;
		return 0;
	}
	m->count = m->size;
	return -1;
}
EXPORT_SYMBOL(seq_puts);

/**
 * seq_write - write arbitrary data to buffer
 * @seq: seq_file identifying the buffer to which data should be written
 * @data: data address
 * @len: number of bytes
 *
 * Return 0 on success, non-zero otherwise.
 */
int seq_write(struct seq_file *seq, const void *data, size_t len)
{
	if (seq->count + len < seq->size) {
		memcpy(seq->buf + seq->count, data, len);
		seq->count += len;
		return 0;
	}
	seq->count = seq->size;
	return -1;
}
EXPORT_SYMBOL(seq_write);

struct list_head *seq_list_start(struct list_head *head, loff_t pos)
{
	struct list_head *lh;

	list_for_each(lh, head)
		if (pos-- == 0)
			return lh;

	return NULL;
}

EXPORT_SYMBOL(seq_list_start);

struct list_head *seq_list_start_head(struct list_head *head, loff_t pos)
{
	if (!pos)
		return head;

	return seq_list_start(head, pos - 1);
}

EXPORT_SYMBOL(seq_list_start_head);

struct list_head *seq_list_next(void *v, struct list_head *head, loff_t *ppos)
{
	struct list_head *lh;

	lh = ((struct list_head *)v)->next;
	++*ppos;
	return lh == head ? NULL : lh;
}

EXPORT_SYMBOL(seq_list_next);
