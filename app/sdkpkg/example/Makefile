# Build the example from the SDK package:   make menu                               ->  example.menu.bin (the record alone, as `cor_pack.py --desc` takes the TOML)
#                                           make test                               ->  the host test (host C++ compiler)
#                                           make app                                ->  the example core's app (appout/app.bin), linked from the package: its own TOML A (gateware/example.toml)
#                                                                                       and TOML B (app/toml_b.toml), no prebuilt image (D31); the menu simulator shows its menu:
#                                                                                       ../bin/menusim --desc app/desc.toml
# Base 4.0 (T239.3 D58) loads no library file: the example library (example.cc, greet.cc) is linked into the app by the [[library]] block of app/toml_b.toml; there is no example.u2a.
include ../mk/sdkroot.mk
.DEFAULT_GOAL := menu

menu: example.menu.bin
example.menu.bin: menu.toml
	python3 $(SDK_TOOLS)/descrec.py compile $< $@

CXX_HOST ?= c++
test_example: test_example.cc example.cc greet.cc
	$(CXX_HOST) -std=gnu++11 -Wall -I../include -I. $^ -o $@
test: test_example
	./test_example

# The app of the example core (T239.3 D31, S17): the package's base archives, the sources that depend on the core compiled with the [sizes] of app/toml_b.toml, and the libraries that TOML B names
# (cbmprg) and its own library example (sources in this directory, `--src .`). Its TOML A is gateware/example.toml (the map of its own block is beside it). The core has no descriptor in the app: the
# record app/desc.toml, checked against the same [sizes], is what describes it, and its code is the library example (hooks = "example").
SDK_ROOT = $(SDK_MK)..
# app, menu and test name no file; app is also the directory of the core's TOMLs, so without .PHONY make finds it up to date
.PHONY: app menu test clean-all
app: app/desc.toml app/toml_b.toml
	mkdir -p appout
	python3 $(SDK_TOOLS)/descrec.py compile app/desc.toml appout/desc.bin --sizes app/toml_b.toml
	python3 $(SDK_TOOLS)/app_build.py build --sdk $(SDK_ROOT) --toml-a gateware/example.toml --maps gateware --toml-b app/toml_b.toml --out appout --src app --src . --record appout/desc.bin

clean-all:
	rm -f example.menu.bin test_example
	rm -rf appout
