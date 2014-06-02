#ifndef _LINUX_SLAB_DEF_H
#define	_LINUX_SLAB_DEF_H

/*
 * Definitions unique to the original Linux SLAB allocator.
 *
 * What we provide here is a way to optimize the frequent kmalloc
 * calls in the kernel by selecting the appropriate general cache
 * if kmalloc was called with a size that can be established at
 * compile time.
 */

#include <linux/init.h>
#include <asm/page.h>		/* kmalloc_sizes.h needs PAGE_SIZE */
#include <asm/cache.h>		/* kmalloc_sizes.h needs L1_CACHE_BYTES */
#include <linux/compiler.h>

/* Size description struct for general caches. */
/**
 * 用于kmalloc，描述每一个可用于kmalloc的kmem_cache
 */
struct cache_sizes {
	/* 缓存长度 */
	size_t		 	cs_size;
	/* 用于kmalloc的缓存 */
	struct kmem_cache	*cs_cachep;
#ifdef CONFIG_ZONE_DMA
	/* 用于分配DMA时的缓存 */
	struct kmem_cache	*cs_dmacachep;
#endif
};
extern struct cache_sizes malloc_sizes[];

void *kmem_cache_alloc(struct kmem_cache *, gfp_t);
void *__kmalloc(size_t size, gfp_t flags);

/**
 * 分配长度为size的一段内存，并返回其首地址。
 * flags是其分配标志，如GFP_KERNEL
 */
static inline void *kmalloc(size_t size, gfp_t flags)
{
	if (__builtin_constant_p(size)) {/* 长度是常量 */
		int i = 0;

		if (!size)/* 长度为0， */
			return ZERO_SIZE_PTR;

#define CACHE(x) \
		if (size <= x) \
			goto found; \
		else \
			i++;
#include "kmalloc_sizes.h"/* 这里根据size找到合适的kmem_cache */
#undef CACHE
		{/* 没找到，编译器提示警告 */
			extern void __you_cannot_kmalloc_that_much(void);
			__you_cannot_kmalloc_that_much();
		}
found:
#ifdef CONFIG_ZONE_DMA
		if (flags & GFP_DMA)/* 从dma中分配 */
			return kmem_cache_alloc(malloc_sizes[i].cs_dmacachep,
						flags);
#endif
		/* 从正常区域中分配 */
		return kmem_cache_alloc(malloc_sizes[i].cs_cachep, flags);
	}
	/* 长度不是常量，调用__kmalloc进行分配 */
	return __kmalloc(size, flags);
}

#ifdef CONFIG_NUMA
extern void *__kmalloc_node(size_t size, gfp_t flags, int node);
extern void *kmem_cache_alloc_node(struct kmem_cache *, gfp_t flags, int node);

static inline void *kmalloc_node(size_t size, gfp_t flags, int node)
{
	if (__builtin_constant_p(size)) {
		int i = 0;

		if (!size)
			return ZERO_SIZE_PTR;

#define CACHE(x) \
		if (size <= x) \
			goto found; \
		else \
			i++;
#include "kmalloc_sizes.h"
#undef CACHE
		{
			extern void __you_cannot_kmalloc_that_much(void);
			__you_cannot_kmalloc_that_much();
		}
found:
#ifdef CONFIG_ZONE_DMA
		if (flags & GFP_DMA)
			return kmem_cache_alloc_node(malloc_sizes[i].cs_dmacachep,
						flags, node);
#endif
		return kmem_cache_alloc_node(malloc_sizes[i].cs_cachep,
						flags, node);
	}
	return __kmalloc_node(size, flags, node);
}

#endif	/* CONFIG_NUMA */

#endif	/* _LINUX_SLAB_DEF_H */
