/*
 * HND MIPS boards setup routines
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
 * $Id: setup.c 258411 2011-05-09 10:58:43Z simonk $
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/serialP.h>
#include <linux/ide.h>
#include <linux/seq_file.h>
#include <asm/bootinfo.h>
#include <asm/cpu.h>
#include <asm/time.h>
#include <asm/reboot.h>

#ifdef CONFIG_MTD_PARTITIONS
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/romfs_fs.h>
#include <linux/cramfs_fs.h>
#include <linux/squashfs_fs.h>
#endif

#include <typedefs.h>
#include <osl.h>
#include <bcmutils.h>
#ifdef NFLASH_SUPPORT
#include <hndsoc.h>
#endif
#include <bcmnvram.h>
#include <siutils.h>
#include <hndcpu.h>
#include <mips33_core.h>
#include <mips74k_core.h>
#include <hndmips.h>
#include <sbchipc.h>
#include <hndchipc.h>
#include <trxhdr.h>
#ifdef CONFIG_BLK_DEV_INITRD
#include <linux/blk.h>
#endif
#ifdef HNDCTF
#include <ctf/hndctf.h>
#endif /* HNDCTF */
#include "bcm947xx.h"
#ifdef NFLASH_SUPPORT
#include "nflash.h"
#endif

extern void bcm947xx_time_init(void);
extern void bcm947xx_timer_setup(struct irqaction *irq);

#ifdef CONFIG_REMOTE_DEBUG
extern void set_debug_traps(void);
extern void rs_kgdb_hook(struct serial_state *);
extern void breakpoint(void);
#endif

#if defined(CONFIG_BLK_DEV_IDE) || defined(CONFIG_BLK_DEV_IDE_MODULE)
extern struct ide_ops std_ide_ops;
#endif

/* Enable CPU wait or not */
extern int cpu_wait_enable;

/* Global assert type */
extern uint32 g_assert_type;

/* Global SB handle */
si_t *bcm947xx_sih = NULL;
spinlock_t bcm947xx_sih_lock = SPIN_LOCK_UNLOCKED;
EXPORT_SYMBOL(bcm947xx_sih);
EXPORT_SYMBOL(bcm947xx_sih_lock);

/* Convenience */
#define sih bcm947xx_sih
#define sih_lock bcm947xx_sih_lock

#ifdef HNDCTF
ctf_t *kcih = NULL;
EXPORT_SYMBOL(kcih);
ctf_attach_t ctf_attach_fn = NULL;
EXPORT_SYMBOL(ctf_attach_fn);
#endif /* HNDCTF */

/* Kernel command line */
char arcs_cmdline[CL_SIZE] __initdata = CONFIG_CMDLINE;
static int lanports_enable = 0;
static int wombo_reset = GPIO_PIN_NOTDEFINED;

static void
bcm947xx_reboot_handler(void)
{
	if (lanports_enable) {
		uint lp = 1 << lanports_enable;

		si_gpioout(sih, lp, 0, GPIO_DRV_PRIORITY);
		si_gpioouten(sih, lp, lp, GPIO_DRV_PRIORITY);
		bcm_mdelay(1);
	}

	/* gpio 0 is also valid wombo_reset */
	if (wombo_reset != GPIO_PIN_NOTDEFINED) {
		int reset = 1 << wombo_reset;

		si_gpioout(sih, reset, 0, GPIO_DRV_PRIORITY);
		si_gpioouten(sih, reset, reset, GPIO_DRV_PRIORITY);
		bcm_mdelay(10);
	}
}

void
bcm947xx_machine_restart(char *command)
{
	printk("Please stand by while rebooting the system...\n");

	/* Set the watchdog timer to reset immediately */
	__cli();

	bcm947xx_reboot_handler();
	hnd_cpu_reset(sih);
}

void
bcm947xx_machine_halt(void)
{
	printk("System halted\n");

	/* Disable interrupts and watchdog and spin forever */
	__cli();
	si_watchdog(sih, 0);
	bcm947xx_reboot_handler();
	while (1);
}

#ifdef CONFIG_SERIAL

static struct serial_struct rs = {
	line: 0,
	flags: ASYNC_BOOT_AUTOCONF,
	io_type: SERIAL_IO_MEM,
};

