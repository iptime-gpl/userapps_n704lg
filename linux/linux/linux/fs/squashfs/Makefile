#
# Makefile for the linux squashfs routines.
#
O_TARGET := squashfs.o

obj-y  := inode.o squashfs2_0.o sqlzma.o

obj-m := $(O_TARGET)

CFLAGS_sqlzma.o += -I $(SRCBASE)/tools/misc/lzma_src/C -DBCMLZMA

ifeq ($(CONFIG_SHRINKMEM),y)
EXTRA_CFLAGS += -DCONFIG_SQUASHFS_VMALLOC
endif

include $(TOPDIR)/Rules.make
