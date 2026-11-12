// The cw_* window of the c64u core: register map shared with the application (T103). Offsets are from 0x101A0000 (the
// host forwards 0x101Axxxx, 0x10048xxx and 0x1004Cxxx to the core as cw_addr[16:0]; bit 16 = the 0x1004C000 window).
// The application's copy of this table is software/io/c64u/c64u_cw.h in the c64u application branch; the values are the same.
`ifndef C64U_CW_VH
`define C64U_CW_VH
// page 0 (trx_io.v of the reference core, where it exists there)
`define CW_ID           16'h0000    // "C64U"
`define CW_CAPS         16'h0004    // R 0xD0: b4 cartridge page, b6 video page, b7 pixel layer RGB565
`define CW_CAPS2        16'h0005    // R 0x42: b1 BUILD, b6 turbo
`define CW_CAPS3        16'h0006    // R b0 reSID8 SID, b1 c64u map v1 (pages 9-D)
`define CW_BUILD        16'h0008
`define CW_IRQ_EN       16'h0010
`define CW_IRQ_CAUSE    16'h0014
`define CW_LEVEL_IRQ    16'h0018    // R b0 UCI, b1 drive A (WD177x), b2 0 (no drive B), b3 tape recorder (live levels)
`define CW_LEVEL_IRQ_EN 16'h0019    // RW
`define CW_DDR_CAL      16'h0020
`define CW_RS8          16'h0040
`define CW_CORE_CAPS    16'h0070    // R 32 bit: host_caps | the core's bits
`define CW_PIN_CTRL     16'h0078    // RW, reset 0: pins released and ignored (see c64u_io.v)
`define CW_SOCK         16'h0080    // T37: + Gideon's C64_IO_BASE offset of SID1/2_BASE (08/09), _MASK (0C/0D), _EN (11/12),
                                    // STEREO_ADDRSEL (14)
`define CW_SOCK_GAIN    16'h00A4    // T37: U64_AUDIO_MIXER bytes 4-7 (socket 1 R, L, socket 2 R, L)
// pages
`define CW_PAGE_CART    16'h4000
`define CW_PAGE_VIDEO   16'h6000
`define CW_PAGE_SLOT    16'h7000
`define CW_PAGE_SAMPLER 16'h8000
`define CW_UDP_TPL      16'h9000    // W 256 bytes, the streams' template RAM
`define CW_STREAM_ENA   16'h9100    // RW b0 video, b1 audio
`define CW_STREAM_MUTE  16'h9101    // RW b0
`define CW_UCI          16'hA000    // 8 KB
`define CW_C2N_PLAY     16'hC000    // 4 KB
`define CW_C2N_REC      16'hD000    // 4 KB
// the core's own bits of CORE_CAPS (Gideon's CAPAB_* numbering, system/itu.h)
`define CAPAB_DRIVE_1541_2  32'h00000004
`define CAPAB_HARDWARE_GCR  32'h00000010
`define CAPAB_C2N_STREAMER  32'h00000080
`define CAPAB_C2N_RECORDER  32'h00000100
`define CAPAB_RAM_EXPANSION 32'h00000400
`define CAPAB_MM_DRIVE      32'h00000800
`define CAPAB_EXTENDED_REU  32'h00010000
`define CAPAB_COMMAND_INTF  32'h00040000
`endif
