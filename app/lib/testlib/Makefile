# make -C app/lib/testlib   ->  testlib.u2a
LIB_NAME = testlib
LIB_VERSION = 1
LIB_NEEDS = base>=3.0
LIB_SRCS = testlib.cc
include ../lib.mk

# The two links the bench compares (tests/loader/run.sh): the same objects at 0 and at 0x1000. Run this after changing testlib.cc,
# lib.ld or the flags, and commit the result.
FIX = ../../../tests/loader/fixtures
fixtures: testlib.o
	$(LD_RV) -m elf32lriscv --emit-relocs --defsym=__lib_base=0 -T $(LIB_DIR)lib.ld $< -o $(FIX)/testlib.at0.elf
	$(LD_RV) -m elf32lriscv --emit-relocs --defsym=__lib_base=0x1000 -T $(LIB_DIR)lib.ld $< -o $(FIX)/testlib.at1000.elf
