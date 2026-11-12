// T92 S7: the RGB565 value of each CPC hardware ink number (0-31), made by tools/cpc_progs.py from the hardware-ink and colour tables
// (CPC hardware facts, the Gate Array's RGB values); the benches compare the frame buffer with it. Do not edit.
function [15:0] hw_rgb565(input [4:0] hw);
    case (hw)
        5'd0: hw_rgb565 = 16'h6BED;
        5'd1: hw_rgb565 = 16'h6BED;
        5'd2: hw_rgb565 = 16'h078D;
        5'd3: hw_rgb565 = 16'hF78D;
        5'd4: hw_rgb565 = 16'h000D;
        5'd5: hw_rgb565 = 16'hF00D;
        5'd6: hw_rgb565 = 16'h03CD;
        5'd7: hw_rgb565 = 16'hF3ED;
        5'd8: hw_rgb565 = 16'hF00D;
        5'd9: hw_rgb565 = 16'hF78D;
        5'd10: hw_rgb565 = 16'hF781;
        5'd11: hw_rgb565 = 16'hFF9F;
        5'd12: hw_rgb565 = 16'hF020;
        5'd13: hw_rgb565 = 16'hF01E;
        5'd14: hw_rgb565 = 16'hF3E1;
        5'd15: hw_rgb565 = 16'hFC1F;
        5'd16: hw_rgb565 = 16'h000D;
        5'd17: hw_rgb565 = 16'h078D;
        5'd18: hw_rgb565 = 16'h0780;
        5'd19: hw_rgb565 = 16'h0F9E;
        5'd20: hw_rgb565 = 16'h0000;
        5'd21: hw_rgb565 = 16'h081E;
        5'd22: hw_rgb565 = 16'h03C0;
        5'd23: hw_rgb565 = 16'h0BDE;
        5'd24: hw_rgb565 = 16'h680D;
        5'd25: hw_rgb565 = 16'h778D;
        5'd26: hw_rgb565 = 16'h77A0;
        5'd27: hw_rgb565 = 16'h779E;
        5'd28: hw_rgb565 = 16'h6800;
        5'd29: hw_rgb565 = 16'h681E;
        5'd30: hw_rgb565 = 16'h6BC0;
        5'd31: hw_rgb565 = 16'h6BDE;
        default: hw_rgb565 = 16'h0000;
    endcase
endfunction
