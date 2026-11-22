# Build the example from the SDK package:   make RV_PREFIX=riscv32-unknown-elf-     ->  example.u2a (carries menu.toml as its menu record)
#                                           make menu                               ->  example.menu.bin (the record alone, as `cor_pack.py --desc` takes the TOML)
#                                           make test                               ->  the host test (host C++ compiler)
LIB_NAME = example
LIB_VERSION = 1
LIB_NEEDS = base>=3.0
LIB_SRCS = example.cc greet.cc
LIB_MENU = menu.toml
include ../mk/lib.mk

menu: example.menu.bin
example.menu.bin: menu.toml
	python3 $(SDK_TOOLS)/descrec.py compile $< $@

CXX_HOST ?= c++
test_example: test_example.cc example.cc greet.cc
	$(CXX_HOST) -std=gnu++11 -Wall -I../include -I. $^ -o $@
test: test_example
	./test_example

clean-all: clean
	rm -f example.menu.bin test_example
