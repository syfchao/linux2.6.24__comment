/*
 *  linux/mm/page_io.c
 *
 *  Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *
 *  Swap reorganised 29.12.95, 
 *  Asynchronous swapping added 30.12.95. Stephen Tweedie
 *  Removed race in async swapping. 14.4.1996. Bruno Haible
 *  Add swap of shared pages through the page cache. 20.2.1998. Stephen Tweedie
 *  Always use brw_page, life becomes simpler. 12 May 1998 Eric Biederman
 */

#include <linux/mm.h>
#include <linux/kernel_stat.h>
#include <linux/pagemap.h>
#include <linux/swap.h>
#include <linux/bio.h>
#include <linux/swapops.h>
#include <linux/writeback.h>
#include <asm/pgtable.h>

static struct bio *get_swap_bio(gfp_t gfp_flags, pgoff_t index,
				struct page *page, bio_end_io_t end_io)
{
	struct bio *bio;

	bio = bio_alloc(gfp_flags, 1);
	if (bio) {
		struct swap_info_struct *sis;
		swp_entry_t entry = { .val = index, };

		sis = get_swap_info_struct(swp_type(entry));
		/* 搜索区间链表，查找槽位与磁盘块之间的映射 */
		bio->bi_sector = map_swap_page(sis, swp_offset(entry)) *
					(PAGE_SIZE >> 9);
		bio->bi_bdev = sis->bdev;
		bio->bi_io_vec[0].bv_page = page;
		bio->bi_io_vec[0].bv_len = PAGE_SIZE;
		bio->bi_io_vec[0].bv_offset = 0;
		bio->bi_vcnt = 1;
		bio->bi_idx = 0;
		bio->bi_size = PAGE_SIZE;
		bio->bi_end_io = end_io;
	}
	return bio;
}

static void end_swap_bio_write(struct bio *bio, int err)
{
	const int uptodate = test_bit(BIO_UPTODATE, &bio->bi_flags);
	struct page *page = bio->bi_io_vec[0].bv_page;

	if (!uptodate) {
		SetPageError(page);
		/*
		 * We failed to write the page out to swap-space.
		 * Re-dirty the page in order to avoid it being reclaimed.
		 * Also print a dire warning that things will go BAD (tm)
		 * very quickly.
		 *
		 * Also clear PG_reclaim to avoid rotate_reclaimable_page()
		 */
		set_page_dirty(page);
		printk(KERN_ALERT "Write-error on swap-device (%u:%u:%Lu)\n",
				imajor(bio->bi_bdev->bd_inode),
				iminor(bio->bi_bdev->bd_inode),
				(unsigned long long)bio->bi_sector);
		ClearPageReclaim(page);
	}
	end_page_writeback(page);
	bio_put(bio);
}

void end_swap_bio_read(struct bio *bio, int err)
{
	const int uptodate = test_bit(BIO_UPTODATE, &bio->bi_flags);
	struct page *page = bio->bi_io_vec[0].bv_page;

	if (!uptodate) {
		SetPageError(page);
		ClearPageUptodate(page);
		printk(KERN_ALERT "Read-error on swap-device (%u:%u:%Lu)\n",
				imajor(bio->bi_bdev->bd_inode),
				iminor(bio->bi_bdev->bd_inode),
				(unsigned long long)bio->bi_sector);
	} else {
		SetPageUptodate(page);
	}
	unlock_page(page);
	bio_put(bio);
}

/*
 * We may have stale swap cache pages in memory: notice
 * them here and get rid of the unnecessary final write.
 */
/**
 * 将页面数据写入到交换区指定位置
 */
int swap_writepage(struct page *page, struct writeback_control *wbc)
{
	struct bio *bio;
	int ret = 0, rw = WRITE;

	/* 如果页面仅仅由交换缓存使用，则可以从内存中移除 */
	if (remove_exclusive_swap_page(page)) {
		unlock_page(page);
		goto out;
	}
	/* 为交换页生成一个bio，并正确填充所需的参数 */
	bio = get_swap_bio(GFP_NOIO, page_private(page), page,
				end_swap_bio_write);/* end_swap_bio_write清除回写标志 */
	if (bio == NULL) {/* 没内存了 */
		set_page_dirty(page);
		unlock_page(page);
		ret = -ENOMEM;
		goto out;
	}
	if (wbc->sync_mode == WB_SYNC_ALL)
		rw |= (1 << BIO_RW_SYNC);
	count_vm_event(PSWPOUT);
	/* 设置回写标志 */
	set_page_writeback(page);
	unlock_page(page);
	/* 提交bio */
	submit_bio(rw, bio);
out:
	return ret;
}

int swap_readpage(struct file *file, struct page *page)
{
	struct bio *bio;
	int ret = 0;

	BUG_ON(!PageLocked(page));
	ClearPageUptodate(page);
	/* 生成读请求BIO */
	bio = get_swap_bio(GFP_KERNEL, page_private(page), page,
				end_swap_bio_read);
	if (bio == NULL) {
		unlock_page(page);
		ret = -ENOMEM;
		goto out;
	}
	count_vm_event(PSWPIN);
	/* 提交读请求 */
	submit_bio(READ, bio);
out:
	return ret;
}
