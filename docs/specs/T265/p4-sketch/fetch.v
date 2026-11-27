// T265 P4 SKETCH: pix_layer's DDR2 fetcher (ui_clk), as it is (fetch_old: host/pix_layer.v, the "DDR2 side" block) and with a second
// requester for the RGB side (fetch_new). Only to measure the difference; the still word path is kept in both.
module fetch_old (
    input  wire ui_clk, ui_rst,
    input  wire rq_tgl, input wire [31:0] la, input wire [7:0] rq_n, input wire [1:0] rq_slot, input wire rq_sm, input wire [11:0] rq_tag,
    input  wire st_tgl, input wire [127:0] st_d, input wire [26:4] st_a,
    output wire [26:0] v_addr, output wire [2:0] v_cmd, output wire v_en, input wire v_rdy,
    output wire [127:0] v_wdf_data, output wire v_wdf_wren, input wire [127:0] v_rd_data, input wire v_rd_data_valid, output reg v_urgent = 1'b0,
    output reg lb_we, output reg [8:0] lb_wa, output reg [127:0] lb_wd,
    output reg done_tgl = 1'b0, output reg [1:0] done_slot = 2'd0, output reg [11:0] done_tag = 12'hFFF, output reg st_ack = 1'b0
);
    (* ASYNC_REG = "TRUE" *) reg [2:0] u_rq = 3'd0;
    (* ASYNC_REG = "TRUE" *) reg [1:0] u_st = 2'd0;
    reg nxt = 1'b0, run = 1'b0;
    reg [31:0] n_addr; reg [7:0] n_n; reg [1:0] n_slot; reg n_sm; reg [11:0] n_tag;
    reg [26:0] rd_a; reg [7:0] n_left, rcv_left; reg [4:0] out = 5'd0; reg [7:0] wa; reg [1:0] cur_slot; reg cur_sm; reg [9:0] age; reg [11:0] cur_tag;
    wire st_pend = u_st[1] != st_ack;
    wire issue = run && !nxt && n_left != 8'd0;
    assign v_en = st_pend || issue;
    assign v_cmd = st_pend ? 3'b000 : 3'b001;
    assign v_addr = st_pend ? {1'b0, st_a, 3'b000} : rd_a;
    assign v_wdf_data = st_d;
    assign v_wdf_wren = st_pend;
    wire rd_go = v_rdy && !st_pend;
    always @(posedge ui_clk) begin
        u_rq <= {u_rq[1:0], rq_tgl};
        u_st <= {u_st[0], st_tgl};
        lb_we <= 1'b0;
        if (u_rq[2] != u_rq[1]) begin nxt <= 1'b1; run <= 1'b0; n_addr <= la; n_n <= rq_n; n_slot <= rq_slot; n_sm <= rq_sm; n_tag <= rq_tag; end
        out <= out + rd_go - v_rd_data_valid;
        if (v_rdy && st_pend) st_ack <= ~st_ack;
        if (rd_go) begin rd_a <= rd_a + 27'd8; n_left <= n_left - 8'd1; end
        if (v_rd_data_valid) begin
            if (!(cur_sm && wa[7])) begin lb_we <= 1'b1; lb_wa <= cur_sm ? {cur_slot, wa[6:0]} : {cur_slot[0], wa[7:0]}; lb_wd <= v_rd_data; end
            wa <= wa + 8'd1;
            if (run) begin
                rcv_left <= rcv_left - 8'd1;
                if (rcv_left == 8'd1) begin run <= 1'b0; done_tag <= cur_tag; done_slot <= cur_slot; done_tgl <= ~done_tgl; end
            end
        end
        if (nxt && u_rq[2] == u_rq[1] && out == 5'd0 && !v_rd_data_valid) begin
            nxt <= 1'b0; run <= n_n != 8'd0; rd_a <= {1'b0, n_addr[26:4], 3'b000}; n_left <= n_n; rcv_left <= n_n; wa <= 8'd0;
            cur_slot <= n_slot; cur_sm <= n_sm; cur_tag <= n_tag;
        end
        if (!run) age <= 10'd0; else if (age != 10'h3FF) age <= age + 10'd1;
        v_urgent <= run && age >= 10'd400;
        if (ui_rst) begin nxt <= 1'b0; run <= 1'b0; out <= 5'd0; end
    end
endmodule

module fetch_new (
    input  wire ui_clk, ui_rst,
    input  wire rq_tgl, input wire [31:0] la, input wire [7:0] rq_n, input wire [1:0] rq_slot, input wire rq_sm, input wire [11:0] rq_tag,
    input  wire st_tgl, input wire [127:0] st_d, input wire [26:4] st_a,
    // the RGB side's request: one or two lines (stride apart) into the pair
    input  wire a_rq_tgl, input wire [26:4] a_rq_addr, input wire [15:0] a_rq_stride, input wire [7:0] a_rq_n, input wire a_rq_two, input wire a_rq_pair,
    output reg a_done_tgl = 1'b0, output reg a_we, output reg [8:0] a_wa,
    output wire [26:0] v_addr, output wire [2:0] v_cmd, output wire v_en, input wire v_rdy,
    output wire [127:0] v_wdf_data, output wire v_wdf_wren, input wire [127:0] v_rd_data, input wire v_rd_data_valid, output reg v_urgent = 1'b0,
    output reg lb_we, output reg [8:0] lb_wa, output reg [127:0] lb_wd,
    output reg done_tgl = 1'b0, output reg [1:0] done_slot = 2'd0, output reg [11:0] done_tag = 12'hFFF, output reg st_ack = 1'b0
);
    (* ASYNC_REG = "TRUE" *) reg [2:0] u_rq = 3'd0, u_aq = 3'd0;
    (* ASYNC_REG = "TRUE" *) reg [1:0] u_st = 2'd0;
    reg nxt = 1'b0, run = 1'b0, a_pend = 1'b0, cur_a = 1'b0, a_second = 1'b0;
    reg [31:0] n_addr; reg [7:0] n_n; reg [1:0] n_slot; reg n_sm; reg [11:0] n_tag;
    reg [26:4] q_addr; reg [15:0] q_stride; reg [7:0] q_n; reg q_two, q_pair;
    reg [26:0] rd_a; reg [7:0] n_left, rcv_left; reg [4:0] out = 5'd0; reg [7:0] wa; reg [1:0] cur_slot; reg cur_sm; reg [9:0] age; reg [11:0] cur_tag;
    wire st_pend = u_st[1] != st_ack;
    wire issue = run && !nxt && n_left != 8'd0;
    assign v_en = st_pend || issue;
    assign v_cmd = st_pend ? 3'b000 : 3'b001;
    assign v_addr = st_pend ? {1'b0, st_a, 3'b000} : rd_a;
    assign v_wdf_data = st_d;
    assign v_wdf_wren = st_pend;
    wire rd_go = v_rdy && !st_pend;
    wire idle = out == 5'd0 && !v_rd_data_valid && !run;
    always @(posedge ui_clk) begin
        u_rq <= {u_rq[1:0], rq_tgl};
        u_aq <= {u_aq[1:0], a_rq_tgl};
        u_st <= {u_st[0], st_tgl};
        lb_we <= 1'b0; a_we <= 1'b0;
        // an HDMI request stops an HDMI fetch in progress (as now), never an RGB one: it waits for it (at most 2 x 128 reads)
        if (u_rq[2] != u_rq[1]) begin nxt <= 1'b1; if (!cur_a) run <= 1'b0; n_addr <= la; n_n <= rq_n; n_slot <= rq_slot; n_sm <= rq_sm; n_tag <= rq_tag; end
        if (u_aq[2] != u_aq[1]) begin a_pend <= 1'b1; q_addr <= a_rq_addr; q_stride <= a_rq_stride; q_n <= a_rq_n; q_two <= a_rq_two; q_pair <= a_rq_pair; end
        out <= out + rd_go - v_rd_data_valid;
        if (v_rdy && st_pend) st_ack <= ~st_ack;
        if (rd_go) begin rd_a <= rd_a + 27'd8; n_left <= n_left - 8'd1; end
        if (v_rd_data_valid) begin
            lb_wd <= v_rd_data;
            if (cur_a) begin a_we <= 1'b1; a_wa <= {q_pair, a_second, wa[6:0]}; end
            else if (!(cur_sm && wa[7])) begin lb_we <= 1'b1; lb_wa <= cur_sm ? {cur_slot, wa[6:0]} : {cur_slot[0], wa[7:0]}; end
            wa <= wa + 8'd1;
            if (run) begin
                rcv_left <= rcv_left - 8'd1;
                if (rcv_left == 8'd1) begin
                    run <= 1'b0;
                    if (!cur_a) begin done_tag <= cur_tag; done_slot <= cur_slot; done_tgl <= ~done_tgl; end
                    else if (!(q_two && !a_second)) a_done_tgl <= ~a_done_tgl;
                end
            end
        end
        if (idle && nxt && u_rq[2] == u_rq[1]) begin
            nxt <= 1'b0; cur_a <= 1'b0; run <= n_n != 8'd0; rd_a <= {1'b0, n_addr[26:4], 3'b000}; n_left <= n_n; rcv_left <= n_n; wa <= 8'd0;
            cur_slot <= n_slot; cur_sm <= n_sm; cur_tag <= n_tag;
        end else if (idle && !nxt && a_pend) begin
            // the first line, then the second one stride on (S = 2)
            cur_a <= 1'b1; run <= 1'b1; wa <= 8'd0; n_left <= q_n; rcv_left <= q_n;
            if (!a_second && cur_a && q_two) begin a_second <= 1'b1; rd_a <= {1'b0, q_addr + {7'd0, q_stride[15:4]}, 3'b000}; a_pend <= 1'b0; end
            else begin a_second <= 1'b0; rd_a <= {1'b0, q_addr, 3'b000}; if (!q_two) a_pend <= 1'b0; end
        end else if (idle) cur_a <= 1'b0;
        if (!run) age <= 10'd0; else if (age != 10'h3FF) age <= age + 10'd1;
        v_urgent <= run && age >= 10'd400;
        if (ui_rst) begin nxt <= 1'b0; run <= 1'b0; out <= 5'd0; a_pend <= 1'b0; cur_a <= 1'b0; end
    end
endmodule
