$(info include $(notdir $(lastword $(MAKEFILE_LIST))))

KERNEL_FILENAME:=vmlinuz.$(BRCM_SDK_VERSION)$(if $(KERNEL_POSTFIX),.$(KERNEL_POSTFIX))
KERNEL_NAME:=./prebuilt/kernel/$(KERNEL_FILENAME)
KMODULE_DIR:=../../bcmapp/sdk/$(BRCM_SDK_VERSION)/modules/2.6.36.4brcmarm$(if $(KERNEL_POSTFIX),.$(KERNEL_POSTFIX))
KERNEL_PATH:=../../linux

