#!/usr/bin/env python3
"""T92 D3: cores/cpc/gen/u765.sv = cores/cpc/rtl/u765/u765.sv (MiSTer-devel/Amstrad_MiSTer, GPL v2 or later, Gyorgy Szombathelyi) plus three changes, counted.
(1) Mechanical, Verilator needs it: the function SECTOR_SIZE declares `reg [15:0] logical_size = <expression of its arguments>;`.
In a static function the initialiser runs once at time 0, which Verilator rejects ("static variable initializer dependent on function I/O")
and Vivado may take either way; the intent is one evaluation per call, so the declaration is split from the assignment. The benches and the
synthesis read gen/, never rtl/ (one artifact for both).
(2) T92 #17: a new image event (mount/eject) of a drive wins over everything the ce-gated blocks do for that drive in the same clock, and releases the
global i_scan_lock when that drive's scan held it and tinfo_lock when that drive's trackinfo load held it (OLD2/NEW3 below).
(3) T92 #17 H2: a READ DATA / READ ID whose drive loses its image mid-command ends with ST0 0x40 / ST1 0x05 (the answer of the SETUP state for a missing image) instead of waiting for it (OLD6..OLD9), in the data phase too (OLD10..OLD13); H3 (OLD14) and the reset (OLD15) clear a stuck sector search; H1/H4: the trackinfo lock waits for a block in flight and for the scan lock (OLD16, OLD17), a scan starts only without a trackinfo lock or a block in flight (OLD18), and two lock takers never take the trackinfo lock in the same clock (tinfo_take, OLD4/OLD4B/OLD16/OLD17).
  tools/cpc_gen.py          (re)make cores/cpc/gen/u765.sv
  tools/cpc_gen.py --check  gen/u765.sv is what rtl/u765/u765.sv gives (the lint stage runs this)
"""
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "cores/cpc/rtl/u765/u765.sv")
DST = os.path.join(ROOT, "cores/cpc/gen/u765.sv")
OLD = "\t\treg [15:0] logical_size = (16'h80 << (n[3] ? 4'h8 : n[2:0]));\n"
NEW = "\t\treg [15:0] logical_size;\n\t\tlogical_size = (16'h80 << (n[3] ? 4'h8 : n[2:0]));\n"
# T92 #17: the image scan owns the global i_scan_lock from state 1 -> 2 until its end (state 2/3 -> 0). A mount or eject of the drive that owns it
# restarts or cancels that scan without releasing the lock, and every later scan then waits in state 1 for ever (RQM low). The new image event of a
# drive in scan state 2 or 3 releases the lock it holds (state 1 does not hold it). Benches: cores/cpc/sim tb_fdcscan.v (fdcscan2).
OLD2 = "\t\t\timage_scan_state[i] <= {1'b0, |img_size};\n"
NEW2 = OLD2 + "\t\t\tif (image_scan_state[i] >= 2'd2) i_scan_lock <= 0; // T92 #17: this drive's running scan held the lock\n"
# T92 #17 H1: the trackinfo load of a drive (seek_state 4..7) owns tinfo_lock; a mount/eject strobe of that drive sets seek_state to 0, and the lock then
# stays 1 for ever (seek never reloads, the sector search waits for it, a READ DATA waits in EXEC1 with RQM low). The seek's owner states are 4..7 (the sector search holds the lock in its states 2..4 too, the strobe of this drive does not end that search; H3 and the reset do).
# The block request that the cancelled scan (state 2/3) or trackinfo load (state 5) had raised is withdrawn with it: only the state that raised it ever
# lowered it, and a request left high is served again and again by cpc_fdc, which then never gets to the next scan (found by fdcscan2, hold + eject).
NEW3 = (NEW2 + "\t\t\tif (image_scan_state[i] >= 2'd2) sd_rd_mount[i] <= 0; // T92 #17: the cancelled scan's block request is withdrawn\n"
        "\t\t\tif (seek_state[i] >= 3'd4) begin tinfo_lock <= 0; sd_rd_tinfo[i] <= 0; end // T92 #17 H1: this drive's trackinfo load held the lock; its block request is withdrawn\n")
