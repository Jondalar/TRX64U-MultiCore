# 15. Troubleshooting

## 15.1 How to use this chapter

15.1.1 Find the symptom. Do the action. If the action does not help, report the problem (15.4).

## 15.2 Symptoms

| Symptom | Cause | Action |
|---|---|---|
| After the install the old firmware starts, not the host menu | Direct boot is on and the auto entry is the backup of the old firmware (2.6.2) | Hold **C=** from the power-on (4.7.3) |
| No picture at all | The screen cannot take the mode | Press **V** (the Picture page opens), **DOWN**, **RETURN**. Press a key within 8 s when a picture shows. The host saves the mode that you keep (3.6) |
| No picture on composite or S-video | Only the C128 core has an analogue output, and its board check is open. The menu of `trx64u` has the row `Analog video mode` (3.6.5) | Use HDMI |
| Updater: `No bitstream for this board. Nothing written.` | The FPGA is not an XC7A50T | This firmware does not fit the board |
| Updater: `Not the expected flash (W25Q128). Nothing written.` | Another flash chip | Do not install |
| Updater: `Package damaged. Nothing written.` | The file is damaged | Copy the file again. Compare it with `SHA256SUMS` |
| Updater: `No SD/USB with x.y MB free for the cores. Make room. Retry?` | No medium has room for the backup, the old firmware and the cores | Free space or insert a medium. Pick **Yes** |
| Updater: `The backup could not be written. Nothing is written to the flash.` | The medium is not writable or is full | Use a medium that is writable and has room |
| Updater: `Update FAILED:` and `Do NOT switch off.` | A part did not verify after three tries | Do not switch off. Report the screen (15.4) |
| Updater: `Cancelled. Nothing written.` or `Cancelled. Nothing written to the flash.` | **No** to one of the two warranty questions, or **No** to a question of the setup (2.4.3) | Nothing changed. Run the update again when you want it (2.4) |
| Updater: `No backup, no update. Nothing written.` | No medium had room for the backup and **No** was the answer to the retry question | Insert a medium with room. Run the update again (2.4.8) |
| Menu: `No auto core is set`, `Auto core: file missing`, `Auto core: file changed (press R to check it)`, `Auto core: refused (see its badge)` | The rules 6 to 9 of the direct boot (4.7.2) | Set the auto entry (6.2). Plug in the medium. Press **R** (4.4.2) |
| Boot menu lists no core | The cores are not in `boot.cfg` | Add them with **A** in the File Browser (3.3) |
| Badge `missing` | The medium or file is gone | Plug the medium in. Press **R** |
| Badge `changed` | The file is not the one that was added | Press **R** to check it. Or remove the entry (6.4) and add it again |
| Note: `refused: built for another host contract` | The `.cor` is for another host | Use the `.cor` files of the release that you installed |
| Note: `refused: routed against host <key>, this host is <key>` | The `.cor` is built against another host image | Use the `.cor` files of the release that you installed |
| Core stays held, the screen names a missing file | A required ROM is not there | Put the file in `roms/` in the folder of the core (12.2). The core starts by itself |
| F3 **Z**: `Unzip is not installed.` | `unzip.u2a` is not in `cores/apps` on the SD card | Copy the file from the release into `cores/apps` on the SD card |
| Core says a file has the wrong size | Wrong ROM | Use the file with the size that the screen shows |
| Amiga: `Kickstart not found` | No `.rom` in the `roms/` folder of the core | Put a 256 KB or 512 KB `.rom` in `cores/amiga/roms/` |
| Apple IIe: a disk image does not mount | `diskii.rom` is missing | Put `diskii.rom` in `cores/apple2/roms/` and reload the ROMs |
| ZX Spectrum Next: `Error initializing SD card!` | No `next.img` yet | Put the image in `cores/zxnext/roms/` or mount one (11.12). The core looks again every second and starts over when the image is there |
| Header: `no network` | No link or no address | Check the cable. The F7 page shows the link state (7.1) |
| After a direct start the core is not the one you want | The auto entry | Change it on **F5** with **D** (6.2) |
| Cannot get back to the host: F8 System has no `Return to host menu` | The core cannot give the FPGA back to the host (the TRX64 core) | Switch the power off and on. Hold **C=** (3.7.2) |
| The menu of a core is cut at the right | The screen mode is narrower than 960 px and the core is an older build | Use the `.cor` of the current release: it has the compact layout (4.10) |
| After a change of the picture the screen goes back by itself | The host and the cores ask 8 s: any key keeps the mode. **LEFT** and **ESC** go back at once | Press a key (not **LEFT** or **ESC**) within 8 s (8.4.8) |
| ZX Spectrum Next: `wrong board revision` | The `Board issue` is higher than the `TBBLUE.FW` on the image takes | Set `Board issue` to `Auto` (the default: the core reads `TBBLUE.FW`) or to a lower issue (11.12.3a) |
| The case LED does not flicker with the floppy | The row `LED Load Indicator` is `Off`, or the core has no such row (it has one in the Amiga, Apple IIe, Atari 800 XL, CPC and the cores with a 1541) | Set the row to `On` (the default). See 11.1.9 |
| Start refused: `the flash could not be protected (...); nothing was started` | The write protection of the flash did not take | Read 8.5.5 |
| Board does not start after a power cycle | The flash is damaged | Use the cable (14.5) |

## 15.3 The network

15.3.1 The web page is at the address on the F7 page, port 80. The JSON is at `/v1/status` (7.4), the text of the bug report at `/v1/report`. FTP is on port 21. None of these asks for a
password (7.2, 7.3). The remote-control routes `/v1/ui/...` need a token (1.4).

## 15.4 Report a problem

15.4.1 Send:

- a **photo of the screen**,
- the **core and its version** (the boot menu shows them; the Home page of the common menu shows core, version and
  application build) and the id of the release,
- the bug report of the host, if the host starts (chapter 10). For a start that went wrong: the file `startlog.txt` in the folder `report` on the SD card,
- what you did, step by step, and what you expected,
- for a ROM problem: the message on the screen, and the file name and size on your stick.
