#!/bin/bash
# ans.sh <worktree> <spec> <Qn> <answer text> : record Alex's answer under Qn, commit, push
set -e
W=$1; S=$2; Q=$3; A=$4
cd "$W"
python3 - "$S" "$Q" "$A" <<'PY'
import sys,re
p,q,a=sys.argv[1:]
L=open(p).read().split('\n')
i=[k for k,l in enumerate(L) if re.match(r'^\d+\. %s:'%q,l)]
assert len(i)==1, i
L.insert(i[0]+1,'   **Answered (Alex, 2026-10-13):** '+a)
open(p,'w').write('\n'.join(L))
PY
python3 tools/check_todo.py >/dev/null
T=$(basename "$S" | cut -d- -f1)
git add "$S" && git commit -q -m "$T $Q answered (Alex)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01TXJJcXhem7KALtF1pMSxk5" && git push -q 2>/dev/null; git log --oneline -1
