#ifndef _LINUX_CDEV_H
#define _LINUX_CDEV_H
#ifdef __KERNEL__

#include <linux/kobject.h>
#include <linux/kdev_t.h>
#include <linux/list.h>

struct file_operations;
struct inode;
struct module;

/* 字符设备对象 */
struct cdev {
	/* 通过此结构将设备添加到通用设备文件系统(/sys/)中 */
	struct kobject kobj;
	/* 所属模块 */
	struct module *owner;
	/* 文件操作回调 */
	const struct file_operations *ops;
	/* 链表，包含所有表示该设备的inode */
	struct list_head list;
	/* 设备号 */
	dev_t dev;
	unsigned int count;
};

void cdev_init(struct cdev *, const struct file_operations *);

struct cdev *cdev_alloc(void);

void cdev_put(struct cdev *p);

int cdev_add(struct cdev *, dev_t, unsigned);

void cdev_del(struct cdev *);

void cd_forget(struct inode *);

extern struct backing_dev_info directly_mappable_cdev_bdi;

#endif
#endif
