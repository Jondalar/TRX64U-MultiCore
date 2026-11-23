// T113: the C128's address map in front of the C64 core's bus arbiter (c64_bus_arbiter_c128.v).
// Written for this project from the machine's documented behaviour (the MMU registers, the ROM banks, the I/O page, the Z80's BIOS
// window) and checked against the MiSTer core's fpga64_buslogic.vhd as a reference only; no line of it is copied.
// C128 mode (c128_n = 0): the MMU (mmu8722) translates the address ($0000/$0100 relocation, common RAM, the RAM block of CR[7:6]) and
// says which of system ROM, internal function ROM, external ROM or RAM a read of $4000-$BFFF, $C000-$CFFF and $E000-$FFFF sees
// (CR[5:2]); $D000-$DFFF is I/O (CR[0] = 0) or, for a read, the character ROM / RAM. $FF00-$FF04 are the MMU's load registers in
// every configuration. The Z80 (z80_n = 0) has its BIOS window at $0000-$0FFF (the MMU gives it tAddr $Dxxx), colour RAM at
// $1000-$13FF, no I/O in the $D000 page except through IN/OUT (z80io = 1, the address is BC). C64 mode (c128_n = 1): the C64's
// PLA decides (c64_pla_c128.v), the MMU still supplies the RAM block.
// ROM image layout in 4 KiB banks (5 bits, spec 5.5): 0-1 C64 BASIC, 2-3 C64 KERNAL, 4 C128 editor ($C000), 5 Z80 BIOS (the image of
// $D000-$DFFF), 6-7 C128 KERNAL ($E000), 8-11 C128 BASIC low ($4000-$7FFF), 12-15 C128 BASIC high ($8000-$BFFF), 16 C64 character
// ROM, 17 C128 character ROM.
`timescale 1ns / 1ps
module c128_map (
    input  wire        clk,
    input  wire        rst,
    input  wire        mmu_en,          // one pulse per CPU cycle, where a register write takes effect
    input  wire [15:0] bus_addr,
    input  wire        bus_we,
    input  wire        ba,
    input  wire [7:0]  cpu_dout,
    input  wire        z80io,           // a Z80 IN/OUT cycle (the address is BC)
    input  wire        z80_act,         // the Z80 owns the bus (it has the bus: not the MMU's wish, the handed-over state)
    input  wire        cart_game,       // the cartridge port's lines (already "no cartridge = high")
    input  wire        cart_exrom,
    input  wire        loram, hiram, charen,
    input  wire        d4080_n,         // the 40/80 DISPLAY key: 0 = pressed (80 columns), as the pin of the MMU
    input  wire        boot64,          // reset state: 1 = C64 mode on the 8502 (the machine starts like a C64)
    input  wire        z80_first,       // reset state of the CPU: 1 = the Z80 starts (MMU CPU bit = 0)
    // PLA-compatible outputs (the arbiter's selects)
    output reg         io, ram, basic, kernal, chr, roml, romh,
    // extras
    output reg  [4:0]  rom_bank,        // 4 KiB bank of the system ROM image for basic/kernal/chr
    output wire        ram_bank,        // RAM block of this CPU cycle
    output wire [15:0] t_addr,          // translated address (page relocation)
    output wire        vic_bank,        // RAM block the VIC reads
    output wire        c128_n,
    output wire        fsdir,           // $D505 bit 3, the fast serial direction (1 = the machine sends; reset: cpumode or osmode, c128_mmu8722.vhd:112)
    output wire        z80_n,           // the MMU's CPU bit as the machine has it (0: the Z80 is selected)
    output reg         cs_mmu_lo,       // $D500-$D5FF with I/O mapped (or written with I/O mapped)
    output reg         cs_mmu_hi,       // $FF00-$FF04
    output reg         cs_vdc,          // $D600-$D6FF with I/O mapped
    output reg         cs_color_mem,    // Z80: colour RAM at $1000-$13FF
    output reg         cs_ff,           // an access that selects nothing the machine has (internal function ROM: the bus floats)
    output wire [7:0]  mmu_do,
    output wire        mmu_game, mmu_exrom,
    output wire        sense_d4080,     // $D505 bit 7 as the MMU reads it
    output wire        io_class,        // the access needs the 1 MHz bus cycle (2 MHz mode: chips, MMU registers, cartridge, CPU port, colour RAM)
    output wire        vic_char_off     // C128 mode: the VIC sees RAM at $1000-$1FFF instead of the character ROM (CPU port bit 2)
);
    wire [1:0] m_rombank, m_cpubank, m_vicbank;
    wire       m_iosel, m_z80_n_mmu, m_fsdir;
    wire       m_z80_n = !z80_act;           // the decode follows who has the bus, not the MMU's wish (it takes the Z80 a moment to give the bus)
    wire [15:0] m_taddr;

    // ---- the C128 decode (c128_n = 0) --------------------------------------------------
    wire a_ff0   = bus_addr[15:4] == 12'hFF0 && bus_addr[3:0] < 4'd5;
    wire [3:0] msn = bus_addr[15:12];
    wire rd      = !bus_we;
    wire rd_mem  = rd && !z80io;
    reg  c_io, c_ram, c_sys, c_roml, c_romh, c_chr, c_mmu_hi, c_mmu_lo_w, c_color, c_float;
    reg  [4:0] c_bank;
    // bank numbers: the 4 KiB bank of the editor/KERNAL ROM follows tAddr[12] (the Z80's window at $0000 reads $Dxxx of it)
    wire [4:0] bank_c128k = {3'b001, bus_addr[13], m_taddr[12]};
    wire [4:0] bank_basic = {2'b01, ~bus_addr[14], bus_addr[13], bus_addr[12]};
    always @(*) begin
        c_io = 1'b0; c_ram = 1'b0; c_sys = 1'b0; c_roml = 1'b0; c_romh = 1'b0; c_chr = 1'b0; c_mmu_hi = 1'b0; c_mmu_lo_w = 1'b0;
        c_color = 1'b0; c_float = 1'b0;
        c_bank = 5'd0;
        case (msn)
            4'hC, 4'hE, 4'hF: begin
                if (a_ff0) c_mmu_hi = 1'b1;
                else if (rd_mem) begin
                    case (m_rombank)
                        2'd0: begin c_sys = 1'b1; c_bank = bank_c128k; end
                        2'd1: c_float = 1'b1;                      // internal function ROM: not in this machine
                        2'd2: c_romh = 1'b1;                       // external ROM: the cartridge's ROMH
                        2'd3: c_ram = 1'b1;
                    endcase
                end else c_ram = 1'b1;
            end
            4'hD: begin
                if ((m_z80_n && !m_iosel) || z80io) c_io = 1'b1;
                else if (rd) begin
                    case (m_rombank)
                        2'd0: if (m_z80_n) c_chr = 1'b1; else c_ram = 1'b1;
                        2'd1: c_float = 1'b1;
                        2'd2: c_romh = 1'b1;
                        2'd3: c_ram = 1'b1;
                    endcase
                end else begin
                    c_ram = 1'b1;
                    if (bus_addr[11:8] == 4'h5 && !m_iosel) c_mmu_lo_w = 1'b1;   // a write with I/O mapped reaches the MMU too
                end
            end
            4'h4, 4'h5, 4'h6, 4'h7, 4'h8, 4'h9, 4'hA, 4'hB: begin
                if (rd_mem) begin
                    case (m_rombank)
                        2'd0: begin c_sys = 1'b1; c_bank = bank_basic; end
                        2'd1: c_float = 1'b1;
                        2'd2: begin
                            c_roml = !(msn == 4'hA || msn == 4'hB);
                            c_romh = (msn == 4'hA || msn == 4'hB);
                        end
                        2'd3: c_ram = 1'b1;
                    endcase
                end else c_ram = 1'b1;
            end
            4'h1: begin
                if (bus_addr[11:10] == 2'b00 && !m_z80_n && !m_iosel) c_color = 1'b1;
                else c_ram = 1'b1;
            end
            4'h0: begin
                if (!m_z80_n && !z80io && m_rombank == 2'd0 && rd) begin c_sys = 1'b1; c_bank = bank_c128k; end
                else c_ram = 1'b1;
            end
            default: c_ram = 1'b1;
        endcase
    end

    // ---- the C64's PLA (c128_n = 1) --------------------------------------------------
    wire p_io, p_ram, p_basic, p_kernal, p_chr, p_roml, p_romh;
    c64_pla_c128 pla (
        .game(mmu_game), .exrom(mmu_exrom), .charen(charen), .hiram(hiram), .loram(loram),
        .msn(msn), .we(bus_we), .ba(ba),
        .io(p_io), .ram(p_ram), .basic(p_basic), .kernal(p_kernal), .chr(p_chr), .roml(p_roml), .romh(p_romh));

    always @(*) begin
        cs_mmu_hi = 1'b0; cs_color_mem = 1'b0; cs_ff = 1'b0;
        if (!c128_n) begin
            io = c_io; ram = c_ram | c_float; basic = 1'b0; kernal = c_sys; chr = c_chr; roml = c_roml; romh = c_romh;
            cs_mmu_hi = c_mmu_hi;
            cs_color_mem = c_color;
            cs_ff = c_float;
            rom_bank = c_chr ? 5'd17 : c_bank;
        end else begin
            io = p_io; ram = p_ram; basic = p_basic; kernal = p_kernal; chr = p_chr; roml = p_roml; romh = p_romh;
            // C64 mode: BASIC = banks 0/1, KERNAL = banks 2/3, character ROM = bank 16
            rom_bank = p_chr ? 5'd16 : {3'b000, bus_addr[13] & bus_addr[14], bus_addr[12]};
        end
        cs_mmu_lo = 1'b0; cs_vdc = 1'b0;
        if (io && bus_addr[11:8] == 4'h5) cs_mmu_lo = 1'b1;
        if (!c128_n && c_mmu_lo_w) cs_mmu_lo = 1'b1;
        if (io && bus_addr[11:8] == 4'h6 && !c128_n) cs_vdc = 1'b1;
    end

    // ---- the MMU -------------------------------------------------------------------------
    mmu8722 mmu (
        .sys256k(1'b0), .osmode(boot64), .cpumode(!z80_first || boot64),
        .clk(clk), .reset(rst), .enable(mmu_en),
        .cs_io(cs_mmu_lo), .cs_lr(cs_mmu_hi), .we(bus_we),
        .addr(bus_addr), .di(cpu_dout), .dout(mmu_do),
        .d4080i(d4080_n), .d4080o(sense_d4080),
        .exromi(cart_exrom), .exromo(mmu_exrom),
        .gamei(cart_game), .gameo(mmu_game),
        .fsdiri(1'b1), .fsdiro(m_fsdir),
        .c128_n(c128_n), .z80_n(m_z80_n_mmu), .rombank(m_rombank), .iosel(m_iosel),
        .tAddr(m_taddr), .cpuBank(m_cpubank), .vicBank(m_vicbank));
    assign fsdir    = m_fsdir;
    assign z80_n    = m_z80_n_mmu;
    assign ram_bank = m_cpubank[0];
    assign vic_bank = m_vicbank[0];
    assign t_addr   = m_taddr;
    assign vic_char_off = !c128_n && charen;
    // from the C128 decode only (the 2 MHz mode exists in C128 mode only): the PLA's roml/romh depend on GAME/EXROM, which the cartridge and the REU drive
    // combinationally from the chip selects this term gates - a loop (DRC LUTLP-1 in the first flat build with the 2 MHz mode)
    assign io_class = !c128_n && (c_io | c_mmu_hi | c_mmu_lo_w | c_roml | c_romh | c_color | (bus_addr[15:1] == 15'd0));
endmodule
