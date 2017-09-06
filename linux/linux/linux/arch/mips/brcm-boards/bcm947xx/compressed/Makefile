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
# $Id: Makefile 241435 2011-02-18 04:04:21Z stakita $
#


# Link at 3 MB offset in RAM
TEXT_START	?= 0x80300000

OBJCOPY		:= $(CROSS_COMPILE)objcopy -O binary -R .reginfo -R .note -R .comment -R .mdebug -S
OBJCOPYSREC	:= $(CROSS_COMPILE)objcopy -O srec -R .reginfo -R .note -R .comment -R .mdebug -S
OBJCOPYQT	:= $(CROSS_COMPILE)objcopy -O swapqt --qt-len 1 -R .reginfo -R .note -R .comment -R .mdebug -S

vpath %.c $(SRCBASE)/shared
vpath %.S $(SRCBASE)/shared
vpath %.lds.in $(SRCBASE)/shared

ASFLAGS		+= -mips32 -D__ASSEMBLY__ -DLOADADDR=$(LOADADDR)
CFLAGS		+= -DLOADADDR=$(LOADADDR)
# The self-decompresor is standalone, get rid of the linux flags
CFLAGS		:= $(subst -Dlinux,,$(CFLAGS))
CFLAGS		+= -Ulinux -D_MINOSL_ -DBCMBUSTYPE=SI_BUS -ffreestanding
ifdef CONFIG_MCOUNT
CFLAGS		:= $(subst -pg,,$(CFLAGS))
endif
CFLAGS		+= -ffunction-sections $(call check_gcc, -fvtable-gc, )

SYSTEM		?= $(TOPDIR)/vmlinux
OBJECTS		:= boot.o sisdram.o sbsdram.o aisdram.o \
		load.o hndmips.o hndchipc.o \
		siutils.o sbutils.o aiutils.o hndpmu.o \
		min_osl.o bcmutils.o sromstubs.o nvramstubs.o bcmstdlib.o bcmotp.o

ifdef CONFIG_MTD_NFLASH 
OBJECTS		+= nflash.o
endif

ifdef CONFIG_SQUASHFS
# Default to lzma
COMPRESS	?= lzma e
CFLAGS		+= -DUSE_LZMA -D_LZMA_NO_SYSTEM_SIZE_T -DBCMLZMA -I$(SRCBASE)/tools/misc/lzma_src/C

else
# Default to bzip2
COMPRESS	?= bzip2 -c

ifneq ($(findstring gzip,$(COMPRESS)),)
CFLAGS		+= -DUSE_GZIP
else
ifneq ($(findstring bzip2,$(COMPRESS)),)
CFLAGS		+= -DUSE_BZIP2
else
COMPRESS	:= cat
endif
endif
endif


#CFLAGS		+= -DBCMDBG

all: zImage vmlinuz

ifdef CONFIG_SQUASHFS
all : vmlinuz-lzma
endif

# Don't build dependencies, this may die if $(CC) isn't gcc
dep:

zImage: vmlinux
	$(OBJCOPY) $< $@
#	$(OBJCOPYQT) $< $@.qt

# Link the loader and the kernel binary together
vmlinux: vmlinux.lds $(OBJECTS) piggy.o
	$(LD) -static --gc-sections -no-warn-mismatch -T vmlinux.lds -o $@ $(OBJECTS) piggy.o

vmlinux.lds: hndrte.lds.in Makefile
	@sed -e s/TEXT_START/$(TEXT_START)/ \
	     -e s/TARGET_ARCH/mips/ < $< > $@

# Create a linkable version of the (possibly compressed) kernel binary
piggy.o: piggz piggy.lds
	$(LD) -no-warn-mismatch -T piggy.lds -r -o $@ -b binary piggz -b elf32-tradlittlemips

piggy.lds:
	@echo "SECTIONS { .data : { input_len = .; LONG(input_data_end - input_data) input_data = .; *(.data) input_data_end = .; }}" > $@

# Always create a gzipped version named vmlinuz for compatibility
vmlinuz: piggy
	gzip -c9 $< > $@

piggz: piggy
ifdef CONFIG_SQUASHFS
	$(COMPRESS) $< $@
else
	$(COMPRESS) $< > $@
endif

ifdef CONFIG_SQUASHFS
# Create an lzma version named vmlinuz-lzma
vmlinuz-lzma: piggy
#	lzma e $<  $@
	/bin/lzma -k -1 -c -z $<  > $@
endif

piggy: $(SYSTEM)
	$(OBJCOPY) $< $@

mrproper: clean

clean:
	rm -f vmlinux vmlinuz zImage piggz piggy *.lds *.o vmlinuz-lzma
	-rm -f zImage.qt
