TARGET := PrinterDemo
ROOT := $(abspath ../..)
O ?= $(ROOT)/output/build/app/$(TARGET)
CPU_ABI ?= armeabi-v7a-hard
NDK_PATH ?= $(ROOT)/toolchain/ndk
SDK_PATH ?= $(ROOT)/toolchain/sdk
APP_C_SDK_PATH ?= $(ROOT)/toolchain/app_c_sdk
TOOLS_PATH ?= $(ROOT)/toolchain/tools
INSTALL_PATH ?= $(ROOT)/output/system
NDK_CFLAGS += -fno-short-enums
export APP_C_SDK_PATH
NPK_STAGE := $(abspath $(O))/npk
OBJDUMP := $(NDK_PATH)/toolchains/arm-none-eabi-hf7.2.1/prebuilt/linux-x86/bin/arm-none-eabi-objdump

.DEFAULT_GOAL := all
.PHONY: all pack check clean

all:
	$(NDK_PATH)/ndk-build $(NDK_BUILD_FLAGS) APP_ABI=$(CPU_ABI) NDK_PROJECT_PATH=. \
	    APP_BUILD_SCRIPT=./Android.mk NDK_OUT=$(O)/obj NDK_LIBS_OUT=$(O)/libs \
	    SDK_PATH=$(SDK_PATH) NDK_CFLAGS="$(NDK_CFLAGS)" MODULE_NAME=$(TARGET)
	mv "$(O)/obj/local/$(CPU_ABI)/$(TARGET)" "$(O)/obj/local/$(CPU_ABI)/$(TARGET).elf"
	$(OBJDUMP) -l -x -d "$(O)/obj/local/$(CPU_ABI)/$(TARGET).elf" > "$(O)/obj/local/$(CPU_ABI)/$(TARGET).dmp"
	$(TOOLS_PATH)/fixsize -a $(TARGET) "$(O)/obj/local/$(CPU_ABI)/$(TARGET).bin"
	mkdir -p "$(INSTALL_PATH)"
	cp "$(O)/obj/local/$(CPU_ABI)/$(TARGET).bin" "$(INSTALL_PATH)/$(TARGET).bin"
	$(MAKE) --no-print-directory pack

pack:
	test -s "$(INSTALL_PATH)/$(TARGET).bin"
	test -s resources/certs/stg/ca.pem
	test -s resources/certs/stg/backend-ca.pem
	test -s resources/certs/stg/starfield-g2.pem
	test -s resources/certs/stg/starfield-class2.pem
	test -s resources/certs/stg/amazon-cross.pem
	test -s resources/certs/stg/cil.crt
	test -s resources/certs/stg/pri.key
	rm -rf "$(NPK_STAGE)"
	install -d -m 755 "$(NPK_STAGE)/$(TARGET)" "$(NPK_STAGE)/Desktop"
	cp "$(INSTALL_PATH)/$(TARGET).bin" "$(NPK_STAGE)/$(TARGET)/"
	cp resources/logo.png "$(NPK_STAGE)/$(TARGET)/logo.png"
	cp Desktop/PrinterDemo.desktop "$(NPK_STAGE)/Desktop/"
	install -d -m 755 "$(NPK_STAGE)/$(TARGET)/resources" "$(NPK_STAGE)/$(TARGET)/resources/certs" "$(NPK_STAGE)/$(TARGET)/resources/certs/stg"
	install -m 644 resources/certs/stg/ca.pem resources/certs/stg/starfield-g2.pem resources/certs/stg/starfield-class2.pem resources/certs/stg/amazon-cross.pem resources/certs/stg/cil.crt resources/certs/stg/pri.key "$(NPK_STAGE)/$(TARGET)/resources/certs/stg/"
	install -m 644 resources/certs/stg/backend-ca.pem "$(NPK_STAGE)/$(TARGET)/resources/certs/stg/"
	cp -r resources/images "$(NPK_STAGE)/$(TARGET)/resources/"
	cd "$(NPK_STAGE)" && umask 077 && zip -rq "$(TARGET).npk" "$(TARGET)" Desktop
	mv "$(NPK_STAGE)/$(TARGET).npk" "$(INSTALL_PATH)/$(TARGET).npk"
	chmod 600 "$(INSTALL_PATH)/$(TARGET).npk"
	rm -rf "$(NPK_STAGE)"

check:
	$(MAKE) -C tests check

clean:
	test -n "$(O)" && test "$(abspath $(O))" != / && rm -rf "$(O)"