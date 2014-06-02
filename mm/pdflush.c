/*
 * mm/pdflush.c - worker threads for writing back filesystem data
 *
 * Copyright (C) 2002, Linus Torvalds.
 *
 * 09Apr2002	akpm@zip.com.au
 *		Initial version
 * 29Feb2004	kaos@sgi.com
 *		Move worker thread creation to kthread to avoid chewing
 *		up stack space with nested calls to kernel_thread.
 */

#include <linux/sched.h>
#include <linux/list.h>
#include <linux/signal.h>
#include <linux/spinlock.h>
#include <linux/gfp.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>		// Needed by writeback.h
#include <linux/writeback.h>	// Prototypes pdflush_operation()
#include <linux/kthread.h>
#include <linux/cpuset.h>
#include <linux/freezer.h>


/*
 * Minimum and maximum number of pdflush instances
 */
#define MIN_PDFLUSH_THREADS	2
#define MAX_PDFLUSH_THREADS	8

static void start_one_pdflush_thread(void);


/*
 * The pdflush threads are worker threads for writing back dirty data.
 * Ideally, we'd like one thread per active disk spindle.  But the disk
 * topology is very hard to divine at this level.   Instead, we take
 * care in various places to prevent more than one pdflush thread from
 * performing writeback against a single filesystem.  pdflush threads
 * have the PF_FLUSHER flag set in current->flags to aid in this.
 */

/*
 * All the pdflush threads.  Protected by pdflush_lock
 */
static LIST_HEAD(pdflush_list);
static DEFINE_SPINLOCK(pdflush_lock);

/*
 * The count of currently-running pdflush threads.  Protected
 * by pdflush_lock.
 *
 * Readable by sysctl, but not writable.  Published to userspace at
 * /proc/sys/vm/nr_pdflush_threads.
 */
int nr_pdflush_threads = 0;

/*
 * The time at which the pdflush thread pool last went empty
 */
static unsigned long last_empty_jifs;

/*
 * The pdflush thread.
 *
 * Thread pool management algorithm:
 * 
 * - The minimum and maximum number of pdflush instances are bound
 *   by MIN_PDFLUSH_THREADS and MAX_PDFLUSH_THREADS.
 * 
 * - If there have been no idle pdflush instances for 1 second, create
 *   a new one.
 * 
 * - If the least-recently-went-to-sleep pdflush thread has been asleep
 *   for more than one second, terminate a thread.
 */

/*
 * A structure for passing work to a pdflush thread.  Also for passing
 * state information between pdflush threads.  Protected by pdflush_lock.
 */
/**
 * pdflush线程待完成的工作描述符
 */
struct pdflush_work {
	/* 完成该任务的pdflush线程 */
	struct task_struct *who;	/* The thread */
	/* 要完成的任务 */
	void (*fn)(unsigned long);	/* A callback function */
	/* 任务参数 */
	unsigned long arg0;		/* An argument to the callback */
	/* 通过此字段将任务链接到全局链表中 */
	struct list_head list;		/* On pdflush_list, when idle */
	/* 上一次进入睡眠的时间，可用于删除多余的线程 */
	unsigned long when_i_went_to_sleep;
};