static void __init
serial_add(void *regs, uint irq, uint baud_base, uint reg_shift)
{
	rs.iomem_base = regs;
	rs.irq = irq + 2;
	rs.baud_base = baud_base / 16;
	rs.iomem_reg_shift = reg_shift;

	early_serial_setup(&rs);

	rs.line++;
}

static void __init
serial_setup(si_t *sih)
{
	si_serial_init(sih, serial_add);

#ifdef CONFIG_REMOTE_DEBUG
	/* Use the last port for kernel debugging */
	if (rs.iomem_base)
		rs_kgdb_hook(&rs);
#endif
}

#endif /* CONFIG_SERIAL */

void __init
brcm_setup(void)
{
	char *value;

	/* Get global SI handle */
	sih = si_kattach(SI_OSH);

	/* Initialize clocks and interrupts */
	si_mips_init(sih, SBMIPS_VIRTIRQ_BASE);

	if (BCM330X(mips_cpu.processor_id) &&
		(read_c0_diag() & BRCM_PFC_AVAIL)) {
		/* 
		 * Now that the sih is inited set the proper PFC value 
		 */	
		printk("Setting the PFC to its default value\n");
		enable_pfc(PFC_AUTO);
	}


#ifdef CONFIG_SERIAL
	/* Initialize UARTs */
	serial_setup(sih);
#endif

#if defined(CONFIG_BLK_DEV_IDE) || defined(CONFIG_BLK_DEV_IDE_MODULE)
	ide_ops = &std_ide_ops;
#endif

	value = nvram_get("assert_type");
	if (value && strlen(value))
		g_assert_type = simple_strtoul(value, NULL, 10);

#ifdef NFLASH_SUPPORT
	if ((sih->ccrev >= 38) && ((sih->chipst & (1 << 4)) != 0)) {
		if (strncmp(arcs_cmdline, "root=/dev/mtdblock", strlen("root=/dev/mtdblock")) == 0)
			strcpy(arcs_cmdline, "root=/dev/mtdblock3 noinitrd console=ttyS0,115200");
	}
#endif
	/* Override default command line arguments */
	value = nvram_get("kernel_args");
	if (value && strlen(value) && strncmp(value, "empty", 5))
		strncpy(arcs_cmdline, value, sizeof(arcs_cmdline));


	if ((lanports_enable = getgpiopin(NULL, "lanports_enable", GPIO_PIN_NOTDEFINED)) ==
		GPIO_PIN_NOTDEFINED)
		lanports_enable = 0;

	/* Generic setup */
	_machine_restart = bcm947xx_machine_restart;
	_machine_halt = bcm947xx_machine_halt;
	_machine_power_off = bcm947xx_machine_halt;

	board_time_init = bcm947xx_time_init;
	board_timer_setup = bcm947xx_timer_setup;

	/* Check if we want to enable cpu wait */
	if (nvram_match("wait", "1"))
		cpu_wait_enable = 1;

	/* wombo reset */
	if ((wombo_reset = getgpiopin(NULL, "wombo_reset", GPIO_PIN_NOTDEFINED)) !=
	    GPIO_PIN_NOTDEFINED) {
		int reset = 1 << wombo_reset;

		printk("wombo_reset set to gpio %d\n", wombo_reset);

		si_gpioout(sih, reset, 0, GPIO_DRV_PRIORITY);
		si_gpioouten(sih, reset, reset, GPIO_DRV_PRIORITY);
		bcm_mdelay(10);

		si_gpioout(sih, reset, reset, GPIO_DRV_PRIORITY);
		bcm_mdelay(20);
	}
}

const char *
get_system_type(void)
{
	static char s[32];
	char cn[8];

	if (bcm947xx_sih) {
		bcm_chipname(bcm947xx_sih->chip, cn, 8);
		sprintf(s, "Broadcom BCM%s chip rev %d",
		        cn, bcm947xx_sih->chiprev);
		return s;
	} else
		return "Broadcom BCM947XX";
}

