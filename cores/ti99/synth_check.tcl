# Standalone out-of-context synthesis of the TI-99/4A core (no registration needed): run from the repo's host/ directory, e.g.
#   docker run --rm --user "$(id -u):$(id -g)" -e HOME=/tmp -v ~/TRX64U-MultiCore:/w -w /w/host vivado-env:2024.1 \
#     vivado -mode batch -nojournal -nolog -source /w/cores/ti99/synth_check.tcl 2>&1 | grep -E "ERROR|CRITICAL|=== ti99|Finished Synth"
set part xc7a50tfgg484-1
set bdir /w/ti99-synth-out
file mkdir $bdir
source ../cores/ti99/core_sources.tcl