static int __pdflush(struct pdflush_work *my_work)
{
	current->flags |= PF_FLUSHER | PF_SWAPWRITE;
	set_freezable();
	my_work->fn = NULL;
	my_work->who = current;
	INIT_LIST_HEAD(&my_work->list);

	spin_lock_irq(&pdflush_lock);
	nr_pdflush_threads++;
	for ( ; ; ) {
		struct pdflush_work *pdf;

		set_current_state(TASK_INTERRUPTIBLE);
		/* 将任务添加到全局链表中，等待唤醒 */
		list_move(&my_work->list, &pdflush_list);
		/* 记录睡眠时间 */
		my_work->when_i_went_to_sleep = jiffies;
		spin_unlock_irq(&pdflush_lock);
		schedule();
		try_to_freeze();
		spin_lock_irq(&pdflush_lock);
		/* 在获取锁以后，判断特殊的唤醒情况 */
		if (!list_empty(&my_work->list)) {
			/*
			 * Someone woke us up, but without removing our control
			 * structure from the global list.  swsusp will do this
			 * in try_to_freeze()->refrigerator().  Handle it.
			 */
			my_work->fn = NULL;
			continue;
		}
		/* 没有指定回调，显然是一种不合理的情况 */
		if (my_work->fn == NULL) {
			printk("pdflush: bogus wakeup\n");
			continue;
		}
		spin_unlock_irq(&pdflush_lock);

		/* 回调处理函数，回写磁盘数据 */
		(*my_work->fn)(my_work->arg0);

		/*
		 * Thread creation: For how long have there been zero
		 * available threads?
		 */
		if (jiffies - last_empty_jifs > 1 * HZ) {/* 运行时间超过了1秒，说明回写数量多，系统忙 */
			/* unlocked list_empty() test is OK here */
			if (list_empty(&pdflush_list)) {/* 所有pdflush进程都在运行 */
				/* unlocked test is OK here */
				/* 进程数量没有超过限制 */
				if (nr_pdflush_threads < MAX_PDFLUSH_THREADS)
					start_one_pdflush_thread();/* 启动新线程 */
			}
		}

		spin_lock_irq(&pdflush_lock);
		my_work->fn = NULL;

		/*
		 * Thread destruction: For how long has the sleepiest
		 * thread slept?
		 */
		if (list_empty(&pdflush_list))
			continue;
		if (nr_pdflush_threads <= MIN_PDFLUSH_THREADS)
			continue;
		pdf = list_entry(pdflush_list.prev, struct pdflush_work, list);
		/* 全局链表中，第一个进程睡眠超过1秒，系统空闲 */
		if (jiffies - pdf->when_i_went_to_sleep > 1 * HZ) {
			/* Limit exit rate */
			/* 修改第一个线程的睡眠时间 */
			pdf->when_i_went_to_sleep = jiffies;
			/* 本线程退出 */
			break;					/* exeunt */
		}
	}
	nr_pdflush_threads--;
	spin_unlock_irq(&pdflush_lock);
	return 0;
}

/*
 * Of course, my_work wants to be just a local in __pdflush().  It is
 * separated out in this manner to hopefully prevent the compiler from
 * performing unfortunate optimisations against the auto variables.  Because
 * these are visible to other tasks and CPUs.  (No problem has actually
 * been observed.  This is just paranoia).
 */
/**
 * pdflush进程主函数
 */
static int pdflush(void *dummy)
{
	struct pdflush_work my_work;
	cpumask_t cpus_allowed;

	/*
	 * pdflush can spend a lot of time doing encryption via dm-crypt.  We
	 * don't want to do that at keventd's priority.
	 */
	set_user_nice(current, 0);

	/*
	 * Some configs put our parent kthread in a limited cpuset,
	 * which kthread() overrides, forcing cpus_allowed == CPU_MASK_ALL.
	 * Our needs are more modest - cut back to our cpusets cpus_allowed.
	 * This is needed as pdflush's are dynamically created and destroyed.
	 * The boottime pdflush's are easily placed w/o these 2 lines.
	 */
	cpus_allowed = cpuset_cpus_allowed(current);
	set_cpus_allowed(current, cpus_allowed);

	return __pdflush(&my_work);
}

/*
 * Attempt to wake up a pdflush thread, and get it to do some work for you.
 * Returns zero if it indeed managed to find a worker thread, and passed your
 * payload to it.
 */
/**
 * 向pdflush分派工作
 */
int pdflush_operation(void (*fn)(unsigned long), unsigned long arg0)
{
	unsigned long flags;
	int ret = 0;

	BUG_ON(fn == NULL);	/* Hard to diagnose if it's deferred */

	spin_lock_irqsave(&pdflush_lock, flags);
	if (list_empty(&pdflush_list)) {/* 没有可唤醒的线程，退出 */
		spin_unlock_irqrestore(&pdflush_lock, flags);
		ret = -1;
	} else {
		struct pdflush_work *pdf;

		/* 将第一个线程从链表中移除 */
		pdf = list_entry(pdflush_list.next, struct pdflush_work, list);
		list_del_init(&pdf->list);
		if (list_empty(&pdflush_list))
			last_empty_jifs = jiffies;
		pdf->fn = fn;
		pdf->arg0 = arg0;
		/* 唤醒该线程 */
		wake_up_process(pdf->who);
		spin_unlock_irqrestore(&pdflush_lock, flags);
	}
	return ret;
}

/**
 * 创建运行一个pdflush线程。不同线程回写不同设备的页面。
 */
static void start_one_pdflush_thread(void)
{
	kthread_run(pdflush, NULL, "pdflush");
}

static int __init pdflush_init(void)
{
	int i;

	for (i = 0; i < MIN_PDFLUSH_THREADS; i++)
		start_one_pdflush_thread();
	return 0;
}

module_init(pdflush_init);
