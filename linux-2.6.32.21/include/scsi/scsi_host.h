#ifndef _SCSI_SCSI_HOST_H
#define _SCSI_SCSI_HOST_H

#include <linux/device.h>
#include <linux/list.h>
#include <linux/types.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>
#include <scsi/scsi.h>

struct request_queue;
struct block_device;
struct completion;
struct module;
struct scsi_cmnd;
struct scsi_device;
struct scsi_target;
struct Scsi_Host;
struct scsi_host_cmd_pool;
struct scsi_transport_template;
struct blk_queue_tags;


/*
 * The various choices mean:
 * NONE: Self evident.	Host adapter is not capable of scatter-gather.
 * ALL:	 Means that the host adapter module can do scatter-gather,
 *	 and that there is no limit to the size of the table to which
 *	 we scatter/gather data.  The value we set here is the maximum
 *	 single element sglist.  To use chained sglists, the adapter
 *	 has to set a value beyond ALL (and correctly use the chain
 *	 handling API.
 * Anything else:  Indicates the maximum number of chains that can be
 *	 used in one scatter-gather request.
 */
#define SG_NONE 0
#define SG_ALL	SCSI_MAX_SG_SEGMENTS

#define MODE_UNKNOWN 0x00
#define MODE_INITIATOR 0x01
#define MODE_TARGET 0x02

#define DISABLE_CLUSTERING 0
#define ENABLE_CLUSTERING 1

//<3:0:0:0> <host:channel:target:lun>
//scsi 主机适配模板  它给出相同型号主机适配器的公用内容
struct scsi_host_template {
	struct module *module; //指向实现这个host模板的低层驱动模块指针
	const char *name;//SCSI HBA驱动的名字

	/*
	 * Used to initialize old-style drivers.  For new-style drivers
	 * just perform all work in your module initialization function.
	 *
	 * Status:  OBSOLETE
	 */
	 //被老式驱动用来检测主机适配器
	int (* detect)(struct scsi_host_template *);

	/*
	 * Used as unload callback for hosts with old-style drivers.
	 *
	 * Status: OBSOLETE
	 */
	 //被老式驱动用来释放主机适配器
	int (* release)(struct Scsi_Host *);

	/*
	 * The info function will return whatever useful information the
	 * developer sees fit.  If not provided, then the name field will
	 * be used instead.
	 *
	 * Status: OPTIONAL
	 */
	 //返回开发人员觉得有用的任何有用信息
	const char *(* info)(struct Scsi_Host *);

	/*
	 * Ioctl interface
	 * Status: OPTIONAL
	 */
	//ioctl接口 
	int (* ioctl)(struct scsi_device *dev, int cmd, void __user *arg);


#ifdef CONFIG_COMPAT
	/* 
	 * Compat handler. Handle 32bit ABI.
	 * When unknown ioctl is passed return -ENOIOCTLCMD.
	 *
	 * Status: OPTIONAL
	 */
	//io接口  在64位内核下执行32位系统调用时候使用 
	int (* compat_ioctl)(struct scsi_device *dev, int cmd, void __user *arg);
#endif

	/*
	 * The queuecommand function is used to queue up a scsi
	 * command block to the LLDD.  When the driver finished
	 * processing the command the done callback is invoked.
	 *
	 * If queuecommand returns 0, then the HBA has accepted the
	 * command.  The done() function must be called on the command
	 * when the driver has finished with it. (you may call done on the
	 * command before queuecommand returns, but in this case you
	 * *must* return 0 from queuecommand).
	 *
	 * Queuecommand may also reject the command, in which case it may
	 * not touch the command and must not call done() for it.
	 *
	 * There are two possible rejection returns:
	 *
	 *   SCSI_MLQUEUE_DEVICE_BUSY: Block this device temporarily, but
	 *   allow commands to other devices serviced by this host.
	 *
	 *   SCSI_MLQUEUE_HOST_BUSY: Block all devices served by this
	 *   host temporarily.
	 *
         * For compatibility, any other non-zero return is treated the
         * same as SCSI_MLQUEUE_HOST_BUSY.
	 *
	 * NOTE: "temporarily" means either until the next command for#
	 * this device/host completes, or a period of time determined by
	 * I/O pressure in the system if there are no other outstanding
	 * commands.
	 *
	 * STATUS: REQUIRED
	 */
	 //将scsi命令排入到LLDD队列 SCSI中间层调用该回调函数 向HBA发送SCSI命令
	 //通过这个函数将scsi命名发送到HBA卡
	int (* queuecommand)(struct scsi_cmnd *,  //指向scsi命令描述符的指针    
			     void (*done)(struct scsi_cmnd *));//指向完成回调函数的指针 在驱动完成处理命令后 此函数被调用

