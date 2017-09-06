#
# Makefile for the Broadcom EMF Layer
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

O_TARGET	:= emf.o
EMF_OBJS	:= emfc.o emf_linux.o

# Search for sources under src/router/emf/emf
ifneq ($(wildcard $(SRCBASE)/router/emf/emf),)
	EXTRA_CFLAGS	+= -I$(SRCBASE)/include/emf/emf -I$(SRCBASE)/include/emf/igs
	EXTRA_CFLAGS	+= -I$(SRCBASE)/router/emf/emf -I$(SRCBASE)/router/emf/igs
	vpath %.c $(SRCBASE)/emf/emf
	vpath %.c $(SRCBASE)/router/emf/emf
	export-objs	:=
	obj-y		:= $(EMF_OBJS)
	obj-m		:= $(O_TARGET)
else
	obj-y		:= $(SRCBASE)/router/emf/emf.o
	obj-m		:= $(O_TARGET)
endif

include $(TOPDIR)/Rules.make
