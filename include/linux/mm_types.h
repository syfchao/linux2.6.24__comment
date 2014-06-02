#ifndef _LINUX_MM_TYPES_H
#define _LINUX_MM_TYPES_H

#include <linux/auxvec.h>
#include <linux/types.h>
#include <linux/threads.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/prio_tree.h>
#include <linux/rbtree.h>
#include <linux/rwsem.h>
#include <linux/completion.h>
#include <asm/page.h>
#include <asm/mmu.h>

#ifndef AT_VECTOR_SIZE_ARCH
#define AT_VECTOR_SIZE_ARCH 0
#endif
#define AT_VECTOR_SIZE (2*(AT_VECTOR_SIZE_ARCH + AT_VECTOR_SIZE_BASE + 1))

struct address_space;

#if NR_CPUS >= CONFIG_SPLIT_PTLOCK_CPUS
typedef atomic_long_t mm_counter_t;
#else  /* NR_CPUS < CONFIG_SPLIT_PTLOCK_CPUS */
typedef unsigned long mm_counter_t;
#endif /* NR_CPUS < CONFIG_SPLIT_PTLOCK_CPUS */

/*
 * Each physical page in the system has a struct page associated with
 * it to keep track of whatever it is we are using the page for at the
 * moment. Note that we have no way to track which tasks are using
 * a page, though if it is a pagecache page, rmap structures can tell us
 * who is mapping it.
 */
struct page {
	/* 体系结构无关的标志，用于描述页的属性。如PG_locked */
	unsigned long flags;		/* Atomic flags, some possibly
					 * updated asynchronously */
	/* 使用计数，表示内核中引用该页的次数 */
	atomic_t _count;		/* Usage count, see below. */
	union {
		/**
		 * 如果映射到用户态，则表示映射到pte的次数 
		 * 初始为-1，添加到反向映射时，值为0。
		 * 每增加一个使用者，计数器加1.这样内核可以快速的检查所有者之外有还哪些使用者。
		 */
		atomic_t _mapcount;	/* Count of ptes mapped in mms,
					 * to show when page is mapped
					 * & limit reverse map searches.
					 */
		/* 如果是内核slab使用的页，则表示其中的slab对象数目 */
		unsigned int inuse;	/* SLUB: Nr of objects */
	};
	union {
	    struct {
		/**
		 * 私有数据指针。一般用于数据缓冲区管理
		 * 如果用于页缓存，则指向第一个块缓冲
		 * 如果页位于交换缓存，则指向存储swp_entry_t结构
		 * 如果页面位于伙伴系统中，则是其迁移类型
		 */
		unsigned long private;		/* Mapping-private opaque data:
					 	 * usually used for buffer_heads
						 * if PagePrivate set; used for
						 * swp_entry_t if PageSwapCache;
						 * indicates order in the buddy
						 * system if PG_buddy is set.
						 */
		/**
		 * 页帧所在的地址空间 
		 * 如果最后一位为1，表示该指针指向一个anon_vma匿名区，用于逆向映射。
		 */
		struct address_space *mapping;	/* If low bit clear, points to
						 * inode address_space, or NULL.
						 * If page mapped as anonymous
						 * memory, low bit is set, and
						 * it points to anon_vma object:
						 * see PAGE_MAPPING_ANON below.
						 */
	    };
#if NR_CPUS >= CONFIG_SPLIT_PTLOCK_CPUS
	    spinlock_t ptl;
#endif
	    struct kmem_cache *slab;	/* SLUB: Pointer to slab */
		/* 如果与其他页组合成一个复合页，则指向复合页的第一个页 */
	    struct page *first_page;	/* Compound tail pages */
	};
	union {
		/* 该页在某个映射内的偏移量 */
		pgoff_t index;		/* Our offset within mapping. */
		void *freelist;		/* SLUB: freelist req. slab lock */
	};
	/* 用于页面换出的链表，可能链接到活动页链表和不活动页链表 */
	struct list_head lru;		/* Pageout list, eg. active_list
					 * protected by zone->lru_lock !
					 */
	/*
	 * On machines where all RAM is mapped into kernel address space,
	 * we can simply calculate the virtual address. On machines with
	 * highmem some memory is mapped into kernel virtual memory
	 * dynamically, so we need a place to store that address.
	 * Note that this field could be 16 bits on x86 ... ;)
	 *
	 * Architectures with slow multiplication can define
	 * WANT_PAGE_VIRTUAL in asm/page.h
	 */
#if defined(WANT_PAGE_VIRTUAL)
	/* kmap映射的内核虚拟地址，如果没有映射，则为NULL。只有个别运行较慢的体系结构使用了此字段 */
	void *virtual;			/* Kernel virtual address (NULL if
					   not kmapped, ie. highmem) */
#endif /* WANT_PAGE_VIRTUAL */
};