	/*
	 * The transfer functions are used to queue a scsi command to
	 * the LLD. When the driver is finished processing the command
	 * the done callback is invoked.
	 *
	 * This is called to inform the LLD to transfer
	 * scsi_bufflen(cmd) bytes. scsi_sg_count(cmd) speciefies the
	 * number of scatterlist entried in the command and
	 * scsi_sglist(cmd) returns the scatterlist.
	 *
	 * return values: see queuecommand
	 *
	 * If the LLD accepts the cmd, it should set the result to an
	 * appropriate value when completed before calling the done function.
	 *
	 * STATUS: REQUIRED FOR TARGET DRIVERS
	 */
	/* TODO: rename */
	//如果主机适配器支持STGT的目标器模式 必须实现这一个回调函数  STGT核心在处理完SCSI命令后 调用它向LLD传送相应
	int (* transfer_response)(struct scsi_cmnd *, //执行scsi命令符指针
				  void (*done)(struct scsi_cmnd *));

	/*
	 * This is an error handling strategy routine.  You don't need to
	 * define one of these if you don't want to - there is a default
	 * routine that is present that should work in most cases.  For those
	 * driver authors that have the inclination and ability to write their
	 * own strategy routine, this is where it is specified.  Note - the
	 * strategy routine is *ALWAYS* run in the context of the kernel eh
	 * thread.  Thus you are guaranteed to *NOT* be in an interrupt
	 * handler when you execute this, and you are also guaranteed to
	 * *NOT* have any other commands being queued while you are in the
	 * strategy routine. When you return from this function, operations
	 * return to normal.
	 *
	 * See scsi_error.c scsi_unjam_host for additional comments about
	 * what this function should and should not be attempting to do.
	 *
	 * Status: REQUIRED	(at least one of them)
	 */
	//错误恢复动作:放弃给定的命令 
	int (* eh_abort_handler)(struct scsi_cmnd *);//撤销一个scsi命令

    //错误恢复动作:SCSI设备复位
	int (* eh_device_reset_handler)(struct scsi_cmnd *);//reset某个硬盘设备

    //错误恢复动作:目标节点恢复
	int (* eh_target_reset_handler)(struct scsi_cmnd *);

    //错误恢复动作:SCSI总线复位
	int (* eh_bus_reset_handler)(struct scsi_cmnd *);//reset整个适配器芯片

	//错误恢复动作:主机适配器复位
	int (* eh_host_reset_handler)(struct scsi_cmnd *);

	/*
	 * Before the mid layer attempts to scan for a new device where none
	 * currently exists, it will call this entry in your driver.  Should
	 * your driver need to allocate any structs or perform any other init
	 * items in order to send commands to a currently unused target/lun
	 * combo, then this is where you can perform those allocations.  This
	 * is specifically so that drivers won't have to perform any kind of
	 * "is this a new device" checks in their queuecommand routine,
	 * thereby making the hot path a bit quicker.
	 *
	 * Return values: 0 on success, non-0 on failure
	 *
	 * Deallocation:  If we didn't find any devices at this ID, you will
	 * get an immediate call to slave_destroy().  If we find something
	 * here then you will get a call to slave_configure(), then the
	 * device will be used for however long it is kept around, then when
	 * the device is removed from the system (or * possibly at reboot
	 * time), you will then get a call to slave_destroy().  This is
	 * assuming you implement slave_configure and slave_destroy.
	 * However, if you allocate memory and hang it off the device struct,
	 * then you must implement the slave_destroy() routine at a minimum
	 * in order to avoid leaking memory
	 * each time a device is tore down.
	 *
	 * Status: OPTIONAL
	 */
	 //在扫描到一个新scsi设备后 用户可以在这个函数为其分配私有device结构
	int (* slave_alloc)(struct scsi_device *);

