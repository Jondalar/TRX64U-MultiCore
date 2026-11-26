# Unzip (T104.3, T180 D9). Built by tools/build_apps.sh in the build image; see app/lib/app.mk.
# T239.3 D60: the application carries miniz (apps/miniz: the zip reader, inflate and the call table of zip_lib.h); it needs the base only, and the core apps link no miniz.
APP_NAME = unzip
APP_VERSION = 1.0
APP_RES = transient
APP_MENU = app.toml
APP_NEEDS = base>=4.0
APP_CSRCS = src/unzip.c miniz.c miniz_tinfl.c miniz_zip.c
APP_CCSRCS = glue.cc miniz_lib.cc
APP_INC = -Isrc -I../miniz -I../miniz/src
vpath %.c ../miniz/src
vpath %.cc ../miniz
include ../../app/lib/app.mk
