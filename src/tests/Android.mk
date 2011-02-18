LOCAL_PATH:= $(call my-dir)

# noop
include $(CLEAR_VARS)
LOCAL_C_INCLUDES := external/libuiomux/include
LOCAL_CFLAGS := -DVERSION=\"1.0.0\"
LOCAL_SRC_FILES := noop.c
LOCAL_SHARED_LIBRARIES := libuiomux
LOCAL_MODULE := noop
LOCAL_MODULE_TAGS := optional
include $(BUILD_EXECUTABLE)

# double-open
include $(CLEAR_VARS)
LOCAL_C_INCLUDES := external/libuiomux/include
LOCAL_CFLAGS := -DVERSION=\"1.0.0\"
LOCAL_SRC_FILES := double-open.c
LOCAL_SHARED_LIBRARIES := libuiomux
LOCAL_MODULE := double-open
LOCAL_MODULE_TAGS := optional
include $(BUILD_EXECUTABLE)

# multiple-open
include $(CLEAR_VARS)
LOCAL_C_INCLUDES := external/libuiomux/include
LOCAL_CFLAGS := -DVERSION=\"1.0.0\"
LOCAL_SRC_FILES := multiple-open.c
LOCAL_SHARED_LIBRARIES := libuiomux
LOCAL_MODULE := multiple-open
LOCAL_MODULE_TAGS := optional
include $(BUILD_EXECUTABLE)

# lock-unlock
include $(CLEAR_VARS)
LOCAL_C_INCLUDES := external/libuiomux/include
LOCAL_CFLAGS := -DVERSION=\"1.0.0\"
LOCAL_SRC_FILES := lock-unlock.c
LOCAL_SHARED_LIBRARIES := libuiomux
LOCAL_MODULE := lock-unlock
LOCAL_MODULE_TAGS := optional
include $(BUILD_EXECUTABLE)

# fork
include $(CLEAR_VARS)
LOCAL_C_INCLUDES := external/libuiomux/include
LOCAL_CFLAGS := -DVERSION=\"1.0.0\"
LOCAL_SRC_FILES := fork.c
LOCAL_SHARED_LIBRARIES := libuiomux
LOCAL_MODULE := fork
LOCAL_MODULE_TAGS := optional
include $(BUILD_EXECUTABLE)

# threads
include $(CLEAR_VARS)
LOCAL_C_INCLUDES := external/libuiomux/include
LOCAL_CFLAGS := -DVERSION=\"1.0.0\"
LOCAL_SRC_FILES := threads.c
LOCAL_SHARED_LIBRARIES := libuiomux
LOCAL_MODULE := threads
LOCAL_MODULE_TAGS := optional
include $(BUILD_EXECUTABLE)

# fork-threads
include $(CLEAR_VARS)
LOCAL_C_INCLUDES := external/libuiomux/include
LOCAL_CFLAGS := -DVERSION=\"1.0.0\"
LOCAL_SRC_FILES := fork-threads.c
LOCAL_SHARED_LIBRARIES := libuiomux
LOCAL_MODULE := fork-threads
LOCAL_MODULE_TAGS := optional
include $(BUILD_EXECUTABLE)

# exit-locked
include $(CLEAR_VARS)
LOCAL_C_INCLUDES := external/libuiomux/include
LOCAL_CFLAGS := -DVERSION=\"1.0.0\"
LOCAL_SRC_FILES := exit-locked.c
LOCAL_SHARED_LIBRARIES := libuiomux
LOCAL_MODULE := exit-locked
LOCAL_MODULE_TAGS := optional
include $(BUILD_EXECUTABLE)

#wakeup
include $(CLEAR_VARS)
LOCAL_C_INCLUDES := external/libuiomux/include
LOCAL_CFLAGS := -DVERSION=\"1.0.0\"
LOCAL_SRC_FILES := wakeup.c
LOCAL_SHARED_LIBRARIES := libuiomux
LOCAL_MODULE := wakeup
LOCAL_MODULE_TAGS := optional
include $(BUILD_EXECUTABLE)

#timeout
include $(CLEAR_VARS)
LOCAL_C_INCLUDES := external/libuiomux/include
LOCAL_CFLAGS := -DVERSION=\"1.0.0\"
LOCAL_SRC_FILES := timeout.c
LOCAL_SHARED_LIBRARIES := libuiomux
LOCAL_MODULE := timeout
LOCAL_MODULE_TAGS := optional
include $(BUILD_EXECUTABLE)
