// TI-99/4A core: the host's C64 keyboard matrix (c64_keys[col*8+row], 1 = down) into Mega99's key_state[0:47] (TI matrix, index = column*8 + row,
// the TIKEY_ numbers of Mega99's spsrc/common/tikeys.h) and the joysticks. Written for this project.
// Mapping: by what is printed on the C64 keycap. Letters, digits, space, comma, period, RETURN (= ENTER) direct; SHIFT + letter = letter (the TI has no lower case).
// Unshifted: + = SHIFT+=, - = SHIFT+/, * = SHIFT+8, @ = SHIFT+2, up-arrow = ^ = SHIFT+6, : = SHIFT+;   Shifted digits as printed on the C64 key: ! # $ % as the TI,
// " = FCTN+P (SHIFT+2), & = SHIFT+7 (SHIFT+6), ' = FCTN+O (SHIFT+7), ( = SHIFT+9 (SHIFT+8), ) = SHIFT+0 (SHIFT+9), ? = FCTN+I (SHIFT+/), [ = FCTN+R (SHIFT+:), ] = FCTN+T (SHIFT+;);
// SHIFT+, = <, SHIFT+. = >, SHIFT+= = +.  CTRL -> CTRL; C= -> FCTN (so C= + P is also ").  left-arrow = FCTN+= (QUIT).
// DEL = FCTN+1 (DEL), SHIFT+DEL = FCTN+2 (INS), HOME = FCTN+5 (BEGIN), RUN/STOP = FCTN+4 (CLEAR), RESTORE = FCTN+9 (BACK);
// cursor right = FCTN+D, cursor down = FCTN+X, shifted: left = FCTN+S, up = FCTN+E. SHIFT is taken off while an FCTN key is down.
`timescale 1ns / 1ps
module ti99_keys (
    input  wire [63:0] keys,
    input  wire        restore,
    input  wire [4:0]  joy1,          // C64 order: 0 up, 1 down, 2 left, 3 right, 4 fire (1 = pressed)
    input  wire [4:0]  joy2,
    output wire [0:47] key_state,
    output wire [0:4]  tj1,            // TI joystick rows: fire, left, right, down, up (1 = pressed)
    output wire [0:4]  tj2
);
    wire shift_c = keys[15] | keys[52];
    wire crr = keys[2], crd = keys[7];
    wire curs = crr | crd;
    wire cur_l = crr & shift_c, cur_r = crr & ~shift_c, cur_u = crd & shift_c, cur_d = crd & ~shift_c;
    wire sh = shift_c;
    // shifted C64 keys that are other symbols on the TI
    wire d2q = sh & keys[59];   // "  = FCTN+P
    wire d6a = sh & keys[19];   // &  = SHIFT+7
    wire d7p = sh & keys[24];   // '  = FCTN+O
    wire d8p = sh & keys[27];   // (  = SHIFT+9
    wire d9p = sh & keys[32];   // )  = SHIFT+0
    wire qst = sh & keys[55];   // ?  = FCTN+I
    wire lbr = sh & keys[45];   // [  = FCTN+R
    wire rbr = sh & keys[50];   // ]  = FCTN+T
    wire colon = ~sh & keys[45];   // :  = SHIFT+;
    wire f_shift = keys[43] | keys[49] | keys[46] | keys[54] | keys[40] | colon | d6a | d8p | d9p;   // - * @ ^ + : & ( ) need SHIFT
    wire sym_fctn = d2q | d7p | qst | lbr | rbr;
    wire f_fctn  = keys[61] | keys[0] | keys[51] | keys[63] | keys[57] | restore | curs | sym_fctn;
    wire no_shift = keys[0] | keys[51] | keys[63] | keys[57] | restore | curs | sym_fctn;   // the user's SHIFT is not passed on with these FCTN keys (C= alone does not take it off)

    assign key_state[0] = keys[40] | keys[53] | keys[57];   // Equals
    assign key_state[1] = keys[60];   // Space
    assign key_state[2] = keys[1];   // Enter
    assign key_state[4] = f_fctn;   // Fctn
    assign key_state[5] = (sh & ~no_shift) | f_shift;   // Shift
    assign key_state[6] = keys[58];   // Ctrl
    assign key_state[8] = keys[44];   // Period
    assign key_state[9] = keys[42];   // L
    assign key_state[10] = keys[38] | d7p;   // O
    assign key_state[11] = (keys[32] & ~d9p) | d8p | restore;   // k9
    assign key_state[12] = (keys[59] & ~d2q) | keys[46] | (keys[0] & sh);   // k2
    assign key_state[13] = keys[13] | cur_l;   // S
    assign key_state[14] = keys[9];   // W
    assign key_state[15] = keys[23] | cur_d;   // X
    assign key_state[16] = keys[47];   // Comma
    assign key_state[17] = keys[37];   // K
    assign key_state[18] = keys[33] | qst;   // I
    assign key_state[19] = (keys[27] & ~d8p) | keys[49];   // k8
    assign key_state[20] = keys[8];   // k3
    assign key_state[21] = keys[18] | cur_r;   // D
    assign key_state[22] = keys[14] | cur_u;   // E
    assign key_state[23] = keys[20];   // C
    assign key_state[24] = keys[36];   // M
    assign key_state[25] = keys[34];   // J
    assign key_state[26] = keys[30];   // U
    assign key_state[27] = (keys[24] & ~d7p) | d6a;   // k7
    assign key_state[28] = keys[11] | keys[63];   // k4
    assign key_state[29] = keys[21];   // F
    assign key_state[30] = keys[17] | lbr;   // R
    assign key_state[31] = keys[31];   // V
    assign key_state[32] = keys[39];   // N
    assign key_state[33] = keys[29];   // H
    assign key_state[34] = keys[25];   // Y
    assign key_state[35] = (keys[19] & ~d6a) | keys[54];   // k6
    assign key_state[36] = keys[16] | keys[51];   // k5
    assign key_state[37] = keys[26];   // G
    assign key_state[38] = keys[22] | rbr;   // T
    assign key_state[39] = keys[28];   // B
    assign key_state[40] = (keys[55] & ~qst) | keys[43];   // Slash
    assign key_state[41] = (keys[50] & ~rbr) | (keys[45] & ~lbr);   // Semic
    assign key_state[42] = keys[41] | d2q;   // P
    assign key_state[43] = keys[35] | d9p;   // k0
    assign key_state[44] = keys[56] | (keys[0] & ~sh);   // k1
    assign key_state[45] = keys[10];   // A
    assign key_state[46] = keys[62];   // Q
    assign key_state[47] = keys[12];   // Z
    assign key_state[3] = 1'b0;
    assign key_state[7] = 1'b0;
    assign tj1 = {joy1[4], joy1[2], joy1[3], joy1[1], joy1[0]};
    assign tj2 = {joy2[4], joy2[2], joy2[3], joy2[1], joy2[0]};
endmodule