/*
 * This struct defines a memory VMM memory area. There is one of these
 * per VM-area/task.  A VM area is any part of the process virtual memory
 * space that has a special rule for the page-fault handlers (ie a shared
 * library, the executable area etc).
 */
/**
 * 进程虚拟地址空间中的一段区域
 */
struct vm_area_struct {
	/* 区域所属进程地址空间 */
	struct mm_struct * vm_mm;	/* The address space we belong to. */
	/* 区域的起始地址和结束地址 */
	unsigned long vm_start;		/* Our start address within vm_mm. */
	unsigned long vm_end;		/* The first byte after our end address
					   within vm_mm. */

	/* linked list of VM areas per task, sorted by address */
	/* 通过此指针将进程虚拟地址空间链接成单链表 */
	struct vm_area_struct *vm_next;

	/* 该区域的访问权限 */
	pgprot_t vm_page_prot;		/* Access permissions of this VMA. */
	/* 区域标志，如VM_GROWSDOWN */
	unsigned long vm_flags;		/* Flags, listed below. */

	/* 通过此字段将地址空间加入到红黑树中 */
	struct rb_node vm_rb;

	/*
	 * For areas with an address space and backing store,
	 * linkage into the address_space->i_mmap prio tree, or
	 * linkage to the list of like vmas hanging off its node, or
	 * linkage of vma in the address_space->i_mmap_nonlinear list.
	 */
	/* 用于反向映射、优先树管理的字段 */
	union {
		struct {
			/* 如果vma是非线性映射，那么通过此字段链入address_space.i_mmap_nonlinear */
			struct list_head list;
			void *parent;	/* aligns with prio_tree_node parent */
			struct vm_area_struct *head;
		} vm_set;

		/* 通过此字段将VMA加入到优先树中，优先树不处理非线性映射 */
		struct raw_prio_tree_node prio_tree_node;
	} shared;

	/*
	 * A file's MAP_PRIVATE vma can be in both i_mmap tree and anon_vma
	 * list, after a COW of one of the file pages.	A MAP_SHARED vma
	 * can only be in the i_mmap tree.  An anonymous MAP_PRIVATE, stack
	 * or brk vma (with NULL file) can only be in an anon_vma list.
	 */
	struct list_head anon_vma_node;	/* Serialized by anon_vma->lock */
	/* 如果VMA是匿名的，则通过此字段链接所有匿名页 */
	struct anon_vma *anon_vma;	/* Serialized by page_table_lock */

	/* Function pointers to deal with this struct. */
	/* 处理该结构的方法集合，用于在该结构上执行一些标准操作 */
	struct vm_operations_struct * vm_ops;

	/* Information about our backing store: */
	/* 映射到文件时，从文件的哪一部分开始映射 */
	unsigned long vm_pgoff;		/* Offset (within vm_file) in PAGE_SIZE
					   units, *not* PAGE_CACHE_SIZE */
	/* 映射的文件对象 */
	struct file * vm_file;		/* File we map to (can be NULL). */
	/* 私有数据 */
	void * vm_private_data;		/* was vm_pte (shared mem) */
	unsigned long vm_truncate_count;/* truncate_count or restart_addr */

#ifndef CONFIG_MMU
	atomic_t vm_usage;		/* refcount (VMAs shared if !MMU) */
#endif
#ifdef CONFIG_NUMA
	struct mempolicy *vm_policy;	/* NUMA policy for the VMA */
#endif
};

