// Drive A's debug hardware (T21, ARCH-lean-state-access §7.2): one comparator,
// step and halt, in the drive's clock (cpu_clk, drive_unit.vhd), its registers
// in page 3 of our IO block in the C64's clock (trx_sys.v rules).
//
// The drive's 6502 reports every cycle it completes (cpu_part_1541's debug
// stream: trace_valid one clock after the cycle, trace {sync, ATN, DATA, CLK,
// SYNC (disk), SO, IRQ, read, data, address}; bit 31, the 6502's opcode
// fetch, comes from tools/patches/gideon/0004). A cycle with bit 31 set ends
// at an instruction boundary (an interrupt that replaces the fetch is not
// told apart). The halt gates the drive's ticks (drive_unit's halt) two clocks
// after the cycle that stops it, long before its next cycle (1 MHz).
//
//   page 3
//     000  CMD       W   0x10 DRV_PAUSE (at the next cycle), 0x11 DRV_RUN
//                        (release the drive's hold), 0x12 DRV_STEP_INSTR (run
//                        the drive alone for ARG instructions, 0 = 1; the C64
//                        stays as it is), 3 RUN (trx_dbg.v's; releases the
//                        drive's hold too)
//     004  ARG       W   b15:0 (shared with trx_dbg.v)
//     0C0  DRV_CMP   W   b15:0 address, b16 enable, b17 type (0 exec at an
//                        instruction boundary, 1 a write to the address): a
//                        hit holds the drive and stops the C64 at its next
//                        phi2 falling edge (trx_dbg.v REASON 8)
//     0C4  DRV_STAT  R   {DPC (the address of the drive's last opcode fetch),
//                        12'd0, comparator enabled, a step runs, held by the
//                        debugger, halted (by the machine pause or the debugger)}
//     0C8  DCYCLES   R   drive CPU cycles (wraps; read while the drive runs it
//                        may be torn)
// The status and counters come into this clock through plain synchronizers:
// exact while the drive stands.
module drv_dbg (
    input  wire        clk,                 // C64 clock (sys_clk)
    input  wire        r_we,
    input  wire [10:0] r_addr,              // {page 3, word}
    input  wire [31:0] r_wdata,
    output reg  [31:0] r_rdata,             // combinational from r_addr
    output wire        stop_ev,             // clk pulse: the comparator held the drive
    output wire        done_ev,             // clk pulse: a drive step ended
    // the drive (dclk = cpu_clk)
    input  wire        dclk,
    input  wire        trace_valid,
    input  wire [31:0] trace,
    input  wire        c64_halted,          // the machine pause, in dclk
    output wire        drive_halt
);
    // ---- C64 clock: registers, commands --------------------------------------------
    reg  [15:0] arg = 16'd0;
    reg  [17:0] cmp = 18'd0;
    reg         c_tgl = 1'b0;
    reg   [1:0] c_code = 2'd0;              // 0 pause, 1 run, 2 step
    reg  [15:0] c_arg = 16'd0;
    always @(posedge clk) begin
        if (r_we && r_addr == 11'h401) arg <= r_wdata[15:0];
        if (r_we && r_addr == 11'h430) cmp <= r_wdata[17:0];
        if (r_we && r_addr == 11'h400)
            case (r_wdata[7:0])
            8'h10: begin c_code <= 2'd0; c_tgl <= ~c_tgl; end
            8'h03, 8'h11: begin c_code <= 2'd1; c_tgl <= ~c_tgl; end
            8'h12: begin c_code <= 2'd2; c_arg <= arg == 16'd0 ? 16'd1 : arg; c_tgl <= ~c_tgl; end
            default: ;
            endcase
    end

    // ---- drive clock ---------------------------------------------------------------
    (* ASYNC_REG = "TRUE" *) reg [2:0] c_s = 3'b000;
    reg         hold = 1'b0, step = 1'b0;
    reg  [15:0] cnt = 16'd0;
    reg  [15:0] dpc = 16'd0;
    reg  [31:0] dcyc = 32'd0;
    reg         hit_tgl = 1'b0, done_tgl = 1'b0;
    wire        c_ev = c_s[2] != c_s[1];
    wire        ib   = trace_valid && trace[31];
    wire        hit  = cmp[16] && trace[15:0] == cmp[15:0] &&
                       (cmp[17] ? trace_valid && !trace[24] : ib);
    always @(posedge dclk) begin
        c_s <= {c_s[1:0], c_tgl};
        if (trace_valid) dcyc <= dcyc + 1'b1;
        if (ib) dpc <= trace[15:0];
        if (step && ib) begin
            if (cnt == 16'd1) begin
                step     <= 1'b0;
                hold     <= 1'b1;
                done_tgl <= ~done_tgl;
            end else
                cnt <= cnt - 1'b1;
        end
        if (hit) begin
            hold    <= 1'b1;
            step    <= 1'b0;
            hit_tgl <= ~hit_tgl;
        end
        if (c_ev)
            case (c_code)
            2'd0: hold <= 1'b1;
            2'd1: begin hold <= 1'b0; step <= 1'b0; end
            default: if (hold || c64_halted) begin
                hold <= 1'b0;
                step <= 1'b1;
                cnt  <= c_arg;
            end
            endcase
    end
    assign drive_halt = (c64_halted && !step) || hold;

    // ---- back into the C64 clock -------------------------------------------------------
    (* ASYNC_REG = "TRUE" *) reg [2:0] h_s = 3'b000, d_s = 3'b000;
    (* ASYNC_REG = "TRUE" *) reg [3:0] st_s1 = 4'd0, st_s2 = 4'd0;
    (* ASYNC_REG = "TRUE" *) reg [47:0] v_s1 = 48'd0, v_s2 = 48'd0;
    always @(posedge clk) begin
        h_s   <= {h_s[1:0], hit_tgl};
        d_s   <= {d_s[1:0], done_tgl};
        st_s1 <= {cmp[16], step, hold, drive_halt};
        st_s2 <= st_s1;
        v_s1  <= {dpc, dcyc};
        v_s2  <= v_s1;
    end
    assign stop_ev = h_s[2] != h_s[1];
    assign done_ev = d_s[2] != d_s[1];

    always @(*) begin
        r_rdata = 32'd0;
        case (r_addr)
        11'h431: r_rdata = {v_s2[47:32], 12'd0, st_s2};
        11'h432: r_rdata = v_s2[31:0];
        default: ;
        endcase
    end
endmodule
