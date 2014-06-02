/*
 *  linux/arch/i386/mm/mmap.c
 *
 *  flexible mmap layout support
 *
 * Copyright 2003-2004 Red Hat Inc., Durham, North Carolina.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *
 * Started by Ingo Molnar <mingo@elte.hu>
 */

#include <linux/personality.h>
#include <linux/mm.h>
#include <linux/random.h>
#include <linux/sched.h>

/*
 * Top of mmap area (just below the process stack).
 *
 * Leave an at least ~128 MB hole.
 */
#define MIN_GAP (128*1024*1024)
#define MAX_GAP (TASK_SIZE/6*5)

static inline unsigned long mmap_base(struct mm_struct *mm)
{
	unsigned long gap = current->signal->rlim[RLIMIT_STACK].rlim_cur;
	unsigned long random_factor = 0;

	if (current->flags & PF_RANDOMIZE)/* 启动了地址空间随机化功能 */
		/* 得到随机因子 */
		random_factor = get_random_int() % (1024*1024);

	/* 确保堆栈至少占用128M */
	if (gap < MIN_GAP)
		gap = MIN_GAP;
	else if (gap > MAX_GAP)/* 堆至少占用1/6的虚拟地址空间 */
		gap = MAX_GAP;

	return PAGE_ALIGN(TASK_SIZE - gap - random_factor);
}

/*
 * This function, called very early during the creation of a new
 * process VM image, sets up which VM layout function to use:
 */
/**
 * 在创建进程时，确定如何为进程虚拟地址空间进行布局。
 */
void arch_pick_mmap_layout(struct mm_struct *mm)
{
	/*
	 * Fall back to the standard layout if the personality
	 * bit is set, or if the expected stack growth is unlimited:
	 */
	if (sysctl_legacy_va_layout ||/* 强制使用旧的布局 */
			(current->personality & ADDR_COMPAT_LAYOUT) ||/* 需要兼容旧的二进制文件 */
			current->signal->rlim[RLIMIT_STACK].rlim_cur == RLIM_INFINITY) {/* 堆栈空间不受限制，无法使用新的布局 */
		/* 经典布局中，mmap空间开始于用户态空间的1/3处 */
		mm->mmap_base = TASK_UNMAPPED_BASE;
		/* x86架构下，使用默认的get_unmapped_area函数 */
		mm->get_unmapped_area = arch_get_unmapped_area;
		mm->unmap_area = arch_unmap_area;
	} else {
		/* 新布局下，基地址靠近堆栈地址 */
		mm->mmap_base = mmap_base(mm);
		/* mmap区域从顶向低地址扩展 */
		mm->get_unmapped_area = arch_get_unmapped_area_topdown;
		mm->unmap_area = arch_unmap_area_topdown;
	}
}
