# 2. Installation

## 2.1 What is in the release

2.1.1 A release of the multi-core firmware contains these files. `<version>` is the version of the release (for example `0.2.0`).

| File | What it is |
|---|---|
| `trx64host_update_<version>.ue2` | The installer. Use it on a board that runs the Commodore firmware 1.1.0 |
| `trx64host_update_<version>.cfw` | The same installer, with a pre-header. For a board whose file browser takes a `.cfw` |
| `trx64host_update_<version>.txu` | The update of a host that runs already (2.1.4) |
| `<core>.cor` | One file per core |
| `unzip.u2a` | The application that unpacks a `.zip` (File Browser **Z**, core menus). The installer copies them to `cores/apps` (2.4.4) |
| `SHA256SUMS` | The checksums of the files above |
| `INSTALL.md` | The guide that belongs to this release |
| `ROMS.txt` | What each core needs in its own folder and where the files can legally come from (chapter 12) |

2.1.1a The release is also one `.zip` that holds all of these files. A `.zip` never holds a core folder.

2.1.2 The installer writes two things to the flash. Nothing else in the flash changes.

- Flash `0x000000`: the bitstream. It holds the host (layer 1) and an empty core (the null core, no machine).
- Flash `0x220000`: the host application. It is the program that shows the boot menu and starts the cores (chapter 4).

2.1.3 The cores are not written to the flash. The installer copies them to a stick or a card (2.4.4).

2.1.4 A `.txu` file (`trx64host_update_<version>.txu`) updates a board that runs the host already. It is not an installer. Use it as in 9.3.

## 2.2 The procedure in short

2.2.1 The installation has these steps.

| Step | Action | Result |
|---|---|---|
| 1 | Put the `.ue2`, the `.cor` files and a medium with free space together (2.3) | The medium is ready |
| 2 | Open the file browser of the firmware that runs on the board. Select the `.ue2`. Pick **Run Update** | The updater starts |
| 3 | Answer the two questions about the warranty (2.4.2) | The updater continues |
| 4 | Choose the medium for the backup and the cores, if it asks (2.4.3) | The updater writes the backup |
| 5 | Wait for `Update OK` | The board switches off |
| 6 | Switch the power off and on | The board starts the Commodore firmware from the backup (2.6) |
| 7 | Hold **C=** at the next power-on | The boot menu of the host shows (chapter 3) |

## 2.3 Prepare the medium

2.3.1 Use a USB stick or an SD card, formatted FAT32 or exFAT. The medium must have free space for the backup, the restore
bundle, the `.cor` of the old firmware and the files that you copy. The updater shows the size that it needs (2.4.3).

2.3.2 Copy the `.ue2` to the medium. Copy the `.cor` files of the release into the **same folder** as the `.ue2`.

2.3.3 The updater searches the media for the `.ue2` or `.cfw` of this very package (the root of each medium and up to three folder levels). It copies every `.cor`, `.cfw`
and `.u2a` that lies in the same folder to the chosen medium (up to 64 files in all): `.cor` and `.cfw` to `cores`, `.u2a` to `cores/apps`. It does not copy a file whose name starts
with `trx64host_update`, `trx64_update` or `trxbk`, and not the installer itself.

## 2.4 Install the host

> **WARNING** The updater writes the flash of the board. A power loss during the write can leave the board unable to start.
> Do not remove power while the updater runs. The updater verifies each part after it writes and repeats a failed write up to three
> times.

2.4.1 Put the medium in the board. Switch the board on. Open the file browser of the firmware that runs. Select the `.ue2`. Pick
**Run Update**.

2.4.2 The updater shows the title `** TRX64 Ultimate Updater **` and these lines:

- the date and time of the board, if it has a valid clock,
- `Detected Flash:` and the type of the flash chip,
- `Detected FPGA Type: XC7A50T.` and `Board Revision:`,
- `Checking the package..`, then `Package OK.`,
- `Installed:` the state of the flash (`bitstream AXSS <key>` or `bitstream: no stamp (not ours)`, the application, the running core), and `Package <id>:` with the data of the new package,
- `firmware: ours (TRX64)` or `firmware: not ours`. A firmware is ours when its bitstream carries the stamp and its application in the flash holds the word `TRX64`. A firmware with a stamp and without the word shows `not ours (stamped, no TRX64)`,
- `started by a host: the running firmware is not in the flash`, when the host started the updater (2.4.9).

