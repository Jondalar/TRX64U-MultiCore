# T160 — every core has a master volume, set in the menu with SHIFT+ / SHIFT-, applied in the host

**Status:** ACCEPTED (2026-10-29), asked by Alex. Status check 2026-11-13: built and merged: A1200 trial (board S3 pass 10-31), host gain T164 WP11; board S3 on another core open. 2026-11-15 sync: host gain confirmed in code (`host/trx_gain.v`, `host/top.v:971-973`); the 'Host plan (not built)' bullet is now marked built.
**Repo:** TRX64U-MultiCore — host gateware (a gain on the core's audio before HDMI and I2S), the generic core application (setting,
keys), the host application (its own menu, if wanted). Trial first in the A1200's own files (§3 D2).
**Number:** T160.
**Origin:** Alex, 2026-10-29 (translated): "does every core have that? So volume? The C64 cores yes ... the others? SHIFT+ / SHIFT-
would be ideal for it, so in the menu, not in the running core." Then: "audio volume in the host then. Test it with the Amiga core too."
**Depends on:** T104 (generic app), the next host change (D7/D8 of T107, T152).
**Milestone:** M3.

---

## §1 The problem

- **Today:** only the C64 cores have volumes (`coreapp/cores/trx64u/menu.toml:1745-1781`, the TRX64 bitstream's mixer). Every other
  core's `audio_l/r` (`contract/layer2_ports.txt:250-251`) goes to HDMI and I2S without a gain stage (`host/top.v:925-935`,
  `snd_l <= pause ? 0 : audio_l`). The application's mixer registers `mix_l`, `mix_r` exist in the host (`host/top.v:447, 680`, out of
  `ultimate_sys`) and drive nothing.
- **Parts:** **P1** host: `snd_l/r = audio_l/r * mix_l/r` (8-bit gain, 2 DSP, a few LUT; 51 DSP free in pb_l1, T107 §5.7), unity
  default so a core without the setting sounds as today; **P2** generic app: setting "Volume" (steps, default unity, kept per core in
  the `.cfg`), SHIFT+ / SHIFT- change it while the menu is open (not in the running core), a short on-screen value; **P3** the same
  for the host app's own sound if any; **P4** the C64 cores keep their own mixer (TRX64 bitstream, not this host).

## §2 Scenarios

```gherkin
Scenario: S1 the gain
  Given a core sample 0x400000 and mix 128 of 255 (or the chosen scale)
  Then  HDMI and I2S carry the scaled sample; unity passes the sample unchanged; 0 is silent
  # iverilog bench of the gain stage; control: gain bypassed -> FAIL

Scenario: S2 the keys
  Given the menu open in a core
  When  SHIFT+ is pressed three times, SHIFT- once
  Then  the volume is two steps up, written to the register, shown, and back after a restart (.cfg)
  # tests/coreapp; control: keys ignored -> FAIL

Scenario: S3 board (A1200 trial, then every core)
  Given the A1200 playing sound
  Then  SHIFT+ / SHIFT- in the menu change the loudness audibly, on HDMI and on the analogue output
```

## §3 Decision

- **D1 (Alex): in the host**, once for all cores, with the next host change.
- **D2 (Alex: "test it with the Amiga core too"):** the trial goes into the A1200 first (large core, flat build), so the host and its
  key do not change before the host package; the app part (P2) is the one that later serves every core.

## §4 Out

- Per-channel mixing (Paula channels, SID voices) and tone controls.

## §5 Decisions taken while building

- **A1200 trial (2026-10-29, built, CI builds the bitstream).** Gateware: `cores/amiga1200/rtl/trx_gain.v` (new file, no fork): `out = sat24(in * vol / 128)`,
  one 24x9 multiplier per channel (one DSP48 each), registered once in `mclk`; the volume byte is taken through two flip-flops and used only when two
  consecutive reads agree. It sits between `amiga_audio` and the core's `audio_l/r` in the A1200's `amiga_core.v` fork. Register `VOL_CFG` 0x0A9 in the A1200's
  `amiga_regs.v` fork (reset 128 = unity, written at once, no commit toggle, readable in the free byte of word 0x2A); the `.diff` files are regenerated, `check_wrapper_fork.sh` is green.
  The shared `cores/amiga` files and the A500 are untouched (the A500 has no gain stage and no Volume setting). Nothing in `host/`, `contract/`, `cores/c64ref`.
