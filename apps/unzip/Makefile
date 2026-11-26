# Unzip on ABI 3 (T104.3). Built by tools/build_apps.sh in the build image; see app/lib/app.mk.
APP_NAME = unzip
APP_VERSION = 1.0
APP_RES = transient
APP_MENU = app.toml
APP_NEEDS = base>=4.0 miniz>=1.0
APP_CSRCS = src/unzip.c
APP_CCSRCS = glue.cc
APP_INC = -Isrc -I../miniz -I../miniz/src
include ../../app/lib/app.mk
