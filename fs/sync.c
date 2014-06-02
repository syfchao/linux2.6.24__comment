/*
 * High-level sync()-related operations
 */

#include <linux/kernel.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/writeback.h>
#include <linux/syscalls.h>
#include <linux/linkage.h>
#include <linux/pagemap.h>
#include <linux/quotaops.h>
#include <linux/buffer_head.h>

#define VALID_FLAGS (SYNC_FILE_RANGE_WAIT_BEFORE|SYNC_FILE_RANGE_WRITE| \
			SYNC_FILE_RANGE_WAIT_AFTER)

/*
 * sync everything.  Start out by waking pdflush, because that writes back
 * all queues in parallel.
 */
static void do_sync(unsigned long wait)
{
	/* 唤醒pdflush，要求它刷新全部所有数据 */
	wakeup_pdflush(0);
	/* 同步inode元数据 */
	sync_inodes(0);		/* All mappings, inodes and their blockdevs */
	DQUOT_SYNC(NULL);
	/* 同步超级块，将超级块回写到磁盘 */
	sync_supers();		/* Write the superblocks */
	/* 调用各文件系统的sys_fs方法，这样文件系统就有机会参与到sync调用中来 */
	sync_filesystems(0);	/* Start syncing the filesystems */
	sync_filesystems(wait);	/* Waitingly sync the filesystems */
	/* 同步节点，并等待其完成 */
	sync_inodes(wait);	/* Mappings, inodes and blockdevs, again. */
	if (!wait)
		printk("Emergency Sync complete\n");
	if (unlikely(laptop_mode))
		laptop_sync_completion();
}

/**
 * sync系统调用
 */
asmlinkage long sys_sync(void)
{
	do_sync(1);
	return 0;
}

void emergency_sync(void)
{
	pdflush_operation(do_sync, 0);
}

/*
 * Generic function to fsync a file.
 *
 * filp may be NULL if called via the msync of a vma.
 */
int file_fsync(struct file *filp, struct dentry *dentry, int datasync)
{
	struct inode * inode = dentry->d_inode;
	struct super_block * sb;
	int ret, err;

	/* sync the inode to buffers */
	ret = write_inode_now(inode, 0);

	/* sync the superblock to buffers */
	sb = inode->i_sb;
	lock_super(sb);
	if (sb->s_op->write_super)
		sb->s_op->write_super(sb);
	unlock_super(sb);

	/* .. finally sync the buffers to disk */
	err = sync_blockdev(sb->s_bdev);
	if (!ret)
		ret = err;
	return ret;
}

long do_fsync(struct file *file, int datasync)
{
	int ret;
	int err;
	struct address_space *mapping = file->f_mapping;

	if (!file->f_op || !file->f_op->fsync) {
		/* Why?  We can still call filemap_fdatawrite */
		ret = -EINVAL;
		goto out;
	}

	/* 回写地址空间中的页面 */
	ret = filemap_fdatawrite(mapping);

	/*
	 * We need to protect against concurrent writers, which could cause
	 * livelocks in fsync_buffers_list().
	 */
	mutex_lock(&mapping->host->i_mutex);
	/**
	 * 回调文件系统的fsync，做一些文件系统相关的同步处理
	 * 一般都会调用通用的sync_file函数
	 */
	err = file->f_op->fsync(file, file->f_path.dentry, datasync);
	if (!ret)
		ret = err;
	mutex_unlock(&mapping->host->i_mutex);
	/* 等待filemap_fdatawrite操作完成 */
	err = filemap_fdatawait(mapping);
	if (!ret)
		ret = err;
out:
	return ret;
}

/**
 * 同步单个文件
 */
static long __do_fsync(unsigned int fd, int datasync)
{
	struct file *file;
	int ret = -EBADF;

	/* 根据fd找到文件描述符 */
	file = fget(fd);
	if (file) {
		/* 同步数据 */
		ret = do_fsync(file, datasync);
		/* 释放对文件的引用 */
		fput(file);
	}
	return ret;
}

asmlinkage long sys_fsync(unsigned int fd)
{
	return __do_fsync(fd, 0);
}

asmlinkage long sys_fdatasync(unsigned int fd)
{
	return __do_fsync(fd, 1);
}

