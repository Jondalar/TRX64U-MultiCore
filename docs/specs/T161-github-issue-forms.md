# T161 — a bug report on GitHub cannot be sent without version, core, steps, photos and the T162 report file

**Status:** ACCEPTED (2026-10-30), asked by Alex. Status check 2026-11-13: built: forms and lint in main (778289484), on GitHub; S3 (Alex looks) open.
**Repo:** TRX64U-MultiCore — `.github/ISSUE_TEMPLATE/` (issue forms), carried to GitHub by `tools/github_snapshot.py`. No gateware, no app.
**Number:** T161.
**Origin:** Alex, 2026-10-30 (translated): "Can we build templates into the GitHub issues? What I would like are photos, the boot config,
and so on. Maybe we should also build a state dump into the next host app, and they have to deliver it." On a one-click flow (F8 ->
bug report -> QR -> issue with the files): "better not ... too easy == more bugs. Data." Then on the summary (report file + REST, no QR;
the template requires the report): "Yes."
**Depends on:** T162 (the report file the form asks for); until a release has T162 the field asks for the `.cfg` files instead.
**Milestone:** M3.

---

## §1 The problem

- **Who:** Alex reading issues from testers on `Jondalar/TRX64U-MultiCore`.
- **Today:** issues are free text. Issues #7, #8, #9 named no release, no board, no configuration, no image source; each needed
  questions back before anything could be checked.
- **Parts (MECE):** **P1** a bug form: release (dropdown), board (C64U / U64E2), core (dropdown), steps, what happened, what was
  expected, image name and where it came from (never the file), photos or video of the screen, the T162 report file; all required
  except a free "anything else"; **P2** a feature form: core or host, what for, how it should behave; **P3** `config.yml`:
  `blank_issues_enabled: false`, questions go to Discussions; **P4** the forms reach GitHub with the snapshot.

## §2 Scenarios

```gherkin
Scenario: S1 the forms are valid
  Given .github/ISSUE_TEMPLATE/bug.yml, feature.yml, config.yml
  Then  a schema check in ci/lint.sh parses them (top-level keys, every required field has an id and a label, dropdowns not empty)
  # control: a field without id -> FAIL

Scenario: S2 the dropdowns follow the tree
  Given the cores in cores/*/core.toml that ship (not build_only)
  Then  the core dropdown of bug.yml names exactly those (lint); control: a core added without the form -> FAIL

Scenario: S3 GitHub
  Given the snapshot pushed (Alex's go)
  When  "New issue" is opened on GitHub
  Then  only the two forms and the Discussions link are offered; the bug form refuses to submit without the required fields (Alex looks)
```

## §3 Decision

- **D1 (Alex):** no one-click path from the board to an issue (no QR, no prefilled link): the reporter fetches the report and attaches
  it by hand. "too easy == more bugs".
- **D2:** the report file of T162 is a required field of the bug form once a release carries it; before that the field asks for
  `/SD/cores/<core>.cfg`.

## §4 Out

- A bot that checks attachments or closes incomplete issues.
- Templates for pull requests.

## §5 Decisions taken while building

- **2026-10-30, built.** `.github/ISSUE_TEMPLATE/bug.yml` (release as free text: the release names change with every set, a dropdown
  would go stale; board and core as dropdowns; steps, what happened, expected, images used, photos, report/configuration file all
  required; "Anything else" free), `feature.yml` (where, what for, how), `config.yml` (blank issues off, Discussions link). Check
  `tools/check_issue_forms.py --selftest` in `ci/lint.sh` (S1, S2): forms well formed, the core dropdown equals the shipped cores
  (`core.toml` without `build_only` and not `null`, `cores/*/mono`, `trx64u`, `trx64g`, `host`; 14 today); controls (a field
  without id, a core missing from the form, blank issues on) fail as required. No YAML library: the lint image is not assumed to
  carry one.
- **2026-10-30, GitHub (Alex):** "The forms to GitHub is ok and I will look at them there" (translated): the snapshot goes out with
  them; S3 is his look.
- 2026-11-14 (Alex: "ja", translated "yes", to cutting the list): the bug form names only the supported areas (host, official firmware, generic core application, trx64u, trx64g; CONTRIBUTING.md, discussion #66); tools/check_issue_forms.py checks that list.
