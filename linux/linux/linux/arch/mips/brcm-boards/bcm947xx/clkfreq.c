/*
 * Broadcom BCM47xx Clock Frequency Routines
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
 * $Id: clkfreq.c,v 1.1.1.1 2012/08/29 05:42:23 bcm5357 Exp $
 */

#include <linux/config.h>

#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <typedefs.h>
#include <osl.h>
#include <bcmutils.h>
#include <siutils.h>
#include <hndcpu.h>
#include <hndsoc.h>
#include <mips74k_core.h>

#define LINE_SIZE      80

extern si_t *bcm947xx_sih;

#define sih bcm947xx_sih

static struct proc_dir_entry *proc_root_clkfreq;

static ssize_t clkfreq_read (struct file *file, char *buf, size_t len,
			  loff_t *ppos)
{
	char clkfreq[256];
	int l;
	
	if (*ppos > 0) return 0;
	sprintf(clkfreq, "%d,%d,%d\n",  si_cpu_clock(sih) / 1000000, si_mem_clock(sih) / 1000000, si_clock(sih) / 1000000);
	l = strlen(clkfreq);
	if (len < l)
		l = len;
	if ( copy_to_user (buf, clkfreq, l) ) return -EFAULT;
	*ppos += l;
	return l;
}

static ssize_t clkfreq_write (struct file *file, const char *buf, size_t len,
			   loff_t *ppos)
{
	unsigned long mipsclock = 0, siclock = 0, pciclock = 0;
	char line[LINE_SIZE];
	char *clkfreq = line, *end;
	void *regs;
	int chclk_otf = 0;
	uint32 *intmask;
	uint32 saved_mask;

	if ( !suser () ) return -EPERM;
	/*  Can't seek (pwrite) on this device  */
	if (ppos != &file->f_pos) return -ESPIPE;
	memset(clkfreq, 0, LINE_SIZE);
	if (len > LINE_SIZE)
		len = LINE_SIZE;
	if (copy_from_user(clkfreq, buf, len - 1)) return -EFAULT;
	end = clkfreq + strlen (clkfreq) - 1;
	if (*end == '\n') *end = '\0';

	mipsclock = bcm_strtoul(clkfreq, &end, 0) * 1000000;
	if (*end == ',') {
		clkfreq = ++end;
		siclock = bcm_strtoul(clkfreq, &end, 0) * 1000000;
		if (*end == ',') {
			clkfreq = ++end;
			pciclock = bcm_strtoul(clkfreq, &end, 0) * 1000000;
		}
	}

	if (mipsclock) {
		regs = si_setcore(sih, MIPS74K_CORE_ID, 0);
		if (regs)
			chclk_otf = ((si_core_sflags(sih, 0, 0) & SISF_CHG_CLK_OTF_PRESENT) != 0);
		if (chclk_otf) {
			printk("Setting clocks %d/%d/%d\n", mipsclock, siclock, pciclock);
			local_irq_disable();
			intmask = (uint32 *) &((mips74kregs_t *)regs)->intmask[5];
			saved_mask = R_REG(NULL, intmask);
			W_REG(NULL, intmask, 0);
			si_mips_setclock(sih, mipsclock, siclock, pciclock);
			W_REG(NULL, intmask, saved_mask);
			local_irq_enable();
		} else {
			printk("On-the-fly clock change is not supported\n");
		}

		return len;
	}

	return -EINVAL;
}

static struct file_operations clkfreq_fops =
{
	owner:		THIS_MODULE,
	read:		clkfreq_read,
	write:		clkfreq_write,
};

static int __init clkfreq_init(void)
{
	proc_root_clkfreq = create_proc_entry ("clkfreq", S_IWUSR | S_IRUGO, &proc_root);
	if (proc_root_clkfreq) {
		proc_root_clkfreq->owner = THIS_MODULE;
		proc_root_clkfreq->proc_fops = &clkfreq_fops;
	}
	return 0;
}

static void __exit clkfreq_cleanup(void)
{
	remove_proc_entry("clkfreq", &proc_root);
}

/* hook it up with system at boot time */
module_init(clkfreq_init);
module_exit(clkfreq_cleanup);

#endif	/* CONFIG_PROC_FS */
