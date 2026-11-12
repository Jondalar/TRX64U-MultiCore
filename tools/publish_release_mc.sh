#!/usr/bin/env bash
# Publish a bundle as a real Gitea release: an annotated tag plus a release with every file of the
# bundle as an asset (Alex, 2026-10-14: "my releases go into gitea as real releases with a tag").
#
#   tools/publish_release_mc.sh --dir <bundle dir> --tag <tag> [--zip <file>] [--title <text>]
#                               [--notes <file>] [--commit <sha>] [--draft]
#
# --dir      the directory tools/make_release_mc.sh wrote (its files become the assets)
# --zip      the zip beside it, added as one more asset
# --notes    the release body, INSTALL.md of the bundle by default
# --commit   what the tag points at; HEAD by default. The tag is pushed to origin.
#
# The token comes from GITEA_TOKEN or from tea's config; it is never printed. A tag name that is not
# v* does not start a pipeline run (.gitea/workflows/pipeline.yml triggers on v* tags only).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DIR="" TAG="" ZIP="" TITLE="" NOTES="" COMMIT="HEAD" DRAFT=false

usage() { sed -n '2,15p' "$0"; exit 2; }
while [ $# -gt 0 ]; do
    case "$1" in
        --dir) DIR="$2"; shift 2;;
        --tag) TAG="$2"; shift 2;;
        --zip) ZIP="$2"; shift 2;;
        --title) TITLE="$2"; shift 2;;
        --notes) NOTES="$2"; shift 2;;
        --commit) COMMIT="$2"; shift 2;;
        --draft) DRAFT=true; shift;;
        -h|--help) usage;;
        *) echo "unknown option $1" >&2; usage;;
    esac
done
[ -n "$DIR" ] && [ -n "$TAG" ] || usage
[ -d "$DIR" ] || { echo "$DIR: not a directory" >&2; exit 1; }
NOTES="${NOTES:-$DIR/INSTALL.md}"
[ -f "$NOTES" ] || { echo "$NOTES: no release notes" >&2; exit 1; }
TITLE="${TITLE:-$TAG}"

TOKEN="${GITEA_TOKEN:-$(awk '/token:/{print $2; exit}' "$HOME/.config/tea/config.yml")}"
[ -n "$TOKEN" ] || { echo "no token: set GITEA_TOKEN" >&2; exit 1; }
URL="$(git -C "$ROOT" remote get-url origin)"          # git@host:OWNER/REPO.git
HOST="${URL#*@}"; HOST="${HOST%%:*}"
SLUG="${URL#*:}"; SLUG="${SLUG%.git}"
API="http://$HOST/api/v1/repos/$SLUG"
api() { curl -sf -H "Authorization: token $TOKEN" "$@"; }

SHA="$(git -C "$ROOT" rev-parse "$COMMIT")"
if git -C "$ROOT" rev-parse -q --verify "refs/tags/$TAG" >/dev/null; then
    [ "$(git -C "$ROOT" rev-parse "$TAG^{commit}")" = "$SHA" ] \
        || { echo "tag $TAG exists and points at another commit" >&2; exit 1; }
    echo ">>> tag $TAG exists"
else
    git -C "$ROOT" tag -a "$TAG" -m "$TITLE" "$SHA"
    echo ">>> tag $TAG created at ${SHA:0:8}"
fi
git -C "$ROOT" push -q origin "refs/tags/$TAG"

if api -o /dev/null "$API/releases/tags/$TAG" 2>/dev/null; then
    echo "release $TAG already exists; delete it in Gitea first if it is to be rebuilt" >&2
    exit 1
fi
echo ">>> release $TAG"
REL="$(python3 - "$NOTES" "$TAG" "$TITLE" "$SHA" "$DRAFT" <<'PY'
import json, sys
notes, tag, title, sha, draft = sys.argv[1:]
print(json.dumps({"tag_name": tag, "target_commitish": sha, "name": title,
                  "body": open(notes, encoding="utf-8").read(),
                  "draft": draft == "true", "prerelease": False}))
PY
)"
ID="$(printf '%s' "$REL" | api -X POST -H "Content-Type: application/json" -d @- "$API/releases" \
      | python3 -c 'import json,sys; print(json.load(sys.stdin)["id"])')"

for f in "$DIR"/* ${ZIP:+"$ZIP"}; do
    n="$(basename "$f")"
    api -o /dev/null -X POST -F "attachment=@$f" "$API/releases/$ID/assets?name=$n" \
        && echo "    asset $n ($(wc -c < "$f") bytes)"
done

api "$API/releases/$ID" | python3 -c '
import json, sys
r = json.load(sys.stdin)
print(">>> %s" % r["html_url"])
print("    %d assets, %d bytes" % (len(r["assets"]), sum(a["size"] for a in r["assets"])))
for a in sorted(r["assets"], key=lambda a: a["name"]):
    print("    %-44s %10d" % (a["name"], a["size"]))'
