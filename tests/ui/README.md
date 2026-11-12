# UI suite (T153)

Gherkin `.feature` files run against the real application image (`coreapp.app`) in ue2emu, headless. The steps assert on the UI **state**
(`GET /v1/ui/state` of the application: the UI service's snapshot, T192.1 contract 2, `coreapp/src/uiapp.cc`; the v1 state is gone since T192.3), never on pixels; a screenshot is only attached when a step fails
(`tests/ui/out/fail_<feature>__<scenario>.png`). Python 3 standard library only. Contract and decisions: `docs/specs/T153-ui-sdk-and-tests.md`.

## Run

Two cores: `features/` runs against the TRX64U menu, `features_a1200/` (first line `# core: amiga1200`) against the Amiga 1200 core menu (profile `tests/coreapp/emu/profiles/amiga1200.toml`, T192.3 section 5, D26).
A feature file is one emulator session of its core; `run.sh` without arguments runs both directories.

```
tools/dev.sh riscv                     # the image: .ci/dev/<branch>/coreapp.app; then COREAPP=<that path> tests/ui/run.sh
tests/ui/build.sh                      # older route (a copied tree on the builder keeps deleted files: check that it really built)
tests/ui/run.sh                        # every feature (about 1 minute)
tests/ui/run.sh features/library.feature --keep-going
tests/ui/run.sh features/settings.feature --only "Discard"     # scenarios whose name has this text
```

Needs: `~/Development/u64-emulator/target/release/ue2emu` (`UE2EMU=`), the emulator's ROM folder `firmware/1541ultimate/roms` (`UE2EMU_ROMS=`), and the image
(`COREAPP=` to take another). The Mac runs one emulator at a time; the ports are 47411 (control) and 47480 (REST) (`UI_CTL_PORT`, `UI_REST_PORT`).
`build.sh` copies the worktree to `~/t153` on the host `builder` (a copy of a tree that built coreapp once; `BUILDER=`, `BUILD_TREE=`), runs the build image
with `nice`, a time limit and `docker run --rm`, and fetches the result.

One emulator session per feature file, started on a fresh stick (made by `fixtures.py`: two D64s and a PRG, nothing third-party) and a fresh flash image,
so a setting a scenario changes lives only inside its feature. Inside a feature the scenarios run in order and say what they need (`I set "Auto save config" to ...`).

## Files

| File | What |
|---|---|
| `features/*.feature` | the suite: navigation, settings, letters, library, home, pack (pack runs without the emulator: first line `# no-emulator`) |
| `runner.py` | the Gherkin subset: `Feature`, `Background`, `Scenario`, `Given/When/Then/And/But`, comments; `--keep-going`, `--only` |
| `steps.py` | the steps (a regular expression and a function each) and `Ctx` (the state, key presses that wait for the menu to take them) |
| `emu.py` | starts ue2emu, speaks the control port (`key`, `wait`, `png`, `button`), reads the state over REST |
| `fixtures.py` | the stick of the test machine |

## Writing a step

```python
@step(r'the page title is "([^"]+)"')
def then_title(c, t):
    expect_eq("page title", t, c.refresh()['title'])
```

An assertion raises `AssertionFailed` (expected and actual in the message); a step that cannot do its job raises `StepError`. A key press waits until the
snapshot's `rev` moved, so no fixed sleeps.

## What a run proves, and what not

It proves what the application's own model says is on the screen for the keys pressed (menu controller, layout items, dialogs, status and footer text, the
pack-time check). It does not prove the pixels (the widget layer `app/view/trxview.cc` draws what the model says; that is looked at, not asserted) and not the
hardware functions behind an op (power, Wi-Fi, SID, streams): those are board steps (`docs/archive/board-tests/board-tests-mc.md`).
