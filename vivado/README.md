# Vivado on the NAS

- Image `vivado-env:2024.1` in the NAS docker: Ubuntu 22.04, the libraries Vivado
  needs, and Vivado ML Standard 2024.1 (Artix-7 only) under `/opt/Xilinx`. Nothing of
  Vivado lives on the NAS file system. 2024.1 is the release Gideon builds the C64U
  images with.
- `build_image.sh [install dir]` builds the image on the NAS from a finished install
  tree (`Dockerfile`, `dockerignore`); the tree is only read and can go afterwards.
  Vitis HLS, the installer client and the simulation data are left out.
- A new install tree comes from AMD's web installer with `install_config_2024.1.txt`
  and an AMD login (Gitea secrets `AMD_USER` / `AMD_PASSWORD` if a workflow does it).
- Builds: `tools/vivado.sh designs/<name>` (work area
  `/srv/trx64-build`) or the Gateware workflow, both in this image.

## Build speed

From TODO.md section 2 (2026-10-01); moved here 2026-10-03.

- `general.maxThreads`: `TRX_VIVADO_THREADS` from the runner config, default 3 (NAS: three of its four cores).
- Out-of-context checkpoint of the C64 core (`c64_core.v`), rebuilt only when its
  sources change (`tools/vivado_lib.tcl`, cache in `/srv/trx64-build/cache`);
  cached GTP IP; clock groups (route 8.2 → 0.7 min).
- Result for c64_basic: 18 min → 4 min when only the glue changes, 12 min cold.
- Bitstream work is one target, so builds run one at a time; items that can be
  done in simulation run beside it in their own worktrees and are merged before a
  combined build.
