$(info include $(notdir $(lastword $(MAKEFILE_LIST))))

include clone_info.mk
include kernel_info.mk

MAKE_KERNEL:= __kernel__ __install__
.PHONY : $(MAKE_KERNEL)
kernel: $(MAKE_KERNEL)

__kernel__:
	yes "" | $(MAKE) -C $(KERNEL_PATH)/linux/linux oldconfig
	[ $(shell sed -n 's/^VERSION\s*=\s*\([0-9]\).*/\1/p' $(KERNEL_PATH)/linux/linux/Makefile) -ge 3 ] || $(MAKE) dep -C $(KERNEL_PATH)/linux/linux 
	$(MAKE) zImage -C $(KERNEL_PATH)/linux/linux

__install__:
	@cp $(KERNEL_PATH)/linux/linux/arch/mips/brcm-boards/bcm947xx/compressed/vmlinuz-lzma ./$(KERNEL_FILENAME)
