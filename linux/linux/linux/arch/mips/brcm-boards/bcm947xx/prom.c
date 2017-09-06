/*
 * Early initialization code for BCM94710 boards
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
 * $Id: prom.c,v 1.1.1.1 2012/08/29 05:42:23 bcm5357 Exp $
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <asm/bootinfo.h>
#include <asm/cpu.h>
#include <typedefs.h>
#include <osl.h>
#include <bcmutils.h>
#include <bcmnvram.h>
#include <bcmendian.h>
#include <hndsoc.h>
#include <siutils.h>
#include <hndcpu.h>
#include <mipsinc.h>
#include <mips74k_core.h>

#ifdef CONFIG_BLK_DEV_INITRD
extern void __init init_ramdisk(unsigned long mem_end);
#endif

extern char ram_nvram_buf[];

void __init
prom_init(int argc, const char **argv)
{
	unsigned long mem;
	struct nvram_header *header;

	mips_machgroup = MACH_GROUP_BRCM;
	mips_machtype = MACH_BCM947XX;

	/* Figure out memory size by finding aliases */
	for (mem = (1 << 20); mem < (128 << 20); mem <<= 1) {
		if (*(unsigned long *)((unsigned long)(prom_init) + mem) == 
		    *(unsigned long *)(prom_init))
			break;
	}
#if CONFIG_RAM_SIZE
	{
		unsigned long config_mem;	
		config_mem = CONFIG_RAM_SIZE * 0x100000;
		if (config_mem < mem)
			mem = config_mem;
	}
#endif

	/* Ignoring the last page when ddr size is 128M. Cached
	 * accesses to last page is causing the processor to prefetch
	 * using address above 128M stepping out of the ddr address
	 * space.
	 */
	if (MIPS74K(mips_cpu.processor_id) && (mem == 0x8000000))
		mem -= 0x1000;

	/* CFE could have loaded nvram during netboot
	 * to top 32KB of RAM, Just check for nvram signature
	 * and copy it to nvram space embedded in linux
	 * image for later use by nvram driver.
	 */
	header = (struct nvram_header *)(KSEG0ADDR(mem - NVRAM_SPACE));
	if (ltoh32(header->magic) == NVRAM_MAGIC) {
		uint32 *src = (uint32 *)header;
		uint32 *dst = (uint32 *)ram_nvram_buf;
		uint32 i;

		printk("Copying NVRAM bytes: %d from: 0x%p To: 0x%p\n", ltoh32(header->len),
			src, dst);
		for (i = 0; i < ltoh32(header->len) && i < NVRAM_SPACE; i += 4)
			*dst++ = ltoh32(*src++);
	}

#ifdef CONFIG_BLK_DEV_INITRD
	init_ramdisk(mem);
#endif

	add_memory_region(0, mem, BOOT_MEM_RAM);
}

void __init
prom_free_prom_memory(void)
{
}
