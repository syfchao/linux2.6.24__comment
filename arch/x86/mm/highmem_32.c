#include <linux/highmem.h>
#include <linux/module.h>

/**
 * 在启用了高端内存的情况下，使用此函数将高端内存映射到内核中
 */
void *kmap(struct page *page)
{
	might_sleep();
	if (!PageHighMem(page))/* 页面属于低端地址，内核可以直接访问 */
		return page_address(page);/* 返回页面的线性映射地址 */
	/* 将高端内存映射到内核地址空间中 */
	return kmap_high(page);
}

/**
 * 解除由kmap建立的映射
 */
void kunmap(struct page *page)
{
	if (in_interrupt())/* 不可能在中断中调用kmap，也不可能在中断调用kunmap */
		BUG();
	if (!PageHighMem(page))/* 如果不是高端内存，则直接退出 */
		return;
	/* 解除高端内存的kmap映射 */
	kunmap_high(page);
}

/*
 * kmap_atomic/kunmap_atomic is significantly faster than kmap/kunmap because
 * no global lock is needed and because the kmap code must perform a global TLB
 * invalidation when the kmap pool wraps.
 *
 * However when holding an atomic kmap is is not legal to sleep, so atomic
 * kmaps are appropriate for short, tight code paths only.
 */
void *kmap_atomic_prot(struct page *page, enum km_type type, pgprot_t prot)
{
	enum fixed_addresses idx;
	unsigned long vaddr;

	/* even !CONFIG_PREEMPT needs this, for in_atomic in do_page_fault */
	pagefault_disable();

	if (!PageHighMem(page))/* 如果不是高端内存，直接返回其线性地址即可 */
		return page_address(page);

	/* 计算该项在全局数组中的位置，与当前CPU有关 */
	idx = type + KM_TYPE_NR*smp_processor_id();
	/* 计算虚拟地址 */
	vaddr = __fix_to_virt(FIX_KMAP_BEGIN + idx);
	/* 相应的pte应当是没有映射的 */
	BUG_ON(!pte_none(*(kmap_pte-idx)));
	/* 将页面映射到虚拟地址 */
	set_pte(kmap_pte-idx, mk_pte(page, prot));
	arch_flush_lazy_mmu_mode();

	return (void *)vaddr;
}

/**
 * 原子的kmap映射一个页面
 * 仅仅用于中断处理函数中
 */
void *kmap_atomic(struct page *page, enum km_type type)
{
	return kmap_atomic_prot(page, type, kmap_prot);
}

/* 解除由kmap_atomic建立的映射 */
void kunmap_atomic(void *kvaddr, enum km_type type)
{
	unsigned long vaddr = (unsigned long) kvaddr & PAGE_MASK;
	/* 计算在kmap数组中的索引 */
	enum fixed_addresses idx = type + KM_TYPE_NR*smp_processor_id();

	/*
	 * Force other mappings to Oops if they'll try to access this pte
	 * without first remap it.  Keeping stale mappings around is a bad idea
	 * also, in case the page changes cacheability attributes or becomes
	 * a protected page in a hypervisor.
	 */
	if (vaddr == __fix_to_virt(FIX_KMAP_BEGIN+idx))
		kpte_clear_flush(kmap_pte-idx, vaddr);/* 清除pte映射 */
	else {
#ifdef CONFIG_DEBUG_HIGHMEM
		BUG_ON(vaddr < PAGE_OFFSET);
		BUG_ON(vaddr >= (unsigned long)high_memory);
#endif
	}

	arch_flush_lazy_mmu_mode();
	pagefault_enable();
}

/* This is the same as kmap_atomic() but can map memory that doesn't
 * have a struct page associated with it.
 */
void *kmap_atomic_pfn(unsigned long pfn, enum km_type type)
{
	enum fixed_addresses idx;
	unsigned long vaddr;

	pagefault_disable();

	idx = type + KM_TYPE_NR*smp_processor_id();
	vaddr = __fix_to_virt(FIX_KMAP_BEGIN + idx);
	set_pte(kmap_pte-idx, pfn_pte(pfn, kmap_prot));
	arch_flush_lazy_mmu_mode();

	return (void*) vaddr;
}

struct page *kmap_atomic_to_page(void *ptr)
{
	unsigned long idx, vaddr = (unsigned long)ptr;
	pte_t *pte;

	if (vaddr < FIXADDR_START)
		return virt_to_page(ptr);

	idx = virt_to_fix(vaddr);
	pte = kmap_pte - (idx - FIX_KMAP_BEGIN);
	return pte_page(*pte);
}

EXPORT_SYMBOL(kmap);
EXPORT_SYMBOL(kunmap);
EXPORT_SYMBOL(kmap_atomic);
EXPORT_SYMBOL(kunmap_atomic);
EXPORT_SYMBOL(kmap_atomic_to_page);