	/*
	 * Once the device has responded to an INQUIRY and we know the
	 * device is online, we call into the low level driver with the
	 * struct scsi_device *.  If the low level device driver implements
	 * this function, it *must* perform the task of setting the queue
	 * depth on the device.  All other tasks are optional and depend
	 * on what the driver supports and various implementation details.
	 * 
	 * Things currently recommended to be handled at this time include:
	 *
	 * 1.  Setting the device queue depth.  Proper setting of this is
	 *     described in the comments for scsi_adjust_queue_depth.
	 * 2.  Determining if the device supports the various synchronous
	 *     negotiation protocols.  The device struct will already have
	 *     responded to INQUIRY and the results of the standard items
	 *     will have been shoved into the various device flag bits, eg.
	 *     device->sdtr will be true if the device supports SDTR messages.
	 * 3.  Allocating command structs that the device will need.
	 * 4.  Setting the default timeout on this device (if needed).
	 * 5.  Anything else the low level driver might want to do on a device
	 *     specific setup basis...
	 * 6.  Return 0 on success, non-0 on error.  The device will be marked
	 *     as offline on error so that no access will occur.  If you return
	 *     non-0, your slave_destroy routine will never get called for this
	 *     device, so don't leave any loose memory hanging around, clean
	 *     up after yourself before returning non-0
	 *
	 * Status: OPTIONAL
	 */
	 //在收到SCSI设备的INQUIRY相应之后调用 
	int (* slave_configure)(struct scsi_device *);

	/*
	 * Immediately prior to deallocating the device and after all activity
	 * has ceased the mid layer calls this point so that the low level
	 * driver may completely detach itself from the scsi device and vice
	 * versa.  The low level driver is responsible for freeing any memory
	 * it allocated in the slave_alloc or slave_configure calls. 
	 *
	 * Status: OPTIONAL
	 */
	 //在销毁SCSI设备之前调用 SCSI子系统调用这个函数 释放关联的私有device结构
	void (* slave_destroy)(struct scsi_device *);

	/*
	 * Before the mid layer attempts to scan for a new device attached
	 * to a target where no target currently exists, it will call this
	 * entry in your driver.  Should your driver need to allocate any
	 * structs or perform any other init items in order to send commands
	 * to a currently unused target, then this is where you can perform
	 * those allocations.
	 *
	 * Return values: 0 on success, non-0 on failure
	 *
	 * Status: OPTIONAL
	 */
	 //在发现一个新的scsi目标器后调用 用户可以在这个函数中分配私有target结构
	int (* target_alloc)(struct scsi_target *);

	/*
	 * Immediately prior to deallocating the target structure, and
	 * after all activity to attached scsi devices has ceased, the
	 * midlayer calls this point so that the driver may deallocate
	 * and terminate any references to the target.
	 *
	 * Status: OPTIONAL
	 */
	 //在销毁SCSI设备之前调用 SCSI子系统调用这个函数 释放关联的私有target结构
	void (* target_destroy)(struct scsi_target *);

	/*
	 * If a host has the ability to discover targets on its own instead
	 * of scanning the entire bus, it can fill in this function and
	 * call scsi_scan_host().  This function will be called periodically
	 * until it returns 1 with the scsi_host and the elapsed time of
	 * the scan in jiffies.
	 *
	 * Status: OPTIONAL
	 */
	 //如果主机适配器实现了自己的扫描逻辑 则需要实现这个回调函数
	int (* scan_finished)(struct Scsi_Host *, unsigned long);

	/*
	 * If the host wants to be called before the scan starts, but
	 * after the midlayer has set up ready for the scan, it can fill
	 * in this function.
	 *
	 * Status: OPTIONAL
	 */
	 //如果主机适配器定义了自己的扫描逻辑 则需要实现这个函数
	void (* scan_start)(struct Scsi_Host *);

	/*
	 * Fill in this function to allow the queue depth of this host
	 * to be changeable (on a per device basis).  Returns either
	 * the current queue depth setting (may be different from what
	 * was passed in) or an error.  An error should only be
	 * returned if the requested depth is legal but the driver was
	 * unable to set it.  If the requested depth is illegal, the
	 * driver should set and return the closest legal queue depth.
	 *
	 * Status: OPTIONAL
	 */
	 //改变主机适配器队列深度的回调函数
	int (* change_queue_depth)(struct scsi_device *, int);

