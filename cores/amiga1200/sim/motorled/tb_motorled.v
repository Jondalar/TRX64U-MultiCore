// T159 S4: the case LED 1 source of the A1200 (trx_motor_led in rtl/minimig.v): on while a present drive's motor runs (latched on the falling
// edge of /SELx like paula_floppy.v), on while the hard disk is accessed, not for an absent drive, cleared by reset.
module tb_motorled;
  reg clk = 0, ce = 0, reset = 1, motor_n = 1, hdd = 0;
  reg [3:0] sel_n = 4'hF;
  reg [1:0] drives = 2'd0;
  wire led, hd;
  integer errors = 0, cyc = 0;
  trx_motor_led #(.HDD_HOLD(16)) dut (.clk(clk), .ce(ce), .reset(reset), .sel_n(sel_n), .motor_n(motor_n), .drives(drives), .hdd(hdd), .led(led), .hd(hd));
  always #5 clk = ~clk;
  always @(posedge clk) begin cyc <= cyc + 1; ce <= (cyc % 4) == 0; end
  task step; begin repeat (12) @(posedge clk); end endtask
  task check(input v, input [639:0] what); begin
    step;
    if (led !== v) begin errors = errors + 1; $display("FAIL %0s: led=%b want %b", what, led, v); end
    else $display("ok   %0s: led=%b", what, led);
  end endtask
  // the kernel's way: set /MTR, then pulse the /SEL of the drive
  task motor(input [1:0] d, input on); begin
    motor_n = ~on; step; sel_n[d] = 1'b0; step; sel_n[d] = 1'b1; step;
  end endtask
  initial begin
    drives = 2'd0; reset = 1; step; step; reset = 0;
    check(0, "idle after reset");
    motor(0, 1);  check(1, "DF0 motor on, latched after /SEL0 released");
    motor_n = 1;  step; check(1, "/MTR alone does not switch it off (no /SEL edge)");
    motor(0, 0);  check(0, "DF0 motor off");
    hdd = 1;      check(1, "hard disk access");
    hdd = 0;      check(1, "hard disk access held after drq ends (16 ce ticks)");
    repeat (80) @(posedge clk); check(0, "hard disk idle");
    hdd = 1; @(posedge clk); @(posedge clk); @(posedge clk); @(posedge clk); hdd = 0;   // a one-ce-tick pulse
    check(1, "short hard disk pulse is visible");
    repeat (80) @(posedge clk); check(0, "and ends");
    motor(1, 1);  check(0, "DF1 absent (drives=0): probing it does not light the LED");
    motor(1, 0);
    motor(0, 1);  check(1, "DF0 on again");
    reset = 1; step; step; reset = 0; check(0, "reset clears the motor");
    drives = 2'd1; reset = 1; step; step; reset = 0;
    motor(1, 1);  check(1, "DF1 present (drives=1): its motor lights the LED");
    motor(1, 0);  check(0, "DF1 off");
    // T159 D4: hd = the hard disk access alone (held), never the floppy motor
    motor(0, 0); repeat (80) @(posedge clk); step;
    if (hd !== 1'b0) begin errors = errors + 1; $display("FAIL hd idle: %b", hd); end
    motor(0, 1); step; if (hd !== 1'b0 || led !== 1'b1) begin errors = errors + 1; $display("FAIL motor on must not raise hd: hd=%b led=%b", hd, led); end
    hdd = 1; step; if (hd !== 1'b1) begin errors = errors + 1; $display("FAIL hd on access"); end
    hdd = 0; step; if (hd !== 1'b1) begin errors = errors + 1; $display("FAIL hd not held"); end
    repeat (80) @(posedge clk); step; if (hd !== 1'b0 || led !== 1'b1) begin errors = errors + 1; $display("FAIL hd ends, motor still lights led: hd=%b led=%b", hd, led); end
    if (errors == 0) $display("PASS tb_motorled errors=0"); else $display("FAIL tb_motorled errors=%0d", errors);
    $finish;
  end
endmodule
