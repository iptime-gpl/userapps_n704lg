#
# Makefile for Broadcom Home Networking Division (HND) shared driver code
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

O_TARGET	:= hnd.o

WLSHARED        := wlconfig_lx_shared
WLCFGDIR        := $(SRCBASE)/wl/config

ifneq ($(wildcard $(WLCFGDIR)),)
include         $(WLCFGDIR)/$(WLSHARED)
include         $(WLCFGDIR)/wl.mk
endif

HND_OBJS	:= $(sort $(WLFILES:.c=.o))

EXTRA_CFLAGS += $(WLFLAGS)

ifneq ($(CONFIG_BCM947XX),y)
HND_OBJS	+= nvramstubs.o
endif

ifeq ($(CONFIG_ET_47XX),y)
HND_OBJS	+= bcmrobo.o
else
ifeq ($(CONFIG_BCM57XX),y)
HND_OBJS	+= bcmrobo.o
endif
endif

ifdef CONFIG_USB_RNDIS
EXTRA_CFLAGS += -DCONFIG_USBRNDIS_RETAIL
endif                                  

ifeq ($(BCMDBG_MEM),1)
EXTRA_CFLAGS += -DBCMDBG_MEM
endif

export-objs	:= shared_ksyms.o
obj-y		:= shared_ksyms.o $(HND_OBJS)
obj-m		:= $(O_TARGET)

vpath %.c $(SRCBASE)/shared $(SRCBASE)/shared/nvram

include $(TOPDIR)/Rules.make

ifeq ($(wildcard $(SRCBASE)/shared/bcmutils.c),)
bcmutils.o: $(SRCBASE)/shared/linux/bcmutils.o
	cp $< $@
endif

ifeq ($(wildcard $(SRCBASE)/shared/bcmrobo.c),)
bcmutils.o: $(SRCBASE)/shared/linux/bcmrobo.o
	cp $< $@
endif

ifeq ($(wildcard $(SRCBASE)/shared/hnddma.c),)
hnddma.o: $(SRCBASE)/shared/linux/hnddma.o
	cp $< $@
endif

ifeq ($(wildcard $(SRCBASE)/shared/linux_osl.c),)
linux_osl.o: $(SRCBASE)/shared/linux/linux_osl.o
	cp $< $@
endif

ifeq ($(wildcard $(SRCBASE)/shared/siutils.c),)
siutils.o: $(SRCBASE)/shared/linux/siutils.o
	cp $< $@
endif

ifeq ($(wildcard $(SRCBASE)/shared/sbutils.c),)
sbutils.o: $(SRCBASE)/shared/linux/sbutils.o
	cp $< $@
endif

ifeq ($(wildcard $(SRCBASE)/shared/aiutils.c),)
aiutils.o: $(SRCBASE)/shared/linux/aiutils.o
	cp $< $@
endif

ifeq ($(wildcard $(SRCBASE)/shared/hndpmu.c),)
hndpmu.o: $(SRCBASE)/shared/linux/hndpmu.o
	cp $< $@
endif

ifeq ($(wildcard $(SRCBASE)/shared/bcmsrom.c),)
bcmsrom.o: $(SRCBASE)/shared/linux/bcmsrom.o
	cp $< $@
endif

ifeq ($(wildcard $(SRCBASE)/shared/nvramstubs.c),)
nvramstubs.o: $(SRCBASE)/shared/linux/nvramstubs.o
	cp $< $@
endif

ifeq ($(wildcard $(SRCBASE)/shared/bcmwpa.c),)
bcmwpa.o: $(SRCBASE)/shared/linux/bcmwpa.o
	cp $< $@
endif

ifeq ($(wildcard $(SRCBASE)/shared/bcmotp.c),)
bcmotp.o: $(SRCBASE)/shared/linux/bcmotp.o
	cp $< $@
endif

ifeq ($(wildcard $(SRCBASE)/shared/nicpci.c),)
nicpci.o: $(SRCBASE)/shared/linux/nicpci.o
	cp $< $@
endif

shared_ksyms.c: shared_ksyms.sh $(HND_OBJS)
	sh -e $< $(HND_OBJS) > $@
