// TRX64 host (T164 WP4): the host's copy of vendor/icepi/hdmi/hdmi.v (host/hdmi_enc/hdmi.diff). Every register changes only when `ce` is high (one
// pixel; clk_pixel is the TMDS clock, `gear` times the pixel clock); cx / cy count pixels, the frame of the mode comes in at run time (sx_e,
// fw_m1, sy_e, ey_e), the VIC, the picture aspect and the pixel clock rate (audio) are inputs; the AVI InfoFrame carries PR = 0.
// Implementation of HDMI Spec v1.4a
// By Sameer Puri https://github.com/sameer
// Converted from SystemVerilog to Verilog

module trx_hdmi
#(
    parameter DVI_OUTPUT = 1'b0,
    parameter IT_CONTENT = 1'b1,
    parameter VIDEO_ID_CODE = 0,
    parameter VIDEO_RATE = 27000000,
    parameter AUDIO_RATE = 48000,
    parameter AUDIO_BIT_WIDTH = 24,
    parameter BIT_WIDTH = 12,
    parameter BIT_HEIGHT = 11,
    parameter [8*8-1:0] VENDOR_NAME = 64'h556e6b6e6f776e00, // "Unknown" + nulls
    parameter [8*16-1:0] PRODUCT_DESCRIPTION = 128'h46504741000000000000000000000000, // "FPGA" + nulls
    parameter [7:0] SOURCE_DEVICE_INFORMATION = 8'h09, // See README.md or CTA-861-G for the list of valid codes
    // TRX64 (T40): RUNTIME = 1 takes the VIC code and the pixel clock rate
    // (for the audio sample tick) from vic_code / video_rate instead of
    // VIDEO_ID_CODE / VIDEO_RATE (then the largest rate); MAX_PACKETS is the
    // number of data island packets per line, at the same place in every mode
    // (cx 4 .. 14 + 32 MAX_PACKETS + 2); the video preamble must still fit behind it
    parameter RUNTIME = 1,
    parameter MAX_PACKETS = 3
)
(
    input clk_pixel_x5,
    input clk_pixel,
    input ce,                            // one pixel (high once per `gear` clocks); every register changes only then

    // synchronous reset back to 0,0
    input reset,

    input [23:0] rgb,
    input hsync,
    input vsync,
    input [BIT_WIDTH-1:0] cx,
    input [BIT_HEIGHT-1:0] cy,
    input de,

    input [6:0] vic_code,
    input [31:0] video_rate,

    input [1:0] aspect,                  // AVI picture aspect M1:M0 (0 none, 1 4:3, 2 16:9)

    // the frame of the mode at run time, in pixels / lines (hdmi_timing.v: cx = 0 is the first pixel after the active area). The data
    // island sits at the same place in every mode: preamble 4 .. 11, guards 12 / 13, packets from 14, behind the active area.
    input [BIT_WIDTH-1:0] sx_e,          // first active pixel of the line
    input [BIT_WIDTH-1:0] fw_m1,         // last pixel of the line
    input [BIT_HEIGHT-1:0] sy_e,         // first active line
    input [BIT_HEIGHT-1:0] ey_e,         // one past the last active line

    input [AUDIO_BIT_WIDTH-1:0] audio_sample_word_0,
    input [AUDIO_BIT_WIDTH-1:0] audio_sample_word_1,
    output audio_sample_en,

    output [9:0] tmds_0,
    output [9:0] tmds_1,
    output [9:0] tmds_2
);

reg [2:0] mode;
reg [23:0] video_data;
reg [5:0] control_data;
reg [11:0] data_island_data;

