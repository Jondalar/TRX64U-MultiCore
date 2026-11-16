# The layer-2 contract

What the host (layer 1) offers a core (layer 2) is fixed: the ports of the `l2` cell in
`host/top.v`. This directory is where that fact is written down, so a core does not have to be
read out of another core.

| File | What it is |
|---|---|
| `layer2_ports.txt` | the 247 ports of the partition, generated from the reference core's `cores/c64ref/layer2_c64ref.v` by `tools/contract_ports.py` |
| `VERSION` | the contract version, an integer. It rises when `layer2_ports.txt` changes (still what a `.cor` carries and the boot ROM compares; T221 D12 folds it into the host gateware MAJOR, see `version.toml`, open) |
| `version.toml` | the SemVer of the host gateware (T221 D5/D7), kept here and not under `host/` so that a bump does not move the host key |

Rules:

- Every `cores/<name>/layer2_<name>.v` has exactly these ports. `tools/contract_ports.py --check`
  compares them in the lint stage of the pipeline, so a core cannot drift from the host.
- The contract version is the number a `.cor` carries: the boot ROM refuses a core built for another contract (T84 D3).
- Changing the port list is a host change: it raises `VERSION`, invalidates every published core
  and rebuilds the whole matrix (T94 D5, Q6).

History:

- **1** - the 228 ports of the first host (T94).
- **2** (T103, 2026-10-17) - 247 ports. Out: the ten `uci_*` ports (the UCI is the core's, behind `cw_*`). In: the cassette pins
  (`c2n_*`, MOTOR too: direction unknown, the core decides), the POT pins (`paddle_*`), the SID socket selects (`sid_sock*`), the codec ADC (`adc_*`, for the sockets' audio), the packet
  port (`pk_*`, 13 signals on `cpu_clk`; the host keeps the MAC and the crossing FIFO, `host/pktport_host.v`), and `host_caps`, the host's
  capability bits as a constant (flops, so the partition input has a real driver). The host's ITU word is unchanged. A core that does not
  use a port leaves it unused and ties its outputs off; the reference core ties them through DONT_TOUCH flops (T87 boundary rule).

Conventions on existing ports (no port change, the version stays 2):

- **`drive_leds_n`** (T159 D5, T164 WP12), active low, on `cpu_clk`: bit 0 = **floppy** activity, bit 1 = **hard disk** activity, bit 2 unused
  (`1`). `host/top.v` feeds bit 0 to case LED 1 (source 4) and to `bling_io`'s `drive_act`, bit 1 to `bling_io`'s `hd_act`. With the app's
  "LED Load Indicator" On (case LED selector source 4) the power lamp (LED 71 of the keyboard chain) is red while the core runs, the red
  flickers off on bit 0, flickers blue on bit 1 (wins over bit 0), and is yellow while SHIFT LOCK is locked. A core without floppy or hard
  disk ties both to `1`; a short access is held for one flicker period by the host. Bit 1 must be a hard disk and nothing else: the 1541
  (`cores/common/drive_l2.vhd`) puts its motor LED there, so a C64-family core must not pass the drive's `leds_n` through unchanged.

Not here yet: the **null core** (a layer-2 module that drives every output to its idle value and
answers every register window as absent) is the gateware of T81 and is explicitly out of T84's
scope; when it is built it belongs beside these files, because it is the contract's own
implementation. The host's power-up image in flash is host + null core (T84 D2).