	/*
	 * Fill in this function to allow the changing of tag types
	 * (this also allows the enabling/disabling of tag command
	 * queueing).  An error should only be returned if something
	 * went wrong in the driver while trying to set the tag type.
	 * If the driver doesn't support the requested tag type, then
	 * it should set the closest type it does support without
	 * returning an error.  Returns the actual tag type set.
	 *
	 * Status: OPTIONAL
	 */
	 //改变主机适配器tag类型的回调函数
	int (* change_queue_type)(struct scsi_device *, int);

	/*
	 * This function determines the BIOS parameters for a given
	 * harddisk.  These tend to be numbers that are made up by
	 * the host adapter.  Parameters:
	 * size, device, list (heads, sectors, cylinders)
	 *
	 * Status: OPTIONAL
	 */
	 //返回磁盘的BIOS参数(柱面 磁道 扇区)
	int (* bios_param)(struct scsi_device *, struct block_device *,
			sector_t, int []);

	/*
	 * Can be used to export driver statistics and other infos to the
	 * world outside the kernel ie. userspace and it also provides an
	 * interface to feed the driver with information.
	 *
	 * Status: OBSOLETE
	 */
	 //用于导出驱动统计喝其他信息到内核外 
	int (*proc_info)(struct Scsi_Host *, char *, char **, off_t, int, int);

	/*
	 * This is an optional routine that allows the transport to become
	 * involved when a scsi io timer fires. The return value tells the
	 * timer routine how to finish the io timeout handling:
	 * EH_HANDLED:		I fixed the error, please complete the command
	 * EH_RESET_TIMER:	I need more time, reset the timer and
	 *			begin counting again
	 * EH_NOT_HANDLED	Begin normal error recovery
	 *
	 * Status: OPTIONAL
	 */
	 //在中间层发送SCSI命令超时 将调用低层驱动的这个回调函数 使底层有机会修正错误
	enum blk_eh_timer_return (*eh_timed_out)(struct scsi_cmnd *);

	/*
	 * Name of proc directory
	 */
	const char *proc_name;//proc目录名

	/*
	 * Used to store the procfs directory if a driver implements the
	 * proc_info method.
	 */
	struct proc_dir_entry *proc_dir;//如果驱动实现了pro_info方法 可以用来保存profs目录

	/*
	 * This determines if we will use a non-interrupt driven
	 * or an interrupt driven scheme.  It is set to the maximum number
	 * of simultaneous commands a given host adapter will accept.
	 */
	 //该域为主机适配器可以同时接收的命令数 必须大于0
	int can_queue;

	/*
	 * In many instances, especially where disconnect / reconnect are
	 * supported, our host also has an ID on the SCSI bus.  If this is
	 * the case, then it must be reserved.  Please set this_id to -1 if
	 * your setup is in single initiator mode, and the host lacks an
	 * ID.
	 */
	 //在很多情况下 尤其支持断开/重连 主机适配器在scsi总线占有一个id 
	int this_id;

	/*
	 * This determines the degree to which the host adapter is capable
	 * of scatter-gather.
	 */
	unsigned short sg_tablesize;//主机适配器支持聚散列表的能力

	/*
	 * Set this if the host adapter has limitations beside segment count.
	 */
	unsigned short max_sectors;//主机适配器单个scsi命令能访问扇区的最大数目

	/*
	 * DMA scatter gather segment boundary limit. A segment crossing this
	 * boundary will be split in two.
	 */
	unsigned long dma_boundary;//dma聚散段边界限制 跨越这个边界的段将被分割为两个

	/*
	 * This specifies "machine infinity" for host templates which don't
	 * limit the transfer size.  Note this limit represents an absolute
	 * maximum, and may be over the transfer limits allowed for
	 * individual devices (e.g. 256 for SCSI-1).
	 */
#define SCSI_DEFAULT_MAX_SECTORS	1024

	/*
	 * True if this host adapter can make good use of linked commands.
	 * This will allow more than one command to be queued to a given
	 * unit on a given host.  Set this to the maximum number of command
	 * blocks to be provided for each device.  Set this to 1 for one
	 * command block per lun, 2 for two, etc.  Do not set this to 0.
	 * You should make sure that the host adapter will do the right thing
	 * before you try setting this above 1.
	 */
	short cmd_per_lun;//允许连接到这个主机适配器的scsi设备的最大数目

