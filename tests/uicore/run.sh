#!/usr/bin/env bash
# Unit tests of uicore (T192.1): the service, the token, the dispatcher, events, jobs and questions against a fake port and domain.
# Negative controls exist for the persistence part only (CLAUDE.md, T174 D5.4): the token file. The product source is copied, one expression is broken
# with sed, the same test is built against the broken copy and must FAIL.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SRC="$ROOT/uicore"
T="$(mktemp -d)"
trap 'rm -rf "$T"' EXIT
CXX="${CXX:-c++}"
FLAGS=(-std=gnu++11 -Wall -Wextra -Wno-unused-function -fno-exceptions -I"$SRC" -fsanitize=address,undefined -fno-sanitize-recover=undefined -g)
build() { local dir="$1" out="$2"; "$CXX" "${FLAGS[@]}" "$ROOT/tests/uicore/test_uicore.cc" "$dir"/uic_json.cc "$dir"/uic_sha.cc "$dir"/uic_svc.cc "$dir"/uic_net.cc -o "$out"; }
build "$SRC" "$T/test"
"$T/test"
echo "PASS uicore"
# the controls: a token that is not written, and a grant that ignores the existing file, must make the test fail
control() {   # control NAME SED-EXPR FILE
    local name="$1" expr="$2" file="$3"
    mkdir -p "$T/m_$name"; cp "$SRC"/* "$T/m_$name/"
    sed -i.bak "$expr" "$T/m_$name/$file"
    cmp -s "$T/m_$name/$file" "$SRC/$file" && { echo "FAIL control $name: the mutation changed nothing"; exit 1; }
    build "$T/m_$name" "$T/m_$name/test" 2>/dev/null
    if "$T/m_$name/test" >/dev/null 2>&1; then echo "FAIL control $name: the test passed against the broken source"; exit 1; fi
    echo "  control $name: fails as it must"
}
control nosave 's/if (!s->port.file_save(s->port.user, hex, UIC_TOKEN_LEN + 1)) {/if (false) {/' uic_svc.cc
control regrant 's/if (s->tok_have || (n >= 0 \&\& hex_token(b, n))) {/if (false) {/' uic_svc.cc
control tokpoll 's/} else if (n < 0 \&\& ++s->tok_miss >= 2) {/} else if (false) {/' uic_svc.cc
