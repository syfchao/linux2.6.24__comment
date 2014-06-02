#ifndef __LINUX_GFP_H
#define __LINUX_GFP_H

#include <linux/mmzone.h>
#include <linux/stddef.h>
#include <linux/linkage.h>

struct vm_area_struct;

/*
 * GFP bitmasks..
 *
 * Zone modifiers (see linux/mmzone.h - low three bits)
 *
 * Do not put any conditional on these. If necessary modify the definitions
 * without the underscores and use the consistently. The definitions here may
 * be used in bit comparisons.
 */
/**
 * 从哪些内存域中分配内存，分别表示从DMA，DMA32，HIGHMEM分配
 * GFP表示Get Free Pages
 */
#define __GFP_DMA	((__force gfp_t)0x01u)
#define __GFP_HIGHMEM	((__force gfp_t)0x02u)
#define __GFP_DMA32	((__force gfp_t)0x04u)

/*
 * Action modifiers - doesn't change the zoning
 *
 * __GFP_REPEAT: Try hard to allocate the memory, but the allocation attempt
 * _might_ fail.  This depends upon the particular VM implementation.
 *
 * __GFP_NOFAIL: The VM implementation _must_ retry infinitely: the caller
 * cannot handle allocation failures.
 *
 * __GFP_NORETRY: The VM implementation must not retry indefinitely.
 *
 * __GFP_MOVABLE: Flag that this page will be movable by the page migration
 * mechanism or reclaimed
 */
/**
 * 可以等待，也就是指进程可以睡眠
 */
#define __GFP_WAIT	((__force gfp_t)0x10u)	/* Can wait and reschedule? */
/**
 * 请求非常重要，内核迫切需要内存
 */
#define __GFP_HIGH	((__force gfp_t)0x20u)	/* Should access emergency pools? */
/**
 * 在申请内存时，可以进行IO操作，也就是可以让内核换出页
 */
#define __GFP_IO	((__force gfp_t)0x40u)	/* Can start physical IO? */
/**
 * 允许内核执行VFS操作。在与VFS有联系的内核子系统中需要禁用。
 */
#define __GFP_FS	((__force gfp_t)0x80u)	/* Can call down to low-level FS? */
/**
 * 需要分配冷页时使用。
 */
#define __GFP_COLD	((__force gfp_t)0x100u)	/* Cache-cold page required */
/**
 * 分配失败不能给出警告信息。内核中个别地方使用此标志
 */
#define __GFP_NOWARN	((__force gfp_t)0x200u)	/* Suppress page allocation failure warning */
/**
 * 分配失败后自动重试一定次数。即使分配的阶大于3也要重试
 */
#define __GFP_REPEAT	((__force gfp_t)0x400u)	/* Retry the allocation.  Might fail */
/**
 * 分配失败后一直重试。
 */
#define __GFP_NOFAIL	((__force gfp_t)0x800u)	/* Retry for ever.  Cannot fail */
/* 不允许重试 */
#define __GFP_NORETRY	((__force gfp_t)0x1000u)/* Do not retry.  Might fail */
#define __GFP_COMP	((__force gfp_t)0x4000u)/* Add compound page metadata */
/**
 * 将返回的页面进行0字节填充
 */
#define __GFP_ZERO	((__force gfp_t)0x8000u)/* Return zeroed page on success */
#define __GFP_NOMEMALLOC ((__force gfp_t)0x10000u) /* Don't use emergency reserves */
/**
 * 仅仅在NUMA系统中使用。
 * 在进程绑定的CPU节点上进行内存分配。如果进程可以在所有CPU上运行，则没有意义。
 */
#define __GFP_HARDWALL   ((__force gfp_t)0x20000u) /* Enforce hardwall cpuset memory allocs */
/**
 * 仅仅在当前节点上分配内存。而不要到其他节点的备用池上分配。
 */
#define __GFP_THISNODE	((__force gfp_t)0x40000u)/* No fallback, no policies */
/* 分配可回收的页，主要是页缓存 */
#define __GFP_RECLAIMABLE ((__force gfp_t)0x80000u) /* Page is reclaimable */
/* 分配可移动的页面，主要是用户态进程所需要的页 */
#define __GFP_MOVABLE	((__force gfp_t)0x100000u)  /* Page is movable */

#define __GFP_BITS_SHIFT 21	/* Room for 21 __GFP_FOO bits */
#define __GFP_BITS_MASK ((__force gfp_t)((1 << __GFP_BITS_SHIFT) - 1))