void
bcm947xx_cpuinfo(struct seq_file *m)
{
	seq_printf(m, "System clocks\n  (cpu/mem/si/xtal)\t: %u/%u/%u/%u Mhz.\n",
		si_cpu_clock(sih) / 1000000,
		si_mem_clock(sih) / 1000000,
		si_clock(sih) / 1000000,
		si_alp_clock(sih) / 1000000);
}

void __init
bus_error_init(void)
{
}

#ifdef CONFIG_MTD_PARTITIONS

static struct mtd_partition bcm947xx_parts[] = {
#ifdef CONFIG_EFM_PATCH
	{ name: "boot",	offset: 0, size: 0, /* mask_flags: MTD_WRITEABLE,*/ },
#else
	{ name: "boot",	offset: 0, size: 0, mask_flags: MTD_WRITEABLE, },
#endif
	{ name: "linux", offset: 0, size: 0, },
	{ name: "rootfs", offset: 0, size: 0, mask_flags: MTD_WRITEABLE, },
#ifdef CONFIG_DUAL_IMAGE
	{ name: "linux2", offset: 0, size: 0, },
	{ name: "rootfs2", offset: 0, size: 0, mask_flags: MTD_WRITEABLE,  },
#endif
#ifdef BCMCONFMTD
	{ name: "confmtd", offset: 0, size: 0, },
#endif /* BCMCONFMTD */
	{ name: "nvram", offset: 0, size: 0, },

#ifdef CONFIG_EFM_PATCH
        { name: "all", offset: 0, size: 0x400000, },
#endif

	{ name: NULL, },
};


#ifdef CONFIG_DUAL_IMAGE
/*
 * Set the rootfs dynamically
 */
static int trim_rootfs(int boot_select)
{
	if (boot_select == 0) {
		ROOT_DEV = MKDEV(31, 2);
		printk("!! Set the ROOT_DEV=%s\n", kdevname(ROOT_DEV));
	} else if (boot_select == 1) {
		ROOT_DEV = MKDEV(31, 4);
		printk("!! Set the ROOT_DEV=%s\n", kdevname(ROOT_DEV));
	} else {
		printk("!! Cannot find any root fs to boot!\n");
	}
	return 0;
}
#endif /*  CONFIG_DUAL_IMAGE */

/* 
 * Refactoring all the file system test here
 * Note that it must be poited to the sector that containing the MAGIC number
 * Return 1: get file systems
 */
static int look_for_rootfs(struct mtd_info *mtd, struct mtd_partition *the_part,
	int *off_got, int off_start, int off_end)
{
	struct romfs_super_block *romfsb;
	struct cramfs_super *cramfsb;
	struct squashfs_super_block *squashfsb;
	struct trx_header *trx;
	unsigned char buf[512];
	int off;
	int ret = -1;
	size_t len;

	romfsb = (struct romfs_super_block *) buf;
	cramfsb = (struct cramfs_super *) buf;
	squashfsb = (struct squashfs_super_block *) buf;
	trx = (struct trx_header *) buf;

	/* Look at every 64 KB boundary */
	for (off = off_start; off < off_end; off += (64 * 1024)) {
		memset(buf, 0xe5, sizeof(buf));

		/*
		 * Read to test for trx header or romfs/cramfs superblock
		 */
		if (MTD_READ(mtd, off, sizeof(buf), &len, buf) || len != sizeof(buf))
			continue;

		/* Try looking at TRX header for rootfs offset */
		if (le32_to_cpu(trx->magic) == TRX_MAGIC) {
			the_part->offset = off;
			if (trx->offsets[1] == 0)
				continue;

			/*
			 * Read to test for romfs and cramfs superblock
			 */
			off += le32_to_cpu(trx->offsets[1]);
			memset(buf, 0xe5, sizeof(buf));
			if (MTD_READ(mtd, off, sizeof(buf), &len, buf) || len != sizeof(buf))
				continue;
		}

		/* romfs is at block zero too */
		if (romfsb->word0 == ROMSB_WORD0 &&
		    romfsb->word1 == ROMSB_WORD1) {
			printk(KERN_NOTICE
			       "%s: romfs filesystem found at block %d\n",
			       mtd->name, off / BLOCK_SIZE);
			ret = 0;
			goto done;
		}

		/* so is cramfs */
		if (cramfsb->magic == CRAMFS_MAGIC) {
			printk(KERN_NOTICE
			       "%s: cramfs filesystem found at block %d\n",
			       mtd->name, off / BLOCK_SIZE);
			ret = 0;
			goto done;
		}

		if (squashfsb->s_magic == SQUASHFS_MAGIC_LZMA) {
			printk(KERN_NOTICE
			       "%s: squash filesystem with lzma found at block %d\n",
			       mtd->name, off / BLOCK_SIZE);
			ret = 0;
			goto done;
		}
	}

	printk(KERN_NOTICE
	       "%s: Couldn't find valid ROM disk image\n",
	       mtd->name);
	ret = -1;
done:
	*off_got = off;
	return (ret);

}