/*
 * sys_sync_file_range() permits finely controlled syncing over a segment of
 * a file in the range offset .. (offset+nbytes-1) inclusive.  If nbytes is
 * zero then sys_sync_file_range() will operate from offset out to EOF.
 *
 * The flag bits are:
 *
 * SYNC_FILE_RANGE_WAIT_BEFORE: wait upon writeout of all pages in the range
 * before performing the write.
 *
 * SYNC_FILE_RANGE_WRITE: initiate writeout of all those dirty pages in the
 * range which are not presently under writeback.
 *
 * SYNC_FILE_RANGE_WAIT_AFTER: wait upon writeout of all pages in the range
 * after performing the write.
 *
 * Useful combinations of the flag bits are:
 *
 * SYNC_FILE_RANGE_WAIT_BEFORE|SYNC_FILE_RANGE_WRITE: ensures that all pages
 * in the range which were dirty on entry to sys_sync_file_range() are placed
 * under writeout.  This is a start-write-for-data-integrity operation.
 *
 * SYNC_FILE_RANGE_WRITE: start writeout of all dirty pages in the range which
 * are not presently under writeout.  This is an asynchronous flush-to-disk
 * operation.  Not suitable for data integrity operations.
 *
 * SYNC_FILE_RANGE_WAIT_BEFORE (or SYNC_FILE_RANGE_WAIT_AFTER): wait for
 * completion of writeout of all pages in the range.  This will be used after an
 * earlier SYNC_FILE_RANGE_WAIT_BEFORE|SYNC_FILE_RANGE_WRITE operation to wait
 * for that operation to complete and to return the result.
 *
 * SYNC_FILE_RANGE_WAIT_BEFORE|SYNC_FILE_RANGE_WRITE|SYNC_FILE_RANGE_WAIT_AFTER:
 * a traditional sync() operation.  This is a write-for-data-integrity operation
 * which will ensure that all pages in the range which were dirty on entry to
 * sys_sync_file_range() are committed to disk.
 *
 *
 * SYNC_FILE_RANGE_WAIT_BEFORE and SYNC_FILE_RANGE_WAIT_AFTER will detect any
 * I/O errors or ENOSPC conditions and will return those to the caller, after
 * clearing the EIO and ENOSPC flags in the address_space.
 *
 * It should be noted that none of these operations write out the file's
 * metadata.  So unless the application is strictly performing overwrites of
 * already-instantiated disk blocks, there are no guarantees here that the data
 * will be available after a crash.
 */
asmlinkage long sys_sync_file_range(int fd, loff_t offset, loff_t nbytes,
					unsigned int flags)
{
	int ret;
	struct file *file;
	loff_t endbyte;			/* inclusive */
	int fput_needed;
	umode_t i_mode;

	ret = -EINVAL;
	if (flags & ~VALID_FLAGS)
		goto out;

	endbyte = offset + nbytes;

	if ((s64)offset < 0)
		goto out;
	if ((s64)endbyte < 0)
		goto out;
	if (endbyte < offset)
		goto out;

	if (sizeof(pgoff_t) == 4) {
		if (offset >= (0x100000000ULL << PAGE_CACHE_SHIFT)) {
			/*
			 * The range starts outside a 32 bit machine's
			 * pagecache addressing capabilities.  Let it "succeed"
			 */
			ret = 0;
			goto out;
		}
		if (endbyte >= (0x100000000ULL << PAGE_CACHE_SHIFT)) {
			/*
			 * Out to EOF
			 */
			nbytes = 0;
		}
	}

	if (nbytes == 0)
		endbyte = LLONG_MAX;
	else
		endbyte--;		/* inclusive */

	ret = -EBADF;
	file = fget_light(fd, &fput_needed);
	if (!file)
		goto out;

	i_mode = file->f_path.dentry->d_inode->i_mode;
	ret = -ESPIPE;
	if (!S_ISREG(i_mode) && !S_ISBLK(i_mode) && !S_ISDIR(i_mode) &&
			!S_ISLNK(i_mode))
		goto out_put;

	ret = do_sync_mapping_range(file->f_mapping, offset, endbyte, flags);
out_put:
	fput_light(file, fput_needed);
out:
	return ret;
}

/* It would be nice if people remember that not all the world's an i386
   when they introduce new system calls */
asmlinkage long sys_sync_file_range2(int fd, unsigned int flags,
				     loff_t offset, loff_t nbytes)
{
	return sys_sync_file_range(fd, offset, nbytes, flags);
}

/*
 * `endbyte' is inclusive
 */
int do_sync_mapping_range(struct address_space *mapping, loff_t offset,
			  loff_t endbyte, unsigned int flags)
{
	int ret;

	if (!mapping) {
		ret = -EINVAL;
		goto out;
	}

	ret = 0;
	if (flags & SYNC_FILE_RANGE_WAIT_BEFORE) {
		ret = wait_on_page_writeback_range(mapping,
					offset >> PAGE_CACHE_SHIFT,
					endbyte >> PAGE_CACHE_SHIFT);
		if (ret < 0)
			goto out;
	}

	if (flags & SYNC_FILE_RANGE_WRITE) {
		ret = __filemap_fdatawrite_range(mapping, offset, endbyte,
						WB_SYNC_NONE);
		if (ret < 0)
			goto out;
	}

	if (flags & SYNC_FILE_RANGE_WAIT_AFTER) {
		ret = wait_on_page_writeback_range(mapping,
					offset >> PAGE_CACHE_SHIFT,
					endbyte >> PAGE_CACHE_SHIFT);
	}
out:
	return ret;
}
EXPORT_SYMBOL_GPL(do_sync_mapping_range);