- **Scale:** 128 = unity (100 %), 255 = +6 dB (saturates, never wraps), 0 = silent. The setting "Volume" has 41 values, 0 % .. 200 % in steps of 5 % of unity (200 % = 255; until 2026-10-30 26 values up to 125 %),
  default 100 %, `WHEN_LIVE`, kept in `<name>.cfg` as `Volume=110%`; the byte is round(percent * 1.28) from a table in `coreapp/cores/amiga.cc`.
- **Generic app:** `VOLUME_KEY` ("Volume", `desc.h`): a core whose descriptor has a setting of that name WITH a register binding gets the keys; any other core
  ignores them. `MenuCtl::change_volume` (`menuctl.cc`) steps by one, clamps at both ends, goes through `settings.set` (the same path as a REST client and the menu row),
  and shows `Volume 110%` (plus `(silent)` / `(maximum)` at the ends) in the status row. Only in the idle menu (not in a text, a question, the monitor, an application screen), never in the running core.
- **Keys as they arrive:** the C64 keyboard delivers SHIFT+`+` as `{` and SHIFT+`-` as `}` (`keymap_shifted` of `keyboard_c64.cc`); a USB keyboard delivers
  SHIFT+`=` (and the keypad `+`) as `+`, SHIFT+`-` as `_`, the bare and keypad `-` as `-` (`keyboard_usb.cc`). Up: `{` `+`. Down: `}` `_` `-`. The bare C64 `+` and `-` keys arrive
  as `+` and `-` too and cannot be told from the USB ones, so they step as well.
- **Measured:** bench `amiga1200_gain` (`cores/amiga1200/sim/gain/`, iverilog): unity exact for 0, +-1, both extremes and 200 random samples, 0 silent, 64 = half, 32 = quarter with the channels
  apart, 255 = 1.992x and saturating on both signs; control `TRX_GAIN_MUT_BYPASS` fails. `tests/coreapp` `volume` plus four controls (keys ignored, register not written, no clamp, USB down keys lost).
- **Host plan (as planned; BUILT 2026-10-31 in T164 WP11, see "Host version built" below; D1):** in `host/top.v` the cores' `audio_l/r` (`:925-935`) get `snd_l <= pause ? 0 : sat24(audio_l * mix_l / 128)` (the same `trx_gain` module,
  `mix_l/mix_r` of `:447, :680` are unused today), one DSP per channel (51 free in pb_l1, T107 §5.7). To check before: the reset value of `mix_l/mix_r` out of `ultimate_sys` must be unity
  (else the core sounds different without the setting) and the generic app needs a way to write them (the host's register, today only the Ultimate app does); until then C64 cores keep
  their own mixer (P4). When the host has it, the A1200 copy (`trx_gain` in `amiga_core.v`, `VOL_CFG`) is removed and `AM_VOL_CFG` becomes the host's register, the setting and keys stay as they are.
- **Board (S3), 2026-10-30 (Alex, A1200 `.cor` of run 1060, translated):** "volume does something too, but I fear for some cores not enough yet":
  SHIFT+ / SHIFT- change the loudness audibly on HDMI; the top step (125 %) may be too low for quiet cores. Analogue output not tried. **Decision (Alex, 2026-10-30: "ja" to steps up to 200 %):** the table goes to 200 % (byte 255); above
  unity loud material saturates.
- **Board, 200 % (2026-10-30, A1200 `.cor` of run 253):** asked "is it enough now, and does it distort too much?", Alex: "ok, and yes" - loud
  enough now; at the top steps loud material distorts (saturation, as designed).
- **Host version built (2026-10-31, T164 WP11):** `host/trx_gain.v` in `host/top.v` for every core; the register is a byte of its own (`U64_AUDIO_MIXER + 0x50`, reset
  unity), not `mix_l/mix_r` (they also feed the SID mixer: counted twice); the generic application adds "Volume" to every core without one (C64: `own_volume`);
  the keys' line is a toast over every page. Details and bench lines: T164 §5 WP11. Open: the A1200's copy goes in WP18; board S3 on another core.
- **Board (S3), 2026-10-31, Sony TV (Alex):** louder and quieter on HDMI; analogue: "sound comes out fine" with the steps. S3 passed. UX: the
  "Volume xx %" status line shows only on F5 -> Settings; on every other page the keys work blind - it must show everywhere.
