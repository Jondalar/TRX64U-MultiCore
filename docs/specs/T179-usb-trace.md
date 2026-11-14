# T179 — A USB trace shows why a core on a USB stick is slow or missing at power-on

**Status:** DECIDED (Q1-Q4 answered 2026-11-04); nothing built (no usb trace code in main; 2026-11-15 sync: confirmed, the syslog setting in coreapp/src/netops.cc is Gideon's stock one).
**Repo:** TRX64U-MultiCore — host app (`host-app/software/hostapp/`, the USB stack it compiles from `vendor/gideon/software/io/usb/`),
the T162 report, a board test script in `tools/`.
**Number:** T179.
**Origin:** Alex, 2026-11-04, after GitHub discussion #15 (a tester on c83a83c2 with the cores on a USB stick: "it still sometimes goes
straight to the menu at boot"; on an SD card "all speed / loading / default core at boot issues have gone"; another tester on 10-31: "USB for
cores is unreliable for me") and the same "USB drops when a lot happens, when flashing for example" seen in the TRX64-Ultimate firmware:
"then we should write a test trace concept and test it through locally here" (translated).
**Depends on:** T135 (direct boot, the #14 wait), T162 (report file), T120 (`vendor/gideon`).
**Milestone:** M3.

---

## §1 The problem

- **Who:** a user whose cores (or default core) are on a USB stick.
- **What happens:** sometimes the direct boot lands in the menu instead of starting the default; loading from USB is slow. Not reproduced by us;
  the #14 board check (T135 S13) was never run.
- **What we know (read, 2026-11-04):**
  - The #14 fix (`3c876683e`, in c83a83c2) waits up to `BOOT_WAIT_MS` 30 s for the medium's root, then decides "missing" once the root has been
    listed for `DB_GRACE_MS` 1.5 s and the file is absent (`directboot.h:53-56`, `hostui.cc:5533`).
  - The mass-storage driver (`usb_scsi.cc`) has retries, a mass-storage reset and 55 `printf`s on the console UART; none reaches the user, the
    report or a file. A drop, a reset or a retry leaves no trace a tester can send.
  - Our USB sources equal Gideon's / the community edition's for mass storage (`usb_scsi`, `usb2` gateware, FAT: byte-equal); `usb_base.cc` is
    ahead (GideonZ#822 mutex fix). There is no upstream fix to take.
- **Parts:**
  - **P1 Trace.** What happens on the USB side, with times: hub/port events, enumeration, mount, every SCSI command that fails, retries,
    resets, a device that goes away and comes back, and the read speed of a `.cor` load.
  - **P2 Direct boot decision.** The wait itself: when the root was first listed, when the file was looked up, why "missing" was decided.
  - **P3 Getting it out.** Into the T162 report (F8 Bug report, `GET /v1/report`), so a tester can attach it; on the console as today.
  - **P4 Local test.** A repeatable run on Alex's board: cores on a USB stick, N power-ups and N core starts, with and without load (flash write,
    FTP, a second USB device), counting how often the default is missed and how long loads take.

## §2 Scenarios (draft; proofs fixed once §3 is decided)

- S1 A `.cor` start from USB writes trace lines (mount, read of the file, bytes and ms) that the report then contains. Proof: host app bench with a
  fake USB device; board.
- S2 A SCSI command that fails and is retried shows as a trace line with the command, the error and the retry. Negative control: a bench device that
  injects one failure; with the trace hook removed the check must fail.
- S3 A stick that disappears and comes back (port disconnect / re-enumeration) shows both events with times. Proof: bench, then board (Alex pulls
  the stick once).
- S4 A direct boot that ends in the menu names in the report: when the root was listed, when the lookup ran, the decision and its reason.
  Proof: `tests/hostapp/test_directboot.cc` with a late root; board.
- S5 The local test (P4) runs unattended over JTAG/REST/FTP and writes one result table: starts, misses, trace excerpt per miss, load times.
  Proof: the script's own dry run against a model, then Alex's board.
- S6 The trace costs nothing visible: start-up time of the host menu and of a `.cor` load from SD unchanged within noise (T146 numbers).

## §3 Proposals (nothing decided)

- **R1 A trace ring in the host app** (fixed size, e.g. 16 KiB of text lines with a ms time stamp), fed by hooks in `usb_base.cc` / `usb_scsi.cc`
  (our copy, as a patch under `tools/patches/gideon`) and in the direct-boot wait; copied into the T162 report as its own section.
- **R2 The local test** as `tools/usbtrace_run.py`: power-ups are Alex's (never by Claude), so it runs N core starts and returns to the host over
  REST, reads the report after each, and collects misses; a power-up series is a short list for Alex.
- **R3 Load during the test:** a flash write (the T152 update path on a test file), an FTP upload, a large USB read in parallel.

## §3a Open questions (one at a time)

1. Scope of the trace: host app only, or the core app too (a core reads its media from USB while it runs)?
   **Answered (Alex, 2026-11-04): both** - "Oof yes, hard disk images can come from USB too" (translated). Host app and core app
   (disk and hard disk images, `.hdf`, ADF, D64, ... read from USB while a core runs).
2. Where the ring lives: host app RAM only (lost when a core starts), or a DDR2 area that survives the core start and the return to the host?
   **Answered differently (Alex, 2026-11-04):** "the C128 has no network, but how about logging it away to a syslog server like the original
   firmware simply does? And a small Python job here for it? In the 1541Ultimate firmware folder/repo there is already a listener ... and then that
   into the settings in all apps" (translated). So the transport is syslog over UDP, as Gideon's firmware does it (`vendor/gideon/software/network/
   syslog.cc`, setting `CFG_NETWORK_REMOTE_SYSLOG_SERVER` "Log to Syslog Server", ip[:port], default port 514; `printf` reaches it); today only the
   TRX64U menu has the row (`coreapp/cores/trx64u/menu.toml:2980`, `netops.cc:45`), neither our host app nor the core app links `syslog.cc`.
   The row "Log to Syslog Server" goes into the settings of every app (host and every core app); the receiver is a small Python job in
   `tools/` after Gideon's `python/syslogd.py` (1541ultimate repo, UDP 514 or 64514).
   **Without network (Alex, 2026-11-04):** "log into the file system (SD or USB) but with rotate, so max lines in steps of 1000" (translated;
   read as: a log file on SD or USB, rotated when it reaches a maximum line count that the setting offers in steps of 1000).
3. Always on, or switched on in F8 (cost on start-up time, S6)?
   **Answered (Alex, 2026-11-04): "only when you switch it on explicitly in the F8 settings"** (translated). Default off in every app;
   syslog only with a server entered, the file log only with a line count chosen (Off / 1000 / 2000 / ...).
4. The local test: which stick(s) (Alex's slow one, a fast one), how many power-ups by hand, and does the TRX64-Ultimate flashing case belong here
   or to the firmware project?
   **Partly answered (Alex, 2026-11-04):** the fast stick = SanDisk "3.2Gen1", 123 GB, exFAT, label `Test` (seen on the Mac: `Amiga/`, `roms/`);
   the slow one is the stick on the board, and Alex has another slow one. Power-ups by hand: 10 per stick ("10 per stick is good",
   translated). The TRX64-Ultimate flashing case stays out (Alex: "no").
   **Alex's hypothesis (2026-11-04, translated):** "I personally believe the USB or I/O part is 'bad' in hardware and there are power problems."
   So the trace must tell a power drop from a software fault: port disconnect / over-current / re-enumeration events with times (a stick that
   loses power re-enumerates; a software stall does not), and the local test adds a run with each stick behind a powered USB hub. If the
   powered hub removes the misses, the cause is power, not the driver.

## §4 Out (to decide with Alex)

- The USB drop while flashing in the TRX64-Ultimate firmware (Alex, 2026-11-04: not part of this test).

- Changing the USB driver itself before the trace shows a cause.

## §5 Build notes

Not started.
