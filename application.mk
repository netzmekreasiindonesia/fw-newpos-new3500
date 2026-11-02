include $(BUILDCONFIG_MK_FILE)
include $(CLEAR_VARS)

LOCAL_MODULE := $(MODULE_NAME)
LOCAL_SRC_FILES := $(call all-c-files-under,src)
LOCAL_SRC_FILES += $(LOCAL_PATH)/../NetzmeSB/src/libs/logger/logger.c \
    $(LOCAL_PATH)/../NetzmeSB/src/libs/logger/logger_uart.c \
    $(LOCAL_PATH)/../NetzmeSB/src/libs/logger/logger_file.c
LOCAL_SRC_FILES += $(SDK_PATH)/include/finfo.c
LOCAL_C_INCLUDES := $(LOCAL_PATH)/include $(LOCAL_PATH)/config \
    $(SDK_PATH)/include $(SDK_PATH)/include/lvgl $(APP_C_SDK_PATH)/include \
    $(LOCAL_PATH)/../NetzmeSB/src $(LOCAL_PATH)/../NetzmeSB/src/libs/logger \
    $(LOCAL_PATH)/../NetzmeSB/src/libs/init \
    $(LOCAL_PATH)/../NetzmeSB/include
LOCAL_CFLAGS := $(NDK_CFLAGS) -std=gnu99 -Wall -include $(LOCAL_PATH)/config/conf.h \
    -DCFG_APP_NAME=\"$(MODULE_NAME)\" -DCFG_APP_PATH=\"/home/user0/$(MODULE_NAME)/\" \
    -DPOSIX_INTERFACE_OK -DCONFIG_HARDAWRE_USRAPP -DLV_LVGL_H_INCLUDE_SIMPLE \
    -DLV_CONF_INCLUDE_SIMPLE -DLV_EX_CONF_INCLUDE_SIMPLE 
LOCAL_CFLAGS += -DCFG_DEVELOPMENT_TEST
LOCAL_CFLAGS += $(APP_WIRELESS_FLAG)
LOCAL_LDFLAGS := -nostartfiles -Wl,--build-id=none,--cref \
    -Wl,-Map=$(NDK_OUT)/local/$(APP_ABI)/$(LOCAL_MODULE).map \
    -T$(LOCAL_PATH)/gcc_application.ld -L$(SDK_PATH)/lib -L$(APP_C_SDK_PATH)/lib
LOCAL_LDFLAGS += -lsyscall -lalioshttp -lseos -lwolfssl -lcjson \
    -Wl,--whole-archive -lsystemui -lhardware -llvgl \
    -lpos -lfreetype -lpng -lz -Wl,--no-whole-archive -lm

include $(BUILD_EXECUTABLE_BINARY)