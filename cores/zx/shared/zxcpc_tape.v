// T91 D9 (shared with the CPC core, T92): the tape pulse engine. The app converts a whole tape (TAP/TZX, CDT) into a stream of 16 bit
// little-endian words in the DDR2 and this engine plays it as a level on `level`, one tape-clock unit per `tick`:
//   0x0001-0xFFFD  the level holds for that many ticks, then it toggles
//   0xFFFF         a gap: the next two words (low, high) are a 32 bit tick count; the level holds and does NOT toggle after it
//   0xFFFE         "stop the tape": the engine stops (playing = 0, stopmark = 1), the position is after the marker; play resumes it
//   0x0000         end of the tape (eof = 1, playing = 0)
// The level starts low. A pulse of 0 is not a pulse (it is the end marker); the app writes pauses as gaps. The block index of the
// tape (pause blocks, block-wise rewind) is the app's: it seeks by writing a word position. Stream rule: the app keeps pulses at least
// 16 ticks long (a turbo loader is out of scope, T91 Q5), so a 16 byte line (8 words) lasts longer than the DDR2 needs to bring the
// next one; `underrun` counts the times the engine had to wait for a word it needed at once.
// Reads go through the host's real-time port c_* (ui_clk), one 128 bit line at a time, one line ahead of the position. The sys_clk
// side requests by toggle and the ui_clk side answers by toggle with the line held stable (the circuit of nes_mem.v's engine).
// Control values (play, base, seek) come from the register block through the caller's synchronisers and are quasi static.
// Written for this project (TREX), no upstream code.
`timescale 1ns / 1ps
module zxcpc_tape (
    input  wire        clk,                     // sys_clk
    input  wire        rst,
    input  wire        hold,                    // Freeze: nothing moves, the position stands
    input  wire        tick,                    // one tape-clock unit (the machine's T-state clock), already gated by hold
    input  wire        play,
    input  wire [31:0] base,                    // DDR2 byte address of word 0 (16 byte aligned; bits 3:0 ignored)
    input  wire        seek_tgl,                // a change loads `seek_pos` into the position
    input  wire [24:0] seek_pos,                // word index
    output reg         level = 1'b0,
    output wire        playing,
    output reg         eof = 1'b0,
    output reg         stopmark = 1'b0,
    output wire [24:0] pos_o,
    output reg  [7:0]  underrun = 8'd0,
    output reg         ddr_err = 1'b0,
    // the host's real-time DDR2 port c_* (ui_clk)
    input  wire        ui_clk,
    input  wire        ui_rst,
    output reg  [26:0] c_addr = 27'd0,
    output reg  [2:0]  c_cmd = 3'd0,
    output wire        c_en,
    input  wire        c_rdy,
    input  wire [127:0] c_rd_data,
    input  wire        c_rd_valid,
    output wire        ui_busy                  // ui_clk: a line transaction is under way (a second client of the port waits for it, cpc_ddr_mux)
);
    // ------------------------------------------------------------------ position, lines
    reg [24:0] pos = 25'd0;                     // word index of the next word to read
    assign pos_o = pos;
    reg [127:0] cur = 128'd0, nxt = 128'd0;
    reg [21:0] cur_tag = 22'd0, nxt_tag = 22'd0;
    reg        cur_v = 1'b0, nxt_v = 1'b0;
    wire [21:0] pl = pos[24:3];
    reg        rq_tgl = 1'b0, busy = 1'b0;
    reg [21:0] rq_tag = 22'd0;
    reg [26:0] rq_addr = 27'd0;
    (* ASYNC_REG = "TRUE" *) reg [2:0] dn_s = 3'b000;
    reg        dn_tgl = 1'b0;                   // ui side, declared below; read through the synchroniser
    reg [127:0] ui_line = 128'd0;
    reg [15:0] to_cnt = 16'd0;
    reg        seek_q = 1'b0;

    wire [31:0] lb_cur = {base[31:4], 4'b0000} + {6'd0, pl, 4'b0000};            // byte address of the line of the position
    wire [31:0] lb_nxt = {base[31:4], 4'b0000} + {6'd0, pl + 22'd1, 4'b0000};
    wire       cur_hit = cur_v && (cur_tag == pl);
    wire       nxt_hit = nxt_v && (nxt_tag == pl);
    wire [15:0] w_cur = cur[16 * pos[2:0] +: 16];

    // ------------------------------------------------------------------ the word machine
    localparam [2:0] S_NEXT = 3'd0, S_CNT = 3'd1, S_ESC1 = 3'd2, S_ESC2 = 3'd3, S_GAP = 3'd4, S_END = 3'd5;
    reg [2:0]  st = S_NEXT;
    reg [15:0] cnt = 16'd0;
    reg [31:0] gap = 32'd0;
    reg [15:0] lo = 16'd0;
    reg        run = 1'b0;                      // play, until a stop marker or the end clears it
    assign playing = run & (st != S_END) & ~hold;
    reg        wait_f = 1'b0;                   // waiting for a word of a line that is not there (counted once per wait)
    reg        play_q = 1'b0;

    always @(posedge clk) begin
        dn_s <= {dn_s[1:0], dn_tgl};
        seek_q <= seek_tgl;
        if (rst) begin
            st <= S_NEXT; run <= 1'b0; play_q <= 1'b0; wait_f <= 1'b0; level <= 1'b0; eof <= 1'b0; stopmark <= 1'b0; cur_v <= 1'b0; nxt_v <= 1'b0; busy <= 1'b0;
            pos <= seek_pos; ddr_err <= 1'b0; underrun <= 8'd0; to_cnt <= 16'd0;
        end else begin
            // ---- the line fetch (request/answer)
            if (busy) begin
                to_cnt <= to_cnt + 16'd1;
                if (to_cnt == 16'hFFFF) ddr_err <= 1'b1;
                if (dn_s[2] != dn_s[1]) begin
                    busy <= 1'b0; to_cnt <= 16'd0;
                    nxt <= ui_line; nxt_tag <= rq_tag; nxt_v <= 1'b1;
                end
            end else if (run) begin
                if (!cur_hit && !nxt_hit) begin
                    busy <= 1'b1; rq_tag <= pl; rq_tgl <= ~rq_tgl;
                    rq_addr <= lb_cur[27:1];
                end else if (cur_hit && !(nxt_v && nxt_tag == pl + 22'd1)) begin
                    busy <= 1'b1; rq_tag <= pl + 22'd1; rq_tgl <= ~rq_tgl;
                    rq_addr <= lb_nxt[27:1];
                end
            end
            if (!cur_hit && nxt_hit) begin cur <= nxt; cur_tag <= nxt_tag; cur_v <= 1'b1; end
            // ---- the control
            if (seek_q != seek_tgl) begin
                pos <= seek_pos; st <= S_NEXT; level <= 1'b0; eof <= 1'b0; stopmark <= 1'b0; cur_v <= 1'b0; nxt_v <= 1'b0;
            end else if (!hold) begin
                play_q <= play;
                if (play && !play_q && st != S_END) begin run <= 1'b1; stopmark <= 1'b0; end      // a rising edge starts it (a stop marker does not restart by itself)
                if (!play) run <= 1'b0;
                case (st)
                S_NEXT: if (run) begin
                    if (cur_hit) begin
                        wait_f <= 1'b0;
                        pos <= pos + 25'd1;
                        if (w_cur == 16'h0000) begin st <= S_END; eof <= 1'b1; run <= 1'b0; end
                        else if (w_cur == 16'hFFFE) begin stopmark <= 1'b1; run <= 1'b0; end
                        else if (w_cur == 16'hFFFF) st <= S_ESC1;
                        else begin cnt <= w_cur; st <= S_CNT; end
                    end else if (!wait_f && cur_v) begin
                        wait_f <= 1'b1;
                        if (underrun != 8'hFF) underrun <= underrun + 8'd1;
                    end
                end
                S_CNT: if (tick && run) begin
                    if (cnt == 16'd1) begin level <= ~level; st <= S_NEXT; end
                    else cnt <= cnt - 16'd1;
                end
                S_ESC1: if (cur_hit) begin lo <= w_cur; pos <= pos + 25'd1; st <= S_ESC2; end
                S_ESC2: if (cur_hit) begin gap <= {w_cur, lo}; pos <= pos + 25'd1; st <= S_GAP; end
                S_GAP: if (tick && run) begin
                    if (gap <= 32'd1) st <= S_NEXT; else gap <= gap - 32'd1;
                end
                S_END: ;
                default: st <= S_NEXT;
                endcase
            end
        end
    end

    // ------------------------------------------------------------------ the ui side
    localparam [1:0] U_IDLE = 2'd0, U_ISSUE = 2'd1, U_WAITR = 2'd2;
    reg [1:0]  us = U_IDLE;
    (* ASYNC_REG = "TRUE" *) reg [2:0] rq_s = 3'b000;
    reg [26:0] u_addr = 27'd0;
    reg        c_en_i = 1'b0;
    always @(posedge ui_clk) begin
        rq_s <= {rq_s[1:0], rq_tgl};
        if (ui_rst) begin
            us <= U_IDLE; c_en_i <= 1'b0;
        end else begin
            case (us)
            U_IDLE: if (rq_s[2] != rq_s[1]) begin u_addr <= rq_addr; us <= U_ISSUE; end
            U_ISSUE: begin
                c_en_i <= 1'b1;
                c_addr <= u_addr;
                c_cmd <= 3'b001;
                if (c_en_i && c_rdy) begin c_en_i <= 1'b0; us <= U_WAITR; end
            end
            U_WAITR: if (c_rd_valid) begin ui_line <= c_rd_data; dn_tgl <= ~dn_tgl; us <= U_IDLE; end
            default: us <= U_IDLE;
            endcase
        end
    end
    assign c_en = c_en_i;
    assign ui_busy = (us != U_IDLE);
endmodule
