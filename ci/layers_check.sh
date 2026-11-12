#!/usr/bin/env bash
# Stage lint (T87 D2): host/layers.txt is complete and unambiguous.
#   - every pathspec of every section matches at least one tracked file
#   - every tracked file under host and cores/ is in exactly one section
#     (host, core:*, ignore); a file in none or in two fails
#   - T174 R2: host/check/ (the post-condition checks of the Vivado session) is no key input, so no file there may call a command that changes
#     the design or reads an input (set_property, create_pblock, read_*, set_*_delay, a flow step, source ...): the exclusion cannot hide a real input
#   - reference: names an existing core: section and that core has a core_sources.tcl (host/build.tcl sources it, T115)
#   ci/layers_check.sh            check the checkout
#   ci/layers_check.sh --selftest the checker on a scratch repository
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT/ci/lib.sh"

check_layers() { # check_layers <repo root> <layers file> ; prints problems, returns 1 on any
  local repo="$1" file="$2" rc=0 sec spec list tmp
  tmp="$(mktemp -d)"
  for sec in $(layer_sections "$file"); do
    layer_specs "$file" "$sec" > "$tmp/specs"
    specs=(); while IFS= read -r spec; do specs+=("$spec"); done < "$tmp/specs"
    if [ "${#specs[@]}" = 0 ]; then echo "section $sec is empty"; rc=1; continue; fi
    for spec in "${specs[@]}"; do
      case "$spec" in ':(exclude)'*) continue ;; esac
      if [ -z "$(git -C "$repo" ls-files -- "$spec" | head -1)" ]; then echo "section $sec: '$spec' matches no tracked file"; rc=1; fi
    done
    git -C "$repo" ls-files -- "${specs[@]}" | sort -u | sed "s|\$| $sec|" >> "$tmp/all"
  done
  [ -f "$tmp/all" ] || : > "$tmp/all"
  list="$(git -C "$repo" ls-files -- host cores | sort)"
  while IFS= read -r f; do
    [ -n "$f" ] || continue
    n="$(awk -v f="$f" '$1 == f { print $2 }' "$tmp/all" | tr '\n' ' ')"
    case "$n" in
      "") echo "$f is in no section"; rc=1 ;;
      *" "?*)
        # Several sections are an error - except when they are all core: sections and the file lives
        # in exactly one of those cores: that core owns it, the others only read it (amiga1200 was
        # the amiga core's gateware with other generics; since T97 it has no descriptor). A file in a
        # core: AND the host: section stays an error, which is the case this check is here for.
        owners=0; readers=0
        for sec in $n; do
          case "$sec" in
            core:*) if [ "${f#cores/${sec#core:}/}" != "$f" ]; then owners=$((owners + 1)); else readers=$((readers + 1)); fi ;;
            *) owners=99 ;;
          esac
        done
        if [ "$owners" = 1 ] && [ "$readers" -ge 1 ]; then continue; fi
        echo "$f is in more than one section: $n"; rc=1 ;;
    esac
  done <<< "$list"
  ref="$(layer_reference "$file")"
  if [ -n "$ref" ] && ! layer_sections "$file" | grep -qx "core:$ref"; then echo "reference: $ref has no core:$ref section"; rc=1; fi
  # T115: host/build.tcl sources cores/<reference>/core_sources.tcl (the recipe that sets core_dcp, l2_dcp, gvhdl and top_synth)
  if [ -n "$ref" ] && [ -z "$(git -C "$repo" ls-files -- "cores/$ref/core_sources.tcl")" ]; then echo "reference: $ref has no cores/$ref/core_sources.tcl for host/build.tcl to source"; rc=1; fi
  rm -rf "$tmp"
  check_host_check "$repo" || rc=1
  return $rc
}

# check_host_check <repo>: the files under host/check/ stay observers (T174 R2). Comments are stripped; a forbidden command is one at the start of a
# command (line start, after ';', '{' or '[').
HOST_CHECK_FORBIDDEN='set_property|create_pblock|add_cells_to_pblock|resize_pblock|delete_pblock|read_[a-z_]+|set_[a-z_]*delay|set_clock_[a-z_]+|set_false_path|set_multicycle_path|set_input_[a-z_]+|set_output_[a-z_]+|create_clock|create_generated_clock|create_cell|create_net|create_ip|connect_net|remove_cell|synth_design|link_design|opt_design|place_design|phys_opt_design|route_design|write_bitstream|write_checkpoint|open_checkpoint|add_files|import_files|upgrade_ip|update_design|lock_design|launch_runs|set_param|source'
check_host_check() {
  local repo="$1" f hits rc=0
  while IFS= read -r f; do
    [ -n "$f" ] || continue
    hits="$(sed -E 's/^[[:space:]]*#.*$//' "$repo/$f" | grep -nE "(^|[;{[])[[:space:]]*($HOST_CHECK_FORBIDDEN)([[:space:]]|;|\\}|\\]|$)" || true)"
    if [ -n "$hits" ]; then echo "$f calls a command that changes the design or reads an input (host/check/ is no key input): $(printf '%s' "$hits" | head -1)"; rc=1; fi
  done < <(git -C "$repo" ls-files -- host/check)
  return $rc
}

