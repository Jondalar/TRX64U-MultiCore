// T92 D5: the CPC's memories, all inferred block RAM, empty at synthesis and filled by the application (no ROM image is in the bitstream):
//   base RAM 64 KB as two byte-wide arrays (even and odd addresses): port A the CPU, port B the Gate Array's 16 bit video fetch (one word =
//     the byte at 2n and the byte at 2n+1, as the SDRAM delivered it to the original) and, in the clock it needs, the app's window
//   extension RAM 64 KB (the 6128's second 64 KB; the MMU's page 3): port A the CPU, port B the window
//   ROM 48 KB as three 16 KB arrays - lower ROM (OS), upper ROM 0 (BASIC), upper ROM 7 (AMSDOS): port A the window, port B the CPU
// The MMU's address (Amstrad_MMU.v) says which: ram_A[22] upper ROM (bank in [21:14]), otherwise [20:16] the 64 KB page (2 = base, 3 = the
// extension; others do not exist here: a read gives $FF, a write is dropped, as on a machine without that expansion) and [15:14] the bank.
// The window's access to the base RAM takes port B for one clock; the Gate Array keeps the last video word (`vq`) while it does, so a
// picture glitches at most in the one pixel pair that is fetched in that clock (the app freezes before it reads or writes RAM while the
// machine runs; during the load the machine is in reset). Written for this project (TREX), no upstream code.
`timescale 1ns / 1ps
module cpc_mem (
    input  wire        clk,
    // CPU
    input  wire [22:0] mem_addr,
    input  wire        mem_rd,
    input  wire        mem_wr,
    input  wire        romen,
    input  wire [7:0]  cpu_d,
    output wire [7:0]  mem_q,                   // valid one clock after the address; $FF where nothing answers
    // video
    input  wire [14:0] vram_addr,
    output wire [15:0] vram_q,
    // window
    input  wire        w_start,                 // one clock
    input  wire        w_we,
    input  wire        w_rom,
    input  wire [16:0] w_a,
    input  wire [7:0]  w_di,
    output reg         w_done = 1'b0,
    output reg  [7:0]  w_do = 8'd0,
    output wire        busy
);
    (* ram_style = "block" *) reg [7:0] ram_e [0:32767];
    (* ram_style = "block" *) reg [7:0] ram_o [0:32767];
    (* ram_style = "block" *) reg [7:0] ram_x [0:65535];
    (* ram_style = "block" *) reg [7:0] rom_os [0:16383];
    (* ram_style = "block" *) reg [7:0] rom_bas [0:16383];
    (* ram_style = "block" *) reg [7:0] rom_ams [0:16383];

    // ---------------------------------------------------------------- the CPU's decode
    wire        up_rom  = mem_addr[22];
    wire [4:0]  page    = mem_addr[20:16];
    wire        is_base = ~up_rom & (page == 5'd2);
    wire        is_ext  = ~up_rom & (page == 5'd3);
    wire [15:0] ba      = {mem_addr[15:14], mem_addr[13:0]};          // byte address in a 64 KB page
    wire [13:0] ra      = mem_addr[13:0];
    wire        wr_e = mem_wr & ~romen & is_base & ~ba[0];
    wire        wr_o = mem_wr & ~romen & is_base &  ba[0];
    wire        wr_x = mem_wr & ~romen & is_ext;

    // ---------------------------------------------------------------- window request
    reg        pend = 1'b0, p_we = 1'b0, p_rom = 1'b0;
    reg [16:0] p_a = 17'd0;
    reg [7:0]  p_d = 8'd0;
    reg        go_q = 1'b0;
    reg [1:0]  go_kind_q = 2'd0;               // 0 base RAM, 1 extension RAM, 2 ROM
    wire       go = pend;
    assign busy = pend;
    wire       p_base = ~p_rom & ~p_a[16];
    wire       p_ext  = ~p_rom &  p_a[16];
    always @(posedge clk) begin
        if (w_start) begin pend <= 1'b1; p_we <= w_we; p_rom <= w_rom; p_a <= w_a; p_d <= w_di; end
        go_q <= go;
        go_kind_q <= p_rom ? 2'd2 : p_ext ? 2'd1 : 2'd0;
        if (go) pend <= 1'b0;
    end
    wire steal = go & p_base;

    // ---------------------------------------------------------------- base RAM
    wire [14:0] b_idx = steal ? p_a[15:1] : vram_addr;
    wire        b_we_e = steal & p_we & ~p_a[0];
    wire        b_we_o = steal & p_we &  p_a[0];
    reg  [7:0]  a_qe = 8'd0, a_qo = 8'd0, b_qe = 8'd0, b_qo = 8'd0;
    always @(posedge clk) begin
        if (wr_e) ram_e[{ba[15:1]}] <= cpu_d;
        a_qe <= ram_e[ba[15:1]];
    end
    always @(posedge clk) begin
        if (b_we_e) ram_e[b_idx] <= p_d;
        b_qe <= ram_e[b_idx];
    end
    always @(posedge clk) begin
        if (wr_o) ram_o[{ba[15:1]}] <= cpu_d;
        a_qo <= ram_o[ba[15:1]];
    end
    always @(posedge clk) begin
        if (b_we_o) ram_o[b_idx] <= p_d;
        b_qo <= ram_o[b_idx];
    end
    reg [15:0] vq = 16'd0;
    reg        steal_q = 1'b0;
    always @(posedge clk) begin
        steal_q <= steal;
        if (!steal_q) vq <= {b_qo, b_qe};
    end
    assign vram_q = vq;

    // ---------------------------------------------------------------- extension RAM
    reg [7:0] x_qa = 8'd0, x_qb = 8'd0;
    always @(posedge clk) begin
        if (wr_x) ram_x[ba] <= cpu_d;
        x_qa <= ram_x[ba];
    end
    always @(posedge clk) begin
        if (go & p_ext & p_we) ram_x[p_a[15:0]] <= p_d;
        x_qb <= ram_x[p_a[15:0]];
    end

    // ---------------------------------------------------------------- ROM
    wire        w_os  = go & p_rom & (p_a[15:14] == 2'b00);
    wire        w_bas = go & p_rom & (p_a[15:14] == 2'b01);
    wire        w_ams = go & p_rom & (p_a[15:14] == 2'b10);
    reg  [7:0]  wq_os = 8'd0, wq_bas = 8'd0, wq_ams = 8'd0, cq_os = 8'd0, cq_bas = 8'd0, cq_ams = 8'd0;
    always @(posedge clk) begin
        if (w_os & p_we) rom_os[p_a[13:0]] <= p_d;
        wq_os <= rom_os[p_a[13:0]];
        cq_os <= rom_os[ra];
    end
    always @(posedge clk) begin
        if (w_bas & p_we) rom_bas[p_a[13:0]] <= p_d;
        wq_bas <= rom_bas[p_a[13:0]];
        cq_bas <= rom_bas[ra];
    end
    always @(posedge clk) begin
        if (w_ams & p_we) rom_ams[p_a[13:0]] <= p_d;
        wq_ams <= rom_ams[p_a[13:0]];
        cq_ams <= rom_ams[ra];
    end

    // ---------------------------------------------------------------- the CPU's byte: the select of the clock before
    reg [2:0] sel_q = 3'd7;           // 0 os, 1 basic, 2 amsdos, 3 base even, 4 base odd, 5 extension, 7 nothing
    always @(posedge clk) begin
        if (romen) sel_q <= up_rom ? ((mem_addr[21:14] == 8'd7) ? 3'd2 : 3'd1) : 3'd0;
        else if (is_base) sel_q <= ba[0] ? 3'd4 : 3'd3;
        else if (is_ext) sel_q <= 3'd5;
        else sel_q <= 3'd7;
    end
    reg [7:0] q;
    always @* begin
        case (sel_q)
            3'd0: q = cq_os;
            3'd1: q = cq_bas;
            3'd2: q = cq_ams;
            3'd3: q = a_qe;
            3'd4: q = a_qo;
            3'd5: q = x_qa;
            default: q = 8'hFF;
        endcase
    end
    assign mem_q = q;

    // ---------------------------------------------------------------- the window's result, the clock after the access
    always @(posedge clk) begin
        w_done <= go_q;
        if (go_q) begin
            if (p_we) w_do <= 8'h00;
            else case (go_kind_q)
                2'd0: w_do <= p_a[0] ? b_qo : b_qe;
                2'd1: w_do <= x_qb;
                default: w_do <= (p_a[15:14] == 2'b00) ? wq_os : (p_a[15:14] == 2'b01) ? wq_bas : (p_a[15:14] == 2'b10) ? wq_ams : 8'h00;
            endcase
        end
    end
endmodule
