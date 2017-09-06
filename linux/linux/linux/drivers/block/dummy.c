/*
 * dummyfs: a placeholder filesystem that sleeps forever when mounted
 *
 * Copyright (C) 2010, Broadcom Corporation. All Rights Reserved.      
 *       
 * Permission to use, copy, modify, and/or distribute this software for any      
 * purpose with or without fee is hereby granted, provided that the above      
 * copyright notice and this permission notice appear in all copies.      
 *       
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES      
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF      
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY      
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES      
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION      
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN      
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.      
 *
 * $Id: dummy.c,v 1.1.1.1 2012/08/29 05:42:23 bcm5357 Exp $
 */

#include <linux/config.h>
#include <linux/module.h>

#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/stat.h>
#include <linux/errno.h>
#include <linux/major.h>
#include <linux/wait.h>
#include <linux/blk.h>
#include <linux/init.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/smp_lock.h>
#include <linux/swap.h>
#include <linux/slab.h>

#include <asm/uaccess.h>

/* I don't thik anyone would mind if we stole CM206_CDROM_MAJOR */
#define DUMMY_MAJOR 0x20

static int dummy_open(struct inode *inode, struct file *file)
{
	DECLARE_WAIT_QUEUE_HEAD(wait);

	for (;;)
		sleep_on(&wait);

	return 0;
}

static struct block_device_operations dummy_fops = {
	open:		dummy_open,
};

int __init dummy_init(void) 
{
	if (devfs_register_blkdev(DUMMY_MAJOR, "dummy", &dummy_fops)) {
		printk(KERN_WARNING "Unable to get major number for dummy device\n");
		return -EIO;
	}

	register_disk(NULL, MKDEV(DUMMY_MAJOR, 0), 1, &dummy_fops, 0);

	return 0;
}

void dummy_exit(void) 
{
	if (devfs_unregister_blkdev(0, "dummy"))
		printk(KERN_WARNING "dummy: cannot unregister blkdev\n");
}

module_init(dummy_init);
module_exit(dummy_exit);
