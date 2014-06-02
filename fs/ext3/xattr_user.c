/*
 * linux/fs/ext3/xattr_user.c
 * Handler for extended user attributes.
 *
 * Copyright (C) 2001 by Andreas Gruenbacher, <a.gruenbacher@computer.org>
 */

#include <linux/module.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/ext3_jbd.h>
#include <linux/ext3_fs.h>
#include "xattr.h"

#define XATTR_USER_PREFIX "user."

static size_t
ext3_xattr_user_list(struct inode *inode, char *list, size_t list_size,
		     const char *name, size_t name_len)
{
	const size_t prefix_len = sizeof(XATTR_USER_PREFIX)-1;
	const size_t total_len = prefix_len + name_len + 1;

	if (!test_opt(inode->i_sb, XATTR_USER))
		return 0;

	if (list && total_len <= list_size) {
		memcpy(list, XATTR_USER_PREFIX, prefix_len);
		memcpy(list+prefix_len, name, name_len);
		list[prefix_len + name_len] = '\0';
	}
	return total_len;
}

/* 获得ext3的user空间扩展属性 */
static int
ext3_xattr_user_get(struct inode *inode, const char *name,
		    void *buffer, size_t size)
{
	/* buffer为null表示获得属性长度，但是name不能为空。 */
	if (strcmp(name, "") == 0)
		return -EINVAL;
	/* 确保装载时允许读写user空间 */
	if (!test_opt(inode->i_sb, XATTR_USER))
		return -EOPNOTSUPP;
 	/* 从inode或者block中获得属性 */
	return ext3_xattr_get(inode, EXT3_XATTR_INDEX_USER, name, buffer, size);
}

/* 设置user空间的扩展属性 */
static int
ext3_xattr_user_set(struct inode *inode, const char *name,
		    const void *value, size_t size, int flags)
{
	if (strcmp(name, "") == 0)
		return -EINVAL;
	/* 加载时禁止了扩展属性功能 */
	if (!test_opt(inode->i_sb, XATTR_USER))
		return -EOPNOTSUPP;
	/* 设置属性值 */
	return ext3_xattr_set(inode, EXT3_XATTR_INDEX_USER, name,
			      value, size, flags);
}

struct xattr_handler ext3_xattr_user_handler = {
	.prefix	= XATTR_USER_PREFIX,
	.list	= ext3_xattr_user_list,
	.get	= ext3_xattr_user_get,
	.set	= ext3_xattr_user_set,
};