generate
    if (!DVI_OUTPUT)
    begin: true_hdmi_output
        // The preamble, guard band and data island windows are registered before they reach `mode` / `control_data`, so they leave the
        // encoder one pixel after the pixel data and the sync bits (video guard -> mode -> tmds_channel mode -> tmds: four stages; rgb and
        // hsync: three). They are therefore decided one pixel ahead, on the next cx, which puts the guard band directly before the first
        // active pixel (the vendor module's windows sit one pixel late and the guard overwrites the first pixel).
        wire [BIT_WIDTH-1:0] cxn = (cx == fw_m1) ? {BIT_WIDTH{1'b0}} : cx + 1'b1;
        reg video_guard;
        reg video_preamble;
        // T164 run 1121 (WNS -0.149 ns at 150 MHz, hact -> video_preamble, 11 levels): the window edges are subtractions of the mode's frame
        // and the line test two compares, all quasi-static; they are registered here, and the windows are taken on cx, one less than cxn.
        // Exact because the video windows (sx_e - 11 .. sx_e - 1) lie far from the line wrap at cx = fw_m1 and cy changes only at that wrap.
        reg [BIT_WIDTH-1:0] sx_m1, sx_m3, sx_m11;
        reg cy_in;
        // T181 (TRX64-Ultimate u16 d48734d8, T222): the last active line (ey_e - 1) is registered too (the compare with cy fed the audio packet CE
        // there: WNS -0.062, 9 levels). ey_e is the mode's, quasi static, so video_field_end is the same cycle as before except one ce after a mode change.
        reg [BIT_HEIGHT-1:0] ey_m1;
        always @(posedge clk_pixel) if (ce)
        begin
            ey_m1 <= ey_e - 1'b1;
            sx_m1 <= sx_e - 1'b1;
            sx_m3 <= sx_e - 2'd3;
            sx_m11 <= sx_e - 4'd11;
            cy_in <= cy >= sy_e && cy < ey_e;
            if (reset)
            begin
                video_guard <= 0;
                video_preamble <= 0;
            end
            else
            begin
                video_guard <= cx >= sx_m3 && cx < sx_m1 && cy_in;
                video_preamble <= cx >= sx_m11 && cx < sx_m3 && cy_in;
            end
        end

        reg data_island_sync;
        reg data_island_guard;
        reg data_island_trail;
        reg data_island_preamble;
        reg data_island_period;
        reg [4:0] data_island_offset;

        wire data_island_period_instantaneous;
        wire video_field_end;
        wire packet_enable;

        // See Section 5.2.3.1. The island starts 4 pixels behind the active area (cx = 4) and never wraps.
        // T164 run 1121: the windows are taken on cx against the constants less one, not on cxn (an adder fewer on the hact paths);
        // exact because no window starts at 0, so cx = fw_m1 (cxn = 0) is in none of them.
        localparam [4:0] NUM_PACKETS_ALONGSIDE = MAX_PACKETS;

        localparam [BIT_WIDTH-1:0] DATA_ISLAND_PREAMBLE_START = 4;
        localparam [BIT_WIDTH-1:0] DATA_ISLAND_PREAMBLE_END = 12;

        localparam [BIT_WIDTH-1:0] DATA_ISLAND_GUARD_START = 12;
        localparam [BIT_WIDTH-1:0] DATA_ISLAND_GUARD_END = 14;

        localparam [BIT_WIDTH-1:0] DATA_ISLAND_START = 14;
        localparam [BIT_WIDTH-1:0] DATA_ISLAND_END = 14 + NUM_PACKETS_ALONGSIDE * 32;

        localparam [BIT_WIDTH-1:0] DATA_ISLAND_TRAIL_START = 14 + NUM_PACKETS_ALONGSIDE * 32;
        localparam [BIT_WIDTH-1:0] DATA_ISLAND_TRAIL_END = 14 + NUM_PACKETS_ALONGSIDE * 32 + 2;

        assign data_island_period_instantaneous = data_island_sync &&
            NUM_PACKETS_ALONGSIDE > 0 && (
            DATA_ISLAND_END > DATA_ISLAND_START ?
                cx >= DATA_ISLAND_START - 1'b1 && cx < DATA_ISLAND_END - 1'b1 :
                cx >= DATA_ISLAND_START - 1'b1 || cx < DATA_ISLAND_END - 1'b1);

        assign video_field_end = cx == fw_m1 && cy == ey_m1;

        assign packet_enable = data_island_period_instantaneous &&
           data_island_offset == 0;

        always @(posedge clk_pixel) if (ce)
        begin
            if (reset)
            begin
                data_island_sync <= 0;
                data_island_preamble <= 0;
                data_island_guard <= 0;
                data_island_trail <= 0;
                data_island_period <= 0;
                data_island_offset <= 5'd0;
            end
            else
            begin
                if (video_field_end)
                    data_island_sync <= 1;

                data_island_preamble <= data_island_sync && NUM_PACKETS_ALONGSIDE > 0 && (
                    DATA_ISLAND_PREAMBLE_END > DATA_ISLAND_PREAMBLE_START ?
                        cx >= DATA_ISLAND_PREAMBLE_START - 1'b1 && cx < DATA_ISLAND_PREAMBLE_END - 1'b1 :
                        cx >= DATA_ISLAND_PREAMBLE_START - 1'b1 || cx < DATA_ISLAND_PREAMBLE_END - 1'b1);

                data_island_guard <= data_island_sync && NUM_PACKETS_ALONGSIDE > 0 && (
                    DATA_ISLAND_GUARD_END > DATA_ISLAND_GUARD_START ?
                        cx >= DATA_ISLAND_GUARD_START - 1'b1 && cx < DATA_ISLAND_GUARD_END - 1'b1 :
                        cx >= DATA_ISLAND_GUARD_START - 1'b1 || cx < DATA_ISLAND_GUARD_END - 1'b1);

                data_island_period <= data_island_period_instantaneous;

                data_island_trail <= data_island_sync && NUM_PACKETS_ALONGSIDE > 0 && (
                    DATA_ISLAND_TRAIL_END > DATA_ISLAND_TRAIL_START ?
                        cx >= DATA_ISLAND_TRAIL_START - 1'b1 && cx < DATA_ISLAND_TRAIL_END - 1'b1 :
                        cx >= DATA_ISLAND_TRAIL_START - 1'b1 || cx < DATA_ISLAND_TRAIL_END - 1'b1);

                if (data_island_period_instantaneous)
                    data_island_offset <= data_island_offset + 5'd1;
            end
        end

        // See Section 5.2.3.4
        wire [23:0] header;
        wire [55:0] sub_0, sub_1, sub_2, sub_3;
        wire [4:0] packet_pixel_counter;

        trx_packet_picker #(
            .VIDEO_ID_CODE(VIDEO_ID_CODE),
            .IT_CONTENT(IT_CONTENT),
            .AUDIO_RATE(AUDIO_RATE),
            .AUDIO_BIT_WIDTH(AUDIO_BIT_WIDTH),
            .VENDOR_NAME(VENDOR_NAME),
            .PRODUCT_DESCRIPTION(PRODUCT_DESCRIPTION),
            .SOURCE_DEVICE_INFORMATION(SOURCE_DEVICE_INFORMATION)
        ) packet_picker_inst (
            .clk_pixel(clk_pixel),
            .ce(ce),
            .vic(RUNTIME ? vic_code : VIDEO_ID_CODE[6:0]),
            .aspect(aspect),
            .audio_sample_en(audio_sample_en),
            .reset(reset),
            .video_field_end(video_field_end),
            .packet_enable(packet_enable),
            .packet_pixel_counter(packet_pixel_counter),
            .audio_sample_word_0(audio_sample_word_0),
            .audio_sample_word_1(audio_sample_word_1),
            .header(header),
            .sub_0(sub_0),
            .sub_1(sub_1),
            .sub_2(sub_2),
            .sub_3(sub_3)
        );

        wire [8:0] packet_data;
        trx_packet_assembler packet_assembler_inst (
            .clk_pixel(clk_pixel),
            .ce(ce),
            .reset(reset),
            .data_island_period(data_island_period),
            .header(header),
            .sub_0(sub_0),
            .sub_1(sub_1),
            .sub_2(sub_2),
            .sub_3(sub_3),
            .packet_data(packet_data),
            .counter(packet_pixel_counter)
        );

        reg data_island_first;

        always @(posedge clk_pixel) if (ce)
        begin
            if (reset)
            begin
                mode <= 3'd2;
                video_data <= 24'd0;
                control_data <= 6'd0;
                data_island_data <= 12'd0;
                data_island_first <= 0;
            end
            else
            begin
                mode <= (data_island_guard || data_island_trail) ? 3'd4 : data_island_period ? 3'd3 : video_guard ? 3'd2 : de ? 3'd1 : 3'd0;
                video_data <= rgb;
                control_data <= {{1'b0, data_island_preamble}, {1'b0, video_preamble || data_island_preamble}, {vsync, hsync}}; // ctrl3, ctrl2, ctrl1, ctrl0, vsync, hsync
                data_island_data[11:4] <= packet_data[8:1];
                data_island_data[3] <= !data_island_first;
                data_island_data[2] <= packet_data[0];
                data_island_data[1:0] <= {vsync, hsync};

                if (data_island_guard)
                    data_island_first <= 1;
                if (data_island_period)
                    data_island_first <= 0;
            end
        end

        localparam integer AUDIO_CLOCK_COUNTER_WIDTH = $clog2(VIDEO_RATE + AUDIO_RATE + 1);

        reg [AUDIO_CLOCK_COUNTER_WIDTH-1:0] audio_clock_counter;

        wire [AUDIO_CLOCK_COUNTER_WIDTH-1:0] rate = RUNTIME ? video_rate[AUDIO_CLOCK_COUNTER_WIDTH-1:0] :
                                                    VIDEO_RATE[AUDIO_CLOCK_COUNTER_WIDTH-1:0];
        wire audio_due = audio_clock_counter >= rate;   // changes with ce only
        assign audio_sample_en = audio_due && ce;       // one clk_pixel pulse per sample, for the user's sample register

        always @(posedge clk_pixel) if (ce) begin
            if (reset) begin
                audio_clock_counter <= 0;
            end else begin
                audio_clock_counter <= audio_clock_counter + AUDIO_RATE[AUDIO_CLOCK_COUNTER_WIDTH-1:0];

                if (audio_due)
                    audio_clock_counter <= audio_clock_counter + AUDIO_RATE[AUDIO_CLOCK_COUNTER_WIDTH-1:0] - rate;
            end
        end
    end
    else // DVI_OUTPUT = 1
    begin: dvi_output
        assign audio_sample_en = 1'b0;

        always @(posedge clk_pixel) if (ce)
        begin
            if (reset)
            begin
                mode <= 3'd0;
                video_data <= 24'd0;
                control_data <= 6'd0;
            end
            else
            begin
                mode <= de ? 3'd1 : 3'd0;
                video_data <= rgb;
                control_data <= {4'b0000, {vsync, hsync}}; // ctrl3, ctrl2, ctrl1, ctrl0, vsync, hsync
            end
        end
    end
endgenerate

// TMDS code production.
trx_tmds_channel #(.CN(0)) tmds_channel_0 (
    .clk_pixel(clk_pixel),
    .ce(ce),
    .video_data_in(video_data[7:0]),
    .data_island_data_in(data_island_data[3:0]),
    .control_data_in(control_data[1:0]),
    .mode_in(mode),
    .tmds(tmds_0)
);

trx_tmds_channel #(.CN(1)) tmds_channel_1 (
    .clk_pixel(clk_pixel),
    .ce(ce),
    .video_data_in(video_data[15:8]),
    .data_island_data_in(data_island_data[7:4]),
    .control_data_in(control_data[3:2]),
    .mode_in(mode),
    .tmds(tmds_1)
);

trx_tmds_channel #(.CN(2)) tmds_channel_2 (
    .clk_pixel(clk_pixel),
    .ce(ce),
    .video_data_in(video_data[23:16]),
    .data_island_data_in(data_island_data[11:8]),
    .control_data_in(control_data[5:4]),
    .mode_in(mode),
    .tmds(tmds_2)
);

endmodule