	/*
	 * present contains counter indicating how many boards of this
	 * type were found when we did the scan.
	 */
	unsigned char present;//一个计数器 给出在扫描过程中发现了多少个这种类型的主机适配器

	/*
	 * This specifies the mode that a LLD supports.
	 */
	unsigned supported_mode:2;//低层驱动支持的模式

	/*
	 * True if this host adapter uses unchecked DMA onto an ISA bus.
	 */
	unsigned unchecked_isa_dma:1;//为1表示只能使用arm的低16M作为DMA地址空间 为0表示可以使用全部的32位

	/*
	 * True if this host adapter can make good use of clustering.
	 * I originally thought that if the tablesize was large that it
	 * was a waste of CPU cycles to prepare a cluster list, but
	 * it works out that the Buslogic is faster if you use a smaller
	 * number of segments (i.e. use clustering).  I guess it is
	 * inefficient.
	 */
	unsigned use_clustering:1;//如果为1 在scsi策略中构建scsi命令聚散列表时 可以合并内存连续的io请求

	/*
	 * True for emulated SCSI host adapters (e.g. ATAPI).
	 */
	unsigned emulated:1;//若是仿真的SCSI主机适配器 则为TRUE

	/*
	 * True if the low-level driver performs its own reset-settle delays.
	 */
	unsigned skip_settle_delay:1;//如果为1 在主机适配器复位和总该复位后 低层驱动自行执行reset-settle延迟

	/*
	 * True if we are using ordered write support.
	 */
	unsigned ordered_tag:1;//如果为1 表示正在使用排序写支持

	/*
	 * Countdown for host blocking with no commands outstanding.
	 */
	unsigned int max_host_blocked;//如果主机适配器没有待处理的命令 则暂时组赛他

	/*
	 * Default value for the blocking.  If the queue is empty,
	 * host_blocked counts down in the request_fn until it restarts
	 * host operations as zero is reached.  
	 *
	 * FIXME: This should probably be a value in the template
	 */
#define SCSI_DEFAULT_HOST_BLOCKED	7

	/*
	 * Pointer to the sysfs class properties for this host, NULL terminated.
	 */
	struct device_attribute **shost_attrs;//属于该模板的公共属性机器操作方法

	/*
	 * Pointer to the SCSI device properties for this host, NULL terminated.
	 */
	struct device_attribute **sdev_attrs;//连接到这个模板主机适配器上的scsi设备的公共属性及其操作方法

	/*
	 * List of hosts per template.
	 *
	 * This is only for use by scsi_module.c for legacy templates.
	 * For these access to it is synchronized implicitly by
	 * module_init/module_exit.
	 */
	struct list_head legacy_hosts;//连接Scsi_Host{}.sht_legacy_list

	/*
	 * Vendor Identifier associated with the host
	 *
	 * Note: When specifying vendor_id, be sure to read the
	 *   Vendor Type and ID formatting requirements specified in
	 *   scsi_netlink.h
	 */
	u64 vendor_id;//和主机适配器管理的厂商标识符
};

/*
 * shost state: If you alter this, you also need to alter scsi_sysfs.c
 * (for the ascii descriptions) and the state model enforcer:
 * scsi_host_set_state()
 */
enum scsi_host_state {
	SHOST_CREATED = 1,
	SHOST_RUNNING,
	SHOST_CANCEL,
	SHOST_DEL,
	SHOST_RECOVERY,
	SHOST_CANCEL_RECOVERY,
	SHOST_DEL_RECOVERY,
};

//主机适配器SCSI主机适配器
struct Scsi_Host {
	/*
	 * __devices is protected by the host_lock, but you should
	 * usually use scsi_device_lookup / shost_for_each_device
	 * to access it and don't care about locking yourself.
	 * In the rare case of beeing in irq context you can use
	 * their __ prefixed variants with the lock held. NEVER
	 * access this list directly from a driver.
	 */
	struct list_head	__devices;//这个主机适配器的scsi设备链表
	struct list_head	__targets;//这个主机适配器的目标节点链表
	
