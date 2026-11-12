// T140 D25: the DDR2 side of the C128 (ui_clk). Replaces c128_rome.v: one command open at a time on the host's engine client e_* (mig_arb's
// contract: a command is taken when `e_en && e_rdy`, a write offers command and data at once, read data comes on c_rd_data with e_rd_valid, in order).
// Three jobs, each a toggle request from the core clock and a toggle back:
//   R  ROM line fetch   (c128_romc.v)   line n at ROM_BASE + 16 n,  result `fill`, `done_r`
//   M  RAM line fetch   (c128_ramd.v)   line n at RAM_BASE + 16 n,  result `fill`, `done_m`
//   W  RAM byte write   (c128_ramd.v)   byte a at RAM_BASE + a, `w_done` once the controller took command and data
// Order: write first (the core side lets a read start only when its writes are done, so this is only a rule of progress), then M, then R.
// `fill` is shared; a read's result is stable until the next read's, which is at least GAP ui clocks after this one's toggle: the core side has
// copied it long before (3 clocks of synchroniser).
// `owns` is high from the start of a command until it is taken: layer2 puts this block's command, data and mask on e_* instead of the frame writer's.
`timescale 1ns / 1ps
module c128_ddrx #(
    parameter [26:0] ROM_BASE_W = 27'h0E00000,       // byte 0x01C00000 / 2 (16 bit words)
    parameter [26:0] RAM_BASE_W = 27'h0E10000        // byte 0x01C20000 / 2
) (
    input  wire         ui_clk,
    input  wire         ui_rst,
    // R
    input  wire         r_req_tgl,
    input  wire [12:0]  r_req_line,
    output reg          r_done_tgl = 1'b0,
    // M
    input  wire         m_req_tgl,
    input  wire [12:0]  m_req_line,                  // RAM line = byte address >> 4 (128 KiB: 13 bits)
    output reg          m_done_tgl = 1'b0,
    // W
    input  wire         w_req_tgl,
    input  wire [16:0]  w_req_addr,                  // RAM byte address, stable until w_done_tgl
    input  wire  [7:0]  w_req_data,
    output reg          w_done_tgl = 1'b0,

    output reg  [127:0] fill = 128'd0,

    output reg          owns = 1'b0,
    output reg          en = 1'b0,
    output reg  [2:0]   cmd = 3'b001,
    output reg  [26:0]  addr = 27'd0,
    output reg  [127:0] wdata = 128'd0,
    output reg  [15:0]  wmask = 16'hFFFF,
    input  wire         rdy,
    input  wire         rd_valid,
    input  wire [127:0] rd_data
);
    (* ASYNC_REG = "TRUE" *) reg [2:0] rs = 3'b000, ms = 3'b000, ws = 3'b000;
    wire r_ev = rs[2] != rs[1];
    wire m_ev = ms[2] != ms[1];
    wire w_ev = ws[2] != ws[1];
    // requests seen and not yet served (an event is one clock; it is kept until its job starts)
    reg r_pend = 1'b0, m_pend = 1'b0, w_pend = 1'b0;
    localparam S_IDLE = 2'd0, S_ISSUE = 2'd1, S_WAIT = 2'd2, S_GAP = 2'd3;
    reg [1:0] st = S_IDLE;
    reg [1:0] who = 2'd0;                            // 0 R, 1 M, 2 W
    reg [3:0] gap = 4'd0;
    always @(posedge ui_clk) begin
        rs <= {rs[1:0], r_req_tgl};
        ms <= {ms[1:0], m_req_tgl};
        ws <= {ws[1:0], w_req_tgl};
        if (r_ev) r_pend <= 1'b1;
        if (m_ev) m_pend <= 1'b1;
        if (w_ev) w_pend <= 1'b1;
        if (ui_rst) begin
            st <= S_IDLE; en <= 1'b0; owns <= 1'b0;
            // A request is a toggle and it is served when the done toggle follows. The reset keeps what came in meanwhile (the synchronisers run on, the
            // `pend` flags are not cleared): c128_ramd starts its sweep with the first clock after configuration, before the controller is calibrated,
            // and has no timeout. A command that was open when the reset came is asked again; one that is finished (S_GAP) is not.
            if (st == S_ISSUE || st == S_WAIT)
                case (who) 2'd0: r_pend <= 1'b1; 2'd1: m_pend <= 1'b1; default: w_pend <= 1'b1; endcase
        end else case (st)
            S_IDLE: begin
                if (w_pend) begin
                    who <= 2'd2; w_pend <= 1'b0;
                    cmd <= 3'b000; en <= 1'b1; owns <= 1'b1;
                    addr  <= RAM_BASE_W + {7'd0, w_req_addr[16:4], 3'b000};
                    wdata <= {16{w_req_data}};
                    wmask <= ~(16'h0001 << w_req_addr[3:0]);
                    st <= S_ISSUE;
                end else if (m_pend) begin
                    who <= 2'd1; m_pend <= 1'b0;
                    cmd <= 3'b001; en <= 1'b1; owns <= 1'b1;
                    addr <= RAM_BASE_W + {11'd0, m_req_line, 3'b000};
                    wmask <= 16'hFFFF;
                    st <= S_ISSUE;
                end else if (r_pend) begin
                    who <= 2'd0; r_pend <= 1'b0;
                    cmd <= 3'b001; en <= 1'b1; owns <= 1'b1;
                    addr <= ROM_BASE_W + {11'd0, r_req_line, 3'b000};
                    wmask <= 16'hFFFF;
                    st <= S_ISSUE;
                end
            end
            S_ISSUE: if (rdy) begin                  // en && rdy: taken in this clock
                en <= 1'b0; owns <= 1'b0;
                if (who == 2'd2) begin
                    w_done_tgl <= ~w_done_tgl;
                    st <= S_IDLE;
                end else
                    st <= S_WAIT;
            end
            S_WAIT: if (rd_valid) begin
                fill <= rd_data;
                if (who == 2'd0) r_done_tgl <= ~r_done_tgl; else m_done_tgl <= ~m_done_tgl;
                gap <= 4'd0; st <= S_GAP;
            end
            default: begin                           // S_GAP: a result is not overwritten before the core side has copied it
                gap <= gap + 4'd1;
                if (gap == 4'd7) st <= S_IDLE;
            end
        endcase
    end
endmodule
