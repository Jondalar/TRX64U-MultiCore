# T244 — Every message of the core application shows its whole text on the screen

**Status:** BUILDING (2026-11-20).
**Repo:** TRX64U-MultiCore — the core application (`coreapp/src/ui.cc`, `coreapp/src/screens.cc`, `coreapp/src/menuctl.h`) and the menu simulator (`tools/menusim`).
**Number:** T244.
**Origin:** GitHub issue #109 (a tester, release 0.2.0, C64 Ultimate): "SNES Core - Warning text off screen". A DSP game without the DSP ROM gives an error whose text runs off the screen. Expected: the text is readable. Same for any cartridge that produces a missing ROM at launch. Alex asked for #109 before the integration (2026-11-20).
**Depends on:** nothing.
**Milestone:** M1.

---

## §1 The problem

The tester starts a DSP cartridge (Run in the Library) without the DSP firmware. The core's load step refuses with one sentence of about 190 characters (`coreapp/cores/snes.cc`, the "needs the %s coprocessor ... is missing" line).

Read from the code and from the tester's photo:

- The sentence does not reach the message pane of `ui_message()`. It goes through the operation result into `MenuCtl::set_status()`. The list draws it as the status line at the pane's foot (`ui.cc`, `g_status`).
- That line is one label at `content_h - 20`, in the caption font, wrapped at the pane width. The first line is drawn. The second line lies below the footer and is cut. The photo shows exactly that: the first line, the rest hidden.
- The list window keeps 24 px for the foot whatever the text needs. A second line cannot take room from the rows.
- `MenuCtl::status_[200]` is shorter than an operation's message (`OP_MSG_MAX` 240). A message of 200 to 239 characters is cut silently, before any drawing. A longer title or folder name makes the DSP sentence that long.
- The concept screens (`screens.cc`, `hint_make`) have the same one-line hint at `content_h - 22` and the same 200-byte copies.

Parts:

- P1 The status line of a list shows only its first line.
- P2 A message of 200 to 239 characters is cut before it is drawn.
- P3 A long word (a path without spaces) must stay inside the width.
- P4 The hint line of the concept screens has the same one-line limit.
- P5 The message pane (`ui_message`) must keep showing its whole text and must not change for short texts. It is capped at 220 px, which holds today's texts. Nothing guarantees it for a longer one.

## §2 Scenarios

| # | Solves | Scenario | Proof |
|---|---|---|---|
| S1 | P1, P2 | The SNES DSP-missing sentence (PILOTWINGS, 191 characters) is fully visible in the Library, at 1080p and at 480p, above the footer. | menusim pictures `before/s1_status_dsp.png`, `after/s1_status_dsp.png`, `after/s1_status_dsp_480p.png` |
| S2 | P3 | A status of 213 characters with a path of 200 characters without a space stays inside the width on 3 lines. | `after/s2_status_longpath.png`, `after/s2_status_nobreak.png`; the message pane: `after/s2_msg_longpath.png` |
| S3 | P1 | A short status and a short message look as before. | `cmp` of `before/s3_status_short.png` with `after/s3_status_short.png`, and `s3_msg_short.png`: byte-identical |
| S4 | P5 | The picture question and the other users of the message pane (`run.cc` saving and loading notes) look right. | `after/s4_picq.png`. The pane's height rule is unchanged below 220 px (S3 is identical). |

All pictures are in `/private/tmp/claude-501/t244/{before,after}/`, taken with `tools/menusim/run.sh snes --keys F3 --say TEXT --shot` and `--message TITLE TEXT --shot`. These are menu-simulator pictures at the coordinator's instruction, not ue2emu pictures (ux-review §3 asks for ue2emu).

## §3 Decision

D1. The status line grows upward. Its label wraps. Its top is `content_h - 20 - extra`, where `extra` is the height the wrapped text needs beyond one line (`ui_note_extra_h`, measured with LVGL on the same width). The list window gives that height back: `list_foot()` is 24 plus `extra`. A one-line status has `extra` 0, so it stays where it was. The line count is part of the list's shape, so a change of line count builds the pane again; a change of text with the same count is still written in place.

D2. `status_[]`, the hint copy and the hint buffer are `OP_MSG_MAX` long, the size of the message they carry.

D3. The concept screens' hint line takes the same rule: the label wraps and its top moves up by `extra`. The screen's own layout is not resized (§4).

D4. The message pane: the panel grows to the text, capped at the content area below its top (`content_h - 60 - space_lg`) instead of 220. The shown text is at most 400 bytes (`msg_text_s`). At the heading font that is 6 to 9 lines in the narrowest canvas, about 190 px. It fits under the cap, so no smaller font and no scrolling is needed. A message pane has no key input, so scrolling would need new plumbing for nothing.

D5. The words break inside the width by LVGL's own wrapping (S2 shows it with and without separators). No code of ours for it.

D6. Nothing names a core. The SNES sentence is only the test case.

D7. The simulator gets `--say TEXT` (the status as a failed start leaves it) and `--message TITLE TEXT` (the pane as `run.cc` raises it). This is tool code in `tools/menusim/menusim.cc`. The product has no test path for it.

## §4 Out

- The concept screens' body is not resized for a long hint. A hint of more than one line grows over the space above it. No screen has such a hint today. Not proven with a picture.
- The top-bar chip (`chip_say[96]`) keeps its 96 characters. It is a chip, and the status line carries the whole text.
- Scrolling a message and a smaller font are not built (D4).
- `msg_text_s[400]` stays. Longer texts are cut at 399 bytes, as before.
- Home draws no status line, as before.

## §5 Decisions taken while building

- The coordinator's brief assumed the text goes through `ui_message()`. The photo and the code show the status line. The first reproduction (`before/s1_status_dsp.png`) matches the photo. The message pane already wrapped and showed the DSP sentence and a long path in full (`before/s2_msg_longpath.png`).
- The RISC-V build and the render benches have not run (brief). `ui_list_probe` now reads `rows_bottom` from the status label, and the render bench's probe asserts depend on it. The coordinator's single test run covers it.