	struct scsi_host_cmd_pool *cmd_pool;//用于分配scsi命令结构和感测数据缓冲区的存储结构
	spinlock_t		free_list_lock;//用户保护free_list链表的自旋锁
	
	struct list_head	free_list; /* 预先准备的scsi命令结构的后备仓库链表 如果从cmd_pool分配失败 尝试从这个链表中获得.  backup store of cmd structs */
	struct list_head	starved_list;//由于主机适配器的处理能力 发送到scsi设备的请求队列中的请求可能无法被立即派发, 这时.将scsi设备连入主机适配器的此链表 
                                     //连入scsi_device{}.starved_entry
									   
	spinlock_t		default_lock; //保护这个主机适配器的自旋锁
	spinlock_t		*host_lock;//指向default_lock的指针

	struct mutex		scan_mutex;/* serialize scanning activity 用于异步扫描的互斥量*/

	struct list_head	eh_cmd_q; //进入错误恢复的scsi命令链表的表头
	struct task_struct    * ehandler;  /* Error recovery thread. 错误恢复线程 在分配结构的同时创建*/
	struct completion     * eh_action; /* Wait for specific actions on the在发送一个用于错误恢复的scsi命令后 需要等待其完成,才能继续
					      host. */
	wait_queue_head_t       host_wait;//scsi错误恢复等待队列 在scis设备错误恢复过程中 要操作scsi设备的进程在此队列上等待 直到恢复
	struct scsi_host_template *hostt;//指向创建这个主机适配器的模板指针
	struct scsi_transport_template *transportt;//指向scsi传输层模板的指针

	/*
	 * Area to keep a shared tag map (if needed, will be
	 * NULL if not).
	 */
	struct blk_queue_tag	*bqt;//指向为该主机适配器管理target请求的描述符指针 为连接到它所有的scsi设备的请求队列

	/*
	 * The following two fields are protected with host_lock;
	 * however, eh routines can safely access during eh processing
	 * without acquiring the lock.
	 */
	unsigned int host_busy;		   /* commands actually active on low-level */
	unsigned int host_failed;	   /* commands that failed. */
	unsigned int host_eh_scheduled;    /* EH scheduled without command */
    
	unsigned int host_no;  /* 适配器号Used for IOCTL_GET_IDLUN, /proc/scsi et al. */
	int resetting; /* if set, it means that last_reset is a valid value */
	unsigned long last_reset;

	/*
	 * These three parameters can be used to allow for wide scsi,
	 * and for host adapters that support multiple busses
	 * The first two should be set to 1 more than the actual max id
	 * or lun (i.e. 8 for normal systems).
	 */
	unsigned int max_id;       /* 最大的scsi node数量 */
	unsigned int max_lun;   /* 最大的lun数量 */
	unsigned int max_channel;  /* 最大的channel数量一个scsi端口适配器可能拥有多个channel，每个channel拥有一条scsi总线 */

	/*
	 * This is a unique identifier that must be assigned so that we
	 * have some way of identifying each detected host adapter properly
	 * and uniquely.  For hosts that do not support more than one card
	 * in the system at one time, this does not need to be set.  It is
	 * initialized to 0 in scsi_register.
	 */
	unsigned int unique_id;

	/*
	 * The maximum length of SCSI commands that this host can accept.
	 * Probably 12 for most host adapters, but could be 16 for others.
	 * or 260 if the driver supports variable length cdbs.
	 * For drivers that don't set this field, a value of 12 is
	 * assumed.
	 */
	unsigned short max_cmd_len;

	int this_id;
	int can_queue;
	short cmd_per_lun;
	short unsigned int sg_tablesize;
	short unsigned int max_sectors;
	unsigned long dma_boundary;
	/* 
	 * Used to assign serial numbers to the cmds.
	 * Protected by the host lock.
	 */
	unsigned long cmd_serial_number;
	
	unsigned active_mode:2;
	unsigned unchecked_isa_dma:1;
	unsigned use_clustering:1;
	unsigned use_blk_tcq:1;

	/*
	 * Host has requested that no further requests come through for the
	 * time being.
	 */
	unsigned host_self_blocked:1;
    
	/*
	 * Host uses correct SCSI ordering not PC ordering. The bit is
	 * set for the minority of drivers whose authors actually read
	 * the spec ;).
	 */
	unsigned reverse_ordering:1;

