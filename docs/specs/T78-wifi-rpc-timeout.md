# T78: WiFi RPC replies with a timeout

Milestone M2, status 📅. Open (one line, as in the TODO table): Spec written, not built

Internal spec (not for upstream as written; a PR to Gideon is a separate decision). Origin: T77 (trxmon hang on RC1, 2026-10-10).
Alex 2026-10-10: "Mach ein todo und eine spec für uns intern. M2".

## 1. Problem

The Ultimate app talks to the ESP32 over the WiFi UART with a SLIP RPC. Every request that expects an answer goes through the
`TRANSMIT` macro (`software/io/wifi/wifi_cmd.h:79-84`, `TRANSMITDBG` 86-93): `esp32.uart->TransmitPacket(buf)`, then
`xTaskNotifyWait(0, 0, &buf, portMAX_DELAY)`. The reply is routed by `wifi_rx_isr` (`wifi_cmd.cc:17-31`): `hdr->thread` (the TX
buffer number) indexes `tasksWaitingForReply[]`; a waiting task gets the RX buffer by notification, any other packet goes to
`cmd_buffer_received_isr` (printed `~`).

When a reply never arrives, the caller blocks forever. A reply is lost when the UART's DMA has no free RX buffer: `dma_uart.cc:179`
prints `^` and disables the buffer request (`DMAUART_BufReq_DIS`). That happens when the tasks that free RX buffers do not run, e.g.
while a task above them keeps the processor (T77: trxmon's stop waits). On RC1 the console showed `^~^~^~` and then nothing; ping,
REST and every socket stayed dead until a power cycle.

Upstream: GideonZ/1541ultimate master still has `portMAX_DELAY` in both macros (checked 2026-10-10). Issue #774 (closed by PR #778,
WiFi reconnect) names the risk ("`TRANSMIT` blocks on `xTaskNotifyWait(..., portMAX_DELAY)` ... invites a stall"); no issue or PR
fixes it. Related open issue #862: syslog goes out on WiFi while Ethernet is connected.

## 2. Users of `TRANSMIT`

20 functions in `software/io/wifi/wifi_cmd.cc` and one in `wifi.cc`, among them `wifi_get_time`, `wifi_is_connected`,
`wifi_getmac`, `wifi_get_voltages`, `wifi_get_serial`, `wifi_enable`/`wifi_disable`, `wifi_wifi_autoconnect`/`_disconnect`,
`wifi_forget_aps`, `wifi_modem_enable`, `wifi_machine_off`/`_reboot`, `wifi_get/set_power_mode`, `wifi_get/set_wake_on_wifi`.
Before the change: list every caller of each (tasks: run-mode task in `wifi.cc`, menu/UI, REST, SNTP, the updaters through
`update_common.h`, the u64ii tester/programmer) and what each does with an error today. Which of them runs in lwIP's tcpip thread
(the case that kills Ethernet too) is not known yet: find it first (T77 left it open).

## 3. Change

1. `TRANSMIT` waits at most `WIFI_RPC_TIMEOUT_MS` (proposal 3000 ms; scans/connects that legitimately take longer get their own
   value, from the ESP32 side's worst case).
2. On timeout: clear `tasksWaitingForReply[bufnr]` (inside a critical section against `wifi_rx_isr`), return the caller's error
   value with `errno = ETIMEDOUT`, print one console line (`WiFi RPC <cmd> seq <n>: no reply in <ms> ms`).
3. A late reply must not reach the next task that waits on the same buffer number: `wifi_rx_isr` hands a reply over only when
   `hdr->sequence` matches the sequence the waiter sent (store it next to the task handle); otherwise it is treated as unsolicited
   and freed.
4. The RX path recovers: when a buffer is freed and the buffer request is disabled, enable it again (check `dma_uart.cc` does this
   already after a `^`; if not, that is part of the change).
5. Callers: each handles the error (no use of the reply buffer after a timeout).

Not in scope: the ESP32 firmware (T26), WiFi reconnect logic (#774/#778, already in our app), Tx/Rx buffer split (802d6143,
9fc82131, already in our app).

## 4. Open question for Alex

- WiFi off while the Ethernet link is up (Alex 2026-10-10: "wifi aus solange das LAN Kabel steckt?"): a separate switch, or part
  of this task? It removes the trigger for most testers but not the fault.

## 5. Tests

- Host or sim: a fake UART that drops the reply: `TRANSMIT` returns after the timeout, the slot is free, the next request gets
  its own reply; a late reply with the old sequence is dropped.
- Board: WiFi on, starve the RX path on purpose (a test app that busy-loops above the network tasks for 5 s while WiFi traffic
  runs), then REST, ping and FTP answer again without a power cycle; console shows the timeout line.

## Status and history

- **2026-10-10: spec written (Claude), not built.** From T77's analysis (docs/todo/T77.md).
