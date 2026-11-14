# 13. Full firmwares (.cfw)

## 13.1 General

13.1.1 A `.cfw` or `.ue2` is a complete firmware: bitstream and application. It is not a core. When the host starts it, the host is
replaced until the next power cycle.

13.1.2 The firmware files of the vendors are not part of this release. The host recognises the files on the list in 9.4.3.

## 13.2 Use a .cfw or .ue2

13.2.1 Copy the file to a medium. Any folder works.

13.2.2 Add it to the boot menu: **F3**, select the file, press **A** (5.4). The entry shows the badge `ready`. A file that the host refuses
shows `unusable`, and the File Browser says why (5.4.5). The entry can be the default (6.2).

13.2.3 To start it once without an entry: select the file in the File Browser and press **S** (5.5).

13.2.4 Select the entry in the boot menu. Press **RETURN**. The host asks `Start full firmware?` and says `<name> replaces the host. The way
back is to switch the power off and on.` **RETURN** starts. Any other key cancels (4.5.2).

13.2.5 A start from the countdown or from direct boot asks nothing (4.6.4, 4.7).

13.2.6 The host checks the file. It keeps the bitstream as one file, `/Flash/legacy/fullcore.bit`, with a stamp `fullcore.stm` that names the firmware it holds. An entry that was checked when it was added and has not changed is used as it is when the stamp names it.
Any other file is read, checked, copied into `fullcore.bit` and compared. Before the handover the host checks on every start that `fullcore.bit` is one complete run of flash clusters (4.5.3).

13.2.7 To return to the host, switch the power off and on.

> **WARNING** With `Protect Flash from updates` on (8.5), the host locks the first 4 MiB of the flash before the handover. The same lock is set before every start of a large core. A partial core does not get it. The updater of
> the firmware cannot then overwrite the host. With the setting off, the updater of the firmware can replace the host. If the lock cannot be set, nothing starts: `the flash could not be protected (...); nothing was started`. The way back is in
> chapter 14.

## 13.3 Refusals

13.3.1 The host refuses a firmware image without a bitstream for the XC7A50T, a damaged image, a full flash drive area or a fragmented flash file. The note line or the
start screen names the reason (4.5.5). Nothing is loaded.

## 13.4 The logo

13.4.1 A `.cfw` or `.ue2` gets a small logo in the menu. The host takes the built-in logo that fits the design name in the file. Optional:
a file `<name>.icon` beside the firmware file replaces it (`<name>` is the name of the firmware file without its extension; 32 x 32 pixels,
RGB565, exactly 2048 bytes). A file of any other size is ignored. The host reads it when it adds the entry or checks it: press **R** in the
boot menu to see a new one (4.4.2).

## 13.5 Package a .cfw or .ue2 as a .cor

13.5.1 **P** in the File Browser makes a `.cor` from the firmware file. The function, its checks and its refusals are in 9.5.

13.5.2 The `.cor` is written to `<medium>/cores/<name>.cor`. The badge in the boot menu is `foreign` (4.3). It starts like any large core,
without the question of 13.2.4.

13.5.3 A `.cor` made this way is for your own board. Do not hand it on.