struct mtd_partition * __init
init_mtd_partitions(struct mtd_info *mtd, size_t size)
{
	int i;
	int off;
#ifdef CONFIG_DUAL_IMAGE
	char *img_boot = nvram_get(IMAGE_BOOT);
	int off2;
	char *imag_1st_offset = nvram_get(IMAGE_FIRST_OFFSET);
	char *imag_2nd_offset = nvram_get(IMAGE_SECOND_OFFSET);
	unsigned int image_first_offset, image_second_offset;
	char dual_image_on = 0;

	/* The image_1st_size and image_2nd_size are necessary if the Flash does not have any
	 * image
	 */
	dual_image_on = (img_boot != NULL && imag_1st_offset != NULL && imag_2nd_offset != NULL);

	if (dual_image_on) {
		image_first_offset = simple_strtol(imag_1st_offset, NULL, 10);
		image_second_offset = simple_strtol(imag_2nd_offset, NULL, 10);
		printk("The first offset=%x, 2nd offset=%x\n", image_first_offset,
			image_second_offset);
		/* Neglect the rootfs size around 512K since the target rootfs is more than 2MB */
		if (look_for_rootfs(mtd, (struct mtd_partition*) &bcm947xx_parts[1], (int*) &off,
			image_first_offset, image_second_offset) == 0) {
		} else {
			printk("!!!!! Cannot find root file systems for 1st image!!\n");
		}
		if (look_for_rootfs(mtd, (struct mtd_partition*) &bcm947xx_parts[3], (int*) &off2,
			image_second_offset, size) == 0) {
		} else {
			printk("!!!!! Cannot find root file systems for 2nd image!!\n");
		}
		trim_rootfs(simple_strtol(img_boot, NULL, 10));
	} else {
		/* Fallback to non-dual-image */
#else
	{
#endif /* CONFIG_DUAL_IMAGE */
		if (look_for_rootfs(mtd, (struct mtd_partition*) &bcm947xx_parts[1], (int*) &off,
			0, size) == 0) {
			goto done;
		} else {
			printk("!!!!! Cannot find root file systems!!\n");
		}
	}
done:
	/* Setup NVRAM MTD partition */
	i = (sizeof(bcm947xx_parts)/sizeof(struct mtd_partition)) - 2;

#ifdef CONFIG_EFM_PATCH
	bcm947xx_parts[3].size = ROUNDUP(NVRAM_SPACE, mtd->erasesize);
	bcm947xx_parts[3].offset = size - bcm947xx_parts[3].size;
#else
	bcm947xx_parts[i].size = ROUNDUP(NVRAM_SPACE, mtd->erasesize);
	bcm947xx_parts[i].offset = size - bcm947xx_parts[i].size;
#endif
#ifdef BCMCONFMTD
	/* Setup WAPI MTD partition */
	i--;
	bcm947xx_parts[i].size = mtd->erasesize;
	bcm947xx_parts[i].offset = bcm947xx_parts[i+1].offset - bcm947xx_parts[i].size;
#endif	/* BCMCONFMTD */
#ifdef CONFIG_DUAL_IMAGE
	if (dual_image_on) {
		i--;
		/* The 2nd rootfs */
		if (off2 < size) {
			bcm947xx_parts[i].offset = off2;
		} else {
			/* 2nd image not got, use linux image offset instead */
			bcm947xx_parts[i].offset = image_second_offset;
		}
		bcm947xx_parts[i].size = bcm947xx_parts[i+1].offset - bcm947xx_parts[i].offset;

		/* Size 2nd Linux. The Linux offset is determined in lookup_root_fs() also,
		 * But we simply use the NVRAM variables, they should match
		 */
		i--;
		bcm947xx_parts[i].offset = image_second_offset;
		bcm947xx_parts[i].size = bcm947xx_parts[i+2].offset - bcm947xx_parts[i].offset;

		/* Find and size 1st rootfs */
		if (off < image_second_offset) {
			bcm947xx_parts[2].offset = off;
		} else {
			bcm947xx_parts[2].offset = image_first_offset;
		}
		bcm947xx_parts[2].size = bcm947xx_parts[3].offset - bcm947xx_parts[2].offset;

		/* Size 1st linux (kernel and rootfs) */
		bcm947xx_parts[1].offset = image_first_offset;
		bcm947xx_parts[1].size = bcm947xx_parts[3].offset - bcm947xx_parts[1].offset;
	} else {
		/* Fix the bcm947xx_parts[] to remove linux2, rootfs2 */
		char* linux2_addr = (char*) &bcm947xx_parts[3];
		char* after_rootfs2_addr = (char*) &bcm947xx_parts[5];
		int copy_size = sizeof(bcm947xx_parts) - 2*sizeof(struct mtd_partition);
		memcpy(linux2_addr, after_rootfs2_addr, copy_size);
		/* Find and size rootfs */
		/* Clone original to fallback */
		if (off < size) {
			bcm947xx_parts[2].offset = off;
			bcm947xx_parts[2].size = bcm947xx_parts[3].offset -
				bcm947xx_parts[2].offset;
		}

		/* Size linux (kernel and rootfs) */
		bcm947xx_parts[1].size = bcm947xx_parts[3].offset - bcm947xx_parts[1].offset;
	}
#else
	/* Find and size rootfs */
	if (off < size) {
		bcm947xx_parts[2].offset = off;
		bcm947xx_parts[2].size = bcm947xx_parts[3].offset - bcm947xx_parts[2].offset;
	}

	/* Size linux (kernel and rootfs) */
	bcm947xx_parts[1].size = bcm947xx_parts[3].offset - bcm947xx_parts[1].offset;
#endif /* CONFIG_DUAL_IMAGE */
	/* Size pmon */
	bcm947xx_parts[0].size = bcm947xx_parts[1].offset - bcm947xx_parts[0].offset;

	return bcm947xx_parts;
}

EXPORT_SYMBOL(init_mtd_partitions);

#ifdef NFLASH_SUPPORT
static struct mtd_partition bcm947xx_nflash_parts[] =
{
	{
		.name = "boot",
		.size = 0,
		.offset = 0,
		.mask_flags = MTD_WRITEABLE
	},
	{
		.name = "nvram",
		.size = 0,
		.offset = 0
	},
	{
		.name = "linux",
		.size = 0,
		.offset = 0
	},
	{
		.name = "rootfs",
		.size = 0,
		.offset = 0,
		.mask_flags = MTD_WRITEABLE
	},
#ifdef BCMCONFMTD
	{
		.name = "confmtd",
		.size = 0,
		.offset = 0
	},
#endif /* BCMCONFMTD */
	{
		.name = 0,
		.size = 0,
		.offset = 0
	}
};

struct mtd_partition * init_nflash_mtd_partitions(struct mtd_info *mtd, size_t size)
{
	struct romfs_super_block *romfsb;
	struct cramfs_super *cramfsb;
	struct squashfs_super_block *squashfsb;
	struct trx_header *trx;
	unsigned char buf[NFL_SECTOR_SIZE];
	uint blocksize, mask, blk_offset, off, shift = 0;
	chipcregs_t *cc;
	uint32 bootsz, *bisz;
	int ret, i;
	uint32 top;

	romfsb = (struct romfs_super_block *) buf;
	cramfsb = (struct cramfs_super *) buf;
	squashfsb = (struct squashfs_super_block *) buf;
	trx = (struct trx_header *) buf;

	if ((cc = (chipcregs_t *)si_setcoreidx(sih, SI_CC_IDX)) == NULL)
		return NULL;

	/* Look at every block boundary till 16MB; higher space is reserved for application data. */
	blocksize = mtd->erasesize;
	for (off = NFL_BOOT_SIZE; off < NFL_BOOT_OS_SIZE; off += blocksize) {
		mask = blocksize - 1;
		blk_offset = off & ~mask;
		if (nflash_checkbadb(sih, cc, blk_offset) != 0)
			continue;
		memset(buf, 0xe5, sizeof(buf));
		if ((ret = nflash_read(sih, cc, off, sizeof(buf), buf)) != sizeof(buf)) {
			printk(KERN_NOTICE
			       "%s: nflash_read return %d\n", mtd->name, ret);
			continue;
		}

		/* Try looking at TRX header for rootfs offset */
		if (le32_to_cpu(trx->magic) == TRX_MAGIC) {
			mask = NFL_SECTOR_SIZE - 1;
			off = NFL_BOOT_SIZE + (le32_to_cpu(trx->offsets[1]) & ~mask) - blocksize;
			shift = (le32_to_cpu(trx->offsets[1]) & mask);
			romfsb = (unsigned char *)romfsb + shift;
			cramfsb = (unsigned char *)cramfsb + shift;
			squashfsb = (unsigned char *)squashfsb + shift;
			continue;
		}

		/* romfs is at block zero too */
		if (romfsb->word0 == ROMSB_WORD0 &&
		    romfsb->word1 == ROMSB_WORD1) {
			printk(KERN_NOTICE
			       "%s: romfs filesystem found at block %d\n",
			       mtd->name, off / BLOCK_SIZE);
			goto done;
		}

		/* so is cramfs */
		if (cramfsb->magic == CRAMFS_MAGIC) {
			printk(KERN_NOTICE
			       "%s: cramfs filesystem found at block %d\n",
			       mtd->name, off / BLOCK_SIZE);
			goto done;
		}

		if (squashfsb->s_magic == SQUASHFS_MAGIC_LZMA) {
			printk(KERN_NOTICE
			       "%s: squash filesystem with lzma found at block %d\n",
			       mtd->name, off / BLOCK_SIZE);
			goto done;
		}
	}

	printk(KERN_NOTICE
	       "%s: Couldn't find valid ROM disk image\n",
	       mtd->name);

done:

	/* Default is 256K boot partition */
	bootsz = 256 * 1024;

	/* Do we have a self-describing binary image? */
	bisz = (uint32 *)KSEG1ADDR(SI_FLASH1 + BISZ_OFFSET);
	if (bisz[BISZ_MAGIC_IDX] == BISZ_MAGIC) {
		int isz = bisz[BISZ_DATAEND_IDX] - bisz[BISZ_TXTST_IDX];

		if (isz > (1024 * 1024))
			bootsz = 2048 * 1024;
		else if (isz > (512 * 1024))
			bootsz = 1024 * 1024;
		else if (isz > (256 * 1024))
			bootsz = 512 * 1024;
		else if (isz <= (128 * 1024))
			bootsz = 128 * 1024;
	}
	if (bootsz > mtd->erasesize) {
		/* Prepare double space in case of bad blocks */
		bootsz = (bootsz << 1);
	} else {
		/* CFE occupies at least one block */
		bootsz = mtd->erasesize;
	}

	printf("Boot partition size = %d(0x%x)\n", bootsz, bootsz);

	/* Size pmon */
	bcm947xx_nflash_parts[0].size = bootsz;

	/* Setup NVRAM MTD partition */
	bcm947xx_nflash_parts[1].offset = bootsz;
	bcm947xx_nflash_parts[1].size = NFL_BOOT_SIZE - bootsz;

	i = (sizeof(bcm947xx_parts)/sizeof(struct mtd_partition)) - 2;
	top = NFL_BOOT_OS_SIZE;
#ifdef BCMCONFMTD
	bcm947xx_nflash_parts[i].size = mtd->erasesize * 4;
	bcm947xx_nflash_parts[i].offset = top - bcm947xx_nflash_parts[i].size;
	top -= bcm947xx_nflash_parts[i].size;
	i--;
#endif
	/* Find and size rootfs */
	if (off < size) {
		bcm947xx_nflash_parts[3].offset = off + shift;
		bcm947xx_nflash_parts[3].size = top - bcm947xx_nflash_parts[3].offset;
	}

	/* Size linux (kernel and rootfs) */
	bcm947xx_nflash_parts[2].offset = NFL_BOOT_SIZE;
	bcm947xx_nflash_parts[2].size = top - bcm947xx_nflash_parts[2].offset;

	return bcm947xx_nflash_parts;
}

EXPORT_SYMBOL(init_nflash_mtd_partitions);
#endif /* NFLASH_SUPPORT */

#ifdef CONFIG_BLK_DEV_INITRD
extern void * __rd_start, * __rd_end;
extern char _end;

/* The check_ramdisk_trx has more exact qualification to look at TRX header from end of linux */
static __init int
check_ramdisk_trx(unsigned long offset, unsigned long ram_size)
{
	struct trx_header *trx;
	uint32 crc;
	unsigned int len;
	uint8 *ptr = (uint8 *)offset;

	trx = (struct trx_header *)ptr;

	/* Not a TRX_MAGIC */
	if (le32_to_cpu(trx->magic) != TRX_MAGIC) {
		printk("check_ramdisk_trx: not a valid TRX magic\n");
		return -1;
	}

	/* TRX len invalid */
	len = le32_to_cpu(trx->len);
	if (offset + len > ram_size) {
		printk("check_ramdisk_trx: not a valid TRX length\n");
		return -1;
	}

	/* Checksum over header */
	crc = hndcrc32((uint8 *) &trx->flag_version,
		sizeof(struct trx_header) - OFFSETOF(struct trx_header, flag_version),
		CRC32_INIT_VALUE);

	/* Move ptr to data */
	ptr += sizeof(struct trx_header);
	len -= sizeof(struct trx_header);

	/* Checksum over data */
	crc = hndcrc32(ptr, len, crc);

	/* Verify checksum */
	if (le32_to_cpu(trx->crc32) != crc) {
		printk("check_ramdisk_trx: checksum invalid\n");
		return -1;
	}

	return 0;
}

void __init init_ramdisk(unsigned long mem_end)
{
	struct trx_header *trx = NULL;
	char *from_rootfs, *to_rootfs;
	unsigned long rootfs_size = 0;
	unsigned long ram_size = mem_end + 0x80000000;
	unsigned long offset;
	char *root_cmd =
		"root=/dev/ram0 ramdisk_size=4096 ramdisk_blocksize=4096 console=ttyS0,115200";

	/* embedded ramdisk */
	if (&__rd_start != &__rd_end)
		return;

	to_rootfs = (((unsigned long)&_end + PAGE_SIZE-1) & PAGE_MASK);
	offset = ((unsigned long)&_end +0xffff) & ~0xffff;

	/* Look at TRX header from end of linux */
	for (; offset < ram_size; offset += 0x10000) {
		trx = (struct trx_header *)offset;
		if (le32_to_cpu(trx->magic) == TRX_MAGIC &&
			check_ramdisk_trx(offset, ram_size) == 0) {
			printk(KERN_NOTICE
				   "Found TRX image  at %08lx\n", offset);
			from_rootfs = (char *)((unsigned long)trx + le32_to_cpu(trx->offsets[1]));
			rootfs_size = le32_to_cpu(trx->len) - le32_to_cpu(trx->offsets[1]);
			rootfs_size = (rootfs_size + 0xffff) & ~0xffff;
			printk("rootfs size is %ld bytes at 0x%p, copying to 0x%p\n",
				rootfs_size, from_rootfs, to_rootfs);
			memmove(to_rootfs, from_rootfs, rootfs_size);

			initrd_start = (int)to_rootfs;
			initrd_end = initrd_start + rootfs_size;
			strncpy(arcs_cmdline, root_cmd, sizeof(arcs_cmdline));
			/* 
			 * In case the system warm boot, the memory won't be zeroed.
			 * So we have to erase trx magic.
			 */
			if (initrd_end < (unsigned long)trx)
				trx->magic = 0;
			break;
		}
	}
}
#endif /* CONFIG_BLK_DEV_INITRD */

#endif /* CONFIG_MTD_PARTITIONS */
