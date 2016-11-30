LOCAL_PATH:= $(call my-dir)

e2fsdroid_src := e2fsdroid.c \
    block_range.c \
    fsmap.c \
    block_list.c \
    base_fs.c

include $(CLEAR_VARS)
LOCAL_SRC_FILES := $(e2fsdroid_src)
LOCAL_MODULE := e2fsdroid
LOCAL_SHARED_LIBRARIES := libext2fs-host \
    libext2_com_err-host
include $(BUILD_HOST_EXECUTABLE)


include $(CLEAR_VARS)
LOCAL_SRC_FILES := $(e2fsdroid_src)
LOCAL_MODULE := e2fsdroid
LOCAL_SHARED_LIBRARIES := libext2fs \
    libext2_com_err
include $(BUILD_EXECUTABLE)
