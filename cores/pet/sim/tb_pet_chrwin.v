// T216 §5: the app's window to the 4 KiB character ROM (rom_sel 2): every byte of offset 0-0xFFF is written and read back through pet_win + pet_mem,
// the upper 2 KiB (0x800-0xFFF, the 8032/8096/SuperPET characters.901640-01) included; offset 0x1000 and up is refused (reads 0).
`timescale 1ns / 1ps
module tb_pet_chrwin;
    reg clk = 0;
    always #5 clk = ~clk;
    reg        p_req = 0, p_we = 0, p_rom = 1;
    reg [15:0] p_addr = 0;
    reg [7:0]  p_wdata = 0;
    reg [3:0]  rom_sel = 4'd2;
    wire w_wr, w_hit, w_rom; wire [3:0] w_sel; wire [15:0] w_ai; wire [7:0] w_di, w_do, port_rdata;
    assign port_rdata = w_do;
    pet_win win (.clk(clk), .dma_req_tgl(1'b0), .dma_addr(16'd0), .dma_we(1'b0), .dma_wdata(8'd0), .dma_rom(1'b0), .dma_done_tgl(), .dma_rdata(), .dbg_busy(1'b0),
        .rom_sel(rom_sel), .port_use(), .port_addr(), .port_rom(), .port_we(), .port_wdata(), .port_rdata(port_rdata),
        .p_req(p_req), .p_addr(p_addr), .p_rom(p_rom), .p_we(p_we), .p_wdata(p_wdata),
        .w_wr(w_wr), .w_hit(w_hit), .w_rom(w_rom), .w_sel(w_sel), .w_ai(w_ai), .w_di(w_di));
    pet_mem mem (.clk(clk), .m_addr(15'd0), .m_wdata(8'd0), .m_we(1'b0), .ram_q(), .r_addr(15'd0), .rom_q(),
        .vc_addr(11'd0), .vc_we(1'b0), .vc_q(), .vv_addr(11'd0), .vv_q(), .ch_addr(12'd0), .ch_q(),
        .w_wr(w_wr), .w_hit(w_hit), .w_rom(w_rom), .w_sel(w_sel), .r9_addr(15'd0), .rom9_q(), .w_ai(w_ai), .w_di(w_di), .w_do(w_do));
    integer errs = 0, i;
    function [7:0] pat(input integer o); pat = (o * 13 + (o >> 8) * 7 + 1) & 8'hFF; endfunction
    task acc(input [15:0] a, input we, input [7:0] d);
        begin
            @(posedge clk); p_req <= 1; p_addr <= a; p_we <= we; p_wdata <= d;
            @(posedge clk); p_req <= 0; p_we <= 0;
            @(posedge clk); @(posedge clk);
        end
    endtask
    initial begin
        #50;
        for (i = 0; i < 4096; i = i + 1) acc(16'h2000 + i, 1, pat(i));
        for (i = 0; i < 4096; i = i + 1) begin
            acc(16'h2000 + i, 0, 8'd0);
            if (w_do !== pat(i)) begin errs = errs + 1; if (errs < 8) $display("FAIL chr offset %h: %h expected %h", i, w_do, pat(i)); end
        end
        acc(16'h3000, 1, 8'hEE); acc(16'h3000, 0, 8'd0);
        if (w_do !== 8'h00) begin errs = errs + 1; $display("FAIL chr offset 0x1000 not refused: %h", w_do); end
        acc(16'h2000, 0, 8'd0);
        if (w_do !== pat(0)) begin errs = errs + 1; $display("FAIL chr offset 0 overwritten by the refused write"); end
        if (errs == 0) $display("PASS tb_pet_chrwin"); else $display("FAIL tb_pet_chrwin errs=%0d", errs);
        $finish;
    end
endmodule
