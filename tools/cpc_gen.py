#!/usr/bin/env python3
"""T92 D3, T233: cores/cpc/gen/u765.sv = cores/cpc/rtl/u765/u765.sv (MiSTer-devel/Amstrad_MiSTer, GPL v2 or later, Gyorgy Szombathelyi) plus
the changes below, each an exact replacement that must match the upstream file once.
(1) Mechanical, Verilator needs it: the function SECTOR_SIZE declares `reg [15:0] logical_size = <expression of its arguments>;`.
In a static function the initialiser runs once at time 0, which Verilator rejects ("static variable initializer dependent on function I/O")
and Vivado may take either way; the intent is one evaluation per call, so the declaration is split from the assignment. The benches and the
synthesis read gen/, never rtl/ (one artifact for both).
(2) T233 (docs/specs/T233-cpc-fdc-repair.md), on the base of the board-good build 66f89900 (T233 D1):
  D2  RQM in COMMAND_IDLE no longer waits for a disk scan; a command that reads the image (READ/WRITE DATA, READ TRACK, READ ID) waits in
      COMMAND_WAIT_IMG while its drive's disk event or scan is pending, and the scan may start there as in COMMAND_IDLE.
  D3  The SD channel (upstream: the always block sdcontrol) moves into the always block fdc: a request flag is cleared when its block is
      issued (no owner withdraws an untaken request any more), a block not ended within SD_TIMEOUT sys clocks or ended with the new input
      sd_err is withdrawn and marks sd_fail_* for its owner (a scan ends with the drive not ready, a trackinfo load with an empty track, a
      command with ST0 0x40 / ST1 0x05), the next block goes out only when sd_ack and its history are low, the buffers take bytes of the
      block in flight only (sd_live).
  D4  A mount/eject strobe is only latched (ev_pend, ev_size, ev_wp); the ce-gated FDC applies it when no block of that drive is in flight
      and its scan, trackinfo load and sector search hold nothing (none of them takes the lock for that drive while the event waits); a
      command on that drive ends with ST0 0x40 / ST1 0x05 first; a reset applies it at once. The scan, the trackinfo load and the sector
      search take the one track table resource (i_scan_lock / tinfo_lock) through lk_take, so two of them never take it in the same clock;
      a reset ends a sector search (it releases the lock the search holds).
  tools/cpc_gen.py          (re)make cores/cpc/gen/u765.sv
  tools/cpc_gen.py --check  gen/u765.sv is what rtl/u765/u765.sv gives (the lint stage runs this)
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "cores/cpc/rtl/u765/u765.sv")
DST = os.path.join(ROOT, "cores/cpc/gen/u765.sv")


def t(s):
    """the blocks below are written with four spaces per level; the upstream file indents with tabs"""
    return re.sub(r"(?m)^((?:    )+)", lambda m: "\t" * (len(m.group(1)) // 4), s)


# (1) SECTOR_SIZE
OLD = "\t\treg [15:0] logical_size = (16'h80 << (n[3] ? 4'h8 : n[2:0]));\n"
NEW = "\t\treg [15:0] logical_size;\n\t\tlogical_size = (16'h80 << (n[3] ? 4'h8 : n[2:0]));\n"

EDITS = [(OLD, NEW)]


def edit(old, new):
    EDITS.append((t(old), t(new)))


# ---- the interface: the time limit of a block, the failure input, the buffers' guard, the wait state
edit("module u765 #(parameter CYCLES = 20'd4000, SPECCY_SPEEDLOCK_HACK = 0)\n",
     "module u765 #(parameter CYCLES = 20'd4000, SPECCY_SPEEDLOCK_HACK = 0,\n"
     "    SD_TIMEOUT = 20'd630000) // T233 D3: sys clocks a block may take from its issue to the fall of sd_ack (20 ms at 31.5 MHz)\n")
edit("    input            sd_ack,\n",
     "    input            sd_ack,\n"
     "    input            sd_err,      // T233 D3: the block that ends with this fall of sd_ack failed (cpc_fdc: a DDR2 line not answered)\n")
edit("reg          sd_buff_type;\n",
     "reg          sd_buff_type;\n"
     "reg          sd_live;     // T233 D3: a block is in flight: only its bytes go into the buffers (a withdrawn one may still be answered)\n")
edit(".wren_a(sd_buff_wr & sd_ack & sd_buff_type == UPD765_SD_BUFF_TRACKINFO),",
     ".wren_a(sd_buff_wr & sd_ack & sd_live & sd_buff_type == UPD765_SD_BUFF_TRACKINFO),")
edit(".wren_a(sd_buff_wr & sd_ack & sd_buff_type == UPD765_SD_BUFF_SECTOR),",
     ".wren_a(sd_buff_wr & sd_ack & sd_live & sd_buff_type == UPD765_SD_BUFF_SECTOR),")
edit(" COMMAND_INVALID1\n} state_t;",
     " COMMAND_INVALID1,\n\n"
     " COMMAND_WAIT_IMG // T233 D2: a command that reads the image waits for its drive's disk event and scan (last: the others keep their numbers)\n"
     "} state_t;")

# ---- D3: the SD channel leaves its own always block (its logic is at the end of the always block fdc, see the last edit)
SDCTL_OLD = re.compile(r"always @\(posedge clk_sys\) begin : sdcontrol\n.*?\nend\n\n////\n", re.S)
SDCTL_DECL = t("""\
// T233 D3: the SD channel is driven at the end of the always block fdc (it clears a request flag when it issues the block); its own state:
reg        sd_drv;          // the drive of the block in flight (or of the last one)
reg [19:0] sd_tmr;          // sys clocks since the block was issued
reg        sd_fail_mount, sd_fail_tinfo, sd_fail_sector;   // the last block of that kind was withdrawn or ended with sd_err (its owner takes the mark)
wire       sd_quiet = !sd_busy_mount & !sd_busy_tinfo & !sd_busy_sector & ack == 6'd0 & !sd_ack;   // nothing in flight, no answer under way