# The mount block comes before the ce-gated blocks, which assign the same registers later in the same always block: a strobe in the clock of this
# drive's ce visit lost its reset to them. mnt_ev is the strobe edge; the scan block and the seek case skip that one visit of that drive.
OLD4 = "\treg [1:0] old_mounted;\n"
NEW4 = OLD4 + "\treg tinfo_take; // T92 #17: a lock taker (seek 3) took tinfo_lock in this clock (blocking)\n" + "\treg [1:0] mnt_ev; // T92 #17: the image event of this clock (the ce-gated scan/seek skip their visit of that drive)\n" + "\treg scan_take; // T92 #17 H4: a scan took i_scan_lock in this clock (blocking)\n"
OLD4B = "\t//new image mounted\n"
NEW4B = "\tmnt_ev = ~old_mounted & img_mounted;\n\tscan_take = 0;\n\ttinfo_take = 0;\n" + OLD4B
OLD5 = "\tif (ce) begin\n\t\tcase (image_scan_state[i_current_drive])\n"
NEW5 = "\tif (ce && !mnt_ev[i_current_drive]) begin\n\t\tcase (image_scan_state[i_current_drive])\n"
OLD5B = "\t\tcase(seek_state[i_current_drive])\n"
NEW5B = "\t\tif (!mnt_ev[i_current_drive]) case(seek_state[i_current_drive])\n"
# T92 #17 H2: the image of ds0 went away (mount/eject strobe) while a command waits for its sector info, which the sector search loads only for a
# ready image and the scan that makes it ready only starts when the FDC is idle. End the command with the answer the SETUP state (READ DATA/WRITE DATA 0x46.., READ ID)
# gives a missing image: ST0 0x40, ST1 0x05 (no data, missing address mark). One answer for a missing image at the start and in the middle of a command: it is the upstream
# answer of this core: cpc_core.v drives the u765 `ready` from image presence (cores/cpc/trx/cpc_core.v:241,244) and SETUP answers ~motor | ~ready | ~image_ready
# with 0x40/0x05. Which message AMSDOS shows for it is not proven here (no ROM in the repo); the board test records it. Covers READ/WRITE DATA and READ ID.
NR0 = "\t\t\t\tstatus[0] <= 8'h40; // T92 #17 H2: missing image, as SETUP\n\t\t\t\tstatus[1] <= 8'b101;\n\t\t\t\tstatus[2] <= 0;\n\t\t\t\tstate <= COMMAND_READ_RESULTS;\n"
# the C/H/R/N of the result phase: the command's own (RW), the head and cylinder (READ ID), the finished sector's (EXEC8 keeps i_sector_*)
NR = "\t\t\t\ti_sector_c <= i_c;\n\t\t\t\ti_sector_h <= i_h;\n\t\t\t\ti_sector_r <= i_r;\n\t\t\t\ti_sector_n <= i_n;\n" + NR0
NRID = "\t\t\t\ti_sector_c <= pcn[ds0];\n\t\t\t\ti_sector_h <= {7'd0, hds};\n\t\t\t\ti_sector_r <= 8'h00;\n\t\t\t\ti_sector_n <= 8'h00;\n" + NR0
OLD6 = "\t\t\tCOMMAND_RW_DATA_EXEC1:\n\t\t\tbegin\n\t\t\t\tif (!image_trackinfo_dirty[ds0] && !tinfo_lock) begin\n"
NEW6 = ("\t\t\tCOMMAND_RW_DATA_EXEC1:\n\t\t\tbegin\n\t\t\t\tif (!image_ready[ds0]) begin\n" + NR +
        "\t\t\t\tend else if (!image_trackinfo_dirty[ds0] && !tinfo_lock) begin\n")
OLD7 = "\t\t\tCOMMAND_RW_DATA_EXEC2:\n\t\t\tif (i_secinfo_valid[ds0][hds]) begin\n"
NEW7 = ("\t\t\tCOMMAND_RW_DATA_EXEC2:\n\t\t\tif (!image_ready[ds0]) begin\n" + NR +
        "\t\t\tend else if (i_secinfo_valid[ds0][hds]) begin\n")
