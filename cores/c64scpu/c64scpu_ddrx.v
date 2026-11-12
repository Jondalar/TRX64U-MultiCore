// T140 D25 (first made for the C128 core): the DDR2 side of the core (ui_clk): one command open at a time on the host's engine client e_* (mig_arb's
// contract: a command is taken when `e_en && e_rdy`, a write offers command and data at once, read data comes on c_rd_data with e_rd_valid, in order).
// Three jobs, each a toggle request from the core clock and a toggle back:
//   R  ROM line fetch   (c64scpu_romc.v)   line n at ROM_BASE + 16 n,  result `fill`, `done_r`
//   M  RAM line fetch   (c64scpu_ramd.v)   line n at RAM_BASE + 16 n,  result `fill`, `done_m`
//   W  RAM byte write   (c64scpu_ramd.v)   byte a at RAM_BASE + a, `w_done` once the controller took command and data
//   S  SCPU line fetch  (scpu_cache.v)     line n at SC_BASE + 16 n (SC_BASE = byte 0x02000000), result `fill`, `done_s`
//   SW SCPU byte write  (scpu_cache.v)     byte a at SC_BASE + a, `sw_done` once the controller took command and data
// Order: writes first (the core side lets a read start only when its writes are done, so this is only a rule of progress), then M, then S, then R.
// `fill` is shared; a read's result is stable until the next read's, which is at least GAP ui clocks after this one's toggle: the core side has
// copied it long before (3 clocks of synchroniser).
// `owns` is high from the start of a command until it is taken: layer2 puts this block's command, data and mask on e_* (the select there has no other user in this core).
`timescale 1ns / 1ps
module c64scpu_ddrx #(
    parameter [26:0] ROM_BASE_W = 27'h0E00000,       // byte 0x01C00000 / 2 (16 bit words)
    parameter [26:0] RAM_BASE_W = 27'h0E10000,       // byte 0x01C20000 / 2
    parameter [26:0] SC_BASE_W  = 27'h1000000        // byte 0x02000000 / 2 (T182 step 3: SuperRAM, SRAM and the SuperCPU ROM are 0x02000000 + p, p < 2^25)
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
    // S, SW (T182 step 3)
    input  wire         s_req_tgl,
    input  wire [20:0]  s_req_line,                  // p >> 4
    output reg          s_done_tgl = 1'b0,
    input  wire         sw_req_tgl,
    input  wire [24:0]  sw_req_addr,                 // p, stable until sw_done_tgl
    input  wire  [7:0]  sw_req_data,
    output reg          sw_done_tgl = 1'b0,

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
    (* ASYNC_REG = "TRUE" *) reg [2:0] rs = 3'b000, ms = 3'b000, ws = 3'b000, ss = 3'b000, sws = 3'b000;
    wire r_ev = rs[2] != rs[1];
    wire m_ev = ms[2] != ms[1];
    wire w_ev = ws[2] != ws[1];
    wire s_ev = ss[2] != ss[1];
    wire sw_ev = sws[2] != sws[1];
    // requests seen and not yet served (an event is one clock; it is kept until its job starts)
    reg r_pend = 1'b0, m_pend = 1'b0, w_pend = 1'b0, s_pend = 1'b0, sw_pend = 1'b0;
    localparam S_IDLE = 2'd0, S_ISSUE = 2'd1, S_WAIT = 2'd2, S_GAP = 2'd3;
    reg [1:0] st = S_IDLE;
    reg [2:0] who = 3'd0;                            // 0 R, 1 M, 2 W, 3 S, 4 SW
    reg [3:0] gap = 4'd0;
    always @(posedge ui_clk) begin
        rs <= {rs[1:0], r_req_tgl};
        ms <= {ms[1:0], m_req_tgl};
        ws <= {ws[1:0], w_req_tgl};
        ss <= {ss[1:0], s_req_tgl};
        sws <= {sws[1:0], sw_req_tgl};
        if (s_ev) s_pend <= 1'b1;
        if (sw_ev) sw_pend <= 1'b1;
        if (r_ev) r_pend <= 1'b1;
        if (m_ev) m_pend <= 1'b1;
        if (w_ev) w_pend <= 1'b1;
        if (ui_rst) begin
            st <= S_IDLE; en <= 1'b0; owns <= 1'b0;
`ifdef DDRX_MUT_SWALLOW
            // the control (the old behaviour): a request that comes while the controller is in reset is forgotten, its requester waits for ever
            r_pend <= 1'b0; m_pend <= 1'b0; w_pend <= 1'b0; s_pend <= 1'b0; sw_pend <= 1'b0;
            rs <= {3{r_req_tgl}}; ms <= {3{m_req_tgl}}; ws <= {3{w_req_tgl}}; ss <= {3{s_req_tgl}}; sws <= {3{sw_req_tgl}};
