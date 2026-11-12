// T182 step 1: AREA TRIAL, not the core. What the trial measures (spec docs/specs/T182-c64scpu-core.md, section 5): the 65C816, the CPU-side cache over the
// DDR2 (SRAM banks 0/1, SuperRAM, SCPU ROM), the SCPU register block ($D07x/$D0Bx, semantics of VICE scpu64mem.c), the 6510 port, the mirroring decision and the
// speed/PHI2 synchronisation, wired so that synthesis cannot drop them. Behaviour is NOT verified here; the C64 side (I/O, VIC, C64 RAM) is the base design's.
// scpu_trial_stub is the world around it (random data, a DDR2 that answers after a delay): its LUT/FF are not part of the figures.
`timescale 1ns / 1ps

module scpu_trial_top (
    input  wire clk,            // 63.06 MHz (sid2x_clk): the CPU is clocked here with a clock enable
    input  wire rst,
    output wire [31:0] obs
);
    wire phi2_tick, irq_n, nmi_n, sw_1mhz, sw_jiffy, game, exrom;
    wire [7:0] io_din;
    wire ddr_rd, ddr_rdone, ddr_wr, ddr_wack;
    wire [23:0] ddr_raddr, ddr_waddr;
    wire [7:0] ddr_wdata;
    wire [127:0] ddr_rdata;
    wire [31:0] cobs;
    wire [15:0] io_a;
    wire io_we, io_cs, mirror_we;
    wire [7:0] io_dout;

    (* keep_hierarchy = "yes" *)
    scpu_trial_stub stub (.clk(clk), .rst(rst), .phi2_tick(phi2_tick), .irq_n(irq_n), .nmi_n(nmi_n), .sw_1mhz(sw_1mhz), .sw_jiffy(sw_jiffy),
        .game(game), .exrom(exrom), .io_din(io_din), .ddr_rd(ddr_rd), .ddr_raddr(ddr_raddr), .ddr_rdone(ddr_rdone), .ddr_rdata(ddr_rdata),
        .ddr_wr(ddr_wr), .ddr_waddr(ddr_waddr), .ddr_wdata(ddr_wdata), .ddr_wack(ddr_wack));

    (* keep_hierarchy = "yes" *)
    scpu_cpu cpu (.clk(clk), .rst(rst), .phi2_tick(phi2_tick), .irq_n(irq_n), .nmi_n(nmi_n), .sw_1mhz(sw_1mhz), .sw_jiffy(sw_jiffy),
        .game(game), .exrom(exrom), .io_din(io_din), .ddr_rd(ddr_rd), .ddr_raddr(ddr_raddr), .ddr_rdone(ddr_rdone), .ddr_rdata(ddr_rdata),
        .ddr_wr(ddr_wr), .ddr_waddr(ddr_waddr), .ddr_wdata(ddr_wdata), .ddr_wack(ddr_wack),
        .io_a(io_a), .io_we(io_we), .io_cs(io_cs), .io_dout(io_dout), .mirror_we(mirror_we), .obs(cobs));

    // everything that leaves the CPU block is folded into 32 flip-flops that must stay (DONT_TOUCH)
    wire [31:0] fold = cobs ^ {io_a, io_dout, 6'd0, io_we, io_cs} ^ {ddr_raddr[23:0], ddr_wdata} ^ {ddr_waddr[23:0], 7'd0, mirror_we} ^ {31'd0, ddr_rd} ^ {31'd0, ddr_wr};
    (* DONT_TOUCH = "TRUE" *) reg [31:0] sink_q;
    always @(posedge clk) sink_q <= fold;
    assign obs = sink_q;
endmodule

// ---------------------------------------------------------------------------------------------------------------------------------------------
module scpu_cpu (
    input  wire clk, rst,
    input  wire phi2_tick,                  // one per C64 PHI2 cycle
    input  wire irq_n, nmi_n, sw_1mhz, sw_jiffy, game, exrom,
    input  wire [7:0] io_din,
    output reg  ddr_rd, output reg [23:0] ddr_raddr, input wire ddr_rdone, input wire [127:0] ddr_rdata,
    output wire ddr_wr, output wire [23:0] ddr_waddr, output wire [7:0] ddr_wdata, input wire ddr_wack,
    output wire [15:0] io_a, output wire io_we, io_cs, output wire [7:0] io_dout,
    output wire mirror_we,
    output wire [31:0] obs
);
    // ---- the CPU
    wire [23:0] a;
    wire [7:0] dout;
    wire we, vpa, vda, mlb, vpb, rdy_out;
    reg  [7:0] din;
    reg  cpu_ce;
    scpu_cpu816 cpu816 (.CLK(clk), .RST_N(~rst), .CE(cpu_ce), .RDY_IN(1'b1), .NMI_N(nmi_n), .IRQ_N(irq_n), .ABORT_N(1'b1),
        .D_IN(din), .D_OUT(dout), .A_OUT(a), .WE(we), .RDY_OUT(rdy_out), .VPA(vpa), .VDA(vda), .MLB(mlb), .VPB(vpb));

    // ---- the 6510 port ($00/$01 live outside the CPU)
    reg [7:0] pddr, pdata;
    wire [2:0] pport = (pdata | ~pddr) & 3'b111;

    // ---- SCPU registers (semantics: VICE scpu64mem.c:688-821 writes, :634-686 reads)
    reg sys1, soft1, hw, dos, ramlink, boot;
    reg [7:0] optim;
    reg [2:0] simm;
    wire fast = ~(sys1 | soft1 | (sw_1mhz & ~hw));
    wire bank0 = (a[23:16] == 8'h00);
    wire grp07 = bank0 & (a[15:4] == 12'h0D07);
    wire grp0b = bank0 & (a[15:4] == 12'h0D0B);
    wire [3:0] rn = a[3:0];
    wire reg_wr = we & vda & cpu_ce;
    always @(posedge clk) begin
        if (rst) begin
            sys1 <= 1'b0; soft1 <= 1'b0; hw <= 1'b0; dos <= 1'b0; ramlink <= 1'b0; boot <= 1'b1; optim <= 8'hC7; simm <= 3'd4;
            pddr <= 8'h2F; pdata <= 8'h37;
        end else begin
            if (reg_wr && grp07) case (rn)
                4'h2: sys1 <= 1'b1;
                4'h3: sys1 <= 1'b0;
                4'h4, 4'h5, 4'h6, 4'h7: if (hw) optim <= {rn[1:0], 6'd0};
                4'h8: if (hw) simm <= dout[2:0];
                4'hA: soft1 <= 1'b1;
                4'h9, 4'hB: soft1 <= 1'b0;
                4'hE: hw <= 1'b1;
                4'hD, 4'hF: hw <= 1'b0;
                default: ;
            endcase
            if (reg_wr && grp0b) case (rn)
                4'h2: if (hw) begin sys1 <= dout[6]; if (!dout[7]) hw <= 1'b0; end
                4'h3: if (hw) optim <= (optim & 8'h38) | (dout & 8'hC7);
                4'h4: if (hw) optim <= (optim & 8'h3F) | (dout & 8'hC0);
                4'h6: if (hw && boot) boot <= 1'b0;
                4'h7: if (hw && !boot) boot <= 1'b1;
                4'h8: if (hw) soft1 <= dout[7];
                4'hC: if (hw) dos <= dout[7];
                4'hE: if (hw && !dos) dos <= 1'b1;
                4'hD, 4'hF: dos <= 1'b0;
                default: ;
            endcase
            if (reg_wr && bank0 && a[15:0] == 16'h0000) pddr  <= dout;
            if (reg_wr && bank0 && a[15:0] == 16'h0001) pdata <= dout;
        end
    end
    reg [7:0] reg_rd;
    always @* begin
        reg_rd = 8'hFF;
        if (grp0b) case (rn)
            4'h0: reg_rd = 8'h40;
            4'h2: reg_rd = {hw, sys1, 6'd0};
            4'h3, 4'h4: reg_rd = optim & 8'hC0;
            4'h5: reg_rd = {1'b1 & sw_jiffy, sw_1mhz, 6'd0};
            4'h6: reg_rd = 8'h00;
            4'h8, 4'h9: reg_rd = {soft1, (soft1 | (sw_1mhz & ~hw) | sys1), 6'd0};
            4'hC, 4'hD, 4'hE, 4'hF: reg_rd = {dos, ramlink, 6'd0};
            default: reg_rd = 8'h00;
        endcase
        if (grp0b) reg_rd = reg_rd | {5'd0, optim[2:0]};
    end

    // ---- mirroring of a bank 0 write into the C64 RAM the VIC sees (VICE scpu64mem.c:81-86 table, :1480-1483 index)
    wire [3:0] mi = {optim[7], optim[6], optim[2], optim[0]};
    reg [15:0] mrange;
    always @* case (mi)
        4'd0, 4'd1: mrange = 16'h80BF;  4'd2: mrange = 16'h003F;  4'd3: mrange = 16'h023F;
        4'd4, 4'd5: mrange = 16'h407F;  4'd6, 4'd7: mrange = 16'hC0FF;
        4'd8, 4'd9: mrange = 16'h0407;  4'd10, 4'd11: mrange = 16'h0000;
        4'd12, 4'd14: mrange = 16'h00FF; default: mrange = 16'h02FF;
    endcase
    assign mirror_we = we & vda & cpu_ce & bank0 & (mrange != 16'd0) & (a[15:8] >= mrange[15:8]) & (a[15:8] <= mrange[7:0]);

    // ---- where an access goes (stand-in for the 256-entry configuration table of scpu64meminit.c:86-257: the index {boot,dos,hw,!game,!exrom,pport} + the page)
    wire [7:0] cfg = {boot, dos, hw, ~game, ~exrom, pport};
    wire io_vis = bank0 & (a[15:12] == 4'hD) & (pport[1] | pport[0]) & ~(cfg[7] & cfg[6] & cfg[5]);
    assign io_cs  = io_vis & vda;
    assign io_a   = a[15:0];
    assign io_we  = we & vda & io_cs & cpu_ce;
    assign io_dout = dout;
    wire sel_reg = grp07 | grp0b;
    wire sel_cached = ~io_vis;                // SRAM banks 0/1, SuperRAM, SCPU ROM: through the cache

    // ---- cache: 512 lines x 16 bytes (8 KiB) direct mapped over the 24 bit space, 11 bit tag
    reg [127:0] dmem [0:511];
    reg [11:0]  tmem [0:511];
    reg [127:0] d_q;
    reg [11:0]  t_q;
    reg [23:0]  a_q;
    reg fill, fill_done;
    wire [8:0] idx = a[12:4];
    wire cwr_hit = we & vda & cpu_ce & sel_cached & t_q[11] & (t_q[10:0] == a_q[23:13]) & (a_q == a);
    wire [127:0] wdat = fill_done ? ddr_rdata : {16{dout}};
    wire [15:0] lane = fill_done ? 16'hFFFF : (16'd1 << a[3:0]);
    wire dw = fill_done | cwr_hit;
    integer i;
    always @(posedge clk) begin
        d_q <= dmem[idx];
        t_q <= tmem[idx];
        a_q <= a;
        if (dw) for (i = 0; i < 16; i = i + 1) if (lane[i]) dmem[idx][8*i +: 8] <= wdat[8*i +: 8];
        if (fill_done) tmem[idx] <= {1'b1, a[23:13]};
    end
    wire hit = t_q[11] & (t_q[10:0] == a_q[23:13]);

    // ---- write queue (4 entries, in order, byte writes to the DDR2)
    reg [31:0] wq [0:3];
    reg [2:0] wcnt;
    reg [1:0] wrp, wwp;
    wire wpush = we & vda & cpu_ce & sel_cached;
    wire full = (wcnt == 3'd4);
    assign ddr_wr = (wcnt != 3'd0);
    assign ddr_waddr = wq[wrp][31:8];
    assign ddr_wdata = wq[wrp][7:0];
    always @(posedge clk) begin
        if (rst) begin wcnt <= 3'd0; wrp <= 2'd0; wwp <= 2'd0; end
        else begin
            if (wpush) begin wq[wwp] <= {a, dout}; wwp <= wwp + 2'd1; end
            if (ddr_wack && wcnt != 3'd0) wrp <= wrp + 2'd1;
            wcnt <= wcnt + {2'd0, wpush} - {2'd0, (ddr_wack && wcnt != 3'd0)};
        end
    end

    // ---- speed: 20 cycles per PHI2 cycle (accumulator against the 63.06 MHz clock), or PHI2 itself; I/O waits for PHI2
    reg [6:0] acc;
    reg [1:0] settle;
    wire [7:0] accn = {1'b0, acc} + 8'd20;
    wire slot_fast = (accn >= 8'd64);
    wire slot = fast ? slot_fast : phi2_tick;
    wire need_phi2 = io_cs & fast;
    wire addr_ok = (settle == 2'd2);
    wire cached_ok = ~sel_cached | (hit & ~fill & ~fill_done);
    wire go = slot & addr_ok & cached_ok & (~need_phi2 | phi2_tick) & ~(full & we & vda & sel_cached);
    always @(posedge clk) begin
        if (rst) begin acc <= 7'd0; settle <= 2'd0; cpu_ce <= 1'b0; fill <= 1'b0; fill_done <= 1'b0; ddr_rd <= 1'b0; ddr_raddr <= 24'd0; end
        else begin
            cpu_ce <= go;
            fill_done <= 1'b0;
            if (slot_fast) acc <= accn[6:0] - 7'd64; else acc <= accn[6:0];
            if (go) settle <= 2'd0; else if (settle != 2'd2) settle <= settle + 2'd1;
            if (!fill && addr_ok && sel_cached && !hit) begin fill <= 1'b1; ddr_rd <= 1'b1; ddr_raddr <= {a[23:4], 4'd0}; end
            if (fill && ddr_rdone) begin fill <= 1'b0; ddr_rd <= 1'b0; fill_done <= 1'b1; end
        end
    end

    // ---- what the CPU reads
    wire [7:0] cbyte = d_q[8*a_q[3:0] +: 8];
    always @* din = sel_reg ? reg_rd : (io_cs ? io_din : cbyte);

    assign obs = {vpa, vda, mlb, vpb, rdy_out, fast, hw, dos, boot, ramlink, simm, optim[7:0], dout[7:0]} ^ {a[23:0], 8'd0};
endmodule

// ---------------------------------------------------------------------------------------------------------------------------------------------
// The world around the CPU block: not part of the figures.
module scpu_trial_stub (
    input  wire clk, rst,
    output wire phi2_tick, irq_n, nmi_n, sw_1mhz, sw_jiffy, game, exrom,
    output wire [7:0] io_din,
    input  wire ddr_rd, input wire [23:0] ddr_raddr, output reg ddr_rdone, output wire [127:0] ddr_rdata,
    input  wire ddr_wr, input wire [23:0] ddr_waddr, input wire [7:0] ddr_wdata, output reg ddr_wack
);
    reg [127:0] lf;
    reg [5:0] pc;
    reg [3:0] dl;
    always @(posedge clk) begin
        lf <= {lf[126:0], lf[127] ^ lf[126] ^ lf[125] ^ lf[120] ^ ~|lf[126:0]} ^ {104'd0, ddr_waddr} ^ {120'd0, ddr_wdata} ^ {104'd0, ddr_raddr};
        pc <= pc + 6'd1;
        dl <= ddr_rd ? dl + 4'd1 : 4'd0;
        ddr_rdone <= ddr_rd & (dl == 4'd9);
        ddr_wack  <= ddr_wr & lf[3];
    end
    assign phi2_tick = (pc == 6'd0);
    assign irq_n = ~lf[7] | lf[9];
    assign nmi_n = ~lf[11] | lf[13] | lf[15];
    assign sw_1mhz = lf[17] & lf[19];
    assign sw_jiffy = lf[21];
    assign game = lf[23];
    assign exrom = lf[25];
    assign io_din = lf[39:32];
    assign ddr_rdata = lf;
endmodule
