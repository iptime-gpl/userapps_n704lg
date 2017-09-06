#
# Makefile for the Broadcom IGS Layer
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
#

O_TARGET	:= igs.o
IGS_OBJS	:= igsc.o igsc_sdb.o igs_linux.o osl_linux.o

# Search for sources under src/router/emf/igs
ifneq ($(wildcard $(SRCBASE)/router/emf/igs),)
	EXTRA_CFLAGS	+= -I$(SRCBASE)/include/emf/igs -I$(SRCBASE)/include/emf/emf
	EXTRA_CFLAGS	+= -I$(SRCBASE)/router/emf/igs -I$(SRCBASE)/router/emf/emf
	vpath %.c $(SRCBASE)/emf/igs
	vpath %.c $(SRCBASE)/router/emf/igs
	export-objs	:=
	obj-y		:= $(IGS_OBJS)
	obj-m		:= $(O_TARGET)
else
	obj-y		:= $(SRCBASE)/router/emf/igs.o
	obj-m		:= $(O_TARGET)
endif

# Enable IGMP WAR only for Media router builds
ifeq ($(CONFIG_WL_USE_APSTA),y)
ifeq ($(CONFIG_WL_APSTA),"wlconfig_lx_router_media")
EXTRA_CFLAGS += -D__CONFIG_MEDIA_IPTV__ -DMEDIAROOM_IGMP_WAR
endif
endif


include $(TOPDIR)/Rules.make