/* This equals 0, but use constants in case they ever change */
#define GFP_NOWAIT	(GFP_ATOMIC & ~__GFP_HIGH)
/* GFP_ATOMIC means both !wait (__GFP_WAIT not set) and use emergency pool */
/* 原子分配内存 */
#define GFP_ATOMIC	(__GFP_HIGH)
/* 禁用IO或VFS进行内存分配 */
#define GFP_NOIO	(__GFP_WAIT)
#define GFP_NOFS	(__GFP_WAIT | __GFP_IO)
/* 内核态内存分配的标准标志 */
#define GFP_KERNEL	(__GFP_WAIT | __GFP_IO | __GFP_FS)
#define GFP_TEMPORARY	(__GFP_WAIT | __GFP_IO | __GFP_FS | \
			 __GFP_RECLAIMABLE)
/* 用户态内存分配 */
#define GFP_USER	(__GFP_WAIT | __GFP_IO | __GFP_FS | __GFP_HARDWALL)
/* 用户态高端内存分配 */
#define GFP_HIGHUSER	(__GFP_WAIT | __GFP_IO | __GFP_FS | __GFP_HARDWALL | \
			 __GFP_HIGHMEM)
/* 用户态内存分配，从可移动区域分配 */
#define GFP_HIGHUSER_MOVABLE	(__GFP_WAIT | __GFP_IO | __GFP_FS | \
				 __GFP_HARDWALL | __GFP_HIGHMEM | \
				 __GFP_MOVABLE)
#define GFP_NOFS_PAGECACHE	(__GFP_WAIT | __GFP_IO | __GFP_MOVABLE)
#define GFP_USER_PAGECACHE	(__GFP_WAIT | __GFP_IO | __GFP_FS | \
				 __GFP_HARDWALL | __GFP_MOVABLE)
#define GFP_HIGHUSER_PAGECACHE	(__GFP_WAIT | __GFP_IO | __GFP_FS | \
				 __GFP_HARDWALL | __GFP_HIGHMEM | \
				 __GFP_MOVABLE)

#ifdef CONFIG_NUMA
#define GFP_THISNODE	(__GFP_THISNODE | __GFP_NOWARN | __GFP_NORETRY)
#else
#define GFP_THISNODE	((__force gfp_t)0)
#endif

/* This mask makes up all the page movable related flags */
#define GFP_MOVABLE_MASK (__GFP_RECLAIMABLE|__GFP_MOVABLE)

/* Control page allocator reclaim behavior */
#define GFP_RECLAIM_MASK (__GFP_WAIT|__GFP_HIGH|__GFP_IO|__GFP_FS|\
			__GFP_NOWARN|__GFP_REPEAT|__GFP_NOFAIL|\
			__GFP_NORETRY|__GFP_NOMEMALLOC)

/* Control allocation constraints */
#define GFP_CONSTRAINT_MASK (__GFP_HARDWALL|__GFP_THISNODE)

/* Do not use these with a slab allocator */
#define GFP_SLAB_BUG_MASK (__GFP_DMA32|__GFP_HIGHMEM|~__GFP_BITS_MASK)

/* Flag - indicates that the buffer will be suitable for DMA.  Ignored on some
   platforms, used as appropriate on others */

/* 从DMA中分配 */
#define GFP_DMA		__GFP_DMA

/* 4GB DMA on some platforms */
#define GFP_DMA32	__GFP_DMA32

/* Convert GFP flags to their corresponding migrate type */
/* 根据页面分配标志确定其迁移类型 */
static inline int allocflags_to_migratetype(gfp_t gfp_flags)
{
	WARN_ON((gfp_flags & GFP_MOVABLE_MASK) == GFP_MOVABLE_MASK);

	/* 没有启用页面迁移功能 */
	if (unlikely(page_group_by_mobility_disabled))
		return MIGRATE_UNMOVABLE;

	/* Group based on mobility */
	/* 根据标志判断迁移类型 */
	return (((gfp_flags & __GFP_MOVABLE) != 0) << 1) |
		((gfp_flags & __GFP_RECLAIMABLE) != 0);
}

/**
 * 根据分配标志来确定，应该从哪一个内存域开始分配内存
 */
static inline enum zone_type gfp_zone(gfp_t flags)
{
	int base = 0;

#ifdef CONFIG_NUMA
	if (flags & __GFP_THISNODE)/* ?? */
		base = MAX_NR_ZONES;
#endif

#ifdef CONFIG_ZONE_DMA
	if (flags & __GFP_DMA)/* 指定DMA标志，只能从DMA分配 */
		return base + ZONE_DMA;
#endif
#ifdef CONFIG_ZONE_DMA32
	if (flags & __GFP_DMA32)/* 指定DMA标志，从DMA32开始分配 */
		return base + ZONE_DMA32;
#endif
	if ((flags & (__GFP_HIGHMEM | __GFP_MOVABLE)) ==
			(__GFP_HIGHMEM | __GFP_MOVABLE))/* 用户态请求分配内存，优先从ZONE_MOVABLE分配 */
		return base + ZONE_MOVABLE;
#ifdef CONFIG_HIGHMEM
	if (flags & __GFP_HIGHMEM)/* 从高端内存开始分配 */
		return base + ZONE_HIGHMEM;
#endif
	return base + ZONE_NORMAL;/* 没有特别指定内存域标志，表示是普通的内核请求，从NORMAL分配 */
}