OLD8 = "\t\t\tCOMMAND_RW_DATA_EXEC3:\n\t\t\tif (i_secinfo_valid[ds0][hds] && i_byte_clk_en) begin\n"
NEW8 = ("\t\t\tCOMMAND_RW_DATA_EXEC3:\n\t\t\tif (!image_ready[ds0]) begin\n" + NR +
        "\t\t\tend else if (i_secinfo_valid[ds0][hds] && i_byte_clk_en) begin\n")
OLD9 = "\t\t\tCOMMAND_READ_ID_WAIT_ID:\n\t\t\tif (!image_trackinfo_dirty[ds0] & i_byte_clk_en) begin\n"
NEW9 = ("\t\t\tCOMMAND_READ_ID_WAIT_ID:\n\t\t\tif (!image_ready[ds0]) begin\n" + NRID +
        "\t\t\tend else if (!image_trackinfo_dirty[ds0] & i_byte_clk_en) begin\n")
# T92 #17 H2 (EXEC8): a strobe in the data phase clears i_secinfo_valid, the rotation then stops (sector_byte_pos advances for a valid sector only) and
# the sector search re-validates only a ready drive: the wait for GAP3 never ends. A drive without an image ends the command here (ST0 0x40, ST1 0x05) instead.
OLD10 = "\t\t\tCOMMAND_RW_DATA_EXEC8:\n\t\t\tif (!sd_busy_sector && sd_rd_sector == 2'b00 && sd_wr_sector == 2'b00 && \n\t\t\t    sector_byte_pos[ds0][hds] >= (sector_end_pos[ds0][hds] + (gap3[ds0][hds]>>2))) // in GAP3\n"
NEW10 = "\t\t\tCOMMAND_RW_DATA_EXEC8:\n\t\t\tif (!sd_busy_sector && sd_rd_sector == 2'b00 && sd_wr_sector == 2'b00 && \n\t\t\t    (!image_ready[ds0] || sector_byte_pos[ds0][hds] >= (sector_end_pos[ds0][hds] + (gap3[ds0][hds]>>2)))) // in GAP3, or the image went away\n"
OLD11 = "\t\t\t\tif (~i_rtrack & ~(i_sk & (i_rw_deleted ^ i_sector_st2[6])) &\n"
NEW11 = "\t\t\t\tif (!image_ready[ds0]) begin\n" + NR0 + "\t\t\t\tend else if (~i_rtrack & ~(i_sk & (i_rw_deleted ^ i_sector_st2[6])) &\n"
# the rest of a sector of a WRITE must not be written into the image that came after the strobe (the mount block has put its size in image_size)
OLD12 = "if (i_write && buff_addr && i_seek_pos < image_size[ds0]) begin"
NEW12 = "if (i_write && buff_addr && image_ready[ds0] && i_seek_pos < image_size[ds0]) begin"
OLD13 = "\t\t\t\t\tif (i_seek_pos < image_size[ds0]) begin\n\t\t\t\t\t\ti_write_prev <= 1;"
NEW13 = "\t\t\t\t\tif (image_ready[ds0] && i_seek_pos < image_size[ds0]) begin\n\t\t\t\t\t\ti_write_prev <= 1;"
# H3: the sector search waits in state 1 for a trackinfo that a drive ejected meanwhile never reloads (the seek needs image_ready): it stops the other drive too
OLD14 = "\t\t\t\tsector_search_state <= 2;\n\t\t\tend\n\n\t\t\t2:\n"
NEW14 = "\t\t\t\tsector_search_state <= 2;\n\t\t\tend else if (!image_ready[sector_search_ds0]) sector_search_state <= 0; // T92 #17 H3: ejected while waiting\n\n\t\t\t2:\n"
OLD15 = "\t\ti_scan_lock <= 0;\n\t\ti_srt <= 4;\n"
NEW15 = "\t\ti_scan_lock <= 0;\n\t\tsector_search_state <= 0; // T92 #17: a reset ends a search that waits (the lock it waited for is cleared here too)\n\t\ti_srt <= 4;\n"
# the lock takers wait for a trackinfo block still in flight (a strobe's lock release does not stop it, sdcontrol has no cancel)
OLD16 = "\t\t\t\tif (!tinfo_lock) begin\n\t\t\t\t\tsector_byte_pos[i_current_drive] <= '{ 0, 0 };\n"
NEW16 = "\t\t\t\tif (!tinfo_lock && !sd_busy_tinfo && !i_scan_lock) begin\n\t\t\t\t\ttinfo_take = 1;\n\t\t\t\t\tsector_byte_pos[i_current_drive] <= '{ 0, 0 };\n"
OLD17 = "\t\t\tif (!image_trackinfo_dirty[sector_search_ds0] && !tinfo_lock) begin\n"
NEW17 = "\t\t\tif (!image_trackinfo_dirty[sector_search_ds0] && !tinfo_lock && !sd_busy_tinfo && !i_scan_lock && !scan_take && !tinfo_take) begin\n"
# T92 #17 H4: the image scan (state 2/3) and the trackinfo loads (seek 3..7, sector search 2) write/read the one image_track_offsets_addr/_wr: a trackinfo
# load of drive 0 that starts while drive 1's scan runs moves the address under the scan, the offsets land at the wrong slots and both drives end with
# 0 sectors on track 0 (two mounts close together, the T180 restore). A scan starts only without a trackinfo lock, a lock taker waits for the scan lock;
# scan_take (blocking, this clock) closes the one clock in which the scan start and the sector search both saw the other's lock free; tinfo_take does the same for seek 3 and
# sector search 1 (both tested the registered tinfo_lock in one clock and took it together). The scan start also waits for a trackinfo or sector block in flight (an H1 release does not stop it, sdcontrol has no cancel).
OLD18 = "\t\t\t\tif (!sd_busy_mount & !i_scan_lock & state == COMMAND_IDLE) begin\n\t\t\t\t\ti_scan_lock <= 1;\n"
NEW18 = "\t\t\t\tif (!sd_busy_mount & !sd_busy_tinfo & !sd_busy_sector & !i_scan_lock & !tinfo_lock & state == COMMAND_IDLE) begin\n\t\t\t\t\tscan_take = 1;\n\t\t\t\t\ti_scan_lock <= 1;\n"
EDITS = [(OLD, NEW), (OLD2, NEW3), (OLD4, NEW4), (OLD4B, NEW4B), (OLD5, NEW5), (OLD5B, NEW5B), (OLD6, NEW6), (OLD7, NEW7), (OLD8, NEW8), (OLD9, NEW9),
         (OLD10, NEW10), (OLD11, NEW11), (OLD12, NEW12), (OLD13, NEW13), (OLD14, NEW14), (OLD15, NEW15), (OLD16, NEW16), (OLD17, NEW17), (OLD18, NEW18)]
