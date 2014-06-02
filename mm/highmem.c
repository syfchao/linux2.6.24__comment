/*
 * High memory handling common code and variables.
 *
 * (C) 1999 Andrea Arcangeli, SuSE GmbH, andrea@suse.de
 *          Gerhard Wichert, Siemens AG, Gerhard.Wichert@pdb.siemens.de
 *
 *
 * Redesigned the x86 32-bit VM architecture to deal with
 * 64-bit physical space. With current x86 CPUs this
 * means up to 64 Gigabytes physical RAM.
 *
 * Rewrote high memory support to move the page cache into
 * high memory. Implemented permanent (schedulable) kmaps
 * based on Linus' idea.
 *
 * Copyright (C) 1999 Ingo Molnar <mingo@redhat.com>
 */

#include <linux/mm.h>
#include <linux/module.h>
#include <linux/swap.h>
#include <linux/bio.h>
#include <linux/pagemap.h>
#include <linux/mempool.h>
#include <linux/blkdev.h>
#include <linux/init.h>
#include <linux/hash.h>
#include <linux/highmem.h>
#include <linux/blktrace_api.h>
#include <asm/tlbflush.h>

/*
 * Virtual_count is not a pure "count".
 *  0 means that it is not mapped, and has not been mapped
 *    since a TLB flush - it is usable.
 *  1 means that there are no users, but it has been mapped
 *    since the last TLB flush - so we can't use it.
 *  n means that there are (n-1) current users of it.
 */
#ifdef CONFIG_HIGHMEM

unsigned long totalhigh_pages __read_mostly;

unsigned int nr_free_highpages (void)
{
	pg_data_t *pgdat;
	unsigned int pages = 0;

	for_each_online_pgdat(pgdat) {
		pages += zone_page_state(&pgdat->node_zones[ZONE_HIGHMEM],
			NR_FREE_PAGES);
		if (zone_movable_is_highmem())
			pages += zone_page_state(
					&pgdat->node_zones[ZONE_MOVABLE],
					NR_FREE_PAGES);
	}

	return pages;
}

/**
 * 该数组的每一个元素对应于一个持久映射的kmap页
 * 表示被映射页的使用计数。
 * 当计数值为2时，表示有一处使用了该页。0表示没有使用。1表示页面已经映射，但是TLB没有更新，因此无法使用。
 */
static int pkmap_count[LAST_PKMAP];
static unsigned int last_pkmap_nr;
static  __cacheline_aligned_in_smp DEFINE_SPINLOCK(kmap_lock);

pte_t * pkmap_page_table;

static DECLARE_WAIT_QUEUE_HEAD(pkmap_map_wait);

static void flush_all_zero_pkmaps(void)
{
	int i;

	flush_cache_kmaps();

	for (i = 0; i < LAST_PKMAP; i++) {
		struct page *page;

		/*
		 * zero means we don't have anything to do,
		 * >1 means that it is still in use. Only
		 * a count of 1 means that it is free but
		 * needs to be unmapped
		 */
		if (pkmap_count[i] != 1)
			continue;
		pkmap_count[i] = 0;

		/* sanity check */
		BUG_ON(pte_none(pkmap_page_table[i]));

		/*
		 * Don't need an atomic fetch-and-clear op here;
		 * no-one has the page mapped, and cannot get at
		 * its virtual address (and hence PTE) without first
		 * getting the kmap_lock (which is held here).
		 * So no dangers, even with speculative execution.
		 */
		page = pte_page(pkmap_page_table[i]);
		pte_clear(&init_mm, (unsigned long)page_address(page),
			  &pkmap_page_table[i]);

		set_page_address(page, NULL);
	}
	flush_tlb_kernel_range(PKMAP_ADDR(0), PKMAP_ADDR(LAST_PKMAP));
}

/* Flush all unused kmap mappings in order to remove stray
   mappings. */
void kmap_flush_unused(void)
{
	spin_lock(&kmap_lock);
	flush_all_zero_pkmaps();
	spin_unlock(&kmap_lock);
}

