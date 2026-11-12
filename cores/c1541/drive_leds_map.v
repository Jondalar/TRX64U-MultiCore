// T159: the 1541 drive's own LED outputs onto the contract port drive_leds_n.
// Drive side (cores/c64u/gideon/mm_drive.vhd through drive_l2, active low): [0] act_led (the drive's front LED: VIA2 PB3 in 1541 mode, CIA PA6 in 1581 mode),
// [1] motor_led_n (a PWM brightness: on for 1/3 to 4/4 of the time whenever the drive is powered, so never an "is spinning" level), [2] dirty.
// Contract side (host bling_io.v / case LED): [0] floppy, [1] hard disk (blue flicker), [2] unused. The floppy lamp follows the drive's own LED;
// a 1541/1581 core has no hard disk, so [1] and [2] stay 1 and the PWM motor LED reaches neither.
module drive_leds_map (
    input  wire [2:0] drv_leds_n,
    output wire [2:0] drive_leds_n
);
    assign drive_leds_n = {1'b1, 1'b1, drv_leds_n[0]};
endmodule
