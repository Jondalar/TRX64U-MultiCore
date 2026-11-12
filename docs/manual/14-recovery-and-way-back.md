# 14. Recovery and the way back

## 14.1 The ways back

| You want | Use | Needs |
|---|---|---|
| The vendor firmware back in the flash, as before the install | R in the File Browser (14.2) | The host starts. A backup `.cor` of the install, or an official `.cfw` or `.ue2` |
| The previous flash content with the settings | The restore bundle `TRXBK_<date>_nnn.CFW` (14.3) | A firmware whose file browser offers **Run Update** |
| The multi-core host again, after a vendor updater replaced it | The installer of the release (14.4) | A firmware whose file browser offers **Run Update** |
| Back from a full firmware that you started from the host | Power off and on (13.2.7) | Nothing |
| A board that does not start | The cable (14.5) | A USB-Blaster |

## 14.2 Restore the vendor firmware from the host (R)

14.2.1 The installer (2.4.4) writes the old firmware as a large `.cor` to `<medium>/cores/`. The boot menu shows it with the badge `backup`.

14.2.2 To restore it: open the File Browser, select the backup `.cor`, press **R**. The pages, the questions and the refusals are in 9.4.

14.2.3 An official `.cfw` or `.ue2` on the list of 9.4.3 is restored the same way.

> **WARNING** The restore removes the host from the flash (9.4.1). The way back to the host is the installer of the release (14.4).

## 14.3 The restore bundle

14.3.1 The installer writes two more files to `<medium>/cores/` (2.4.4):

| File | What it is |
|---|---|
| `TRXBK_<date>_nnn.BIN` | The raw copy of the flash: bitstream, application and the configuration pages |
| `TRXBK_<date>_nnn.CFW` | An installable package of the same state: bitstream, application and the configuration pages |

14.3.2 The updater accepts the `.CFW` as a restore. With **Run Update** it shows `Restore backup <id>:` and the bitstream and the application
that the bundle holds, `settings included`, and it checks the package (`Package OK.`). Then it asks `About to restore. Continue?`. Pick **Yes**.

14.3.3 The updater asks `Restore your settings from the backup?` with the buttons `No` and `Yes`. The default is `No`.

- `No`: the settings of the board stay as they are now. The bitstream and the application are restored.
- `Yes`: the saved configuration pages are written back too, after the bitstream and the application.

14.3.4 Before it writes, the updater takes a **fresh backup** of the present state to the first medium that has room. It shows the size
that it needs (`Backing up the flash (x.y MB) to USB/SD..`). If no medium has room, it asks `No USB/SD with x.y MB free. Insert one. Retry?`. With **No** it
stops and writes nothing.

14.3.5 The updater writes the application, then the bitstream, then (with `Yes`) the settings. It verifies each part and repeats a failed
write up to three times. It shows `Restore OK, installed <id>` (with `Yes`: `Restore OK, installed <id> with settings`) and switches the board off.

14.3.6 Switch the power off and on. The restored firmware starts.

> **CAUTION** A power loss while the settings are written leaves default settings. The board still starts.

## 14.4 After a vendor updater

14.4.1 The updater of a vendor firmware replaces the host, if the flash is not protected (8.5, 13.2.7).

14.4.2 To get the host back: run the installer of the release (`trx64host_update_<id>.ue2` on the Commodore firmware 1.1.0) with **Run Update**
as in chapter 2.

## 14.5 The cable

14.5.1 You need a USB-Blaster. The cable is the way back when the board does not start.

14.5.2 The flash holds the bitstream at `0x000000` and the host application at `0x220000`. Write them with `openFPGALoader`:

```
openFPGALoader -c usb-blaster --fpga-part xc7a50tfgg484 -f <bitstream>
openFPGALoader -c usb-blaster --fpga-part xc7a50tfgg484 -f --offset 0x220000 --file-type bin <application>
```

14.5.3 `--fpga-part` loads a bridge into the FPGA first. After the write the FPGA runs this bridge until the next power cycle. Switch the
power off and on after the write.