////
""")

# ---- D4: the strobe is latched; the per-clock flags of the take and the apply
edit("    reg [1:0] old_mounted;\n",
     "    reg [1:0] old_mounted;\n"
     "    reg [1:0] ev_pend, ev_wp, ev_new, ev_go; // T233 D4: a disk event waits / its write protect / a strobe in this clock / applied in this clock\n"
     "    reg[31:0] ev_size[2];\n"
     "    reg       lk_take;  // T233: the track table resource (i_scan_lock / tinfo_lock) was taken in this clock\n")
edit("""\
    //new image mounted
    for(int i=0;i<2;i++) begin\x20
        old_mounted[i] <= img_mounted[i];
        if(~old_mounted[i] & img_mounted[i]) begin
            image_wp[i] <= img_wp;
            image_size[i] <= img_size;
            image_scan_state[i] <= {1'b0, |img_size};
            image_ready[i] <= 0;
            disk_changed[i] <= 1;
            int_state[i] <= 0;
            seek_state[i] <= 0;
            i_seek_start[i] <= 0;
            next_weak_sector[i] <= 0;
            i_current_sector_pos[i] <= '{ 0, 0 };
            i_secinfo_valid[i] <= '{ 0, 0 };
        end
    end
""", """\
    //new image mounted: T233 D4 latches it only; the FDC applies it below (or the reset at once)
    lk_take = 0;
    ev_go = 0;
    ev_new = ~old_mounted & img_mounted;
    for(int i=0;i<2;i++) begin
        old_mounted[i] <= img_mounted[i];
        if (ev_new[i]) begin
            ev_pend[i] <= 1;
            ev_wp[i] <= img_wp;
            ev_size[i] <= img_size;
        end
    end
""")

# ---- the scan: starts beside no trackinfo owner, also in COMMAND_WAIT_IMG (D2), not for a drive whose event waits; a failed block ends it
edit("""\
            1: //read the first 512 byte
                if (!sd_busy_mount & !i_scan_lock & state == COMMAND_IDLE) begin
                    i_scan_lock <= 1;
""", """\
            1: //read the first 512 byte
                if (!sd_busy_mount & !i_scan_lock & !tinfo_lock & !lk_take & !ev_pend[i_current_drive] & (state == COMMAND_IDLE | state == COMMAND_WAIT_IMG)) begin
                    lk_take = 1;
                    i_scan_lock <= 1;
""")
edit("""\
            2: //process the header
            if (!sd_busy_mount & sd_rd_mount == 2'b00) begin
                if (!buff_wait) begin
""", """\
            2: //process the header
            if (!sd_busy_mount & sd_rd_mount == 2'b00) begin
                if (sd_fail_mount) begin // T233 D3: the block did not come: the drive is not ready
                    sd_fail_mount <= 0;
                    image_ready[i_current_drive] <= 0;
                    image_scan_state[i_current_drive] <= 0;
                    i_scan_lock <= 0;
                end else if (!buff_wait) begin
""")
edit("""\
            end else begin
                sd_rd_mount <= 0;
            end
            3: begin
""", """\
            end
            3: begin
""")

# ---- the seek's trackinfo load: takes the resource through lk_take, not for a drive whose event waits; a failed block = an empty track
edit("""\
                if (!tinfo_lock) begin
                    sector_byte_pos[i_current_drive] <= '{ 0, 0 };
""", """\
                if (!tinfo_lock && !i_scan_lock && !lk_take && !ev_pend[i_current_drive]) begin
                    lk_take = 1;
                    sector_byte_pos[i_current_drive] <= '{ 0, 0 };
""")
edit("""\
            5:
            if (!sd_busy_tinfo & sd_rd_tinfo == 2'b00) begin
                tinfo_addr <= {image_track_offsets_in[0], 8'h16}; //gap3 length
                tinfo_wait <= 1;
                seek_state[i_current_drive] <= 6;
            end else begin
                sd_rd_tinfo <= 0;
            end
""", """\
            5:
            if (!sd_busy_tinfo & sd_rd_tinfo == 2'b00) begin
                if (sd_fail_tinfo) begin // T233 D3: the block did not come: an empty track
                    sd_fail_tinfo <= 0;
                    i_current_track_sectors[i_current_drive][tinfo_hds] <= 0;
                    tinfo_lock <= 0;
                    image_trackinfo_dirty[i_current_drive] <= 0;
                    seek_state[i_current_drive] <= 0;
                end else begin
                    tinfo_addr <= {image_track_offsets_in[0], 8'h16}; //gap3 length
                    tinfo_wait <= 1;
                    seek_state[i_current_drive] <= 6;
                end
            end
""")

# ---- the sector search: the same take
edit("""\
            1:
            if (!image_trackinfo_dirty[sector_search_ds0] && !tinfo_lock) begin
                tinfo_lock <= 1;
""", """\
            1:
            if (!image_trackinfo_dirty[sector_search_ds0] && !tinfo_lock && !i_scan_lock && !lk_take && !ev_pend[sector_search_ds0]) begin
                lk_take = 1;
                tinfo_lock <= 1;
""")

# ---- the reset: ends a sector search (it held the lock released here) and applies a waiting disk event
edit("""\
        i_scan_lock <= 0;
        i_srt <= 4;
        tinfo_lock <= 0;
    end else if (ce) begin
""", """\
        i_scan_lock <= 0;
        i_srt <= 4;
        tinfo_lock <= 0;
        sector_search_state <= 0; // T233 D4
        for (int e = 0; e < 2; e++) if (ev_pend[e] && !ev_new[e]) begin // T233 D4: a disk event waiting is applied in reset at once
            ev_pend[e] <= 0;
            image_wp[e] <= ev_wp[e];
            image_size[e] <= ev_size[e];
            image_scan_state[e] <= {1'b0, |ev_size[e]};
            image_ready[e] <= 0;
            next_weak_sector[e] <= 0;
            i_current_sector_pos[e] <= '{ 0, 0 };
        end
    end else if (ce) begin
""")

# ---- D2: RQM in COMMAND_IDLE never waits for a scan; a command that reads the image waits in COMMAND_WAIT_IMG
edit("""\
                m_status[UPD765_MAIN_RQM] <= !image_scan_state[0] & !image_scan_state[1];

                if (~old_wr & wr & a0 & !image_scan_state[0] & !image_scan_state[1]) begin
""", """\
                m_status[UPD765_MAIN_RQM] <= 1; // T233 D2: never held for a disk scan (AMSDOS polls RQM without a time limit)

                if (~old_wr & wr & a0) begin
""")
edit("""\
            COMMAND_READ_ID1:
            if (~old_wr & wr & a0) begin
                ds0 <= din[0];
                if (~motor[din[0]] | ~ready[din[0]] | ~image_ready[din[0]]) begin
""", """\
            COMMAND_READ_ID1:
            if (~old_wr & wr & a0) begin
                ds0 <= din[0];
                if (motor[din[0]] & ready[din[0]] & (image_scan_state[din[0]] != 0 | ev_pend[din[0]])) begin // T233 D2: the image is being read
                    hds <= din[2];
                    i_command <= COMMAND_READ_ID_WAIT_ID;
                    m_status[UPD765_MAIN_RQM] <= 0;
                    state <= COMMAND_WAIT_IMG;
                end else if (~motor[din[0]] | ~ready[din[0]] | ~image_ready[din[0]]) begin
""")
edit("""\
                    7: begin
                            i_dtl <= din;
                            i_substate <= 0;
                            if (~motor[ds0] | ~ready[ds0] | ~image_ready[ds0]) begin
""", """\
                    7: begin
                            i_dtl <= din;
                            i_substate <= 0;
                            if (motor[ds0] & ready[ds0] & (image_scan_state[ds0] != 0 | ev_pend[ds0])) begin // T233 D2: the image is being read
                                m_status[UPD765_MAIN_RQM] <= 0;
                                state <= COMMAND_WAIT_IMG;
                            end else if (~motor[ds0] | ~ready[ds0] | ~image_ready[ds0]) begin
""")
edit("""\
            COMMAND_READ_RESULTS:
            begin
""", """\
            COMMAND_WAIT_IMG: // T233 D2: the checks of SETUP / READ ID once the drive's event is applied and its scan is over
            if (!image_scan_state[ds0] && !ev_pend[ds0]) begin
                if (~motor[ds0] | ~ready[ds0] | ~image_ready[ds0]) begin
                    status[0] <= 8'h40;
                    status[1] <= 8'b101;
                    status[2] <= 0;
                    state <= COMMAND_READ_RESULTS;
                end else if (hds & ~image_sides[ds0]) begin
                    if (i_command != COMMAND_READ_ID_WAIT_ID) hds <= 0;
                    status[0] <= 8'h48; //no side B
                    status[1] <= 0;
                    status[2] <= 0;
                    state <= COMMAND_READ_RESULTS;
                end else begin
                    state <= i_command;
                end
            end

            COMMAND_READ_RESULTS:
            begin
""")

# ---- D3: the sector states wait for their flag and busy (the SD channel clears the flag when it issues); no owner withdraws an untaken request
edit("""\
                state <= COMMAND_RW_DATA_EXEC6;
            end else begin
                sd_rd_sector <= 0;
                sd_wr_sector <= 0;
            end
""", """\
                state <= COMMAND_RW_DATA_EXEC6;
            end
""")
edit("""\
                    i_timeout <= i_timeout - 1'd1;
                end
            end else begin
                sd_rd_sector <= 0;
                sd_wr_sector <= 0;
            end
""", """\
                    i_timeout <= i_timeout - 1'd1;
                end
            end
""")
edit("""\
                    state <= COMMAND_RW_DATA_EXEC2;
                end
            end else begin
                sd_rd_sector <= 0;
                sd_wr_sector <= 0;
            end
""", """\
                    state <= COMMAND_RW_DATA_EXEC2;
                end
            end
""")


def not_ready(ind, c, h, r, n):
    """end the command as SETUP answers a missing image: ST0 0x40, ST1 0x05 (missing address mark, no data)"""
    p = "    " * ind
    return (p + "i_sector_c <= %s;\n" % c + p + "i_sector_h <= %s;\n" % h + p + "i_sector_r <= %s;\n" % r + p + "i_sector_n <= %s;\n" % n +
            p + "status[0] <= 8'h40;\n" + p + "status[1] <= 8'b101;\n" + p + "status[2] <= 0;\n" + p + "state <= COMMAND_READ_RESULTS;\n")


# ---- after the command machine (its assignments win): a failed sector block ends the command (D3); the disk events are applied (D4);
# then, outside ce, the SD channel (D3)
edit("""\
        endcase //status

    end
end

endmodule
""", """\
        endcase //status

        // T233 D3: a sector block that did not come, or a write that did not go, ends the command as SETUP answers a missing image
        if (sd_fail_sector && !sd_busy_sector && sd_rd_sector == 2'b00 && sd_wr_sector == 2'b00 &&
            (state == COMMAND_RW_DATA_EXEC5 || state == COMMAND_RW_DATA_EXEC6 || state == COMMAND_RW_DATA_EXEC8)) begin   // the states that wait for a block
            sd_fail_sector <= 0;
            sd_rd_sector <= 0;
            sd_wr_sector <= 0;
""" + not_ready(3, "i_c", "i_h", "i_r", "i_n") + """\
        end

        // T233 D4: the disk event of drive e is applied here and nowhere else (the reset aside): when no block of e is in flight and its scan,
        // trackinfo load and sector search hold nothing (none of them takes the lock for e while the event waits). A command on e ends first.
        for (int e = 0; e < 2; e++) begin
            if (ev_pend[e] && !ev_new[e] && (sd_quiet || sd_drv != e) && image_scan_state[e] < 2 && seek_state[e] < 4 &&
                !(sector_search_ds0 == e && sector_search_state >= 2)) begin
                ev_go[e] = 1;
                ev_pend[e] <= 0;
                image_wp[e] <= ev_wp[e];
                image_size[e] <= ev_size[e];
                image_scan_state[e] <= {1'b0, |ev_size[e]};
                image_ready[e] <= 0;
                image_trackinfo_dirty[e] <= 1;
                disk_changed[e] <= 1;
                int_state[e] <= 0;
                seek_state[e] <= 0;
                i_seek_start[e] <= 0;
                next_weak_sector[e] <= 0;
                i_current_sector_pos[e] <= '{ 0, 0 };
                i_secinfo_valid[e] <= '{ 0, 0 };
                sd_rd_mount[e] <= 0;
                sd_rd_tinfo[e] <= 0;
                sd_rd_sector[e] <= 0;
                sd_wr_sector[e] <= 0;
                if (sector_search_ds0 == e && sector_search_state == 1) sector_search_state <= 0;
                if (ds0 == e && state >= COMMAND_RW_DATA_EXEC && state <= COMMAND_RW_DATA_EXEC8) begin
""" + not_ready(5, "i_c", "i_h", "i_r", "i_n") + """\
                end
                if (ds0 == e && state == COMMAND_READ_ID_WAIT_ID) begin
""" + not_ready(5, "pcn[ds0]", "{7'd0, hds}", "8'h00", "8'h00") + """\
                end
            end
        end
    end

    // T233 D3: the SD channel (upstream: the always block sdcontrol). One block at a time, in the order mount, trackinfo, sector read, sector write;
    // the flag of a request is cleared here when its block is issued, so its owner waits for flag and busy both low. A block not ended within
    // SD_TIMEOUT, or ended with sd_err, is withdrawn and marks sd_fail_* for its owner. The next block goes out only when sd_ack and its history
    // are low (a late answer of a withdrawn block never ends the next one); never a request of a drive whose event is applied in this clock.
    ack <= {ack[4:0], sd_ack};
    if (reset) begin
        sd_rd <= 0;
        sd_wr <= 0;
        sd_busy_mount <= 0;
        sd_busy_tinfo <= 0;
        sd_busy_sector <= 0;
        sd_live <= 0;
        sd_fail_mount <= 0;
        sd_fail_tinfo <= 0;
        sd_fail_sector <= 0;
    end else if (sd_busy_mount | sd_busy_tinfo | sd_busy_sector) begin
        if (ack[5:4] == 'b01) begin
            sd_rd <= 0;
            sd_wr <= 0;
        end
        if (ack[5:4] == 'b10 || sd_tmr == SD_TIMEOUT) begin
            if (sd_err || sd_tmr == SD_TIMEOUT) begin
                if (sd_busy_mount) sd_fail_mount <= 1;
                if (sd_busy_tinfo) sd_fail_tinfo <= 1;
                if (sd_busy_sector) sd_fail_sector <= 1;
            end
            sd_rd <= 0;
            sd_wr <= 0;
            sd_busy_mount <= 0;
            sd_busy_tinfo <= 0;
            sd_busy_sector <= 0;
            sd_live <= 0;
        end else begin
            sd_tmr <= sd_tmr + 1'd1;
        end
    end else if (ack == 6'd0 && !sd_ack) begin
        sd_tmr <= 0;
        if (sd_rd_mount != 2'b00 && (sd_rd_mount & ev_go) == 2'b00) begin
            sd_lba <= 0;
            sd_rd  <= sd_rd_mount;
            sd_drv <= sd_rd_mount[1];
            sd_rd_mount <= 0;
            sd_buff_type <= UPD765_SD_BUFF_SECTOR;
            sd_busy_mount <= 1;
            sd_fail_mount <= 0;
            sd_live <= 1;
        end else if (sd_rd_tinfo != 2'b00 && (sd_rd_tinfo & ev_go) == 2'b00) begin
            sd_lba <= image_track_offsets_in[15:1];
            sd_rd  <= sd_rd_tinfo;
            sd_drv <= sd_rd_tinfo[1];
            sd_rd_tinfo <= 0;
            sd_buff_type <= UPD765_SD_BUFF_TRACKINFO;
            sd_busy_tinfo <= 1;
            sd_fail_tinfo <= 0;
            sd_live <= 1;
        end else if (sd_rd_sector != 2'b00 && (sd_rd_sector & ev_go) == 2'b00) begin
            sd_lba <= i_seek_pos[31:9];
            sd_rd  <= sd_rd_sector;
            sd_drv <= sd_rd_sector[1];
            sd_rd_sector <= 0;
            sd_buff_type <= UPD765_SD_BUFF_SECTOR;
            sd_busy_sector <= 1;
            sd_fail_sector <= 0;
            sd_live <= 1;
        end else if (sd_wr_sector != 2'b00 && (sd_wr_sector & ev_go) == 2'b00) begin
            sd_lba <= i_seek_pos[31:9] - i_write_prev;
            sd_wr  <= sd_wr_sector;
            sd_drv <= sd_wr_sector[1];
            sd_wr_sector <= 0;
            sd_buff_type <= UPD765_SD_BUFF_SECTOR;
            sd_busy_sector <= 1;
            sd_fail_sector <= 0;
            sd_live <= 1;
        end
    end
end

endmodule
""")

HDR = ("// T92 D3, T233: cores/cpc/rtl/u765/u765.sv with the changes tools/cpc_gen.py makes (the initialiser of `logical_size` in SECTOR_SIZE is an\n"
       "// assignment per call; T233 D2: RQM never waits for a disk scan, a command that reads the image waits for it in COMMAND_WAIT_IMG; D3: the SD\n"
       "// channel inside the FDC's always block, every block bounded (SD_TIMEOUT, sd_err) with a failure answer; D4: a disk event is latched and applied\n"
       "// by the FDC at a safe point, one taker of the track table resource per clock). Everything else, the licence header included, is the upstream\n"
       "// file. Do not edit.\n")


def make():
    s = open(SRC).read()
    s, n = SDCTL_OLD.subn(lambda m: SDCTL_DECL, s)
    if n != 1:
        raise SystemExit("cpc_gen: expected the always block sdcontrol once in the upstream file, found %d (upstream changed?)" % n)
    for old, new in EDITS:
        if s.count(old) != 1:
            raise SystemExit("cpc_gen: expected exactly one of %r in the upstream file, found %d (upstream changed?)" % (old[:70], s.count(old)))
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
