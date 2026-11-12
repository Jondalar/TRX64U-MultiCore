// T159 D3/D4: the C64U power lamp = LED 71 of the keyboard chain, as host/bling_io.v sends it. The bench decodes the WS2812 frame
// on bling_led (0 = ~0.35 us high, 1 = ~0.8 us high, a gap of 310 us ends the frame) and checks, per state, the 71st LED (G R B) and that LEDs 1-70 are dark.
module tb_bling_lamp;
  reg clk = 0, rst = 1, drive_act = 0, hd_act = 0, lamp_en = 0, locked = 0;
  wire bling_led, irq, lock_off;
  wire [7:0] rdata;
  bling_io #(.FLICK_HALF(300000)) dut (.clk(clk), .rst(rst), .we(1'b0), .addr(12'h000), .wdata(8'h00), .rdata(rdata), .irq(irq),
                .rx_tgl(1'b0), .rx_byte(8'h00), .locked(locked), .drive_act(drive_act), .hd_act(hd_act), .lamp_en(lamp_en),
                .lock_off(lock_off), .bling_led(bling_led));
  always #5 clk = ~clk;                         // 100 MHz = the module's CLK_HZ
  integer errors = 0;
  localparam [23:0] RED = 24'h008000, BLUE = 24'h000080, YELLOW = 24'h808000, AMBER = 24'h1E2400;   // GRB
  reg [1703:0] frame;                           // bit 0 = the first bit sent
  integer nbits, hi;
  time t_rise;
  // wait for the reset gap (low for more than 100 us), then decode 71 * 24 bits
  task get_frame; integer low; begin
    low = 0;
    while (low < 10000) begin @(posedge clk); if (bling_led) low = 0; else low = low + 1; end
    nbits = 0;
    while (nbits < 1704) begin
      @(posedge bling_led); t_rise = $time;
      @(negedge bling_led);
      hi = ($time - t_rise) / 10;               // clocks high
      frame[nbits] = hi > 55 ? 1'b1 : 1'b0;
      nbits = nbits + 1;
    end
  end endtask
  function [23:0] led71; input dummy; integer k; begin
    for (k = 0; k < 24; k = k + 1) led71[23 - k] = frame[70 * 24 + k];
  end endfunction
  function dark70; input dummy; begin dark70 = (frame[1679:0] == 1680'd0); end endfunction
  reg [4:0] seen; reg dark_ok;
  // N frames after `skip` settling frames: which colours LED 71 took (bit0 red, bit1 off, bit2 blue, bit3 yellow, bit4 anything else), and whether 1-70 stayed dark
  task observe(input integer n, input integer skip); integer i; reg [23:0] c; begin
    repeat (skip) get_frame; seen = 0; dark_ok = 1;
    for (i = 0; i < n; i = i + 1) begin
      get_frame; c = led71(0);
      if (!dark70(0)) dark_ok = 0;
      if (c == RED) seen[0] = 1; else if (c == 24'h0) seen[1] = 1; else if (c == BLUE) seen[2] = 1;
      else if (c == YELLOW) seen[3] = 1; else seen[4] = 1;
    end
  end endtask
  task check(input [4:0] want, input [639:0] what); begin
    observe(14, 2);
    if (seen !== want || !dark_ok) begin errors = errors + 1; $display("FAIL %0s: seen=%b (red,off,blue,yellow,other from bit 0) want %b, LEDs 1-70 dark=%b", what, seen, want, dark_ok); end
    else $display("ok   %0s: seen=%b, LEDs 1-70 dark", what, seen);
  end endtask
  task check_const(input [23:0] want, input [639:0] what); begin
    observe(3, 2);
    if (!(seen == 5'b00001 && want == RED) && !(seen == 5'b00010 && want == 24'h0) && !(seen == 5'b01000 && want == YELLOW) && !(seen == 5'b10000 && want == AMBER) || !dark_ok) begin
      errors = errors + 1; $display("FAIL %0s: seen=%b want colour %06x, LEDs 1-70 dark=%b", what, seen, want, dark_ok); end
    else $display("ok   %0s: steady %06x, LEDs 1-70 dark", what, want);
  end endtask
  initial begin
    repeat (20) @(posedge clk); rst = 0;
    // lamp off (the app's setting Off): dark, SHIFT LOCK as before (amber), activity ignored
    check_const(24'h0, "Off, idle: dark");
    drive_act = 1; hd_act = 1; check_const(24'h0, "Off, floppy + HD active: still dark");
    drive_act = 0; hd_act = 0; locked = 1; check_const(AMBER, "Off, SHIFT LOCK: amber as before");
    drive_act = 1; hd_act = 1; check_const(AMBER, "Off, SHIFT LOCK + activity: amber");
    drive_act = 0; hd_act = 0; locked = 0; observe(4, 0);   // the holds of the activity above run out (Off: the lamp ignored it)
    lamp_en = 1;
    check_const(RED, "On, idle: red, steady");
    drive_act = 1; check(5'b00011, "On, floppy active: red flickers off (red and off frames)");
    hd_act = 1; check(5'b00110, "On, HD active: blue flickers (blue and off frames, no red)");
    drive_act = 0; check(5'b00110, "On, HD alone: blue flicker");
    locked = 1; check_const(YELLOW, "On, SHIFT LOCK over floppy + HD: yellow, steady");
    drive_act = 1; hd_act = 0; check_const(YELLOW, "On, SHIFT LOCK + floppy: yellow, steady");
    drive_act = 0; check_const(YELLOW, "On, SHIFT LOCK: yellow");
    locked = 0; check_const(RED, "On, idle again: red, steady");
    // a short access: one pulse of 10 clocks must still show (held for a full flicker period)
    hd_act = 1; repeat (10) @(posedge clk); hd_act = 0;
    observe(3, 0); if (!seen[2]) begin errors = errors + 1; $display("FAIL short HD pulse not visible: seen=%b", seen); end else $display("ok   short HD pulse (10 clocks): blue seen, seen=%b", seen);
    check_const(RED, "after the pulse: back to red");
    drive_act = 1; repeat (10) @(posedge clk); drive_act = 0;
    observe(3, 0); if (!seen[1]) begin errors = errors + 1; $display("FAIL short floppy pulse not visible: seen=%b", seen); end else $display("ok   short floppy pulse (10 clocks): off frame seen, seen=%b", seen);
    check_const(RED, "after the pulse: back to red");
    lamp_en = 0; check_const(24'h0, "switched Off again: dark");
    if (errors == 0) $display("PASS tb_bling_lamp errors=0"); else $display("FAIL tb_bling_lamp errors=%0d", errors);
    $finish;
  end
  initial begin #2000000000; $display("FAIL tb_bling_lamp timeout"); $finish; end
endmodule
