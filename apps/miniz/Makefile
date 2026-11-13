# The zip library (T197 P1), format 3, resident. Built by ci/trxapps.sh in the build image; see app/lib/app.mk.
APP_NAME = miniz
APP_KIND = lib
APP_VERSION = 1.0
APP_NEEDS = base>=3.1
APP_CSRCS = src/miniz.c src/miniz_tinfl.c src/miniz_zip.c
APP_CCSRCS = miniz_lib.cc
APP_INC = -Isrc -I.
include ../../app/lib/app.mk
