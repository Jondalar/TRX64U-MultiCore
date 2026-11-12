// T113 S8/S9: the CPU side of the VDC as the C128 core drives it (include inside a testbench module that has `clk` and `dout`).
// An access holds cs/rs/we/db_in for a bus cycle, raises the enableBus strobe (bus_en) for one clock in the middle of it and the
// CPU samples the VDC's data out at the end; the default gap makes one access 32 clocks = a 1 MHz cycle at 31.5278 MHz.
reg        bus_en = 1'b0, cs = 1'b0, rs = 1'b0, we = 1'b0;
reg  [7:0] din = 8'd0;
integer    gap = 24;
longint    cyc = 0;
longint    t_pulse = 0;
always @(posedge clk) cyc <= cyc + 1;

task automatic bus(input r, input wr, input [7:0] d, output [7:0] q);
    begin
        @(posedge clk); cs <= 1'b1; rs <= r; we <= wr; din <= d;
        @(posedge clk); @(posedge clk);
        bus_en <= 1'b1; t_pulse = cyc;
        @(posedge clk); bus_en <= 1'b0;
        repeat (3) @(posedge clk);
        q = dout;
        cs <= 1'b0; we <= 1'b0;
        repeat (gap) @(posedge clk);
    end
endtask

// the programmer's rule: poll status bit 7 (1 = ready) before the next access
task automatic wait_ready;
    reg [7:0] s;
    integer n;
    begin
        n = 0; s = 8'd0;
        while (!s[7] && n < 200000) begin bus(1'b0, 1'b0, 8'd0, s); n = n + 1; end
        if (!s[7]) $display("tb: status bit 7 never came back");
    end
endtask

task automatic wr_sel(input [7:0] n);
    reg [7:0] q;
    begin bus(1'b0, 1'b1, n, q); end
endtask

task automatic wr_dat(input [7:0] v);
    reg [7:0] q;
    begin bus(1'b1, 1'b1, v, q); end
endtask

task automatic wr_reg(input [7:0] n, input [7:0] v);
    begin wr_sel(n); wr_dat(v); wait_ready; end
endtask
