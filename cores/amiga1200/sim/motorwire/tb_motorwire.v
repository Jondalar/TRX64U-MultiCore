// T159 S4c: from the motor bit at amiga_core's drv_led to the case LED and the bling lamp, through the shipped wiring: layer2_amiga.v with build_mono.tcl's edits ->
// drive_leds_n[0] -> top.v's cled_s1/cled_s2 -> case_leds (source 4 = "drive A activity") and bling_io's drive_act.
module tb_motorwire;
  reg cpu_clk = 0; always #5 cpu_clk = ~cpu_clk;
  reg [7:0] sel = 8'h00; reg set = 0;
  wire bling, led1n;
  wire_top dut (.cpu_clk(cpu_clk), .caseled_sel(sel), .caseled_set(set), .BLING_LED(bling), .led1n_o(led1n));
  integer errors = 0;
  task check(input cond, input [639:0] what); begin
    if (!cond) begin errors = errors + 1; $display("FAIL %0s", what); end else $display("ok   %0s", what);
  end endtask
  task settle; repeat (20) @(posedge cpu_clk); endtask
  initial begin
    settle;
    sel = 8'h04; set = 1;                       // the app: LED 1 = source 4
    settle;
    check(led1n === 1'b1 && dut.bio.drive_act === 1'b0, "motor off: case LED 1 off, lamp not active");
    dut.l2.amiga.drv_led = 1'b1; settle;
    check(led1n === 1'b0, "motor on: case LED 1 on (source 4)");
    check(dut.bio.drive_act === 1'b1, "motor on: bling_io drive_act high");
    check(dut.bio.lamp_en === 1'b1, "selector 0x04: lamp enabled");
    dut.l2.amiga.drv_led = 1'b0; settle;
    check(led1n === 1'b1 && dut.bio.drive_act === 1'b0, "motor off again: both off");
    dut.l2.amiga.drv_hd = 1'b1; settle;
    check(dut.bio.hd_act === 1'b1, "hard disk: bling_io hd_act high (T159 D4)");
    check(led1n === 1'b1 && dut.bio.drive_act === 1'b0, "hd alone: case LED 1, drive_act untouched");
    dut.l2.amiga.drv_hd = 1'b0; settle;
    check(dut.bio.hd_act === 1'b0, "hard disk off: hd_act low");
    sel = 8'h00; settle;
    check(dut.bio.lamp_en === 1'b0, "selector 0x00 (Off written): lamp disabled");
    if (errors == 0) $display("PASS tb_motorwire errors=0"); else $display("FAIL tb_motorwire errors=%0d", errors);
    $finish;
  end
endmodule
