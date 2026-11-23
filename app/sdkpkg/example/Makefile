# Build the example from the SDK package:   make RV_PREFIX=riscv32-unknown-elf-     ->  example.u2a (carries menu.toml as its menu record)
#                                           make menu                               ->  example.menu.bin (the record alone, as `cor_pack.py --desc` takes the TOML)
#                                           make test                               ->  the host test (host C++ compiler)
#                                           make app                                ->  the example core's app (appout/app.bin), linked from the package: its TOML A and TOML B, no
#                                                                                       prebuilt image (D31); the menu simulator shows its menu: ../bin/menusim --desc app/desc.toml
LIB_NAME = example
LIB_VERSION = 1
LIB_NEEDS = base>=3.0
LIB_SRCS = example.cc greet.cc
LIB_MENU = menu.toml
LIB_BUDGET = image=16384 heap_keep=0 heap_peak=0 ops=1 states=1 events=0 nodes=2 file_actions=1 settings=0 tabs=1 polls=0
include ../mk/lib.mk

menu: example.menu.bin
example.menu.bin: menu.toml
	python3 $(SDK_TOOLS)/descrec.py compile $< $@

CXX_HOST ?= c++
test_example: test_example.cc example.cc greet.cc
	$(CXX_HOST) -std=gnu++11 -Wall -I../include -I. $^ -o $@
test: test_example
	./test_example

# The app of the example core (T239.3 D31, P3-2): the package's base archives, the sources that depend on the core compiled with the [sizes] of app/toml_b.toml, and the libraries that TOML B names
# (none). The record is app/desc.toml, checked against the same [sizes].
SDK_ROOT = $(SDK_MK)..
app: app/desc.toml app/toml_b.toml
	mkdir -p appout
	python3 $(SDK_TOOLS)/descrec.py compile app/desc.toml appout/desc.bin --sizes app/toml_b.toml
	python3 $(SDK_TOOLS)/app_build.py build --sdk $(SDK_ROOT) --toml-a $(SDK_ROOT)/gateware/shared.toml --toml-b app/toml_b.toml --out appout --src app --record appout/desc.bin

clean-all: clean
	rm -f example.menu.bin test_example
	rm -rf appout