Then it asks two questions, on every install of this firmware (also over an earlier install of it). The button `No` is preselected in both. `No` to either ends the run: `Cancelled. Nothing written.` and `Back to the menu..`

| Question | Answer |
|---|---|
| `You are leaving the warranty of Gideon and Commodore. Do you want that?` | `Yes` |
| `Do you really want that?` | `Yes` |

2.4.3 If the installed firmware is not ours (a vendor firmware, or a self-built one without the word `TRX64` in its application), the updater sets the machine up before it
writes the flash:

1. If it cannot pack the old firmware as a `.cor`, it shows the reason and asks `Go on without it?`.
2. It shows `Firmware files beside the update file:` with the folder and the number of files. If it cannot find the `.ue2`, it shows
   `The update file was not found on SD or USB, so no firmware files are copied.`
3. It looks for a medium with enough free space. If there is none, it asks `No SD/USB with x.y MB free for the cores. Make room. Retry?`
   **No** ends the update. Nothing is written.
4. If an SD card and a USB stick both qualify, it asks `Where should the backup and the cores go?` with the buttons `SD` and `USB`.
   The first button is the medium that holds the `.ue2`.
5. It makes the folder `cores` on that medium. If it cannot, it shows `Cannot create the cores folder. Nothing is written to the flash.`

2.4.4 The updater writes into `<medium>/cores/`, before it writes the flash:

| File | What it is |
|---|---|
| `TRXBK_<date>_nnn.BIN` | The raw backup: bitstream, application and the configuration pages of the flash |
| `TRXBK_<date>_nnn.CFW` | An installable package of the previous state: bitstream, application and the configuration pages (2.5) |
| `<vendor>-<version>.cor` | The previous firmware as a large `.cor` (for example `commodore-110.cor`). The boot menu shows it with the badge `backup` |
| The `.cor` and `.cfw` files from the folder of the `.ue2` | The cores of the release (2.3.3). A file that exists with the same content is skipped |
| `<medium>/cores/<name>/`, with `roms/`, `saves/` and `configs/` | The core folder (2.7.1) of every `.cor` the updater copied, and of the backup `.cor`. A folder that exists stays as it is. A `.cfw` file gets none |
| `<medium>/cores/apps/*.u2a` | The applications from the folder of the `.ue2` (`unzip.u2a`). They are not menu entries |

The updater ends the copy with `Files: n copied, n same, n failed`. If a file failed it asks `Not every file could be copied. Go on anyway?`. If the folder holds more files than the updater takes (64), it shows `n more file(s) left out`. It shows `Core folders: n, m could not be made`.

2.4.5 `<date>` is the date of the board's clock (`YYYYMMDD`). With no valid date, the names are `TRXBKnnn.BIN` and `TRXBKnnn.CFW`. The name of
the `.cor` is the vendor and the version of the old firmware without dots (for example `commodore-110.cor`, `gideon-315.cor`; a version that starts with another digit gives `firmware-<version>.cor`). If the
updater cannot read the version, the name is `backup<date>.cor` (`backup20261113.cor`; `backup.cor` with no date) and the entry shows `Backup <date>`. `nnn` counts up from 000, so a new backup never replaces an older one. After each file the updater reads it back and compares it.

2.4.6 The updater writes `Flash:/boot.cfg` (the boot menu, 8.9). The old `boot.cfg` of an earlier install is kept as `boot.cfg.bak`. The new
file holds: countdown 5, direct boot on, the backup `.cor` as the default (entry 1), and one entry for every copied core, up to 32 entries. If more cores were copied, it shows `n core(s) not added: the menu holds 32.` If it cannot
write `boot.cfg`, it asks `Flash:/boot.cfg cannot be written. The cores stay on the medium. Go on anyway?`

2.4.7 If the backup cannot be written, the updater shows `The backup could not be written. Nothing is written to the flash.` and returns to
the menu of the firmware. No backup, no update.

