# TREX CRT on ABI 3 (T104.3). See app/lib/app.mk. An application with screens: lib_register registers what they bind to.
APP_NAME = trex_crt
APP_VERSION = 1.1
APP_RES = transient
APP_REG = 1
APP_MENU = app.toml
APP_CSRCS = src/trex_main.c src/ui_shim.c src/cart_base.c src/cart_screen.c src/cart_dumper.c src/cart_writer.c src/cart_inspector.c src/eapi_drivers.c
APP_CCSRCS = glue.cc
APP_INC = -Isrc
include ../../app/lib/app.mk
