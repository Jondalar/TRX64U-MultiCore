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
    reg [7:0]  ctrl_r = 8'h00;
    reg [2:0]  model_r = 3'd0, cpux_r = 3'd0;
    reg        diag_r = 1'b0, hz_r = 1'b0;
    reg [1:0]  phos_r = 2'd0;
    reg [1:0]  ramsz_r = 2'd2;
    reg [3:0]  sel_r = 4'd0;
    reg [1:0]  spcpu_r = 2'd0;
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
                12'h010: irq_en <= wdata;
                12'h014: cause <= (cause & ~wdata) | ev;
                12'h0A0: ctrl_r <= wdata;
                12'h0A1: model_r <= wdata[2:0];
                12'h0A2: cpux_r <= wdata[2:0];
                12'h0A3: diag_r <= wdata[0];
                12'h0A5: hz_r <= wdata[0];
                12'h0A6: phos_r <= wdata[1:0];
                12'h0A7: ramsz_r <= wdata[1:0];
                12'h0AA: spcpu_r <= wdata[1:0];
                12'h0B0: sel_r <= wdata[3:0];
                12'h0C4: base_r[7:0] <= wdata;
                12'h0C5: base_r[15:8] <= wdata;
                12'h0C6: base_r[23:16] <= wdata;
                12'h0C7: base_r[31:24] <= wdata;
                12'h0C8: stride_r[7:0] <= wdata;
                12'h0C9: stride_r[15:8] <= wdata;
                12'h0CA: stride_r[23:16] <= wdata;
                12'h0CB: stride_r[31:24] <= wdata;
                12'h0F0: if (wdata[1]) lg_ovf <= 1'b0;
                12'h0F3: if (lg_n != 5'd0) lg_rp <= lg_rp + 5'd1;
                12'h140: tctrl_r <= wdata;
                12'h144: tbase_r[7:0] <= wdata;
                12'h145: tbase_r[15:8] <= wdata;
                12'h146: tbase_r[23:16] <= wdata;
                12'h147: tbase_r[31:24] <= wdata;
                12'h148: tpos_r[7:0] <= wdata;
                12'h149: tpos_r[15:8] <= wdata;
                12'h14A: tpos_r[23:16] <= wdata;
                12'h14B: tpos_r[24] <= wdata[0];
                12'h14C: tseek_t <= ~tseek_t;
                12'h152: tpin_r <= wdata;
                12'h0D0: dctl <= wdata[1:0];
                12'h0D1: dro <= wdata[1:0];
                12'h0D2: dmnt <= dmnt ^ wdata[1:0];
                12'h0D3: dclr <= dclr ^ wdata[1:0];
                12'h0D8: dsz0[7:0] <= wdata;
                12'h0D9: dsz0[15:8] <= wdata;
                12'h0DA: dsz0[23:16] <= wdata;
                12'h0DB: dsz0[31:24] <= wdata;
                12'h0DC: dsz1[7:0] <= wdata;
                12'h0DD: dsz1[15:8] <= wdata;
                12'h0DE: dsz1[23:16] <= wdata;
                12'h0DF: dsz1[31:24] <= wdata;
                12'h0E0: dbs0[7:0] <= wdata;
                12'h0E1: dbs0[15:8] <= wdata;
                12'h0E2: dbs0[23:16] <= wdata;
                12'h0E3: dbs0[31:24] <= wdata;
                12'h0E4: dbs1[7:0] <= wdata;
                12'h0E5: dbs1[15:8] <= wdata;
                12'h0E6: dbs1[23:16] <= wdata;
                12'h0E7: dbs1[31:24] <= wdata;
                default: ;
            endcase
        end

        q <= 8'h00;
        if (!win && page == 4'h0) begin
            case (off)
                12'h000: q <= id[7:0];
                12'h001: q <= id[15:8];
                12'h002: q <= id[23:16];
                12'h003: q <= id[31:24];
                12'h004: q <= 8'hC0;
                12'h005: q <= 8'h02;
                12'h008: q <= build[7:0];
                12'h009: q <= build[15:8];
                12'h00A: q <= build[23:16];
                12'h00B: q <= build[31:24];
                12'h010: q <= irq_en;
                12'h014: q <= cause;
                12'h020: q <= ddr_cal[7:0];
                12'h021: q <= ddr_cal[15:8];
                12'h022: q <= ddr_cal[23:16];
                12'h023: q <= ddr_cal[31:24];
                12'h024: q <= ddr_cal[39:32];
                12'h025: q <= ddr_cal[47:40];
                12'h026: q <= ddr_cal[55:48];
                12'h027: q <= ddr_cal[63:56];
                12'h0A0: q <= ctrl_r;
                12'h0A1: q <= {5'd0, model_r};
                12'h0A2: q <= {5'd0, cpux_r};
                12'h0A3: q <= {7'd0, diag_r};
                12'h0A5: q <= {7'd0, hz_r};
                12'h0A6: q <= {6'd0, phos_r};
                12'h0A7: q <= {6'd0, ramsz_r};
                12'h0A4: q <= s_q;
                12'h0A8: q <= f_q[7:0];
                12'h0A9: q <= f_q[15:8];
                12'h0AC: q <= c_q[7:0];
                12'h0AD: q <= c_q[15:8];
                12'h0AA: q <= {6'd0, spcpu_r};
                12'h0B0: q <= {4'd0, sel_r};
                12'h0C0: q <= l_q;
                12'h0C1: q <= n_q;
                12'h0C2: q <= o_q[7:0];
                12'h0C3: q <= o_q[15:8];
                12'h0C4: q <= base_r[7:0];
                12'h0C5: q <= base_r[15:8];
                12'h0C6: q <= base_r[23:16];
                12'h0C7: q <= base_r[31:24];
                12'h0C8: q <= stride_r[7:0];
                12'h0C9: q <= stride_r[15:8];
                12'h0CA: q <= stride_r[23:16];
                12'h0CB: q <= stride_r[31:24];
                12'h0F0: q <= {6'd0, lg_ovf, lg_n != 5'd0};
                12'h0F1: q <= lg_head[7:0];
                12'h0F2: q <= {7'd0, lg_head[8]};
                12'h140: q <= tctrl_r;
                12'h144: q <= tbase_r[7:0];
                12'h145: q <= tbase_r[15:8];
                12'h146: q <= tbase_r[23:16];
                12'h147: q <= tbase_r[31:24];
                12'h148: q <= t_pos[7:0];
                12'h149: q <= t_pos[15:8];
                12'h14A: q <= t_pos[23:16];
                12'h14B: q <= {7'd0, t_pos[24]};
                12'h150: q <= {4'd0, t_stat};
                12'h151: q <= t_under;
                12'h152: q <= tpin_r;
                12'h153: q <= {4'd0, tl_q};
                12'h0D0: q <= {6'd0, dctl};
                12'h0D1: q <= {6'd0, dro};
                12'h0D4: q <= {4'd0, ds_q};
                12'h0D8: q <= dsz0[7:0];
                12'h0D9: q <= dsz0[15:8];
                12'h0DA: q <= dsz0[23:16];
                12'h0DB: q <= dsz0[31:24];
                12'h0DC: q <= dsz1[7:0];
                12'h0DD: q <= dsz1[15:8];
                12'h0DE: q <= dsz1[23:16];
                12'h0DF: q <= dsz1[31:24];
                12'h0E0: q <= dbs0[7:0];
                12'h0E1: q <= dbs0[15:8];
                12'h0E2: q <= dbs0[23:16];
                12'h0E3: q <= dbs0[31:24];
                12'h0E4: q <= dbs1[7:0];
                12'h0E5: q <= dbs1[15:8];
                12'h0E6: q <= dbs1[23:16];
                12'h0E7: q <= dbs1[31:24];
                default: ;
            endcase
        end
    end
endmodule
