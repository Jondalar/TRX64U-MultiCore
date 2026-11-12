// The C64 palette (T59): 16 x 24 bit, written by the app's bus (C64_PALETTE
// 0x10180800, ultimate_sys.vhd: entry n's R, G, B at 4n+0..2, byte writes),
// read in the pixel domain by colour index. Reset contents are Colodore, so the
// picture is unchanged until the app writes (it does, once, at boot).
// Distributed RAM: the write port is in clk_w, the read port is asynchronous
// and is registered by the caller in its own clock. The two clocks are timed
// as asynchronous groups (pins.xdc), so the read does not see a write in
// progress consistently: a byte write can show for one pixel as the old and
// the new byte of an entry mixed. The value is quasi-static (the app writes the
// whole palette in a burst, never per pixel); the pixel that reads it during
// the write is the only one affected. A write takes effect at once, not at the
// next frame.
module pal_ram (
    input  wire        clk_w,
    input  wire        we,
    input  wire [5:0]  addr,            // 4n + 0 R, 1 G, 2 B (3 is ignored)
    input  wire [7:0]  wdata,
    input  wire [3:0]  idx,
    output wire [23:0] rgb
);
    (* ram_style = "distributed" *) reg [7:0] pal_r [0:15];
    (* ram_style = "distributed" *) reg [7:0] pal_g [0:15];
    (* ram_style = "distributed" *) reg [7:0] pal_b [0:15];
    initial begin
        {pal_r[0],  pal_g[0],  pal_b[0]}  = 24'h000000;
        {pal_r[1],  pal_g[1],  pal_b[1]}  = 24'hFFFFFF;
        {pal_r[2],  pal_g[2],  pal_b[2]}  = 24'h813338;
        {pal_r[3],  pal_g[3],  pal_b[3]}  = 24'h75CEC8;
        {pal_r[4],  pal_g[4],  pal_b[4]}  = 24'h8E3C97;
        {pal_r[5],  pal_g[5],  pal_b[5]}  = 24'h56AC4D;
        {pal_r[6],  pal_g[6],  pal_b[6]}  = 24'h2E2C9B;
        {pal_r[7],  pal_g[7],  pal_b[7]}  = 24'hEDF171;
        {pal_r[8],  pal_g[8],  pal_b[8]}  = 24'h8E5029;
        {pal_r[9],  pal_g[9],  pal_b[9]}  = 24'h553800;
        {pal_r[10], pal_g[10], pal_b[10]} = 24'hC46C71;
        {pal_r[11], pal_g[11], pal_b[11]} = 24'h4A4A4A;
        {pal_r[12], pal_g[12], pal_b[12]} = 24'h7B7B7B;
        {pal_r[13], pal_g[13], pal_b[13]} = 24'hA9FF9F;
        {pal_r[14], pal_g[14], pal_b[14]} = 24'h706DEB;
        {pal_r[15], pal_g[15], pal_b[15]} = 24'hB2B2B2;
    end
    always @(posedge clk_w)
        if (we)
            case (addr[1:0])
            2'd0: pal_r[addr[5:2]] <= wdata;
            2'd1: pal_g[addr[5:2]] <= wdata;
            2'd2: pal_b[addr[5:2]] <= wdata;
            default: ;
            endcase
    assign rgb = {pal_r[idx], pal_g[idx], pal_b[idx]};
endmodule