static inline unsigned long map_new_virtual(struct page *page)
{
	unsigned long vaddr;
	int count;

start:
	count = LAST_PKMAP;/* 最多循环遍历一次pkmap_count数组 */
	/* Find an empty entry */
	for (;;) {
		/* 从上一次查找的位置开始查找空闲虚拟地址 */
		last_pkmap_nr = (last_pkmap_nr + 1) & LAST_PKMAP_MASK;
		if (!last_pkmap_nr) {/* 需要从数组元素0开始遍历了 */
			/* 将所有计数为1的地址，清除其pte映射，刷新tlb。延迟刷新tlb，因为刷新tlb是耗时的操作 */
			flush_all_zero_pkmaps();
			/* flush_all_zero_pkmaps修改了计数，需要完全重新查找可用地址 */
			count = LAST_PKMAP;
		}
		/* 找到可用地址 */
		if (!pkmap_count[last_pkmap_nr])
			break;	/* Found a usable entry */
		if (--count)/* 当前节点已经使用，继续查找下一个 */
			continue;

		/*
		 * Sleep for somebody else to unmap their entries
		 */
		/* 运行到这里，说明没有可用虚拟地址，必须等待其他地方调用kunmap释放虚拟地址 */
		{
			DECLARE_WAITQUEUE(wait, current);

			/* 将自己挂到pkmap_map_wait等待队列 */
			__set_current_state(TASK_UNINTERRUPTIBLE);
			add_wait_queue(&pkmap_map_wait, &wait);
			/* 释放全局锁并睡眠。该锁由调用者获取 */
			spin_unlock(&kmap_lock);
			schedule();
			/* 其他地方调用了kunmap，重新获取全局锁并重试 */
			remove_wait_queue(&pkmap_map_wait, &wait);
			spin_lock(&kmap_lock);

			/* Somebody else might have mapped it while we slept */
			/* 在睡眠的过程中，其他地方可能已经重新映射了该页，直接访问即可 */
			if (page_address(page))
				return (unsigned long)page_address(page);

			/* Re-start */
			/* 重新查找可用虚拟地址 */
			goto start;
		}
	}
	/* 计算获得的虚拟地址 */
	vaddr = PKMAP_ADDR(last_pkmap_nr);
	/* 修改pte映射项 */
	set_pte_at(&init_mm, vaddr,
		   &(pkmap_page_table[last_pkmap_nr]), mk_pte(page, kmap_prot));

	/* 这里将使用计数初始化为1，调用者会再增加计数为2，表示有一个使用计数 */
	pkmap_count[last_pkmap_nr] = 1;
	set_page_address(page, (void *)vaddr);

	return vaddr;
}

/**
 * 将高端内存映射到虚拟地址
 */
void fastcall *kmap_high(struct page *page)
{
	unsigned long vaddr;

	/*
	 * For highmem pages, we can't trust "virtual" until
	 * after we have the lock.
	 *
	 * We cannot call this from interrupts, as it may block
	 */
	spin_lock(&kmap_lock);/* 这里必须获得这个全局锁，才能确信page_address是正确的 */
	vaddr = (unsigned long)page_address(page);
	if (!vaddr)/* 还没映射到高端地址 */
		/* 获得虚拟地址并映射到页面，注意这里会释放锁，并阻塞，因此本函数不能在中断中调用 */
		vaddr = map_new_virtual(page);
	/* 增加虚拟地址使用计数，在map_new_virtual中设置了初始值为1，此时应该为2或者更大的值 */
	pkmap_count[PKMAP_NR(vaddr)]++;
	BUG_ON(pkmap_count[PKMAP_NR(vaddr)] < 2);
	/* 释放全局锁，并返回地址 */
	spin_unlock(&kmap_lock);
	return (void*) vaddr;
}

EXPORT_SYMBOL(kmap_high);

void fastcall kunmap_high(struct page *page)
{
	unsigned long vaddr;
	unsigned long nr;
	int need_wakeup;

	/* 获取全局kmap锁 */
	spin_lock(&kmap_lock);
	/* 查找页面的虚拟地址 */
	vaddr = (unsigned long)page_address(page);
	BUG_ON(!vaddr);/* 如果页面没有被映射过，说明调用者遇到异常情况 */
	nr = PKMAP_NR(vaddr);/* 计算该地址在kmap虚拟空间中的索引 */

	/*
	 * A count must never go down to zero
	 * without a TLB flush!
	 */
	need_wakeup = 0;
	switch (--pkmap_count[nr]) {/* 递减虚拟地址引用计数 */
	case 0:/* 永远不可能为0，为1才表示没有映射 */
		BUG();
	case 1:/* 完全解除映射了 */
		/*
		 * Avoid an unnecessary wake_up() function call.
		 * The common case is pkmap_count[] == 1, but
		 * no waiters.
		 * The tasks queued in the wait-queue are guarded
		 * by both the lock in the wait-queue-head and by
		 * the kmap_lock.  As the kmap_lock is held here,
		 * no need for the wait-queue-head's lock.  Simply
		 * test if the queue is empty.
		 */
		/* 如果有等待虚拟地址的进程，则需要唤醒 */
		need_wakeup = waitqueue_active(&pkmap_map_wait);
	}
	/* 释放全局锁 */
	spin_unlock(&kmap_lock);

	/* do wake-up, if needed, race-free outside of the spin lock */
	if (need_wakeup)/* 释放锁以后再唤醒，避免长时间获得锁 */
		wake_up(&pkmap_map_wait);
}

