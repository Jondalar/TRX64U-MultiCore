# T238 — Faults in the experimental cores are reported through their own issue form, and the issue forms fit the released packages

**Status:** BUILT on main (2026-11-19); goes to GitHub with Alex's go. Earlier: PROPOSED (2026-11-19, Alex).
**Repo:** TRX64U-MultiCore — `.github/ISSUE_TEMPLATE/`, `CONTRIBUTING.md`, `tools/check_issue_forms.py` (lint); carried to
GitHub by `tools/github_snapshot.py` (only with Alex's go). No gateware, no application.
**Number:** T238.
**Origin:** Alex, 2026-11-19 (translated): "look at the issues now after the release, not to solve them - I want the templates
upgraded. We are doing support for other cores (I am not alone any more, the testers help from now on in parts) and I would like
there to be an 'experimental cores' area too." The same day on GitHub discussion #103: "I will enhance the templates with
'experimental cores' and people can then file issues for those. In my eyes it needs triage, where I would love to have your
support." The guardrails he proposed there: a real bug across several programs, or a general misbehaviour of the core, is looked
at, one at a time; a fault in one demo only, or with a rare piece of hardware, may be a bug but is not worth the effort. The
discussion stays open for 2-3 days.
**Depends on:** T161 (the issue forms and their lint).
**Milestone:** after release 0.2.0.

Open (one line, as in the TODO table): Built on main 11-19 (D1-D4, text OK); GitHub push + 4 labels wait for Alex's go (#103)

---

## §1 The problem

- **Who:** testers who find a fault in a core other than the host, the official firmwares, the generic core application and
  TRX64; Alex and the testers who triage those reports.
- **What happens today** (read 2026-11-19):
  - `bug.yml` offers in "Area" only host, firmware, coreapp, trx64u and trx64g (T161, 2026-11-14). Every other core goes to
    Discussions (`CONTRIBUTING.md`, "What we do not support").
  - The label `experimentalCore` ("Core marked experimental") exists; its 11 issues were all opened by the maintainers.
  - Issue #107 (a tester, release 0.2.0) used the bug form: the required photo and report fields carry "(none provided)" and
    "if this is really needed, I can reproduce the issue". The release field's placeholder still reads `trx64mc-main-922deb78`;
    releases are named `0.2.0` now (T221).
- **Parts:**
  - **P1** a bug form for the experimental cores.
  - **P2** the guardrails of discussion #103 asked in that form.
  - **P3** triage by testers: labels, and the GitHub role that lets them set labels and close issues.
  - **P4** `CONTRIBUTING.md` says what a report on an experimental core gets.
  - **P5** the existing forms fit the released packages (placeholders, required fields).

## §2 Scenarios — what proves it is solved

| # | Part | Scenario | Proof |
|---|---|---|---|
| S1 | P1 | The new form parses: every required field has an id and a label, no dropdown is empty | `tools/check_issue_forms.py` in lint; control: a field without id fails. Run 2026-11-19: "issue forms: 5 controls fail as required"; all four files also parse with Ruby's YAML |
| S2 | P1 | The experimental form's core list names exactly the shipped cores that are not supported (T161's list) and not excluded (D4) | lint; controls: a core missing from the form fails, `diag` in the form fails. Run 2026-11-19: "issue forms ok (3 forms, 5 supported areas, 15 experimental cores)" |
| S3 | P1 | On GitHub, "New issue" offers the bug report, the experimental core form, the feature request and the Discussions link; the experimental form sets `experimentalCore` | Alex looks after the snapshot push (his go) |

Scenarios for P2-P5 follow their decisions.

## §3 Decision

- **D1 (Alex, 2026-11-19): an own form for the experimental cores, beside "Bug report".** Asked: an own form, or an entry in the
  bug form's "Area" list; with the note that GitHub forms have no conditional fields, so in one form every reporter would see the
  triage questions, also for a host fault. Alex chose "Eigenes Formular" (own form). The preview he chose showed release, core,
  "How wide is it?" (every program or a general fault / several programs / one program or demo only / rare hardware or add-on),
  steps, what happened, what was expected, programs used (names and source), photo or video optional; the field details are a
  draft until built.
