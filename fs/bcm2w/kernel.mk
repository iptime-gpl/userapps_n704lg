$(info include $(notdir $(lastword $(MAKEFILE_LIST))))

TARGET = $(shell cat $(USERAPPS_ROOT)/.product_name)

.PHONY : DUMMY
DUMMY:

include $(USERAPPS_ROOT)/misc_config
include $(USERAPPS_ROOT)/rootfs/clones/$(TARGET)/clone_info.mk
include $(USERAPPS_ROOT)/rootfs/kernel_info.mk

ifeq ($(.DEFAULT_GOAL),DUMMY)
.DEFAULT_GOAL:=all
endif

check_defined = $(strip $(foreach 1,$1, $(call __check_defined,$1,$(strip $(value 2)))))
__check_defined = $(if $(value $1),, $(error Undefined $1$(if $2, ($2))))

$(call check_defined,KERNEL_PATH LINUX_PATH ROUTER_PATH KCONFIG_FILE_NAME KERNEL_NAME)

MAKE_KERNEL:= kernel install
.PHONY : $(MAKE_KERNEL)
all: $(MAKE_KERNEL)

kernel:
	$(MAKE) efm_make -C $(KERNEL_PATH)/$(ROUTER_PATH)

install:
	@cp $(KERNEL_PATH)/$(ROUTER_PATH)/compressed/vmlinuz $(KERNEL_NAME)

