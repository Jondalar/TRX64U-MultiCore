# Provenance of host-app (T84)

Everything under `host-app/software/<dir>/` except `hostapp/` is a byte-for-byte copy of `vendor/gideon/software/<dir>/` (Ultimate app, GPL-3.0, Gideon Zweijtzer and contributors; licence headers untouched), vendor/gideon is the 1541ultimate fork at commit `32e5fd5f759e559c386ff711250e732964c6062d`, made by `tools/hostapp_vendor.py copy` from `vendor.list`. `target/` holds the Makefiles derived from `target/common`, `target/libs/riscv/{lwip,lvgl}` and `target/u64ii/riscv/ultimate` of the same commit (trimmed, the C64 side removed).

Not copied, linked to vendor/gideon's copy (third-party, own licences): `software/FreeRTOS`, `software/chan_fat`, `software/httpd`, `software/lvgl`, `software/lwip`.

Own code (this repo's licence): `host-app/software/hostapp/`, `coreapp/` (T93, built on this base), `tools/cor_pack.py`, `tools/build_hostapp.sh`, `tools/build_coreapp.sh`.

| File (under host-app/) | Origin (under vendor/gideon/) | sha256 (first 12) |
|---|---|---|
| `software/api/jsmn.h` | `software/api/jsmn.h` | 4b5d707eb800 |
| `software/api/json.cc` | `software/api/json.cc` | 11d57afce195 |
| `software/api/json.h` | `software/api/json.h` | 1665a061b372 |
| `software/app_loader/app_api.h` | `software/app_loader/app_api.h` | 1dd59cbb44ac |
| `software/app_loader/app_api_net.h` | `software/app_loader/app_api_net.h` | 352da69e2745 |
| `software/application/versions.h` | `software/application/versions.h` | 78e2bb0ab060 |
| `software/components/cbmname.h` | `software/components/cbmname.h` | 603b57f9b8ad |
| `software/components/config.cc` | `software/components/config.cc` | a27198d44d72 (MODIFIED: T163: ConfigBackend - with one set (host app) the stores live in the backend's file; the flash pages are only read, once, to take them over) |
| `software/components/config.h` | `software/components/config.h` | b2f66025d014 (MODIFIED: T163: declares ConfigBackend, config_set_backend()) |
| `software/components/dict.h` | `software/components/dict.h` | 1176429afc1c |
| `software/components/factory.h` | `software/components/factory.h` | f183d2b58c72 |
| `software/components/fifo.h` | `software/components/fifo.h` | 24893c5811c4 |
| `software/components/indexed_list.h` | `software/components/indexed_list.h` | 754da6a3a4eb |
| `software/components/init_function.cc` | `software/components/init_function.cc` | 999fba703fc9 |
| `software/components/init_function.h` | `software/components/init_function.h` | ec452bfae593 |
| `software/components/managed_array.h` | `software/components/managed_array.h` | a499b9a72998 |
| `software/components/mystring.cc` | `software/components/mystring.cc` | 01b15a2ab24c |
| `software/components/mystring.h` | `software/components/mystring.h` | 658736a54912 |
| `software/components/pattern.cc` | `software/components/pattern.cc` | db1b1f401611 |
| `software/components/pattern.h` | `software/components/pattern.h` | 234877e1ea1c |
| `software/components/size_str.cc` | `software/components/size_str.cc` | 6f39e4e307fc |
| `software/components/size_str.h` | `software/components/size_str.h` | b05d2397493b |
| `software/components/stack.h` | `software/components/stack.h` | f70a8fbf7a30 |
| `software/components/u64_str.h` | `software/components/u64_str.h` | db8ef528343c |
| `software/filemanager/cached_tree_node.h` | `software/filemanager/cached_tree_node.h` | be127a832180 |
| `software/filemanager/file_device.cc` | `software/filemanager/file_device.cc` | b13ebc1de9fe (MODIFIED: T111: a list of all FileDevices and find(name); release_volume(), the unmount the format needs (detach_disk is not changed)) |
| `software/filemanager/file_device.h` | `software/filemanager/file_device.h` | d245637349f7 (MODIFIED: T111: declares them and get_block_device()) |
| `software/filemanager/file_partition.cc` | `software/filemanager/file_partition.cc` | bb164bae3270 |
| `software/filemanager/file_partition.h` | `software/filemanager/file_partition.h` | d094c88267e3 |
| `software/filemanager/filemanager.cc` | `software/filemanager/filemanager.cc` | 5e14436b28fd |
| `software/filemanager/filemanager.h` | `software/filemanager/filemanager.h` | 1f543c0f8cb2 |
| `software/filemanager/path.cc` | `software/filemanager/path.cc` | a411ec1b48f6 |
| `software/filemanager/path.h` | `software/filemanager/path.h` | 2bd6de48dbf9 |
| `software/filemanager/ult_syscalls.cc` | `software/filemanager/ult_syscalls.cc` | 0f72df7a77f8 |
| `software/filesystem/blockdev.cc` | `software/filesystem/blockdev.cc` | 19bb0c8481c1 |
| `software/filesystem/blockdev.h` | `software/filesystem/blockdev.h` | a8505581473d |
| `software/filesystem/blockdev_flash.cc` | `software/filesystem/blockdev_flash.cc` | 0d2463a23d1d (MODIFIED: T139: write() erases every sector of the run, not only the first (FatFS writes runs of sectors; the rest was programmed over old data)) |
| `software/filesystem/blockdev_flash.h` | `software/filesystem/blockdev_flash.h` | 54a98e81a1ac |
| `software/filesystem/chanfat_manager.h` | `software/filesystem/chanfat_manager.h` | 668e42feb723 |
| `software/filesystem/directory.cc` | `software/filesystem/directory.cc` | 5067c1ef30f2 |
| `software/filesystem/directory.h` | `software/filesystem/directory.h` | a5aa7279d2e1 |
| `software/filesystem/disk.cc` | `software/filesystem/disk.cc` | 4b4e80d58760 |
| `software/filesystem/disk.h` | `software/filesystem/disk.h` | 45e5462c09ea |
| `software/filesystem/diskio.cc` | `software/filesystem/diskio.cc` | b47c149ca8d1 |
| `software/filesystem/diskio.h` | `software/filesystem/diskio.h` | d7c08ef6b372 |
| `software/filesystem/embedded_fs.h` | `software/filesystem/embedded_fs.h` | a7fc1ee007de |
| `software/filesystem/file.cc` | `software/filesystem/file.cc` | ec7ab667929a |
| `software/filesystem/file.h` | `software/filesystem/file.h` | 033dfa72c697 |
| `software/filesystem/file_info.h` | `software/filesystem/file_info.h` | 1f6b839a7c26 |
| `software/filesystem/file_system.cc` | `software/filesystem/file_system.cc` | c5bfb552ce1e |
| `software/filesystem/file_system.h` | `software/filesystem/file_system.h` | 80d59a78b88d |
| `software/filesystem/filesystem_fat.cc` | `software/filesystem/filesystem_fat.cc` | a284e2b7131f |
| `software/filesystem/filesystem_fat.h` | `software/filesystem/filesystem_fat.h` | de6afba0163b |
| `software/filesystem/filesystem_root.cc` | `software/filesystem/filesystem_root.cc` | 498f2fd1efa2 |
| `software/filesystem/filesystem_root.h` | `software/filesystem/filesystem_root.h` | 1f230d76fe8b |
| `software/filesystem/fs_errors_flags.h` | `software/filesystem/fs_errors_flags.h` | 9a6fc114b166 |
| `software/filesystem/partition.cc` | `software/filesystem/partition.cc` | 70bebda38337 |
| `software/filesystem/partition.h` | `software/filesystem/partition.h` | 0fa7a15ad7a2 |
| `software/infra/action.h` | `software/infra/action.h` | e447958b1a59 |
| `software/infra/host.h` | `software/infra/host.h` | f353187c9b12 |
| `software/infra/observer.h` | `software/infra/observer.h` | 95ffc900f9e1 |
| `software/infra/return_codes.h` | `software/infra/return_codes.h` | 39c377cf3b76 |
| `software/infra/subsys.cc` | `software/infra/subsys.cc` | e5dbb198fd2f |
| `software/infra/subsys.h` | `software/infra/subsys.h` | 39da944d5e49 |
| `software/io/audio/nau8822.cc` | `software/io/audio/nau8822.cc` | b62f55684d72 |
| `software/io/audio/nau8822.h` | `software/io/audio/nau8822.h` | d27c18e33e54 |
| `software/io/c64/c64.h` | `software/io/c64/c64.h` | f2d8f78e0410 |
| `software/io/c64/joystick_output.cc` | `software/io/c64/joystick_output.cc` | 5e2ec0de2d3f (MODIFIED: T228 R10: setPadPort(), the core application's pad service as a source of its own per port, ANDed into both ports' lines) |
| `software/io/c64/joystick_output.h` | `software/io/c64/joystick_output.h` | 97ff9b716e38 (MODIFIED: T228 R10: declares setPadPort() and the two sources) |
| `software/io/c64/keyboard.h` | `software/io/c64/keyboard.h` | a354a0373984 |
| `software/io/c64/keyboard_c64.cc` | `software/io/c64/keyboard_c64.cc` | 55238b068d1b |
| `software/io/c64/keyboard_c64.h` | `software/io/c64/keyboard_c64.h` | aa58421a1b78 (MODIFIED: KEYBOARD_C64_TIMER_SCAN also for -DTRX_HOSTAPP (taps shorter than one UI loop were lost); getch_nowait()) |
| `software/io/c64/mouse_pot_pacer.h` | `software/io/c64/mouse_pot_pacer.h` | 8beb6e4d4cf9 |
| `software/io/c64/screen.h` | `software/io/c64/screen.h` | 9466480d30ba |
| `software/io/c64/trx_cart.h` | `software/io/c64/trx_cart.h` | 9cd27b8b9796 |
| `software/io/c64/trx_io.cc` | `software/io/c64/trx_io.cc` | 9571a9217a87 (MODIFIED: T93: trx_io_alt_id, a core application's own ID besides "TRX2"; the host app never sets it) |
| `software/io/c64/trx_io.h` | `software/io/c64/trx_io.h` | 1b9aa4fc768d (MODIFIED: T93: declares trx_io_alt_id) |
| `software/io/flash/at45_flash.h` | `software/io/flash/at45_flash.h` | 974d7210414b |
| `software/io/flash/at49_flash.h` | `software/io/flash/at49_flash.h` | 7c1239df1c33 |
| `software/io/flash/flash.cc` | `software/io/flash/flash.cc` | 1697aa86fecd |
| `software/io/flash/flash.h` | `software/io/flash/flash.h` | d76d72e34ebd |
| `software/io/flash/s25fl_flash.cc` | `software/io/flash/s25fl_flash.cc` | a909a6a1eaa8 |
| `software/io/flash/s25fl_flash.h` | `software/io/flash/s25fl_flash.h` | 5df1fa201d8f |
| `software/io/flash/s25fl_l_flash.cc` | `software/io/flash/s25fl_l_flash.cc` | ed44354ea565 |
| `software/io/flash/s25fl_l_flash.h` | `software/io/flash/s25fl_l_flash.h` | cb5c3b2082c1 |
| `software/io/flash/w25q_flash.cc` | `software/io/flash/w25q_flash.cc` | 40d6643b1a0e |
| `software/io/flash/w25q_flash.h` | `software/io/flash/w25q_flash.h` | 5464ed6d0124 |
| `software/io/i2c/hw_i2c.h` | `software/io/i2c/hw_i2c.h` | b0d9b9d92763 |
| `software/io/i2c/hw_i2c_drv.cc` | `software/io/i2c/hw_i2c_drv.cc` | 59fb757afdaf |
| `software/io/i2c/hw_i2c_drv.h` | `software/io/i2c/hw_i2c_drv.h` | be5e767ebbbe |
| `software/io/i2c/i2c_drv.cc` | `software/io/i2c/i2c_drv.cc` | c90a2732f1c3 |
| `software/io/i2c/i2c_drv.h` | `software/io/i2c/i2c_drv.h` | 564696abd6dd |
| `software/io/icap/icap.h` | `software/io/icap/icap.h` | bd53218a1ab6 |
| `software/io/mdio/mdio.c` | `software/io/mdio/mdio.c` | 8f31954da3ad |
| `software/io/mdio/mdio.h` | `software/io/mdio/mdio.h` | d73e54b4bd8f |
| `software/io/network/network_esp32.cc` | `software/io/network/network_esp32.cc` | 99c9ff3394e6 (MODIFIED: T184: -DTRX_HOSTAPP leaves out the tree browser, popups and string boxes of the Ultimate menu (getDisplayString, context actions, AP list/enter); the settings store, effectuate_settings and on_edit are unchanged) |
| `software/io/network/network_esp32.h` | `software/io/network/network_esp32.h` | d9dde2b81cf0 (MODIFIED: T184: declares only what network_esp32.cc keeps with -DTRX_HOSTAPP) |
| `software/io/network/network_interface.cc` | `software/io/network/network_interface.cc` | 59994df6000a |
| `software/io/network/network_interface.h` | `software/io/network/network_interface.h` | d0eb14d37256 |
| `software/io/network/pcap.h` | `software/io/network/pcap.h` | 70b63a86d191 |
| `software/io/network/rmii_interface.cc` | `software/io/network/rmii_interface.cc` | e56ae033cb7e |
| `software/io/network/rmii_interface.h` | `software/io/network/rmii_interface.h` | 946ffc9bc6a0 |
| `software/io/rtc/rtc.h` | `software/io/rtc/rtc.h` | b40bdd65df53 |
| `software/io/rtc/rtc_dummy.cc` | `software/io/rtc/rtc_dummy.cc` | 9cedd4ca9142 (MODIFIED: T164 D1: get_fat_time() of a clock that is not set (1970, negative year) reads 1980-01-01 00:00:00, not a wrapped 2097) |
| `software/io/rtc/rtc_dummy.h` | `software/io/rtc/rtc_dummy.h` | 673cd0a337a5 |
| `software/io/rtc/rtc_epoch.h` | `software/io/rtc/rtc_epoch.h` | 22aa001153ce |
| `software/io/rtc/timezones.cc` | `software/io/rtc/timezones.cc` | 3cdf705dcd5c (MODIFIED: T164 D1: every zone carries its own DST rule (IANA tzdata, POSIX form); Gideon's table gave all zones the US rule) |
| `software/io/rtc/trx_time_handover.h` | `software/io/rtc/trx_time_handover.h` | 36e7e9960871 |
| `software/io/sd_card/sd_card.cc` | `software/io/sd_card/sd_card.cc` | 552078ed90d4 (MODIFIED: T139: the data CRC16 of every block / CID / CSD is checked and a damaged read is repeated (SD_READ_RETRIES); T149 D5: RW_SPEED 3 (12.5 MHz), RW_SPEED_IOB 1 (25 MHz) only where sdio_set_rw_speed sees SDIO_CAP_IOB) |
| `software/io/sd_card/sd_card.h` | `software/io/sd_card/sd_card.h` | 9db5363fb4f1 (MODIFIED: T139: declares read_once(), read_register(), SD_READ_RETRIES) |
| `software/io/sd_card/sdcard_manager.cc` | `software/io/sd_card/sdcard_manager.cc` | 6ff6873ce21e |
| `software/io/sd_card/sdcard_manager.h` | `software/io/sd_card/sdcard_manager.h` | 94b2f3ee9825 |
| `software/io/sd_card/sdio.cc` | `software/io/sd_card/sdio.cc` | 6bc1bf45f095 (MODIFIED: T139: sdio_read_block checks the CRC16 (sdio_crc16, SDIO_ERR_CRC), counters; uintptr_t for the alignment test; T149 D5: sdio_set_rw_speed (25 MHz only if the control register's bit 7 reads 1)) |
| `software/io/sd_card/sdio.h` | `software/io/sd_card/sdio.h` | d9ec39b943b4 (MODIFIED: T139: SDIO_ERR_CRC, sdio_crc16(), counters; SDIO_EXTERNAL_REGS lets a native test supply the registers; T149 D5: SDIO_CAP_IOB, sdio_set_rw_speed()) |
| `software/io/stream/stream.cc` | `software/io/stream/stream.cc` | c7299e694542 |
| `software/io/stream/stream.h` | `software/io/stream/stream.h` | de7184fe8e43 |
| `software/io/stream/stream_ramfile.h` | `software/io/stream/stream_ramfile.h` | 89b2a720589c |
| `software/io/stream/stream_uart.cc` | `software/io/stream/stream_uart.cc` | 65fa206645b5 |
| `software/io/stream/stream_uart.h` | `software/io/stream/stream_uart.h` | 4a861fa15b58 |
| `software/io/uart/cmd_buffer.c` | `software/io/uart/cmd_buffer.c` | f7b96b9b12c5 |
| `software/io/uart/cmd_buffer.h` | `software/io/uart/cmd_buffer.h` | 5bb28c6c071b |
| `software/io/uart/dma_uart.cc` | `software/io/uart/dma_uart.cc` | 7dd2029391ea |
| `software/io/uart/dma_uart.h` | `software/io/uart/dma_uart.h` | a7c287f12223 |
| `software/io/usb/hid_decoder.h` | `software/io/usb/hid_decoder.h` | 6f172ab5acef |
| `software/io/usb/keyboard_usb.cc` | `software/io/usb/keyboard_usb.cc` | ca7c4a3f7f0e |
| `software/io/usb/keyboard_usb.h` | `software/io/usb/keyboard_usb.h` | a8adcc3bff3e |
| `software/io/usb/nano_minimal.nan` | `software/io/usb/nano_minimal.nan` | 9f65c1377422 |
| `software/io/usb/usb_ax88772.cc` | `software/io/usb/usb_ax88772.cc` | e3de7fc166a5 |
| `software/io/usb/usb_ax88772.h` | `software/io/usb/usb_ax88772.h` | 7030abdd262a |
| `software/io/usb/usb_base.cc` | `software/io/usb/usb_base.cc` | 7d86b33a9bbb |
| `software/io/usb/usb_base.h` | `software/io/usb/usb_base.h` | 20e4b1d1e51c |
| `software/io/usb/usb_device.cc` | `software/io/usb/usb_device.cc` | bc08b0423aaf |
| `software/io/usb/usb_device.h` | `software/io/usb/usb_device.h` | 8cb2e740f929 |
| `software/io/usb/usb_hid.cc` | `software/io/usb/usb_hid.cc` | 09a187ae8f62 (MODIFIED: T104.1 5.6: usb_hid_set_mouse_sink(), one hook per mouse report for the mouse library; T141: usb_hid_set_keyboard_sink(), the combined boot report per keyboard report; T156: usb_hid_set_pad_sink(), an interface that is neither keyboard nor mouse is offered to the gamepad library; no sink (host app): unchanged) |
| `software/io/usb/usb_hid.h` | `software/io/usb/usb_hid.h` | 31e350cf617b (MODIFIED: T104.1 5.6: declares the hook, usb_hid_set_mouse1_position(), usb_hid_set_joy1_output(); T141: the keyboard hook; T156: UsbPadSink, usb_hid_set_pad_sink()) |
| `software/io/usb/usb_hid_config.h` | `software/io/usb/usb_hid_config.h` | 44264e9edfc9 |
| `software/io/usb/usb_hid_selection.h` | `software/io/usb/usb_hid_selection.h` | 105bcbc9f5fc |
| `software/io/usb/usb_hub.cc` | `software/io/usb/usb_hub.cc` | 88264ac1ae27 |
| `software/io/usb/usb_hub.h` | `software/io/usb/usb_hub.h` | f9e2de06096e |
| `software/io/usb/usb_hwinit.cc` | `software/io/usb/usb_hwinit.cc` | 04d17be6e902 |
| `software/io/usb/usb_ms_cbi.cc` | `software/io/usb/usb_ms_cbi.cc` | 4fce98d02649 |
| `software/io/usb/usb_ms_cbi.h` | `software/io/usb/usb_ms_cbi.h` | 3e69c93ff8d5 |
| `software/io/usb/usb_nano.h` | `software/io/usb/usb_nano.h` | f50c8af693aa |
| `software/io/usb/usb_scsi.cc` | `software/io/usb/usb_scsi.cc` | 64d895fc484e |
| `software/io/usb/usb_scsi.h` | `software/io/usb/usb_scsi.h` | 87e35fb26889 |
| `software/io/wifi/esp32.cc` | `software/io/wifi/esp32.cc` | c0e33bcc7ca9 |
| `software/io/wifi/esp32.h` | `software/io/wifi/esp32.h` | de7e22bff53f (MODIFIED: T184: no menu.h (the Ultimate menu classes are not in the host or core app; nothing in esp32.h or esp32.cc uses them)) |
| `software/io/wifi/wifi.cc` | `software/io/wifi/wifi.cc` | 54a3c111d71a (MODIFIED: T184: -DTRX_HOSTAPP leaves out the U64Config power-mode store and low-voltage message and the Browsable access-point list, connectWithFeedback (the host page reads wifi_aps and getConnectionFeedback() itself)) |
| `software/io/wifi/wifi.h` | `software/io/wifi/wifi.h` | 4782a6133cd4 (MODIFIED: T184 D4: connectWithFeedback (popup + progress box of the Ultimate menu) is left out with -DTRX_HOSTAPP) |
| `software/io/wifi/wifi_cmd.cc` | `software/io/wifi/wifi_cmd.cc` | e9afbd2d3dc2 |
| `software/io/wifi/wifi_cmd.h` | `software/io/wifi/wifi_cmd.h` | 066b3cd9f563 |
| `software/io/wifi/wifi_connection_feedback.h` | `software/io/wifi/wifi_connection_feedback.h` | 6b8c860e9020 |
| `software/io/wifi/wifi_reply.h` | `software/io/wifi/wifi_reply.h` | 4e9504a4d0cb |
| `software/io/wifi/wifi_scan_result.h` | `software/io/wifi/wifi_scan_result.h` | c19591a0d973 |
| `software/lvgl_port/trx_lv_port.cc` | `software/lvgl_port/trx_lv_port.cc` | bd3e2facfdff (MODIFIED: T101: UI_RES (scale and PX_POS from the build); T93 D9: trx_lv_suspend() / trx_lv_resume(), the layer handed to a core's picture and back; 5.14: trx_lv_refresh_now()) |
| `software/lvgl_port/trx_lv_port.h` | `software/lvgl_port/trx_lv_port.h` | 17ef6d2b300f (MODIFIED: T93 D9 and 5.14: declares them; T101: UI_RES, TRX_LV_SCALE/X0/Y0) |
| `software/network/config/arch/bpstruct.h` | `software/network/config/arch/bpstruct.h` | 8d2741e38afa |
| `software/network/config/arch/cc.h` | `software/network/config/arch/cc.h` | d8d5b4ab9436 |
| `software/network/config/arch/epstruct.h` | `software/network/config/arch/epstruct.h` | 29d9172f9792 |
| `software/network/config/arch/sys_arch.h` | `software/network/config/arch/sys_arch.h` | 327f6b96b573 |
| `software/network/config/lwipopts.h` | `software/network/config/lwipopts.h` | ecab05ea065a |
| `software/network/ftp_format.h` | `software/network/ftp_format.h` | c69933484e35 |
| `software/network/ftpd.cc` | `software/network/ftpd.cc` | e541dc8b3603 |
| `software/network/ftpd.h` | `software/network/ftpd.h` | 5ed3de96dc61 |
| `software/network/network_config.cc` | `software/network/network_config.cc` | e1ac25766119 |
| `software/network/network_config.h` | `software/network/network_config.h` | 8c2d99f46d15 |
| `software/network/sntp_time.cc` | `software/network/sntp_time.cc` | bb15d6d597f4 |
| `software/network/sntp_time.h` | `software/network/sntp_time.h` | 164c26ac41f0 |
| `software/network/socket_keepalive.h` | `software/network/socket_keepalive.h` | 6a1351c65063 |
| `software/network/sys_arch.c` | `software/network/sys_arch.c` | 76ca23d6cc0d |
| `software/network/time.c` | `software/network/time.c` | 00e776023a27 |
| `software/network/vfs.cc` | `software/network/vfs.cc` | 78710bc4eff6 |
| `software/network/vfs.h` | `software/network/vfs.h` | 924be1911af9 |
| `software/portable/riscv/crt0.S` | `software/portable/riscv/crt0.S` | 8d3d6b4d3c81 |
| `software/portable/riscv/do_ctors.c` | `software/portable/riscv/do_ctors.c` | f65a08860875 |
| `software/portable/riscv/endianness.h` | `software/portable/riscv/endianness.h` | 98a029effd53 |
| `software/portable/riscv/malloc_lock.c` | `software/portable/riscv/malloc_lock.c` | 6c4883c7c153 |
| `software/portable/riscv/riscv_main.c` | `software/portable/riscv/riscv_main.c` | fc668cb9cdb7 |
| `software/system/assert.c` | `software/system/assert.c` | e416900a54f3 |
| `software/system/dump_hex.c` | `software/system/dump_hex.c` | 2c12a27625ca |
| `software/system/dump_hex.h` | `software/system/dump_hex.h` | acad37bb3ca9 |
| `software/system/integer.h` | `software/system/integer.h` | a19dbe545c8a |
| `software/system/iomap.h` | `software/system/iomap.h` | b52f56985242 |
| `software/system/itu.c` | `software/system/itu.c` | 6f5d38cd578a |
| `software/system/itu.h` | `software/system/itu.h` | 89b4ff0bef27 |
| `software/system/keys.h` | `software/system/keys.h` | d55cb74558a9 |
| `software/system/memory_wrap.cc` | `software/system/memory_wrap.cc` | 6e27e0732628 |
| `software/system/product.cc` | `software/system/product.cc` | 21774142b570 |
| `software/system/product.h` | `software/system/product.h` | 431f0e312f55 |
| `software/system/profiler.h` | `software/system/profiler.h` | 474f9d33ea04 |
| `software/system/small_printf.cc` | `software/system/small_printf.cc` | 95eedfc65898 |
| `software/system/small_printf.h` | `software/system/small_printf.h` | 6ae25079ef00 |
| `software/system/u2p.h` | `software/system/u2p.h` | 66a8cef874ff |
| `software/system/u64.h` | `software/system/u64.h` | d0624d6ad1e6 |
| `software/system/u64ii_init.cc` | `software/system/u64ii_init.cc` | dc6ced689013 |
| `tools/parse_nano.py` | `tools/parse_nano.py` | 595c0ce0152b |
| `software/u64/color_timings.cc` | `software/u64/color_timings.cc` | d44c925624ff |
| `software/u64/color_timings.h` | `software/u64/color_timings.h` | f400defcf7d6 |
| `software/u64/hdmi_scan.cc` | `software/u64/hdmi_scan.cc` | 71a4c9f5a2a6 |
| `software/u64/hdmi_scan.h` | `software/u64/hdmi_scan.h` | 7ed69ab6544a |
| `software/u64ctrl/main/rpc_calls.h` | `software/u64ctrl/main/rpc_calls.h` | 78a3cc301727 |
| `software/userinterface/browsable.h` | `software/userinterface/browsable.h` | 4f9a1ab003de |
| `software/userinterface/config_collection.h` | `software/userinterface/config_collection.h` | 35277403e6ca |
| `software/userinterface/editor.h` | `software/userinterface/editor.h` | a6eac9975bce |
| `software/userinterface/ui_elements.h` | `software/userinterface/ui_elements.h` | 9fdc1914bf02 |
| `software/userinterface/userinterface.h` | `software/userinterface/userinterface.h` | fc3f62b7baae |
