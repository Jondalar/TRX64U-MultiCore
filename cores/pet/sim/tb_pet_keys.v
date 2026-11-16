// T216 S1/S2/S14/S15: the PET's keyboard matrices and the pad keys, without the machine. pet_keys turns the host's C64 key matrix into the PET's; every check
// presses one C64 key (plus the C64 shift or C=) and requires exactly the PET bits that the expectation names, on every row.
//   positional (sym = 0): VICE's gtk3_buuk_pos.vkm / gtk3_grus_pos.vkm (keys_expect.vh: bchk_all, gchk_all)
//   symbolic (sym = 1): the character the C64 key types, found on the PET's key legends (keys_expect.vh: ssym_all, gen_key_expect.py)
//   pad keys: pet_padkeys ORs the key a button names into the matrix (S15)
`timescale 1ns / 1ps
module tb_pet_keys;
    reg [63:0] kk = 64'd0;
    reg [3:0]  kr = 4'd0;
    wire [7:0] kb, kg, ksb, ksg;
    pet_keys kx_b  (.keys(kk), .bus(1'b1), .sym(1'b0), .keyrow(kr), .keyin(kb));
    pet_keys kx_g  (.keys(kk), .bus(1'b0), .sym(1'b0), .keyrow(kr), .keyin(kg));
    pet_keys kx_sb (.keys(kk), .bus(1'b1), .sym(1'b1), .keyrow(kr), .keyin(ksb));
    pet_keys kx_sg (.keys(kk), .bus(1'b0), .sym(1'b1), .keyrow(kr), .keyin(ksg));
    integer kerr = 0, kn = 0, kri;
    // one C64 key (sh = C64 shift held, cbm = C= held) must pull the bits of (row, col) and, when psh, the PET shift key (graphics row 8 column 5, business row 6 column 0), nothing else
    task kcheck(input bus, input sym, input integer key, input integer sh, input integer cbm, input integer row, input integer col, input integer psh);
        reg [7:0] want, got;
        integer shr, shc;
        begin
            kk = 64'd0; kk[key] = 1'b1; kk[15] = kk[15] | (sh != 0); kk[61] = (cbm != 0) | kk[61];
            shr = bus ? 6 : 8; shc = bus ? 0 : 5;
            for (kri = 0; kri < 10; kri = kri + 1) begin
                kr = kri; #10;
                want = 8'hFF;
                if (kri == row) want = want & ~(8'd1 << col);
                if (psh != 0 && kri == shr) want = want & ~(8'd1 << shc);
                if (psh == 0 && sh != 0 && !sym && kri == shr) want = want & ~(8'd1 << shc);       // positional: the C64 shift is the PET shift
                got = bus ? (sym ? ksb : kb) : (sym ? ksg : kg);
                if (got !== want) begin
                    $display("FAIL %s %s key %0d shift %0d cbm %0d: row %0d reads %h, wanted %h (row %0d column %0d, PET shift %0d)", bus ? "business" : "graphics",
                             sym ? "symbolic" : "positional", key, sh, cbm, kri, got, want, row, col, psh);
                    kerr = kerr + 1;
                end
            end
            kn = kn + 1;
        end
    endtask
    task bchk(input integer key, input integer cbm, input integer row, input integer col); kcheck(1'b1, 1'b0, key, 0, cbm, row, col, 0); endtask
    task gchk(input integer key, input integer cbm, input integer row, input integer col); kcheck(1'b0, 1'b0, key, 0, cbm, row, col, 0); endtask
    task ssym_b(input integer key, input integer sh, input integer row, input integer col, input integer psh); kcheck(1'b1, 1'b1, key, sh, 0, row, col, psh); endtask
    task ssym_g(input integer key, input integer sh, input integer row, input integer col, input integer psh); kcheck(1'b0, 1'b1, key, sh, 0, row, col, psh); endtask
`include "keys_expect.vh"

    // ---- pad keys: five buttons, two pads and two joystick ports, one key code each
    reg [15:0] pad1 = 0, pad2 = 0;
    reg [4:0]  joy1 = 0, joy2 = 0;
    reg [39:0] code = {5{8'hFF}};
    reg        pclk = 0;
    always #5 pclk = ~pclk;
    wire [63:0] ko;
    pet_padkeys pk (.clk(pclk), .keys(64'd0), .pad1(pad1), .pad2(pad2), .joy1(joy1), .joy2(joy2), .code(code), .keys_o(ko));
    integer perr = 0;
    task pexpect(input [63:0] want, input [255:0] what); begin
        repeat (6) @(posedge pclk); #1;
        if (ko !== want) begin $display("FAIL pad keys %0s: %h, wanted %h", what, ko, want); perr = perr + 1; end
    end endtask
    initial begin
        #1000;
        bchk_all; gchk_all; ssym_all;
        $display("keys: %0d key checks (positional + symbolic, business + graphics), %0d errors", kn, kerr);
        // codes: up = C64 key 56 ('1'), down = 59 ('2'), left = 2 + SHIFT (CRSR right with shift), right = 6 ('F5'), fire = 60 (SPACE)
        code = {8'd60, 8'd6, 8'h40 | 8'd2, 8'd59, 8'd56};
        pexpect(64'd0, "idle");
        pad1[0] = 1; pexpect(64'd1 << 56, "pad 1 up");
        pad1[0] = 0; pad2[1] = 1; pexpect(64'd1 << 59, "pad 2 down");
        pad2[1] = 0; joy1[2] = 1; pexpect((64'd1 << 2) | (64'd1 << 15), "joystick 1 left = key + shift");
        joy1[2] = 0; joy2[4] = 1; pexpect(64'd1 << 60, "joystick 2 fire");
        joy2[4] = 0; pad1[3] = 1; pad1[4] = 1; pexpect((64'd1 << 6) | (64'd1 << 60), "pad 1 right + fire together");
        pad1 = 0; code[39:32] = 8'hFF; joy1[4] = 1; pexpect(64'd0, "fire has no key");
        joy1 = 0;
        $display("pad keys: %0d errors", perr);
        if (kerr + perr == 0) $display("PASS tb_pet_keys");
        else $display("FAIL tb_pet_keys: %0d errors", kerr + perr);
        $finish;
    end
endmodule
