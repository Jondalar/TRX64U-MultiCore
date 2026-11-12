// mig_arb with all its clients (cpu_mem_bridge, ddr2_port, the drive's toggle
// port, cart_fill's commands) against the MIG model:
// iverilog -g2012 -o tb_mig_arb.vvp tb_mig_arb.v ../ddr2_port.v mig_ui_model.v && vvp tb_mig_arb.vvp
`timescale 1ns/1ps
module tb;
    reg cpu_clk = 0, ui_clk = 0, sys_clk = 0;
    always #5 cpu_clk = ~cpu_clk;
    always #6.667 ui_clk = ~ui_clk;
    always #15.86 sys_clk = ~sys_clk;

    wire ui_rst, calib;
    wire [26:0] app_addr; wire [2:0] app_cmd; wire app_en, app_rdy, app_wdf_wren, app_wdf_rdy;
    wire [127:0] app_wdf_data, app_rd_data; wire [15:0] app_wdf_mask; wire app_rd_data_valid;
    mig_ui_model #(.MEM_WORDS(256), .STALL_PCT(20), .CALIB(20)) mig (.ui_clk(ui_clk), .ui_rst(ui_rst),
        .calib_done(calib), .app_addr(app_addr), .app_cmd(app_cmd), .app_en(app_en), .app_rdy(app_rdy),
        .app_wdf_data(app_wdf_data), .app_wdf_mask(app_wdf_mask), .app_wdf_wren(app_wdf_wren),
        .app_wdf_rdy(app_wdf_rdy), .app_rd_data(app_rd_data), .app_rd_data_valid(app_rd_data_valid));
    integer k;

    // the drive's port (drive_mem's side)
    reg dtgl = 0, dwe = 0; reg [25:0] daddr; reg [31:0] dwdata; reg [3:0] dbe; reg [1:0] ds = 0;
    wire ddone; wire [127:0] dline;
    always @(posedge cpu_clk) ds <= {ds[0], ddone};
    task drive_access(input w, input [25:0] a, input [3:0] b, input [31:0] d);
        begin
            @(posedge cpu_clk); dwe <= w; daddr <= a; dbe <= b; dwdata <= d; dtgl <= ~dtgl;
            @(posedge cpu_clk); while (ds[1] != dtgl) @(posedge cpu_clk);
        end
    endtask

    // cart_fill's side: four reads in a row from c_a, or one write
    reg [26:0] c_addr; reg [2:0] c_cmd; reg c_en = 0, c_wren = 0; reg [127:0] c_wd; reg [15:0] c_mask;
    wire c_rdy, c_wdf_rdy, c_valid; wire [127:0] c_rd;
    reg [127:0] c_got [0:3]; integer c_n = 0;
    always @(posedge ui_clk) if (c_valid) begin c_got[c_n] <= c_rd; c_n <= c_n + 1; end
    task cart_line(input [26:0] a);
        integer i;
        begin
            c_n = 0;
            for (i = 0; i < 4; i = i + 1) begin
                @(posedge ui_clk); c_en <= 1; c_cmd <= 3'b001; c_addr <= a + 8 * i;
                @(posedge ui_clk); while (!c_rdy) @(posedge ui_clk);
                c_en <= 0;
            end
            while (c_n != 4) @(posedge ui_clk);
        end
    endtask

    // CPU side, as bus_converter drives it
    reg request = 0, rwn = 1; reg [7:0] tag = 8'h21; reg [25:0] address; reg [3:0] be; reg [31:0] wdata;
    wire [31:0] rdata; wire rack; wire [7:0] rack_tag, dack_tag;
    wire cpu_tgl, cpu_done_tgl, cpu_we; wire [25:0] cpu_addr; wire [31:0] cpu_wdata, cpu_done_rdata; wire [3:0] cpu_be;
    cpu_mem_bridge br (.clk(cpu_clk), .rst(1'b0), .request(request), .tag(tag), .read_writen(rwn),
        .address(address), .byte_en(be), .wdata(wdata), .rdata(rdata), .rack(rack), .rack_tag(rack_tag),
        .dack_tag(dack_tag), .req_tgl(cpu_tgl), .a_addr(cpu_addr), .a_we(cpu_we), .a_wdata(cpu_wdata),
        .a_be(cpu_be), .done_tgl(cpu_done_tgl), .done_rdata(cpu_done_rdata));

    reg preq = 0, pwe = 0; reg [25:0] paddr; reg [31:0] pwdata; reg [3:0] pbe;
    wire pack; wire [31:0] prdata; wire port_tgl, port_done_tgl; wire [31:0] port_done_rdata;
    ddr2_port port (.clk(sys_clk), .req(preq), .addr(paddr), .we(pwe), .wdata(pwdata), .be(pbe),
        .ack(pack), .rdata(prdata), .req_tgl(port_tgl), .done_tgl(port_done_tgl), .done_rdata(port_done_rdata));

    wire ext; integer n_ext = 0, n0;
    always @(ext) n_ext = n_ext + 1;
    mig_arb arb (.ext_wr_tgl(ext), .ui_clk(ui_clk), .ui_rst(ui_rst), .calib_done(calib),
        .req_tgl({dtgl, port_tgl, cpu_tgl}), .addr({daddr, paddr, cpu_addr}), .we({dwe, pwe, cpu_we}),
        .wdata({dwdata, pwdata, cpu_wdata}), .be({dbe, pbe, cpu_be}),
        .done_tgl({ddone, port_done_tgl, cpu_done_tgl}), .rdata({port_done_rdata, cpu_done_rdata}),
        .rdata_line(dline),
        .c_addr(c_addr), .c_cmd(c_cmd), .c_en(c_en), .c_rdy(c_rdy), .c_wdf_data(c_wd),
        .c_wdf_mask(c_mask), .c_wdf_wren(c_wren), .c_wdf_rdy(c_wdf_rdy), .c_rd_data(c_rd),
        .c_rd_data_valid(c_valid),
        .v_addr(27'd0), .v_cmd(3'b001), .v_en(1'b0), .v_rdy(), .v_wdf_data(128'd0), .v_wdf_mask(16'd0),
        .v_wdf_wren(1'b0), .v_wdf_rdy(), .v_rd_data_valid(), .v_urgent(1'b0),
        .e_addr(27'd0), .e_cmd(3'd0), .e_en(1'b0), .e_rdy(), .e_wdf_data(128'd0), .e_wdf_mask(16'd0),
        .e_wdf_wren(1'b0), .e_rd_data_valid(),
        .app_addr(app_addr), .app_cmd(app_cmd), .app_en(app_en), .app_rdy(app_rdy),
        .app_wdf_data(app_wdf_data), .app_wdf_mask(app_wdf_mask), .app_wdf_wren(app_wdf_wren),
        .app_wdf_rdy(app_wdf_rdy), .app_rd_data(app_rd_data), .app_rd_data_valid(app_rd_data_valid));

    task cpu_write(input [25:0] a, input [3:0] b, input [31:0] d);
        begin
            @(posedge cpu_clk); request <= 1; rwn <= 0; address <= a; be <= b; wdata <= d;
            @(posedge cpu_clk); while (!(rack && rack_tag == tag)) @(posedge cpu_clk);
            request <= 0;
        end
    endtask
    task cpu_read(input [25:0] a, output [31:0] d);
        begin
            @(posedge cpu_clk); request <= 1; rwn <= 1; address <= a; be <= 4'hF;
            @(posedge cpu_clk); while (!(rack && rack_tag == tag)) @(posedge cpu_clk);
            request <= 0;
            while (dack_tag != tag) @(posedge cpu_clk);
            d = rdata;
        end
    endtask
    task port_access(input w, input [25:0] a, input [3:0] b, input [31:0] d, output [31:0] q);
        begin
            @(posedge sys_clk); preq <= 1; pwe <= w; paddr <= a; pbe <= b; pwdata <= d;
            @(posedge sys_clk); while (!pack) @(posedge sys_clk);
            preq <= 0; q = prdata;
        end
    endtask

    reg [31:0] v, w; integer errors = 0;
    task check(input [31:0] got, input [31:0] want, input [8*24-1:0] what);
        begin
            if (got !== want) begin errors = errors + 1; $display("FAIL %0s: %08x, want %08x", what, got, want); end
            else $display("ok   %0s: %08x", what, got);
        end
    endtask
    initial begin
        for (k = 0; k < 256; k = k + 1) mig.mem[k] = 0;
        while (!calib) @(posedge ui_clk);
        cpu_write(26'h0010, 4'hF, 32'h12345678);
        cpu_read (26'h0010, v);              check(v, 32'h12345678, "cpu word");
        cpu_write(26'h0010, 4'h2, 32'h0000AA00);
        cpu_read (26'h0010, v);              check(v, 32'h1234AA78, "cpu byte 1");
        cpu_write(26'h0010, 4'hC, 32'h69690000);
        cpu_read (26'h0010, v);              check(v, 32'h6969AA78, "cpu half 1");
        cpu_write(26'h001C, 4'hF, 32'hCAFEF00D);
        port_access(0, 26'h000E, 4'h0, 0, v); check(v, 32'hCAFEF00D, "port word at 0x1C");
        port_access(0, 26'h0009, 4'h0, 0, v); check(v[31:16], 16'h6969, "port 16-bit odd");
        port_access(1, 26'h0020, 4'hF, 32'hA5A55A5A, v);
        cpu_read (26'h0040, v);              check(v, 32'hA5A55A5A, "port write, cpu read");
        // both at once
        fork
            cpu_read(26'h001C, v);
            port_access(0, 26'h0008, 4'h0, 0, v);
        join
        check(v, 32'h6969AA78, "concurrent port");
        // the drive (16-bit word addresses): byte 0x12, then the 128-bit word at 0x10
        drive_access(1, 26'h0009, 4'h4, 32'hEEEEEEEE);
        drive_access(0, 26'h0008, 4'h0, 0);   check(dline[31:0], 32'h69EEAA78, "drive line word 0");
        check(dline[127:96], 32'hCAFEF00D, "drive line word 3");
        cpu_read (26'h0010, v);              check(v, 32'h69EEAA78, "drive byte, cpu read");
        // cart_fill: a line of four words while the others go on
        mig.mem[16] = {4{32'h11111111}}; mig.mem[17] = {4{32'h22222222}};
        mig.mem[18] = {4{32'h33333333}}; mig.mem[19] = {4{32'h44444444}};
        fork
            cart_line(27'h80);
            cpu_read(26'h0040, v);
            drive_access(0, 26'h0008, 4'h0, 0);
            port_access(0, 26'h000E, 4'h0, 0, w);
        join
        check(c_got[0][31:0], 32'h11111111, "cart line word 0");
        check(c_got[3][127:96], 32'h44444444, "cart line word 3");
        check(v, 32'hA5A55A5A, "cpu next to the cart");
        check(w, 32'hCAFEF00D, "port next to the cart");
        check(dline[31:0], 32'h69EEAA78, "drive next to the cart");
        // T133: ext_wr_tgl toggles once for every write into the RISC-V's cacheable window (word address < 0x750000)
        // that is not client 0's, never for the CPU's own, never for reads, never above the window
        n0 = n_ext;
        cpu_write(26'h0030, 4'hF, 32'h0BADF00D);   repeat (30) @(posedge ui_clk);
        cpu_read (26'h0030, v);                     repeat (30) @(posedge ui_clk);
        check(n_ext - n0, 0, "ext: cpu write and read");
        port_access(0, 26'h0020, 4'h0, 0, v);       repeat (30) @(posedge ui_clk);
        check(n_ext - n0, 0, "ext: port read");
        port_access(1, 26'h0024, 4'hF, 32'h1, v);   repeat (30) @(posedge ui_clk);
        check(n_ext - n0, 1, "ext: port write");
        drive_access(1, 26'h0009, 4'h4, 32'h2);     repeat (30) @(posedge ui_clk);
        check(n_ext - n0, 2, "ext: drive write");
        @(posedge ui_clk); c_en <= 1; c_cmd <= 3'b000; c_addr <= 27'h000040; c_wd <= {4{32'h3}}; c_mask <= 16'h0; c_wren <= 1;
        @(posedge ui_clk); while (!c_rdy) @(posedge ui_clk);
        c_en <= 0; c_wren <= 0;                     repeat (30) @(posedge ui_clk);
        check(n_ext - n0, 3, "ext: cart_fill write");
        port_access(1, 26'h0760000, 4'hF, 32'h4, v); repeat (30) @(posedge ui_clk);
        check(n_ext - n0, 3, "ext: write above the window");
        if (errors) $display("FAILED"); else $display("PASS");
        $finish;
    end
endmodule
