include $(USERAPPS_ROOT)/config
-include $(USERAPPS_ROOT)/reg_config
include $(USERAPPS_ROOT)/rootfs/clone_info.mk
ifeq ($(USE_CUSTOM_VERSION),y)
include $(USERAPPS_ROOT)/rootfs/clones/$(TARGET)/version.mk
else
include $(USERAPPS_ROOT)/rootfs/version.mk
endif
include $(USERAPPS_ROOT)/lang_profile
include $(USERAPPS_ROOT)/rootfs/kernel_info.mk

ROOT_DIR:=root
PLUGIN_DIR:=./plugin



MODULE_SRC_DIR:=
MODULE_DEST_DIR:=

STRIP:=arm-brcm-linux-uclibcgnueabi-strip


$(TARGET): target.fs image


post_targetfs:
	@echo -e "\t--->Post processing..." 

ROOTFS_IMG=rootfs.lzma
CHIPSET_APP_INSTALL_DIR:=bcmapp
CLIB_DIR:=fs/$(PROJECT_ID)/clib
IPTABLES_BIN_PATH:=$(USERAPPS_ROOT)/iptables-1.2.7a
IPTABLES_BINS:=iptables
STRIP_OPTION:=
LDCONFIG_CMD:=/sbin/ldconfig -r $(ROOT_DIR)/default
MAKE_FS_BIANRY_CMD:=./mkcramfs $(ROOT_DIR) $(ROOTFS_IMG)


include $(USERAPPS_ROOT)/mkscripts/target.mk


BOOT_FILE:=cfez-gmac.bin.5357c0
MAX_FIRMWARE_SIZE:=0x3f0000

# Make firmware
BOOT_BIN_PATH:=./$(BOOT_FILE)
ifeq ($(ENGLISH_DEFAULT_NVRAM),y)
NVRAM_FILENAME:=nvram.5357c0.en.txt
else
NVRAM_FILENAME:=nvram.5357c0.txt
endif
NVRAM_FILE:=./clones/$(TARGET)/$(NVRAM_FILENAME)

CFE_NV_IMG:=./clones/$(TARGET)/$(TARGET)_xboot.bin
TRX_NAME:=linux.trx

FIRMWARE_NAME:=n704lg_kr_90_023.bin
FINAL_FIRMWARE_NAME:=n704lg_kr_10_023.bin

image:
	@echo "--->Making firmware..."
	@trx -o $(TRX_NAME) $(KERNEL_FILENAME) $(ROOTFS_IMG);
	@nvserial -i $(BOOT_BIN_PATH) -o $(CFE_NV_IMG) $(NVRAM_FILE)
	@./makefirm -t trx -a $(PRODUCT_ID) -k $(TRX_NAME) -v 0_00 -l $(LANGUAGE_POSTFIX) -b $(CFE_NV_IMG) -s 20000 -f $(MAX_FIRMWARE_SIZE) >>mk.log.$(PRODUCT_ID)
	@mv $(PRODUCT_ID)_$(LANGUAGE_POSTFIX)_0_00.bin binary/$(FINAL_FIRMWARE_NAME).boot
	@./makefirm -t trx -a $(PRODUCT_ID) -k $(TRX_NAME) -v $(F_MAJOR_VER)_$(MINOR_VER) -l $(LANGUAGE_POSTFIX) -b $(CFE_NV_IMG) -s 20000 -f $(MAX_FIRMWARE_SIZE) >>mk.log.$(PRODUCT_ID)
	@./firmware_size_check.sh $(FIRMWARE_NAME) $(MAX_FIRMWARE_SIZE)
	@mv $(FIRMWARE_NAME)* binary/$(FINAL_FIRMWARE_NAME)
	@echo -e "\n---------------------------------------------------------------------\n"
	@echo -e "\tFirmware Name   : binary / $(FINAL_FIRMWARE_NAME)"
	@echo -e "\tTo upgrade boot : binary / $(FINAL_FIRMWARE_NAME).boot"
	@echo -e "\n---------------------------------------------------------------------"
	@rm -rf $(ROOTFS_IMG)
#	rm -rf root