	/*
	 * Ordered write support
	 */
	unsigned ordered_tag:1;

	/* Task mgmt function in progress */
	unsigned tmf_in_progress:1;

	/* Asynchronous scan in progress */
	unsigned async_scan:1;

	/*
	 * Optional work queue to be utilized by the transport
	 */
	char work_q_name[20];
	struct workqueue_struct *work_q;

	/*
	 * Host has rejected a command because it was busy.
	 */
	unsigned int host_blocked;

	/*
	 * Value host_blocked counts down from
	 */
	unsigned int max_host_blocked;

	/* Protection Information */
	unsigned int prot_capabilities;
	unsigned char prot_guard_type;

	/*
	 * q used for scsi_tgt msgs, async events or any other requests that
	 * need to be processed in userspace
	 */
	struct request_queue *uspace_req_q;

	/* legacy crap */
	unsigned long base;
	unsigned long io_port;
	unsigned char n_io_port;
	unsigned char dma_channel;
	unsigned int  irq;
	

	enum scsi_host_state shost_state;

	/* ldm bits */
	struct device		shost_gendev, shost_dev;

	/*
	 * List of hosts per template.
	 *
	 * This is only for use by scsi_module.c for legacy templates.
	 * For these access to it is synchronized implicitly by
	 * module_init/module_exit.
	 */
	struct list_head sht_legacy_list;

	/*
	 * Points to the transport data (if any) which is allocated
	 * separately
	 */
	void *shost_data;

	/*
	 * Points to the physical bus device we'd use to do DMA
	 * Needed just in case we have virtual hosts.
	 */
	struct device *dma_dev;

	/*
	 * We should ensure that this is aligned, both for better performance
	 * and also because some compilers (m68k) don't automatically force
	 * alignment to a long boundary.
	 */
	unsigned long hostdata[0]  /* Used for storage of host specific stuff 对于U盘指向struct us_data */
		__attribute__ ((aligned (sizeof(unsigned long))));
};

#define		class_to_shost(d)	\
	container_of(d, struct Scsi_Host, shost_dev)

