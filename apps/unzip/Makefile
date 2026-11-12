# Unzip on ABI 3 (T104.3). Built by tools/build_apps.sh in the build image; see app/lib/app.mk.
APP_NAME = unzip
APP_VERSION = 1.0
APP_RES = transient
APP_MENU = app.toml
APP_CSRCS = src/unzip.c src/miniz/miniz.c src/miniz/miniz_tinfl.c src/miniz/miniz_zip.c
APP_CCSRCS = glue.cc
APP_INC = -Isrc -Isrc/miniz
include ../../app/lib/app.mk
