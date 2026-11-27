// T134 (from cores/c264/rtl/trx/pet_regs.v, T90, same author): the register block the app sees at its core window (cw_*, cpu_clk, the bytes at
// 0x101A0000; the same window trx_io.v answers for the C64 core). Written for this project (TREX), no upstream code.
//   000-003 ID        "PET1"
//   004 CAPS 0xC0     b6 page 6 (pixel layer), b7 RGB565 pixel layer: the host's, forwarded below
//   005 CAPS2 0x02    b1 BUILD is there
//   008-00B BUILD     the bitstream's build stamp (USR_ACCESS)
//   010 IRQ_EN, 014 IRQ_CAUSE (W1C)   b7 page flip done (px_ev_flip), b6 video frame start (px_ev_vsync), b5 a PET frame is complete
//   020-027 DDR_CAL   the host's DDR2 calibration, as for the C64 core
//   0A0 CTRL     RW   b0 reset hold, b1 halt
//   0A1 MODEL    RW   0 2001, 1 3032, 2 4032, 3 8032, 4 8096, 5 SuperPET (taken at the core's next reset)
//   0A2 CPU_X    RW   0..7 = CPU speed x1..x8 (live)
//   0A3 DIAG     RW   b0 the DIAG jumper (PIA1 PA7 low): the diagnostic ROM path of the editor ROMs
//   0A4 STATUS   R    b0 machine in reset, b1 held (Freeze), b2 80 column picture
//   0A5 HZ       RW   b0 0 = 60 Hz picture, 1 = 50 Hz
//   0A6 PHOSPHOR RW   0 green, 1 amber, 2 white
//   0A7 RAMSZ    RW   b1:0 CPU RAM below $8000: 0 8 KiB, 1 16 KiB, 2 or 3 32 KiB (taken at the core's next reset, like MODEL; the 8096's extra 64 KiB comes with MODEL 4)
//   0A8-0A9 FRAMES    R   PET frame counter (low 16 bits)
//   0AA SP_CPU   RW   b1:0 the SuperPET's CPU switch: 0 6502, 1 6809, 2 PROG (6502 at reset, the program switches through $EFF8 b0); taken at the core's next reset, like MODEL
//   0AB KEYMODE RW   b0 the keyboard mapping (T216 P10): 0 symbolic (the C64 key legend counts), 1 positional (the PET key at the same place); live
//   0AE SP_RAM  RW   b0 the SuperPET has its 64 KiB bank RAM (96 KiB with the 32 KiB below $8000, T216 P6) or not (32 KiB: ROM behind $9000); taken at the core's next reset
//   0AC-0AD CPUADDR   R   the CPU's address bus (a sample, read twice to be sure)
//   0B0 ROM_SEL  RW   b3:0 what is behind the app's 16 KB aperture: 3 the Waterloo ROM $A000-$DFFF (SuperPET, 16 KB), 8 the Waterloo ROM $E000-$FFFF (8 KB), 0 ROM space $8000-$BFFF, 1 ROM space $C000-$FFFF, 2 character ROM (2 KB), 4 / 5 the 4040 drive's DOS (0x1000-0x3FFF of the first half, a 12 KB DOS goes to 0x1000) / controller ROM (first 2 KB of the second half),
//                    6 / 7 the same for the 8250 (16 KB DOS at 0); write only
//   0C0 FB_LATEST R   b7 valid, b1:0 the buffer of the newest complete frame
//   0C1 FRAME_SEQ R   8 bit counter that changes with every complete frame
//   0C2-0C3 FB_OVERRUNS R   lines that could not be handed to the DDR2 side in time
//   0C4-0C7 FB_BASE   RW  DDR2 byte address of frame buffer 0 (default 0x01D00000: the REU block of the host, as the C264's)
//   0C8-0CB FB_STRIDE RW  bytes between the four frame buffers (default 0x00100000)
//   0D0 DRV_CTL RW   b0 device 8 on, b1 drive type 1 = 4040 (D64) 0 = 8250 (D80 / D82)
//   0D1 DRV_RO  RW   b0 / b1 the image of drive 0 / 1 is write protected
//   0D2 DRV_MNT W    b0 / b1: toggles the mount strobe of drive 0 / 1 (the drive takes size and read-only flag at the strobe; size 0 = no disk)
//   0D3 DRV_CLR W    b0 / b1: clears the dirty flag of drive 0 / 1
//   0D4 DRV_ST  R    b1:0 dirty (the drive wrote to its image in DDR2: the app saves it), b3:2 drive activity
//   0D8-0DB SIZE0, 0DC-0DF SIZE1 RW   image size in bytes     0E0-0E3 BASE0, 0E4-0E7 BASE1 RW   DDR2 byte address of the image (default 0x01100000 / 0x01220000, the app's DDR2 window slots 0 and 1)
//   0F0 DLOG_ST R/W1C b0 the log has an entry, b1 it overflowed (write b1 to clear)   0F1 DLOG_TRACK R   the 8 KiB chunk of the image the oldest entry names
//   0F2 DLOG_DRIVE R   the drive (0 / 1) of it   0F3 DLOG_POP W   any write drops the oldest entry     (the write log of the app's window media: the drive wrote there)
//   140 TAPE_CTRL RW b0 play (a rising edge starts the tape, the motor gates it)   144-147 TAPE_BASE RW DDR2 byte address of the pulse stream (default 0x01800000)
//   148-14B TAPE_POS  W: the word position a seek loads, R: the position now   14C TAPE_SEEK W: any write loads TAPE_POS
//   150 TAPE_STATUS R b0 playing, b1 end of tape, b2 stop marker, b3 DDR2 read stuck   151 TAPE_UNDERRUN R
//   152 TAPE_PIN RW  b0 motor pin driven, b1 write pin driven, b3 SENSE pulled low while the tape plays (the pins are released at 0), b4 the datasette's READ and SENSE pins are the PET's inputs
//   153 TAPE_LINES R  the PET side of the port: b0 motor on (PIA1 CB2 low), b1 SENSE low (PA4), b2 READ line (PIA1 CA1), b3 WRITE line (VIA PB3)
//   160-161 PAD1, 162-163 PAD2 RW   the USB pads' button words (T156 service, T216 P4): b0 up, b1 down, b2 left, b3 right, b4 fire; low byte first
//   164-168 PADKEY0-4 RW   the C64 key each of the five buttons presses (b5:0 the key's matrix bit col*8+row, b6 with C64 SHIFT, 0xFF none); the joystick ports 1 and 2 act as pads too
//   page 6 (0x6000-0x6FFF)   forwarded to the host's pixel layer (px_we, px_re, px_rdata)
//   everything else reads 0 and is acknowledged; the EEPROM window (addr[16]) is not here.
`timescale 1ns / 1ps
module pet_regs (
    input  wire        clk,                         // cpu_clk
    input  wire        we,
    input  wire        re,
    input  wire [16:0] addr,
    input  wire [7:0]  wdata,
    output wire [7:0]  rdata,
    output wire        ack,
    output wire        irq,
    input  wire [31:0] build,
    input  wire [63:0] ddr_cal,
    output wire        px_we,
    output wire        px_re,
    input  wire [7:0]  px_rdata,
    input  wire        ev_vsync,
    input  wire        ev_flip,
    input  wire        ev_frame,                    // a TED frame is complete (any clock rate: a toggle seen from sys_clk is synchronised by the caller)
    // the core's side (the values cross from and to sys_clk through the caller's synchronisers)
    output wire [7:0]  ctrl,
    output wire [2:0]  model,
    output wire [2:0]  cpu_x,
    output wire        diag,
    output wire        hz50,
    output wire [1:0]  phos,
    output wire [1:0]  ram_sz,
    output wire [3:0]  rom_sel,
    output wire [1:0]  sp_cpu,
    output wire        keymode,
    output wire        sp_ram,
    output wire [15:0] pad1,
    output wire [15:0] pad2,
    output wire [39:0] padkey,
    output wire [31:0] fb_base,
    output wire [31:0] fb_stride,
    input  wire [7:0]  status,
    input  wire [15:0] frames,
    input  wire [15:0] cpuaddr,
    input  wire [7:0]  fb_latest,
    input  wire [7:0]  fb_seq,
    input  wire [15:0] fb_over,
    output wire        d_en,
    output wire        d_4040,
    output wire [1:0]  d_ro,
    output wire [1:0]  d_mnt_tgl,
    output wire [1:0]  d_clr_tgl,
    output wire [31:0] d_size0,
    output wire [31:0] d_size1,
    output wire [31:0] d_base0,
    output wire [31:0] d_base1,
    input  wire [3:0]  d_status,
    input  wire        d_ev_tgl,                    // sys_clk: the drives' write log
    input  wire [7:0]  d_ev_chunk,
    input  wire        d_ev_unit,
    // cassette 1 (the pulse engine, zxcpc_tape.v) and the cassette port pins
    output wire        t_play,
    output wire [31:0] t_base,
    output wire [24:0] t_seek_pos,
    output wire        t_seek_tgl,
    output wire [7:0]  t_pin,
    input  wire [24:0] t_pos,
    input  wire [3:0]  t_stat,
    input  wire [7:0]  t_under,
    input  wire [3:0]  t_lines
);
    // >>> generated from cores/pet/app/gateware.toml (T264 S2): do not edit, `tools/toml_a_gen.py gen` rewrites it
    localparam [11:0] R_IDENT_ID = 12'h000;
    localparam [11:0] R_IDENT_CAPS = 12'h004;
    localparam [11:0] R_IDENT_CAPS2 = 12'h005;
    localparam [11:0] R_IDENT_BUILD = 12'h008;
    localparam [11:0] R_IRQ_IRQ_EN = 12'h010;
    localparam [11:0] R_IRQ_IRQ_CAUSE = 12'h014;
    localparam [11:0] R_DDR_CAL_DDR_CAL0 = 12'h020;
    localparam [11:0] R_DDR_CAL_DDR_CAL1 = 12'h024;
    localparam [11:0] R_MACHINE_CTRL = 12'h0A0;
    localparam [11:0] R_MACHINE_MODEL = 12'h0A1;
    localparam [11:0] R_MACHINE_CPU_X = 12'h0A2;
    localparam [11:0] R_MACHINE_DIAG = 12'h0A3;
    localparam [11:0] R_MACHINE_STATUS = 12'h0A4;
    localparam [11:0] R_MACHINE_HZ = 12'h0A5;
    localparam [11:0] R_MACHINE_PHOSPHOR = 12'h0A6;
    localparam [11:0] R_MACHINE_RAMSZ = 12'h0A7;
    localparam [11:0] R_MACHINE_FRAMES = 12'h0A8;
    localparam [11:0] R_MACHINE_SP_CPU = 12'h0AA;
    localparam [11:0] R_MACHINE_KEYMODE = 12'h0AB;
    localparam [11:0] R_MACHINE_CPUADDR = 12'h0AC;
    localparam [11:0] R_MACHINE_SP_RAM = 12'h0AE;
    localparam [11:0] R_ROM_ROM_SEL = 12'h0B0;
    localparam [11:0] R_VIDEO_FB_LATEST = 12'h0C0;
    localparam [11:0] R_VIDEO_FRAME_SEQ = 12'h0C1;
    localparam [11:0] R_VIDEO_FB_OVERRUNS = 12'h0C2;
    localparam [11:0] R_VIDEO_FB_BASE = 12'h0C4;
    localparam [11:0] R_VIDEO_FB_STRIDE = 12'h0C8;
    localparam [11:0] R_DRIVE_CTL = 12'h0D0;
    localparam [11:0] R_DRIVE_RO = 12'h0D1;
    localparam [11:0] R_DRIVE_MNT = 12'h0D2;
    localparam [11:0] R_DRIVE_CLR = 12'h0D3;
    localparam [11:0] R_DRIVE_STATUS = 12'h0D4;
    localparam [11:0] R_DRIVE_SIZE0 = 12'h0D8;
    localparam [11:0] R_DRIVE_SIZE1 = 12'h0DC;
    localparam [11:0] R_DRIVE_BASE0 = 12'h0E0;
    localparam [11:0] R_DRIVE_BASE1 = 12'h0E4;
    localparam [11:0] R_DLOG_LOG_ST = 12'h0F0;
    localparam [11:0] R_DLOG_LOG_TRACK = 12'h0F1;
    localparam [11:0] R_DLOG_LOG_DRIVE = 12'h0F2;
    localparam [11:0] R_DLOG_LOG_POP = 12'h0F3;
    localparam [11:0] R_TAPE_CTRL = 12'h140;
    localparam [11:0] R_TAPE_BASE = 12'h144;
    localparam [11:0] R_TAPE_POS = 12'h148;
    localparam [11:0] R_TAPE_SEEK = 12'h14C;
    localparam [11:0] R_TAPE_STATUS = 12'h150;
    localparam [11:0] R_TAPE_UNDERRUN = 12'h151;
    localparam [11:0] R_TAPE_PIN = 12'h152;
    localparam [11:0] R_TAPE_LINES = 12'h153;
    localparam [11:0] R_PADS_PAD1 = 12'h160;
    localparam [11:0] R_PADS_PAD2 = 12'h162;
    localparam [11:0] R_PADS_PADKEY0 = 12'h164;
    localparam [11:0] R_PADS_PADKEY1 = 12'h165;
    localparam [11:0] R_PADS_PADKEY2 = 12'h166;
    localparam [11:0] R_PADS_PADKEY3 = 12'h167;
    localparam [11:0] R_PADS_PADKEY4 = 12'h168;
    // <<< generated
    reg [7:0]  ctrl_r = 8'h00;
    reg [2:0]  model_r = 3'd0, cpux_r = 3'd0;
    reg        diag_r = 1'b0, hz_r = 1'b0;
    reg [1:0]  phos_r = 2'd0;
    reg [1:0]  ramsz_r = 2'd2;
    reg [3:0]  sel_r = 4'd0;
    reg [1:0]  spcpu_r = 2'd0;
    reg        keymode_r = 1'b0, spram_r = 1'b1;
    reg [15:0] pad1_r = 16'd0, pad2_r = 16'd0;
    reg [39:0] padkey_r = {5{8'hFF}};
    reg [31:0] base_r = 32'h01D00000;
    reg [31:0] stride_r = 32'h00100000;
    reg [7:0]  irq_en = 8'd0, cause = 8'd0;
    reg [1:0]  dctl = 2'b00, dro = 2'b00, dmnt = 2'b00, dclr = 2'b00;
    reg [31:0] dsz0 = 32'd0, dsz1 = 32'd0, dbs0 = 32'h01100000, dbs1 = 32'h01220000;
    reg [3:0]  ds_a = 4'd0, ds_b = 4'd0, ds_q = 4'd0;
    // the write log: a toggle from sys_clk with its payload stable before it, 16 entries
    (* ASYNC_REG = "TRUE" *) reg [2:0] ev_s = 3'b000;
    reg [8:0]  lg [0:15];
    reg [4:0]  lg_wp = 5'd0, lg_rp = 5'd0;
    reg        lg_ovf = 1'b0;
    wire [4:0] lg_n = lg_wp - lg_rp;
    wire [8:0] lg_head = lg[lg_rp[3:0]];
    assign d_en = dctl[0]; assign d_4040 = dctl[1]; assign d_ro = dro; assign d_mnt_tgl = dmnt; assign d_clr_tgl = dclr;
    assign d_size0 = dsz0; assign d_size1 = dsz1; assign d_base0 = dbs0; assign d_base1 = dbs1;
    reg [7:0]  tctrl_r = 8'h00, tpin_r = 8'h00;
    reg [31:0] tbase_r = 32'h01800000;
    reg [24:0] tpos_r = 25'd0;
    reg        tseek_t = 1'b0;
    reg [3:0]  tl_a = 4'd0, tl_b = 4'd0, tl_q = 4'd0;
    assign t_play = tctrl_r[0]; assign t_base = tbase_r; assign t_seek_pos = tpos_r; assign t_seek_tgl = tseek_t; assign t_pin = tpin_r;
    reg [7:0]  q = 8'd0;
    reg        q_p6 = 1'b0, ack_r = 1'b0;
    // the sys_clk values, accepted when two samples in a row agree
    reg [15:0] f_a = 16'd0, f_b = 16'd0, f_q = 16'd0;
    reg [15:0] c_a = 16'd0, c_b = 16'd0, c_q = 16'd0;
    reg [7:0]  s_a = 8'd0, s_b = 8'd0, s_q = 8'd0;
    reg [7:0]  l_a = 8'd0, l_b = 8'd0, l_q = 8'd0;
    reg [7:0]  n_a = 8'd0, n_b = 8'd0, n_q = 8'd0;
    reg [15:0] o_a = 16'd0, o_b = 16'd0, o_q = 16'd0;
    wire       win  = addr[16];
    wire [3:0] page = addr[15:12];
    wire [11:0] off = addr[11:0];
    wire [7:0] ev = {ev_flip, ev_vsync, ev_frame, 5'b00000};

    assign px_we = we & ~win & (page == 4'h6);
    assign px_re = re & ~win & (page == 4'h6);
    assign ctrl = ctrl_r;
    assign model = model_r;
    assign cpu_x = cpux_r;
    assign diag = diag_r;
    assign hz50 = hz_r;
    assign phos = phos_r;
    assign ram_sz = ramsz_r;
    assign rom_sel = sel_r;
    assign sp_cpu = spcpu_r;
    assign keymode = keymode_r;
    assign sp_ram = spram_r;
    assign pad1 = pad1_r;
    assign pad2 = pad2_r;
    assign padkey = padkey_r;
    assign fb_base = base_r;
    assign fb_stride = stride_r;
    assign ack = ack_r;
    assign irq = |(irq_en & cause);
    assign rdata = q_p6 ? px_rdata : q;

    wire [31:0] id = 32'h31544550;   // bytes "TRX2" / "C264", byte 0 first

    always @(posedge clk) begin
        f_a <= frames;   f_b <= f_a;   if (f_a == f_b) f_q <= f_b;
        c_a <= cpuaddr;  c_b <= c_a;   if (c_a == c_b) c_q <= c_b;
        s_a <= status;   s_b <= s_a;   if (s_a == s_b) s_q <= s_b;
        l_a <= fb_latest; l_b <= l_a;  if (l_a == l_b) l_q <= l_b;
        n_a <= fb_seq;   n_b <= n_a;   if (n_a == n_b) n_q <= n_b;
        ev_s <= {ev_s[1:0], d_ev_tgl};
        if (ev_s[2] != ev_s[1]) begin
            if (lg_n == 5'd16) lg_ovf <= 1'b1;
            else begin lg[lg_wp[3:0]] <= {d_ev_unit, d_ev_chunk}; lg_wp <= lg_wp + 5'd1; end
        end
        tl_a <= t_lines; tl_b <= tl_a; if (tl_a == tl_b) tl_q <= tl_b;
        ds_a <= d_status; ds_b <= ds_a; if (ds_a == ds_b) ds_q <= ds_b;
        o_a <= fb_over;  o_b <= o_a;   if (o_a == o_b) o_q <= o_b;
        cause <= cause | ev;
        ack_r <= 1'b0;
        if (re | we) ack_r <= 1'b1;
        q_p6 <= 1'b0;
        if (re && !win && page == 4'h6) q_p6 <= 1'b1;

        if (we && !win && page == 4'h0) begin
            case (off)
                R_IRQ_IRQ_EN: irq_en <= wdata;
                R_IRQ_IRQ_CAUSE: cause <= (cause & ~wdata) | ev;
                R_MACHINE_CTRL: ctrl_r <= wdata;
                R_MACHINE_MODEL: model_r <= wdata[2:0];
                R_MACHINE_CPU_X: cpux_r <= wdata[2:0];
                R_MACHINE_DIAG: diag_r <= wdata[0];
                R_MACHINE_HZ: hz_r <= wdata[0];
                R_MACHINE_PHOSPHOR: phos_r <= wdata[1:0];
                R_MACHINE_RAMSZ: ramsz_r <= wdata[1:0];
                R_MACHINE_SP_CPU: spcpu_r <= wdata[1:0];
                R_MACHINE_KEYMODE: keymode_r <= wdata[0];
                R_MACHINE_SP_RAM: spram_r <= wdata[0];
                R_PADS_PAD1: pad1_r[7:0] <= wdata;
                R_PADS_PAD1 + 12'd1: pad1_r[15:8] <= wdata;
                R_PADS_PAD2: pad2_r[7:0] <= wdata;
                R_PADS_PAD2 + 12'd1: pad2_r[15:8] <= wdata;
                R_PADS_PADKEY0: padkey_r[7:0] <= wdata;
                R_PADS_PADKEY1: padkey_r[15:8] <= wdata;
                R_PADS_PADKEY2: padkey_r[23:16] <= wdata;
                R_PADS_PADKEY3: padkey_r[31:24] <= wdata;
                R_PADS_PADKEY4: padkey_r[39:32] <= wdata;
                R_ROM_ROM_SEL: sel_r <= wdata[3:0];
                R_VIDEO_FB_BASE: base_r[7:0] <= wdata;
                R_VIDEO_FB_BASE + 12'd1: base_r[15:8] <= wdata;
                R_VIDEO_FB_BASE + 12'd2: base_r[23:16] <= wdata;
                R_VIDEO_FB_BASE + 12'd3: base_r[31:24] <= wdata;
                R_VIDEO_FB_STRIDE: stride_r[7:0] <= wdata;
                R_VIDEO_FB_STRIDE + 12'd1: stride_r[15:8] <= wdata;
                R_VIDEO_FB_STRIDE + 12'd2: stride_r[23:16] <= wdata;
                R_VIDEO_FB_STRIDE + 12'd3: stride_r[31:24] <= wdata;
                R_DLOG_LOG_ST: if (wdata[1]) lg_ovf <= 1'b0;
                R_DLOG_LOG_POP: if (lg_n != 5'd0) lg_rp <= lg_rp + 5'd1;
                R_TAPE_CTRL: tctrl_r <= wdata;
                R_TAPE_BASE: tbase_r[7:0] <= wdata;
                R_TAPE_BASE + 12'd1: tbase_r[15:8] <= wdata;
                R_TAPE_BASE + 12'd2: tbase_r[23:16] <= wdata;
                R_TAPE_BASE + 12'd3: tbase_r[31:24] <= wdata;
                R_TAPE_POS: tpos_r[7:0] <= wdata;
                R_TAPE_POS + 12'd1: tpos_r[15:8] <= wdata;
                R_TAPE_POS + 12'd2: tpos_r[23:16] <= wdata;
                R_TAPE_POS + 12'd3: tpos_r[24] <= wdata[0];
                R_TAPE_SEEK: tseek_t <= ~tseek_t;
                R_TAPE_PIN: tpin_r <= wdata;
                R_DRIVE_CTL: dctl <= wdata[1:0];
                R_DRIVE_RO: dro <= wdata[1:0];
                R_DRIVE_MNT: dmnt <= dmnt ^ wdata[1:0];
                R_DRIVE_CLR: dclr <= dclr ^ wdata[1:0];
                R_DRIVE_SIZE0: dsz0[7:0] <= wdata;
                R_DRIVE_SIZE0 + 12'd1: dsz0[15:8] <= wdata;
                R_DRIVE_SIZE0 + 12'd2: dsz0[23:16] <= wdata;
                R_DRIVE_SIZE0 + 12'd3: dsz0[31:24] <= wdata;
                R_DRIVE_SIZE1: dsz1[7:0] <= wdata;
                R_DRIVE_SIZE1 + 12'd1: dsz1[15:8] <= wdata;
                R_DRIVE_SIZE1 + 12'd2: dsz1[23:16] <= wdata;
                R_DRIVE_SIZE1 + 12'd3: dsz1[31:24] <= wdata;
                R_DRIVE_BASE0: dbs0[7:0] <= wdata;
                R_DRIVE_BASE0 + 12'd1: dbs0[15:8] <= wdata;
                R_DRIVE_BASE0 + 12'd2: dbs0[23:16] <= wdata;
                R_DRIVE_BASE0 + 12'd3: dbs0[31:24] <= wdata;
                R_DRIVE_BASE1: dbs1[7:0] <= wdata;
                R_DRIVE_BASE1 + 12'd1: dbs1[15:8] <= wdata;
                R_DRIVE_BASE1 + 12'd2: dbs1[23:16] <= wdata;
                R_DRIVE_BASE1 + 12'd3: dbs1[31:24] <= wdata;
                default: ;
            endcase
        end

        q <= 8'h00;
        if (!win && page == 4'h0) begin
            case (off)
                R_IDENT_ID: q <= id[7:0];
                R_IDENT_ID + 12'd1: q <= id[15:8];
                R_IDENT_ID + 12'd2: q <= id[23:16];
                R_IDENT_ID + 12'd3: q <= id[31:24];
                R_IDENT_CAPS: q <= 8'hC0;
                R_IDENT_CAPS2: q <= 8'h02;
                R_IDENT_BUILD: q <= build[7:0];
                R_IDENT_BUILD + 12'd1: q <= build[15:8];
                R_IDENT_BUILD + 12'd2: q <= build[23:16];
                R_IDENT_BUILD + 12'd3: q <= build[31:24];
                R_IRQ_IRQ_EN: q <= irq_en;
                R_IRQ_IRQ_CAUSE: q <= cause;
                R_DDR_CAL_DDR_CAL0: q <= ddr_cal[7:0];
                R_DDR_CAL_DDR_CAL0 + 12'd1: q <= ddr_cal[15:8];
                R_DDR_CAL_DDR_CAL0 + 12'd2: q <= ddr_cal[23:16];
                R_DDR_CAL_DDR_CAL0 + 12'd3: q <= ddr_cal[31:24];
                R_DDR_CAL_DDR_CAL1: q <= ddr_cal[39:32];
                R_DDR_CAL_DDR_CAL1 + 12'd1: q <= ddr_cal[47:40];
                R_DDR_CAL_DDR_CAL1 + 12'd2: q <= ddr_cal[55:48];
                R_DDR_CAL_DDR_CAL1 + 12'd3: q <= ddr_cal[63:56];
                R_MACHINE_CTRL: q <= ctrl_r;
                R_MACHINE_MODEL: q <= {5'd0, model_r};
                R_MACHINE_CPU_X: q <= {5'd0, cpux_r};
                R_MACHINE_DIAG: q <= {7'd0, diag_r};
                R_MACHINE_HZ: q <= {7'd0, hz_r};
                R_MACHINE_PHOSPHOR: q <= {6'd0, phos_r};
                R_MACHINE_RAMSZ: q <= {6'd0, ramsz_r};
                R_MACHINE_STATUS: q <= s_q;
                R_MACHINE_FRAMES: q <= f_q[7:0];
                R_MACHINE_FRAMES + 12'd1: q <= f_q[15:8];
                R_MACHINE_CPUADDR: q <= c_q[7:0];
                R_MACHINE_CPUADDR + 12'd1: q <= c_q[15:8];
                R_MACHINE_SP_CPU: q <= {6'd0, spcpu_r};
                R_MACHINE_KEYMODE: q <= {7'd0, keymode_r};
                R_MACHINE_SP_RAM: q <= {7'd0, spram_r};
                R_PADS_PAD1: q <= pad1_r[7:0];
                R_PADS_PAD1 + 12'd1: q <= pad1_r[15:8];
                R_PADS_PAD2: q <= pad2_r[7:0];
                R_PADS_PAD2 + 12'd1: q <= pad2_r[15:8];
                R_PADS_PADKEY0: q <= padkey_r[7:0];
                R_PADS_PADKEY1: q <= padkey_r[15:8];
                R_PADS_PADKEY2: q <= padkey_r[23:16];
                R_PADS_PADKEY3: q <= padkey_r[31:24];
                R_PADS_PADKEY4: q <= padkey_r[39:32];
                R_ROM_ROM_SEL: q <= {4'd0, sel_r};
                R_VIDEO_FB_LATEST: q <= l_q;
                R_VIDEO_FRAME_SEQ: q <= n_q;
                R_VIDEO_FB_OVERRUNS: q <= o_q[7:0];
                R_VIDEO_FB_OVERRUNS + 12'd1: q <= o_q[15:8];
                R_VIDEO_FB_BASE: q <= base_r[7:0];
                R_VIDEO_FB_BASE + 12'd1: q <= base_r[15:8];
                R_VIDEO_FB_BASE + 12'd2: q <= base_r[23:16];
                R_VIDEO_FB_BASE + 12'd3: q <= base_r[31:24];
                R_VIDEO_FB_STRIDE: q <= stride_r[7:0];
                R_VIDEO_FB_STRIDE + 12'd1: q <= stride_r[15:8];
                R_VIDEO_FB_STRIDE + 12'd2: q <= stride_r[23:16];
                R_VIDEO_FB_STRIDE + 12'd3: q <= stride_r[31:24];
                R_DLOG_LOG_ST: q <= {6'd0, lg_ovf, lg_n != 5'd0};
                R_DLOG_LOG_TRACK: q <= lg_head[7:0];
                R_DLOG_LOG_DRIVE: q <= {7'd0, lg_head[8]};
                R_TAPE_CTRL: q <= tctrl_r;
                R_TAPE_BASE: q <= tbase_r[7:0];
                R_TAPE_BASE + 12'd1: q <= tbase_r[15:8];
                R_TAPE_BASE + 12'd2: q <= tbase_r[23:16];
                R_TAPE_BASE + 12'd3: q <= tbase_r[31:24];
                R_TAPE_POS: q <= t_pos[7:0];
                R_TAPE_POS + 12'd1: q <= t_pos[15:8];
                R_TAPE_POS + 12'd2: q <= t_pos[23:16];
                R_TAPE_POS + 12'd3: q <= {7'd0, t_pos[24]};
                R_TAPE_STATUS: q <= {4'd0, t_stat};
                R_TAPE_UNDERRUN: q <= t_under;
                R_TAPE_PIN: q <= tpin_r;
                R_TAPE_LINES: q <= {4'd0, tl_q};
                R_DRIVE_CTL: q <= {6'd0, dctl};
                R_DRIVE_RO: q <= {6'd0, dro};
                R_DRIVE_STATUS: q <= {4'd0, ds_q};
                R_DRIVE_SIZE0: q <= dsz0[7:0];
                R_DRIVE_SIZE0 + 12'd1: q <= dsz0[15:8];
                R_DRIVE_SIZE0 + 12'd2: q <= dsz0[23:16];
                R_DRIVE_SIZE0 + 12'd3: q <= dsz0[31:24];
                R_DRIVE_SIZE1: q <= dsz1[7:0];
                R_DRIVE_SIZE1 + 12'd1: q <= dsz1[15:8];
                R_DRIVE_SIZE1 + 12'd2: q <= dsz1[23:16];
                R_DRIVE_SIZE1 + 12'd3: q <= dsz1[31:24];
                R_DRIVE_BASE0: q <= dbs0[7:0];
                R_DRIVE_BASE0 + 12'd1: q <= dbs0[15:8];
                R_DRIVE_BASE0 + 12'd2: q <= dbs0[23:16];
                R_DRIVE_BASE0 + 12'd3: q <= dbs0[31:24];
                R_DRIVE_BASE1: q <= dbs1[7:0];
                R_DRIVE_BASE1 + 12'd1: q <= dbs1[15:8];
                R_DRIVE_BASE1 + 12'd2: q <= dbs1[23:16];
                R_DRIVE_BASE1 + 12'd3: q <= dbs1[31:24];
                default: ;
            endcase
        end
    end
endmodule
