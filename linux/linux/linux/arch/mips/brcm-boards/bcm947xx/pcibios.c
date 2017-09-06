/*
 * Low-Level PCI and SI support for BCM47xx (Linux support code)
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
 * $Id: pcibios.c,v 1.1.1.1 2012/08/29 05:42:23 bcm5357 Exp $
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/paccess.h>

#include <typedefs.h>
#include <bcmutils.h>
#include <hndsoc.h>
#include <siutils.h>
#include <hndcpu.h>
#include <hndpci.h>
#include <pcicfg.h>
#include <bcmdevs.h>
#include <bcmnvram.h>

/* Global SI handle */
extern si_t *bcm947xx_sih;
extern spinlock_t bcm947xx_sih_lock;

/* Convenience */
#define sih bcm947xx_sih
#define sih_lock bcm947xx_sih_lock


static int
hndpci_read_config_byte(struct pci_dev *dev, int where, u8 *value)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&sih_lock, flags);
	ret = hndpci_read_config(sih, dev->bus->number, PCI_SLOT(dev->devfn),
		PCI_FUNC(dev->devfn), where, value, sizeof(*value));
	spin_unlock_irqrestore(&sih_lock, flags);
	return ret ? PCIBIOS_DEVICE_NOT_FOUND : PCIBIOS_SUCCESSFUL;
}

static int
hndpci_read_config_word(struct pci_dev *dev, int where, u16 *value)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&sih_lock, flags);
	ret = hndpci_read_config(sih, dev->bus->number, PCI_SLOT(dev->devfn),
		PCI_FUNC(dev->devfn), where, value, sizeof(*value));
	spin_unlock_irqrestore(&sih_lock, flags);
	return ret ? PCIBIOS_DEVICE_NOT_FOUND : PCIBIOS_SUCCESSFUL;
}

static int
hndpci_read_config_dword(struct pci_dev *dev, int where, u32 *value)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&sih_lock, flags);
	ret = hndpci_read_config(sih, dev->bus->number, PCI_SLOT(dev->devfn),
		PCI_FUNC(dev->devfn), where, value, sizeof(*value));
	spin_unlock_irqrestore(&sih_lock, flags);
	return ret ? PCIBIOS_DEVICE_NOT_FOUND : PCIBIOS_SUCCESSFUL;
}

static int
hndpci_write_config_byte(struct pci_dev *dev, int where, u8 value)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&sih_lock, flags);
	ret = hndpci_write_config(sih, dev->bus->number, PCI_SLOT(dev->devfn),
		PCI_FUNC(dev->devfn), where, &value, sizeof(value));
	spin_unlock_irqrestore(&sih_lock, flags);
	return ret ? PCIBIOS_DEVICE_NOT_FOUND : PCIBIOS_SUCCESSFUL;
}

static int
hndpci_write_config_word(struct pci_dev *dev, int where, u16 value)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&sih_lock, flags);
	ret = hndpci_write_config(sih, dev->bus->number, PCI_SLOT(dev->devfn),
		PCI_FUNC(dev->devfn), where, &value, sizeof(value));
	spin_unlock_irqrestore(&sih_lock, flags);
	return ret ? PCIBIOS_DEVICE_NOT_FOUND : PCIBIOS_SUCCESSFUL;
}

static int
hndpci_write_config_dword(struct pci_dev *dev, int where, u32 value)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&sih_lock, flags);
	ret = hndpci_write_config(sih, dev->bus->number, PCI_SLOT(dev->devfn),
		PCI_FUNC(dev->devfn), where, &value, sizeof(value));
	spin_unlock_irqrestore(&sih_lock, flags);
	return ret ? PCIBIOS_DEVICE_NOT_FOUND : PCIBIOS_SUCCESSFUL;
}

static struct pci_ops pcibios_ops = {
	hndpci_read_config_byte,
	hndpci_read_config_word,
	hndpci_read_config_dword,
	hndpci_write_config_byte,
	hndpci_write_config_word,
	hndpci_write_config_dword
};

static u32 pci_iobase = 0x100;
static u32 pci_membase = SI_PCI_DMA;

