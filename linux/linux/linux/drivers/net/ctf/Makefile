#
# Makefile for Broadcom Cut-Thru Forwarding
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

O_TARGET	:= ctf.o

CTF_OBJS	:= hndctf.o hndctf_linux.o


export-objs	:= ctf_ksyms.o
obj-y		:= ctf_ksyms.o $(CTF_OBJS)
obj-m		:= $(O_TARGET)

ifneq ($(wildcard $(SRCBASE)/router/ctf/*.c),)
EXTRA_CFLAGS	+= -I$(SRCBASE)/router/ctf
vpath %.c $(SRCBASE)/router/ctf
else
obj-y		:= $(SRCBASE)/router/ctf/linux/ctf.o
endif

include $(TOPDIR)/Rules.make

ctf_ksyms.c: ctf_ksyms.sh $(CTF_OBJS)
	sh -e $< $(CTF_OBJS) > $@

clean:
	rm -f core *.o *.a *.s