static inline gfp_t set_migrateflags(gfp_t gfp, gfp_t migrate_flags)
{
	BUG_ON((gfp & GFP_MOVABLE_MASK) == GFP_MOVABLE_MASK);
	return (gfp & ~(GFP_MOVABLE_MASK)) | migrate_flags;
}

/*
 * There is only one page-allocator function, and two main namespaces to
 * it. The alloc_page*() variants return 'struct page *' and as such
 * can allocate highmem pages, the *get*page*() variants return
 * virtual kernel addresses to the allocated page(s).
 */

/*
 * We get the zone list from the current node and the gfp_mask.
 * This zone list contains a maximum of MAXNODES*MAX_NR_ZONES zones.
 *
 * For the normal case of non-DISCONTIGMEM systems the NODE_DATA() gets
 * optimized to &contig_page_data at compile-time.
 */

#ifndef HAVE_ARCH_FREE_PAGE
static inline void arch_free_page(struct page *page, int order) { }
#endif
#ifndef HAVE_ARCH_ALLOC_PAGE
static inline void arch_alloc_page(struct page *page, int order) { }
#endif

extern struct page *
FASTCALL(__alloc_pages(gfp_t, unsigned int, struct zonelist *));

/**
 * 分配页面的主函数
 */
static inline struct page *alloc_pages_node(int nid, gfp_t gfp_mask,
						unsigned int order)
{
	if (unlikely(order >= MAX_ORDER))/* 分配过大的页面块，失败 */
		return NULL;

	/* Unknown node is current node */
	if (nid < 0)/* 未指定特定的NUMA节点，则使用当前节点 */
		nid = numa_node_id();

	/**
	 * 实际工作由__alloc_pages完成。
	 * gfp_zone确定分配标志对应的内存域，从此内存域开始分配内存。
	 */
	return __alloc_pages(gfp_mask, order,
		NODE_DATA(nid)->node_zonelists + gfp_zone(gfp_mask));
}

#ifdef CONFIG_NUMA
extern struct page *alloc_pages_current(gfp_t gfp_mask, unsigned order);

/**
 * 分配2^order个连续页面，并返回首页
 */
static inline struct page *
alloc_pages(gfp_t gfp_mask, unsigned int order)
{
	if (unlikely(order >= MAX_ORDER))
		return NULL;

	return alloc_pages_current(gfp_mask, order);
}
extern struct page *alloc_page_vma(gfp_t gfp_mask,
			struct vm_area_struct *vma, unsigned long addr);
#else
#define alloc_pages(gfp_mask, order) \
		alloc_pages_node(numa_node_id(), gfp_mask, order)
#define alloc_page_vma(gfp_mask, vma, addr) alloc_pages(gfp_mask, 0)
#endif
/* 分配一个页面 */
#define alloc_page(gfp_mask) alloc_pages(gfp_mask, 0)

extern unsigned long FASTCALL(__get_free_pages(gfp_t gfp_mask, unsigned int order));
extern unsigned long FASTCALL(get_zeroed_page(gfp_t gfp_mask));

/**
 * 在线性映射区中，分配连续的页面并返回其首地址
 * 该函数不能用于分配高端内存
 */
#define __get_free_page(gfp_mask) \
		__get_free_pages((gfp_mask),0)

/**
 * 从DMA区域中分配连续的页面块并返回其首地址
 */
#define __get_dma_pages(gfp_mask, order) \
		__get_free_pages((gfp_mask) | GFP_DMA,(order))

extern void FASTCALL(__free_pages(struct page *page, unsigned int order));
extern void FASTCALL(free_pages(unsigned long addr, unsigned int order));
extern void FASTCALL(free_hot_page(struct page *page));
extern void FASTCALL(free_cold_page(struct page *page));

/**
 * 释放一个内存页，参数是内存页结构
 */
#define __free_page(page) __free_pages((page), 0)
/**
 * 释放一个内存页，参数是内存虚拟地址
 */
#define free_page(addr) free_pages((addr),0)

void page_alloc_init(void);
void drain_zone_pages(struct zone *zone, struct per_cpu_pages *pcp);

#endif /* __LINUX_GFP_H */
