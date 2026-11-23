# Online search on ABI 3 (T239.3 L-3): a resident sub-app, format 3, built by ci/trxapps.sh in the build image; see app/lib/app.mk.
# `base>=3.3`: api->net connects (SvcNet::connect, resolve) and the loader serves the named export. APP_EXPORTS lists the table the file manager finds.
APP_NAME = online
APP_KIND = lib
APP_VERSION = 1.0
APP_NEEDS = base>=3.3
APP_EXPORTS = online=1
APP_MENU = app.toml
APP_CCSRCS = online_lib.cc online_search.cc online_http.cc online_json.cc
include ../../app/lib/app.mk
