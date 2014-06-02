#ifndef _LINUX_VMALLOC_H
#define _LINUX_VMALLOC_H

#include <linux/spinlock.h>
#include <asm/page.h>		/* pgprot_t */

struct vm_area_struct;

/* bits in vm_struct->flags */
/* 这段区域是由ioremap产生的 */
#define VM_IOREMAP	0x00000001	/* ioremap() and friends */
/* 这段区域是由vmalloc产生的 */
#define VM_ALLOC	0x00000002	/* vmalloc() */
/* 这段区域是由vmap产生的，与vmalloc的区别在于，其物理页由调用者管理 */
#define VM_MAP		0x00000004	/* vmap()ed pages */
#define VM_USERMAP	0x00000008	/* suitable for remap_vmalloc_range */
/* 这段区域用于vmalloc内部使用，例如，用于保存vmalloc页面指针 */
#define VM_VPAGES	0x00000010	/* buffer for pages was vmalloc'ed */
/* bits [20..32] reserved for arch specific ioremap internals */

/*
 * Maximum alignment for ioremap() regions.
 * Can be overriden by arch-specific value.
 */
#ifndef IOREMAP_MAX_ORDER
#define IOREMAP_MAX_ORDER	(7 + PAGE_SHIFT)	/* 128 pages */
#endif

/**
 * 管理vmalloc虚拟地址空间的数据结构。代表了一次vmalloc分配的虚拟地址空间。
 */
struct vm_struct {
	/* keep next,addr,size together to speedup lookups */
	/* 用于将vm_struct与下一个vm_struct结构连接起来 */
	struct vm_struct	*next;
	/* 起始地址 */
	void			*addr;
	/* 长度 */
	unsigned long		size;
	/* 内存区域标志，如VM_ALLOC */
	unsigned long		flags;
	/* 这是一个指向page指针的数组。数组每一项表示映射到虚拟址空间的物理内存页 */
	struct page		**pages;
	/* pages数组中的数目 */
	unsigned int		nr_pages;
	/* ioremap映射的物理地址 */
	unsigned long		phys_addr;
};

/*
 *	Highlevel APIs for driver use
 */
extern void *vmalloc(unsigned long size);
extern void *vmalloc_user(unsigned long size);
extern void *vmalloc_node(unsigned long size, int node);
extern void *vmalloc_exec(unsigned long size);
extern void *vmalloc_32(unsigned long size);
extern void *vmalloc_32_user(unsigned long size);
extern void *__vmalloc(unsigned long size, gfp_t gfp_mask, pgprot_t prot);
extern void *__vmalloc_area(struct vm_struct *area, gfp_t gfp_mask,
				pgprot_t prot);
extern void vfree(void *addr);

extern void *vmap(struct page **pages, unsigned int count,
			unsigned long flags, pgprot_t prot);
extern void vunmap(void *addr);

extern int remap_vmalloc_range(struct vm_area_struct *vma, void *addr,
							unsigned long pgoff);
void vmalloc_sync_all(void);
 
/*
 *	Lowlevel-APIs (not for driver use!)
 */

static inline size_t get_vm_area_size(const struct vm_struct *area)
{
	/* return actual size without guard page */
	return area->size - PAGE_SIZE;
}

extern struct vm_struct *get_vm_area(unsigned long size, unsigned long flags);
extern struct vm_struct *__get_vm_area(unsigned long size, unsigned long flags,
					unsigned long start, unsigned long end);
extern struct vm_struct *get_vm_area_node(unsigned long size,
					  unsigned long flags, int node,
					  gfp_t gfp_mask);
extern struct vm_struct *remove_vm_area(void *addr);

extern int map_vm_area(struct vm_struct *area, pgprot_t prot,
			struct page ***pages);
extern void unmap_kernel_range(unsigned long addr, unsigned long size);

/* Allocate/destroy a 'vmalloc' VM area. */
extern struct vm_struct *alloc_vm_area(size_t size);
extern void free_vm_area(struct vm_struct *area);

/*
 *	Internals.  Dont't use..
 */
extern rwlock_t vmlist_lock;
extern struct vm_struct *vmlist;

#endif /* _LINUX_VMALLOC_H */
