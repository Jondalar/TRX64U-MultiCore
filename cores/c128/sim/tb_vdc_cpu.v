// T113 bench for vdc_cpu (variant 2): the CPU-visible behaviour
`timescale 1ns / 1ps
module tb_vdc_cpu;
    reg clk = 0, vd_clk = 0, rst = 1;
    always #16 clk = ~clk;
    always #5 vd_clk = ~vd_clk;
    reg bus_en = 0, cs = 0, rs = 0, we = 0;
    reg [7:0] din = 0;
    wire [7:0] dout;
    reg [15:0] vd_addr = 0; reg vd_we = 0;
    wire [7:0] vd_rdata;
    vdc_cpu #(.RAM_BITS(14)) d (.clk(clk), .rst(rst), .bus_en(bus_en), .cs(cs), .rs(rs), .we(we), .db_in(din), .db_out(dout),
        .vd_clk(vd_clk), .vd_addr(vd_addr), .vd_we(vd_we), .vd_wdata(8'd0), .vd_rdata(vd_rdata));
    integer errors = 0;
    task acc(input r, input w, input [7:0] v);   // one CPU access
        begin
            @(posedge clk); cs <= 1; rs <= r; we <= w; din <= v; bus_en <= 1;
            @(posedge clk); bus_en <= 0; cs <= 0; we <= 0;
            @(posedge clk); @(posedge clk);
        end
    endtask
    task wreg(input [5:0] n, input [7:0] v); begin acc(0,1,{2'b0,n}); acc(1,1,v); end endtask
    task rreg(input [5:0] n, output [7:0] v); begin acc(0,1,{2'b0,n}); acc(1,0,0); v = dout; end endtask
    task wait_ready; reg [7:0] s; integer k; begin k = 0; s = 0; while (!s[7] && k < 1000) begin acc(0,0,0); s = dout; k = k + 1; end end endtask
    task chk(input [7:0] got, input [7:0] exp, input [255:0] what);
        begin if (got !== exp) begin errors = errors + 1; $display("FAIL %0s: got %h expected %h", what, got, exp); end end
    endtask
    reg [7:0] v; integer i;
    initial begin
        repeat (4) @(posedge clk); rst = 0; repeat (4) @(posedge clk);
        // 1. UA, data write with auto-increment, read back
        wreg(18, 8'h00); wreg(19, 8'h10);          // UA = $0010
        for (i = 0; i < 4; i = i + 1) begin wreg(31, 8'hA0 + i); wait_ready; end
        rreg(18, v); chk(v, 8'h00, "UA hi"); rreg(19, v); chk(v, 8'h14, "UA lo after 4 writes");
        wreg(19, 8'h10); wait_ready;               // UA = $0010 again, DA loaded
        for (i = 0; i < 4; i = i + 1) begin rreg(31, v); chk(v, 8'hA0 + i, "read back"); wait_ready; end
        // 2. fill: DA is the fill value (last written R31), WC = 8
        wreg(18, 8'h00); wreg(19, 8'h40); wait_ready;
        wreg(31, 8'h5A); wait_ready;               // writes $5A at $0040, UA -> $0041
        wreg(30, 8'd8); wait_ready;                // fill 8 bytes from $0041
        wreg(19, 8'h45); wait_ready; rreg(31, v); chk(v, 8'h5A, "fill byte");
        wreg(19, 8'h48); wait_ready; rreg(31, v); chk(v, 8'h5A, "fill last byte");
        // 3. copy: R24 bit 7, source BA = $0010, dest UA = $0100, WC = 4
        wreg(24, 8'h80); wreg(32, 8'h00); wreg(33, 8'h10); wreg(18, 8'h01); wreg(19, 8'h00); wait_ready;
        wreg(30, 8'd4); wait_ready;
        wreg(24, 8'h00); wreg(19, 8'h02); wait_ready; rreg(31, v); chk(v, 8'hA2, "copy byte 2");
        // 4. the application's window sees it
        @(posedge vd_clk); vd_addr <= 16'h0102; repeat (3) @(posedge vd_clk); chk(vd_rdata, 8'hA2, "window VRAM read");
        vd_addr <= 16'h4000 + 16'd24; repeat (3) @(posedge vd_clk); chk(vd_rdata, 8'h00, "window R24");
        // 5. dirty snapshot: pages 0 and 1 were written
        vd_addr <= 16'h4042; vd_we <= 1; @(posedge vd_clk); vd_we <= 0; repeat (20) @(posedge clk); repeat (20) @(posedge vd_clk);
        vd_addr <= 16'h4080; repeat (3) @(posedge vd_clk); chk(vd_rdata, 8'h03, "dirty pages 0,1");
        if (errors == 0) $display("PASS tb_vdc_cpu"); else $display("FAILED %0d", errors);
        $finish;
    end
endmodule
