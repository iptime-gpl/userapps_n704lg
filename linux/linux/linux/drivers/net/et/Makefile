#
# Makefile for the Broadcom et driver
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

O_TARGET	:= et.o

ET_OBJS		:= et_linux.o etc.o

ifeq ($(CONFIG_ET_47XX),y)
ET_OBJS		+= etc47xx.o etc_adm.o etcgmac.o
EXTRA_CFLAGS	+= -DCFG_GMAC -DETROBO -DETADM -DBCMDMA64 -DBCMDMA32
endif

export-objs	:=
obj-y		:= $(ET_OBJS)
obj-m		:= $(O_TARGET)

EXTRA_CFLAGS	+= -DDMA -Werror

# Search for sources under src/et/sys or objects under src/et/linux
ifneq ($(wildcard $(SRCBASE)/et/sys),)
EXTRA_CFLAGS	+= -I$(SRCBASE)/et/sys
vpath %.c $(SRCBASE)/et/sys $(SRCBASE)/shared
else
#obj-y		:= $(foreach obj,$(ET_OBJS),$(SRCBASE)/et/linux/$(obj))
obj-y		:= $(SRCBASE)/et/linux/et.o
endif

include $(TOPDIR)/Rules.make
