#
# Makefile for Broadcom BCM947XX boards
#
# Copyright (C) 2010, Broadcom Corporation. All Rights Reserved.
# 
# Permission to use, copy, modify, and/or distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
# 
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
# SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
# OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
# CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
#
# $Id: Makefile,v 1.1.1.1 2012/08/29 05:42:23 bcm5357 Exp $
#

O_TARGET	:= bcm947xx.o

export-objs	:= nvram_linux.o setup.o
obj-y		:= prom.o setup.o time.o irq.o
obj-y		+= hndmips.o hndchipc.o perfcntr.o linux_gpio.o
obj-y		+= sflash.o nvram.o nvram_linux.o
obj-y		+= clkfreq.o
obj-$(CONFIG_PCI) += hndpci.o pcibios.o

vpath %.c $(SRCBASE)/shared $(SRCBASE)/shared/nvram

ifeq ($(CONFIG_SHRINKMEM),y)
EXTRA_CFLAGS += -DCONFIG_NVRAM_VMALLOC
endif

ifeq ($(CONFIG_MTD_BCMCONF_PARTS),y)
EXTRA_CFLAGS += -DBCMCONFMTD
endif

obj-$(CONFIG_MTD_NFLASH) += nflash.o


include $(TOPDIR)/Rules.make
