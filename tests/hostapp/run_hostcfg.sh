#!/usr/bin/env bash
# T163 S1/S2: the host's own settings store (hostcfg.cc, the backend hooks in components/config.cc, coreapp/src/cfgfile.cc) against the real ConfigManager
# with a fake SPI flash and a fake file (test_hostcfg.cc). Each decision has a control: a mutant of the source under test has to go red.
#   tests/hostapp/run_hostcfg.sh [scratch dir]          (run.sh calls it with its own)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
source "$ROOT/tests/hostgen.sh"   # T239.3 V-3b: CPATH holds hostio.h (IOBASE, U2P_IO_BASE) and basetab.h, generated from the TOML A of the host
HS="$ROOT/host-app/software"
T="${1:-}"
if [ -z "$T" ]; then T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT; fi
T="$T/hostcfg"; mkdir -p "$T"
INC=(-I"$ROOT/tests/hostapp/cfgstub" -I"$ROOT/tests/hostapp/stubfs" -I"$HS/hostapp" -I"$ROOT/coreapp/src" -I"$HS/components" -I"$HS/filesystem" -I"$HS/filemanager" -I"$HS/io/flash"
     -I"$HS/infra" -I"$HS/system" -I"$HS/userinterface")
TEST="$ROOT/tests/hostapp/test_hostcfg.cc"
srcs=("$HS/components/config.cc" "$HS/components/mystring.cc" "$HS/hostapp/hostcfg.cc" "$ROOT/coreapp/src/cfgfile.cc")
c++ -std=gnu++14 -w -g -DU64=2 -DRUNS_ON_PC "${INC[@]}" -o "$T/test_hostcfg" "$TEST" "${srcs[@]}" 2>&1 | head -30
"$T/test_hostcfg" | tail -3
"$T/test_hostcfg" | grep -q ' 0 failed' || { echo "run_hostcfg: the test is red" >&2; exit 1; }

hc_mutant() {   # name, source file (full path), sed expression (or perl: prefix "perl:")
    local name="$1" src="$2" expr="$3" bad="$T/mut_$1_$(basename "$2")" list=()
    case "$expr" in
        perl:*) perl -0pe "${expr#perl:}" "$src" > "$bad" ;;
        *) sed -e "$expr" "$src" > "$bad" ;;
    esac
    cmp -s "$bad" "$src" && { echo "CONTROL $name: the edit changed nothing" >&2; exit 1; }
    for f in "${srcs[@]}"; do if [ "$f" = "$src" ]; then list+=("$bad"); else list+=("$f"); fi; done
    c++ -std=gnu++14 -w -g -DU64=2 -DRUNS_ON_PC "${INC[@]}" -o "$T/mut_$name" "$TEST" "${list[@]}" 2>&1 | head -20
    if "$T/mut_$name" > "$T/mut_$name.log" 2>&1; then echo "CONTROL $name: the test PASSED against the mutant" >&2; exit 1; fi
    grep -q '^FAIL' "$T/mut_$name.log" || { echo "CONTROL $name: it failed without a red check" >&2; exit 1; }
    echo "control $name: fails as it must ($(grep -c '^FAIL' "$T/mut_$name.log") check(s) red)"
}
CFG="$HS/components/config.cc"
HC="$HS/hostapp/hostcfg.cc"
# S1 control: the foreign page is read at every start, not only while our file is new -> FTP follows Gideon's page, the page is read
hc_mutant s1_foreign_always "$CFG" 's/if (r == CFG_LOAD_TAKEOVER) {/if (true) {/'
# S1 control: the store looks for its page in the flash at registration (the original's scan) -> the flash is read although our file has the section
hc_mutant s1_scan_at_register "$CFG" 'perl:s/(Try to find the page in Flash \(T163[^\n]*\n\s*)if \(!page && !cfg_backend\)/$1if (!page)/'
# S2 control: a new file does not take over -> the first start comes up with defaults, not with Gideon's values
hc_mutant s2_no_takeover "$HC" 's/^        is_new = true;/        is_new = false;/'
# S2 control: a store without a section is taken over even though the file exists -> a later change in Gideon's pages is seen (first start is not over)
hc_mutant s2_takeover_late "$HC" 's/return is_new ? CFG_LOAD_TAKEOVER : CFG_LOAD_DEFAULT;/return CFG_LOAD_TAKEOVER;/'
# an unreadable file counts as a new one -> our settings would be overwritten by Gideon's
hc_mutant unreadable_is_new "$HC" 's/^        unavailable = true;/        is_new = true;/'
# the take-over does not save -> the file is not made, the next start would take over again
hc_mutant takeover_not_saved "$CFG" 'perl:s/(old\.unpack\(this\);\s*break;\s*\}\s*\}\s*)cfg_backend->save\(this\);/$1/'
# a store's write goes to the original's page (file) again -> the FileManager is reached
hc_mutant write_to_page "$CFG" 'perl:s/(void ConfigStore :: write\(\)\r?\n\{\r?\n)    if \(cfg_backend\) \{/$1    if (false) {/;s/(void ConfigPage :: write\(\)\r?\n\{\r?\n)    if \(cfg_backend\) \{/$1    if (false) {/'
# the original's page-file fallback is read again
hc_mutant page_file_fallback "$CFG" 'perl:s/(Not found in config pages[^\n]*\n\s*)if \(!page && !cfg_backend\)/$1if (!page)/'
echo "run_hostcfg: ok"