#define shost_printk(prefix, shost, fmt, a...)	\
	dev_printk(prefix, &(shost)->shost_gendev, fmt, ##a)

static inline void *shost_priv(struct Scsi_Host *shost)
{
	return (void *)shost->hostdata;
}

int scsi_is_host_device(const struct device *);

static inline struct Scsi_Host *dev_to_shost(struct device *dev)
{
	while (!scsi_is_host_device(dev)) {
		if (!dev->parent)
			return NULL;
		dev = dev->parent;
	}
	return container_of(dev, struct Scsi_Host, shost_gendev);
}

static inline int scsi_host_in_recovery(struct Scsi_Host *shost)
{
	return shost->shost_state == SHOST_RECOVERY ||
		shost->shost_state == SHOST_CANCEL_RECOVERY ||
		shost->shost_state == SHOST_DEL_RECOVERY ||
		shost->tmf_in_progress;
}

extern int scsi_queue_work(struct Scsi_Host *, struct work_struct *);
extern void scsi_flush_work(struct Scsi_Host *);

extern struct Scsi_Host *scsi_host_alloc(struct scsi_host_template *, int);
extern int __must_check scsi_add_host_with_dma(struct Scsi_Host *,
					       struct device *,
					       struct device *);
extern void scsi_scan_host(struct Scsi_Host *);
extern void scsi_rescan_device(struct device *);
extern void scsi_remove_host(struct Scsi_Host *);
extern struct Scsi_Host *scsi_host_get(struct Scsi_Host *);
extern void scsi_host_put(struct Scsi_Host *t);
extern struct Scsi_Host *scsi_host_lookup(unsigned short);
extern const char *scsi_host_state_name(enum scsi_host_state);

extern u64 scsi_calculate_bounce_limit(struct Scsi_Host *);

static inline int __must_check scsi_add_host(struct Scsi_Host *host,
					     struct device *dev)
{
	return scsi_add_host_with_dma(host, dev, dev);
}

static inline struct device *scsi_get_device(struct Scsi_Host *shost)
{
        return shost->shost_gendev.parent;
}

/**
 * scsi_host_scan_allowed - Is scanning of this host allowed
 * @shost:	Pointer to Scsi_Host.
 **/
static inline int scsi_host_scan_allowed(struct Scsi_Host *shost)
{
	return shost->shost_state == SHOST_RUNNING;
}

extern void scsi_unblock_requests(struct Scsi_Host *);
extern void scsi_block_requests(struct Scsi_Host *);

struct class_container;

extern struct request_queue *__scsi_alloc_queue(struct Scsi_Host *shost,
						void (*) (struct request_queue *));
/*
 * These two functions are used to allocate and free a pseudo device
 * which will connect to the host adapter itself rather than any
 * physical device.  You must deallocate when you are done with the
 * thing.  This physical pseudo-device isn't real and won't be available
 * from any high-level drivers.
 */
extern void scsi_free_host_dev(struct scsi_device *);
extern struct scsi_device *scsi_get_host_dev(struct Scsi_Host *);

/*
 * DIF defines the exchange of protection information between
 * initiator and SBC block device.
 *
 * DIX defines the exchange of protection information between OS and
 * initiator.
 */
enum scsi_host_prot_capabilities {
	SHOST_DIF_TYPE1_PROTECTION = 1 << 0, /* T10 DIF Type 1 */
	SHOST_DIF_TYPE2_PROTECTION = 1 << 1, /* T10 DIF Type 2 */
	SHOST_DIF_TYPE3_PROTECTION = 1 << 2, /* T10 DIF Type 3 */

	SHOST_DIX_TYPE0_PROTECTION = 1 << 3, /* DIX between OS and HBA only */
	SHOST_DIX_TYPE1_PROTECTION = 1 << 4, /* DIX with DIF Type 1 */
	SHOST_DIX_TYPE2_PROTECTION = 1 << 5, /* DIX with DIF Type 2 */
	SHOST_DIX_TYPE3_PROTECTION = 1 << 6, /* DIX with DIF Type 3 */
};

/*
 * SCSI hosts which support the Data Integrity Extensions must
 * indicate their capabilities by setting the prot_capabilities using
 * this call.
 */
static inline void scsi_host_set_prot(struct Scsi_Host *shost, unsigned int mask)
{
	shost->prot_capabilities = mask;
}

static inline unsigned int scsi_host_get_prot(struct Scsi_Host *shost)
{
	return shost->prot_capabilities;
}

static inline unsigned int scsi_host_dif_capable(struct Scsi_Host *shost, unsigned int target_type)
{
	static unsigned char cap[] = { 0,
				       SHOST_DIF_TYPE1_PROTECTION,
				       SHOST_DIF_TYPE2_PROTECTION,
				       SHOST_DIF_TYPE3_PROTECTION };

	return shost->prot_capabilities & cap[target_type] ? target_type : 0;
}

static inline unsigned int scsi_host_dix_capable(struct Scsi_Host *shost, unsigned int target_type)
{
#if defined(CONFIG_BLK_DEV_INTEGRITY)
	static unsigned char cap[] = { SHOST_DIX_TYPE0_PROTECTION,
				       SHOST_DIX_TYPE1_PROTECTION,
				       SHOST_DIX_TYPE2_PROTECTION,
				       SHOST_DIX_TYPE3_PROTECTION };

	return shost->prot_capabilities & cap[target_type];
#endif
	return 0;
}

/*
 * All DIX-capable initiators must support the T10-mandated CRC
 * checksum.  Controllers can optionally implement the IP checksum
 * scheme which has much lower impact on system performance.  Note
 * that the main rationale for the checksum is to match integrity
 * metadata with data.  Detecting bit errors are a job for ECC memory
 * and buses.
 */

enum scsi_host_guard_type {
	SHOST_DIX_GUARD_CRC = 1 << 0,
	SHOST_DIX_GUARD_IP  = 1 << 1,
};

static inline void scsi_host_set_guard(struct Scsi_Host *shost, unsigned char type)
{
	shost->prot_guard_type = type;
}

static inline unsigned char scsi_host_get_guard(struct Scsi_Host *shost)
{
	return shost->prot_guard_type;
}

/* legacy interfaces */
extern struct Scsi_Host *scsi_register(struct scsi_host_template *, int);
extern void scsi_unregister(struct Scsi_Host *);
extern int scsi_host_set_state(struct Scsi_Host *, enum scsi_host_state);

#endif /* _SCSI_SCSI_HOST_H */