/**
 * 进程的内存管理描述符
 */
struct mm_struct {
	/* 进程虚拟地址空间，单链表的第一个结点 */
	struct vm_area_struct * mmap;		/* list of VMAs */
	/* 进程虚拟地址空间，红黑树形式 */
	struct rb_root mm_rb;
	/* 上一次find_vma调用查找到的区域 */
	struct vm_area_struct * mmap_cache;	/* last find_vma result */
	/**
	 * 为进程查找可用虚拟地址空间的回调函数
	 */
	unsigned long (*get_unmapped_area) (struct file *filp,
				unsigned long addr, unsigned long len,
				unsigned long pgoff, unsigned long flags);
	void (*unmap_area) (struct mm_struct *mm, unsigned long addr);
	/**
	 * 虚拟地址空间中用于内存映射的起始地址。可能是随机的。
	 */
	unsigned long mmap_base;		/* base of mmap area */
	/**
	 * 进程的地址空间长度。通常是TASK_SIZE。
	 * 但是在64位系统上支持32位应用时，有所不同。
	 */
	unsigned long task_size;		/* size of task vm space */
	unsigned long cached_hole_size; 	/* if non-zero, the largest hole below free_area_cache */
	unsigned long free_area_cache;		/* first hole of size cached_hole_size or larger */
	pgd_t * pgd;
	atomic_t mm_users;			/* How many users with user space? */
	atomic_t mm_count;			/* How many references to "struct mm_struct" (users count as 1) */
	int map_count;				/* number of VMAs */
	struct rw_semaphore mmap_sem;
	spinlock_t page_table_lock;		/* Protects page tables and some counters */

	struct list_head mmlist;		/* List of maybe swapped mm's.	These are globally strung
						 * together off init_mm.mmlist, and are protected
						 * by mmlist_lock
						 */

	/* Special counters, in some configurations protected by the
	 * page_table_lock, in other configurations by being atomic.
	 */
	mm_counter_t _file_rss;
	mm_counter_t _anon_rss;

	unsigned long hiwater_rss;	/* High-watermark of RSS usage */
	unsigned long hiwater_vm;	/* High-water virtual memory usage */

	unsigned long total_vm, locked_vm, shared_vm, exec_vm;
	unsigned long stack_vm, reserved_vm, def_flags, nr_ptes;
	/* 代码段、数据段的起始、结束地址 */
	unsigned long start_code, end_code, start_data, end_data;
	/* 堆的边界，栈的位置 */
	unsigned long start_brk, brk, start_stack;
	/* 程序的参数和环境变量，位于栈的高位 */
	unsigned long arg_start, arg_end, env_start, env_end;

	unsigned long saved_auxv[AT_VECTOR_SIZE]; /* for /proc/PID/auxv */

	cpumask_t cpu_vm_mask;

	/* Architecture-specific MM context */
	mm_context_t context;

	/* Swap token stuff */
	/*
	 * Last value of global fault stamp as seen by this process.
	 * In other words, this value gives an indication of how long
	 * it has been since this task got the token.
	 * Look at mm/thrash.c
	 */
	/* 上一次试图获取交换令牌时的global_faults值 */
	unsigned int faultstamp;
	/* 与交换令牌相关的优先级，控制交换令牌的访问 */
	unsigned int token_priority;
	/* 等待交换令牌的时间长度 */
	unsigned int last_interval;

	unsigned long flags; /* Must use atomic bitops to access the bits */

	/* coredumping support */
	int core_waiters;
	struct completion *core_startup_done, core_done;

	/* aio bits */
	rwlock_t		ioctx_list_lock;
	struct kioctx		*ioctx_list;
};

#endif /* _LINUX_MM_TYPES_H */
