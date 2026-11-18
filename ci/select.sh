#!/usr/bin/env bash
# Which optional parts a run does, one answer per call.
#
#   ci/select.sh sims-tier   0 (no sims) on main and feature branches since 2026-11-08 (Alex: sims only on demand); 2 on integ-*, release/tags, the dispatch
#                            input `sims`, or "[sims]" in the head commit. The nightly workflow passes tier 2 itself.
#   ci/select.sh ue2         yes only for a workflow_dispatch with the input ue2 = true (of the
#                            pipeline, or of bitstream.yml/package.yml alone); else no. Alex, 2026-10-03:
#                            the 100T (UE2) is built "at the end, when the thing is
#                            round", never on a normal push. Since 2026-11-18 not on a tag or a
#                            release/* branch either (Alex: "do that", T221): its first run (2461,
#                            tag v0.2.0) was red, and no 100T board is released.
#   ci/select.sh update      yes on integ-m1, main and tags v* (the tester update files are built for
#                            every commit there), or when UPDATE_FORCE=true (dispatch input `update`), or
#                            BUNDLE_INPUT=true (dispatch input `bundle`, T172), or the head commit message contains
#                            "[bundle]" (T172 D1), for any other branch; else no.
#
# Read from GITHUB_REF_NAME, GITHUB_REF_TYPE, GITHUB_EVENT_NAME, UE2_INPUT (the
# dispatch input; a called workflow does not see the caller's event inputs, so the
# pipeline passes it on with `with: ue2:`) and the message of HEAD, so the same
# call answers on the Mac for the checkout there.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ref="${GITHUB_REF_NAME:-$(git -C "$ROOT" rev-parse --abbrev-ref HEAD)}"
event="${GITHUB_EVENT_NAME:-}"
release=no
{ [ "${GITHUB_REF_TYPE:-}" = tag ] || [[ "$ref" == release/* ]]; } && release=yes
# SELECT_MSG stands in for the commit message (ci/lint.sh tests both cases; HEAD may carry "[sims]", which raises the push pipeline to tier 2)
msg="${SELECT_MSG-$(git -C "$ROOT" log -1 --format=%B)}"
case "${1:?sims-tier, ue2 or update}" in
  sims-tier)
    # Alex 2026-11-08: "the sims come out of the CI ... sims only on demand"; "after W1 we introduce an integration branch, a sim can run there".
    # 0 = no sim child at all (no bench has tier 0): a push to main or a feature branch builds apps, bitstreams and the package only.
    if [[ "$ref" == integ-* ]] || [ "$release" = yes ] || [ "${SIMS_INPUT:-}" = true ] || [[ "$msg" == *"[sims]"* ]]; then echo 2; else echo 0; fi ;;
  ue2)
    # UE2_INPUT is only ever true from a dispatch (a push has no such input); the event
    # name cannot be checked, a called workflow sees "workflow_call" there; a tag or a release/* branch alone no longer asks (2026-11-18)
    if [ "${UE2_INPUT:-}" = true ]; then echo yes; else echo no; fi ;;
  update)
    if [ "$ref" = integ-m1 ] || [ "$ref" = main ] || [ "${GITHUB_REF_TYPE:-}" = tag ] || [ "${UPDATE_FORCE:-}" = true ] \
       || [ "${BUNDLE_INPUT:-}" = true ] || [[ "$msg" == *"[bundle]"* ]]; then echo yes; else echo no; fi ;;
  *) echo "unknown question $1" >&2; exit 2 ;;
esac
