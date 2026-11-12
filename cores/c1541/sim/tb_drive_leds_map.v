// T159: drive_leds_map for all eight input combinations. The drive's leds_n is {dirty, motor, activity}, active low;
// the contract port wants [0] = floppy lit by the drive's own LED (activity) alone, [1] = hard disk (never from a 1541), [2] = 1.
`timescale 1ns/1ps
module tb_drive_leds_map;
    reg  [2:0] d;
    wire [2:0] c;
    drive_leds_map dut (.drv_leds_n(d), .drive_leds_n(c));
    integer i, bad;
    reg floppy_lit;
    initial begin
        bad = 0;
        for (i = 0; i < 8; i = i + 1) begin
            d = i[2:0];
            #1;
            floppy_lit = !d[0];                         // the drive LED; the PWM motor LED d[1] must not count
            if (c[0] !== !floppy_lit) begin bad = bad + 1; $display("FAIL d=%b: floppy bit %b, want %b", d, c[0], !floppy_lit); end
            if (c[1] !== 1'b1)        begin bad = bad + 1; $display("FAIL d=%b: hard-disk bit %b, want 1", d, c[1]); end
            if (c[2] !== 1'b1)        begin bad = bad + 1; $display("FAIL d=%b: bit 2 %b, want 1", d, c[2]); end
        end
        d = 3'b101; #1;                             // motor LED lit, activity off: the case that lit the blue lamp
        if (c !== 3'b111) begin bad = bad + 1; $display("FAIL motor only: %b, want 111", c); end
        if (bad == 0) $display("PASS tb_drive_leds_map (8 combinations, motor LED alone: all three bits 1, activity: floppy 0)");
        else          $display("FAIL tb_drive_leds_map (%0d errors)", bad);
        $finish;
    end
endmodule