`else
            // A request is a toggle and it is served when the done toggle follows. The reset keeps what came in meanwhile (the synchronisers run on, the
            // `pend` flags are not cleared): c64scpu_ramd starts its sweep with the first clock after configuration, before the controller is calibrated,
            // and has no timeout. A command that was open when the reset came is asked again; one that is finished (S_GAP) is not.
            if (st == S_ISSUE || st == S_WAIT)
                case (who) 3'd0: r_pend <= 1'b1; 3'd1: m_pend <= 1'b1; 3'd3: s_pend <= 1'b1; 3'd4: sw_pend <= 1'b1; default: w_pend <= 1'b1; endcase
`endif
        end else case (st)
            S_IDLE: begin
                if (sw_pend) begin
                    who <= 3'd4; sw_pend <= 1'b0;
                    cmd <= 3'b000; en <= 1'b1; owns <= 1'b1;
                    addr  <= SC_BASE_W + {2'd0, sw_req_addr[24:4], 3'b000};
                    wdata <= {16{sw_req_data}};
                    wmask <= ~(16'h0001 << sw_req_addr[3:0]);
                    st <= S_ISSUE;
                end else if (w_pend) begin
                    who <= 3'd2; w_pend <= 1'b0;
                    cmd <= 3'b000; en <= 1'b1; owns <= 1'b1;
                    addr  <= RAM_BASE_W + {7'd0, w_req_addr[16:4], 3'b000};
                    wdata <= {16{w_req_data}};
                    wmask <= ~(16'h0001 << w_req_addr[3:0]);
                    st <= S_ISSUE;
                end else if (m_pend) begin
                    who <= 3'd1; m_pend <= 1'b0;
                    cmd <= 3'b001; en <= 1'b1; owns <= 1'b1;
                    addr <= RAM_BASE_W + {11'd0, m_req_line, 3'b000};
                    wmask <= 16'hFFFF;
                    st <= S_ISSUE;
                end else if (s_pend) begin
                    who <= 3'd3; s_pend <= 1'b0;
                    cmd <= 3'b001; en <= 1'b1; owns <= 1'b1;
                    addr <= SC_BASE_W + {2'd0, s_req_line, 3'b000};
                    wmask <= 16'hFFFF;
                    st <= S_ISSUE;
                end else if (r_pend) begin
                    who <= 3'd0; r_pend <= 1'b0;
                    cmd <= 3'b001; en <= 1'b1; owns <= 1'b1;
                    addr <= ROM_BASE_W + {11'd0, r_req_line, 3'b000};
                    wmask <= 16'hFFFF;
                    st <= S_ISSUE;
                end
            end
            S_ISSUE: if (rdy) begin                  // en && rdy: taken in this clock
                en <= 1'b0; owns <= 1'b0;
                if (who == 3'd2) begin
                    w_done_tgl <= ~w_done_tgl;
                    st <= S_IDLE;
                end else if (who == 3'd4) begin
                    sw_done_tgl <= ~sw_done_tgl;
                    st <= S_IDLE;
                end else
                    st <= S_WAIT;
            end
            S_WAIT: if (rd_valid) begin
                fill <= rd_data;
                if (who == 3'd0) r_done_tgl <= ~r_done_tgl; else if (who == 3'd3) s_done_tgl <= ~s_done_tgl; else m_done_tgl <= ~m_done_tgl;
                gap <= 4'd0; st <= S_GAP;
            end
            default: begin                           // S_GAP: a result is not overwritten before the core side has copied it
                gap <= gap + 4'd1;
                if (gap == 4'd7) st <= S_IDLE;
            end
        endcase
    end
endmodule
