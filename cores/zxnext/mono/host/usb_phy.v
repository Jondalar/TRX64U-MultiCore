// usb_phy: the board side of the USB host (Gideon's usb_host_nano in
// ultimate_sys): ULPI pins, the PHY's and the hub's 24 MHz references, the
// two resets, and a meter for the PHY's 60 MHz clock.
//
//  - ULPI_CLOCK (V4, MRCC) is the PHY's output; it clocks the link through a
//    BUFG. The output flops (data, STP) sit in the IOBs (usb_pins.xdc);
//    the data lines turn around with DIR, as in Gideon's ulpi_bus.
//  - ULPI_REFCLK and HUB_CLOCK: 24 MHz from a PLL on the 50 MHz RMII clock,
//    forwarded through ODDRs.
//  - ULPI_RESET is high while the design runs (Gideon's design drives it high,
//    JTAG SAMPLE 2026-09); low during the CPU reset. ctl[0] inverts it, ctl[1]
//    adds the app's U2PIO_ULPI_RESET (for bring-up over JTAG).
//  - khz: ULPI_CLOCK in kHz, counted over 16 ms of cpu_clk (100 MHz) in
//    edges of a divide-by-16; dir_edges: rising DIR edges (PHY talking).
module usb_phy #(
    parameter integer GATE = 1_600_000      // cpu_clk cycles in 16 ms
) (
    input  wire       clk50,                // BUFG'd RMII clock, PLL reference
    input  wire       pll_rst,
    input  wire       cpu_clk,
    input  wire       cpu_rst,

    input  wire       ULPI_CLOCK,
    input  wire       ULPI_NXT,
    input  wire       ULPI_DIR,
    output wire       ULPI_STP,
    inout  wire [7:0] ULPI_DATA,
    output wire       ULPI_RESET,
    output wire       ULPI_REFCLK,
    output wire       HUB_RESETn,
    output wire       HUB_CLOCK,

    output wire       ulpi_clk,
    output wire       ulpi_nxt,
    output wire       ulpi_dir,
    output wire [7:0] ulpi_data_i,
    input  wire       ulpi_stp,
    input  wire [7:0] ulpi_data_o,
    input  wire       ulpi_data_t,          // 1 = drive
    input  wire       ulpi_reset_req,       // cpu_clk
    input  wire       hub_reset_n,          // cpu_clk

    input  wire [1:0] ctl,                  // any clock, static
    output reg [31:0] khz = 32'd0,          // cpu_clk
    output wire [15:0] dir_edges            // cpu_clk
);
    // ---- ULPI ------------------------------------------------------------------
    BUFG bg_ulpi (.I(ULPI_CLOCK), .O(ulpi_clk));
    genvar i;
    generate for (i = 0; i < 8; i = i + 1) begin : g_data
        IOBUF b_data (.IO(ULPI_DATA[i]), .I(ulpi_data_o[i]), .T(~ulpi_data_t), .O(ulpi_data_i[i]));
    end endgenerate
    assign ulpi_nxt = ULPI_NXT;
    assign ulpi_dir = ULPI_DIR;
    assign ULPI_STP = ulpi_stp;

    // ---- 24 MHz references ----------------------------------------------------------
    wire fb, ref24_pll, ref24;
    PLLE2_BASE #(
        .CLKIN1_PERIOD(20.0), .DIVCLK_DIVIDE(1), .CLKFBOUT_MULT(24),   // VCO 1200 MHz
        .CLKOUT0_DIVIDE(50)                                            // 24 MHz
    ) pll_usb (
        .CLKIN1(clk50), .CLKFBIN(fb), .CLKFBOUT(fb), .CLKOUT0(ref24_pll),
        .CLKOUT1(), .CLKOUT2(), .CLKOUT3(), .CLKOUT4(), .CLKOUT5(),
        .LOCKED(), .RST(pll_rst), .PWRDWN(1'b0)
    );
    BUFG bg_ref24 (.I(ref24_pll), .O(ref24));
    ODDR #(.DDR_CLK_EDGE("SAME_EDGE")) fwd_phy (.Q(ULPI_REFCLK), .C(ref24), .CE(1'b1),
        .D1(1'b1), .D2(1'b0), .R(1'b0), .S(1'b0));
    ODDR #(.DDR_CLK_EDGE("SAME_EDGE")) fwd_hub (.Q(HUB_CLOCK), .C(ref24), .CE(1'b1),
        .D1(1'b1), .D2(1'b0), .R(1'b0), .S(1'b0));

    // ---- resets ----------------------------------------------------------------------
    (* ASYNC_REG = "TRUE" *) reg [1:0] ctl_s1 = 2'b00, ctl_s2 = 2'b00;
    reg phy_rst = 1'b1, hub_n = 1'b0;
    always @(posedge cpu_clk) begin
        ctl_s1  <= ctl;
        ctl_s2  <= ctl_s1;
        phy_rst <= cpu_rst | (ctl_s2[1] & ulpi_reset_req);
        hub_n   <= hub_reset_n;
    end
    assign ULPI_RESET = phy_rst ^ ~ctl_s2[0];
    assign HUB_RESETn = hub_n;

    // ---- meter -------------------------------------------------------------------------
    reg [3:0]  div = 4'd0;
    reg [15:0] dcnt = 16'd0, dgray = 16'd0;
    reg        dir_d = 1'b0;
    always @(posedge ulpi_clk) begin
        div   <= div + 4'd1;
        dir_d <= ulpi_dir;
        if (ulpi_dir && !dir_d) dcnt <= dcnt + 16'd1;
        dgray <= dcnt ^ (dcnt >> 1);
    end
    (* ASYNC_REG = "TRUE" *) reg [2:0]  div_s = 3'b000;
    (* ASYNC_REG = "TRUE" *) reg [15:0] dg_s1 = 16'd0, dg_s2 = 16'd0;
    reg [20:0] gate = 21'd0;
    reg [31:0] edges = 32'd0;
    always @(posedge cpu_clk) begin
        div_s <= {div_s[1:0], div[3]};
        dg_s1 <= dgray;
        dg_s2 <= dg_s1;
        if (gate == GATE - 1) begin
            gate  <= 21'd0;
            khz   <= edges + (div_s[2] & ~div_s[1]);
            edges <= 32'd0;
        end else begin
            gate  <= gate + 21'd1;
            edges <= edges + (div_s[2] & ~div_s[1]);
        end
    end
    // gray to binary
    generate for (i = 0; i < 16; i = i + 1) begin : g_ungray
        assign dir_edges[i] = ^dg_s2[15:i];
    end endgenerate
endmodule
