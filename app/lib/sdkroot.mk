# Where the pieces of the SDK are, for lib.mk and app.mk. Included by both; it finds its own place, so the same files work in this tree and in the
# package of tools/sdk_pack.sh:
#   package  mk/sdkroot.mk   beside include/ (every header, flat), sdk/ (C sources of the applications' runtime) and tools/ (the packer)
#   tree     app/lib/sdkroot.mk   beside app/sdk, coreapp/src and tools/
SDK_MK := $(dir $(lastword $(MAKEFILE_LIST)))
ifneq ($(wildcard $(SDK_MK)../include/libapi.h),)
SDK_INC    := -I$(SDK_MK)../include
SDK_HIDDEN := $(SDK_MK)../include/hidden.h
SDK_TOOLS  := $(SDK_MK)../tools
else
SDK_INC    := -I$(SDK_MK)../sdk -I$(SDK_MK)../../coreapp/src
SDK_HIDDEN := $(SDK_MK)../sdk/hidden.h
SDK_TOOLS  := $(SDK_MK)../../tools
endif
SDK_SRC := $(SDK_MK)../sdk
