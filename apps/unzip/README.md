Unzip on ABI 3 (T104.3 D9). PROVENANCE: `src/unzip.c` is a copy of `apps/ultimate/apps/unzip/src` (the TRX64 line's unzip application,
GPL-3.0, miniz MIT; submodule commit `aa2ef5dc`). Changed: `app_main` is `unzip_main` (the entry is `glue.cc`, which gives it the context of ABI 2); the
calls of ABI 2 land on the base's service tables through `app/sdk/u2p_compat.c`. `app.toml` is what the application adds to the core's menu.
