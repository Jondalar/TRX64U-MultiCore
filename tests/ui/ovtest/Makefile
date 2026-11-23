# The test library of the overlay service (T256 S3, S5, S10; the ue2emu features tests/ui/features*/overlay_*.feature).
#   make -C tests/ui/ovtest RV_PREFIX=riscv32-unknown-elf-   ->  ovtest.u2a     (the build image's compiler; tests/ui/ovtest/build.sh makes the whole kit)
LIB_NAME = ovtest
LIB_VERSION = 1
LIB_NEEDS = base>=3.3
LIB_SRCS = ovtest.cc
include ../../../app/lib/lib.mk
