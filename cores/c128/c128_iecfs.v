// The fast serial wiring of the C128 (T170): FSDIR ($D505 bit 3) and the serial bus's SRQ line decide whether CIA 1's serial register and its
// CNT pin face the bus. Combinational; levels, 1 = high / released, 0 = pulled low.
//   FSDIR = 1 (output): CIA 1's CNT drives SRQ, its SP drives DATA (open collector: it pulls only when it sends a 0); CIA 1 sees nothing of the bus
//   FSDIR = 0 and C128 mode (input): the bus's SRQ is CIA 1's CNT, the bus's DATA is its SP
//   FLAG of CIA 1 sees SRQ too (the cassette's READ line stays in the AND), and, as on the machine, its own CNT while it drives SRQ
// The equations are those of the MEGA65 C128 core (fpga64_sid_iec.vhd, Erik Scheffers' C128_MiSTer; PROVENANCE.md), written again in Verilog.
module c128_iecfs (
    input  wire fs_dir,         // MMU $D505 bit 3: 1 = the machine sends (reset state: 1)
    input  wire c128_n,         // 0 = C128 mode, 1 = C64 mode (the input direction exists in C128 mode only)
    input  wire srq_bus,        // the bus's SRQ line as the core hears it
    input  wire data_bus,       // the bus's DATA line
    input  wire cnt_out,        // CIA 1's CNT, SP outputs
    input  wire sp_out,
    input  wire tape_read,      // the cassette's READ line
    output wire cnt_in,         // CIA 1's CNT, SP, FLAG inputs
    output wire sp_in,
    output wire flag_n,
    output wire srq_out,        // to the bus: SRQ, and the gate for the DATA line (AND it into the machine's DATA pull)
    output wire data_gate
);
    wire fs_in = !fs_dir && !c128_n;
    assign srq_out   = cnt_out | !fs_dir;
    assign data_gate = sp_out  | !fs_dir;
    assign cnt_in    = srq_bus | !fs_in;
    assign sp_in     = data_bus | !fs_in;
    assign flag_n    = srq_bus & srq_out & tape_read;
endmodule
