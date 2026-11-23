// T113 measurement skeleton: the VIC-IIe's two extra registers ($D02F, $D030) and the VDC at $D600/$D601.
//   `VDC_WHOLE : the whole MiSTer VDC (vdc/vdc_top.sv with its patches, PROVENANCE) incl. drawing -> pixel stream (variant 1)
//   `VDC_SPLIT : vdc/vdc_cpu.v, the CPU-visible part only; the application draws (variant 2)
//   neither    : no VDC (the baseline)
`timescale 1ns / 1ps
module c128_x (
    input  wire        clk, rst, warm, bus_en,
    input  wire        vdc_cs, vdc_we, vdc_rs,
    input  wire  [7:0] vdc_din,
    output wire  [7:0] vdc_dout,
    input  wire        vicie_cs, vicie_we, vicie_a0,
    input  wire  [7:0] vicie_din,
    output wire  [7:0] vicie_dout,
    output wire  [2:0] k_sel,
    output wire        fast,
    output wire  [3:0] vdc_rgbi,
    output wire        vdc_ce, vdc_hb, vdc_vb,
    output wire  [1:0] vdc_il,                   // T113 D11: {R8[1:0] == 3, the field being drawn}
    input  wire        vd_clk,
    input  wire [15:0] vd_addr,
    input  wire        vd_we,
    input  wire  [7:0] vd_wdata,
    output wire  [7:0] vd_rdata,
    // T257: the read-back of the VDC's registers and VRAM without side effects (VDC_WHOLE only), a toggle handshake from cpu_clk (c128_regs.v):
    //   vk_req = {request toggle, 1 = VRAM / 0 = register, 16 bit address or register number}, stable from the toggle on until the answer;
    //   vk_ans = {answer toggle, the byte}
    input  wire [17:0] vk_req,
    output wire  [8:0] vk_ans
);
    // $D02F: bits 2:0 select the extra keyboard lines K0-K2 (low = selected), reads back 1 in bits 7:3 .
    // $D030: bit 0 = 2 MHz, bit 1 = test; reads back 1 in bits 7:2. The 2 MHz clock itself (the 8502's enable at twice the
    // rate, the VIC blanked while it is on) is not in this skeleton: `fast` is the request.
    reg [2:0] d02f = 3'b111;
    reg [1:0] d030 = 2'b00;
    always @(posedge clk) begin
        if (rst) begin d02f <= 3'b111; d030 <= 2'b00; end
        else if (vicie_cs && vicie_we && bus_en) begin
            if (vicie_a0) d02f <= vicie_din[2:0]; else d030 <= vicie_din[1:0];       // $D02F is the odd address, $D030 the even one
        end
    end
    assign vicie_dout = vicie_a0 ? {5'b11111, d02f} : {6'b111111, d030};
    assign k_sel = d02f;
    assign fast  = d030[0];

`ifdef VDC64
 `define VCPU_BITS 16
`else
 `define VCPU_BITS 14
`endif
`ifdef VDC_WHOLE
    wire act_h, act_v;
    wire [7:0] vk_ram_q, vk_reg_q;
    (* ASYNC_REG = "TRUE" *) reg [2:0] vk_s = 3'b000;
    reg [15:0] vk_a = 16'd0;
`ifdef VDC64
    localparam VBITS = 16, V64 = 1'b1;     // 64 KiB of VRAM (the real chip's maximum)
`else
    localparam VBITS = 14, V64 = 1'b0;     // 16 KiB
`endif
    vdc_top #(.RAM_ADDR_BITS(VBITS)) vdc (
        .version(1'b0), .ram64k(V64), .initRam(1'b1), .debug(1'b0),
        .clk(clk), .enableBus(bus_en), .reset(rst), .ctl_reset(warm), .init(1'b0),
        .cs(vdc_cs), .rs(vdc_rs), .we(vdc_we), .db_in(vdc_din), .db_out(vdc_dout),
        .lp_n(1'b1), .vsync(), .hsync(), .pixel_ce(vdc_ce), .active_h(act_h), .active_v(act_v), .rgbi(vdc_rgbi), .ilace(vdc_il),
        .dbg_addr(vk_a), .dbg_ram(vk_ram_q), .dbg_reg(vk_reg_q));
    // The request crosses with the toggle (three flip-flops, the address is stable by then), the engine latches the address, waits for the block RAM and the
    // register mux (one clock after the address) and answers with the byte and a toggle; nothing here touches the CPU's side of the VDC.
    reg        vk_isram = 1'b0, vk_tgl = 1'b0;
    reg  [1:0] vk_st = 2'd0;
    reg  [7:0] vk_d = 8'd0;
    always @(posedge clk) begin
        vk_s <= {vk_s[1:0], vk_req[17]};
        case (vk_st)
        2'd0: if (vk_s[2] != vk_s[1]) begin vk_a <= vk_req[15:0]; vk_isram <= vk_req[16]; vk_st <= 2'd1; end
        2'd1: vk_st <= 2'd2;
        default: begin vk_d <= vk_isram ? vk_ram_q : vk_reg_q; vk_tgl <= ~vk_tgl; vk_st <= 2'd0; end
        endcase
    end
    assign vk_ans = {vk_tgl, vk_d};
    assign vdc_hb = !act_h;
    assign vdc_vb = !act_v;
    assign vd_rdata = 8'd0;
`elsif VDC_SPLIT
    vdc_cpu #(.RAM_BITS(`VCPU_BITS)) vdc (
        .clk(clk), .rst(rst), .bus_en(bus_en), .cs(vdc_cs), .rs(vdc_rs), .we(vdc_we), .db_in(vdc_din), .db_out(vdc_dout),
        .vd_clk(vd_clk), .vd_addr(vd_addr), .vd_we(vd_we), .vd_wdata(vd_wdata), .vd_rdata(vd_rdata));
    assign vdc_rgbi = 4'd0; assign vdc_ce = 1'b0; assign vdc_hb = 1'b1; assign vdc_vb = 1'b1; assign vdc_il = 2'b00;
    assign vk_ans = 9'd0;
`else
    assign vdc_dout = 8'hFF; assign vd_rdata = 8'd0; assign vk_ans = 9'd0;
    assign vdc_rgbi = 4'd0; assign vdc_ce = 1'b0; assign vdc_hb = 1'b1; assign vdc_vb = 1'b1; assign vdc_il = 2'b00;
`endif
endmodule