EXPORT_SYMBOL(kunmap_high);
#endif

#if defined(HASHED_PAGE_VIRTUAL)

#define PA_HASH_ORDER	7

/*
 * Describes one page->virtual association
 */
/**
 * 描述物理内存页与虚拟地址之间的关联
 */
struct page_address_map {
	/* 页面对象 */
	struct page *page;
	/* 映射的虚拟地址 */
	void *virtual;
	/* 通过此字段链接到哈希表page_address_htable的桶中 */
	struct list_head list;
};

/*
 * page_address_map freelist, allocated from page_address_maps.
 */
static struct list_head page_address_pool;	/* freelist */
static spinlock_t pool_lock;			/* protects page_address_pool */

/*
 * Hash table bucket
 */
/**
 * page_address_htable哈希表，用于防止虚拟地址冲突
 * 其散列函数是page_slot
 */
static struct page_address_slot {
	struct list_head lh;			/* List of page_address_maps */
	spinlock_t lock;			/* Protect this bucket's list */
} ____cacheline_aligned_in_smp page_address_htable[1<<PA_HASH_ORDER];

static struct page_address_slot *page_slot(struct page *page)
{
	return &page_address_htable[hash_ptr(page, PA_HASH_ORDER)];
}

/**
 * 确定某个物理页面的虚拟地址。
 * 可能在page_address_htable哈希表中查找
 */
void *page_address(struct page *page)
{
	unsigned long flags;
	void *ret;
	struct page_address_slot *pas;

	if (!PageHighMem(page))/* 如果不是高端内存，则直接返回其线性地址 */
		return lowmem_page_address(page);

	/* 计算在page_address_htable中的位置 */
	pas = page_slot(page);
	ret = NULL;
	/* 获取桶的锁 */
	spin_lock_irqsave(&pas->lock, flags);
	if (!list_empty(&pas->lh)) {/* 桶不为空 */
		struct page_address_map *pam;

		list_for_each_entry(pam, &pas->lh, list) {/* 在桶中遍历 */
			if (pam->page == page) {/* 找到该页，并返回其地址 */
				ret = pam->virtual;
				goto done;
			}
		}
	}
done:
	/* 释放锁并返回结果 */
	spin_unlock_irqrestore(&pas->lock, flags);
	return ret;
}

EXPORT_SYMBOL(page_address);

void set_page_address(struct page *page, void *virtual)
{
	unsigned long flags;
	struct page_address_slot *pas;
	struct page_address_map *pam;

	BUG_ON(!PageHighMem(page));

	pas = page_slot(page);
	if (virtual) {		/* Add */
		BUG_ON(list_empty(&page_address_pool));

		spin_lock_irqsave(&pool_lock, flags);
		pam = list_entry(page_address_pool.next,
				struct page_address_map, list);
		list_del(&pam->list);
		spin_unlock_irqrestore(&pool_lock, flags);

		pam->page = page;
		pam->virtual = virtual;

		spin_lock_irqsave(&pas->lock, flags);
		list_add_tail(&pam->list, &pas->lh);
		spin_unlock_irqrestore(&pas->lock, flags);
	} else {		/* Remove */
		spin_lock_irqsave(&pas->lock, flags);
		list_for_each_entry(pam, &pas->lh, list) {
			if (pam->page == page) {
				list_del(&pam->list);
				spin_unlock_irqrestore(&pas->lock, flags);
				spin_lock_irqsave(&pool_lock, flags);
				list_add_tail(&pam->list, &page_address_pool);
				spin_unlock_irqrestore(&pool_lock, flags);
				goto done;
			}
		}
		spin_unlock_irqrestore(&pas->lock, flags);
	}
done:
	return;
}

static struct page_address_map page_address_maps[LAST_PKMAP];

void __init page_address_init(void)
{
	int i;

	INIT_LIST_HEAD(&page_address_pool);
	for (i = 0; i < ARRAY_SIZE(page_address_maps); i++)
		list_add(&page_address_maps[i].list, &page_address_pool);
	for (i = 0; i < ARRAY_SIZE(page_address_htable); i++) {
		INIT_LIST_HEAD(&page_address_htable[i].lh);
		spin_lock_init(&page_address_htable[i].lock);
	}
	spin_lock_init(&pool_lock);
}

#endif	/* defined(CONFIG_HIGHMEM) && !defined(WANT_PAGE_VIRTUAL) */