if [ "${1:-}" = --selftest ]; then
  t="$(mktemp -d)"; trap 'rm -rf "$t"' EXIT
  git -C "$t" init -q; mkdir -p "$t/host" "$t/cores/x" "$t/tools"
  : > "$t/host/a.v"; : > "$t/host/b.v"; : > "$t/cores/x/c.v"
: > "$t/cores/x/core_sources.tcl"
  cp "$ROOT/tools/core_toml.py" "$t/tools/core_toml.py"
  # the core's sources live in its descriptor now (T94 D1), so a case sets both files
  desc() { # desc <sources as toml list>
    printf 'id = "x"\nname = "x"\nversion = "0.0.1"\ncontract = 1\napp = "none"\norder = 1\nexperimental = true\nsources = %s\n' "$1" > "$t/cores/x/core.toml"
  }
  desc '["host/b.v", "cores/x"]'
  git -C "$t" add -A
  printf 'reference: x\nhost:\nhost\n:(exclude)host/b.v\n' > "$t/layers.txt"
  check_layers "$t" "$t/layers.txt" > /dev/null || { echo "selftest: a good file was refused"; exit 1; }
  printf 'reference: x\nhost:\nhost\n' > "$t/layers.txt"
  out="$(check_layers "$t" "$t/layers.txt")" && { echo "selftest: a file in two sections passed"; exit 1; }
  printf '%s' "$out" | grep -q "b.v is in more than one" || { echo "selftest: wrong message: $out"; exit 1; }
  printf 'reference: x\nhost:\nhost\n:(exclude)host/b.v\n' > "$t/layers.txt"
  desc '["cores/x"]'; git -C "$t" add -A
  out="$(check_layers "$t" "$t/layers.txt")" && { echo "selftest: a file in no section passed"; exit 1; }
  printf '%s' "$out" | grep -q "b.v is in no section" || { echo "selftest: wrong message: $out"; exit 1; }
  desc '["host/b.v", "cores/x", "cores/nothere"]'; git -C "$t" add -A
  out="$(check_layers "$t" "$t/layers.txt")" && { echo "selftest: a dead pathspec passed"; exit 1; }
  printf '%s' "$out" | grep -q "cores/nothere' matches no tracked file" || { echo "selftest: wrong message: $out"; exit 1; }
  # T115: the reference core has to have the recipe host/build.tcl sources
  desc '["host/b.v", "cores/x"]'; git -C "$t" rm -q -f cores/x/core_sources.tcl; git -C "$t" add -A
  printf 'reference: x\nhost:\nhost\n:(exclude)host/b.v\n' > "$t/layers.txt"
  out="$(check_layers "$t" "$t/layers.txt")" && { echo "selftest: a reference core without core_sources.tcl passed"; exit 1; }
  printf '%s' "$out" | grep -q "x has no cores/x/core_sources.tcl" || { echo "selftest: wrong message: $out"; exit 1; }
  # T174 R2: an observer in host/check/ passes, one that changes the design (or only mentions it in a comment: it must not trip) is judged by its commands
  mkdir -p "$t/host/check"; desc '["host/b.v", "cores/x"]'
  printf 'reference: x\nhost:\nhost/a.v\nhost/b.v\nignore:\nhost/check\n' > "$t/layers.txt"
  printf '# set_property is only named here\nproc chk {w} {\n  if {[catch {get_msg_config -count} n]} { puts "x" }\n}\n' > "$t/host/check/ok.tcl"
  git -C "$t" add -A; git -C "$t" rm -q -f --cached cores/x/core_sources.tcl 2>/dev/null; : > "$t/cores/x/core_sources.tcl"; git -C "$t" add -A
  out="$(check_host_check "$t")" || { echo "selftest: an observer in host/check was refused: $out"; exit 1; }
  for bad in 'set_property PROP x [current_design]' 'proc p {} { create_pblock pb }' 'if {1} { read_xdc a.xdc }' 'set_max_delay -from a -to b 3' 'source other.tcl' 'foo; opt_design'; do
    printf 'proc chk {} {}\n%s\n' "$bad" > "$t/host/check/bad.tcl"; git -C "$t" add -A
    out="$(check_host_check "$t")" && { echo "selftest: host/check/ with '$bad' passed"; exit 1; }
    printf '%s' "$out" | grep -q "host/check/ is no key input" || { echo "selftest: wrong message: $out"; exit 1; }
  done
  echo "layers_check selftest ok"
  exit 0
fi

[ -f "$ROOT/host/layers.txt" ] || { echo "no layers.txt: monolithic tree, nothing to check"; exit 0; }
check_layers "$ROOT" "$ROOT/host/layers.txt" && echo "layers.txt: every file in exactly one section"