void __init
pcibios_init(void)
{
	ulong flags;

	/* On 4716, use sbtopcie0 to access the device. We
	 * can't use address match 2 (1 GB window) region as MIPS
	 * can not generate 64-bit address on the backplane.
	 */
	if ((sih->chip == BCM4716_CHIP_ID) || (sih->chip == BCM4748_CHIP_ID)) {
		printk("PCI: Using membase %x\n", SI_PCI_MEM);
		pci_membase = SI_PCI_MEM;
	}


	if (!(sih = si_kattach(SI_OSH)))
		panic("si_kattach failed");
	spin_lock_init(&sih_lock);

	spin_lock_irqsave(&sih_lock, flags);
	hndpci_init(sih);
	spin_unlock_irqrestore(&sih_lock, flags);

	set_io_port_base((unsigned long) ioremap_nocache(SI_PCI_MEM, 0x04000000));

	/* Scan the SI bus */
	pci_scan_bus(0, &pcibios_ops, NULL);
}

char * __init
pcibios_setup(char *str)
{
	if (!strncmp(str, "ban=", 4)) {
		hndpci_ban(simple_strtoul(str + 4, NULL, 0));
		return NULL;
	}

	return (str);
}

void __init
pcibios_fixup_bus(struct pci_bus *b)
{
	struct list_head *ln;
	struct pci_dev *d, *dev;
	struct resource *res;
	int pos, size;
	u32 *base, capw;
	u8 irq;

	printk("PCI: Fixing up bus %d\n", b->number);

	/* Fix up SI */
	if (b->number == 0) {
		for (ln = b->devices.next; ln != &b->devices; ln = ln->next) {
			d = pci_dev_b(ln);
			/* Fix up interrupt lines */
			pci_read_config_byte(d, PCI_INTERRUPT_LINE, &irq);
			d->irq = irq + 2;
			pci_write_config_byte(d, PCI_INTERRUPT_LINE, d->irq);
		}
	} else {
		irq = 0;
		/* Find the corresponding IRQ of the PCI/PCIe core per bus number */
		/* All devices on the bus use the same IRQ as the core */
		pci_for_each_dev(dev) {
			if ((dev != NULL) &&
			    ((dev->device == PCI_CORE_ID) ||
			    (dev->device == PCIE_CORE_ID))) {
				if (dev->subordinate && dev->subordinate->number == b->number) {
					irq = dev->irq;
					break;
				}
			}
		}

		pci_membase = hndpci_get_membase(b->number);

		/* Fix up external PCI */
		for (ln = b->devices.next; ln != &b->devices; ln = ln->next) {
			bool is_hostbridge;

			d = pci_dev_b(ln);
			is_hostbridge = hndpci_is_hostbridge(b->number, PCI_SLOT(d->devfn));
			/* Fix up resource bases */
			for (pos = 0; pos < 6; pos++) {
				res = &d->resource[pos];
				base = (res->flags & IORESOURCE_IO) ? &pci_iobase : &pci_membase;
				if (res->end) {
					size = res->end - res->start + 1;
					if (*base & (size - 1))
						*base = (*base + size) & ~(size - 1);
					res->start = *base;
					res->end = res->start + size - 1;
					*base += size;
					pci_write_config_dword(d,
						PCI_BASE_ADDRESS_0 + (pos << 2), res->start);
				}
				/* Fix up PCI bridge BAR0 only */
				if (is_hostbridge)
					break;
			}
			/* Fix up interrupt lines */
			d->irq = irq;
			pci_write_config_byte(d, PCI_INTERRUPT_LINE, d->irq);

			/* If the device is a Broadcom HND device, corerev 18 or higher,
			 * make sure it does not issue requests > 128 bytes.
			 */
			pci_read_config_dword(d, 0x58, &capw);
			if ((capw & 0xff00ff) == 0x780009) {
				/* There is a Vendor Specific Info capability of the
				 * right length at 0x58, preety good bet its an HND
				 * pcie core.
				 */

				pci_read_config_dword(d, 0x5c, &capw);
				printk("HND PCIE device corerev %d found at %d/%d/%d\n",
				       capw, d->bus->number,  PCI_SLOT(d->devfn),
				       PCI_FUNC(d->devfn));
				if (capw >= 18) {
					u32 pciecap;
					u16 devctrl;

					pci_read_config_dword(d, 0xd0, &pciecap);
					pci_read_config_word(d, 0xd8, &devctrl);
					if (pciecap == 0x10010) {
						u16 new = devctrl & ~0x7000;

						if (devctrl != new) {
							printk("  Setting DevCtrl to 0x%x "
								"(was 0x%x)\n", new, devctrl);
							pci_write_config_word(d, 0xd8, new);
						} else
							printk("  DevCtrl is ok: 0x%x\n", new);
					} else
						printk(" ERROR: PCIECap is not at 0xd0\n");
				}
			}
		}
		hndpci_arb_park(sih, PCI_PARK_NVRAM);
	}
}

