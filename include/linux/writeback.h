/*
 * include/linux/writeback.h
 */
#ifndef WRITEBACK_H
#define WRITEBACK_H

#include <linux/sched.h>
#include <linux/fs.h>

struct backing_dev_info;

extern spinlock_t inode_lock;
extern struct list_head inode_in_use;
extern struct list_head inode_unused;

/*
 * Yes, writeback.h requires sched.h
 * No, sched.h is not included from here.
 */
static inline int task_is_pdflush(struct task_struct *task)
{
	return task->flags & PF_FLUSHER;
}

#define current_is_pdflush()	task_is_pdflush(current)

/*
 * fs/fs-writeback.c
 */
enum writeback_sync_modes {
	/* 同提交同步请求，不等待同步完成 */
	WB_SYNC_NONE,	/* Don't wait on anything */
	/* 等待所有数据同步完毕 */
	WB_SYNC_ALL,	/* Wait on every mapping */
	/* 用于sync系统调用，类似于WB_SYNC_NONE。在同步时，并不将节点移动到s_io_more，而是将它放到脏链表中 */
	WB_SYNC_HOLD,	/* Hold the inode on sb_dirty for sys_sync() */
};

/*
 * A control structure which tells the writeback code what to do.  These are
 * always on the stack, and hence need no locking.  They are always initialised
 * in a manner such that unspecified fields are set to zero.
 */
/**
 * 用于控制脏页回写，以及返回回写结果的数据结构。
 */
struct writeback_control {
	/* 回写设备信息，只回写该设备上的脏节点 */
	struct backing_dev_info *bdi;	/* If !NULL, only write back this
					   queue */
	/* 同步模式 */
	enum writeback_sync_modes sync_mode;
	/* 如果脏的缓存数据，变脏时间超过此时间，将回写 */
	unsigned long *older_than_this;	/* If !NULL, only write back inodes
					   older than this */
	/* 回写的数量，超过此数量将不再回写 */
	long nr_to_write;		/* Write this many pages, and decrement
					   this for each page written */
	/* 未被回写的页数量 */
	long pages_skipped;		/* Pages which were not written */

	/*
	 * For a_ops->writepages(): is start or end are non-zero then this is
	 * a hint that the filesystem need only write out the pages inside that
	 * byterange.  The byte at `end' is included in the writeout request.
	 */
	loff_t range_start;
	loff_t range_end;

	/* 因写队列在遇到拥塞时是否阻塞 */
	unsigned nonblocking:1;		/* Don't get stuck on request queues */
	/* 通知高层，回写期间发生了拥塞 */
	unsigned encountered_congestion:1; /* An output: a queue is full */
	/* 周期性回写，由pdflush发出 */
	unsigned for_kupdate:1;		/* A kupdate writeback */
	/* 由内存回收引起的回写 */
	unsigned for_reclaim:1;		/* Invoked from the page allocator */
	/* 由do_writepages引起的回写 */
	unsigned for_writepages:1;	/* This is a writepages() call */
	/* 是否只回写由range_start和range_end限定的范围。如果为1，表示可能多次遍历页。 */
	unsigned range_cyclic:1;	/* range_start is cyclic */
};

/*
 * fs/fs-writeback.c
 */	
void writeback_inodes(struct writeback_control *wbc);
int inode_wait(void *);
void sync_inodes_sb(struct super_block *, int wait);
void sync_inodes(int wait);

/* writeback.h requires fs.h; it, too, is not included from here. */
static inline void wait_on_inode(struct inode *inode)
{
	might_sleep();
	wait_on_bit(&inode->i_state, __I_LOCK, inode_wait,
							TASK_UNINTERRUPTIBLE);
}
static inline void inode_sync_wait(struct inode *inode)
{
	might_sleep();
	wait_on_bit(&inode->i_state, __I_SYNC, inode_wait,
							TASK_UNINTERRUPTIBLE);
}


/*
 * mm/page-writeback.c
 */
int wakeup_pdflush(long nr_pages);
void laptop_io_completion(void);
void laptop_sync_completion(void);
void throttle_vm_writeout(gfp_t gfp_mask);

/* These are exported to sysctl. */
extern int dirty_background_ratio;
extern int vm_dirty_ratio;
extern int dirty_writeback_interval;
extern int dirty_expire_interval;
extern int block_dump;
extern int laptop_mode;

extern int dirty_ratio_handler(struct ctl_table *table, int write,
		struct file *filp, void __user *buffer, size_t *lenp,
		loff_t *ppos);

struct ctl_table;
struct file;
int dirty_writeback_centisecs_handler(struct ctl_table *, int, struct file *,
				      void __user *, size_t *, loff_t *);

void page_writeback_init(void);
void balance_dirty_pages_ratelimited_nr(struct address_space *mapping,
					unsigned long nr_pages_dirtied);

static inline void
balance_dirty_pages_ratelimited(struct address_space *mapping)
{
	balance_dirty_pages_ratelimited_nr(mapping, 1);
}

typedef int (*writepage_t)(struct page *page, struct writeback_control *wbc,
				void *data);

int pdflush_operation(void (*fn)(unsigned long), unsigned long arg0);
int generic_writepages(struct address_space *mapping,
		       struct writeback_control *wbc);
int write_cache_pages(struct address_space *mapping,
		      struct writeback_control *wbc, writepage_t writepage,
		      void *data);
int do_writepages(struct address_space *mapping, struct writeback_control *wbc);
int sync_page_range(struct inode *inode, struct address_space *mapping,
			loff_t pos, loff_t count);
int sync_page_range_nolock(struct inode *inode, struct address_space *mapping,
			   loff_t pos, loff_t count);
void set_page_dirty_balance(struct page *page, int page_mkwrite);
void writeback_set_ratelimit(void);

/* pdflush.c */
extern int nr_pdflush_threads;	/* Global so it can be exported to sysctl
				   read-only. */


#endif		/* WRITEBACK_H */
