Softboot on ABI 3 (T104.3 D9). PROVENANCE: `src/softboot.c` is a copy of `apps/ultimate/apps/softboot/src/softboot.c` (the TRX64 line's softboot application, GPL-3.0;
submodule commit `aa2ef5dc`). Changed: `app_main` is `softboot_main` (the entry is `glue.cc`: with no file it asks for the image); the calls of ABI 2 land on the base's
service tables through `app/sdk/u2p_compat.c` (`flash_read` and `machine_handover` are `coreapp/src/appsvc.cc`).