unsigned int
pcibios_assign_all_busses(void)
{
	return 1;
}

void
pcibios_align_resource(void *data, struct resource *res,
	unsigned long size, unsigned long align)
{
}

int
pcibios_enable_resources(struct pci_dev *dev)
{
	u16 cmd, old_cmd;
	int idx;
	struct resource *r;

	/* External PCI only */
	if (dev->bus->number == 0)
		return 0;

	pci_read_config_word(dev, PCI_COMMAND, &cmd);
	old_cmd = cmd;
	for (idx = 0; idx < 6; idx++) {
		r = &dev->resource[idx];
		if (r->flags & IORESOURCE_IO)
			cmd |= PCI_COMMAND_IO;
		if (r->flags & IORESOURCE_MEM)
			cmd |= PCI_COMMAND_MEMORY;
	}
	if (dev->resource[PCI_ROM_RESOURCE].start)
		cmd |= PCI_COMMAND_MEMORY;
	if (cmd != old_cmd) {
		printk("PCI: Enabling device %s (%04x -> %04x)\n", dev->slot_name, old_cmd, cmd);
		pci_write_config_word(dev, PCI_COMMAND, cmd);
	}
	return 0;
}

int
pcibios_enable_device(struct pci_dev *dev, int mask)
{
	ulong flags;
	uint coreidx;
	void *regs;

	/* External PCI device enable */
	if (dev->bus->number != 0)
		return pcibios_enable_resources(dev);

	/* These cores come out of reset enabled */
	if (dev->device == MIPS_CORE_ID ||
	    dev->device == MIPS33_CORE_ID ||
	    dev->device == MIPS74K_CORE_ID ||
	    dev->device == CC_CORE_ID)
		return 0;

	spin_lock_irqsave(&sih_lock, flags);
	coreidx = si_coreidx(sih);
	regs = si_setcoreidx(sih, PCI_SLOT(dev->devfn));
	if (!regs)
		return -ENODEV;

	/* 
	 * The USB core requires a special bit to be set during core
	 * reset to enable host (OHCI) mode. Resetting the SI core in
	 * pcibios_enable_device() is a hack for compatibility with
	 * vanilla usb-ohci so that it does not have to know about
	 * SI. A driver that wants to use the USB core in device mode
	 * should know about SI and should reset the bit back to 0
	 * after calling pcibios_enable_device().
	 */
	if (si_coreid(sih) == USB_CORE_ID) {
		si_core_disable(sih, si_core_cflags(sih, 0, 0));
		si_core_reset(sih, 1 << 13, 0);
	}
	/*
	 * USB 2.0 special considerations:
	 *
	 * 1. Since the core supports both OHCI and EHCI functions, it must
	 *    only be reset once.
	 *
	 * 2. In addition to the standard SI reset sequence, the Host Control
	 *    Register must be programmed to bring the USB core and various
	 *    phy components out of reset.
	 */
	else if (si_coreid(sih) == USB20H_CORE_ID) {
		if (!si_iscoreup(sih)) {
			si_core_reset(sih, 0, 0);
			mdelay(10);
			if (si_corerev(sih) >= 5) {
				uint32 tmp;
				/* Enable Misc PLL */
				tmp = readl((uintptr)regs + 0x1e0);
				tmp |= 0x100;
				writel(tmp, (uintptr)regs + 0x1e0);
				SPINWAIT((((tmp = readl((uintptr)regs + 0x1e0)) & (1 << 24))
					== 0), 1000);
				printk("USB20H misc PLL 0x%08x\n", tmp);

				/* Take out of resets */
				writel(0x4ff, (uintptr)regs + 0x200);
				udelay(25);
				writel(0x6ff, (uintptr)regs + 0x200);
				udelay(25);

				writel(0x2b, (uintptr)regs + 0x524);	/* turn on mdio clock */
				udelay(50);
				writel(0x10ab, (uintptr)regs + 0x524);	/* write mdio address */
				udelay(50);

				/* make sure clock is locked */
				{
					int us = 200000;
					int countdown = (us) + 9;
					while ((((tmp = readl((uintptr)regs + 0x528)) & 0xc000)
						!= 0xc000) && (countdown >= 10)) {
						udelay(50);
						/* clear mdio data */
						writel(0x80000000, (uintptr)regs + 0x528);
						udelay(50);
						/* write mdio address */
						writel(0x10ab, (uintptr)regs + 0x524);
						OSL_DELAY(10);
						countdown -= 110;
					}

					if (countdown < 10)
						return -ENODEV;
				}

				/* checking for desired value */
				if ((tmp & 0xc000) != 0xc000) {
					printk("WARNING! USB20H mdio_rddata 0x%08x\n", tmp);
				}
				udelay(50);
				/* clear mdio data */
				writel(0x80000000, (uintptr)regs + 0x528);
				udelay(50);

				writel(0x7ff, (uintptr)regs + 0x200);
				udelay(10);

				/* Take USB and HSIC out of non-driving modes */
				writel(0, (uintptr)regs + 0x510);
			} else {
				writel(0x7ff, (uintptr)regs + 0x200);
				udelay(1);
			}
		}
				/* War for 4716 failures. */
		if ((sih->chip == BCM4716_CHIP_ID) ||
			(sih->chip == BCM4748_CHIP_ID)) {
			uint32 tmp;
			uint32 delay = 500;
			uint32 val = 0;
			uint32 clk_freq;

			clk_freq = si_cpu_clock(sih);
			if (clk_freq >= 480000000)
				val = 0x1846b; /* set CDR to 0x11(fast) */
			else if (clk_freq == 453000000)
				val = 0x1046b; /* set CDR to 0x10(slow) */

			/* Change Shim mdio control reg to fix host not acking at high frequencies
			*/
			if (val) {
				writel(0x1, (uintptr)regs + 0x524); /* write sel to enable */
				udelay(delay);

				writel(val, (uintptr)regs + 0x524);
				udelay(delay);
				writel(0x4ab, (uintptr)regs + 0x524);
				udelay(delay);
				tmp = readl((uintptr)regs + 0x528);
				printk("USB20H mdio control register : 0x%x\n", tmp);
				writel(0x80000000, (uintptr)regs + 0x528);
			}


		}

		/* War for 5354 failures. */
		if ((si_corerev(sih) == 2) && (sih->chip == BCM5354_CHIP_ID)) {
			uint32 tmp;

			tmp = readl((uintptr)regs + 0x400);
			tmp &= ~8;
			writel(tmp, (uintptr)regs + 0x400);
			tmp = readl((uintptr)regs + 0x400);
			printk("USB20H fcr: 0x%x\n", tmp);

			tmp = readl((uintptr)regs + 0x304);
			tmp &= ~0x100;
			writel(tmp, (uintptr)regs + 0x304);
			tmp = readl((uintptr)regs + 0x304);
			printk("USB20H shim cr: 0x%x\n", tmp);

			tmp = 0x00fe00fe;
			writel(tmp, (uintptr)regs + 0x894);
			tmp = readl((uintptr)regs + 0x894);
			printk("USB20H syn01 register : 0x%x\n", tmp);

			tmp = readl((uintptr)regs + 0x89c);
			tmp |= 0x1;
			writel(tmp, (uintptr)regs + 0x89c);
			tmp = readl((uintptr)regs + 0x89c);
			printk("USB20H syn03 register : 0x%x\n", tmp);
		}
	} else
		si_core_reset(sih, 0, 0);

	si_setcoreidx(sih, coreidx);
	spin_unlock_irqrestore(&sih_lock, flags);

	return 0;
}

void
pcibios_update_resource(struct pci_dev *dev, struct resource *root,
	struct resource *res, int resource)
{
	unsigned long where, size;
	u32 reg;

	/* External PCI only */
	if (dev->bus->number == 0)
		return;

	where = PCI_BASE_ADDRESS_0 + (resource * 4);
	size = res->end - res->start;
	pci_read_config_dword(dev, where, &reg);
	reg = (reg & size) | (((u32)(res->start - root->start)) & ~size);
	pci_write_config_dword(dev, where, reg);
}

static void __init
quirk_hndpci_bridge(struct pci_dev *dev)
{
	if (dev->bus->number != 1 || PCI_SLOT(dev->devfn) != 0)
		return;

	printk("PCI: Fixing up bridge\n");

	/* Enable PCI bridge bus mastering and memory space */
	pci_set_master(dev);
	pcibios_enable_resources(dev);

	/* Enable PCI bridge BAR1 prefetch and burst */
	pci_write_config_dword(dev, PCI_BAR1_CONTROL, 3);
}

struct pci_fixup pcibios_fixups[] = {
	{ PCI_FIXUP_HEADER, PCI_ANY_ID, PCI_ANY_ID, quirk_hndpci_bridge },
	{ 0 }
};
