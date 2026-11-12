// T37 in the c64u core (iverilog, run_sock.sh): the SID sockets on the port's bus (slot_port_c64u.v) and their audio from
// the codec's ADCs (sid_sock_audio_c64u.v).
//
// Port: one C64 cycle is 32 clocks (phi2_p at core tick 0, the CPU half core ticks 1-17, the VIC's from 18); the bench gives
// the core's signals of each tick as c64_top_c64u does: the CPU's address, R/W and the socket decode in the CPU half, the
// VIC's address after it. No cartridge is selected (C64_BUS_EXTERNAL = 0).
// Audio: host/i2s_out.v, the host's transmitter, is the codec's ADC here: the same frame the codec sends (24 bits, left
// while LRCK is low, the MSB one BCLK after the LRCK edge). i2s_out changes its data one MCLK before BCLK falls; the codec
// changes SDO after the falling BCLK, so the model's data is one MCLK and 10 ns later.
`timescale 1ns/1ps
module tb_c64u_sock;
    reg clk = 1'b0;
    always #15.873 clk = ~clk;              // 31.5 MHz (dot4x)

    reg  [4:0] t = 5'd31;                   // the bench's tick: the core's tk
    reg        sk_on1 = 1'b0, sk_on2 = 1'b0;
    reg  [2:0] split = 3'd0;
    reg [15:0] cyc_addr = 16'hD418;
    reg        cyc_we = 1'b0;
    reg  [1:0] cyc_hit = 2'b00;             // the decode's answer for cyc_addr
    reg  [7:0] pin_d = 8'h5A;               // what the chip drives on D in a read
    reg        rst = 1'b1;
    wire       cpu_half = t >= 5'd1 && t <= 5'd17;
    wire [15:0] slot_a;
    wire  [7:0] p_d_o, dout;
    wire        p_d_t, p_ahi_t, p_rw, p_adir, p_aoen, p_ddir, p_doen, p_roml, p_romh, p_io1, p_io2;
    wire        p_phi2, p_dotclk, p_ba, p_rst_o, p_sid1, p_sid2, p_sid_a;
    wire [31:0] status, last;

    slot_port_c64u #(.EN(1)) dut (
        .clk(clk), .rst(rst), .ce(1'b1), .ce_d(1'b1), .halted(1'b0),
        .bus_int(4'hF), .bus_ext(4'h0), .cart_int(1'b0), .rom_wr_en(1'b0), .ultimax(1'b0), .own_reset(1'b0),
        .phi2_p(t == 5'd0), .cpu_reset(rst), .vic(!cpu_half), .vaddr(12'h123), .ba(1'b1),
        .addr(cpu_half ? cyc_addr : 16'h0123), .din(8'hC3), .we(cpu_half && cyc_we),
        .roml_n(1'b1), .romh_n(1'b1), .io1_n(1'b1), .io2_n(1'b1),
        .sock_hit(cpu_half ? cyc_hit : 2'b00), .sock_en({sk_on2, sk_on1}), .sock_split(split),
        .rom_port(), .io1_rd(), .io2_rd(), .io2_ext(), .irq_ext(), .present(), .game(), .exrom(), .irq(), .nmi(),
        .dout(dout), .reset_req(), .status(status), .cnt0(), .cnt1(), .last(last),
        .p_addr(slot_a), .p_ahi_t(p_ahi_t), .p_rw(p_rw), .p_d_o(p_d_o), .p_d_t(p_d_t),
        .p_d_i(!p_ddir && !p_doen ? pin_d : 8'hFF), .p_adir(p_adir), .p_aoen(p_aoen), .p_ddir(p_ddir), .p_doen(p_doen),
        .p_roml(p_roml), .p_romh(p_romh), .p_io1(p_io1), .p_io2(p_io2),
        .p_phi2(p_phi2), .p_dotclk(p_dotclk), .p_ba(p_ba), .p_rst_o(p_rst_o),
        .p_sid1(p_sid1), .p_sid2(p_sid2), .p_sid_a(p_sid_a),
        .p_game(1'b1), .p_exrom(1'b1), .p_dma(1'b1), .p_irq(1'b1), .p_nmi(1'b1), .p_rst_i(1'b1)
    );
    always @(posedge clk) t <= t + 5'd1;    // phi2_p in tick 0: the port's tk equals t

    // what one cycle shows on the pins (from the clock after its tick 0 to its last)
    integer n1, n2, nphi, first1, cs_at_fall, d_drv, rw_lo, sa1;
    reg     phi_q;
    task cycle;
        integer k;
        begin
            n1 = 0; n2 = 0; nphi = 0; first1 = -1; cs_at_fall = 0; d_drv = 0; rw_lo = 0; sa1 = 0;
            @(posedge clk); #1; while (t != 5'd0) begin @(posedge clk); #1; end
            phi_q = p_phi2;
            for (k = 0; k < 32; k = k + 1) begin
                @(posedge clk); #1;
                if (!p_sid1) begin n1 = n1 + 1; if (first1 < 0) first1 = k + 1; end
                if (!p_sid2) n2 = n2 + 1;
                if (p_phi2) nphi = nphi + 1;
                if (phi_q && !p_phi2 && (!p_sid1 || !p_sid2)) cs_at_fall = 1;
                if (!p_d_t) d_drv = d_drv + 1;
                if (!p_rw) rw_lo = rw_lo + 1;
                if (p_sid_a) sa1 = sa1 + 1;
                phi_q = p_phi2;
            end
        end
    endtask

    integer errors = 0;
    task check(input ok, input [8*72-1:0] what);
        begin
            if (ok) $display("ok   %0s", what);
            else $display("     n1=%0d n2=%0d first1=%0d nphi=%0d dout=%h last=%h adc_l=%0d adc_r=%0d out=%0d/%0d", n1, n2, first1, nphi, dout, last, aud.adc_l, aud.adc_r, aud.out_l, aud.out_r);
            if (ok) ;
            else begin $display("FAIL: %0s", what); errors = errors + 1; end
        end
    endtask

    // ---- audio ------------------------------------------------------------------------------------------------------------
    reg mclk = 1'b0;
    always #40.69 mclk = ~mclk;             // 12.288 MHz
    reg signed [23:0] adc_in_l = 24'sd100000, adc_in_r = -24'sd30000;
    reg signed [23:0] core_l = 24'sd1000, core_r = -24'sd500;
    reg  [31:0] gain = {8'h80, 8'h00, 8'h80, 8'h40};    // bytes 7..4: socket 2 L, R, socket 1 L, R
    reg   [1:0] aen = 2'b11;
    wire        bclk, lrck, sdata, frame_en;
    reg         sdo_m = 1'b0;
    always @(posedge mclk) sdo_m <= sdata;
    wire #10    sdo_pin = sdo_m;
    wire signed [23:0] out_l, out_r;
    i2s_out codec (.mclk(mclk), .frame_en(frame_en), .in_l(adc_in_l), .in_r(adc_in_r), .spk_en(1'b0), .spk_gain(8'd0),
                   .bclk(bclk), .lrck(lrck), .sdata(sdata), .spk());
    sid_sock_audio_c64u aud (.mclk(mclk), .bclk(bclk), .lrck(lrck), .sdo(sdo_pin), .gain(gain), .en(aen),
                             .in_l(core_l), .in_r(core_r), .out_l(out_l), .out_r(out_r));
    task frames(input integer n);
        integer k;
        for (k = 0; k < n; k = k + 1) begin @(posedge frame_en); end
    endtask

    initial begin
        repeat (70) @(posedge clk);
        rst = 1'b0;
        repeat (70) @(posedge clk);

        // ---- the port
        cycle;
        check(nphi == 0 && n1 == 0 && n2 == 0 && p_aoen, "sockets off: the port idles (no PHI2, no select)");
        sk_on1 = 1'b1; cycle; cycle;
        check(nphi == 16, "socket 1 enabled: PHI2 runs (16 clocks high a cycle)");
        check(!status[16], "  SLOT_STATUS b16 stays 0: a socket is not a cartridge");
        check(n1 == 0 && n2 == 0, "  a cycle without a socket hit selects no socket");
        cyc_we = 1'b1; cyc_hit = 2'b01; cycle;
        check(n1 == 16 && first1 == 3 && n2 == 0, "a write to socket 1: select low port ticks 3-18, socket 2 high");
        check(cs_at_fall == 1, "  the select is still low when PHI2 falls (the chip latches the write)");
        check(d_drv >= 15 && rw_lo >= 16, "  D driven and R/W low in the write");
        cyc_we = 1'b0; cycle;
        check(n1 == 16 && first1 == 3, "a read of socket 1: select low port ticks 3-18");
        check(last[27] && last[26:24] == 3'd5 && last[23:16] == 8'h5A,
              "  the chip's byte is the port's sample (SLOT_LAST: read, strobe 5 = socket 1, the byte)");
        sk_on2 = 1'b1; cyc_hit = 2'b10; cyc_addr = 16'hD420; split = 3'd1; cycle;
        check(n2 == 16 && n1 == 0 && sa1 >= 16, "socket 2 at $D420, split A5: socket 2 selected, SID_ADDR high");
        cyc_addr = 16'hD400; cycle;
        check(sa1 == 0, "  and low for an address with A5 = 0");
        split = 3'd0; cyc_addr = 16'hD420; cycle;
        check(sa1 == 0, "  split off: SID_ADDR stays low");
        cyc_hit = 2'b00; sk_on1 = 1'b0; sk_on2 = 1'b0; cycle; cycle;
        check(nphi == 0 && n1 == 0 && n2 == 0, "both off again: the port idles");
        check(!status[17], "no guard fault");

        // ---- the audio
        frames(4);
        check(aud.adc_l == adc_in_l && aud.adc_r == adc_in_r, "the receiver takes both ADC channels");
        check(out_l == 24'sd71000 && out_r == 24'sd49500,
              "mix: L = core + s1 x 0 dB + s2 x 0 dB, R = core + s1 x -6 dB + s2 x 0");
        aen = 2'b01; frames(3);
        check(out_l == 24'sd101000 && out_r == 24'sd49500, "socket 2 disabled: it adds nothing");
        aen = 2'b00; frames(3);
        check(out_l == core_l && out_r == core_r, "both disabled: the core's mix alone");
        aen = 2'b11; adc_in_l = 24'sd8000000; adc_in_r = 24'sd8000000; frames(4);
        check(out_l == 24'sd8388607, "the sum saturates");

        if (errors == 0) $display("PASS tb_c64u_sock");
        else $display("FAIL tb_c64u_sock (%0d)", errors);
        $finish;
    end
endmodule
