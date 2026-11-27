# T239.3 D19, real compiler and real link: a library with a global constructor or destructor, built through app/lib/lib.mk. tools/test_u2a_pack.py runs
# `make SRC=ctor.cc` and `make SRC=dtor.cc`; both must stop in the packer's check of the objects with the D19 message, not in ld.
SRC ?= ctor.cc
LIB_NAME = d19
LIB_VERSION = 1
LIB_SRCS = $(SRC)
include ../../../app/lib/lib.mk