HDR = ("// T92 D3: cores/cpc/rtl/u765/u765.sv with changes made by tools/cpc_gen.py (the initialiser of `logical_size` in SECTOR_SIZE is an\n"
       "// assignment per call; T92 #17: a new image event wins over the ce-gated scan/seek of its drive and releases the scan/trackinfo lock its\n"
       "// drive held; a command whose image went away ends with the missing-image answer of SETUP (ST0 0x40, ST1 0x05), in its data phase too, and a WRITE stops writing; a sector search waiting for an\n"
       "// ejected drive or a reset ends; the trackinfo lock waits for a block in flight; a scan and a trackinfo load never run together; two lock takers never take the trackinfo lock in the same clock). Everything else, the licence header included, is the upstream file. Do not edit.\n")


def make():
    s = open(SRC).read()
    for old, new in EDITS:
        if s.count(old) != 1:
            raise SystemExit("cpc_gen: expected exactly one of %r in the upstream file, found %d (upstream changed?)" % (old[:60], s.count(old)))
        s = s.replace(old, new)
    return HDR + s


def main(argv):
    if argv[1:] == ["--check"]:
        if not os.path.exists(DST) or open(DST).read() != make():
            print("cpc_gen: cores/cpc/gen/u765.sv is not what tools/cpc_gen.py makes of cores/cpc/rtl/u765/u765.sv (run tools/cpc_gen.py)")
            return 1
        print("cpc_gen: u765.sv matches")
        return 0
    os.makedirs(os.path.dirname(DST), exist_ok=True)
    open(DST, "w").write(make())
    print("cpc_gen: wrote", DST)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
