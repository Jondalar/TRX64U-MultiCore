// T134: the PET's disk drives. One dual unit (device 8, drives 0 and 1) of the MiSTer 4040 / 8250 IEEE drive (cores/pet/rtl/ieee_drive, GPL-2, Erik Scheffers),
// the PET's IEEE-488 lines on its bus, and pet_dblk as the block service that reads and writes the image in DDR2. All in sys_clk (the drive wants
// at least 16 MHz; it divides its own 1 MHz from CLK, so the PAL / NTSC change of sys_clk moves the drive's speed by 4 %, which no PET program can tell),
// except the DDR2 port, which pet_dblk keeps in cpu_clk. SystemVerilog because the drive's bus is a packed struct.
// The registers come in from pet_regs (cpu_clk, quasi static, the mount strobes as toggles).
// Written for this project (TREX), no upstream code.
`timescale 1ns / 1ps
module pet_ieee (
    input  wire        sys_clk,
    input  wire        cpu_clk,
    input  wire        rst,                 // the machine's reset (sys_clk)
    // the PET's side (pet2001io's ieee_* ports; the bus level, 1 = released)
    input  wire [7:0]  p_data_o,
    input  wire        p_atn_o,
    input  wire        p_ndac_o,
    input  wire        p_nrfd_o,
    input  wire        p_dav_o,
    input  wire        p_eoi_o,
    output wire [7:0]  p_data_i,
    output wire        p_atn_i,
    output wire        p_ndac_i,
    output wire        p_nrfd_i,
    output wire        p_dav_i,
    output wire        p_eoi_i,
    // the registers (cpu_clk values, quasi static)
    input  wire        r_en,                // device 8 on
    input  wire        r_4040,              // 1 = 4040 (D64), 0 = 8250 (D80 / D82)
    input  wire [31:0] r_size0,
    input  wire [31:0] r_size1,
    input  wire [1:0]  r_ro,
    input  wire [1:0]  r_mnt_tgl,           // toggles: an image was (un)mounted
    input  wire [31:0] r_base0,
    input  wire [31:0] r_base1,
    input  wire [1:0]  r_clr_dirty_tgl,
    output wire [1:0]  s_dirty,             // sys_clk
    output wire [1:0]  s_led,               // sys_clk: activity of the two drives
    output wire        s_led_err,
    output wire        ev_tgl,              // the dirty log (sys_clk): a block was written to the image
    output wire [7:0]  ev_chunk,
    output wire        ev_unit,
    // the DOS and controller ROMs, written by the app through the window (sys_clk)
    input  wire        rom_wr,
    input  wire        rom_sel,             // 1 = 4040 set, 0 = 8250 set
    input  wire [14:0] rom_addr,
    input  wire [7:0]  rom_data,
    // the host's drive DDR2 port (cpu_clk)
    output wire        drv_tgl,
    output wire [25:0] drv_addr,
    output wire        drv_we,
    output wire [31:0] drv_wdata,
    output wire [3:0]  drv_be,
    input  wire        drv_done_tgl,
    input  wire [127:0] drv_rdata
);
    // ---- registers into sys_clk
    (* ASYNC_REG = "TRUE" *) reg [1:0] mt1 = 2'b00, mt2 = 2'b00, mt3 = 2'b00;
    (* ASYNC_REG = "TRUE" *) reg [1:0] ct1 = 2'b00, ct2 = 2'b00, ct3 = 2'b00;
    (* ASYNC_REG = "TRUE" *) reg [1:0] ro1 = 2'b00, ro2 = 2'b00;
    (* ASYNC_REG = "TRUE" *) reg [1:0] en1 = 2'b00, en2 = 2'b00;
    reg [31:0] sz0 = 32'd0, sz1 = 32'd0;
    always @(posedge sys_clk) begin
        mt1 <= r_mnt_tgl; mt2 <= mt1; mt3 <= mt2;
        ct1 <= r_clr_dirty_tgl; ct2 <= ct1; ct3 <= ct2;
        ro1 <= r_ro; ro2 <= ro1;
        en1 <= {r_4040, r_en}; en2 <= en1;
        sz0 <= r_size0; sz1 <= r_size1;
    end
    wire [1:0] mounted_p = mt3 ^ mt2;           // a mount strobe per drive, one clock
    wire [1:0] clr_dirty = ct3 ^ ct2;
    wire       d_en  = en2[0];
    wire       d_4040 = en2[1];

    // ---- the drive
    st_ieee_bus bus_pet, bus_drv;
    assign bus_pet = '{atn: p_atn_o, eoi: p_eoi_o, srq: 1'b1, ren: 1'b1, ifc: 1'b1, dav: p_dav_o, ndac: p_ndac_o, nrfd: p_nrfd_o, data: p_data_o};
    assign p_data_i = bus_drv.data;
    assign p_atn_i  = bus_drv.atn;
    assign p_ndac_i = bus_drv.ndac;
    assign p_nrfd_i = bus_drv.nrfd;
    assign p_dav_i  = bus_drv.dav;
    assign p_eoi_i  = bus_drv.eoi;

    wire [31:0] sd_lba [0:1];
    wire [5:0]  sd_blk_cnt [0:1];
    wire [1:0]  sd_rd, sd_wr, sd_ack;
    wire [12:0] sd_buff_addr;
    wire [7:0]  sd_buff_dout;
    wire [7:0]  sd_buff_din [0:1];
    wire        sd_buff_wr;
    wire [0:0]  led;
    wire [1:0]  mounted_img = mounted_p;
    // the size that goes with a mount strobe
    reg  [31:0] img_size = 32'd0;
    reg         img_ro = 1'b0;
    reg  [1:0]  img_mnt = 2'b00;
    always @(posedge sys_clk) begin
        img_mnt <= 2'b00;
        if (mounted_p[0]) begin img_size <= sz0; img_ro <= ro2[0]; img_mnt <= 2'b01; end
        else if (mounted_p[1]) begin img_size <= sz1; img_ro <= ro2[1]; img_mnt <= 2'b10; end
    end

    ieee_drive #(.DRIVES(1), .SUBDRV(2), .PAUSE_CTL(1)) drive (
        .CLK(32'd31500000),
        .clk_sys(sys_clk),
        .reset(rst | ~d_en),
        .pause(1'b0),
        .led(led),
        .bus_i(bus_pet),
        .bus_o(bus_drv),
        .drv_type(d_4040),
        .img_mounted(img_mnt),
        .img_size(img_size),
        .img_readonly(img_ro),
        .sd_lba(sd_lba),
        .sd_blk_cnt(sd_blk_cnt),
        .sd_rd(sd_rd),
        .sd_wr(sd_wr),
        .sd_ack(sd_ack),
        .sd_buff_addr(sd_buff_addr),
        .sd_buff_dout(sd_buff_dout),
        .sd_buff_din(sd_buff_din),
        .sd_buff_wr(sd_buff_wr),
        .rom_wr(rom_wr),
        .rom_sel(rom_sel),
        .rom_addr(rom_addr),
        .rom_data(rom_data)
    );
    assign s_led = {led[0], led[0]};
    assign s_led_err = 1'b0;

    pet_dblk #(.NU(2)) blk (
        .sys_clk(sys_clk), .cpu_clk(cpu_clk),
        .sd_lba({sd_lba[1], sd_lba[0]}), .sd_blk_cnt({sd_blk_cnt[1], sd_blk_cnt[0]}),
        .sd_rd(sd_rd), .sd_wr(sd_wr), .sd_ack(sd_ack),
        .sd_buff_addr(sd_buff_addr), .sd_buff_dout(sd_buff_dout), .sd_buff_wr(sd_buff_wr),
        .sd_buff_din({sd_buff_din[1], sd_buff_din[0]}),
        .base({r_base1, r_base0}), .dirty(s_dirty), .clr_dirty(clr_dirty), .ev_tgl(ev_tgl), .ev_chunk(ev_chunk), .ev_unit(ev_unit),
        .drv_tgl(drv_tgl), .drv_addr(drv_addr), .drv_we(drv_we), .drv_wdata(drv_wdata), .drv_be(drv_be),
        .drv_done_tgl(drv_done_tgl), .drv_rdata(drv_rdata)
    );
endmodule