- **D2 (Alex, 2026-11-19): a fault in one program or demo only, or with rare hardware, is closed with a label after triage.**
  Asked with three ways (close with a label / keep open with `help wanted` / convert to a discussion); Alex chose "Schließen mit
  Label" (close with a label). The flow he chose: a new report carries `experimentalCore` + `triage`; a tester who reproduces it
  across several programs or as a general fault sets `confirmed` (open); missing information gets `needs-info` (open); one
  program or demo or rare hardware gets `edge-case` and is closed: it stays searchable and is reopened when more programs show the
  same fault. Alex picks his work from `confirmed`.
- **D3 (Alex, 2026-11-19): in the bug form the report file stays required, the photo or video becomes optional.** Asked with
  #107's filler texts and three ways (report required and photo optional / both required as T161 / both optional); Alex chose
  "Report Pflicht, Foto optional" (report required, photo optional). The photo field says it is needed when the fault shows on the
  screen (GitHub forms have no conditional requirement); the report field names F8 "Bug report", `/SD/report/<date>.txt` and
  `http://<board>/v1/report` without "once your release has it" (T162 ships since 0.2.0). Changes T161 §1 P1 (photos required).
- P3 roles (read 2026-11-19): the 7 testers have the GitHub role Write, which already sets labels and closes issues; no change is
  needed for the triage. Write also allows pushes; the role "Triage" would be enough. That is Alex's setting, outside this spec.
- **D4 (Alex, 2026-11-19): the experimental form as previewed, without the Port Diagnostics core and the two DDT demos.** The
  preview: labels `experimentalCore` + `triage`; a note that experimental cores come without guarantee, testers triage, a fault in
  one program or with rare hardware is closed as `edge-case` and reopened when more programs show it (#103); required: release
  (placeholder `0.2.0`), board, core, "How wide is it?" (every program or a general fault / several programs / one program or demo
  only / rare hardware or add-on), steps, what happened, what was expected, programs used (names and where they came from, never
  the files), the report file (as D3); optional: photo or video, anything else. Asked against "report optional" and "without
  diag and demos"; Alex chose "Ohne Diag und Demos" (without diag and demos). The core list is the shipped cores minus the
  supported areas minus `diag`, `ddt_mandel` and `ddt_novic`: 15 in 0.2.0. The lint names the three exceptions.
- **D5 (Alex, 2026-11-19): the `CONTRIBUTING.md` text as proposed** ("Ja"): "What we do not support" becomes "Experimental cores"
  (the 15 cores, no guarantee, the form, the triage steps of D2, demos and Port Diagnostics in Discussions, pull requests welcome
  for every core); "What an issue must contain" names the release example `0.2.0`, the core and its width, the report file and
  photos when the fault shows on the screen.
- Open: when it goes to GitHub (#103 runs 2-3 days).

## §4 Out

Nothing decided yet.

## §5 Built

- **2026-11-19, main.** `.github/ISSUE_TEMPLATE/experimental.yml` (D1, D4): labels `experimentalCore` + `triage`; the note of D4;
  required release (placeholder `0.2.0`), board, core, "How wide is it?", steps, what happened, what was expected, programs
  used, report file; optional photos, anything else. Core options are written `<id> - <name>` like the bug form's areas, so the
  lint reads the id. `bug.yml` (D3): photo optional ("Needed when the fault shows on the screen"), "Report file" required with
  F8 / `/SD/report/<date>.txt` / `/v1/report`, release placeholder `0.2.0`, the area text points to the experimental form, the
  intro names the report file instead of the photos. `tools/check_issue_forms.py`: the experimental form must exist; its core
  list equals the shipped cores (`tools/core_toml.py`, the filter of `tools/gen_roms_txt.py`) minus the supported areas minus
  `EXCLUDED` (`diag`, `ddt_mandel`, `ddt_novic`); the photos are optional in both forms; five controls. `CONTRIBUTING.md` (D5).
- **Open on GitHub (Alex's go):** the snapshot push, and the labels `triage`, `confirmed`, `needs-info`, `edge-case` (a label a
  form names that does not exist in the repository is not set; `experimentalCore` exists). Date: after #103 (2-3 days)?