2.4.8 If the installed firmware is ours (2.4.2), the updater does not set the machine up: it asks the two questions, writes no `.cor` of the old firmware, copies no core and does not touch `boot.cfg`. It
makes the backup (`TRXBK_<date>_nnn.BIN` and `.CFW`) in the root of the first medium that has room, not in `cores`. It shows `Backing up the flash (x.y MB) to USB/SD..`. If no medium has room it asks
`No USB/SD with x.y MB free. Insert one. Retry?`. **No** ends the run with `No backup, no update. Nothing written.`

2.4.9 If the host started the updater (a `.cfw` or `.ue2` started from the boot menu, chapter 13) and the flash holds this firmware, the updater makes no backup. It shows `No backup of the flash: it holds MultiCore, not the
firmware that is running.` A backup of the host cannot bring the running firmware back.

> **NOTE** The raw `.BIN` and the `.CFW` hold the configuration pages of the flash. Keep the medium safe.

## 2.5 The write

2.5.1 The updater writes the application, then the bitstream. After each part it shows `Verifying the flashed application..` and
`Verifying the flashed bitstream..` and compares the flash with the package.

2.5.2 On a failed write the updater shows `Rewriting the application (try 2)` or the same for the bitstream. It tries up to three times. If
it still fails, it stops with `Update FAILED:` and the name of the part, and `Do NOT switch off.` Do not switch off. Report the screen (15.4).

2.5.3 At the end the updater shows `Update OK, installed <id>` and `First start: <name>` and `C= at power-on shows the menu.` Then it
switches the board off.

## 2.6 Power cycle

2.6.1 Switch the power off, wait a few seconds, then switch it on.

2.6.2 The host starts. Direct boot is on and the default entry is the backup of the old firmware (2.4.6). The board therefore starts the
old firmware, as before the install, and the host menu does not show. At the first start the host writes the bitstream of the old firmware once into the flash drive (`/Flash/legacy`, 13.2.6); later starts only check that file. Without a backup `.cor` the default is the first core that the updater added.

2.6.3 To see the host menu: switch the power off. Hold **C=** (or the fire button of joystick port 1) and switch the power on. The rules
are in 4.7.

2.6.4 The menu shows the entries of 2.4.4. Continue with chapter 3.

## 2.7 The medium

2.7.1 The layout of a medium after the install:

```
/cores/                  the .cor files of the release, the backup .cor, TRXBK_*.BIN and TRXBK_*.CFW
    trx64.cor  vc20.cor  amiga.cor  ...
    apps/                unzip.u2a: the application that every core and the File Browser can start
    <name>/              one folder per core, made by the installer for every `.cor` it copies, else by the core at its first start; never part of a release
        <name>.cfg       its settings
        roms/            its ROM files (chapter 12)
        saves/           what it saves
        configs/         its named configs, <config>.cfg, up to 16
anywhere                 your media: .adf .hdf .atr .xex .d64 .prg .nes .dsk ...
```

2.7.2 **Cores.** The host lists the cores that are in `boot.cfg` (chapter 4). You add a `.cor` or `.cfw` with **A** in the File Browser (5.4).
The File Browser shows every folder of a medium, so a core can lie anywhere. Keep them in `cores` for a clear layout.

2.7.3 **ROMs.** A core reads its ROM files from `roms/` in its own folder and nowhere else (12.2). The C64 cores use the ROMs that are in the
board's flash. The updater does not write them.

2.7.4 **Media.** Disks, games and images can lie anywhere. You browse to them from the core's menu.

2.7.5 **Settings.** A core keeps its settings in `<name>.cfg` in its own folder (`/cores/<name>/<name>.cfg`). A config is a named copy of the settings and of the media that were mounted. It lies in `configs/` of the folder of the core.

2.7.6 **Update of the cores.** Copy the `.cor` files of a new release over the old ones. The folders of the cores are not touched.

## 2.8 Check the result

2.8.1 The boot menu lists the backup entry (badge `backup`) and one entry for every core that the updater copied.

2.8.2 Continue with chapter 3.
