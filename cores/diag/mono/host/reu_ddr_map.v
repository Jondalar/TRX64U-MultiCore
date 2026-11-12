// The REU's request on the way to DDR2 (top.v): c64_top's command address is the 16-bit word address inside the
// REU's 16 MB (23 bits, byte address bits 23:1); the REU lives from BASE (a 16-bit word address, 0x0800000 = byte
// 16 MB, Gideon's REU_MEMORY_BASE) and uses 0x0800000-0x0FFFFFF in words, 0x1000000-0x1FFFFFF in bytes. be: the byte lane of the 16-bit word.
module reu_ddr_map #(
    parameter [25:0] BASE = 26'h0800000
) (
    input  wire [23:0] cmd_addr,
    input  wire [1:0]  wdata_we,
    output wire [25:0] ddr_addr,
    output wire [3:0]  ddr_be
);
    assign ddr_addr = BASE + {3'd0, cmd_addr[22:0]};
    assign ddr_be   = cmd_addr[0] ? {wdata_we, 2'b00} : {2'b00, wdata_we};
endmodule
