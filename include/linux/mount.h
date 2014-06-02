/*
 *
 * Definitions for mount interface. This describes the in the kernel build 
 * linkedlist with mounted filesystems.
 *
 * Author:  Marco van Wieringen <mvw@planets.elm.net>
 *
 * Version: $Id: mount.h,v 2.0 1996/11/17 16:48:14 mvw Exp mvw $
 *
 */
#ifndef _LINUX_MOUNT_H
#define _LINUX_MOUNT_H
#ifdef __KERNEL__

#include <linux/types.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <asm/atomic.h>

struct super_block;
struct vfsmount;
struct dentry;
struct mnt_namespace;

/**
 * 禁止setuid
 */
#define MNT_NOSUID	0x01
/**
 * 虚拟文件系统，没有与物理设备关联。
 */
#define MNT_NODEV	0x02
/**
 * 不允许执行
 */
#define MNT_NOEXEC	0x04
/**
 * 不记录访问时间和修改时间
 */
#define MNT_NOATIME	0x08
#define MNT_NODIRATIME	0x10
#define MNT_RELATIME	0x20

/**
 * 用于NFS和AFS
 */
#define MNT_SHRINKABLE	0x100

/* 共享和不可绑定装载 */
#define MNT_SHARED	0x1000	/* if the vfsmount is a shared mount */
#define MNT_UNBINDABLE	0x2000	/* if the vfsmount is a unbindable mount */
#define MNT_PNODE_MASK	0x3000	/* propagation flag mask */

/**
 * 文件系统装载点
 */
struct vfsmount {
	/* 用于将装载点加入到散列表中 */
	struct list_head mnt_hash;
	/* 父文件系统的装载点 */
	struct vfsmount *mnt_parent;	/* fs we are mounted on */
	/* 装载点在父目录中的dentry结构 */
	struct dentry *mnt_mountpoint;	/* dentry of mountpoint */
	/* 装载点所在的根目录缓存 */
	struct dentry *mnt_root;	/* root of the mounted tree */
	/* 装载点的超级块 */
	struct super_block *mnt_sb;	/* pointer to superblock */
	/* 链表头，表示所有装载在文件系统内的文件系统 */
	struct list_head mnt_mounts;	/* list of children, anchored here */
	/* 通过此字段，将文件系统装载点添加到父文件系统的mnt_mounts链表中 */
	struct list_head mnt_child;	/* and going through their mnt_child */
	/* 加载标志，如MNT_NOSUID */
	int mnt_flags;
	/* 4 bytes hole on 64bits arches */
	char *mnt_devname;		/* Name of device e.g. /dev/dsk/hda1 */
	/* 通过此字段添加到命名空间的加载点链表中 */
	struct list_head mnt_list;
	/* 自动过期的装载链表元素 */
	struct list_head mnt_expire;	/* link in fs-specific expiry list */
	/* 共享装载链表元素 */
	struct list_head mnt_share;	/* circular list of shared mounts */
	/* 从属装载、主装载使用的链表和指针 */
	struct list_head mnt_slave_list;/* list of slave mounts */
	struct list_head mnt_slave;	/* slave list entry */
	struct vfsmount *mnt_master;	/* slave is on master->mnt_slave_list */
	/* 所属的命名空间 */
	struct mnt_namespace *mnt_ns;	/* containing namespace */
	/*
	 * We put mnt_count & mnt_expiry_mark at the end of struct vfsmount
	 * to let these frequently modified fields in a separate cache line
	 * (so that reads of mnt_flags wont ping-pong on SMP machines)
	 */
	/* 计数器，使用mntput和mntget维护 */
	atomic_t mnt_count;
	/* 是否处理装载过期 */
	int mnt_expiry_mark;		/* true if marked for expiry */
	int mnt_pinned;
};

static inline struct vfsmount *mntget(struct vfsmount *mnt)
{
	if (mnt)
		atomic_inc(&mnt->mnt_count);
	return mnt;
}

extern void mntput_no_expire(struct vfsmount *mnt);
extern void mnt_pin(struct vfsmount *mnt);
extern void mnt_unpin(struct vfsmount *mnt);

static inline void mntput(struct vfsmount *mnt)
{
	if (mnt) {
		mnt->mnt_expiry_mark = 0;
		mntput_no_expire(mnt);
	}
}

extern void free_vfsmnt(struct vfsmount *mnt);
extern struct vfsmount *alloc_vfsmnt(const char *name);
extern struct vfsmount *do_kern_mount(const char *fstype, int flags,
				      const char *name, void *data);

struct file_system_type;
extern struct vfsmount *vfs_kern_mount(struct file_system_type *type,
				      int flags, const char *name,
				      void *data);

struct nameidata;

extern int do_add_mount(struct vfsmount *newmnt, struct nameidata *nd,
			int mnt_flags, struct list_head *fslist);

extern void mark_mounts_for_expiry(struct list_head *mounts);
extern void shrink_submounts(struct vfsmount *mountpoint, struct list_head *mounts);

extern spinlock_t vfsmount_lock;
extern dev_t name_to_dev_t(char *name);

#endif
#endif /* _LINUX_MOUNT_H */
