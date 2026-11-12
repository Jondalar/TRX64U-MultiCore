// trx_engine.v alone (iverilog, run_f4.sh): the drain's F4 (T20 §16.6).
// Every state-bus word the capture sequencer reads has $F3 in its high byte,
// so every word record of a chip block has $F3 on top (as the 6510's S = $F3
// at BASIC READY makes record 0's). A frame's stream 2 must be exactly
// F1 F2 F3 F4, then B1, the 29 word records, B2: one F4 per marker, right
// after its F3, none inside the block. The F4's stream 0 count is the
// number of C64 RAM writes made before the marker's clock.
`timescale 1ns / 1ps
module tb_eng_f4;
    reg clk = 0, ui_clk = 0;
    always #15.859 clk = ~clk;              // 31.5 MHz
    always #5 ui_clk = ~ui_clk;             // 100 MHz

    reg         r_we = 0;
    reg   [9:0] r_addr = 0;
    reg  [31:0] r_wdata = 0;
    reg         mk = 0, mk_cap = 0;
    reg         wa_we = 0;
    reg  [15:0] wa_addr = 0;
    wire        sb_req;
    wire [11:0] sb_addr;
    reg  [31:0] sb_rdata = 0;
    reg  [11:0] sb_q1 = 0;
    wire [26:0] e_addr;
    wire  [2:0] e_cmd;
    wire        e_en, e_wdf_wren;
    wire [127:0] e_wdf_data;
    wire [15:0] e_wdf_mask;

    trx_engine eng (
        .clk(clk), .r_we(r_we), .r_re(1'b0), .r_addr(r_addr), .r_wdata(r_wdata), .r_rdata(),
        .log_on(), .seq_busy(), .mk(mk), .mk_snap(1'b1), .mk_cap(mk_cap), .mk_skip(1'b0),
        .mk_intg(1'b0), .reu_busy(1'b0), .mk_x(10'd100), .mk_line(9'd16), .phi2_tick(1'b1),
        .dbg_put(1'b0), .dbg_rec(64'd0), .cap_gen(16'h0001),
        .tr_ib(1'b0), .tr_pc(16'd0), .tr_op(8'd0), .tr_b1(8'd0), .tr_b2(8'd0), .tr_regs(40'd0),
        .sb_req(sb_req), .sb_addr(sb_addr), .sb_gnt(sb_req), .sb_rdata(sb_rdata),
        .wa_we(wa_we), .wa_addr(wa_addr), .wa_new(8'h55), .wa_old(8'hAA),
        .wb_we(1'b0), .wb_addr(16'd0), .wb_new(8'd0), .wb_old(8'd0),
        .wc_we(1'b0), .wc_addr(10'd0), .wc_new(4'd0), .wc_old(4'd0),
        .wd_we(1'b0), .wd_addr(10'd0), .wd_new(4'd0), .wd_old(4'd0),
        .wk_we(1'b0), .wk_addr(15'd0), .wk_new(8'd0), .cart_rbw(),
        .eng_tgl(), .gap_tgl(),
        .ui_clk(ui_clk), .ui_rst(1'b0), .e_addr(e_addr), .e_cmd(e_cmd), .e_en(e_en), .e_rdy(1'b1),
        .e_wdf_data(e_wdf_data), .e_wdf_mask(e_wdf_mask), .e_wdf_wren(e_wdf_wren),
        .e_rd_data(128'd0), .e_rd_data_valid(1'b0),
        .ob_v(1'b0), .ob_addr(15'd0), .ob_old(8'd0)
    );

    // the state bus: {$F3, address}, two clocks after the address
    always @(posedge clk) begin
        sb_q1 <= sb_addr;
        sb_rdata <= {8'hF3, 12'd0, sb_q1};
    end

    // the drain's writes: stream 2 lies at 0x5C00000-0x5FFFFFF (reset value)
    reg  [63:0] s2 [0:1023];
    integer     n2 = 0, n0 = 0;
    wire [26:0] byte_a = {e_addr[25:3], 4'd0};
    always @(posedge ui_clk)
        if (e_en && e_wdf_wren && e_cmd == 3'd0) begin
            if (byte_a >= 27'h5C00000 && byte_a < 27'h6000000) begin
                s2[n2] = e_wdf_mask[7:0] == 8'h00 ? e_wdf_data[63:0] : e_wdf_data[127:64];
                n2 = n2 + 1;
            end else if (byte_a >= 27'h7000000)
                n0 = n0 + 1;
        end

    task wr(input [9:0] a, input [31:0] d);
        begin
            @(posedge clk) #1; r_we = 1; r_addr = a; r_wdata = d;
            @(posedge clk) #1; r_we = 0;
        end
    endtask

    localparam FRAMES = 4, NREC = 29;       // the reset table's 58 words
    integer f, k, i, fails = 0, writes = 0;
    integer at_mk [0:FRAMES - 1];
    initial begin
        #500;
        wr(10'h000, 32'h101);               // restart, on (C64 RAM writes)
        for (f = 0; f < FRAMES; f = f + 1) begin
            // some RAM writes in the frame, then the boundary with a capture
            for (k = 0; k < 5 + f; k = k + 1) begin
                @(posedge clk) #1; wa_we = 1; wa_addr = 16'h0400 + k;
                @(posedge clk) #1; wa_we = 0;
                writes = writes + 1;
                repeat (3) @(posedge clk);
            end
            @(posedge clk) #1; mk = 1; mk_cap = 1; at_mk[f] = writes;
            @(posedge clk) #1; mk = 0; mk_cap = 0;
            repeat (1500) @(posedge clk);   // the block is copied and drained
        end
        repeat (200) @(posedge ui_clk);

        // check the stream
        i = 0;
        $display("stream 2: %0d records, stream 0: %0d (%0d writes)", n2, n0, writes);
        for (f = 0; f < FRAMES; f = f + 1) begin
            if (i + 4 + NREC + 2 > n2) begin
                fails = fails + 1; $display("FAIL: frame %0d: stream 2 ends at %0d", f, n2);
            end else begin
                if (s2[i][63:56] != 8'hF1 || s2[i + 1][63:56] != 8'hF2 || s2[i + 2][63:56] != 8'hF3 ||
                    s2[i + 3][63:56] != 8'hF4) begin
                    fails = fails + 1;
                    $display("FAIL: frame %0d: no F1-F4 at %0d: %h %h %h %h", f, i, s2[i], s2[i + 1],
                             s2[i + 2], s2[i + 3]);
                end
                if (s2[i + 3][27:0] != at_mk[f]) begin
                    fails = fails + 1;
                    $display("FAIL: frame %0d: F4 stream 0 count %0d, %0d writes before the marker",
                             f, s2[i + 3][27:0], at_mk[f]);
                end
                if (s2[i + 4][63:56] != 8'hB1) begin
                    fails = fails + 1; $display("FAIL: frame %0d: no B1 at %0d: %h", f, i + 4, s2[i + 4]);
                end
                for (k = 0; k < NREC; k++)
                    if (s2[i + 5 + k][63:56] != 8'hF3 && !(k == NREC - 1 && s2[i + 5 + k][63:32] == 0)) begin
                        fails = fails + 1;
                        $display("FAIL: frame %0d: word record %0d: %h", f, k, s2[i + 5 + k]);
                    end
                if (s2[i + 5 + NREC][63:56] != 8'hB2) begin
                    fails = fails + 1;
                    $display("FAIL: frame %0d: no B2 at %0d: %h", f, i + 5 + NREC, s2[i + 5 + NREC]);
                end
                $display("frame %0d: F4 {s1 %0d, s0 %0d}, block %0d word records, tail %h", f,
                         s2[i + 3][55:28], s2[i + 3][27:0], NREC, s2[i + 5 + NREC]);
            end
            i = i + 4 + NREC + 2;
        end
        if (n2 != i) begin
            fails = fails + 1; $display("FAIL: %0d stream 2 records, %0d expected", n2, i);
        end
        for (k = 0; k < n2; k++)
            if (s2[k][63:56] == 8'hF4 && (k < 3 || s2[k - 3][63:56] != 8'hF1)) begin
                fails = fails + 1; $display("FAIL: an F4 at %0d not after a marker", k);
            end
        if (fails) $display("%0d FAILED", fails); else $display("PASS");
        $finish;
    end
endmodule
