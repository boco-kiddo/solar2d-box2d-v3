# ndk-build makefile for libplugin.box2dv3.so (alternative to build_zig.sh; used by build.bat).
# Variables passed by build.bat: CORONA_ROOT (Solar2D "Native" folder), CORONA_LIBS (unpacked Corona.aar).
LOCAL_PATH := $(call my-dir)
ROOT := $(LOCAL_PATH)/../..

include $(CLEAR_VARS)
LOCAL_MODULE := liblua
LOCAL_SRC_FILES := $(CORONA_LIBS)/jni/$(TARGET_ARCH_ABI)/liblua.so
include $(PREBUILT_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := libplugin.box2dv3
BOX2D_SRC := $(wildcard $(ROOT)/shared/src/*.c)
PLUGIN_SRC := $(wildcard $(ROOT)/shared/plugin/*.cpp)
LOCAL_SRC_FILES := $(BOX2D_SRC:$(LOCAL_PATH)/%=%) $(PLUGIN_SRC:$(LOCAL_PATH)/%=%)
LOCAL_C_INCLUDES := $(ROOT)/shared/include $(ROOT)/shared/plugin \
	$(CORONA_ROOT)/Corona/shared/include/Corona $(CORONA_ROOT)/Corona/shared/include/lua
LOCAL_CFLAGS := -DNDEBUG -DRtt_ANDROID_ENV -O2 -fvisibility=hidden -ffunction-sections -fdata-sections
LOCAL_CONLYFLAGS := -std=c17
LOCAL_CPPFLAGS := -std=c++17 -fno-exceptions -fno-rtti
LOCAL_LDFLAGS := -Wl,--gc-sections -Wl,-z,max-page-size=16384
LOCAL_SHARED_LIBRARIES := liblua
include $(BUILD_SHARED_LIBRARY)
