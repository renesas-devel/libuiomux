LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_C_INCLUDES := \
	external/libuiomux/include \

#LOCAL_CFLAGS := -DDEBUG
LOCAL_CFLAGS := -DVERSION=\"1.0.0\"

ifneq ($(UIO_MAX_MAP_MEM),)
        LOCAL_CFLAGS += -DUIO_BUFFER_MAX="($(UIO_MAX_MAP_MEM) << 20)"
endif

LOCAL_SRC_FILES := \
	uio.c \
	uiomux.c

LOCAL_SHARED_LIBRARIES += libbinder libcutils libutils

LOCAL_MODULE := libuiomux
LOCAL_MODULE_TAGS := optional
LOCAL_PRELINK_MODULE := false
include $(BUILD_SHARED_LIBRARY)
