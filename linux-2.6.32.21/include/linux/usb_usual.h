/*
 * Interface to the libusual.
 *
 * Copyright (c) 2005 Pete Zaitcev <zaitcev@redhat.com>
 * Copyright (c) 1999-2002 Matthew Dharm (mdharm-usb@one-eyed-alien.net)
 * Copyright (c) 1999 Michael Gee (michael@linuxspecific.com)
 */

#ifndef __LINUX_USB_USUAL_H
#define __LINUX_USB_USUAL_H


/* We should do this for cleanliness... But other usb_foo.h do not do this. */
/* #include <linux/usb.h> */

/*
 * The flags field, which we store in usb_device_id.driver_info.
 * It is compatible with the old usb-storage flags in lower 24 bits.
 */

/*
 * Static flag definitions.  We use this roundabout technique so that the
 * proc_info() routine can automatically display a message for each flag.
 */
#define US_DO_ALL_FLAGS						\
	US_FLAG(SINGLE_LUN,	0x00000001)			\
		/* allow access to only LUN 0 */		\
	US_FLAG(NEED_OVERRIDE,	0x00000002)			\
		/* unusual_devs entry is necessary */		\
	US_FLAG(SCM_MULT_TARG,	0x00000004)			\
		/* supports multiple targets */			\
	US_FLAG(FIX_INQUIRY,	0x00000008)			\
		/* INQUIRY response needs faking */		\
	US_FLAG(FIX_CAPACITY,	0x00000010)			\
		/* READ CAPACITY response too big */		\
	US_FLAG(IGNORE_RESIDUE,	0x00000020)			\
		/* reported residue is wrong */			\
	US_FLAG(BULK32,		0x00000040)			\
		/* Uses 32-byte CBW length */			\
	US_FLAG(NOT_LOCKABLE,	0x00000080)			\
		/* PREVENT/ALLOW not supported */		\
	US_FLAG(GO_SLOW,	0x00000100)			\
		/* Need delay after Command phase */		\
	US_FLAG(NO_WP_DETECT,	0x00000200)			\
		/* Don't check for write-protect */		\
	US_FLAG(MAX_SECTORS_64,	0x00000400)			\
		/* Sets max_sectors to 64    */			\
	US_FLAG(IGNORE_DEVICE,	0x00000800)			\
		/* Don't claim device */			\
	US_FLAG(CAPACITY_HEURISTICS,	0x00001000)		\
		/* sometimes sizes is too big */		\
	US_FLAG(MAX_SECTORS_MIN,0x00002000)			\
		/* Sets max_sectors to arch min */		\
	US_FLAG(BULK_IGNORE_TAG,0x00004000)			\
		/* Ignore tag mismatch in bulk operations */    \
	US_FLAG(SANE_SENSE,     0x00008000)			\
		/* Sane Sense (> 18 bytes) */			\
	US_FLAG(CAPACITY_OK,	0x00010000)			\
		/* READ CAPACITY response is correct */		\
	US_FLAG(BAD_SENSE,	0x00020000)			\
		/* Bad Sense (never more than 18 bytes) */

#define US_FLAG(name, value)	US_FL_##name = value ,
enum { US_DO_ALL_FLAGS };
#undef US_FLAG

/*
 * The bias field for libusual and friends.
 */
#define USB_US_TYPE_NONE   0
#define USB_US_TYPE_STOR   1		/* usb-storage */
#define USB_US_TYPE_UB     2		/* ub */

#define USB_US_TYPE(flags) 		(((flags) >> 24) & 0xFF)
#define USB_US_ORIG_FLAGS(flags)	((flags) & 0x00FFFFFF)

/*
 * This is probably not the best place to keep these constants, conceptually.
 * But it's the only header included into all places which need them.
 */

/* Sub Classes */

#define US_SC_RBC	0x01		/* Typically, flash devices */
#define US_SC_8020	0x02		/* CD-ROM */
#define US_SC_QIC	0x03		/* QIC-157 Tapes */
#define US_SC_UFI	0x04		/* Floppy */
#define US_SC_8070	0x05		/* Removable media */
#define US_SC_SCSI	0x06		/* Transparent */
#define US_SC_LOCKABLE	0x07		/* Password-protected */

#define US_SC_ISD200    0xf0		/* ISD200 ATA */
#define US_SC_CYP_ATACB 0xf1		/* Cypress ATACB */
#define US_SC_DEVICE	0xff		/* Use device's value */

/* Protocols */

/*
USB的传输模式有4种如下
控制传输（Control Transfer）
	所有设备都要求有支持控制传输的端点，一般端点号为0的为控制端点，USB协议将其定义设备的默认端点
中断传输（Interrupt Transfer）
	发送或接收少量的数据，而且并不经常进行数据传输,键盘、鼠标、游戏杆等
批量传输或叫块传输（Bulk Transfer）
    提供批量传输类型是为了支持在某些不确定的时间内进行大量的数据通信，如打印机、扫描仪、硬盘、光盘等
实时传输或叫同步传输（Isochronous Transfer）
	实时传输是为支持某些对时间要求很高、数据量很大应用要求而提出的，使用这种传输类型的设备有麦克风、调制解调器、音频设备等
*/
#define US_PR_CBI	0x00		/* Control/Bulk/Interrupt 支持控制传输 批量传输和终端传输*/
#define US_PR_CB	0x01		/* Control/Bulk w/o interrupt 支持控制传输 批量传输*/
#define US_PR_BULK	0x50		/* bulk only 对于U盘属于 Bulk-only批量传输*/

#define US_PR_USBAT	0x80		/* SCM-ATAPI bridge */
#define US_PR_EUSB_SDDR09	0x81	/* SCM-SCSI bridge for SDDR-09 */
#define US_PR_SDDR55	0x82		/* SDDR-55 (made up) */
#define US_PR_DPCM_USB  0xf0		/* Combination CB/SDDR09 */
#define US_PR_FREECOM   0xf1		/* Freecom */
#define US_PR_DATAFAB   0xf2		/* Datafab chipsets */
#define US_PR_JUMPSHOT  0xf3		/* Lexar Jumpshot */
#define US_PR_ALAUDA    0xf4		/* Alauda chipsets */
#define US_PR_KARMA     0xf5		/* Rio Karma */

#define US_PR_DEVICE	0xff		/* Use device's value */

/*
 */
extern int usb_usual_ignore_device(struct usb_interface *intf);
extern struct usb_device_id usb_storage_usb_ids[];

#ifdef CONFIG_USB_LIBUSUAL

extern void usb_usual_set_present(int type);
extern void usb_usual_clear_present(int type);
extern int usb_usual_check_type(const struct usb_device_id *, int type);
#else

#define usb_usual_set_present(t)	do { } while(0)
#define usb_usual_clear_present(t)	do { } while(0)
#define usb_usual_check_type(id, t)	(0)
#endif /* CONFIG_USB_LIBUSUAL */

#endif /* __LINUX_USB_USUAL_H */
