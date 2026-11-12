// T95: the NES's 6502 (T65, VHDL, rtl/t65) converted by GHDL --synth --out=verilog; tools/nes_gen.py --vhdl made this
// file from the VHDL whose hash is in t65_nes.vhd.sha256. Do not edit.
module t65_alu
  (input  [1:0] mode,
   input  bcd_en,
   input  [4:0] op,
   input  [7:0] busa,
   input  [7:0] busb,
   input  [7:0] p_in,
   output [7:0] p_out,
   output [7:0] q);
  wire adc_z;
  wire adc_c;
  wire adc_v;
  wire adc_n;
  wire [7:0] adc_q;
  wire sbc_z;
  wire sbc_c;
  wire sbc_v;
  wire sbc_n;
  wire [7:0] sbc_q;
  wire [7:0] sbx_q;
  wire [3:0] n3204_o;
  wire n3205_o;
  wire [4:0] n3206_o;
  wire [6:0] n3207_o;
  wire [3:0] n3208_o;
  wire [4:0] n3210_o;
  wire [6:0] n3211_o;
  wire [6:0] n3212_o;
  wire [3:0] n3213_o;
  wire n3214_o;
  wire [4:0] n3215_o;
  wire [6:0] n3216_o;
  wire [3:0] n3217_o;
  wire [4:0] n3219_o;
  wire [6:0] n3220_o;
  wire [6:0] n3221_o;
  wire [3:0] n3222_o;
  wire n3224_o;
  wire [3:0] n3225_o;
  wire n3227_o;
  wire n3228_o;
  wire n3231_o;
  wire [4:0] n3232_o;
  wire n3234_o;
  wire n3235_o;
  wire n3236_o;
  wire n3237_o;
  wire [5:0] n3238_o;
  wire [5:0] n3240_o;
  wire [5:0] n3241_o;
  wire [5:0] n3242_o;
  wire n3243_o;
  wire [6:0] n3244_o;
  wire n3245_o;
  wire [6:0] n3246_o;
  wire n3247_o;
  wire n3248_o;
  wire [3:0] n3249_o;
  wire [4:0] n3250_o;
  wire [6:0] n3251_o;
  wire [3:0] n3252_o;
  wire [4:0] n3254_o;
  wire [6:0] n3255_o;
  wire [6:0] n3256_o;
  wire n3257_o;
  wire n3258_o;
  wire n3259_o;
  wire n3260_o;
  wire n3261_o;
  wire n3262_o;
  wire n3263_o;
  wire n3264_o;
  wire n3265_o;
  wire [4:0] n3266_o;
  wire n3268_o;
  wire n3269_o;
  wire n3270_o;
  wire n3271_o;
  wire [5:0] n3272_o;
  wire [5:0] n3274_o;
  wire [5:0] n3275_o;
  wire [5:0] n3276_o;
  wire n3277_o;
  wire [6:0] n3278_o;
  wire n3279_o;
  wire [6:0] n3280_o;
  wire n3281_o;
  wire n3282_o;
  wire [6:0] n3283_o;
  wire [3:0] n3284_o;
  wire [6:0] n3285_o;
  wire [3:0] n3286_o;
  wire [7:0] n3287_o;
  wire n3298_o;
  wire n3300_o;
  wire n3301_o;
  wire n3303_o;
  wire n3304_o;
  wire n3306_o;
  wire n3307_o;
  wire n3309_o;
  wire n3310_o;
  wire n3312_o;
  wire n3313_o;
  wire n3315_o;
  wire n3316_o;
  wire n3319_o;
  wire n3321_o;
  wire n3322_o;
  wire n3323_o;
  wire [3:0] n3324_o;
  wire [4:0] n3325_o;
  wire [6:0] n3326_o;
  wire [3:0] n3327_o;
  wire [4:0] n3329_o;
  wire [5:0] n3330_o;
  wire [6:0] n3331_o;
  wire [6:0] n3332_o;
  wire [3:0] n3333_o;
  wire [4:0] n3335_o;
  wire [5:0] n3336_o;
  wire [3:0] n3337_o;
  wire n3338_o;
  wire [4:0] n3339_o;
  wire [5:0] n3340_o;
  wire [5:0] n3341_o;
  wire [3:0] n3342_o;
  wire n3344_o;
  wire [3:0] n3345_o;
  wire n3347_o;
  wire n3348_o;
  wire n3351_o;
  wire n3352_o;
  wire n3353_o;
  wire n3354_o;
  wire n3355_o;
  wire n3356_o;
  wire n3357_o;
  wire n3358_o;
  wire n3359_o;
  wire n3360_o;
  wire n3361_o;
  wire [3:0] n3362_o;
  wire [3:0] n3363_o;
  wire [7:0] n3364_o;
  wire n3365_o;
  wire n3366_o;
  wire n3367_o;
  wire [4:0] n3368_o;
  wire [4:0] n3370_o;
  wire [4:0] n3371_o;
  wire [4:0] n3372_o;
  wire [3:0] n3373_o;
  wire [4:0] n3375_o;
  wire [5:0] n3376_o;
  wire [3:0] n3377_o;
  wire n3378_o;
  wire n3379_o;
  wire [6:0] n3380_o;
  wire n3381_o;
  wire [4:0] n3382_o;
  wire [5:0] n3383_o;
  wire [5:0] n3384_o;
  wire n3385_o;
  wire [4:0] n3386_o;
  wire [4:0] n3388_o;
  wire [4:0] n3389_o;
  wire [4:0] n3390_o;
  wire n3391_o;
  wire [4:0] n3392_o;
  wire [4:0] n3393_o;
  wire n3394_o;
  wire n3395_o;
  wire [5:0] n3396_o;
  wire [5:0] n3397_o;
  wire [3:0] n3398_o;
  wire [6:0] n3399_o;
  wire [3:0] n3400_o;
  wire [7:0] n3401_o;
  wire [7:0] n3408_o;
  wire n3410_o;
  wire [7:0] n3411_o;
  wire n3413_o;
  wire [7:0] n3414_o;
  wire n3416_o;
  wire n3418_o;
  wire n3420_o;
  wire n3422_o;
  wire n3424_o;
  wire [6:0] n3425_o;
  wire [7:0] n3427_o;
  wire n3428_o;
  wire n3430_o;
  wire [6:0] n3431_o;
  wire n3432_o;
  wire [7:0] n3433_o;
  wire n3434_o;
  wire n3436_o;
  wire [6:0] n3437_o;
  wire [7:0] n3439_o;
  wire n3440_o;
  wire n3442_o;
  wire n3443_o;
  wire [6:0] n3444_o;
  wire [7:0] n3445_o;
  wire n3446_o;
  wire n3448_o;
  wire n3449_o;
  wire [6:0] n3450_o;
  wire [6:0] n3451_o;
  wire [6:0] n3452_o;
  wire [7:0] n3453_o;
  wire n3454_o;
  wire n3455_o;
  wire n3456_o;
  wire n3457_o;
  wire n3458_o;
  wire [3:0] n3459_o;
  wire [3:0] n3460_o;
  wire [3:0] n3461_o;
  wire n3463_o;
  wire [3:0] n3464_o;
  wire [3:0] n3466_o;
  wire [3:0] n3467_o;
  wire [3:0] n3468_o;
  wire [3:0] n3469_o;
  wire [3:0] n3470_o;
  wire [3:0] n3471_o;
  wire n3473_o;
  wire [3:0] n3474_o;
  wire [3:0] n3476_o;
  wire n3479_o;
  wire [3:0] n3480_o;
  wire [3:0] n3481_o;
  wire n3482_o;
  wire n3483_o;
  wire [7:0] n3484_o;
  wire [7:0] n3485_o;
  wire n3487_o;
  wire n3488_o;
  wire n3490_o;
  wire [7:0] n3492_o;
  wire n3494_o;
  wire [7:0] n3496_o;
  wire n3498_o;
  wire [14:0] n3499_o;
  wire n3500_o;
  reg n3501_o;
  wire n3502_o;
  reg n3503_o;
  wire n3505_o;
  reg [7:0] n3507_o;
  reg [7:0] n3508_o;
  wire n3510_o;
  wire n3512_o;
  wire n3514_o;
  wire n3515_o;
  wire n3517_o;
  wire n3518_o;
  wire n3520_o;
  wire n3521_o;
  wire [7:0] n3522_o;
  wire n3524_o;
  wire n3527_o;
  wire n3529_o;
  wire n3530_o;
  wire n3531_o;
  wire n3533_o;
  wire n3536_o;
  wire n3538_o;
  wire n3539_o;
  wire n3541_o;
  wire n3544_o;
  wire [4:0] n3545_o;
  reg n3546_o;
  wire n3547_o;
  reg n3548_o;
  reg n3549_o;
  wire [3:0] n3550_o;
  wire n3552_o;
  wire [7:0] n3553_o;
  wire [7:0] n3555_o;
  assign p_out = n3555_o; //(module output)
  assign q = n3553_o; //(module output)
  /* T65_ALU.vhd:73:15  */
  assign adc_z = n3231_o; // (signal)
  /* T65_ALU.vhd:74:15  */
  assign adc_c = n3282_o; // (signal)
  /* T65_ALU.vhd:75:15  */
  assign adc_v = n3265_o; // (signal)
  /* T65_ALU.vhd:76:15  */
  assign adc_n = n3257_o; // (signal)
  /* T65_ALU.vhd:77:15  */
  assign adc_q = n3287_o; // (signal)
  /* T65_ALU.vhd:78:15  */
  assign sbc_z = n3351_o; // (signal)
  /* T65_ALU.vhd:79:15  */
  assign sbc_c = n3353_o; // (signal)
  /* T65_ALU.vhd:80:15  */
  assign sbc_v = n3360_o; // (signal)
  /* T65_ALU.vhd:81:15  */
  assign sbc_n = n3361_o; // (signal)
  /* T65_ALU.vhd:82:15  */
  assign sbc_q = n3401_o; // (signal)
  /* T65_ALU.vhd:83:15  */
  assign sbx_q = n3364_o; // (signal)
  /* T65_ALU.vhd:92:31  */
  assign n3204_o = busa[3:0]; // extract
  /* T65_ALU.vhd:92:50  */
  assign n3205_o = p_in[0]; // extract
  /* T65_ALU.vhd:92:44  */
  assign n3206_o = {n3204_o, n3205_o};
  /* T65_ALU.vhd:92:11  */
  assign n3207_o = {2'b0, n3206_o};  //  uext
  /* T65_ALU.vhd:92:86  */
  assign n3208_o = busb[3:0]; // extract
  /* T65_ALU.vhd:92:99  */
  assign n3210_o = {n3208_o, 1'b1};
  /* T65_ALU.vhd:92:66  */
  assign n3211_o = {2'b0, n3210_o};  //  uext
  /* T65_ALU.vhd:92:64  */
  assign n3212_o = n3207_o + n3211_o;
  /* T65_ALU.vhd:93:31  */
  assign n3213_o = busa[7:4]; // extract
  /* T65_ALU.vhd:93:48  */
  assign n3214_o = n3212_o[5]; // extract
  /* T65_ALU.vhd:93:44  */
  assign n3215_o = {n3213_o, n3214_o};
  /* T65_ALU.vhd:93:11  */
  assign n3216_o = {2'b0, n3215_o};  //  uext
  /* T65_ALU.vhd:93:79  */
  assign n3217_o = busb[7:4]; // extract
  /* T65_ALU.vhd:93:92  */
  assign n3219_o = {n3217_o, 1'b1};
  /* T65_ALU.vhd:93:59  */
  assign n3220_o = {2'b0, n3219_o};  //  uext
  /* T65_ALU.vhd:93:57  */
  assign n3221_o = n3216_o + n3220_o;
  /* T65_ALU.vhd:100:10  */
  assign n3222_o = n3212_o[4:1]; // extract
  /* T65_ALU.vhd:100:23  */
  assign n3224_o = n3222_o == 4'b0000;
  /* T65_ALU.vhd:100:33  */
  assign n3225_o = n3221_o[4:1]; // extract
  /* T65_ALU.vhd:100:46  */
  assign n3227_o = n3225_o == 4'b0000;
  /* T65_ALU.vhd:100:27  */
  assign n3228_o = n3227_o & n3224_o;
  /* T65_ALU.vhd:100:5  */
  assign n3231_o = n3228_o ? 1'b1 : 1'b0;
  /* T65_ALU.vhd:106:10  */
  assign n3232_o = n3212_o[5:1]; // extract
  /* T65_ALU.vhd:106:23  */
  assign n3234_o = $unsigned(n3232_o) > $unsigned(5'b01001);
  /* T65_ALU.vhd:106:35  */
  assign n3235_o = p_in[3]; // extract
  /* T65_ALU.vhd:106:27  */
  assign n3236_o = n3235_o & n3234_o;
  /* T65_ALU.vhd:106:50  */
  assign n3237_o = bcd_en & n3236_o;
  /* T65_ALU.vhd:107:27  */
  assign n3238_o = n3212_o[6:1]; // extract
  /* T65_ALU.vhd:107:40  */
  assign n3240_o = n3238_o + 6'b000110;
  assign n3241_o = n3212_o[6:1]; // extract
  /* T65_ALU.vhd:106:5  */
  assign n3242_o = n3237_o ? n3240_o : n3241_o;
  assign n3243_o = n3212_o[0]; // extract
  /* T65_MCode.vhd:452:15  */
  assign n3244_o = {n3242_o, n3243_o};
  /* T65_ALU.vhd:110:12  */
  assign n3245_o = n3244_o[6]; // extract
  assign n3246_o = {n3242_o, n3243_o};
  /* T65_ALU.vhd:110:21  */
  assign n3247_o = n3246_o[5]; // extract
  /* T65_ALU.vhd:110:16  */
  assign n3248_o = n3245_o | n3247_o;
  /* T65_ALU.vhd:111:31  */
  assign n3249_o = busa[7:4]; // extract
  /* T65_ALU.vhd:111:44  */
  assign n3250_o = {n3249_o, n3248_o};
  /* T65_ALU.vhd:111:11  */
  assign n3251_o = {2'b0, n3250_o};  //  uext
  /* T65_ALU.vhd:111:75  */
  assign n3252_o = busb[7:4]; // extract
  /* T65_ALU.vhd:111:88  */
  assign n3254_o = {n3252_o, 1'b1};
  /* T65_ALU.vhd:111:55  */
  assign n3255_o = {2'b0, n3254_o};  //  uext
  /* T65_ALU.vhd:111:53  */
  assign n3256_o = n3251_o + n3255_o;
  /* T65_ALU.vhd:113:16  */
  assign n3257_o = n3256_o[4]; // extract
  /* T65_ALU.vhd:114:17  */
  assign n3258_o = n3256_o[4]; // extract
  /* T65_ALU.vhd:114:29  */
  assign n3259_o = busa[7]; // extract
  /* T65_ALU.vhd:114:21  */
  assign n3260_o = n3258_o ^ n3259_o;
  /* T65_ALU.vhd:114:47  */
  assign n3261_o = busa[7]; // extract
  /* T65_ALU.vhd:114:59  */
  assign n3262_o = busb[7]; // extract
  /* T65_ALU.vhd:114:51  */
  assign n3263_o = n3261_o ^ n3262_o;
  /* T65_ALU.vhd:114:38  */
  assign n3264_o = ~n3263_o;
  /* T65_ALU.vhd:114:34  */
  assign n3265_o = n3260_o & n3264_o;
  /* T65_ALU.vhd:120:10  */
  assign n3266_o = n3256_o[5:1]; // extract
  /* T65_ALU.vhd:120:23  */
  assign n3268_o = $unsigned(n3266_o) > $unsigned(5'b01001);
  /* T65_ALU.vhd:120:35  */
  assign n3269_o = p_in[3]; // extract
  /* T65_ALU.vhd:120:27  */
  assign n3270_o = n3269_o & n3268_o;
  /* T65_ALU.vhd:120:50  */
  assign n3271_o = bcd_en & n3270_o;
  /* T65_ALU.vhd:121:27  */
  assign n3272_o = n3256_o[6:1]; // extract
  /* T65_ALU.vhd:121:40  */
  assign n3274_o = n3272_o + 6'b000110;
  assign n3275_o = n3256_o[6:1]; // extract
  /* T65_ALU.vhd:120:5  */
  assign n3276_o = n3271_o ? n3274_o : n3275_o;
  assign n3277_o = n3256_o[0]; // extract
  assign n3278_o = {n3276_o, n3277_o};
  /* T65_ALU.vhd:124:16  */
  assign n3279_o = n3278_o[6]; // extract
  assign n3280_o = {n3276_o, n3277_o};
  /* T65_ALU.vhd:124:25  */
  assign n3281_o = n3280_o[5]; // extract
  /* T65_ALU.vhd:124:20  */
  assign n3282_o = n3279_o | n3281_o;
  assign n3283_o = {n3276_o, n3277_o};
  /* T65_ALU.vhd:126:33  */
  assign n3284_o = n3283_o[4:1]; // extract
  assign n3285_o = {n3242_o, n3243_o};
  /* T65_ALU.vhd:126:50  */
  assign n3286_o = n3285_o[4:1]; // extract
  /* T65_ALU.vhd:126:46  */
  assign n3287_o = {n3284_o, n3286_o};
  /* T65_ALU.vhd:136:12  */
  assign n3298_o = op == 5'b00001;
  /* T65_ALU.vhd:137:12  */
  assign n3300_o = op == 5'b00011;
  /* T65_ALU.vhd:136:24  */
  assign n3301_o = n3298_o | n3300_o;
  /* T65_ALU.vhd:138:12  */
  assign n3303_o = op == 5'b00101;
  /* T65_ALU.vhd:137:24  */
  assign n3304_o = n3301_o | n3303_o;
  /* T65_ALU.vhd:139:12  */
  assign n3306_o = op == 5'b00111;
  /* T65_ALU.vhd:138:24  */
  assign n3307_o = n3304_o | n3306_o;
  /* T65_ALU.vhd:140:12  */
  assign n3309_o = op == 5'b01001;
  /* T65_ALU.vhd:139:24  */
  assign n3310_o = n3307_o | n3309_o;
  /* T65_ALU.vhd:141:12  */
  assign n3312_o = op == 5'b01011;
  /* T65_ALU.vhd:140:24  */
  assign n3313_o = n3310_o | n3312_o;
  /* T65_ALU.vhd:143:12  */
  assign n3315_o = op == 5'b01110;
  /* T65_ALU.vhd:141:24  */
  assign n3316_o = n3313_o | n3315_o;
  /* T65_ALU.vhd:136:5  */
  assign n3319_o = n3316_o ? 1'b1 : 1'b0;
  /* T65_ALU.vhd:148:14  */
  assign n3321_o = p_in[0]; // extract
  /* T65_ALU.vhd:148:26  */
  assign n3322_o = ~n3319_o;
  /* T65_ALU.vhd:148:23  */
  assign n3323_o = n3321_o | n3322_o;
  /* T65_ALU.vhd:149:31  */
  assign n3324_o = busa[3:0]; // extract
  /* T65_ALU.vhd:149:44  */
  assign n3325_o = {n3324_o, n3323_o};
  /* T65_ALU.vhd:149:11  */
  assign n3326_o = {2'b0, n3325_o};  //  uext
  /* T65_ALU.vhd:149:75  */
  assign n3327_o = busb[3:0]; // extract
  /* T65_ALU.vhd:149:88  */
  assign n3329_o = {n3327_o, 1'b1};
  /* T65_ALU.vhd:149:55  */
  assign n3330_o = {1'b0, n3329_o};  //  uext
  /* T65_ALU.vhd:149:53  */
  assign n3331_o = {1'b0, n3330_o};  //  uext
  /* T65_ALU.vhd:149:53  */
  assign n3332_o = n3326_o - n3331_o;
  /* T65_ALU.vhd:150:31  */
  assign n3333_o = busa[7:4]; // extract
  /* T65_ALU.vhd:150:44  */
  assign n3335_o = {n3333_o, 1'b0};
  /* T65_ALU.vhd:150:11  */
  assign n3336_o = {1'b0, n3335_o};  //  uext
  /* T65_ALU.vhd:150:77  */
  assign n3337_o = busb[7:4]; // extract
  /* T65_ALU.vhd:150:94  */
  assign n3338_o = n3332_o[5]; // extract
  /* T65_ALU.vhd:150:90  */
  assign n3339_o = {n3337_o, n3338_o};
  /* T65_ALU.vhd:150:57  */
  assign n3340_o = {1'b0, n3339_o};  //  uext
  /* T65_ALU.vhd:150:55  */
  assign n3341_o = n3336_o - n3340_o;
  /* T65_ALU.vhd:157:10  */
  assign n3342_o = n3332_o[4:1]; // extract
  /* T65_ALU.vhd:157:23  */
  assign n3344_o = n3342_o == 4'b0000;
  /* T65_ALU.vhd:157:33  */
  assign n3345_o = n3341_o[4:1]; // extract
  /* T65_ALU.vhd:157:46  */
  assign n3347_o = n3345_o == 4'b0000;
  /* T65_ALU.vhd:157:27  */
  assign n3348_o = n3347_o & n3344_o;
  /* T65_ALU.vhd:157:5  */
  assign n3351_o = n3348_o ? 1'b1 : 1'b0;
  /* T65_ALU.vhd:163:20  */
  assign n3352_o = n3341_o[5]; // extract
  /* T65_ALU.vhd:163:14  */
  assign n3353_o = ~n3352_o;
  /* T65_ALU.vhd:164:17  */
  assign n3354_o = n3341_o[4]; // extract
  /* T65_ALU.vhd:164:29  */
  assign n3355_o = busa[7]; // extract
  /* T65_ALU.vhd:164:21  */
  assign n3356_o = n3354_o ^ n3355_o;
  /* T65_ALU.vhd:164:43  */
  assign n3357_o = busa[7]; // extract
  /* T65_ALU.vhd:164:55  */
  assign n3358_o = busb[7]; // extract
  /* T65_ALU.vhd:164:47  */
  assign n3359_o = n3357_o ^ n3358_o;
  /* T65_ALU.vhd:164:34  */
  assign n3360_o = n3356_o & n3359_o;
  /* T65_ALU.vhd:165:16  */
  assign n3361_o = n3341_o[4]; // extract
  /* T65_ALU.vhd:167:33  */
  assign n3362_o = n3341_o[4:1]; // extract
  /* T65_ALU.vhd:167:50  */
  assign n3363_o = n3332_o[4:1]; // extract
  /* T65_ALU.vhd:167:46  */
  assign n3364_o = {n3362_o, n3363_o};
  /* T65_ALU.vhd:169:12  */
  assign n3365_o = p_in[3]; // extract
  /* T65_ALU.vhd:169:27  */
  assign n3366_o = bcd_en & n3365_o;
  /* T65_ALU.vhd:170:12  */
  assign n3367_o = n3332_o[5]; // extract
  /* T65_ALU.vhd:171:29  */
  assign n3368_o = n3332_o[5:1]; // extract
  /* T65_ALU.vhd:171:42  */
  assign n3370_o = n3368_o - 5'b00110;
  assign n3371_o = n3332_o[5:1]; // extract
  /* T65_ALU.vhd:170:7  */
  assign n3372_o = n3367_o ? n3370_o : n3371_o;
  /* T65_ALU.vhd:173:33  */
  assign n3373_o = busa[7:4]; // extract
  /* T65_ALU.vhd:173:46  */
  assign n3375_o = {n3373_o, 1'b0};
  /* T65_ALU.vhd:173:13  */
  assign n3376_o = {1'b0, n3375_o};  //  uext
  /* T65_ALU.vhd:173:79  */
  assign n3377_o = busb[7:4]; // extract
  assign n3378_o = n3332_o[0]; // extract
  assign n3379_o = n3332_o[6]; // extract
  assign n3380_o = {n3379_o, n3372_o, n3378_o};
  /* T65_ALU.vhd:173:96  */
  assign n3381_o = n3380_o[6]; // extract
  /* T65_ALU.vhd:173:92  */
  assign n3382_o = {n3377_o, n3381_o};
  /* T65_ALU.vhd:173:59  */
  assign n3383_o = {1'b0, n3382_o};  //  uext
  /* T65_ALU.vhd:173:57  */
  assign n3384_o = n3376_o - n3383_o;
  /* T65_ALU.vhd:174:12  */
  assign n3385_o = n3384_o[5]; // extract
  /* T65_ALU.vhd:175:29  */
  assign n3386_o = n3384_o[5:1]; // extract
  /* T65_ALU.vhd:175:42  */
  assign n3388_o = n3386_o - 5'b00110;
  assign n3389_o = n3384_o[5:1]; // extract
  /* T65_ALU.vhd:174:7  */
  assign n3390_o = n3385_o ? n3388_o : n3389_o;
  assign n3391_o = n3384_o[0]; // extract
  assign n3392_o = n3332_o[5:1]; // extract
  /* T65_ALU.vhd:169:5  */
  assign n3393_o = n3366_o ? n3372_o : n3392_o;
  assign n3394_o = n3332_o[6]; // extract
  assign n3395_o = n3332_o[0]; // extract
  assign n3396_o = {n3390_o, n3391_o};
  /* T65_ALU.vhd:169:5  */
  assign n3397_o = n3366_o ? n3396_o : n3341_o;
  /* T65_ALU.vhd:179:33  */
  assign n3398_o = n3397_o[4:1]; // extract
  assign n3399_o = {n3394_o, n3393_o, n3395_o};
  /* T65_ALU.vhd:179:50  */
  assign n3400_o = n3399_o[4:1]; // extract
  /* T65_ALU.vhd:179:46  */
  assign n3401_o = {n3398_o, n3400_o};
  /* T65_ALU.vhd:196:21  */
  assign n3408_o = busa | busb;
  /* T65_ALU.vhd:195:7  */
  assign n3410_o = op == 5'b00000;
  /* T65_ALU.vhd:198:21  */
  assign n3411_o = busa & busb;
  /* T65_ALU.vhd:197:7  */
  assign n3413_o = op == 5'b00001;
  /* T65_ALU.vhd:200:21  */
  assign n3414_o = busa ^ busb;
  /* T65_ALU.vhd:199:7  */
  assign n3416_o = op == 5'b00010;
  /* T65_ALU.vhd:201:7  */
  assign n3418_o = op == 5'b00011;
  /* T65_ALU.vhd:205:7  */
  assign n3420_o = op == 5'b00110;
  /* T65_ALU.vhd:207:7  */
  assign n3422_o = op == 5'b10001;
  /* T65_ALU.vhd:210:7  */
  assign n3424_o = op == 5'b00111;
  /* T65_ALU.vhd:215:20  */
  assign n3425_o = busa[6:0]; // extract
  /* T65_ALU.vhd:215:33  */
  assign n3427_o = {n3425_o, 1'b0};
  /* T65_ALU.vhd:216:30  */
  assign n3428_o = busa[7]; // extract
  /* T65_ALU.vhd:214:7  */
  assign n3430_o = op == 5'b01000;
  /* T65_ALU.vhd:218:20  */
  assign n3431_o = busa[6:0]; // extract
  /* T65_ALU.vhd:218:39  */
  assign n3432_o = p_in[0]; // extract
  /* T65_ALU.vhd:218:33  */
  assign n3433_o = {n3431_o, n3432_o};
  /* T65_ALU.vhd:219:30  */
  assign n3434_o = busa[7]; // extract
  /* T65_ALU.vhd:217:7  */
  assign n3436_o = op == 5'b01001;
  /* T65_ALU.vhd:221:26  */
  assign n3437_o = busa[7:1]; // extract
  /* T65_ALU.vhd:221:20  */
  assign n3439_o = {1'b0, n3437_o};
  /* T65_ALU.vhd:222:30  */
  assign n3440_o = busa[0]; // extract
  /* T65_ALU.vhd:220:7  */
  assign n3442_o = op == 5'b01010;
  /* T65_ALU.vhd:224:20  */
  assign n3443_o = p_in[0]; // extract
  /* T65_ALU.vhd:224:35  */
  assign n3444_o = busa[7:1]; // extract
  /* T65_ALU.vhd:224:29  */
  assign n3445_o = {n3443_o, n3444_o};
  /* T65_ALU.vhd:225:30  */
  assign n3446_o = busa[0]; // extract
  /* T65_ALU.vhd:223:7  */
  assign n3448_o = op == 5'b01011;
  /* T65_ALU.vhd:227:20  */
  assign n3449_o = p_in[0]; // extract
  /* T65_ALU.vhd:227:36  */
  assign n3450_o = busa[7:1]; // extract
  /* T65_ALU.vhd:227:57  */
  assign n3451_o = busb[7:1]; // extract
  /* T65_ALU.vhd:227:49  */
  assign n3452_o = n3450_o & n3451_o;
  /* T65_ALU.vhd:227:29  */
  assign n3453_o = {n3449_o, n3452_o};
  /* T65_ALU.vhd:228:29  */
  assign n3454_o = n3453_o[5]; // extract
  /* T65_ALU.vhd:228:40  */
  assign n3455_o = n3453_o[6]; // extract
  /* T65_ALU.vhd:228:33  */
  assign n3456_o = n3454_o ^ n3455_o;
  /* T65_ALU.vhd:230:16  */
  assign n3457_o = p_in[3]; // extract
  /* T65_ALU.vhd:230:29  */
  assign n3458_o = bcd_en & n3457_o;
  /* T65_ALU.vhd:231:19  */
  assign n3459_o = busa[3:0]; // extract
  /* T65_ALU.vhd:231:40  */
  assign n3460_o = busb[3:0]; // extract
  /* T65_ALU.vhd:231:32  */
  assign n3461_o = n3459_o & n3460_o;
  /* T65_ALU.vhd:231:54  */
  assign n3463_o = $unsigned(n3461_o) > $unsigned(4'b0100);
  /* T65_ALU.vhd:232:62  */
  assign n3464_o = n3453_o[3:0]; // extract
  /* T65_ALU.vhd:232:76  */
  assign n3466_o = n3464_o + 4'b0110;
  assign n3467_o = n3453_o[3:0]; // extract
  /* T65_ALU.vhd:231:11  */
  assign n3468_o = n3463_o ? n3466_o : n3467_o;
  /* T65_ALU.vhd:234:19  */
  assign n3469_o = busa[7:4]; // extract
  /* T65_ALU.vhd:234:40  */
  assign n3470_o = busb[7:4]; // extract
  /* T65_ALU.vhd:234:32  */
  assign n3471_o = n3469_o & n3470_o;
  /* T65_ALU.vhd:234:54  */
  assign n3473_o = $unsigned(n3471_o) > $unsigned(4'b0100);
  /* T65_ALU.vhd:235:62  */
  assign n3474_o = n3453_o[7:4]; // extract
  /* T65_ALU.vhd:235:76  */
  assign n3476_o = n3474_o + 4'b0110;
  /* T65_ALU.vhd:234:11  */
  assign n3479_o = n3473_o ? 1'b1 : 1'b0;
  assign n3480_o = n3453_o[7:4]; // extract
  /* T65_ALU.vhd:234:11  */
  assign n3481_o = n3473_o ? n3476_o : n3480_o;
  /* T65_ALU.vhd:241:31  */
  assign n3482_o = n3453_o[6]; // extract
  /* T65_ALU.vhd:230:9  */
  assign n3483_o = n3458_o ? n3479_o : n3482_o;
  assign n3484_o = {n3481_o, n3468_o};
  /* T65_ALU.vhd:230:9  */
  assign n3485_o = n3458_o ? n3484_o : n3453_o;
  /* T65_ALU.vhd:226:7  */
  assign n3487_o = op == 5'b01111;
  /* T65_ALU.vhd:244:30  */
  assign n3488_o = busb[6]; // extract
  /* T65_ALU.vhd:243:7  */
  assign n3490_o = op == 5'b01100;
  /* T65_ALU.vhd:246:48  */
  assign n3492_o = busa - 8'b00000001;
  /* T65_ALU.vhd:245:7  */
  assign n3494_o = op == 5'b01101;
  /* T65_ALU.vhd:248:48  */
  assign n3496_o = busa + 8'b00000001;
  /* T65_ALU.vhd:247:7  */
  assign n3498_o = op == 5'b01110;
  assign n3499_o = {n3498_o, n3494_o, n3490_o, n3487_o, n3448_o, n3442_o, n3436_o, n3430_o, n3424_o, n3422_o, n3420_o, n3418_o, n3416_o, n3413_o, n3410_o};
  assign n3500_o = p_in[0]; // extract
  /* T65_ALU.vhd:194:5  */
  always @*
    case (n3499_o)
      15'b100000000000000: n3501_o = n3500_o;
      15'b010000000000000: n3501_o = n3500_o;
      15'b001000000000000: n3501_o = n3500_o;
      15'b000100000000000: n3501_o = n3483_o;
      15'b000010000000000: n3501_o = n3446_o;
      15'b000001000000000: n3501_o = n3440_o;
      15'b000000100000000: n3501_o = n3434_o;
      15'b000000010000000: n3501_o = n3428_o;
      15'b000000001000000: n3501_o = sbc_c;
      15'b000000000100000: n3501_o = sbc_c;
      15'b000000000010000: n3501_o = sbc_c;
      15'b000000000001000: n3501_o = adc_c;
      15'b000000000000100: n3501_o = n3500_o;
      15'b000000000000010: n3501_o = n3500_o;
      15'b000000000000001: n3501_o = n3500_o;
      default: n3501_o = n3500_o;
    endcase
  assign n3502_o = p_in[6]; // extract
  /* T65_ALU.vhd:194:5  */
  always @*
    case (n3499_o)
      15'b100000000000000: n3503_o = n3502_o;
      15'b010000000000000: n3503_o = n3502_o;
      15'b001000000000000: n3503_o = n3488_o;
      15'b000100000000000: n3503_o = n3456_o;
      15'b000010000000000: n3503_o = n3502_o;
      15'b000001000000000: n3503_o = n3502_o;
      15'b000000100000000: n3503_o = n3502_o;
      15'b000000010000000: n3503_o = n3502_o;
      15'b000000001000000: n3503_o = sbc_v;
      15'b000000000100000: n3503_o = n3502_o;
      15'b000000000010000: n3503_o = n3502_o;
      15'b000000000001000: n3503_o = adc_v;
      15'b000000000000100: n3503_o = n3502_o;
      15'b000000000000010: n3503_o = n3502_o;
      15'b000000000000001: n3503_o = n3502_o;
      default: n3503_o = n3502_o;
    endcase
  assign n3505_o = p_in[7]; // extract
  /* T65_ALU.vhd:194:5  */
  always @*
    case (n3499_o)
      15'b100000000000000: n3507_o = n3496_o;
      15'b010000000000000: n3507_o = n3492_o;
      15'b001000000000000: n3507_o = busa;
      15'b000100000000000: n3507_o = n3453_o;
      15'b000010000000000: n3507_o = n3445_o;
      15'b000001000000000: n3507_o = n3439_o;
      15'b000000100000000: n3507_o = n3433_o;
      15'b000000010000000: n3507_o = n3427_o;
      15'b000000001000000: n3507_o = sbc_q;
      15'b000000000100000: n3507_o = sbx_q;
      15'b000000000010000: n3507_o = busa;
      15'b000000000001000: n3507_o = adc_q;
      15'b000000000000100: n3507_o = n3414_o;
      15'b000000000000010: n3507_o = n3411_o;
      15'b000000000000001: n3507_o = n3408_o;
      default: n3507_o = busa;
    endcase
  /* T65_ALU.vhd:194:5  */
  always @*
    case (n3499_o)
      15'b100000000000000: n3508_o = busa;
      15'b010000000000000: n3508_o = busa;
      15'b001000000000000: n3508_o = busa;
      15'b000100000000000: n3508_o = n3485_o;
      15'b000010000000000: n3508_o = busa;
      15'b000001000000000: n3508_o = busa;
      15'b000000100000000: n3508_o = busa;
      15'b000000010000000: n3508_o = busa;
      15'b000000001000000: n3508_o = busa;
      15'b000000000100000: n3508_o = busa;
      15'b000000000010000: n3508_o = busa;
      15'b000000000001000: n3508_o = busa;
      15'b000000000000100: n3508_o = busa;
      15'b000000000000010: n3508_o = busa;
      15'b000000000000001: n3508_o = busa;
      default: n3508_o = busa;
    endcase
  /* T65_ALU.vhd:255:7  */
  assign n3510_o = op == 5'b00011;
  /* T65_ALU.vhd:258:7  */
  assign n3512_o = op == 5'b00110;
  /* T65_ALU.vhd:258:22  */
  assign n3514_o = op == 5'b00111;
  /* T65_ALU.vhd:258:22  */
  assign n3515_o = n3512_o | n3514_o;
  /* T65_ALU.vhd:258:33  */
  assign n3517_o = op == 5'b10001;
  /* T65_ALU.vhd:258:33  */
  assign n3518_o = n3515_o | n3517_o;
  /* T65_ALU.vhd:261:7  */
  assign n3520_o = op == 5'b00100;
  /* T65_ALU.vhd:263:30  */
  assign n3521_o = busb[7]; // extract
  /* T65_ALU.vhd:264:18  */
  assign n3522_o = busa & busb;
  /* T65_ALU.vhd:264:28  */
  assign n3524_o = n3522_o == 8'b00000000;
  /* T65_ALU.vhd:264:9  */
  assign n3527_o = n3524_o ? 1'b1 : 1'b0;
  /* T65_ALU.vhd:262:7  */
  assign n3529_o = op == 5'b01100;
  /* T65_ALU.vhd:270:29  */
  assign n3530_o = n3507_o[7]; // extract
  /* T65_ALU.vhd:271:29  */
  assign n3531_o = n3507_o[7]; // extract
  /* T65_ALU.vhd:272:16  */
  assign n3533_o = n3507_o == 8'b00000000;
  /* T65_ALU.vhd:272:9  */
  assign n3536_o = n3533_o ? 1'b1 : 1'b0;
  /* T65_ALU.vhd:269:7  */
  assign n3538_o = op == 5'b10000;
  /* T65_ALU.vhd:278:29  */
  assign n3539_o = n3507_o[7]; // extract
  /* T65_ALU.vhd:279:16  */
  assign n3541_o = n3507_o == 8'b00000000;
  /* T65_ALU.vhd:279:9  */
  assign n3544_o = n3541_o ? 1'b1 : 1'b0;
  assign n3545_o = {n3538_o, n3529_o, n3520_o, n3518_o, n3510_o};
  /* T65_ALU.vhd:254:5  */
  always @*
    case (n3545_o)
      5'b10000: n3546_o = n3531_o;
      5'b01000: n3546_o = n3501_o;
      5'b00100: n3546_o = n3501_o;
      5'b00010: n3546_o = n3501_o;
      5'b00001: n3546_o = n3501_o;
      default: n3546_o = n3501_o;
    endcase
  assign n3547_o = p_in[1]; // extract
  /* T65_ALU.vhd:254:5  */
  always @*
    case (n3545_o)
      5'b10000: n3548_o = n3536_o;
      5'b01000: n3548_o = n3527_o;
      5'b00100: n3548_o = n3547_o;
      5'b00010: n3548_o = sbc_z;
      5'b00001: n3548_o = adc_z;
      default: n3548_o = n3544_o;
    endcase
  /* T65_ALU.vhd:254:5  */
  always @*
    case (n3545_o)
      5'b10000: n3549_o = n3530_o;
      5'b01000: n3549_o = n3521_o;
      5'b00100: n3549_o = n3505_o;
      5'b00010: n3549_o = sbc_n;
      5'b00001: n3549_o = adc_n;
      default: n3549_o = n3539_o;
    endcase
  assign n3550_o = p_in[5:2]; // extract
  /* T65_ALU.vhd:286:10  */
  assign n3552_o = op == 5'b01111;
  /* T65_ALU.vhd:286:5  */
  assign n3553_o = n3552_o ? n3508_o : n3507_o;
  assign n3555_o = {n3549_o, n3503_o, n3550_o, n3548_o, n3546_o};
endmodule

module t65_mcode
  (input  [1:0] mode,
   input  bcd_en,
   input  [7:0] ir,
   input  [2:0] mcycle,
   input  [7:0] p,
   input  rdy_mod,
   output [2:0] lcycle,
   output [4:0] alu_op,
   output [3:0] set_busa_to,
   output [1:0] set_addr_to,
   output [3:0] write_data,
   output [1:0] jump,
   output [1:0] baadd,
   output [1:0] baquirk,
   output breakatna,
   output adadd,
   output addy,
   output pcadd,
   output inc_s,
   output dec_s,
   output lda,
   output ldp,
   output ldx,
   output ldy,
   output lds,
   output lddi,
   output ldalu,
   output ldad,
   output ldbal,
   output ldbah,
   output savep,
   output write);
  wire branch;
  wire alumore;
  wire [2:0] n997_o;
  wire n998_o;
  wire n999_o;
  wire n1001_o;
  wire n1002_o;
  wire n1004_o;
  wire n1005_o;
  wire n1006_o;
  wire n1008_o;
  wire n1009_o;
  wire n1011_o;
  wire n1012_o;
  wire n1013_o;
  wire n1015_o;
  wire n1016_o;
  wire n1018_o;
  wire n1019_o;
  wire n1020_o;
  wire n1022_o;
  wire n1023_o;
  wire [6:0] n1024_o;
  reg n1025_o;
  wire [2:0] n1028_o;
  wire [1:0] n1029_o;
  wire [2:0] n1030_o;
  wire n1032_o;
  wire n1033_o;
  wire [3:0] n1036_o;
  wire [3:0] n1038_o;
  wire n1040_o;
  wire [2:0] n1041_o;
  wire n1043_o;
  wire n1044_o;
  wire [3:0] n1047_o;
  wire [3:0] n1049_o;
  wire n1051_o;
  wire [2:0] n1052_o;
  wire n1054_o;
  wire [3:0] n1057_o;
  wire n1060_o;
  wire [2:0] n1061_o;
  wire n1063_o;
  wire [2:0] n1064_o;
  wire n1066_o;
  wire n1067_o;
  wire [2:0] n1068_o;
  wire n1070_o;
  wire n1071_o;
  wire n1072_o;
  wire [3:0] n1075_o;
  wire [3:0] n1077_o;
  wire n1079_o;
  wire [2:0] n1080_o;
  reg [3:0] n1084_o;
  reg [3:0] n1086_o;
  reg n1088_o;
  wire n1090_o;
  wire [1:0] n1091_o;
  wire n1092_o;
  wire n1094_o;
  wire n1095_o;
  wire n1097_o;
  wire n1098_o;
  wire n1101_o;
  wire n1103_o;
  wire n1105_o;
  wire n1107_o;
  wire [2:0] n1108_o;
  wire n1110_o;
  wire [3:0] n1113_o;
  wire n1116_o;
  wire [2:0] n1117_o;
  reg [3:0] n1119_o;
  reg n1124_o;
  reg n1128_o;
  reg n1130_o;
  reg n1132_o;
  wire n1134_o;
  wire [1:0] n1135_o;
  wire n1136_o;
  wire n1137_o;
  wire n1140_o;
  wire n1142_o;
  reg [3:0] n1145_o;
  reg n1147_o;
  wire n1149_o;
  wire [1:0] n1150_o;
  wire n1151_o;
  wire n1152_o;
  wire n1155_o;
  wire n1157_o;
  reg [3:0] n1160_o;
  reg n1162_o;
  wire n1164_o;
  wire [3:0] n1165_o;
  reg [3:0] n1167_o;
  reg [3:0] n1170_o;
  reg n1173_o;
  reg n1176_o;
  reg n1179_o;
  reg n1182_o;
  wire [1:0] n1184_o;
  wire n1186_o;
  wire n1187_o;
  wire n1188_o;
  wire n1190_o;
  wire n1191_o;
  wire n1192_o;
  wire n1193_o;
  wire n1194_o;
  wire n1196_o;
  wire [3:0] n1199_o;
  wire [3:0] n1200_o;
  wire [4:0] n1201_o;
  wire n1203_o;
  wire n1205_o;
  wire n1207_o;
  wire n1209_o;
  wire n1211_o;
  wire n1213_o;
  wire [5:0] n1214_o;
  reg [1:0] n1221_o;
  reg [3:0] n1225_o;
  reg [1:0] n1228_o;
  reg n1233_o;
  reg n1236_o;
  reg n1241_o;
  wire n1243_o;
  wire n1245_o;
  wire n1247_o;
  wire n1249_o;
  wire n1251_o;
  wire n1253_o;
  wire [4:0] n1254_o;
  reg [1:0] n1259_o;
  reg [3:0] n1262_o;
  reg [1:0] n1266_o;
  reg n1270_o;
  reg n1273_o;
  reg n1277_o;
  wire n1279_o;
  wire n1281_o;
  wire n1283_o;
  wire n1285_o;
  wire n1287_o;
  wire n1289_o;
  wire [4:0] n1290_o;
  reg [3:0] n1292_o;
  reg [1:0] n1298_o;
  reg [1:0] n1301_o;
  reg n1306_o;
  reg n1309_o;
  reg n1312_o;
  wire n1314_o;
  wire n1316_o;
  wire n1318_o;
  wire n1320_o;
  wire n1322_o;
  wire n1324_o;
  wire [4:0] n1325_o;
  reg [1:0] n1330_o;
  reg [1:0] n1334_o;
  reg n1338_o;
  reg n1341_o;
  wire n1343_o;
  wire n1345_o;
  wire n1346_o;
  wire n1347_o;
  wire [2:0] n1350_o;
  wire n1353_o;
  wire n1354_o;
  wire n1355_o;
  wire n1356_o;
  wire [3:0] n1357_o;
  wire n1359_o;
  wire n1361_o;
  wire n1363_o;
  wire [3:0] n1365_o;
  wire n1368_o;
  wire n1370_o;
  wire n1372_o;
  wire [3:0] n1374_o;
  wire n1377_o;
  wire n1379_o;
  wire [3:0] n1380_o;
  reg [3:0] n1383_o;
  reg n1385_o;
  wire [1:0] n1389_o;
  wire [3:0] n1390_o;
  wire n1392_o;
  wire n1394_o;
  wire n1396_o;
  wire [1:0] n1397_o;
  reg [1:0] n1399_o;
  reg [3:0] n1400_o;
  reg n1403_o;
  reg n1405_o;
  wire n1407_o;
  wire n1409_o;
  wire n1410_o;
  wire n1412_o;
  wire n1413_o;
  wire n1415_o;
  wire n1416_o;
  wire n1418_o;
  wire n1419_o;
  wire n1420_o;
  wire [2:0] n1423_o;
  wire [3:0] n1425_o;
  wire n1427_o;
  wire n1429_o;
  wire n1431_o;
  wire n1433_o;
  wire n1435_o;
  wire n1437_o;
  wire n1439_o;
  wire n1441_o;
  wire [3:0] n1442_o;
  reg n1444_o;
  reg n1447_o;
  reg n1448_o;
  reg n1449_o;
  wire n1451_o;
  wire n1452_o;
  wire n1453_o;
  wire n1454_o;
  wire n1457_o;
  wire n1459_o;
  wire n1461_o;
  wire n1462_o;
  wire n1463_o;
  wire n1464_o;
  wire [1:0] n1467_o;
  wire n1469_o;
  wire n1471_o;
  wire n1473_o;
  wire n1475_o;
  wire [3:0] n1476_o;
  reg [3:0] n1478_o;
  reg [1:0] n1481_o;
  reg n1484_o;
  reg n1486_o;
  reg n1488_o;
  wire n1490_o;
  wire n1492_o;
  wire n1493_o;
  wire n1495_o;
  wire n1496_o;
  wire n1498_o;
  wire n1499_o;
  wire n1501_o;
  wire n1503_o;
  wire [1:0] n1504_o;
  reg [1:0] n1507_o;
  wire n1509_o;
  wire n1511_o;
  wire n1512_o;
  wire n1514_o;
  wire n1515_o;
  wire n1517_o;
  wire n1519_o;
  wire [1:0] n1520_o;
  reg [3:0] n1522_o;
  wire n1524_o;
  wire n1526_o;
  wire n1528_o;
  wire [1:0] n1529_o;
  reg [3:0] n1531_o;
  wire n1533_o;
  wire n1535_o;
  wire n1537_o;
  wire n1539_o;
  wire n1541_o;
  wire [1:0] n1542_o;
  reg [3:0] n1544_o;
  wire n1546_o;
  wire n1548_o;
  wire n1549_o;
  wire n1556_o;
  wire n1558_o;
  wire n1559_o;
  wire n1561_o;
  wire n1562_o;
  wire n1564_o;
  wire n1565_o;
  wire n1572_o;
  wire n1574_o;
  wire n1575_o;
  wire n1577_o;
  wire n1579_o;
  wire [1:0] n1580_o;
  reg [3:0] n1582_o;
  wire n1584_o;
  wire n1586_o;
  wire n1587_o;
  wire n1589_o;
  wire n1591_o;
  wire n1593_o;
  wire [1:0] n1594_o;
  reg [3:0] n1596_o;
  wire n1598_o;
  wire n1600_o;
  wire n1602_o;
  wire [1:0] n1603_o;
  reg [1:0] n1606_o;
  wire n1608_o;
  wire [15:0] n1611_o;
  reg [2:0] n1617_o;
  reg [3:0] n1619_o;
  reg [1:0] n1621_o;
  reg [3:0] n1622_o;
  reg [1:0] n1624_o;
  reg n1626_o;
  reg n1628_o;
  reg n1631_o;
  reg n1633_o;
  reg n1636_o;
  reg n1638_o;
  reg n1640_o;
  reg n1642_o;
  reg n1644_o;
  reg n1646_o;
  wire n1648_o;
  wire n1650_o;
  wire n1651_o;
  wire n1653_o;
  wire n1654_o;
  wire n1656_o;
  wire n1657_o;
  wire n1659_o;
  wire n1660_o;
  wire [1:0] n1661_o;
  wire n1663_o;
  wire n1665_o;
  wire n1666_o;
  wire n1667_o;
  wire [2:0] n1670_o;
  wire [2:0] n1672_o;
  wire n1675_o;
  wire n1677_o;
  wire n1679_o;
  wire n1681_o;
  wire [2:0] n1682_o;
  wire n1684_o;
  wire n1687_o;
  wire n1689_o;
  wire n1691_o;
  wire n1692_o;
  wire n1693_o;
  wire [1:0] n1694_o;
  wire n1696_o;
  wire n1697_o;
  wire [1:0] n1700_o;
  wire n1703_o;
  wire n1706_o;
  wire n1708_o;
  wire n1710_o;
  wire n1712_o;
  wire [6:0] n1713_o;
  reg [3:0] n1715_o;
  reg [1:0] n1722_o;
  reg [1:0] n1725_o;
  reg [1:0] n1728_o;
  reg n1731_o;
  reg n1733_o;
  reg n1736_o;
  reg n1739_o;
  reg n1742_o;
  reg n1745_o;
  reg n1748_o;
  reg n1751_o;
  reg n1754_o;
  wire n1756_o;
  wire n1758_o;
  wire n1759_o;
  wire [2:0] n1760_o;
  wire n1762_o;
  wire n1764_o;
  wire n1766_o;
  reg [1:0] n1769_o;
  wire n1771_o;
  wire n1773_o;
  wire [2:0] n1774_o;
  wire n1776_o;
  wire n1778_o;
  wire n1779_o;
  wire n1781_o;
  wire n1782_o;
  wire n1784_o;
  wire n1785_o;
  wire n1787_o;
  wire n1789_o;
  wire [3:0] n1792_o;
  wire n1794_o;
  wire [3:0] n1795_o;
  reg [3:0] n1799_o;
  reg n1804_o;
  reg n1806_o;
  wire n1808_o;
  reg [1:0] n1811_o;
  wire [3:0] n1812_o;
  wire [1:0] n1814_o;
  wire n1815_o;
  wire n1816_o;
  wire n1818_o;
  wire n1820_o;
  wire n1822_o;
  wire [3:0] n1823_o;
  wire n1825_o;
  wire [3:0] n1826_o;
  wire n1828_o;
  wire n1829_o;
  wire [3:0] n1830_o;
  wire n1832_o;
  wire n1833_o;
  wire [2:0] n1836_o;
  wire [1:0] n1839_o;
  wire [2:0] n1841_o;
  wire [1:0] n1843_o;
  wire n1845_o;
  wire n1847_o;
  wire [1:0] n1848_o;
  reg [2:0] n1850_o;
  reg [1:0] n1852_o;
  reg n1853_o;
  wire n1855_o;
  wire n1857_o;
  wire n1858_o;
  wire [2:0] n1859_o;
  wire n1861_o;
  wire n1864_o;
  wire n1866_o;
  wire [2:0] n1867_o;
  wire n1869_o;
  wire n1872_o;
  wire n1874_o;
  wire n1876_o;
  wire [2:0] n1877_o;
  reg [1:0] n1880_o;
  reg [1:0] n1883_o;
  reg n1886_o;
  reg n1888_o;
  reg n1890_o;
  wire n1892_o;
  wire [1:0] n1893_o;
  wire n1895_o;
  wire n1896_o;
  wire n1897_o;
  wire n1899_o;
  wire n1900_o;
  wire n1901_o;
  wire n1902_o;
  wire n1903_o;
  wire n1905_o;
  wire n1906_o;
  wire n1907_o;
  wire n1909_o;
  wire n1911_o;
  wire n1913_o;
  wire n1916_o;
  wire n1918_o;
  wire n1920_o;
  wire n1922_o;
  wire n1923_o;
  wire n1924_o;
  wire [3:0] n1926_o;
  wire n1929_o;
  wire n1932_o;
  wire n1934_o;
  wire [3:0] n1935_o;
  reg [3:0] n1936_o;
  reg [1:0] n1941_o;
  reg [1:0] n1944_o;
  reg n1947_o;
  reg n1950_o;
  reg n1953_o;
  reg n1956_o;
  reg n1959_o;
  reg n1961_o;
  wire [1:0] n1962_o;
  wire n1964_o;
  wire n1966_o;
  wire n1968_o;
  wire [2:0] n1969_o;
  wire n1971_o;
  wire n1974_o;
  wire n1976_o;
  wire n1978_o;
  wire [2:0] n1979_o;
  reg [1:0] n1982_o;
  reg [1:0] n1985_o;
  reg n1988_o;
  reg n1990_o;
  wire [2:0] n1993_o;
  wire [3:0] n1994_o;
  wire [1:0] n1995_o;
  wire [1:0] n1996_o;
  wire n1997_o;
  wire n1999_o;
  wire n2001_o;
  wire n2002_o;
  wire n2004_o;
  wire n2005_o;
  wire n2007_o;
  wire n2009_o;
  wire n2011_o;
  wire n2012_o;
  wire n2014_o;
  wire n2015_o;
  wire [1:0] n2016_o;
  wire n2018_o;
  wire [4:0] n2019_o;
  wire n2021_o;
  wire n2022_o;
  wire n2023_o;
  wire n2024_o;
  wire n2026_o;
  wire n2028_o;
  wire [1:0] n2029_o;
  reg [1:0] n2033_o;
  reg n2036_o;
  wire n2038_o;
  wire n2040_o;
  wire [1:0] n2043_o;
  wire n2045_o;
  wire [1:0] n2048_o;
  wire n2050_o;
  wire n2052_o;
  wire [1:0] n2055_o;
  wire [1:0] n2058_o;
  wire [1:0] n2061_o;
  wire n2063_o;
  wire n2065_o;
  wire [3:0] n2066_o;
  reg [1:0] n2068_o;
  reg [1:0] n2072_o;
  reg [1:0] n2074_o;
  reg n2078_o;
  reg n2081_o;
  reg n2084_o;
  wire [2:0] n2087_o;
  wire [1:0] n2089_o;
  wire [1:0] n2090_o;
  wire [1:0] n2092_o;
  wire n2093_o;
  wire n2095_o;
  wire n2097_o;
  wire [2:0] n2098_o;
  wire n2100_o;
  wire n2103_o;
  wire n2105_o;
  wire n2107_o;
  wire [2:0] n2108_o;
  wire n2110_o;
  wire n2113_o;
  wire n2115_o;
  wire n2117_o;
  wire [3:0] n2118_o;
  reg [1:0] n2121_o;
  reg [1:0] n2125_o;
  reg n2128_o;
  reg n2131_o;
  reg n2133_o;
  reg n2135_o;
  wire [2:0] n2137_o;
  wire [1:0] n2138_o;
  wire [1:0] n2139_o;
  wire [1:0] n2141_o;
  wire n2143_o;
  wire n2144_o;
  wire n2145_o;
  wire n2147_o;
  wire n2149_o;
  wire n2151_o;
  wire [1:0] n2152_o;
  wire n2154_o;
  wire n2155_o;
  wire n2156_o;
  wire n2158_o;
  wire n2159_o;
  wire n2160_o;
  wire n2161_o;
  wire n2162_o;
  wire n2164_o;
  wire n2165_o;
  wire n2166_o;
  wire n2168_o;
  wire n2170_o;
  wire n2172_o;
  wire n2174_o;
  wire n2177_o;
  wire n2179_o;
  wire n2181_o;
  wire n2183_o;
  wire n2184_o;
  wire n2185_o;
  wire [3:0] n2187_o;
  wire n2190_o;
  wire n2192_o;
  wire [4:0] n2193_o;
  reg [3:0] n2194_o;
  reg [1:0] n2199_o;
  reg [1:0] n2203_o;
  reg n2206_o;
  reg n2209_o;
  reg n2212_o;
  reg n2215_o;
  reg n2218_o;
  reg n2221_o;
  reg n2223_o;
  wire [1:0] n2224_o;
  wire n2226_o;
  wire n2228_o;
  wire n2230_o;
  wire n2232_o;
  wire [2:0] n2233_o;
  wire n2235_o;
  wire n2238_o;
  wire n2240_o;
  wire n2242_o;
  wire [3:0] n2243_o;
  reg [1:0] n2246_o;
  reg [1:0] n2250_o;
  reg n2253_o;
  reg n2256_o;
  reg n2258_o;
  wire [2:0] n2261_o;
  wire [3:0] n2262_o;
  wire [1:0] n2263_o;
  wire [1:0] n2264_o;
  wire n2265_o;
  wire n2267_o;
  wire n2269_o;
  wire n2270_o;
  wire n2271_o;
  wire n2273_o;
  wire n2274_o;
  wire n2276_o;
  wire n2278_o;
  wire n2280_o;
  wire n2281_o;
  wire n2283_o;
  wire n2284_o;
  wire [2:0] n2287_o;
  wire n2289_o;
  wire n2291_o;
  wire n2293_o;
  wire [2:0] n2294_o;
  reg [1:0] n2298_o;
  reg n2301_o;
  reg n2304_o;
  wire n2306_o;
  wire [1:0] n2307_o;
  wire n2309_o;
  wire n2311_o;
  wire n2312_o;
  wire n2313_o;
  wire [2:0] n2316_o;
  wire [2:0] n2318_o;
  wire n2321_o;
  wire n2323_o;
  wire n2325_o;
  wire n2327_o;
  wire [2:0] n2328_o;
  wire n2330_o;
  wire [3:0] n2331_o;
  wire n2333_o;
  wire [1:0] n2336_o;
  wire n2337_o;
  wire n2338_o;
  wire n2340_o;
  wire n2341_o;
  wire n2344_o;
  wire [1:0] n2346_o;
  wire n2348_o;
  wire n2351_o;
  wire n2353_o;
  wire n2355_o;
  wire n2356_o;
  wire n2357_o;
  wire [1:0] n2358_o;
  wire n2360_o;
  wire n2361_o;
  wire [1:0] n2364_o;
  wire n2367_o;
  wire n2370_o;
  wire n2372_o;
  wire n2374_o;
  wire n2376_o;
  wire [6:0] n2377_o;
  reg [3:0] n2380_o;
  reg [1:0] n2387_o;
  reg [1:0] n2390_o;
  reg [1:0] n2395_o;
  reg [1:0] n2397_o;
  reg n2399_o;
  reg n2401_o;
  reg n2404_o;
  reg n2407_o;
  reg n2410_o;
  reg n2413_o;
  reg n2416_o;
  reg n2419_o;
  reg n2422_o;
  wire n2424_o;
  wire n2426_o;
  wire n2427_o;
  wire [1:0] n2428_o;
  wire n2430_o;
  wire n2431_o;
  wire n2432_o;
  wire n2434_o;
  wire n2435_o;
  wire n2436_o;
  wire n2437_o;
  wire n2438_o;
  wire n2440_o;
  wire n2441_o;
  wire n2442_o;
  wire n2444_o;
  wire n2446_o;
  wire n2448_o;
  wire n2450_o;
  wire n2453_o;
  wire n2455_o;
  wire n2457_o;
  wire n2458_o;
  wire n2459_o;
  wire n2462_o;
  wire n2464_o;
  wire n2466_o;
  wire n2467_o;
  wire n2468_o;
  wire [3:0] n2470_o;
  wire n2473_o;
  wire n2475_o;
  wire [4:0] n2476_o;
  reg [3:0] n2477_o;
  reg [1:0] n2483_o;
  reg [1:0] n2486_o;
  reg n2489_o;
  reg n2492_o;
  reg n2495_o;
  reg n2498_o;
  reg n2501_o;
  reg n2504_o;
  reg n2506_o;
  wire [1:0] n2507_o;
  wire n2509_o;
  wire n2510_o;
  wire n2511_o;
  wire n2513_o;
  wire n2515_o;
  wire n2517_o;
  wire [2:0] n2518_o;
  wire n2520_o;
  wire n2523_o;
  wire [2:0] n2524_o;
  wire n2526_o;
  wire n2529_o;
  wire n2531_o;
  wire n2533_o;
  wire [3:0] n2534_o;
  reg [1:0] n2538_o;
  reg [1:0] n2541_o;
  reg n2544_o;
  reg n2546_o;
  reg n2549_o;
  reg n2551_o;
  wire [2:0] n2554_o;
  wire [3:0] n2555_o;
  wire [1:0] n2556_o;
  wire [1:0] n2557_o;
  wire n2558_o;
  wire n2560_o;
  wire n2561_o;
  wire n2563_o;
  wire n2565_o;
  wire n2566_o;
  wire n2568_o;
  wire n2569_o;
  wire n2571_o;
  wire n2573_o;
  wire n2575_o;
  wire n2576_o;
  wire n2578_o;
  wire n2579_o;
  wire n2581_o;
  wire n2582_o;
  wire [1:0] n2583_o;
  wire n2585_o;
  wire n2587_o;
  wire n2588_o;
  wire n2589_o;
  wire [2:0] n2592_o;
  wire [2:0] n2594_o;
  wire n2597_o;
  wire n2599_o;
  wire n2601_o;
  wire [2:0] n2602_o;
  wire n2604_o;
  wire [3:0] n2605_o;
  wire n2607_o;
  wire [1:0] n2610_o;
  wire n2611_o;
  wire n2612_o;
  wire n2614_o;
  wire n2615_o;
  wire n2618_o;
  wire [1:0] n2620_o;
  wire n2622_o;
  wire n2625_o;
  wire n2627_o;
  wire n2629_o;
  wire n2630_o;
  wire n2631_o;
  wire [1:0] n2632_o;
  wire n2634_o;
  wire n2635_o;
  wire [1:0] n2638_o;
  wire n2641_o;
  wire n2644_o;
  wire n2646_o;
  wire n2648_o;
  wire n2650_o;
  wire [5:0] n2651_o;
  reg [3:0] n2654_o;
  reg [1:0] n2659_o;
  reg [1:0] n2663_o;
  reg [1:0] n2667_o;
  reg [1:0] n2669_o;
  reg n2671_o;
  reg n2673_o;
  reg n2676_o;
  reg n2679_o;
  reg n2682_o;
  reg n2685_o;
  reg n2688_o;
  reg n2691_o;
  wire n2693_o;
  wire n2695_o;
  wire n2696_o;
  wire [1:0] n2697_o;
  wire n2699_o;
  wire n2700_o;
  wire n2701_o;
  wire n2703_o;
  wire n2704_o;
  wire n2705_o;
  wire n2706_o;
  wire n2707_o;
  wire n2709_o;
  wire n2710_o;
  wire n2711_o;
  wire n2713_o;
  wire n2715_o;
  wire n2717_o;
  wire n2719_o;
  wire n2721_o;
  wire n2724_o;
  wire n2726_o;
  wire n2728_o;
  wire n2730_o;
  wire n2731_o;
  wire n2732_o;
  wire [3:0] n2734_o;
  wire n2737_o;
  wire n2739_o;
  wire [5:0] n2740_o;
  reg [3:0] n2742_o;
  reg [1:0] n2748_o;
  reg [1:0] n2752_o;
  reg [1:0] n2756_o;
  reg n2759_o;
  reg n2762_o;
  reg n2765_o;
  reg n2768_o;
  reg n2771_o;
  reg n2774_o;
  reg n2776_o;
  wire [1:0] n2777_o;
  wire n2779_o;
  wire n2781_o;
  wire n2782_o;
  wire n2783_o;
  wire n2784_o;
  wire [1:0] n2785_o;
  wire n2787_o;
  wire n2788_o;
  wire n2790_o;
  wire n2791_o;
  wire n2793_o;
  wire n2795_o;
  wire [1:0] n2796_o;
  wire n2798_o;
  wire [3:0] n2799_o;
  wire n2801_o;
  wire n2802_o;
  wire [3:0] n2805_o;
  wire n2807_o;
  wire [2:0] n2808_o;
  wire n2810_o;
  wire [1:0] n2811_o;
  wire n2813_o;
  wire n2815_o;
  wire n2816_o;
  wire n2818_o;
  wire [1:0] n2819_o;
  reg [1:0] n2823_o;
  wire [1:0] n2825_o;
  wire n2828_o;
  wire n2831_o;
  wire n2833_o;
  wire n2835_o;
  wire [4:0] n2836_o;
  reg [3:0] n2837_o;
  reg [1:0] n2841_o;
  reg [1:0] n2845_o;
  reg [1:0] n2849_o;
  reg [1:0] n2851_o;
  reg n2853_o;
  reg n2856_o;
  reg n2859_o;
  reg n2861_o;
  wire [2:0] n2864_o;
  wire [3:0] n2865_o;
  wire [1:0] n2866_o;
  wire [1:0] n2867_o;
  wire [1:0] n2868_o;
  wire [1:0] n2870_o;
  wire n2872_o;
  wire n2873_o;
  wire n2875_o;
  wire n2877_o;
  wire n2878_o;
  wire n2879_o;
  wire n2881_o;
  wire n2882_o;
  wire n2884_o;
  wire n2886_o;
  wire n2888_o;
  wire n2889_o;
  wire n2891_o;
  wire n2892_o;
  wire n2894_o;
  wire n2895_o;
  wire [13:0] n2896_o;
  reg [2:0] n2899_o;
  reg [3:0] n2901_o;
  reg [1:0] n2903_o;
  reg [3:0] n2905_o;
  reg [1:0] n2907_o;
  reg [1:0] n2910_o;
  reg [1:0] n2913_o;
  reg n2916_o;
  reg n2919_o;
  reg n2922_o;
  reg n2925_o;
  reg n2928_o;
  reg n2931_o;
  reg n2933_o;
  reg n2935_o;
  reg n2937_o;
  reg n2938_o;
  reg n2939_o;
  reg n2941_o;
  reg n2944_o;
  reg n2947_o;
  reg n2950_o;
  reg n2953_o;
  reg n2956_o;
  reg n2959_o;
  reg n2962_o;
  wire [1:0] n2967_o;
  wire [2:0] n2968_o;
  wire [2:0] n2969_o;
  wire n2971_o;
  wire n2973_o;
  wire n2974_o;
  wire n2976_o;
  wire n2978_o;
  wire [2:0] n2979_o;
  reg [4:0] n2984_o;
  wire n2986_o;
  wire n2988_o;
  wire n2989_o;
  wire n2991_o;
  wire n2992_o;
  wire [2:0] n2993_o;
  wire n2995_o;
  wire n2997_o;
  wire n2998_o;
  wire n3000_o;
  wire [1:0] n3001_o;
  reg [4:0] n3005_o;
  wire n3007_o;
  wire [2:0] n3008_o;
  wire n3010_o;
  reg [4:0] n3013_o;
  wire n3015_o;
  wire [2:0] n3016_o;
  wire n3018_o;
  reg [4:0] n3021_o;
  wire [2:0] n3022_o;
  reg [4:0] n3023_o;
  wire n3025_o;
  wire [2:0] n3026_o;
  wire [30:0] n3027_o;
  wire n3029_o;
  wire n3031_o;
  wire n3033_o;
  wire n3035_o;
  wire n3037_o;
  wire n3039_o;
  wire n3041_o;
  wire [6:0] n3042_o;
  reg [4:0] n3051_o;
  wire n3053_o;
  wire [2:0] n3054_o;
  wire [30:0] n3055_o;
  wire [2:0] n3056_o;
  wire n3058_o;
  wire n3060_o;
  wire n3061_o;
  wire [4:0] n3064_o;
  wire n3067_o;
  wire [2:0] n3068_o;
  wire n3070_o;
  wire n3072_o;
  wire n3073_o;
  wire [4:0] n3076_o;
  wire n3079_o;
  wire n3081_o;
  wire n3083_o;
  wire [2:0] n3084_o;
  wire n3086_o;
  wire [4:0] n3089_o;
  wire n3092_o;
  wire n3094_o;
  wire n3096_o;
  wire [6:0] n3097_o;
  reg [4:0] n3103_o;
  wire n3105_o;
  wire [2:0] n3106_o;
  wire [30:0] n3107_o;
  wire n3109_o;
  wire [4:0] n3112_o;
  wire n3114_o;
  wire n3116_o;
  wire n3118_o;
  wire n3120_o;
  wire n3122_o;
  wire n3123_o;
  wire n3125_o;
  wire [2:0] n3126_o;
  wire [30:0] n3127_o;
  wire n3129_o;
  wire n3131_o;
  wire n3133_o;
  wire n3135_o;
  wire n3137_o;
  wire n3139_o;
  wire n3141_o;
  wire [6:0] n3142_o;
  reg [4:0] n3151_o;
  wire [2:0] n3152_o;
  wire [30:0] n3153_o;
  wire n3155_o;
  wire n3157_o;
  wire n3159_o;
  wire n3161_o;
  wire n3163_o;
  wire n3165_o;
  wire [2:0] n3166_o;
  wire n3168_o;
  wire [4:0] n3171_o;
  wire n3174_o;
  wire [6:0] n3175_o;
  reg [4:0] n3183_o;
  wire [4:0] n3184_o;
  wire [4:0] n3186_o;
  wire [4:0] n3188_o;
  wire [4:0] n3190_o;
  wire [4:0] n3192_o;
  reg [4:0] n3193_o;
  wire [2:0] n3194_o;
  reg [4:0] n3195_o;
  assign lcycle = n2899_o; //(module output)
  assign alu_op = n3195_o; //(module output)
  assign set_busa_to = n2901_o; //(module output)
  assign set_addr_to = n2903_o; //(module output)
  assign write_data = n2905_o; //(module output)
  assign jump = n2907_o; //(module output)
  assign baadd = n2910_o; //(module output)
  assign baquirk = n2913_o; //(module output)
  assign breakatna = n2916_o; //(module output)
  assign adadd = n2919_o; //(module output)
  assign addy = n2922_o; //(module output)
  assign pcadd = n2925_o; //(module output)
  assign inc_s = n2928_o; //(module output)
  assign dec_s = n2931_o; //(module output)
  assign lda = n2933_o; //(module output)
  assign ldp = n2935_o; //(module output)
  assign ldx = n2937_o; //(module output)
  assign ldy = n2938_o; //(module output)
  assign lds = n2939_o; //(module output)
  assign lddi = n2941_o; //(module output)
  assign ldalu = n2944_o; //(module output)
  assign ldad = n2947_o; //(module output)
  assign ldbal = n2950_o; //(module output)
  assign ldbah = n2953_o; //(module output)
  assign savep = n2956_o; //(module output)
  assign write = n2959_o; //(module output)
  /* T65_MCode.vhd:97:10  */
  assign branch = n1025_o; // (signal)
  /* T65_MCode.vhd:98:10  */
  assign alumore = n2962_o; // (signal)
  /* T65_MCode.vhd:102:10  */
  assign n997_o = ir[7:5]; // extract
  /* T65_MCode.vhd:103:20  */
  assign n998_o = p[7]; // extract
  /* T65_MCode.vhd:103:15  */
  assign n999_o = ~n998_o;
  /* T65_MCode.vhd:103:29  */
  assign n1001_o = n997_o == 3'b000;
  /* T65_MCode.vhd:104:20  */
  assign n1002_o = p[7]; // extract
  /* T65_MCode.vhd:104:29  */
  assign n1004_o = n997_o == 3'b001;
  /* T65_MCode.vhd:105:20  */
  assign n1005_o = p[6]; // extract
  /* T65_MCode.vhd:105:15  */
  assign n1006_o = ~n1005_o;
  /* T65_MCode.vhd:105:29  */
  assign n1008_o = n997_o == 3'b010;
  /* T65_MCode.vhd:106:20  */
  assign n1009_o = p[6]; // extract
  /* T65_MCode.vhd:106:29  */
  assign n1011_o = n997_o == 3'b011;
  /* T65_MCode.vhd:107:20  */
  assign n1012_o = p[0]; // extract
  /* T65_MCode.vhd:107:15  */
  assign n1013_o = ~n1012_o;
  /* T65_MCode.vhd:107:29  */
  assign n1015_o = n997_o == 3'b100;
  /* T65_MCode.vhd:108:20  */
  assign n1016_o = p[0]; // extract
  /* T65_MCode.vhd:108:29  */
  assign n1018_o = n997_o == 3'b101;
  /* T65_MCode.vhd:109:20  */
  assign n1019_o = p[1]; // extract
  /* T65_MCode.vhd:109:15  */
  assign n1020_o = ~n1019_o;
  /* T65_MCode.vhd:109:29  */
  assign n1022_o = n997_o == 3'b110;
  /* T65_MCode.vhd:110:20  */
  assign n1023_o = p[1]; // extract
  assign n1024_o = {n1022_o, n1018_o, n1015_o, n1011_o, n1008_o, n1004_o, n1001_o};
  /* T65_MCode.vhd:102:3  */
  always @*
    case (n1024_o)
      7'b1000000: n1025_o = n1020_o;
      7'b0100000: n1025_o = n1016_o;
      7'b0010000: n1025_o = n1013_o;
      7'b0001000: n1025_o = n1009_o;
      7'b0000100: n1025_o = n1006_o;
      7'b0000010: n1025_o = n1002_o;
      7'b0000001: n1025_o = n999_o;
      default: n1025_o = n1023_o;
    endcase
  /* T65_MCode.vhd:141:12  */
  assign n1028_o = ir[7:5]; // extract
  /* T65_MCode.vhd:143:16  */
  assign n1029_o = ir[1:0]; // extract
  /* T65_MCode.vhd:146:18  */
  assign n1030_o = ir[4:2]; // extract
  /* T65_MCode.vhd:146:30  */
  assign n1032_o = n1030_o == 3'b111;
  /* T65_MCode.vhd:147:26  */
  assign n1033_o = ~rdy_mod;
  /* T65_MCode.vhd:147:15  */
  assign n1036_o = n1033_o ? 4'b1011 : 4'b0011;
  /* T65_MCode.vhd:146:13  */
  assign n1038_o = n1032_o ? n1036_o : 4'b0011;
  /* T65_MCode.vhd:144:11  */
  assign n1040_o = n1029_o == 2'b00;
  /* T65_MCode.vhd:157:18  */
  assign n1041_o = ir[4:2]; // extract
  /* T65_MCode.vhd:157:30  */
  assign n1043_o = n1041_o == 3'b111;
  /* T65_MCode.vhd:158:26  */
  assign n1044_o = ~rdy_mod;
  /* T65_MCode.vhd:158:15  */
  assign n1047_o = n1044_o ? 4'b1010 : 4'b0010;
  /* T65_MCode.vhd:157:13  */
  assign n1049_o = n1043_o ? n1047_o : 4'b0010;
  /* T65_MCode.vhd:155:11  */
  assign n1051_o = n1029_o == 2'b10;
  /* T65_MCode.vhd:167:18  */
  assign n1052_o = ir[4:2]; // extract
  /* T65_MCode.vhd:167:30  */
  assign n1054_o = n1052_o == 3'b110;
  /* T65_MCode.vhd:167:13  */
  assign n1057_o = n1054_o ? 4'b1001 : 4'b0001;
  /* T65_MCode.vhd:167:13  */
  assign n1060_o = n1054_o ? 1'b1 : 1'b0;
  /* T65_MCode.vhd:173:18  */
  assign n1061_o = ir[4:2]; // extract
  /* T65_MCode.vhd:173:30  */
  assign n1063_o = n1061_o == 3'b111;
  /* T65_MCode.vhd:173:42  */
  assign n1064_o = ir[4:2]; // extract
  /* T65_MCode.vhd:173:54  */
  assign n1066_o = n1064_o == 3'b110;
  /* T65_MCode.vhd:173:37  */
  assign n1067_o = n1063_o | n1066_o;
  /* T65_MCode.vhd:173:66  */
  assign n1068_o = ir[4:2]; // extract
  /* T65_MCode.vhd:173:78  */
  assign n1070_o = n1068_o == 3'b100;
  /* T65_MCode.vhd:173:61  */
  assign n1071_o = n1067_o | n1070_o;
  /* T65_MCode.vhd:174:26  */
  assign n1072_o = ~rdy_mod;
  /* T65_MCode.vhd:174:15  */
  assign n1075_o = n1072_o ? 4'b1001 : 4'b1000;
  /* T65_MCode.vhd:173:13  */
  assign n1077_o = n1071_o ? n1075_o : 4'b1000;
  /* T65_MCode.vhd:166:11  */
  assign n1079_o = n1029_o == 2'b11;
  /* T65.vhd:261:16  */
  assign n1080_o = {n1079_o, n1051_o, n1040_o};
  /* T65_MCode.vhd:143:9  */
  always @*
    case (n1080_o)
      3'b100: n1084_o = n1057_o;
      3'b010: n1084_o = 4'b0010;
      3'b001: n1084_o = 4'b0011;
      default: n1084_o = 4'b0001;
    endcase
  /* T65_MCode.vhd:143:9  */
  always @*
    case (n1080_o)
      3'b100: n1086_o = n1077_o;
      3'b010: n1086_o = n1049_o;
      3'b001: n1086_o = n1038_o;
      default: n1086_o = 4'b0001;
    endcase
  /* T65_MCode.vhd:143:9  */
  always @*
    case (n1080_o)
      3'b100: n1088_o = n1060_o;
      3'b010: n1088_o = 1'b0;
      3'b001: n1088_o = 1'b0;
      default: n1088_o = 1'b0;
    endcase
  /* T65_MCode.vhd:142:7  */
  assign n1090_o = n1028_o == 3'b100;
  /* T65_MCode.vhd:187:16  */
  assign n1091_o = ir[1:0]; // extract
  /* T65_MCode.vhd:189:18  */
  assign n1092_o = ir[4]; // extract
  /* T65_MCode.vhd:189:22  */
  assign n1094_o = n1092_o != 1'b1;
  /* T65_MCode.vhd:189:34  */
  assign n1095_o = ir[2]; // extract
  /* T65_MCode.vhd:189:38  */
  assign n1097_o = n1095_o != 1'b0;
  /* T65_MCode.vhd:189:29  */
  assign n1098_o = n1094_o | n1097_o;
  /* T65_MCode.vhd:189:13  */
  assign n1101_o = n1098_o ? 1'b1 : 1'b0;
  /* T65_MCode.vhd:188:11  */
  assign n1103_o = n1091_o == 2'b00;
  /* T65_MCode.vhd:192:11  */
  assign n1105_o = n1091_o == 2'b01;
  /* T65_MCode.vhd:194:11  */
  assign n1107_o = n1091_o == 2'b10;
  /* T65_MCode.vhd:199:18  */
  assign n1108_o = ir[4:2]; // extract
  /* T65_MCode.vhd:199:30  */
  assign n1110_o = n1108_o == 3'b110;
  /* T65_MCode.vhd:199:13  */
  assign n1113_o = n1110_o ? 4'b0100 : 4'b0000;
  /* T65_MCode.vhd:199:13  */
  assign n1116_o = n1110_o ? 1'b1 : 1'b0;
  assign n1117_o = {n1107_o, n1105_o, n1103_o};
  /* T65_MCode.vhd:187:9  */
  always @*
    case (n1117_o)
      3'b100: n1119_o = 4'b0000;
      3'b010: n1119_o = 4'b0000;
      3'b001: n1119_o = 4'b0000;
      default: n1119_o = n1113_o;
    endcase
  /* T65_MCode.vhd:187:9  */
  always @*
    case (n1117_o)
      3'b100: n1124_o = 1'b0;
      3'b010: n1124_o = 1'b1;
      3'b001: n1124_o = 1'b0;
      default: n1124_o = 1'b1;
    endcase
  /* T65_MCode.vhd:187:9  */
  always @*
    case (n1117_o)
      3'b100: n1128_o = 1'b1;
      3'b010: n1128_o = 1'b0;
      3'b001: n1128_o = 1'b0;
      default: n1128_o = 1'b1;
    endcase
  /* T65_MCode.vhd:187:9  */
  always @*
    case (n1117_o)
      3'b100: n1130_o = 1'b0;
      3'b010: n1130_o = 1'b0;
      3'b001: n1130_o = n1101_o;
      default: n1130_o = 1'b0;
    endcase
  /* T65_MCode.vhd:187:9  */
  always @*
    case (n1117_o)
      3'b100: n1132_o = 1'b0;
      3'b010: n1132_o = 1'b0;
      3'b001: n1132_o = 1'b0;
      default: n1132_o = n1116_o;
    endcase
  /* T65_MCode.vhd:185:7  */
  assign n1134_o = n1028_o == 3'b101;
  /* T65_MCode.vhd:205:16  */
  assign n1135_o = ir[1:0]; // extract
  /* T65_MCode.vhd:207:18  */
  assign n1136_o = ir[4]; // extract
  /* T65_MCode.vhd:207:22  */
  assign n1137_o = ~n1136_o;
  /* T65_MCode.vhd:207:13  */
  assign n1140_o = n1137_o ? 1'b1 : 1'b0;
  /* T65_MCode.vhd:206:11  */
  assign n1142_o = n1135_o == 2'b00;
  /* T65_MCode.vhd:205:9  */
  always @*
    case (n1142_o)
      1'b1: n1145_o = 4'b0011;
      default: n1145_o = 4'b0001;
    endcase
  /* T65_MCode.vhd:205:9  */
  always @*
    case (n1142_o)
      1'b1: n1147_o = n1140_o;
      default: n1147_o = 1'b0;
    endcase
  /* T65_MCode.vhd:204:7  */
  assign n1149_o = n1028_o == 3'b110;
  /* T65_MCode.vhd:215:16  */
  assign n1150_o = ir[1:0]; // extract
  /* T65_MCode.vhd:217:16  */
  assign n1151_o = ir[4]; // extract
  /* T65_MCode.vhd:217:20  */
  assign n1152_o = ~n1151_o;
  /* T65_MCode.vhd:217:11  */
  assign n1155_o = n1152_o ? 1'b1 : 1'b0;
  /* T65_MCode.vhd:216:9  */
  assign n1157_o = n1150_o == 2'b00;
  /* T65_MCode.vhd:215:9  */
  always @*
    case (n1157_o)
      1'b1: n1160_o = 4'b0010;
      default: n1160_o = 4'b0001;
    endcase
  /* T65_MCode.vhd:215:9  */
  always @*
    case (n1157_o)
      1'b1: n1162_o = n1155_o;
      default: n1162_o = 1'b0;
    endcase
  /* T65_MCode.vhd:214:7  */
  assign n1164_o = n1028_o == 3'b111;
  assign n1165_o = {n1164_o, n1149_o, n1134_o, n1090_o};
  /* T65_MCode.vhd:141:5  */
  always @*
    case (n1165_o)
      4'b1000: n1167_o = n1160_o;
      4'b0100: n1167_o = n1145_o;
      4'b0010: n1167_o = n1119_o;
      4'b0001: n1167_o = n1084_o;
      default: n1167_o = 4'b0001;
    endcase
  /* T65_MCode.vhd:141:5  */
  always @*
    case (n1165_o)
      4'b1000: n1170_o = 4'b0000;
      4'b0100: n1170_o = 4'b0000;
      4'b0010: n1170_o = 4'b0000;
      4'b0001: n1170_o = n1086_o;
      default: n1170_o = 4'b0000;
    endcase
  /* T65_MCode.vhd:141:5  */
  always @*
    case (n1165_o)
      4'b1000: n1173_o = 1'b0;
      4'b0100: n1173_o = 1'b0;
      4'b0010: n1173_o = n1124_o;
      4'b0001: n1173_o = 1'b0;
      default: n1173_o = 1'b0;
    endcase
  /* T65_MCode.vhd:141:5  */
  always @*
    case (n1165_o)
      4'b1000: n1176_o = n1162_o;
      4'b0100: n1176_o = 1'b0;
      4'b0010: n1176_o = n1128_o;
      4'b0001: n1176_o = 1'b0;
      default: n1176_o = 1'b0;
    endcase
  /* T65_MCode.vhd:141:5  */
  always @*
    case (n1165_o)
      4'b1000: n1179_o = 1'b0;
      4'b0100: n1179_o = n1147_o;
      4'b0010: n1179_o = n1130_o;
      4'b0001: n1179_o = 1'b0;
      default: n1179_o = 1'b0;
    endcase
  /* T65_MCode.vhd:141:5  */
  always @*
    case (n1165_o)
      4'b1000: n1182_o = 1'b0;
      4'b0100: n1182_o = 1'b0;
      4'b0010: n1182_o = n1132_o;
      4'b0001: n1182_o = n1088_o;
      default: n1182_o = 1'b0;
    endcase
  /* T65_MCode.vhd:227:10  */
  assign n1184_o = ir[7:6]; // extract
  /* T65_MCode.vhd:227:23  */
  assign n1186_o = n1184_o != 2'b10;
  /* T65_MCode.vhd:227:37  */
  assign n1187_o = ir[1]; // extract
  /* T65_MCode.vhd:227:31  */
  assign n1188_o = n1187_o & n1186_o;
  /* T65_MCode.vhd:227:56  */
  assign n1190_o = mode == 2'b00;
  /* T65_MCode.vhd:227:67  */
  assign n1191_o = ir[0]; // extract
  /* T65_MCode.vhd:227:70  */
  assign n1192_o = ~n1191_o;
  /* T65_MCode.vhd:227:62  */
  assign n1193_o = n1190_o | n1192_o;
  /* T65_MCode.vhd:227:47  */
  assign n1194_o = n1193_o & n1188_o;
  /* T65_MCode.vhd:228:12  */
  assign n1196_o = ir == 8'b11101011;
  /* T65_MCode.vhd:228:7  */
  assign n1199_o = n1196_o ? 4'b0001 : 4'b0000;
  /* T65_MCode.vhd:227:5  */
  assign n1200_o = n1194_o ? n1199_o : n1167_o;
  /* T65_MCode.vhd:235:12  */
  assign n1201_o = ir[4:0]; // extract
  /* T65_MCode.vhd:248:15  */
  assign n1203_o = mcycle == 3'b001;
  /* T65_MCode.vhd:252:15  */
  assign n1205_o = mcycle == 3'b010;
  /* T65_MCode.vhd:257:15  */
  assign n1207_o = mcycle == 3'b011;
  /* T65_MCode.vhd:262:15  */
  assign n1209_o = mcycle == 3'b100;
  /* T65_MCode.vhd:265:15  */
  assign n1211_o = mcycle == 3'b101;
  /* T65_MCode.vhd:268:15  */
  assign n1213_o = mcycle == 3'b110;
  assign n1214_o = {n1213_o, n1211_o, n1209_o, n1207_o, n1205_o, n1203_o};
  /* T65_MCode.vhd:247:13  */
  always @*
    case (n1214_o)
      6'b100000: n1221_o = 2'b00;
      6'b010000: n1221_o = 2'b11;
      6'b001000: n1221_o = 2'b11;
      6'b000100: n1221_o = 2'b01;
      6'b000010: n1221_o = 2'b01;
      6'b000001: n1221_o = 2'b01;
      default: n1221_o = 2'b00;
    endcase
  /* T65_MCode.vhd:247:13  */
  always @*
    case (n1214_o)
      6'b100000: n1225_o = n1170_o;
      6'b010000: n1225_o = n1170_o;
      6'b001000: n1225_o = n1170_o;
      6'b000100: n1225_o = 4'b0101;
      6'b000010: n1225_o = 4'b0110;
      6'b000001: n1225_o = 4'b0111;
      default: n1225_o = n1170_o;
    endcase
  /* T65_MCode.vhd:247:13  */
  always @*
    case (n1214_o)
      6'b100000: n1228_o = 2'b10;
      6'b010000: n1228_o = 2'b00;
      6'b001000: n1228_o = 2'b00;
      6'b000100: n1228_o = 2'b00;
      6'b000010: n1228_o = 2'b00;
      6'b000001: n1228_o = 2'b00;
      default: n1228_o = 2'b00;
    endcase
  /* T65_MCode.vhd:247:13  */
  always @*
    case (n1214_o)
      6'b100000: n1233_o = 1'b0;
      6'b010000: n1233_o = 1'b0;
      6'b001000: n1233_o = 1'b1;
      6'b000100: n1233_o = 1'b1;
      6'b000010: n1233_o = 1'b1;
      6'b000001: n1233_o = 1'b0;
      default: n1233_o = 1'b0;
    endcase
  /* T65_MCode.vhd:247:13  */
  always @*
    case (n1214_o)
      6'b100000: n1236_o = 1'b0;
      6'b010000: n1236_o = 1'b1;
      6'b001000: n1236_o = 1'b0;
      6'b000100: n1236_o = 1'b0;
      6'b000010: n1236_o = 1'b0;
      6'b000001: n1236_o = 1'b0;
      default: n1236_o = 1'b0;
    endcase
  /* T65_MCode.vhd:247:13  */
  always @*
    case (n1214_o)
      6'b100000: n1241_o = 1'b0;
      6'b010000: n1241_o = 1'b0;
      6'b001000: n1241_o = 1'b0;
      6'b000100: n1241_o = 1'b1;
      6'b000010: n1241_o = 1'b1;
      6'b000001: n1241_o = 1'b1;
      default: n1241_o = 1'b0;
    endcase
  /* T65_MCode.vhd:244:11  */
  assign n1243_o = ir == 8'b00000000;
  /* T65_MCode.vhd:275:15  */
  assign n1245_o = mcycle == 3'b001;
  /* T65_MCode.vhd:279:15  */
  assign n1247_o = mcycle == 3'b010;
  /* T65_MCode.vhd:283:15  */
  assign n1249_o = mcycle == 3'b011;
  /* T65_MCode.vhd:288:15  */
  assign n1251_o = mcycle == 3'b100;
  /* T65_MCode.vhd:290:15  */
  assign n1253_o = mcycle == 3'b101;
  assign n1254_o = {n1253_o, n1251_o, n1249_o, n1247_o, n1245_o};
  /* T65_MCode.vhd:274:13  */
  always @*
    case (n1254_o)
      5'b10000: n1259_o = 2'b00;
      5'b01000: n1259_o = 2'b00;
      5'b00100: n1259_o = 2'b01;
      5'b00010: n1259_o = 2'b01;
      5'b00001: n1259_o = 2'b01;
      default: n1259_o = 2'b00;
    endcase
  /* T65_MCode.vhd:274:13  */
  always @*
    case (n1254_o)
      5'b10000: n1262_o = n1170_o;
      5'b01000: n1262_o = n1170_o;
      5'b00100: n1262_o = 4'b0110;
      5'b00010: n1262_o = 4'b0111;
      5'b00001: n1262_o = n1170_o;
      default: n1262_o = n1170_o;
    endcase
  /* T65_MCode.vhd:274:13  */
  always @*
    case (n1254_o)
      5'b10000: n1266_o = 2'b10;
      5'b01000: n1266_o = 2'b00;
      5'b00100: n1266_o = 2'b00;
      5'b00010: n1266_o = 2'b00;
      5'b00001: n1266_o = 2'b01;
      default: n1266_o = 2'b00;
    endcase
  /* T65_MCode.vhd:274:13  */
  always @*
    case (n1254_o)
      5'b10000: n1270_o = 1'b0;
      5'b01000: n1270_o = 1'b1;
      5'b00100: n1270_o = 1'b1;
      5'b00010: n1270_o = 1'b0;
      5'b00001: n1270_o = 1'b0;
      default: n1270_o = 1'b0;
    endcase
  /* T65_MCode.vhd:274:13  */
  always @*
    case (n1254_o)
      5'b10000: n1273_o = 1'b0;
      5'b01000: n1273_o = 1'b0;
      5'b00100: n1273_o = 1'b0;
      5'b00010: n1273_o = 1'b0;
      5'b00001: n1273_o = 1'b1;
      default: n1273_o = 1'b0;
    endcase
  /* T65_MCode.vhd:274:13  */
  always @*
    case (n1254_o)
      5'b10000: n1277_o = 1'b0;
      5'b01000: n1277_o = 1'b0;
      5'b00100: n1277_o = 1'b1;
      5'b00010: n1277_o = 1'b1;
      5'b00001: n1277_o = 1'b0;
      default: n1277_o = 1'b0;
    endcase
  /* T65_MCode.vhd:272:11  */
  assign n1279_o = ir == 8'b00100000;
  /* T65_MCode.vhd:297:15  */
  assign n1281_o = mcycle == 3'b001;
  /* T65_MCode.vhd:299:15  */
  assign n1283_o = mcycle == 3'b010;
  /* T65_MCode.vhd:302:15  */
  assign n1285_o = mcycle == 3'b011;
  /* T65_MCode.vhd:306:15  */
  assign n1287_o = mcycle == 3'b100;
  /* T65_MCode.vhd:311:15  */
  assign n1289_o = mcycle == 3'b101;
  assign n1290_o = {n1289_o, n1287_o, n1285_o, n1283_o, n1281_o};
  /* T65_MCode.vhd:296:15  */
  always @*
    case (n1290_o)
      5'b10000: n1292_o = n1200_o;
      5'b01000: n1292_o = n1200_o;
      5'b00100: n1292_o = 4'b0000;
      5'b00010: n1292_o = n1200_o;
      5'b00001: n1292_o = n1200_o;
      default: n1292_o = n1200_o;
    endcase
  /* T65_MCode.vhd:296:15  */
  always @*
    case (n1290_o)
      5'b10000: n1298_o = 2'b00;
      5'b01000: n1298_o = 2'b01;
      5'b00100: n1298_o = 2'b01;
      5'b00010: n1298_o = 2'b01;
      5'b00001: n1298_o = 2'b01;
      default: n1298_o = 2'b00;
    endcase
  /* T65_MCode.vhd:296:15  */
  always @*
    case (n1290_o)
      5'b10000: n1301_o = 2'b10;
      5'b01000: n1301_o = 2'b00;
      5'b00100: n1301_o = 2'b00;
      5'b00010: n1301_o = 2'b00;
      5'b00001: n1301_o = 2'b00;
      default: n1301_o = 2'b00;
    endcase
  /* T65_MCode.vhd:296:15  */
  always @*
    case (n1290_o)
      5'b10000: n1306_o = 1'b0;
      5'b01000: n1306_o = 1'b1;
      5'b00100: n1306_o = 1'b1;
      5'b00010: n1306_o = 1'b1;
      5'b00001: n1306_o = 1'b0;
      default: n1306_o = 1'b0;
    endcase
  /* T65_MCode.vhd:296:15  */
  always @*
    case (n1290_o)
      5'b10000: n1309_o = 1'b0;
      5'b01000: n1309_o = 1'b1;
      5'b00100: n1309_o = 1'b0;
      5'b00010: n1309_o = 1'b0;
      5'b00001: n1309_o = 1'b0;
      default: n1309_o = 1'b0;
    endcase
  /* T65_MCode.vhd:296:15  */
  always @*
    case (n1290_o)
      5'b10000: n1312_o = 1'b0;
      5'b01000: n1312_o = 1'b1;
      5'b00100: n1312_o = 1'b0;
      5'b00010: n1312_o = 1'b0;
      5'b00001: n1312_o = 1'b0;
      default: n1312_o = 1'b0;
    endcase
  /* T65_MCode.vhd:294:13  */
  assign n1314_o = ir == 8'b01000000;
  /* T65_MCode.vhd:318:15  */
  assign n1316_o = mcycle == 3'b001;
  /* T65_MCode.vhd:320:15  */
  assign n1318_o = mcycle == 3'b010;
  /* T65_MCode.vhd:323:15  */
  assign n1320_o = mcycle == 3'b011;
  /* T65_MCode.vhd:327:15  */
  assign n1322_o = mcycle == 3'b100;
  /* T65_MCode.vhd:329:15  */
  assign n1324_o = mcycle == 3'b101;
  assign n1325_o = {n1324_o, n1322_o, n1320_o, n1318_o, n1316_o};
  /* T65_MCode.vhd:317:13  */
  always @*
    case (n1325_o)
      5'b10000: n1330_o = 2'b00;
      5'b01000: n1330_o = 2'b00;
      5'b00100: n1330_o = 2'b01;
      5'b00010: n1330_o = 2'b01;
      5'b00001: n1330_o = 2'b01;
      default: n1330_o = 2'b00;
    endcase
  /* T65_MCode.vhd:317:13  */
  always @*
    case (n1325_o)
      5'b10000: n1334_o = 2'b01;
      5'b01000: n1334_o = 2'b10;
      5'b00100: n1334_o = 2'b00;
      5'b00010: n1334_o = 2'b00;
      5'b00001: n1334_o = 2'b00;
      default: n1334_o = 2'b00;
    endcase
  /* T65_MCode.vhd:317:13  */
  always @*
    case (n1325_o)
      5'b10000: n1338_o = 1'b0;
      5'b01000: n1338_o = 1'b0;
      5'b00100: n1338_o = 1'b1;
      5'b00010: n1338_o = 1'b1;
      5'b00001: n1338_o = 1'b0;
      default: n1338_o = 1'b0;
    endcase
  /* T65_MCode.vhd:317:13  */
  always @*
    case (n1325_o)
      5'b10000: n1341_o = 1'b0;
      5'b01000: n1341_o = 1'b0;
      5'b00100: n1341_o = 1'b1;
      5'b00010: n1341_o = 1'b0;
      5'b00001: n1341_o = 1'b0;
      default: n1341_o = 1'b0;
    endcase
  /* T65_MCode.vhd:315:11  */
  assign n1343_o = ir == 8'b01100000;
  /* T65_MCode.vhd:335:23  */
  assign n1345_o = mode == 2'b00;
  /* T65_MCode.vhd:335:36  */
  assign n1346_o = ir[1]; // extract
  /* T65_MCode.vhd:335:30  */
  assign n1347_o = n1346_o & n1345_o;
  /* T65_MCode.vhd:335:15  */
  assign n1350_o = n1347_o ? 3'b001 : 3'b010;
  /* T65_MCode.vhd:340:24  */
  assign n1353_o = mode != 2'b00;
  /* T65_MCode.vhd:340:36  */
  assign n1354_o = ir[1]; // extract
  /* T65_MCode.vhd:340:39  */
  assign n1355_o = ~n1354_o;
  /* T65_MCode.vhd:340:31  */
  assign n1356_o = n1353_o | n1355_o;
  /* T65_MCode.vhd:342:26  */
  assign n1357_o = ir[7:4]; // extract
  /* T65_MCode.vhd:343:19  */
  assign n1359_o = n1357_o == 4'b0000;
  /* T65_MCode.vhd:345:19  */
  assign n1361_o = n1357_o == 4'b0100;
  /* T65_MCode.vhd:348:29  */
  assign n1363_o = mode != 2'b00;
  /* T65_MCode.vhd:348:21  */
  assign n1365_o = n1363_o ? 4'b0011 : n1170_o;
  /* T65_MCode.vhd:348:21  */
  assign n1368_o = n1363_o ? 1'b1 : 1'b0;
  /* T65_MCode.vhd:347:19  */
  assign n1370_o = n1357_o == 4'b0101;
  /* T65_MCode.vhd:354:29  */
  assign n1372_o = mode != 2'b00;
  /* T65_MCode.vhd:354:21  */
  assign n1374_o = n1372_o ? 4'b0010 : n1170_o;
  /* T65_MCode.vhd:354:21  */
  assign n1377_o = n1372_o ? 1'b1 : 1'b0;
  /* T65_MCode.vhd:353:19  */
  assign n1379_o = n1357_o == 4'b1101;
  assign n1380_o = {n1379_o, n1370_o, n1361_o, n1359_o};
  /* T65_MCode.vhd:342:19  */
  always @*
    case (n1380_o)
      4'b1000: n1383_o = n1374_o;
      4'b0100: n1383_o = n1365_o;
      4'b0010: n1383_o = 4'b0001;
      4'b0001: n1383_o = 4'b0101;
      default: n1383_o = n1170_o;
    endcase
  /* T65_MCode.vhd:342:19  */
  always @*
    case (n1380_o)
      4'b1000: n1385_o = n1377_o;
      4'b0100: n1385_o = n1368_o;
      4'b0010: n1385_o = 1'b1;
      4'b0001: n1385_o = 1'b1;
      default: n1385_o = 1'b1;
    endcase
  /* T65_MCode.vhd:340:17  */
  assign n1389_o = n1356_o ? 2'b01 : 2'b00;
  /* T65_MCode.vhd:340:17  */
  assign n1390_o = n1356_o ? n1383_o : n1170_o;
  /* T65_MCode.vhd:340:17  */
  assign n1392_o = n1356_o ? n1385_o : 1'b0;
  /* T65_MCode.vhd:339:15  */
  assign n1394_o = mcycle == 3'b001;
  /* T65_MCode.vhd:363:15  */
  assign n1396_o = mcycle == 3'b010;
  assign n1397_o = {n1396_o, n1394_o};
  /* T65_MCode.vhd:338:15  */
  always @*
    case (n1397_o)
      2'b10: n1399_o = 2'b00;
      2'b01: n1399_o = n1389_o;
      default: n1399_o = 2'b00;
    endcase
  /* T65_MCode.vhd:338:15  */
  always @*
    case (n1397_o)
      2'b10: n1400_o = n1170_o;
      2'b01: n1400_o = n1390_o;
      default: n1400_o = n1170_o;
    endcase
  /* T65_MCode.vhd:338:15  */
  always @*
    case (n1397_o)
      2'b10: n1403_o = 1'b1;
      2'b01: n1403_o = 1'b0;
      default: n1403_o = 1'b0;
    endcase
  /* T65_MCode.vhd:338:15  */
  always @*
    case (n1397_o)
      2'b10: n1405_o = 1'b0;
      2'b01: n1405_o = n1392_o;
      default: n1405_o = 1'b0;
    endcase
  /* T65_MCode.vhd:333:13  */
  assign n1407_o = ir == 8'b00001000;
  /* T65_MCode.vhd:333:24  */
  assign n1409_o = ir == 8'b01001000;
  /* T65_MCode.vhd:333:24  */
  assign n1410_o = n1407_o | n1409_o;
  /* T65_MCode.vhd:333:32  */
  assign n1412_o = ir == 8'b01011010;
  /* T65_MCode.vhd:333:32  */
  assign n1413_o = n1410_o | n1412_o;
  /* T65_MCode.vhd:333:40  */
  assign n1415_o = ir == 8'b11011010;
  /* T65_MCode.vhd:333:40  */
  assign n1416_o = n1413_o | n1415_o;
  /* T65_MCode.vhd:369:21  */
  assign n1418_o = mode == 2'b00;
  /* T65_MCode.vhd:369:34  */
  assign n1419_o = ir[1]; // extract
  /* T65_MCode.vhd:369:28  */
  assign n1420_o = n1419_o & n1418_o;
  /* T65_MCode.vhd:369:13  */
  assign n1423_o = n1420_o ? 3'b001 : 3'b011;
  /* T65_MCode.vhd:372:20  */
  assign n1425_o = ir[7:4]; // extract
  /* T65_MCode.vhd:373:15  */
  assign n1427_o = n1425_o == 4'b0010;
  /* T65_MCode.vhd:375:15  */
  assign n1429_o = n1425_o == 4'b0110;
  /* T65_MCode.vhd:378:25  */
  assign n1431_o = mode != 2'b00;
  /* T65_MCode.vhd:378:17  */
  assign n1433_o = n1431_o ? 1'b1 : n1179_o;
  /* T65_MCode.vhd:377:15  */
  assign n1435_o = n1425_o == 4'b0111;
  /* T65_MCode.vhd:382:25  */
  assign n1437_o = mode != 2'b00;
  /* T65_MCode.vhd:382:17  */
  assign n1439_o = n1437_o ? 1'b1 : n1176_o;
  /* T65_MCode.vhd:381:15  */
  assign n1441_o = n1425_o == 4'b1111;
  assign n1442_o = {n1441_o, n1435_o, n1429_o, n1427_o};
  /* T65_MCode.vhd:372:13  */
  always @*
    case (n1442_o)
      4'b1000: n1444_o = n1173_o;
      4'b0100: n1444_o = n1173_o;
      4'b0010: n1444_o = 1'b1;
      4'b0001: n1444_o = n1173_o;
      default: n1444_o = n1173_o;
    endcase
  /* T65_MCode.vhd:372:13  */
  always @*
    case (n1442_o)
      4'b1000: n1447_o = 1'b0;
      4'b0100: n1447_o = 1'b0;
      4'b0010: n1447_o = 1'b0;
      4'b0001: n1447_o = 1'b1;
      default: n1447_o = 1'b0;
    endcase
  /* T65_MCode.vhd:372:13  */
  always @*
    case (n1442_o)
      4'b1000: n1448_o = n1439_o;
      4'b0100: n1448_o = n1176_o;
      4'b0010: n1448_o = n1176_o;
      4'b0001: n1448_o = n1176_o;
      default: n1448_o = n1176_o;
    endcase
  /* T65_MCode.vhd:372:13  */
  always @*
    case (n1442_o)
      4'b1000: n1449_o = n1179_o;
      4'b0100: n1449_o = n1433_o;
      4'b0010: n1449_o = n1179_o;
      4'b0001: n1449_o = n1179_o;
      default: n1449_o = n1179_o;
    endcase
  /* T65_MCode.vhd:389:25  */
  assign n1451_o = mode != 2'b00;
  /* T65_MCode.vhd:389:38  */
  assign n1452_o = ir[1]; // extract
  /* T65_MCode.vhd:389:42  */
  assign n1453_o = ~n1452_o;
  /* T65_MCode.vhd:389:33  */
  assign n1454_o = n1451_o | n1453_o;
  /* T65_MCode.vhd:389:17  */
  assign n1457_o = n1454_o ? 1'b1 : 1'b0;
  /* T65_MCode.vhd:388:15  */
  assign n1459_o = mcycle == 3'b000;
  /* T65_MCode.vhd:393:25  */
  assign n1461_o = mode != 2'b00;
  /* T65_MCode.vhd:393:38  */
  assign n1462_o = ir[1]; // extract
  /* T65_MCode.vhd:393:42  */
  assign n1463_o = ~n1462_o;
  /* T65_MCode.vhd:393:33  */
  assign n1464_o = n1461_o | n1463_o;
  /* T65_MCode.vhd:393:17  */
  assign n1467_o = n1464_o ? 2'b01 : 2'b00;
  /* T65_MCode.vhd:393:17  */
  assign n1469_o = n1464_o ? 1'b0 : n1447_o;
  /* T65_MCode.vhd:392:15  */
  assign n1471_o = mcycle == 3'b001;
  /* T65_MCode.vhd:397:15  */
  assign n1473_o = mcycle == 3'b010;
  /* T65_MCode.vhd:401:15  */
  assign n1475_o = mcycle == 3'b011;
  assign n1476_o = {n1475_o, n1473_o, n1471_o, n1459_o};
  /* T65_MCode.vhd:387:13  */
  always @*
    case (n1476_o)
      4'b1000: n1478_o = 4'b0000;
      4'b0100: n1478_o = n1200_o;
      4'b0010: n1478_o = n1200_o;
      4'b0001: n1478_o = n1200_o;
      default: n1478_o = n1200_o;
    endcase
  /* T65_MCode.vhd:387:13  */
  always @*
    case (n1476_o)
      4'b1000: n1481_o = 2'b00;
      4'b0100: n1481_o = 2'b01;
      4'b0010: n1481_o = n1467_o;
      4'b0001: n1481_o = 2'b00;
      default: n1481_o = 2'b00;
    endcase
  /* T65_MCode.vhd:387:13  */
  always @*
    case (n1476_o)
      4'b1000: n1484_o = 1'b0;
      4'b0100: n1484_o = 1'b1;
      4'b0010: n1484_o = 1'b0;
      4'b0001: n1484_o = 1'b0;
      default: n1484_o = 1'b0;
    endcase
  /* T65_MCode.vhd:387:13  */
  always @*
    case (n1476_o)
      4'b1000: n1486_o = n1447_o;
      4'b0100: n1486_o = 1'b0;
      4'b0010: n1486_o = n1469_o;
      4'b0001: n1486_o = n1447_o;
      default: n1486_o = n1447_o;
    endcase
  /* T65_MCode.vhd:387:13  */
  always @*
    case (n1476_o)
      4'b1000: n1488_o = 1'b0;
      4'b0100: n1488_o = 1'b0;
      4'b0010: n1488_o = 1'b0;
      4'b0001: n1488_o = n1457_o;
      default: n1488_o = 1'b0;
    endcase
  /* T65_MCode.vhd:367:11  */
  assign n1490_o = ir == 8'b00101000;
  /* T65_MCode.vhd:367:23  */
  assign n1492_o = ir == 8'b01101000;
  /* T65_MCode.vhd:367:23  */
  assign n1493_o = n1490_o | n1492_o;
  /* T65_MCode.vhd:367:31  */
  assign n1495_o = ir == 8'b01111010;
  /* T65_MCode.vhd:367:31  */
  assign n1496_o = n1493_o | n1495_o;
  /* T65_MCode.vhd:367:39  */
  assign n1498_o = ir == 8'b11111010;
  /* T65_MCode.vhd:367:39  */
  assign n1499_o = n1496_o | n1498_o;
  /* T65_MCode.vhd:408:15  */
  assign n1501_o = mcycle == 3'b000;
  /* T65_MCode.vhd:409:15  */
  assign n1503_o = mcycle == 3'b001;
  assign n1504_o = {n1503_o, n1501_o};
  /* T65_MCode.vhd:407:13  */
  always @*
    case (n1504_o)
      2'b10: n1507_o = 2'b01;
      2'b01: n1507_o = 2'b00;
      default: n1507_o = 2'b00;
    endcase
  /* T65_MCode.vhd:405:11  */
  assign n1509_o = ir == 8'b10100000;
  /* T65_MCode.vhd:405:22  */
  assign n1511_o = ir == 8'b11000000;
  /* T65_MCode.vhd:405:22  */
  assign n1512_o = n1509_o | n1511_o;
  /* T65_MCode.vhd:405:30  */
  assign n1514_o = ir == 8'b11100000;
  /* T65_MCode.vhd:405:30  */
  assign n1515_o = n1512_o | n1514_o;
  /* T65_MCode.vhd:416:15  */
  assign n1517_o = mcycle == 3'b000;
  /* T65_MCode.vhd:417:15  */
  assign n1519_o = mcycle == 3'b001;
  assign n1520_o = {n1519_o, n1517_o};
  /* T65_MCode.vhd:415:13  */
  always @*
    case (n1520_o)
      2'b10: n1522_o = 4'b0011;
      2'b01: n1522_o = n1200_o;
      default: n1522_o = n1200_o;
    endcase
  /* T65_MCode.vhd:413:11  */
  assign n1524_o = ir == 8'b10001000;
  /* T65_MCode.vhd:424:15  */
  assign n1526_o = mcycle == 3'b000;
  /* T65_MCode.vhd:425:15  */
  assign n1528_o = mcycle == 3'b001;
  assign n1529_o = {n1528_o, n1526_o};
  /* T65_MCode.vhd:423:13  */
  always @*
    case (n1529_o)
      2'b10: n1531_o = 4'b0010;
      2'b01: n1531_o = n1200_o;
      default: n1531_o = n1200_o;
    endcase
  /* T65_MCode.vhd:421:11  */
  assign n1533_o = ir == 8'b11001010;
  /* T65_MCode.vhd:430:21  */
  assign n1535_o = mode != 2'b00;
  /* T65_MCode.vhd:430:13  */
  assign n1537_o = n1535_o ? 1'b1 : n1173_o;
  /* T65_MCode.vhd:436:15  */
  assign n1539_o = mcycle == 3'b000;
  /* T65_MCode.vhd:437:15  */
  assign n1541_o = mcycle == 3'b001;
  assign n1542_o = {n1541_o, n1539_o};
  /* T65_MCode.vhd:435:13  */
  always @*
    case (n1542_o)
      2'b10: n1544_o = 4'b0100;
      2'b01: n1544_o = n1200_o;
      default: n1544_o = n1200_o;
    endcase
  /* T65_MCode.vhd:429:11  */
  assign n1546_o = ir == 8'b00011010;
  /* T65_MCode.vhd:429:22  */
  assign n1548_o = ir == 8'b00111010;
  /* T65_MCode.vhd:429:22  */
  assign n1549_o = n1546_o | n1548_o;
  /* T65_MCode.vhd:441:11  */
  assign n1556_o = ir == 8'b00001010;
  /* T65_MCode.vhd:441:22  */
  assign n1558_o = ir == 8'b00101010;
  /* T65_MCode.vhd:441:22  */
  assign n1559_o = n1556_o | n1558_o;
  /* T65_MCode.vhd:441:30  */
  assign n1561_o = ir == 8'b01001010;
  /* T65_MCode.vhd:441:30  */
  assign n1562_o = n1559_o | n1561_o;
  /* T65_MCode.vhd:441:38  */
  assign n1564_o = ir == 8'b01101010;
  /* T65_MCode.vhd:441:38  */
  assign n1565_o = n1562_o | n1564_o;
  /* T65_MCode.vhd:449:11  */
  assign n1572_o = ir == 8'b10001010;
  /* T65_MCode.vhd:449:22  */
  assign n1574_o = ir == 8'b10011000;
  /* T65_MCode.vhd:449:22  */
  assign n1575_o = n1572_o | n1574_o;
  /* T65_MCode.vhd:458:15  */
  assign n1577_o = mcycle == 3'b000;
  /* T65_MCode.vhd:459:15  */
  assign n1579_o = mcycle == 3'b001;
  assign n1580_o = {n1579_o, n1577_o};
  /* T65_MCode.vhd:457:13  */
  always @*
    case (n1580_o)
      2'b10: n1582_o = 4'b0001;
      2'b01: n1582_o = n1200_o;
      default: n1582_o = n1200_o;
    endcase
  /* T65_MCode.vhd:456:11  */
  assign n1584_o = ir == 8'b10101010;
  /* T65_MCode.vhd:456:22  */
  assign n1586_o = ir == 8'b10101000;
  /* T65_MCode.vhd:456:22  */
  assign n1587_o = n1584_o | n1586_o;
  /* T65_MCode.vhd:463:11  */
  assign n1589_o = ir == 8'b10011010;
  /* T65_MCode.vhd:468:15  */
  assign n1591_o = mcycle == 3'b000;
  /* T65_MCode.vhd:469:15  */
  assign n1593_o = mcycle == 3'b001;
  assign n1594_o = {n1593_o, n1591_o};
  /* T65_MCode.vhd:467:13  */
  always @*
    case (n1594_o)
      2'b10: n1596_o = 4'b0100;
      2'b01: n1596_o = n1200_o;
      default: n1596_o = n1200_o;
    endcase
  /* T65_MCode.vhd:465:11  */
  assign n1598_o = ir == 8'b10111010;
  /* T65_MCode.vhd:475:15  */
  assign n1600_o = mcycle == 3'b000;
  /* T65_MCode.vhd:476:15  */
  assign n1602_o = mcycle == 3'b001;
  assign n1603_o = {n1602_o, n1600_o};
  /* T65_MCode.vhd:474:13  */
  always @*
    case (n1603_o)
      2'b10: n1606_o = 2'b01;
      2'b01: n1606_o = 2'b00;
      default: n1606_o = 2'b00;
    endcase
  /* T65_MCode.vhd:473:11  */
  assign n1608_o = ir == 8'b10000000;
  assign n1611_o = {n1608_o, n1598_o, n1589_o, n1587_o, n1575_o, n1565_o, n1549_o, n1533_o, n1524_o, n1515_o, n1499_o, n1416_o, n1343_o, n1314_o, n1279_o, n1243_o};
  /* T65_MCode.vhd:243:9  */
  always @*
    case (n1611_o)
      16'b1000000000000000: n1617_o = 3'b001;
      16'b0100000000000000: n1617_o = 3'b001;
      16'b0010000000000000: n1617_o = 3'b001;
      16'b0001000000000000: n1617_o = 3'b001;
      16'b0000100000000000: n1617_o = 3'b001;
      16'b0000010000000000: n1617_o = 3'b001;
      16'b0000001000000000: n1617_o = 3'b001;
      16'b0000000100000000: n1617_o = 3'b001;
      16'b0000000010000000: n1617_o = 3'b001;
      16'b0000000001000000: n1617_o = 3'b001;
      16'b0000000000100000: n1617_o = n1423_o;
      16'b0000000000010000: n1617_o = n1350_o;
      16'b0000000000001000: n1617_o = 3'b101;
      16'b0000000000000100: n1617_o = 3'b101;
      16'b0000000000000010: n1617_o = 3'b101;
      16'b0000000000000001: n1617_o = 3'b110;
      default: n1617_o = 3'b001;
    endcase
  /* T65_MCode.vhd:243:9  */
  always @*
    case (n1611_o)
      16'b1000000000000000: n1619_o = n1200_o;
      16'b0100000000000000: n1619_o = n1596_o;
      16'b0010000000000000: n1619_o = n1200_o;
      16'b0001000000000000: n1619_o = n1582_o;
      16'b0000100000000000: n1619_o = n1200_o;
      16'b0000010000000000: n1619_o = 4'b0001;
      16'b0000001000000000: n1619_o = n1544_o;
      16'b0000000100000000: n1619_o = n1531_o;
      16'b0000000010000000: n1619_o = n1522_o;
      16'b0000000001000000: n1619_o = n1200_o;
      16'b0000000000100000: n1619_o = n1478_o;
      16'b0000000000010000: n1619_o = n1200_o;
      16'b0000000000001000: n1619_o = n1200_o;
      16'b0000000000000100: n1619_o = n1292_o;
      16'b0000000000000010: n1619_o = n1200_o;
      16'b0000000000000001: n1619_o = n1200_o;
      default: n1619_o = n1200_o;
    endcase
  /* T65_MCode.vhd:243:9  */
  always @*
    case (n1611_o)
      16'b1000000000000000: n1621_o = 2'b00;
      16'b0100000000000000: n1621_o = 2'b00;
      16'b0010000000000000: n1621_o = 2'b00;
      16'b0001000000000000: n1621_o = 2'b00;
      16'b0000100000000000: n1621_o = 2'b00;
      16'b0000010000000000: n1621_o = 2'b00;
      16'b0000001000000000: n1621_o = 2'b00;
      16'b0000000100000000: n1621_o = 2'b00;
      16'b0000000010000000: n1621_o = 2'b00;
      16'b0000000001000000: n1621_o = 2'b00;
      16'b0000000000100000: n1621_o = n1481_o;
      16'b0000000000010000: n1621_o = n1399_o;
      16'b0000000000001000: n1621_o = n1330_o;
      16'b0000000000000100: n1621_o = n1298_o;
      16'b0000000000000010: n1621_o = n1259_o;
      16'b0000000000000001: n1621_o = n1221_o;
      default: n1621_o = 2'b00;
    endcase
  /* T65_MCode.vhd:243:9  */
  always @*
    case (n1611_o)
      16'b1000000000000000: n1622_o = n1170_o;
      16'b0100000000000000: n1622_o = n1170_o;
      16'b0010000000000000: n1622_o = n1170_o;
      16'b0001000000000000: n1622_o = n1170_o;
      16'b0000100000000000: n1622_o = n1170_o;
      16'b0000010000000000: n1622_o = n1170_o;
      16'b0000001000000000: n1622_o = n1170_o;
      16'b0000000100000000: n1622_o = n1170_o;
      16'b0000000010000000: n1622_o = n1170_o;
      16'b0000000001000000: n1622_o = n1170_o;
      16'b0000000000100000: n1622_o = n1170_o;
      16'b0000000000010000: n1622_o = n1400_o;
      16'b0000000000001000: n1622_o = n1170_o;
      16'b0000000000000100: n1622_o = n1170_o;
      16'b0000000000000010: n1622_o = n1262_o;
      16'b0000000000000001: n1622_o = n1225_o;
      default: n1622_o = n1170_o;
    endcase
  /* T65_MCode.vhd:243:9  */
  always @*
    case (n1611_o)
      16'b1000000000000000: n1624_o = n1606_o;
      16'b0100000000000000: n1624_o = 2'b00;
      16'b0010000000000000: n1624_o = 2'b00;
      16'b0001000000000000: n1624_o = 2'b00;
      16'b0000100000000000: n1624_o = 2'b00;
      16'b0000010000000000: n1624_o = 2'b00;
      16'b0000001000000000: n1624_o = 2'b00;
      16'b0000000100000000: n1624_o = 2'b00;
      16'b0000000010000000: n1624_o = 2'b00;
      16'b0000000001000000: n1624_o = n1507_o;
      16'b0000000000100000: n1624_o = 2'b00;
      16'b0000000000010000: n1624_o = 2'b00;
      16'b0000000000001000: n1624_o = n1334_o;
      16'b0000000000000100: n1624_o = n1301_o;
      16'b0000000000000010: n1624_o = n1266_o;
      16'b0000000000000001: n1624_o = n1228_o;
      default: n1624_o = 2'b00;
    endcase
  /* T65_MCode.vhd:243:9  */
  always @*
    case (n1611_o)
      16'b1000000000000000: n1626_o = 1'b0;
      16'b0100000000000000: n1626_o = 1'b0;
      16'b0010000000000000: n1626_o = 1'b0;
      16'b0001000000000000: n1626_o = 1'b0;
      16'b0000100000000000: n1626_o = 1'b0;
      16'b0000010000000000: n1626_o = 1'b0;
      16'b0000001000000000: n1626_o = 1'b0;
      16'b0000000100000000: n1626_o = 1'b0;
      16'b0000000010000000: n1626_o = 1'b0;
      16'b0000000001000000: n1626_o = 1'b0;
      16'b0000000000100000: n1626_o = n1484_o;
      16'b0000000000010000: n1626_o = 1'b0;
      16'b0000000000001000: n1626_o = n1338_o;
      16'b0000000000000100: n1626_o = n1306_o;
      16'b0000000000000010: n1626_o = 1'b0;
      16'b0000000000000001: n1626_o = 1'b0;
      default: n1626_o = 1'b0;
    endcase
  /* T65_MCode.vhd:243:9  */
  always @*
    case (n1611_o)
      16'b1000000000000000: n1628_o = 1'b0;
      16'b0100000000000000: n1628_o = 1'b0;
      16'b0010000000000000: n1628_o = 1'b0;
      16'b0001000000000000: n1628_o = 1'b0;
      16'b0000100000000000: n1628_o = 1'b0;
      16'b0000010000000000: n1628_o = 1'b0;
      16'b0000001000000000: n1628_o = 1'b0;
      16'b0000000100000000: n1628_o = 1'b0;
      16'b0000000010000000: n1628_o = 1'b0;
      16'b0000000001000000: n1628_o = 1'b0;
      16'b0000000000100000: n1628_o = 1'b0;
      16'b0000000000010000: n1628_o = n1403_o;
      16'b0000000000001000: n1628_o = 1'b0;
      16'b0000000000000100: n1628_o = 1'b0;
      16'b0000000000000010: n1628_o = n1270_o;
      16'b0000000000000001: n1628_o = n1233_o;
      default: n1628_o = 1'b0;
    endcase
  /* T65_MCode.vhd:243:9  */
  always @*
    case (n1611_o)
      16'b1000000000000000: n1631_o = n1173_o;
      16'b0100000000000000: n1631_o = n1173_o;
      16'b0010000000000000: n1631_o = n1173_o;
      16'b0001000000000000: n1631_o = n1173_o;
      16'b0000100000000000: n1631_o = 1'b1;
      16'b0000010000000000: n1631_o = 1'b1;
      16'b0000001000000000: n1631_o = n1537_o;
      16'b0000000100000000: n1631_o = n1173_o;
      16'b0000000010000000: n1631_o = n1173_o;
      16'b0000000001000000: n1631_o = n1173_o;
      16'b0000000000100000: n1631_o = n1444_o;
      16'b0000000000010000: n1631_o = n1173_o;
      16'b0000000000001000: n1631_o = n1173_o;
      16'b0000000000000100: n1631_o = n1173_o;
      16'b0000000000000010: n1631_o = n1173_o;
      16'b0000000000000001: n1631_o = n1173_o;
      default: n1631_o = n1173_o;
    endcase
  /* T65_MCode.vhd:243:9  */
  always @*
    case (n1611_o)
      16'b1000000000000000: n1633_o = 1'b0;
      16'b0100000000000000: n1633_o = 1'b0;
      16'b0010000000000000: n1633_o = 1'b0;
      16'b0001000000000000: n1633_o = 1'b0;
      16'b0000100000000000: n1633_o = 1'b0;
      16'b0000010000000000: n1633_o = 1'b0;
      16'b0000001000000000: n1633_o = 1'b0;
      16'b0000000100000000: n1633_o = 1'b0;
      16'b0000000010000000: n1633_o = 1'b0;
      16'b0000000001000000: n1633_o = 1'b0;
      16'b0000000000100000: n1633_o = n1486_o;
      16'b0000000000010000: n1633_o = 1'b0;
      16'b0000000000001000: n1633_o = 1'b0;
      16'b0000000000000100: n1633_o = n1309_o;
      16'b0000000000000010: n1633_o = 1'b0;
      16'b0000000000000001: n1633_o = 1'b0;
      default: n1633_o = 1'b0;
    endcase
  /* T65_MCode.vhd:243:9  */
  always @*
    case (n1611_o)
      16'b1000000000000000: n1636_o = n1176_o;
      16'b0100000000000000: n1636_o = 1'b1;
      16'b0010000000000000: n1636_o = n1176_o;
      16'b0001000000000000: n1636_o = n1176_o;
      16'b0000100000000000: n1636_o = n1176_o;
      16'b0000010000000000: n1636_o = n1176_o;
      16'b0000001000000000: n1636_o = n1176_o;
      16'b0000000100000000: n1636_o = 1'b1;
      16'b0000000010000000: n1636_o = n1176_o;
      16'b0000000001000000: n1636_o = n1176_o;
      16'b0000000000100000: n1636_o = n1448_o;
      16'b0000000000010000: n1636_o = n1176_o;
      16'b0000000000001000: n1636_o = n1176_o;
      16'b0000000000000100: n1636_o = n1176_o;
      16'b0000000000000010: n1636_o = n1176_o;
      16'b0000000000000001: n1636_o = n1176_o;
      default: n1636_o = n1176_o;
    endcase
  /* T65_MCode.vhd:243:9  */
  always @*
    case (n1611_o)
      16'b1000000000000000: n1638_o = n1179_o;
      16'b0100000000000000: n1638_o = n1179_o;
      16'b0010000000000000: n1638_o = n1179_o;
      16'b0001000000000000: n1638_o = n1179_o;
      16'b0000100000000000: n1638_o = n1179_o;
      16'b0000010000000000: n1638_o = n1179_o;
      16'b0000001000000000: n1638_o = n1179_o;
      16'b0000000100000000: n1638_o = n1179_o;
      16'b0000000010000000: n1638_o = 1'b1;
      16'b0000000001000000: n1638_o = n1179_o;
      16'b0000000000100000: n1638_o = n1449_o;
      16'b0000000000010000: n1638_o = n1179_o;
      16'b0000000000001000: n1638_o = n1179_o;
      16'b0000000000000100: n1638_o = n1179_o;
      16'b0000000000000010: n1638_o = n1179_o;
      16'b0000000000000001: n1638_o = n1179_o;
      default: n1638_o = n1179_o;
    endcase
  /* T65_MCode.vhd:243:9  */
  always @*
    case (n1611_o)
      16'b1000000000000000: n1640_o = n1182_o;
      16'b0100000000000000: n1640_o = n1182_o;
      16'b0010000000000000: n1640_o = 1'b1;
      16'b0001000000000000: n1640_o = n1182_o;
      16'b0000100000000000: n1640_o = n1182_o;
      16'b0000010000000000: n1640_o = n1182_o;
      16'b0000001000000000: n1640_o = n1182_o;
      16'b0000000100000000: n1640_o = n1182_o;
      16'b0000000010000000: n1640_o = n1182_o;
      16'b0000000001000000: n1640_o = n1182_o;
      16'b0000000000100000: n1640_o = n1182_o;
      16'b0000000000010000: n1640_o = n1182_o;
      16'b0000000000001000: n1640_o = n1182_o;
      16'b0000000000000100: n1640_o = n1182_o;
      16'b0000000000000010: n1640_o = n1182_o;
      16'b0000000000000001: n1640_o = n1182_o;
      default: n1640_o = n1182_o;
    endcase
  /* T65_MCode.vhd:243:9  */
  always @*
    case (n1611_o)
      16'b1000000000000000: n1642_o = 1'b0;
      16'b0100000000000000: n1642_o = 1'b0;
      16'b0010000000000000: n1642_o = 1'b0;
      16'b0001000000000000: n1642_o = 1'b0;
      16'b0000100000000000: n1642_o = 1'b0;
      16'b0000010000000000: n1642_o = 1'b0;
      16'b0000001000000000: n1642_o = 1'b0;
      16'b0000000100000000: n1642_o = 1'b0;
      16'b0000000010000000: n1642_o = 1'b0;
      16'b0000000001000000: n1642_o = 1'b0;
      16'b0000000000100000: n1642_o = 1'b0;
      16'b0000000000010000: n1642_o = 1'b0;
      16'b0000000000001000: n1642_o = n1341_o;
      16'b0000000000000100: n1642_o = n1312_o;
      16'b0000000000000010: n1642_o = n1273_o;
      16'b0000000000000001: n1642_o = n1236_o;
      default: n1642_o = 1'b0;
    endcase
  /* T65_MCode.vhd:243:9  */
  always @*
    case (n1611_o)
      16'b1000000000000000: n1644_o = 1'b0;
      16'b0100000000000000: n1644_o = 1'b0;
      16'b0010000000000000: n1644_o = 1'b0;
      16'b0001000000000000: n1644_o = 1'b0;
      16'b0000100000000000: n1644_o = 1'b0;
      16'b0000010000000000: n1644_o = 1'b0;
      16'b0000001000000000: n1644_o = 1'b0;
      16'b0000000100000000: n1644_o = 1'b0;
      16'b0000000010000000: n1644_o = 1'b0;
      16'b0000000001000000: n1644_o = 1'b0;
      16'b0000000000100000: n1644_o = n1488_o;
      16'b0000000000010000: n1644_o = 1'b0;
      16'b0000000000001000: n1644_o = 1'b0;
      16'b0000000000000100: n1644_o = 1'b0;
      16'b0000000000000010: n1644_o = 1'b0;
      16'b0000000000000001: n1644_o = 1'b0;
      default: n1644_o = 1'b0;
    endcase
  /* T65_MCode.vhd:243:9  */
  always @*
    case (n1611_o)
      16'b1000000000000000: n1646_o = 1'b0;
      16'b0100000000000000: n1646_o = 1'b0;
      16'b0010000000000000: n1646_o = 1'b0;
      16'b0001000000000000: n1646_o = 1'b0;
      16'b0000100000000000: n1646_o = 1'b0;
      16'b0000010000000000: n1646_o = 1'b0;
      16'b0000001000000000: n1646_o = 1'b0;
      16'b0000000100000000: n1646_o = 1'b0;
      16'b0000000010000000: n1646_o = 1'b0;
      16'b0000000001000000: n1646_o = 1'b0;
      16'b0000000000100000: n1646_o = 1'b0;
      16'b0000000000010000: n1646_o = n1405_o;
      16'b0000000000001000: n1646_o = 1'b0;
      16'b0000000000000100: n1646_o = 1'b0;
      16'b0000000000000010: n1646_o = n1277_o;
      16'b0000000000000001: n1646_o = n1241_o;
      default: n1646_o = 1'b0;
    endcase
  /* T65_MCode.vhd:241:7  */
  assign n1648_o = n1201_o == 5'b00000;
  /* T65_MCode.vhd:241:20  */
  assign n1650_o = n1201_o == 5'b01000;
  /* T65_MCode.vhd:241:20  */
  assign n1651_o = n1648_o | n1650_o;
  /* T65_MCode.vhd:241:30  */
  assign n1653_o = n1201_o == 5'b01010;
  /* T65_MCode.vhd:241:30  */
  assign n1654_o = n1651_o | n1653_o;
  /* T65_MCode.vhd:241:40  */
  assign n1656_o = n1201_o == 5'b11000;
  /* T65_MCode.vhd:241:40  */
  assign n1657_o = n1654_o | n1656_o;
  /* T65_MCode.vhd:241:50  */
  assign n1659_o = n1201_o == 5'b11010;
  /* T65_MCode.vhd:241:50  */
  assign n1660_o = n1657_o | n1659_o;
  /* T65_MCode.vhd:492:14  */
  assign n1661_o = ir[7:6]; // extract
  /* T65_MCode.vhd:492:27  */
  assign n1663_o = n1661_o != 2'b10;
  /* T65_MCode.vhd:494:18  */
  assign n1665_o = mode == 2'b00;
  /* T65_MCode.vhd:494:30  */
  assign n1666_o = ir[1]; // extract
  /* T65_MCode.vhd:494:24  */
  assign n1667_o = n1666_o & n1665_o;
  /* T65_MCode.vhd:494:11  */
  assign n1670_o = n1667_o ? 3'b111 : 3'b101;
  /* T65_MCode.vhd:492:9  */
  assign n1672_o = n1663_o ? n1670_o : 3'b101;
  /* T65_MCode.vhd:492:9  */
  assign n1675_o = n1663_o ? 1'b1 : n1173_o;
  /* T65_MCode.vhd:499:11  */
  assign n1677_o = mcycle == 3'b001;
  /* T65_MCode.vhd:503:11  */
  assign n1679_o = mcycle == 3'b010;
  /* T65_MCode.vhd:506:11  */
  assign n1681_o = mcycle == 3'b011;
  /* T65_MCode.vhd:512:18  */
  assign n1682_o = ir[7:5]; // extract
  /* T65_MCode.vhd:512:31  */
  assign n1684_o = n1682_o == 3'b100;
  /* T65_MCode.vhd:512:13  */
  assign n1687_o = n1684_o ? 1'b1 : 1'b0;
  /* T65_MCode.vhd:510:11  */
  assign n1689_o = mcycle == 3'b100;
  /* T65_MCode.vhd:517:20  */
  assign n1691_o = mode == 2'b00;
  /* T65_MCode.vhd:517:32  */
  assign n1692_o = ir[1]; // extract
  /* T65_MCode.vhd:517:26  */
  assign n1693_o = n1692_o & n1691_o;
  /* T65_MCode.vhd:517:46  */
  assign n1694_o = ir[7:6]; // extract
  /* T65_MCode.vhd:517:58  */
  assign n1696_o = n1694_o != 2'b10;
  /* T65_MCode.vhd:517:40  */
  assign n1697_o = n1696_o & n1693_o;
  /* T65_MCode.vhd:517:13  */
  assign n1700_o = n1697_o ? 2'b11 : 2'b00;
  /* T65_MCode.vhd:517:13  */
  assign n1703_o = n1697_o ? 1'b1 : 1'b0;
  /* T65_MCode.vhd:517:13  */
  assign n1706_o = n1697_o ? 1'b1 : 1'b0;
  /* T65_MCode.vhd:516:11  */
  assign n1708_o = mcycle == 3'b101;
  /* T65_MCode.vhd:522:11  */
  assign n1710_o = mcycle == 3'b110;
  /* T65_MCode.vhd:527:11  */
  assign n1712_o = mcycle == 3'b111;
  assign n1713_o = {n1712_o, n1710_o, n1708_o, n1689_o, n1681_o, n1679_o, n1677_o};
  /* T65_MCode.vhd:498:9  */
  always @*
    case (n1713_o)
      7'b1000000: n1715_o = 4'b0001;
      7'b0100000: n1715_o = n1200_o;
      7'b0010000: n1715_o = n1200_o;
      7'b0001000: n1715_o = n1200_o;
      7'b0000100: n1715_o = n1200_o;
      7'b0000010: n1715_o = n1200_o;
      7'b0000001: n1715_o = n1200_o;
      default: n1715_o = n1200_o;
    endcase
  /* T65_MCode.vhd:498:9  */
  always @*
    case (n1713_o)
      7'b1000000: n1722_o = 2'b00;
      7'b0100000: n1722_o = 2'b11;
      7'b0010000: n1722_o = n1700_o;
      7'b0001000: n1722_o = 2'b11;
      7'b0000100: n1722_o = 2'b10;
      7'b0000010: n1722_o = 2'b10;
      7'b0000001: n1722_o = 2'b10;
      default: n1722_o = 2'b00;
    endcase
  /* T65_MCode.vhd:498:9  */
  always @*
    case (n1713_o)
      7'b1000000: n1725_o = 2'b00;
      7'b0100000: n1725_o = 2'b00;
      7'b0010000: n1725_o = 2'b00;
      7'b0001000: n1725_o = 2'b00;
      7'b0000100: n1725_o = 2'b00;
      7'b0000010: n1725_o = 2'b00;
      7'b0000001: n1725_o = 2'b01;
      default: n1725_o = 2'b00;
    endcase
  /* T65_MCode.vhd:498:9  */
  always @*
    case (n1713_o)
      7'b1000000: n1728_o = 2'b00;
      7'b0100000: n1728_o = 2'b00;
      7'b0010000: n1728_o = 2'b00;
      7'b0001000: n1728_o = 2'b00;
      7'b0000100: n1728_o = 2'b01;
      7'b0000010: n1728_o = 2'b00;
      7'b0000001: n1728_o = 2'b00;
      default: n1728_o = 2'b00;
    endcase
  /* T65_MCode.vhd:498:9  */
  always @*
    case (n1713_o)
      7'b1000000: n1731_o = 1'b0;
      7'b0100000: n1731_o = 1'b0;
      7'b0010000: n1731_o = 1'b0;
      7'b0001000: n1731_o = 1'b0;
      7'b0000100: n1731_o = 1'b0;
      7'b0000010: n1731_o = 1'b1;
      7'b0000001: n1731_o = 1'b0;
      default: n1731_o = 1'b0;
    endcase
  /* T65_MCode.vhd:498:9  */
  always @*
    case (n1713_o)
      7'b1000000: n1733_o = 1'b0;
      7'b0100000: n1733_o = 1'b0;
      7'b0010000: n1733_o = n1703_o;
      7'b0001000: n1733_o = 1'b0;
      7'b0000100: n1733_o = 1'b0;
      7'b0000010: n1733_o = 1'b0;
      7'b0000001: n1733_o = 1'b0;
      default: n1733_o = 1'b0;
    endcase
  /* T65_MCode.vhd:498:9  */
  always @*
    case (n1713_o)
      7'b1000000: n1736_o = 1'b0;
      7'b0100000: n1736_o = 1'b1;
      7'b0010000: n1736_o = 1'b0;
      7'b0001000: n1736_o = 1'b0;
      7'b0000100: n1736_o = 1'b0;
      7'b0000010: n1736_o = 1'b0;
      7'b0000001: n1736_o = 1'b0;
      default: n1736_o = 1'b0;
    endcase
  /* T65_MCode.vhd:498:9  */
  always @*
    case (n1713_o)
      7'b1000000: n1739_o = 1'b0;
      7'b0100000: n1739_o = 1'b0;
      7'b0010000: n1739_o = 1'b0;
      7'b0001000: n1739_o = 1'b0;
      7'b0000100: n1739_o = 1'b0;
      7'b0000010: n1739_o = 1'b0;
      7'b0000001: n1739_o = 1'b1;
      default: n1739_o = 1'b0;
    endcase
  /* T65_MCode.vhd:498:9  */
  always @*
    case (n1713_o)
      7'b1000000: n1742_o = 1'b0;
      7'b0100000: n1742_o = 1'b0;
      7'b0010000: n1742_o = 1'b0;
      7'b0001000: n1742_o = 1'b0;
      7'b0000100: n1742_o = 1'b1;
      7'b0000010: n1742_o = 1'b0;
      7'b0000001: n1742_o = 1'b0;
      default: n1742_o = 1'b0;
    endcase
  /* T65_MCode.vhd:498:9  */
  always @*
    case (n1713_o)
      7'b1000000: n1745_o = 1'b0;
      7'b0100000: n1745_o = 1'b0;
      7'b0010000: n1745_o = 1'b0;
      7'b0001000: n1745_o = 1'b1;
      7'b0000100: n1745_o = 1'b0;
      7'b0000010: n1745_o = 1'b0;
      7'b0000001: n1745_o = 1'b0;
      default: n1745_o = 1'b0;
    endcase
  /* T65_MCode.vhd:498:9  */
  always @*
    case (n1713_o)
      7'b1000000: n1748_o = 1'b0;
      7'b0100000: n1748_o = 1'b1;
      7'b0010000: n1748_o = 1'b0;
      7'b0001000: n1748_o = 1'b0;
      7'b0000100: n1748_o = 1'b0;
      7'b0000010: n1748_o = 1'b0;
      7'b0000001: n1748_o = 1'b0;
      default: n1748_o = 1'b0;
    endcase
  /* T65_MCode.vhd:498:9  */
  always @*
    case (n1713_o)
      7'b1000000: n1751_o = 1'b0;
      7'b0100000: n1751_o = 1'b1;
      7'b0010000: n1751_o = n1706_o;
      7'b0001000: n1751_o = n1687_o;
      7'b0000100: n1751_o = 1'b0;
      7'b0000010: n1751_o = 1'b0;
      7'b0000001: n1751_o = 1'b0;
      default: n1751_o = 1'b0;
    endcase
  /* T65_MCode.vhd:498:9  */
  always @*
    case (n1713_o)
      7'b1000000: n1754_o = 1'b1;
      7'b0100000: n1754_o = 1'b0;
      7'b0010000: n1754_o = 1'b0;
      7'b0001000: n1754_o = 1'b0;
      7'b0000100: n1754_o = 1'b0;
      7'b0000010: n1754_o = 1'b0;
      7'b0000001: n1754_o = 1'b0;
      default: n1754_o = 1'b0;
    endcase
  /* T65_MCode.vhd:489:7  */
  assign n1756_o = n1201_o == 5'b00001;
  /* T65_MCode.vhd:489:20  */
  assign n1758_o = n1201_o == 5'b00011;
  /* T65_MCode.vhd:489:20  */
  assign n1759_o = n1756_o | n1758_o;
  /* T65_MCode.vhd:536:14  */
  assign n1760_o = ir[7:5]; // extract
  /* T65_MCode.vhd:536:26  */
  assign n1762_o = n1760_o != 3'b100;
  /* T65_MCode.vhd:536:9  */
  assign n1764_o = n1762_o ? 1'b1 : n1173_o;
  /* T65_MCode.vhd:540:11  */
  assign n1766_o = mcycle == 3'b001;
  /* T65_MCode.vhd:539:9  */
  always @*
    case (n1766_o)
      1'b1: n1769_o = 2'b01;
      default: n1769_o = 2'b00;
    endcase
  /* T65_MCode.vhd:534:7  */
  assign n1771_o = n1201_o == 5'b01001;
  /* T65_MCode.vhd:547:16  */
  assign n1773_o = mode == 2'b00;
  /* T65_MCode.vhd:549:18  */
  assign n1774_o = ir[7:5]; // extract
  /* T65_MCode.vhd:550:13  */
  assign n1776_o = n1774_o == 3'b010;
  /* T65_MCode.vhd:550:23  */
  assign n1778_o = n1774_o == 3'b011;
  /* T65_MCode.vhd:550:23  */
  assign n1779_o = n1776_o | n1778_o;
  /* T65_MCode.vhd:550:29  */
  assign n1781_o = n1774_o == 3'b000;
  /* T65_MCode.vhd:550:29  */
  assign n1782_o = n1779_o | n1781_o;
  /* T65_MCode.vhd:550:35  */
  assign n1784_o = n1774_o == 3'b001;
  /* T65_MCode.vhd:550:35  */
  assign n1785_o = n1782_o | n1784_o;
  /* T65_MCode.vhd:553:13  */
  assign n1787_o = n1774_o == 3'b100;
  /* T65_MCode.vhd:556:13  */
  assign n1789_o = n1774_o == 3'b110;
  /* T65_MCode.vhd:560:15  */
  assign n1792_o = bcd_en ? 4'b0111 : 4'b0000;
  /* T65_MCode.vhd:559:13  */
  assign n1794_o = n1774_o == 3'b101;
  assign n1795_o = {n1794_o, n1789_o, n1787_o, n1785_o};
  /* T65_MCode.vhd:549:11  */
  always @*
    case (n1795_o)
      4'b1000: n1799_o = n1792_o;
      4'b0100: n1799_o = 4'b1001;
      4'b0010: n1799_o = 4'b1000;
      4'b0001: n1799_o = 4'b0110;
      default: n1799_o = n1200_o;
    endcase
  /* T65_MCode.vhd:549:11  */
  always @*
    case (n1795_o)
      4'b1000: n1804_o = 1'b1;
      4'b0100: n1804_o = n1173_o;
      4'b0010: n1804_o = 1'b1;
      4'b0001: n1804_o = 1'b1;
      default: n1804_o = 1'b1;
    endcase
  /* T65_MCode.vhd:549:11  */
  always @*
    case (n1795_o)
      4'b1000: n1806_o = n1176_o;
      4'b0100: n1806_o = 1'b1;
      4'b0010: n1806_o = n1176_o;
      4'b0001: n1806_o = n1176_o;
      default: n1806_o = n1176_o;
    endcase
  /* T65_MCode.vhd:570:13  */
  assign n1808_o = mcycle == 3'b001;
  /* T65_MCode.vhd:569:11  */
  always @*
    case (n1808_o)
      1'b1: n1811_o = 2'b01;
      default: n1811_o = 2'b00;
    endcase
  /* T65_MCode.vhd:547:9  */
  assign n1812_o = n1773_o ? n1799_o : n1200_o;
  /* T65_MCode.vhd:547:9  */
  assign n1814_o = n1773_o ? n1811_o : 2'b00;
  /* T65_MCode.vhd:547:9  */
  assign n1815_o = n1773_o ? n1804_o : n1173_o;
  /* T65_MCode.vhd:547:9  */
  assign n1816_o = n1773_o ? n1806_o : n1176_o;
  /* T65_MCode.vhd:546:7  */
  assign n1818_o = n1201_o == 5'b01011;
  /* T65_MCode.vhd:581:11  */
  assign n1820_o = mcycle == 3'b000;
  /* T65_MCode.vhd:583:19  */
  assign n1822_o = ir == 8'b10100010;
  /* T65_MCode.vhd:587:21  */
  assign n1823_o = ir[7:4]; // extract
  /* T65_MCode.vhd:587:33  */
  assign n1825_o = n1823_o == 4'b1000;
  /* T65_MCode.vhd:587:46  */
  assign n1826_o = ir[7:4]; // extract
  /* T65_MCode.vhd:587:58  */
  assign n1828_o = n1826_o == 4'b1100;
  /* T65_MCode.vhd:587:41  */
  assign n1829_o = n1825_o | n1828_o;
  /* T65_MCode.vhd:587:71  */
  assign n1830_o = ir[7:4]; // extract
  /* T65_MCode.vhd:587:83  */
  assign n1832_o = n1830_o == 4'b1110;
  /* T65_MCode.vhd:587:66  */
  assign n1833_o = n1829_o | n1832_o;
  /* T65_MCode.vhd:587:13  */
  assign n1836_o = n1833_o ? 3'b001 : 3'b000;
  /* T65_MCode.vhd:587:13  */
  assign n1839_o = n1833_o ? 2'b01 : 2'b00;
  /* T65_MCode.vhd:583:13  */
  assign n1841_o = n1822_o ? 3'b001 : n1836_o;
  /* T65_MCode.vhd:583:13  */
  assign n1843_o = n1822_o ? 2'b01 : n1839_o;
  /* T65_MCode.vhd:583:13  */
  assign n1845_o = n1822_o ? 1'b1 : n1176_o;
  /* T65_MCode.vhd:582:11  */
  assign n1847_o = mcycle == 3'b001;
  assign n1848_o = {n1847_o, n1820_o};
  /* T65_MCode.vhd:580:9  */
  always @*
    case (n1848_o)
      2'b10: n1850_o = n1841_o;
      2'b01: n1850_o = 3'b001;
      default: n1850_o = 3'b001;
    endcase
  /* T65_MCode.vhd:580:9  */
  always @*
    case (n1848_o)
      2'b10: n1852_o = n1843_o;
      2'b01: n1852_o = 2'b00;
      default: n1852_o = 2'b00;
    endcase
  /* T65_MCode.vhd:580:9  */
  always @*
    case (n1848_o)
      2'b10: n1853_o = n1845_o;
      2'b01: n1853_o = n1176_o;
      default: n1853_o = n1176_o;
    endcase
  /* T65_MCode.vhd:578:7  */
  assign n1855_o = n1201_o == 5'b00010;
  /* T65_MCode.vhd:578:20  */
  assign n1857_o = n1201_o == 5'b10010;
  /* T65_MCode.vhd:578:20  */
  assign n1858_o = n1855_o | n1857_o;
  /* T65_MCode.vhd:603:18  */
  assign n1859_o = ir[7:5]; // extract
  /* T65_MCode.vhd:603:31  */
  assign n1861_o = n1859_o == 3'b001;
  /* T65_MCode.vhd:603:13  */
  assign n1864_o = n1861_o ? 1'b1 : 1'b0;
  /* T65_MCode.vhd:602:11  */
  assign n1866_o = mcycle == 3'b000;
  /* T65_MCode.vhd:609:18  */
  assign n1867_o = ir[7:5]; // extract
  /* T65_MCode.vhd:609:31  */
  assign n1869_o = n1867_o == 3'b100;
  /* T65_MCode.vhd:609:13  */
  assign n1872_o = n1869_o ? 1'b1 : 1'b0;
  /* T65_MCode.vhd:606:11  */
  assign n1874_o = mcycle == 3'b001;
  /* T65_MCode.vhd:613:11  */
  assign n1876_o = mcycle == 3'b010;
  assign n1877_o = {n1876_o, n1874_o, n1866_o};
  /* T65_MCode.vhd:601:9  */
  always @*
    case (n1877_o)
      3'b100: n1880_o = 2'b00;
      3'b010: n1880_o = 2'b10;
      3'b001: n1880_o = 2'b00;
      default: n1880_o = 2'b00;
    endcase
  /* T65_MCode.vhd:601:9  */
  always @*
    case (n1877_o)
      3'b100: n1883_o = 2'b00;
      3'b010: n1883_o = 2'b01;
      3'b001: n1883_o = 2'b00;
      default: n1883_o = 2'b00;
    endcase
  /* T65_MCode.vhd:601:9  */
  always @*
    case (n1877_o)
      3'b100: n1886_o = 1'b0;
      3'b010: n1886_o = 1'b1;
      3'b001: n1886_o = 1'b0;
      default: n1886_o = 1'b0;
    endcase
  /* T65_MCode.vhd:601:9  */
  always @*
    case (n1877_o)
      3'b100: n1888_o = 1'b0;
      3'b010: n1888_o = 1'b0;
      3'b001: n1888_o = n1864_o;
      default: n1888_o = 1'b0;
    endcase
  /* T65_MCode.vhd:601:9  */
  always @*
    case (n1877_o)
      3'b100: n1890_o = 1'b0;
      3'b010: n1890_o = n1872_o;
      3'b001: n1890_o = 1'b0;
      default: n1890_o = 1'b0;
    endcase
  /* T65_MCode.vhd:598:7  */
  assign n1892_o = n1201_o == 5'b00100;
  /* T65_MCode.vhd:622:14  */
  assign n1893_o = ir[7:6]; // extract
  /* T65_MCode.vhd:622:27  */
  assign n1895_o = n1893_o != 2'b10;
  /* T65_MCode.vhd:622:41  */
  assign n1896_o = ir[1]; // extract
  /* T65_MCode.vhd:622:35  */
  assign n1897_o = n1896_o & n1895_o;
  /* T65_MCode.vhd:622:60  */
  assign n1899_o = mode == 2'b00;
  /* T65_MCode.vhd:622:71  */
  assign n1900_o = ir[0]; // extract
  /* T65_MCode.vhd:622:74  */
  assign n1901_o = ~n1900_o;
  /* T65_MCode.vhd:622:66  */
  assign n1902_o = n1899_o | n1901_o;
  /* T65_MCode.vhd:622:51  */
  assign n1903_o = n1902_o & n1897_o;
  /* T65_MCode.vhd:625:18  */
  assign n1905_o = mode == 2'b00;
  /* T65_MCode.vhd:625:30  */
  assign n1906_o = ir[0]; // extract
  /* T65_MCode.vhd:625:24  */
  assign n1907_o = n1906_o & n1905_o;
  /* T65_MCode.vhd:625:11  */
  assign n1909_o = n1907_o ? 1'b1 : n1173_o;
  /* T65_MCode.vhd:629:13  */
  assign n1911_o = mcycle == 3'b001;
  /* T65_MCode.vhd:635:22  */
  assign n1913_o = mode == 2'b00;
  /* T65_MCode.vhd:635:15  */
  assign n1916_o = n1913_o ? 1'b1 : 1'b0;
  /* T65_MCode.vhd:633:13  */
  assign n1918_o = mcycle == 3'b010;
  /* T65_MCode.vhd:639:13  */
  assign n1920_o = mcycle == 3'b011;
  /* T65_MCode.vhd:645:22  */
  assign n1922_o = mode == 2'b00;
  /* T65_MCode.vhd:645:34  */
  assign n1923_o = ir[0]; // extract
  /* T65_MCode.vhd:645:28  */
  assign n1924_o = n1923_o & n1922_o;
  /* T65_MCode.vhd:645:15  */
  assign n1926_o = n1924_o ? 4'b0001 : n1200_o;
  /* T65_MCode.vhd:645:15  */
  assign n1929_o = n1924_o ? 1'b1 : 1'b0;
  /* T65_MCode.vhd:645:15  */
  assign n1932_o = n1924_o ? 1'b1 : 1'b0;
  /* T65_MCode.vhd:644:13  */
  assign n1934_o = mcycle == 3'b100;
  assign n1935_o = {n1934_o, n1920_o, n1918_o, n1911_o};
  /* T65_MCode.vhd:628:11  */
  always @*
    case (n1935_o)
      4'b1000: n1936_o = n1926_o;
      4'b0100: n1936_o = n1200_o;
      4'b0010: n1936_o = n1200_o;
      4'b0001: n1936_o = n1200_o;
      default: n1936_o = n1200_o;
    endcase
  /* T65_MCode.vhd:628:11  */
  always @*
    case (n1935_o)
      4'b1000: n1941_o = 2'b00;
      4'b0100: n1941_o = 2'b10;
      4'b0010: n1941_o = 2'b10;
      4'b0001: n1941_o = 2'b10;
      default: n1941_o = 2'b00;
    endcase
  /* T65_MCode.vhd:628:11  */
  always @*
    case (n1935_o)
      4'b1000: n1944_o = 2'b00;
      4'b0100: n1944_o = 2'b00;
      4'b0010: n1944_o = 2'b00;
      4'b0001: n1944_o = 2'b01;
      default: n1944_o = 2'b00;
    endcase
  /* T65_MCode.vhd:628:11  */
  always @*
    case (n1935_o)
      4'b1000: n1947_o = n1929_o;
      4'b0100: n1947_o = 1'b0;
      4'b0010: n1947_o = 1'b1;
      4'b0001: n1947_o = 1'b0;
      default: n1947_o = 1'b0;
    endcase
  /* T65_MCode.vhd:628:11  */
  always @*
    case (n1935_o)
      4'b1000: n1950_o = 1'b0;
      4'b0100: n1950_o = 1'b1;
      4'b0010: n1950_o = 1'b0;
      4'b0001: n1950_o = 1'b0;
      default: n1950_o = 1'b0;
    endcase
  /* T65_MCode.vhd:628:11  */
  always @*
    case (n1935_o)
      4'b1000: n1953_o = 1'b0;
      4'b0100: n1953_o = 1'b0;
      4'b0010: n1953_o = 1'b0;
      4'b0001: n1953_o = 1'b1;
      default: n1953_o = 1'b0;
    endcase
  /* T65_MCode.vhd:628:11  */
  always @*
    case (n1935_o)
      4'b1000: n1956_o = 1'b0;
      4'b0100: n1956_o = 1'b1;
      4'b0010: n1956_o = 1'b0;
      4'b0001: n1956_o = 1'b0;
      default: n1956_o = 1'b0;
    endcase
  /* T65_MCode.vhd:628:11  */
  always @*
    case (n1935_o)
      4'b1000: n1959_o = 1'b0;
      4'b0100: n1959_o = 1'b1;
      4'b0010: n1959_o = n1916_o;
      4'b0001: n1959_o = 1'b0;
      default: n1959_o = 1'b0;
    endcase
  /* T65_MCode.vhd:628:11  */
  always @*
    case (n1935_o)
      4'b1000: n1961_o = n1932_o;
      4'b0100: n1961_o = 1'b0;
      4'b0010: n1961_o = 1'b0;
      4'b0001: n1961_o = 1'b0;
      default: n1961_o = 1'b0;
    endcase
  /* T65_MCode.vhd:654:16  */
  assign n1962_o = ir[7:6]; // extract
  /* T65_MCode.vhd:654:29  */
  assign n1964_o = n1962_o != 2'b10;
  /* T65_MCode.vhd:654:11  */
  assign n1966_o = n1964_o ? 1'b1 : n1173_o;
  /* T65_MCode.vhd:658:13  */
  assign n1968_o = mcycle == 3'b000;
  /* T65_MCode.vhd:662:20  */
  assign n1969_o = ir[7:5]; // extract
  /* T65_MCode.vhd:662:33  */
  assign n1971_o = n1969_o == 3'b100;
  /* T65_MCode.vhd:662:15  */
  assign n1974_o = n1971_o ? 1'b1 : 1'b0;
  /* T65_MCode.vhd:659:13  */
  assign n1976_o = mcycle == 3'b001;
  /* T65_MCode.vhd:666:13  */
  assign n1978_o = mcycle == 3'b010;
  assign n1979_o = {n1978_o, n1976_o, n1968_o};
  /* T65_MCode.vhd:657:11  */
  always @*
    case (n1979_o)
      3'b100: n1982_o = 2'b00;
      3'b010: n1982_o = 2'b10;
      3'b001: n1982_o = 2'b00;
      default: n1982_o = 2'b00;
    endcase
  /* T65_MCode.vhd:657:11  */
  always @*
    case (n1979_o)
      3'b100: n1985_o = 2'b00;
      3'b010: n1985_o = 2'b01;
      3'b001: n1985_o = 2'b00;
      default: n1985_o = 2'b00;
    endcase
  /* T65_MCode.vhd:657:11  */
  always @*
    case (n1979_o)
      3'b100: n1988_o = 1'b0;
      3'b010: n1988_o = 1'b1;
      3'b001: n1988_o = 1'b0;
      default: n1988_o = 1'b0;
    endcase
  /* T65_MCode.vhd:657:11  */
  always @*
    case (n1979_o)
      3'b100: n1990_o = 1'b0;
      3'b010: n1990_o = n1974_o;
      3'b001: n1990_o = 1'b0;
      default: n1990_o = 1'b0;
    endcase
  /* T65_MCode.vhd:622:9  */
  assign n1993_o = n1903_o ? 3'b100 : 3'b010;
  /* T65_MCode.vhd:622:9  */
  assign n1994_o = n1903_o ? n1936_o : n1200_o;
  /* T65_MCode.vhd:622:9  */
  assign n1995_o = n1903_o ? n1941_o : n1982_o;
  /* T65_MCode.vhd:622:9  */
  assign n1996_o = n1903_o ? n1944_o : n1985_o;
  /* T65_MCode.vhd:622:9  */
  assign n1997_o = n1903_o ? n1909_o : n1966_o;
  /* T65_MCode.vhd:622:9  */
  assign n1999_o = n1903_o ? n1947_o : 1'b0;
  /* T65_MCode.vhd:622:9  */
  assign n2001_o = n1903_o ? n1950_o : 1'b0;
  /* T65_MCode.vhd:622:9  */
  assign n2002_o = n1903_o ? n1953_o : n1988_o;
  /* T65_MCode.vhd:622:9  */
  assign n2004_o = n1903_o ? n1956_o : 1'b0;
  /* T65_MCode.vhd:622:9  */
  assign n2005_o = n1903_o ? n1959_o : n1990_o;
  /* T65_MCode.vhd:622:9  */
  assign n2007_o = n1903_o ? n1961_o : 1'b0;
  /* T65_MCode.vhd:620:7  */
  assign n2009_o = n1201_o == 5'b00101;
  /* T65_MCode.vhd:620:20  */
  assign n2011_o = n1201_o == 5'b00110;
  /* T65_MCode.vhd:620:20  */
  assign n2012_o = n2009_o | n2011_o;
  /* T65_MCode.vhd:620:30  */
  assign n2014_o = n1201_o == 5'b00111;
  /* T65_MCode.vhd:620:30  */
  assign n2015_o = n2012_o | n2014_o;
  /* T65_MCode.vhd:674:14  */
  assign n2016_o = ir[7:6]; // extract
  /* T65_MCode.vhd:674:27  */
  assign n2018_o = n2016_o == 2'b01;
  /* T65_MCode.vhd:674:40  */
  assign n2019_o = ir[4:0]; // extract
  /* T65_MCode.vhd:674:53  */
  assign n2021_o = n2019_o == 5'b01100;
  /* T65_MCode.vhd:674:34  */
  assign n2022_o = n2021_o & n2018_o;
  /* T65_MCode.vhd:675:16  */
  assign n2023_o = ir[5]; // extract
  /* T65_MCode.vhd:675:20  */
  assign n2024_o = ~n2023_o;
  /* T65_MCode.vhd:678:15  */
  assign n2026_o = mcycle == 3'b001;
  /* T65_MCode.vhd:681:15  */
  assign n2028_o = mcycle == 3'b010;
  assign n2029_o = {n2028_o, n2026_o};
  /* T65_MCode.vhd:677:13  */
  always @*
    case (n2029_o)
      2'b10: n2033_o = 2'b10;
      2'b01: n2033_o = 2'b01;
      default: n2033_o = 2'b00;
    endcase
  /* T65_MCode.vhd:677:13  */
  always @*
    case (n2029_o)
      2'b10: n2036_o = 1'b0;
      2'b01: n2036_o = 1'b1;
      default: n2036_o = 1'b0;
    endcase
  /* T65_MCode.vhd:688:15  */
  assign n2038_o = mcycle == 3'b001;
  /* T65_MCode.vhd:694:25  */
  assign n2040_o = mode != 2'b00;
  /* T65_MCode.vhd:694:17  */
  assign n2043_o = n2040_o ? 2'b10 : 2'b00;
  /* T65_MCode.vhd:697:25  */
  assign n2045_o = mode == 2'b00;
  /* T65_MCode.vhd:697:17  */
  assign n2048_o = n2045_o ? 2'b11 : 2'b00;
  /* T65_MCode.vhd:692:15  */
  assign n2050_o = mcycle == 3'b010;
  /* T65_MCode.vhd:702:25  */
  assign n2052_o = mode == 2'b00;
  /* T65_MCode.vhd:702:17  */
  assign n2055_o = n2052_o ? 2'b11 : 2'b00;
  /* T65_MCode.vhd:702:17  */
  assign n2058_o = n2052_o ? 2'b00 : 2'b01;
  /* T65_MCode.vhd:702:17  */
  assign n2061_o = n2052_o ? 2'b01 : 2'b00;
  /* T65_MCode.vhd:700:15  */
  assign n2063_o = mcycle == 3'b011;
  /* T65_MCode.vhd:708:15  */
  assign n2065_o = mcycle == 3'b100;
  assign n2066_o = {n2065_o, n2063_o, n2050_o, n2038_o};
  /* T65_MCode.vhd:687:13  */
  always @*
    case (n2066_o)
      4'b1000: n2068_o = 2'b00;
      4'b0100: n2068_o = n2055_o;
      4'b0010: n2068_o = n2048_o;
      4'b0001: n2068_o = 2'b00;
      default: n2068_o = 2'b00;
    endcase
  /* T65_MCode.vhd:687:13  */
  always @*
    case (n2066_o)
      4'b1000: n2072_o = 2'b10;
      4'b0100: n2072_o = n2058_o;
      4'b0010: n2072_o = n2043_o;
      4'b0001: n2072_o = 2'b01;
      default: n2072_o = 2'b00;
    endcase
  /* T65_MCode.vhd:687:13  */
  always @*
    case (n2066_o)
      4'b1000: n2074_o = 2'b00;
      4'b0100: n2074_o = n2061_o;
      4'b0010: n2074_o = 2'b00;
      4'b0001: n2074_o = 2'b00;
      default: n2074_o = 2'b00;
    endcase
  /* T65_MCode.vhd:687:13  */
  always @*
    case (n2066_o)
      4'b1000: n2078_o = 1'b0;
      4'b0100: n2078_o = 1'b1;
      4'b0010: n2078_o = 1'b0;
      4'b0001: n2078_o = 1'b1;
      default: n2078_o = 1'b0;
    endcase
  /* T65_MCode.vhd:687:13  */
  always @*
    case (n2066_o)
      4'b1000: n2081_o = 1'b0;
      4'b0100: n2081_o = 1'b0;
      4'b0010: n2081_o = 1'b0;
      4'b0001: n2081_o = 1'b1;
      default: n2081_o = 1'b0;
    endcase
  /* T65_MCode.vhd:687:13  */
  always @*
    case (n2066_o)
      4'b1000: n2084_o = 1'b0;
      4'b0100: n2084_o = 1'b0;
      4'b0010: n2084_o = 1'b1;
      4'b0001: n2084_o = 1'b0;
      default: n2084_o = 1'b0;
    endcase
  /* T65_MCode.vhd:675:11  */
  assign n2087_o = n2024_o ? 3'b010 : 3'b100;
  /* T65_MCode.vhd:675:11  */
  assign n2089_o = n2024_o ? 2'b00 : n2068_o;
  /* T65_MCode.vhd:675:11  */
  assign n2090_o = n2024_o ? n2033_o : n2072_o;
  /* T65_MCode.vhd:675:11  */
  assign n2092_o = n2024_o ? 2'b00 : n2074_o;
  /* T65_MCode.vhd:675:11  */
  assign n2093_o = n2024_o ? n2036_o : n2078_o;
  /* T65_MCode.vhd:675:11  */
  assign n2095_o = n2024_o ? 1'b0 : n2081_o;
  /* T65_MCode.vhd:675:11  */
  assign n2097_o = n2024_o ? 1'b0 : n2084_o;
  /* T65_MCode.vhd:717:20  */
  assign n2098_o = ir[7:5]; // extract
  /* T65_MCode.vhd:717:33  */
  assign n2100_o = n2098_o == 3'b001;
  /* T65_MCode.vhd:717:15  */
  assign n2103_o = n2100_o ? 1'b1 : 1'b0;
  /* T65_MCode.vhd:716:13  */
  assign n2105_o = mcycle == 3'b000;
  /* T65_MCode.vhd:720:13  */
  assign n2107_o = mcycle == 3'b001;
  /* T65_MCode.vhd:726:20  */
  assign n2108_o = ir[7:5]; // extract
  /* T65_MCode.vhd:726:33  */
  assign n2110_o = n2108_o == 3'b100;
  /* T65_MCode.vhd:726:15  */
  assign n2113_o = n2110_o ? 1'b1 : 1'b0;
  /* T65_MCode.vhd:723:13  */
  assign n2115_o = mcycle == 3'b010;
  /* T65_MCode.vhd:730:13  */
  assign n2117_o = mcycle == 3'b011;
  assign n2118_o = {n2117_o, n2115_o, n2107_o, n2105_o};
  /* T65_MCode.vhd:715:11  */
  always @*
    case (n2118_o)
      4'b1000: n2121_o = 2'b00;
      4'b0100: n2121_o = 2'b11;
      4'b0010: n2121_o = 2'b00;
      4'b0001: n2121_o = 2'b00;
      default: n2121_o = 2'b00;
    endcase
  /* T65_MCode.vhd:715:11  */
  always @*
    case (n2118_o)
      4'b1000: n2125_o = 2'b00;
      4'b0100: n2125_o = 2'b01;
      4'b0010: n2125_o = 2'b01;
      4'b0001: n2125_o = 2'b00;
      default: n2125_o = 2'b00;
    endcase
  /* T65_MCode.vhd:715:11  */
  always @*
    case (n2118_o)
      4'b1000: n2128_o = 1'b0;
      4'b0100: n2128_o = 1'b0;
      4'b0010: n2128_o = 1'b1;
      4'b0001: n2128_o = 1'b0;
      default: n2128_o = 1'b0;
    endcase
  /* T65_MCode.vhd:715:11  */
  always @*
    case (n2118_o)
      4'b1000: n2131_o = 1'b0;
      4'b0100: n2131_o = 1'b1;
      4'b0010: n2131_o = 1'b0;
      4'b0001: n2131_o = 1'b0;
      default: n2131_o = 1'b0;
    endcase
  /* T65_MCode.vhd:715:11  */
  always @*
    case (n2118_o)
      4'b1000: n2133_o = 1'b0;
      4'b0100: n2133_o = 1'b0;
      4'b0010: n2133_o = 1'b0;
      4'b0001: n2133_o = n2103_o;
      default: n2133_o = 1'b0;
    endcase
  /* T65_MCode.vhd:715:11  */
  always @*
    case (n2118_o)
      4'b1000: n2135_o = 1'b0;
      4'b0100: n2135_o = n2113_o;
      4'b0010: n2135_o = 1'b0;
      4'b0001: n2135_o = 1'b0;
      default: n2135_o = 1'b0;
    endcase
  /* T65_MCode.vhd:674:9  */
  assign n2137_o = n2022_o ? n2087_o : 3'b011;
  /* T65_MCode.vhd:674:9  */
  assign n2138_o = n2022_o ? n2089_o : n2121_o;
  /* T65_MCode.vhd:674:9  */
  assign n2139_o = n2022_o ? n2090_o : n2125_o;
  /* T65_MCode.vhd:674:9  */
  assign n2141_o = n2022_o ? n2092_o : 2'b00;
  /* T65_MCode.vhd:674:9  */
  assign n2143_o = n2022_o ? n2093_o : 1'b0;
  /* T65_MCode.vhd:674:9  */
  assign n2144_o = n2022_o ? n2095_o : n2128_o;
  /* T65_MCode.vhd:674:9  */
  assign n2145_o = n2022_o ? n2097_o : n2131_o;
  /* T65_MCode.vhd:674:9  */
  assign n2147_o = n2022_o ? 1'b0 : n2133_o;
  /* T65_MCode.vhd:674:9  */
  assign n2149_o = n2022_o ? 1'b0 : n2135_o;
  /* T65_MCode.vhd:672:7  */
  assign n2151_o = n1201_o == 5'b01100;
  /* T65_MCode.vhd:740:14  */
  assign n2152_o = ir[7:6]; // extract
  /* T65_MCode.vhd:740:27  */
  assign n2154_o = n2152_o != 2'b10;
  /* T65_MCode.vhd:740:41  */
  assign n2155_o = ir[1]; // extract
  /* T65_MCode.vhd:740:35  */
  assign n2156_o = n2155_o & n2154_o;
  /* T65_MCode.vhd:740:60  */
  assign n2158_o = mode == 2'b00;
  /* T65_MCode.vhd:740:71  */
  assign n2159_o = ir[0]; // extract
  /* T65_MCode.vhd:740:74  */
  assign n2160_o = ~n2159_o;
  /* T65_MCode.vhd:740:66  */
  assign n2161_o = n2158_o | n2160_o;
  /* T65_MCode.vhd:740:51  */
  assign n2162_o = n2161_o & n2156_o;
  /* T65_MCode.vhd:743:18  */
  assign n2164_o = mode == 2'b00;
  /* T65_MCode.vhd:743:30  */
  assign n2165_o = ir[0]; // extract
  /* T65_MCode.vhd:743:24  */
  assign n2166_o = n2165_o & n2164_o;
  /* T65_MCode.vhd:743:11  */
  assign n2168_o = n2166_o ? 1'b1 : n1173_o;
  /* T65_MCode.vhd:747:13  */
  assign n2170_o = mcycle == 3'b001;
  /* T65_MCode.vhd:750:13  */
  assign n2172_o = mcycle == 3'b010;
  /* T65_MCode.vhd:756:22  */
  assign n2174_o = mode == 2'b00;
  /* T65_MCode.vhd:756:15  */
  assign n2177_o = n2174_o ? 1'b1 : 1'b0;
  /* T65_MCode.vhd:754:13  */
  assign n2179_o = mcycle == 3'b011;
  /* T65_MCode.vhd:760:13  */
  assign n2181_o = mcycle == 3'b100;
  /* T65_MCode.vhd:766:22  */
  assign n2183_o = mode == 2'b00;
  /* T65_MCode.vhd:766:34  */
  assign n2184_o = ir[0]; // extract
  /* T65_MCode.vhd:766:28  */
  assign n2185_o = n2184_o & n2183_o;
  /* T65_MCode.vhd:766:15  */
  assign n2187_o = n2185_o ? 4'b0001 : n1200_o;
  /* T65_MCode.vhd:766:15  */
  assign n2190_o = n2185_o ? 1'b1 : 1'b0;
  /* T65_MCode.vhd:765:13  */
  assign n2192_o = mcycle == 3'b101;
  assign n2193_o = {n2192_o, n2181_o, n2179_o, n2172_o, n2170_o};
  /* T65_MCode.vhd:746:11  */
  always @*
    case (n2193_o)
      5'b10000: n2194_o = n2187_o;
      5'b01000: n2194_o = n1200_o;
      5'b00100: n2194_o = n1200_o;
      5'b00010: n2194_o = n1200_o;
      5'b00001: n2194_o = n1200_o;
      default: n2194_o = n1200_o;
    endcase
  /* T65_MCode.vhd:746:11  */
  always @*
    case (n2193_o)
      5'b10000: n2199_o = 2'b00;
      5'b01000: n2199_o = 2'b11;
      5'b00100: n2199_o = 2'b11;
      5'b00010: n2199_o = 2'b11;
      5'b00001: n2199_o = 2'b00;
      default: n2199_o = 2'b00;
    endcase
  /* T65_MCode.vhd:746:11  */
  always @*
    case (n2193_o)
      5'b10000: n2203_o = 2'b00;
      5'b01000: n2203_o = 2'b00;
      5'b00100: n2203_o = 2'b00;
      5'b00010: n2203_o = 2'b01;
      5'b00001: n2203_o = 2'b01;
      default: n2203_o = 2'b00;
    endcase
  /* T65_MCode.vhd:746:11  */
  always @*
    case (n2193_o)
      5'b10000: n2206_o = 1'b0;
      5'b01000: n2206_o = 1'b0;
      5'b00100: n2206_o = 1'b1;
      5'b00010: n2206_o = 1'b0;
      5'b00001: n2206_o = 1'b0;
      default: n2206_o = 1'b0;
    endcase
  /* T65_MCode.vhd:746:11  */
  always @*
    case (n2193_o)
      5'b10000: n2209_o = 1'b0;
      5'b01000: n2209_o = 1'b1;
      5'b00100: n2209_o = 1'b0;
      5'b00010: n2209_o = 1'b0;
      5'b00001: n2209_o = 1'b0;
      default: n2209_o = 1'b0;
    endcase
  /* T65_MCode.vhd:746:11  */
  always @*
    case (n2193_o)
      5'b10000: n2212_o = 1'b0;
      5'b01000: n2212_o = 1'b0;
      5'b00100: n2212_o = 1'b0;
      5'b00010: n2212_o = 1'b0;
      5'b00001: n2212_o = 1'b1;
      default: n2212_o = 1'b0;
    endcase
  /* T65_MCode.vhd:746:11  */
  always @*
    case (n2193_o)
      5'b10000: n2215_o = 1'b0;
      5'b01000: n2215_o = 1'b0;
      5'b00100: n2215_o = 1'b0;
      5'b00010: n2215_o = 1'b1;
      5'b00001: n2215_o = 1'b0;
      default: n2215_o = 1'b0;
    endcase
  /* T65_MCode.vhd:746:11  */
  always @*
    case (n2193_o)
      5'b10000: n2218_o = 1'b0;
      5'b01000: n2218_o = 1'b1;
      5'b00100: n2218_o = 1'b0;
      5'b00010: n2218_o = 1'b0;
      5'b00001: n2218_o = 1'b0;
      default: n2218_o = 1'b0;
    endcase
  /* T65_MCode.vhd:746:11  */
  always @*
    case (n2193_o)
      5'b10000: n2221_o = 1'b0;
      5'b01000: n2221_o = 1'b1;
      5'b00100: n2221_o = n2177_o;
      5'b00010: n2221_o = 1'b0;
      5'b00001: n2221_o = 1'b0;
      default: n2221_o = 1'b0;
    endcase
  /* T65_MCode.vhd:746:11  */
  always @*
    case (n2193_o)
      5'b10000: n2223_o = n2190_o;
      5'b01000: n2223_o = 1'b0;
      5'b00100: n2223_o = 1'b0;
      5'b00010: n2223_o = 1'b0;
      5'b00001: n2223_o = 1'b0;
      default: n2223_o = 1'b0;
    endcase
  /* T65_MCode.vhd:774:16  */
  assign n2224_o = ir[7:6]; // extract
  /* T65_MCode.vhd:774:29  */
  assign n2226_o = n2224_o != 2'b10;
  /* T65_MCode.vhd:774:11  */
  assign n2228_o = n2226_o ? 1'b1 : n1173_o;
  /* T65_MCode.vhd:778:13  */
  assign n2230_o = mcycle == 3'b000;
  /* T65_MCode.vhd:779:13  */
  assign n2232_o = mcycle == 3'b001;
  /* T65_MCode.vhd:785:20  */
  assign n2233_o = ir[7:5]; // extract
  /* T65_MCode.vhd:785:33  */
  assign n2235_o = n2233_o == 3'b100;
  /* T65_MCode.vhd:785:15  */
  assign n2238_o = n2235_o ? 1'b1 : 1'b0;
  /* T65_MCode.vhd:782:13  */
  assign n2240_o = mcycle == 3'b010;
  /* T65_MCode.vhd:789:13  */
  assign n2242_o = mcycle == 3'b011;
  assign n2243_o = {n2242_o, n2240_o, n2232_o, n2230_o};
  /* T65_MCode.vhd:777:11  */
  always @*
    case (n2243_o)
      4'b1000: n2246_o = 2'b00;
      4'b0100: n2246_o = 2'b11;
      4'b0010: n2246_o = 2'b00;
      4'b0001: n2246_o = 2'b00;
      default: n2246_o = 2'b00;
    endcase
  /* T65_MCode.vhd:777:11  */
  always @*
    case (n2243_o)
      4'b1000: n2250_o = 2'b00;
      4'b0100: n2250_o = 2'b01;
      4'b0010: n2250_o = 2'b01;
      4'b0001: n2250_o = 2'b00;
      default: n2250_o = 2'b00;
    endcase
  /* T65_MCode.vhd:777:11  */
  always @*
    case (n2243_o)
      4'b1000: n2253_o = 1'b0;
      4'b0100: n2253_o = 1'b0;
      4'b0010: n2253_o = 1'b1;
      4'b0001: n2253_o = 1'b0;
      default: n2253_o = 1'b0;
    endcase
  /* T65_MCode.vhd:777:11  */
  always @*
    case (n2243_o)
      4'b1000: n2256_o = 1'b0;
      4'b0100: n2256_o = 1'b1;
      4'b0010: n2256_o = 1'b0;
      4'b0001: n2256_o = 1'b0;
      default: n2256_o = 1'b0;
    endcase
  /* T65_MCode.vhd:777:11  */
  always @*
    case (n2243_o)
      4'b1000: n2258_o = 1'b0;
      4'b0100: n2258_o = n2238_o;
      4'b0010: n2258_o = 1'b0;
      4'b0001: n2258_o = 1'b0;
      default: n2258_o = 1'b0;
    endcase
  /* T65_MCode.vhd:740:9  */
  assign n2261_o = n2162_o ? 3'b101 : 3'b011;
  /* T65_MCode.vhd:740:9  */
  assign n2262_o = n2162_o ? n2194_o : n1200_o;
  /* T65_MCode.vhd:740:9  */
  assign n2263_o = n2162_o ? n2199_o : n2246_o;
  /* T65_MCode.vhd:740:9  */
  assign n2264_o = n2162_o ? n2203_o : n2250_o;
  /* T65_MCode.vhd:740:9  */
  assign n2265_o = n2162_o ? n2168_o : n2228_o;
  /* T65_MCode.vhd:740:9  */
  assign n2267_o = n2162_o ? n2206_o : 1'b0;
  /* T65_MCode.vhd:740:9  */
  assign n2269_o = n2162_o ? n2209_o : 1'b0;
  /* T65_MCode.vhd:740:9  */
  assign n2270_o = n2162_o ? n2212_o : n2253_o;
  /* T65_MCode.vhd:740:9  */
  assign n2271_o = n2162_o ? n2215_o : n2256_o;
  /* T65_MCode.vhd:740:9  */
  assign n2273_o = n2162_o ? n2218_o : 1'b0;
  /* T65_MCode.vhd:740:9  */
  assign n2274_o = n2162_o ? n2221_o : n2258_o;
  /* T65_MCode.vhd:740:9  */
  assign n2276_o = n2162_o ? n2223_o : 1'b0;
  /* T65_MCode.vhd:738:7  */
  assign n2278_o = n1201_o == 5'b01101;
  /* T65_MCode.vhd:738:20  */
  assign n2280_o = n1201_o == 5'b01110;
  /* T65_MCode.vhd:738:20  */
  assign n2281_o = n2278_o | n2280_o;
  /* T65_MCode.vhd:738:30  */
  assign n2283_o = n1201_o == 5'b01111;
  /* T65_MCode.vhd:738:30  */
  assign n2284_o = n2281_o | n2283_o;
  /* T65_MCode.vhd:801:9  */
  assign n2287_o = branch ? 3'b011 : 3'b001;
  /* T65_MCode.vhd:815:11  */
  assign n2289_o = mcycle == 3'b001;
  /* T65_MCode.vhd:826:11  */
  assign n2291_o = mcycle == 3'b010;
  /* T65_MCode.vhd:835:11  */
  assign n2293_o = mcycle == 3'b011;
  assign n2294_o = {n2293_o, n2291_o, n2289_o};
  /* T65_MCode.vhd:810:9  */
  always @*
    case (n2294_o)
      3'b100: n2298_o = 2'b00;
      3'b010: n2298_o = 2'b11;
      3'b001: n2298_o = 2'b01;
      default: n2298_o = 2'b00;
    endcase
  /* T65_MCode.vhd:810:9  */
  always @*
    case (n2294_o)
      3'b100: n2301_o = 1'b0;
      3'b010: n2301_o = 1'b1;
      3'b001: n2301_o = 1'b0;
      default: n2301_o = 1'b0;
    endcase
  /* T65_MCode.vhd:810:9  */
  always @*
    case (n2294_o)
      3'b100: n2304_o = 1'b0;
      3'b010: n2304_o = 1'b0;
      3'b001: n2304_o = 1'b1;
      default: n2304_o = 1'b0;
    endcase
  /* T65_MCode.vhd:795:7  */
  assign n2306_o = n1201_o == 5'b10000;
  /* T65_MCode.vhd:843:14  */
  assign n2307_o = ir[7:6]; // extract
  /* T65_MCode.vhd:843:27  */
  assign n2309_o = n2307_o != 2'b10;
  /* T65_MCode.vhd:845:18  */
  assign n2311_o = mode == 2'b00;
  /* T65_MCode.vhd:845:30  */
  assign n2312_o = ir[1]; // extract
  /* T65_MCode.vhd:845:24  */
  assign n2313_o = n2312_o & n2311_o;
  /* T65_MCode.vhd:845:11  */
  assign n2316_o = n2313_o ? 3'b111 : 3'b101;
  /* T65_MCode.vhd:843:9  */
  assign n2318_o = n2309_o ? n2316_o : 3'b101;
  /* T65_MCode.vhd:843:9  */
  assign n2321_o = n2309_o ? 1'b1 : n1173_o;
  /* T65_MCode.vhd:850:11  */
  assign n2323_o = mcycle == 3'b001;
  /* T65_MCode.vhd:854:11  */
  assign n2325_o = mcycle == 3'b010;
  /* T65_MCode.vhd:858:11  */
  assign n2327_o = mcycle == 3'b011;
  /* T65_MCode.vhd:865:18  */
  assign n2328_o = ir[7:5]; // extract
  /* T65_MCode.vhd:865:31  */
  assign n2330_o = n2328_o == 3'b100;
  /* T65_MCode.vhd:867:20  */
  assign n2331_o = ir[3:0]; // extract
  /* T65_MCode.vhd:867:33  */
  assign n2333_o = n2331_o == 4'b0011;
  /* T65_MCode.vhd:867:15  */
  assign n2336_o = n2333_o ? 2'b10 : 2'b00;
  /* T65_MCode.vhd:870:21  */
  assign n2337_o = ir[1]; // extract
  /* T65_MCode.vhd:870:24  */
  assign n2338_o = ~n2337_o;
  /* T65_MCode.vhd:870:34  */
  assign n2340_o = ir == 8'b10110011;
  /* T65_MCode.vhd:870:29  */
  assign n2341_o = n2338_o | n2340_o;
  /* T65_MCode.vhd:870:13  */
  assign n2344_o = n2341_o ? 1'b1 : 1'b0;
  /* T65_MCode.vhd:865:13  */
  assign n2346_o = n2330_o ? n2336_o : 2'b00;
  /* T65_MCode.vhd:865:13  */
  assign n2348_o = n2330_o ? 1'b0 : n2344_o;
  /* T65_MCode.vhd:865:13  */
  assign n2351_o = n2330_o ? 1'b1 : 1'b0;
  /* T65_MCode.vhd:863:11  */
  assign n2353_o = mcycle == 3'b100;
  /* T65_MCode.vhd:875:20  */
  assign n2355_o = mode == 2'b00;
  /* T65_MCode.vhd:875:32  */
  assign n2356_o = ir[1]; // extract
  /* T65_MCode.vhd:875:26  */
  assign n2357_o = n2356_o & n2355_o;
  /* T65_MCode.vhd:875:46  */
  assign n2358_o = ir[7:6]; // extract
  /* T65_MCode.vhd:875:58  */
  assign n2360_o = n2358_o != 2'b10;
  /* T65_MCode.vhd:875:40  */
  assign n2361_o = n2360_o & n2357_o;
  /* T65_MCode.vhd:875:13  */
  assign n2364_o = n2361_o ? 2'b11 : 2'b00;
  /* T65_MCode.vhd:875:13  */
  assign n2367_o = n2361_o ? 1'b1 : 1'b0;
  /* T65_MCode.vhd:875:13  */
  assign n2370_o = n2361_o ? 1'b1 : 1'b0;
  /* T65_MCode.vhd:874:11  */
  assign n2372_o = mcycle == 3'b101;
  /* T65_MCode.vhd:880:11  */
  assign n2374_o = mcycle == 3'b110;
  /* T65_MCode.vhd:885:11  */
  assign n2376_o = mcycle == 3'b111;
  assign n2377_o = {n2376_o, n2374_o, n2372_o, n2353_o, n2327_o, n2325_o, n2323_o};
  /* T65_MCode.vhd:849:9  */
  always @*
    case (n2377_o)
      7'b1000000: n2380_o = 4'b0001;
      7'b0100000: n2380_o = n1200_o;
      7'b0010000: n2380_o = n1200_o;
      7'b0001000: n2380_o = n1200_o;
      7'b0000100: n2380_o = 4'b0011;
      7'b0000010: n2380_o = n1200_o;
      7'b0000001: n2380_o = n1200_o;
      default: n2380_o = n1200_o;
    endcase
  /* T65_MCode.vhd:849:9  */
  always @*
    case (n2377_o)
      7'b1000000: n2387_o = 2'b00;
      7'b0100000: n2387_o = 2'b11;
      7'b0010000: n2387_o = n2364_o;
      7'b0001000: n2387_o = 2'b11;
      7'b0000100: n2387_o = 2'b11;
      7'b0000010: n2387_o = 2'b10;
      7'b0000001: n2387_o = 2'b10;
      default: n2387_o = 2'b00;
    endcase
  /* T65_MCode.vhd:849:9  */
  always @*
    case (n2377_o)
      7'b1000000: n2390_o = 2'b00;
      7'b0100000: n2390_o = 2'b00;
      7'b0010000: n2390_o = 2'b00;
      7'b0001000: n2390_o = 2'b00;
      7'b0000100: n2390_o = 2'b00;
      7'b0000010: n2390_o = 2'b00;
      7'b0000001: n2390_o = 2'b01;
      default: n2390_o = 2'b00;
    endcase
  /* T65_MCode.vhd:849:9  */
  always @*
    case (n2377_o)
      7'b1000000: n2395_o = 2'b00;
      7'b0100000: n2395_o = 2'b00;
      7'b0010000: n2395_o = 2'b00;
      7'b0001000: n2395_o = 2'b11;
      7'b0000100: n2395_o = 2'b10;
      7'b0000010: n2395_o = 2'b01;
      7'b0000001: n2395_o = 2'b00;
      default: n2395_o = 2'b00;
    endcase
  /* T65_MCode.vhd:849:9  */
  always @*
    case (n2377_o)
      7'b1000000: n2397_o = 2'b00;
      7'b0100000: n2397_o = 2'b00;
      7'b0010000: n2397_o = 2'b00;
      7'b0001000: n2397_o = n2346_o;
      7'b0000100: n2397_o = 2'b00;
      7'b0000010: n2397_o = 2'b00;
      7'b0000001: n2397_o = 2'b00;
      default: n2397_o = 2'b00;
    endcase
  /* T65_MCode.vhd:849:9  */
  always @*
    case (n2377_o)
      7'b1000000: n2399_o = 1'b0;
      7'b0100000: n2399_o = 1'b0;
      7'b0010000: n2399_o = 1'b0;
      7'b0001000: n2399_o = n2348_o;
      7'b0000100: n2399_o = 1'b0;
      7'b0000010: n2399_o = 1'b0;
      7'b0000001: n2399_o = 1'b0;
      default: n2399_o = 1'b0;
    endcase
  /* T65_MCode.vhd:849:9  */
  always @*
    case (n2377_o)
      7'b1000000: n2401_o = 1'b0;
      7'b0100000: n2401_o = 1'b0;
      7'b0010000: n2401_o = n2367_o;
      7'b0001000: n2401_o = 1'b0;
      7'b0000100: n2401_o = 1'b0;
      7'b0000010: n2401_o = 1'b0;
      7'b0000001: n2401_o = 1'b0;
      default: n2401_o = 1'b0;
    endcase
  /* T65_MCode.vhd:849:9  */
  always @*
    case (n2377_o)
      7'b1000000: n2404_o = 1'b0;
      7'b0100000: n2404_o = 1'b1;
      7'b0010000: n2404_o = 1'b0;
      7'b0001000: n2404_o = 1'b0;
      7'b0000100: n2404_o = 1'b0;
      7'b0000010: n2404_o = 1'b0;
      7'b0000001: n2404_o = 1'b0;
      default: n2404_o = 1'b0;
    endcase
  /* T65_MCode.vhd:849:9  */
  always @*
    case (n2377_o)
      7'b1000000: n2407_o = 1'b0;
      7'b0100000: n2407_o = 1'b0;
      7'b0010000: n2407_o = 1'b0;
      7'b0001000: n2407_o = 1'b0;
      7'b0000100: n2407_o = 1'b0;
      7'b0000010: n2407_o = 1'b0;
      7'b0000001: n2407_o = 1'b1;
      default: n2407_o = 1'b0;
    endcase
  /* T65_MCode.vhd:849:9  */
  always @*
    case (n2377_o)
      7'b1000000: n2410_o = 1'b0;
      7'b0100000: n2410_o = 1'b0;
      7'b0010000: n2410_o = 1'b0;
      7'b0001000: n2410_o = 1'b0;
      7'b0000100: n2410_o = 1'b0;
      7'b0000010: n2410_o = 1'b1;
      7'b0000001: n2410_o = 1'b0;
      default: n2410_o = 1'b0;
    endcase
  /* T65_MCode.vhd:849:9  */
  always @*
    case (n2377_o)
      7'b1000000: n2413_o = 1'b0;
      7'b0100000: n2413_o = 1'b0;
      7'b0010000: n2413_o = 1'b0;
      7'b0001000: n2413_o = 1'b0;
      7'b0000100: n2413_o = 1'b1;
      7'b0000010: n2413_o = 1'b0;
      7'b0000001: n2413_o = 1'b0;
      default: n2413_o = 1'b0;
    endcase
  /* T65_MCode.vhd:849:9  */
  always @*
    case (n2377_o)
      7'b1000000: n2416_o = 1'b0;
      7'b0100000: n2416_o = 1'b1;
      7'b0010000: n2416_o = 1'b0;
      7'b0001000: n2416_o = 1'b0;
      7'b0000100: n2416_o = 1'b0;
      7'b0000010: n2416_o = 1'b0;
      7'b0000001: n2416_o = 1'b0;
      default: n2416_o = 1'b0;
    endcase
  /* T65_MCode.vhd:849:9  */
  always @*
    case (n2377_o)
      7'b1000000: n2419_o = 1'b0;
      7'b0100000: n2419_o = 1'b1;
      7'b0010000: n2419_o = n2370_o;
      7'b0001000: n2419_o = n2351_o;
      7'b0000100: n2419_o = 1'b0;
      7'b0000010: n2419_o = 1'b0;
      7'b0000001: n2419_o = 1'b0;
      default: n2419_o = 1'b0;
    endcase
  /* T65_MCode.vhd:849:9  */
  always @*
    case (n2377_o)
      7'b1000000: n2422_o = 1'b1;
      7'b0100000: n2422_o = 1'b0;
      7'b0010000: n2422_o = 1'b0;
      7'b0001000: n2422_o = 1'b0;
      7'b0000100: n2422_o = 1'b0;
      7'b0000010: n2422_o = 1'b0;
      7'b0000001: n2422_o = 1'b0;
      default: n2422_o = 1'b0;
    endcase
  /* T65_MCode.vhd:841:7  */
  assign n2424_o = n1201_o == 5'b10001;
  /* T65_MCode.vhd:841:20  */
  assign n2426_o = n1201_o == 5'b10011;
  /* T65_MCode.vhd:841:20  */
  assign n2427_o = n2424_o | n2426_o;
  /* T65_MCode.vhd:897:14  */
  assign n2428_o = ir[7:6]; // extract
  /* T65_MCode.vhd:897:27  */
  assign n2430_o = n2428_o != 2'b10;
  /* T65_MCode.vhd:897:41  */
  assign n2431_o = ir[1]; // extract
  /* T65_MCode.vhd:897:35  */
  assign n2432_o = n2431_o & n2430_o;
  /* T65_MCode.vhd:897:60  */
  assign n2434_o = mode == 2'b00;
  /* T65_MCode.vhd:897:71  */
  assign n2435_o = ir[0]; // extract
  /* T65_MCode.vhd:897:74  */
  assign n2436_o = ~n2435_o;
  /* T65_MCode.vhd:897:66  */
  assign n2437_o = n2434_o | n2436_o;
  /* T65_MCode.vhd:897:51  */
  assign n2438_o = n2437_o & n2432_o;
  /* T65_MCode.vhd:899:18  */
  assign n2440_o = mode == 2'b00;
  /* T65_MCode.vhd:899:30  */
  assign n2441_o = ir[0]; // extract
  /* T65_MCode.vhd:899:24  */
  assign n2442_o = n2441_o & n2440_o;
  /* T65_MCode.vhd:899:11  */
  assign n2444_o = n2442_o ? 1'b1 : n1173_o;
  /* T65_MCode.vhd:904:13  */
  assign n2446_o = mcycle == 3'b001;
  /* T65_MCode.vhd:908:13  */
  assign n2448_o = mcycle == 3'b010;
  /* T65_MCode.vhd:913:22  */
  assign n2450_o = mode == 2'b00;
  /* T65_MCode.vhd:913:15  */
  assign n2453_o = n2450_o ? 1'b1 : 1'b0;
  /* T65_MCode.vhd:911:13  */
  assign n2455_o = mcycle == 3'b011;
  /* T65_MCode.vhd:922:22  */
  assign n2457_o = mode == 2'b00;
  /* T65_MCode.vhd:922:34  */
  assign n2458_o = ir[0]; // extract
  /* T65_MCode.vhd:922:28  */
  assign n2459_o = n2458_o & n2457_o;
  /* T65_MCode.vhd:922:15  */
  assign n2462_o = n2459_o ? 1'b1 : 1'b0;
  /* T65_MCode.vhd:917:13  */
  assign n2464_o = mcycle == 3'b100;
  /* T65_MCode.vhd:926:22  */
  assign n2466_o = mode == 2'b00;
  /* T65_MCode.vhd:926:34  */
  assign n2467_o = ir[0]; // extract
  /* T65_MCode.vhd:926:28  */
  assign n2468_o = n2467_o & n2466_o;
  /* T65_MCode.vhd:926:15  */
  assign n2470_o = n2468_o ? 4'b0001 : n1200_o;
  /* T65_MCode.vhd:926:15  */
  assign n2473_o = n2468_o ? 1'b1 : 1'b0;
  /* T65_MCode.vhd:925:13  */
  assign n2475_o = mcycle == 3'b101;
  assign n2476_o = {n2475_o, n2464_o, n2455_o, n2448_o, n2446_o};
  /* T65_MCode.vhd:903:11  */
  always @*
    case (n2476_o)
      5'b10000: n2477_o = n2470_o;
      5'b01000: n2477_o = n1200_o;
      5'b00100: n2477_o = n1200_o;
      5'b00010: n2477_o = n1200_o;
      5'b00001: n2477_o = n1200_o;
      default: n2477_o = n1200_o;
    endcase
  /* T65_MCode.vhd:903:11  */
  always @*
    case (n2476_o)
      5'b10000: n2483_o = 2'b00;
      5'b01000: n2483_o = 2'b10;
      5'b00100: n2483_o = 2'b10;
      5'b00010: n2483_o = 2'b10;
      5'b00001: n2483_o = 2'b10;
      default: n2483_o = 2'b00;
    endcase
  /* T65_MCode.vhd:903:11  */
  always @*
    case (n2476_o)
      5'b10000: n2486_o = 2'b00;
      5'b01000: n2486_o = 2'b00;
      5'b00100: n2486_o = 2'b00;
      5'b00010: n2486_o = 2'b00;
      5'b00001: n2486_o = 2'b01;
      default: n2486_o = 2'b00;
    endcase
  /* T65_MCode.vhd:903:11  */
  always @*
    case (n2476_o)
      5'b10000: n2489_o = 1'b0;
      5'b01000: n2489_o = 1'b0;
      5'b00100: n2489_o = 1'b0;
      5'b00010: n2489_o = 1'b1;
      5'b00001: n2489_o = 1'b0;
      default: n2489_o = 1'b0;
    endcase
  /* T65_MCode.vhd:903:11  */
  always @*
    case (n2476_o)
      5'b10000: n2492_o = 1'b0;
      5'b01000: n2492_o = n2462_o;
      5'b00100: n2492_o = 1'b1;
      5'b00010: n2492_o = 1'b0;
      5'b00001: n2492_o = 1'b0;
      default: n2492_o = 1'b0;
    endcase
  /* T65_MCode.vhd:903:11  */
  always @*
    case (n2476_o)
      5'b10000: n2495_o = 1'b0;
      5'b01000: n2495_o = 1'b1;
      5'b00100: n2495_o = 1'b0;
      5'b00010: n2495_o = 1'b0;
      5'b00001: n2495_o = 1'b0;
      default: n2495_o = 1'b0;
    endcase
  /* T65_MCode.vhd:903:11  */
  always @*
    case (n2476_o)
      5'b10000: n2498_o = 1'b0;
      5'b01000: n2498_o = 1'b0;
      5'b00100: n2498_o = 1'b0;
      5'b00010: n2498_o = 1'b0;
      5'b00001: n2498_o = 1'b1;
      default: n2498_o = 1'b0;
    endcase
  /* T65_MCode.vhd:903:11  */
  always @*
    case (n2476_o)
      5'b10000: n2501_o = 1'b0;
      5'b01000: n2501_o = 1'b1;
      5'b00100: n2501_o = 1'b0;
      5'b00010: n2501_o = 1'b0;
      5'b00001: n2501_o = 1'b0;
      default: n2501_o = 1'b0;
    endcase
  /* T65_MCode.vhd:903:11  */
  always @*
    case (n2476_o)
      5'b10000: n2504_o = 1'b0;
      5'b01000: n2504_o = 1'b1;
      5'b00100: n2504_o = n2453_o;
      5'b00010: n2504_o = 1'b0;
      5'b00001: n2504_o = 1'b0;
      default: n2504_o = 1'b0;
    endcase
  /* T65_MCode.vhd:903:11  */
  always @*
    case (n2476_o)
      5'b10000: n2506_o = n2473_o;
      5'b01000: n2506_o = 1'b0;
      5'b00100: n2506_o = 1'b0;
      5'b00010: n2506_o = 1'b0;
      5'b00001: n2506_o = 1'b0;
      default: n2506_o = 1'b0;
    endcase
  /* T65_MCode.vhd:934:16  */
  assign n2507_o = ir[7:6]; // extract
  /* T65_MCode.vhd:934:29  */
  assign n2509_o = n2507_o != 2'b10;
  /* T65_MCode.vhd:934:43  */
  assign n2510_o = ir[0]; // extract
  /* T65_MCode.vhd:934:37  */
  assign n2511_o = n2510_o & n2509_o;
  /* T65_MCode.vhd:934:11  */
  assign n2513_o = n2511_o ? 1'b1 : n1173_o;
  /* T65_MCode.vhd:938:13  */
  assign n2515_o = mcycle == 3'b000;
  /* T65_MCode.vhd:939:13  */
  assign n2517_o = mcycle == 3'b001;
  /* T65_MCode.vhd:946:21  */
  assign n2518_o = ir[3:1]; // extract
  /* T65_MCode.vhd:946:34  */
  assign n2520_o = n2518_o == 3'b011;
  /* T65_MCode.vhd:946:15  */
  assign n2523_o = n2520_o ? 1'b1 : 1'b0;
  /* T65_MCode.vhd:949:20  */
  assign n2524_o = ir[7:5]; // extract
  /* T65_MCode.vhd:949:33  */
  assign n2526_o = n2524_o == 3'b100;
  /* T65_MCode.vhd:949:15  */
  assign n2529_o = n2526_o ? 1'b1 : 1'b0;
  /* T65_MCode.vhd:943:13  */
  assign n2531_o = mcycle == 3'b010;
  /* T65_MCode.vhd:953:13  */
  assign n2533_o = mcycle == 3'b011;
  assign n2534_o = {n2533_o, n2531_o, n2517_o, n2515_o};
  /* T65_MCode.vhd:937:11  */
  always @*
    case (n2534_o)
      4'b1000: n2538_o = 2'b00;
      4'b0100: n2538_o = 2'b10;
      4'b0010: n2538_o = 2'b10;
      4'b0001: n2538_o = 2'b00;
      default: n2538_o = 2'b00;
    endcase
  /* T65_MCode.vhd:937:11  */
  always @*
    case (n2534_o)
      4'b1000: n2541_o = 2'b00;
      4'b0100: n2541_o = 2'b00;
      4'b0010: n2541_o = 2'b01;
      4'b0001: n2541_o = 2'b00;
      default: n2541_o = 2'b00;
    endcase
  /* T65_MCode.vhd:937:11  */
  always @*
    case (n2534_o)
      4'b1000: n2544_o = 1'b0;
      4'b0100: n2544_o = 1'b1;
      4'b0010: n2544_o = 1'b0;
      4'b0001: n2544_o = 1'b0;
      default: n2544_o = 1'b0;
    endcase
  /* T65_MCode.vhd:937:11  */
  always @*
    case (n2534_o)
      4'b1000: n2546_o = 1'b0;
      4'b0100: n2546_o = n2523_o;
      4'b0010: n2546_o = 1'b0;
      4'b0001: n2546_o = 1'b0;
      default: n2546_o = 1'b0;
    endcase
  /* T65_MCode.vhd:937:11  */
  always @*
    case (n2534_o)
      4'b1000: n2549_o = 1'b0;
      4'b0100: n2549_o = 1'b0;
      4'b0010: n2549_o = 1'b1;
      4'b0001: n2549_o = 1'b0;
      default: n2549_o = 1'b0;
    endcase
  /* T65_MCode.vhd:937:11  */
  always @*
    case (n2534_o)
      4'b1000: n2551_o = 1'b0;
      4'b0100: n2551_o = n2529_o;
      4'b0010: n2551_o = 1'b0;
      4'b0001: n2551_o = 1'b0;
      default: n2551_o = 1'b0;
    endcase
  /* T65_MCode.vhd:897:9  */
  assign n2554_o = n2438_o ? 3'b101 : 3'b011;
  /* T65_MCode.vhd:897:9  */
  assign n2555_o = n2438_o ? n2477_o : n1200_o;
  /* T65_MCode.vhd:897:9  */
  assign n2556_o = n2438_o ? n2483_o : n2538_o;
  /* T65_MCode.vhd:897:9  */
  assign n2557_o = n2438_o ? n2486_o : n2541_o;
  /* T65_MCode.vhd:897:9  */
  assign n2558_o = n2438_o ? n2489_o : n2544_o;
  /* T65_MCode.vhd:897:9  */
  assign n2560_o = n2438_o ? 1'b0 : n2546_o;
  /* T65_MCode.vhd:897:9  */
  assign n2561_o = n2438_o ? n2444_o : n2513_o;
  /* T65_MCode.vhd:897:9  */
  assign n2563_o = n2438_o ? n2492_o : 1'b0;
  /* T65_MCode.vhd:897:9  */
  assign n2565_o = n2438_o ? n2495_o : 1'b0;
  /* T65_MCode.vhd:897:9  */
  assign n2566_o = n2438_o ? n2498_o : n2549_o;
  /* T65_MCode.vhd:897:9  */
  assign n2568_o = n2438_o ? n2501_o : 1'b0;
  /* T65_MCode.vhd:897:9  */
  assign n2569_o = n2438_o ? n2504_o : n2551_o;
  /* T65_MCode.vhd:897:9  */
  assign n2571_o = n2438_o ? n2506_o : 1'b0;
  /* T65_MCode.vhd:895:7  */
  assign n2573_o = n1201_o == 5'b10100;
  /* T65_MCode.vhd:895:20  */
  assign n2575_o = n1201_o == 5'b10101;
  /* T65_MCode.vhd:895:20  */
  assign n2576_o = n2573_o | n2575_o;
  /* T65_MCode.vhd:895:30  */
  assign n2578_o = n1201_o == 5'b10110;
  /* T65_MCode.vhd:895:30  */
  assign n2579_o = n2576_o | n2578_o;
  /* T65_MCode.vhd:895:40  */
  assign n2581_o = n1201_o == 5'b10111;
  /* T65_MCode.vhd:895:40  */
  assign n2582_o = n2579_o | n2581_o;
  /* T65_MCode.vhd:963:14  */
  assign n2583_o = ir[7:6]; // extract
  /* T65_MCode.vhd:963:27  */
  assign n2585_o = n2583_o != 2'b10;
  /* T65_MCode.vhd:965:18  */
  assign n2587_o = mode == 2'b00;
  /* T65_MCode.vhd:965:30  */
  assign n2588_o = ir[1]; // extract
  /* T65_MCode.vhd:965:24  */
  assign n2589_o = n2588_o & n2587_o;
  /* T65_MCode.vhd:965:11  */
  assign n2592_o = n2589_o ? 3'b110 : 3'b100;
  /* T65_MCode.vhd:963:9  */
  assign n2594_o = n2585_o ? n2592_o : 3'b100;
  /* T65_MCode.vhd:963:9  */
  assign n2597_o = n2585_o ? 1'b1 : n1173_o;
  /* T65_MCode.vhd:970:11  */
  assign n2599_o = mcycle == 3'b001;
  /* T65_MCode.vhd:973:11  */
  assign n2601_o = mcycle == 3'b010;
  /* T65_MCode.vhd:981:18  */
  assign n2602_o = ir[7:5]; // extract
  /* T65_MCode.vhd:981:31  */
  assign n2604_o = n2602_o == 3'b100;
  /* T65_MCode.vhd:983:20  */
  assign n2605_o = ir[3:0]; // extract
  /* T65_MCode.vhd:983:33  */
  assign n2607_o = n2605_o == 4'b1011;
  /* T65_MCode.vhd:983:15  */
  assign n2610_o = n2607_o ? 2'b01 : 2'b00;
  /* T65_MCode.vhd:986:21  */
  assign n2611_o = ir[1]; // extract
  /* T65_MCode.vhd:986:24  */
  assign n2612_o = ~n2611_o;
  /* T65_MCode.vhd:986:34  */
  assign n2614_o = ir == 8'b10111011;
  /* T65_MCode.vhd:986:29  */
  assign n2615_o = n2612_o | n2614_o;
  /* T65_MCode.vhd:986:13  */
  assign n2618_o = n2615_o ? 1'b1 : 1'b0;
  /* T65_MCode.vhd:981:13  */
  assign n2620_o = n2604_o ? n2610_o : 2'b00;
  /* T65_MCode.vhd:981:13  */
  assign n2622_o = n2604_o ? 1'b0 : n2618_o;
  /* T65_MCode.vhd:981:13  */
  assign n2625_o = n2604_o ? 1'b1 : 1'b0;
  /* T65_MCode.vhd:979:11  */
  assign n2627_o = mcycle == 3'b011;
  /* T65_MCode.vhd:991:20  */
  assign n2629_o = mode == 2'b00;
  /* T65_MCode.vhd:991:32  */
  assign n2630_o = ir[1]; // extract
  /* T65_MCode.vhd:991:26  */
  assign n2631_o = n2630_o & n2629_o;
  /* T65_MCode.vhd:991:46  */
  assign n2632_o = ir[7:6]; // extract
  /* T65_MCode.vhd:991:58  */
  assign n2634_o = n2632_o != 2'b10;
  /* T65_MCode.vhd:991:40  */
  assign n2635_o = n2634_o & n2631_o;
  /* T65_MCode.vhd:991:13  */
  assign n2638_o = n2635_o ? 2'b11 : 2'b00;
  /* T65_MCode.vhd:991:13  */
  assign n2641_o = n2635_o ? 1'b1 : 1'b0;
  /* T65_MCode.vhd:991:13  */
  assign n2644_o = n2635_o ? 1'b1 : 1'b0;
  /* T65_MCode.vhd:990:11  */
  assign n2646_o = mcycle == 3'b100;
  /* T65_MCode.vhd:996:11  */
  assign n2648_o = mcycle == 3'b101;
  /* T65_MCode.vhd:1001:11  */
  assign n2650_o = mcycle == 3'b110;
  assign n2651_o = {n2650_o, n2648_o, n2646_o, n2627_o, n2601_o, n2599_o};
  /* T65_MCode.vhd:969:9  */
  always @*
    case (n2651_o)
      6'b100000: n2654_o = 4'b0001;
      6'b010000: n2654_o = n1200_o;
      6'b001000: n2654_o = n1200_o;
      6'b000100: n2654_o = n1200_o;
      6'b000010: n2654_o = 4'b0011;
      6'b000001: n2654_o = n1200_o;
      default: n2654_o = n1200_o;
    endcase
  /* T65_MCode.vhd:969:9  */
  always @*
    case (n2651_o)
      6'b100000: n2659_o = 2'b00;
      6'b010000: n2659_o = 2'b11;
      6'b001000: n2659_o = n2638_o;
      6'b000100: n2659_o = 2'b11;
      6'b000010: n2659_o = 2'b11;
      6'b000001: n2659_o = 2'b00;
      default: n2659_o = 2'b00;
    endcase
  /* T65_MCode.vhd:969:9  */
  always @*
    case (n2651_o)
      6'b100000: n2663_o = 2'b00;
      6'b010000: n2663_o = 2'b00;
      6'b001000: n2663_o = 2'b00;
      6'b000100: n2663_o = 2'b00;
      6'b000010: n2663_o = 2'b01;
      6'b000001: n2663_o = 2'b01;
      default: n2663_o = 2'b00;
    endcase
  /* T65_MCode.vhd:969:9  */
  always @*
    case (n2651_o)
      6'b100000: n2667_o = 2'b00;
      6'b010000: n2667_o = 2'b00;
      6'b001000: n2667_o = 2'b00;
      6'b000100: n2667_o = 2'b11;
      6'b000010: n2667_o = 2'b10;
      6'b000001: n2667_o = 2'b00;
      default: n2667_o = 2'b00;
    endcase
  /* T65_MCode.vhd:969:9  */
  always @*
    case (n2651_o)
      6'b100000: n2669_o = 2'b00;
      6'b010000: n2669_o = 2'b00;
      6'b001000: n2669_o = 2'b00;
      6'b000100: n2669_o = n2620_o;
      6'b000010: n2669_o = 2'b00;
      6'b000001: n2669_o = 2'b00;
      default: n2669_o = 2'b00;
    endcase
  /* T65_MCode.vhd:969:9  */
  always @*
    case (n2651_o)
      6'b100000: n2671_o = 1'b0;
      6'b010000: n2671_o = 1'b0;
      6'b001000: n2671_o = 1'b0;
      6'b000100: n2671_o = n2622_o;
      6'b000010: n2671_o = 1'b0;
      6'b000001: n2671_o = 1'b0;
      default: n2671_o = 1'b0;
    endcase
  /* T65_MCode.vhd:969:9  */
  always @*
    case (n2651_o)
      6'b100000: n2673_o = 1'b0;
      6'b010000: n2673_o = 1'b0;
      6'b001000: n2673_o = n2641_o;
      6'b000100: n2673_o = 1'b0;
      6'b000010: n2673_o = 1'b0;
      6'b000001: n2673_o = 1'b0;
      default: n2673_o = 1'b0;
    endcase
  /* T65_MCode.vhd:969:9  */
  always @*
    case (n2651_o)
      6'b100000: n2676_o = 1'b0;
      6'b010000: n2676_o = 1'b1;
      6'b001000: n2676_o = 1'b0;
      6'b000100: n2676_o = 1'b0;
      6'b000010: n2676_o = 1'b0;
      6'b000001: n2676_o = 1'b0;
      default: n2676_o = 1'b0;
    endcase
  /* T65_MCode.vhd:969:9  */
  always @*
    case (n2651_o)
      6'b100000: n2679_o = 1'b0;
      6'b010000: n2679_o = 1'b0;
      6'b001000: n2679_o = 1'b0;
      6'b000100: n2679_o = 1'b0;
      6'b000010: n2679_o = 1'b0;
      6'b000001: n2679_o = 1'b1;
      default: n2679_o = 1'b0;
    endcase
  /* T65_MCode.vhd:969:9  */
  always @*
    case (n2651_o)
      6'b100000: n2682_o = 1'b0;
      6'b010000: n2682_o = 1'b0;
      6'b001000: n2682_o = 1'b0;
      6'b000100: n2682_o = 1'b0;
      6'b000010: n2682_o = 1'b1;
      6'b000001: n2682_o = 1'b0;
      default: n2682_o = 1'b0;
    endcase
  /* T65_MCode.vhd:969:9  */
  always @*
    case (n2651_o)
      6'b100000: n2685_o = 1'b0;
      6'b010000: n2685_o = 1'b1;
      6'b001000: n2685_o = 1'b0;
      6'b000100: n2685_o = 1'b0;
      6'b000010: n2685_o = 1'b0;
      6'b000001: n2685_o = 1'b0;
      default: n2685_o = 1'b0;
    endcase
  /* T65_MCode.vhd:969:9  */
  always @*
    case (n2651_o)
      6'b100000: n2688_o = 1'b0;
      6'b010000: n2688_o = 1'b1;
      6'b001000: n2688_o = n2644_o;
      6'b000100: n2688_o = n2625_o;
      6'b000010: n2688_o = 1'b0;
      6'b000001: n2688_o = 1'b0;
      default: n2688_o = 1'b0;
    endcase
  /* T65_MCode.vhd:969:9  */
  always @*
    case (n2651_o)
      6'b100000: n2691_o = 1'b1;
      6'b010000: n2691_o = 1'b0;
      6'b001000: n2691_o = 1'b0;
      6'b000100: n2691_o = 1'b0;
      6'b000010: n2691_o = 1'b0;
      6'b000001: n2691_o = 1'b0;
      default: n2691_o = 1'b0;
    endcase
  /* T65_MCode.vhd:960:7  */
  assign n2693_o = n1201_o == 5'b11001;
  /* T65_MCode.vhd:960:20  */
  assign n2695_o = n1201_o == 5'b11011;
  /* T65_MCode.vhd:960:20  */
  assign n2696_o = n2693_o | n2695_o;
  /* T65_MCode.vhd:1013:14  */
  assign n2697_o = ir[7:6]; // extract
  /* T65_MCode.vhd:1013:27  */
  assign n2699_o = n2697_o != 2'b10;
  /* T65_MCode.vhd:1013:41  */
  assign n2700_o = ir[1]; // extract
  /* T65_MCode.vhd:1013:35  */
  assign n2701_o = n2700_o & n2699_o;
  /* T65_MCode.vhd:1013:60  */
  assign n2703_o = mode == 2'b00;
  /* T65_MCode.vhd:1013:71  */
  assign n2704_o = ir[0]; // extract
  /* T65_MCode.vhd:1013:74  */
  assign n2705_o = ~n2704_o;
  /* T65_MCode.vhd:1013:66  */
  assign n2706_o = n2703_o | n2705_o;
  /* T65_MCode.vhd:1013:51  */
  assign n2707_o = n2706_o & n2701_o;
  /* T65_MCode.vhd:1016:18  */
  assign n2709_o = mode == 2'b00;
  /* T65_MCode.vhd:1016:30  */
  assign n2710_o = ir[0]; // extract
  /* T65_MCode.vhd:1016:24  */
  assign n2711_o = n2710_o & n2709_o;
  /* T65_MCode.vhd:1016:11  */
  assign n2713_o = n2711_o ? 1'b1 : n1173_o;
  /* T65_MCode.vhd:1020:13  */
  assign n2715_o = mcycle == 3'b001;
  /* T65_MCode.vhd:1023:13  */
  assign n2717_o = mcycle == 3'b010;
  /* T65_MCode.vhd:1029:13  */
  assign n2719_o = mcycle == 3'b011;
  /* T65_MCode.vhd:1034:22  */
  assign n2721_o = mode == 2'b00;
  /* T65_MCode.vhd:1034:15  */
  assign n2724_o = n2721_o ? 1'b1 : 1'b0;
  /* T65_MCode.vhd:1032:13  */
  assign n2726_o = mcycle == 3'b100;
  /* T65_MCode.vhd:1038:13  */
  assign n2728_o = mcycle == 3'b101;
  /* T65_MCode.vhd:1044:22  */
  assign n2730_o = mode == 2'b00;
  /* T65_MCode.vhd:1044:34  */
  assign n2731_o = ir[0]; // extract
  /* T65_MCode.vhd:1044:28  */
  assign n2732_o = n2731_o & n2730_o;
  /* T65_MCode.vhd:1044:15  */
  assign n2734_o = n2732_o ? 4'b0001 : n1200_o;
  /* T65_MCode.vhd:1044:15  */
  assign n2737_o = n2732_o ? 1'b1 : 1'b0;
  /* T65_MCode.vhd:1043:13  */
  assign n2739_o = mcycle == 3'b110;
  assign n2740_o = {n2739_o, n2728_o, n2726_o, n2719_o, n2717_o, n2715_o};
  /* T65_MCode.vhd:1019:11  */
  always @*
    case (n2740_o)
      6'b100000: n2742_o = n2734_o;
      6'b010000: n2742_o = n1200_o;
      6'b001000: n2742_o = n1200_o;
      6'b000100: n2742_o = n1200_o;
      6'b000010: n2742_o = 4'b0010;
      6'b000001: n2742_o = n1200_o;
      default: n2742_o = n1200_o;
    endcase
  /* T65_MCode.vhd:1019:11  */
  always @*
    case (n2740_o)
      6'b100000: n2748_o = 2'b00;
      6'b010000: n2748_o = 2'b11;
      6'b001000: n2748_o = 2'b11;
      6'b000100: n2748_o = 2'b11;
      6'b000010: n2748_o = 2'b11;
      6'b000001: n2748_o = 2'b00;
      default: n2748_o = 2'b00;
    endcase
  /* T65_MCode.vhd:1019:11  */
  always @*
    case (n2740_o)
      6'b100000: n2752_o = 2'b00;
      6'b010000: n2752_o = 2'b00;
      6'b001000: n2752_o = 2'b00;
      6'b000100: n2752_o = 2'b00;
      6'b000010: n2752_o = 2'b01;
      6'b000001: n2752_o = 2'b01;
      default: n2752_o = 2'b00;
    endcase
  /* T65_MCode.vhd:1019:11  */
  always @*
    case (n2740_o)
      6'b100000: n2756_o = 2'b00;
      6'b010000: n2756_o = 2'b00;
      6'b001000: n2756_o = 2'b00;
      6'b000100: n2756_o = 2'b11;
      6'b000010: n2756_o = 2'b10;
      6'b000001: n2756_o = 2'b00;
      default: n2756_o = 2'b00;
    endcase
  /* T65_MCode.vhd:1019:11  */
  always @*
    case (n2740_o)
      6'b100000: n2759_o = 1'b0;
      6'b010000: n2759_o = 1'b0;
      6'b001000: n2759_o = 1'b1;
      6'b000100: n2759_o = 1'b0;
      6'b000010: n2759_o = 1'b0;
      6'b000001: n2759_o = 1'b0;
      default: n2759_o = 1'b0;
    endcase
  /* T65_MCode.vhd:1019:11  */
  always @*
    case (n2740_o)
      6'b100000: n2762_o = 1'b0;
      6'b010000: n2762_o = 1'b1;
      6'b001000: n2762_o = 1'b0;
      6'b000100: n2762_o = 1'b0;
      6'b000010: n2762_o = 1'b0;
      6'b000001: n2762_o = 1'b0;
      default: n2762_o = 1'b0;
    endcase
  /* T65_MCode.vhd:1019:11  */
  always @*
    case (n2740_o)
      6'b100000: n2765_o = 1'b0;
      6'b010000: n2765_o = 1'b0;
      6'b001000: n2765_o = 1'b0;
      6'b000100: n2765_o = 1'b0;
      6'b000010: n2765_o = 1'b0;
      6'b000001: n2765_o = 1'b1;
      default: n2765_o = 1'b0;
    endcase
  /* T65_MCode.vhd:1019:11  */
  always @*
    case (n2740_o)
      6'b100000: n2768_o = 1'b0;
      6'b010000: n2768_o = 1'b0;
      6'b001000: n2768_o = 1'b0;
      6'b000100: n2768_o = 1'b0;
      6'b000010: n2768_o = 1'b1;
      6'b000001: n2768_o = 1'b0;
      default: n2768_o = 1'b0;
    endcase
  /* T65_MCode.vhd:1019:11  */
  always @*
    case (n2740_o)
      6'b100000: n2771_o = 1'b0;
      6'b010000: n2771_o = 1'b1;
      6'b001000: n2771_o = 1'b0;
      6'b000100: n2771_o = 1'b0;
      6'b000010: n2771_o = 1'b0;
      6'b000001: n2771_o = 1'b0;
      default: n2771_o = 1'b0;
    endcase
  /* T65_MCode.vhd:1019:11  */
  always @*
    case (n2740_o)
      6'b100000: n2774_o = 1'b0;
      6'b010000: n2774_o = 1'b1;
      6'b001000: n2774_o = n2724_o;
      6'b000100: n2774_o = 1'b0;
      6'b000010: n2774_o = 1'b0;
      6'b000001: n2774_o = 1'b0;
      default: n2774_o = 1'b0;
    endcase
  /* T65_MCode.vhd:1019:11  */
  always @*
    case (n2740_o)
      6'b100000: n2776_o = n2737_o;
      6'b010000: n2776_o = 1'b0;
      6'b001000: n2776_o = 1'b0;
      6'b000100: n2776_o = 1'b0;
      6'b000010: n2776_o = 1'b0;
      6'b000001: n2776_o = 1'b0;
      default: n2776_o = 1'b0;
    endcase
  /* T65_MCode.vhd:1052:16  */
  assign n2777_o = ir[7:6]; // extract
  /* T65_MCode.vhd:1052:29  */
  assign n2779_o = n2777_o != 2'b10;
  /* T65_MCode.vhd:1053:20  */
  assign n2781_o = mode != 2'b00;
  /* T65_MCode.vhd:1053:32  */
  assign n2782_o = ir[4]; // extract
  /* T65_MCode.vhd:1053:35  */
  assign n2783_o = ~n2782_o;
  /* T65_MCode.vhd:1053:27  */
  assign n2784_o = n2781_o | n2783_o;
  /* T65_MCode.vhd:1053:45  */
  assign n2785_o = ir[1:0]; // extract
  /* T65_MCode.vhd:1053:57  */
  assign n2787_o = n2785_o != 2'b00;
  /* T65_MCode.vhd:1053:40  */
  assign n2788_o = n2784_o | n2787_o;
  /* T65_MCode.vhd:1052:11  */
  assign n2790_o = n2791_o ? 1'b1 : n1173_o;
  /* T65_MCode.vhd:1052:11  */
  assign n2791_o = n2788_o & n2779_o;
  /* T65_MCode.vhd:1058:13  */
  assign n2793_o = mcycle == 3'b000;
  /* T65_MCode.vhd:1059:13  */
  assign n2795_o = mcycle == 3'b001;
  /* T65_MCode.vhd:1065:20  */
  assign n2796_o = ir[7:6]; // extract
  /* T65_MCode.vhd:1065:32  */
  assign n2798_o = n2796_o == 2'b10;
  /* T65_MCode.vhd:1065:44  */
  assign n2799_o = ir[4:1]; // extract
  /* T65_MCode.vhd:1065:56  */
  assign n2801_o = n2799_o == 4'b1111;
  /* T65_MCode.vhd:1065:38  */
  assign n2802_o = n2801_o & n2798_o;
  /* T65_MCode.vhd:1065:15  */
  assign n2805_o = n2802_o ? 4'b0011 : 4'b0010;
  /* T65_MCode.vhd:1062:13  */
  assign n2807_o = mcycle == 3'b010;
  /* T65_MCode.vhd:1075:20  */
  assign n2808_o = ir[7:5]; // extract
  /* T65_MCode.vhd:1075:33  */
  assign n2810_o = n2808_o == 3'b100;
  /* T65_MCode.vhd:1077:24  */
  assign n2811_o = ir[1:0]; // extract
  /* T65_MCode.vhd:1078:17  */
  assign n2813_o = n2811_o == 2'b00;
  /* T65_MCode.vhd:1078:26  */
  assign n2815_o = n2811_o == 2'b10;
  /* T65_MCode.vhd:1078:26  */
  assign n2816_o = n2813_o | n2815_o;
  /* T65_MCode.vhd:1079:17  */
  assign n2818_o = n2811_o == 2'b11;
  assign n2819_o = {n2818_o, n2816_o};
  /* T65_MCode.vhd:1077:17  */
  always @*
    case (n2819_o)
      2'b10: n2823_o = 2'b10;
      2'b01: n2823_o = 2'b01;
      default: n2823_o = 2'b00;
    endcase
  /* T65_MCode.vhd:1075:15  */
  assign n2825_o = n2810_o ? n2823_o : 2'b00;
  /* T65_MCode.vhd:1075:15  */
  assign n2828_o = n2810_o ? 1'b0 : 1'b1;
  /* T65_MCode.vhd:1075:15  */
  assign n2831_o = n2810_o ? 1'b1 : 1'b0;
  /* T65_MCode.vhd:1073:13  */
  assign n2833_o = mcycle == 3'b011;
  /* T65_MCode.vhd:1086:13  */
  assign n2835_o = mcycle == 3'b100;
  assign n2836_o = {n2835_o, n2833_o, n2807_o, n2795_o, n2793_o};
  /* T65_MCode.vhd:1057:11  */
  always @*
    case (n2836_o)
      5'b10000: n2837_o = n1200_o;
      5'b01000: n2837_o = n1200_o;
      5'b00100: n2837_o = n2805_o;
      5'b00010: n2837_o = n1200_o;
      5'b00001: n2837_o = n1200_o;
      default: n2837_o = n1200_o;
    endcase
  /* T65_MCode.vhd:1057:11  */
  always @*
    case (n2836_o)
      5'b10000: n2841_o = 2'b00;
      5'b01000: n2841_o = 2'b11;
      5'b00100: n2841_o = 2'b11;
      5'b00010: n2841_o = 2'b00;
      5'b00001: n2841_o = 2'b00;
      default: n2841_o = 2'b00;
    endcase
  /* T65_MCode.vhd:1057:11  */
  always @*
    case (n2836_o)
      5'b10000: n2845_o = 2'b00;
      5'b01000: n2845_o = 2'b00;
      5'b00100: n2845_o = 2'b01;
      5'b00010: n2845_o = 2'b01;
      5'b00001: n2845_o = 2'b00;
      default: n2845_o = 2'b00;
    endcase
  /* T65_MCode.vhd:1057:11  */
  always @*
    case (n2836_o)
      5'b10000: n2849_o = 2'b00;
      5'b01000: n2849_o = 2'b11;
      5'b00100: n2849_o = 2'b10;
      5'b00010: n2849_o = 2'b00;
      5'b00001: n2849_o = 2'b00;
      default: n2849_o = 2'b00;
    endcase
  /* T65_MCode.vhd:1057:11  */
  always @*
    case (n2836_o)
      5'b10000: n2851_o = 2'b00;
      5'b01000: n2851_o = n2825_o;
      5'b00100: n2851_o = 2'b00;
      5'b00010: n2851_o = 2'b00;
      5'b00001: n2851_o = 2'b00;
      default: n2851_o = 2'b00;
    endcase
  /* T65_MCode.vhd:1057:11  */
  always @*
    case (n2836_o)
      5'b10000: n2853_o = 1'b0;
      5'b01000: n2853_o = n2828_o;
      5'b00100: n2853_o = 1'b0;
      5'b00010: n2853_o = 1'b0;
      5'b00001: n2853_o = 1'b0;
      default: n2853_o = 1'b0;
    endcase
  /* T65_MCode.vhd:1057:11  */
  always @*
    case (n2836_o)
      5'b10000: n2856_o = 1'b0;
      5'b01000: n2856_o = 1'b0;
      5'b00100: n2856_o = 1'b0;
      5'b00010: n2856_o = 1'b1;
      5'b00001: n2856_o = 1'b0;
      default: n2856_o = 1'b0;
    endcase
  /* T65_MCode.vhd:1057:11  */
  always @*
    case (n2836_o)
      5'b10000: n2859_o = 1'b0;
      5'b01000: n2859_o = 1'b0;
      5'b00100: n2859_o = 1'b1;
      5'b00010: n2859_o = 1'b0;
      5'b00001: n2859_o = 1'b0;
      default: n2859_o = 1'b0;
    endcase
  /* T65_MCode.vhd:1057:11  */
  always @*
    case (n2836_o)
      5'b10000: n2861_o = 1'b0;
      5'b01000: n2861_o = n2831_o;
      5'b00100: n2861_o = 1'b0;
      5'b00010: n2861_o = 1'b0;
      5'b00001: n2861_o = 1'b0;
      default: n2861_o = 1'b0;
    endcase
  /* T65_MCode.vhd:1013:9  */
  assign n2864_o = n2707_o ? 3'b110 : 3'b100;
  /* T65_MCode.vhd:1013:9  */
  assign n2865_o = n2707_o ? n2742_o : n2837_o;
  /* T65_MCode.vhd:1013:9  */
  assign n2866_o = n2707_o ? n2748_o : n2841_o;
  /* T65_MCode.vhd:1013:9  */
  assign n2867_o = n2707_o ? n2752_o : n2845_o;
  /* T65_MCode.vhd:1013:9  */
  assign n2868_o = n2707_o ? n2756_o : n2849_o;
  /* T65_MCode.vhd:1013:9  */
  assign n2870_o = n2707_o ? 2'b00 : n2851_o;
  /* T65_MCode.vhd:1013:9  */
  assign n2872_o = n2707_o ? 1'b0 : n2853_o;
  /* T65_MCode.vhd:1013:9  */
  assign n2873_o = n2707_o ? n2713_o : n2790_o;
  /* T65_MCode.vhd:1013:9  */
  assign n2875_o = n2707_o ? n2759_o : 1'b0;
  /* T65_MCode.vhd:1013:9  */
  assign n2877_o = n2707_o ? n2762_o : 1'b0;
  /* T65_MCode.vhd:1013:9  */
  assign n2878_o = n2707_o ? n2765_o : n2856_o;
  /* T65_MCode.vhd:1013:9  */
  assign n2879_o = n2707_o ? n2768_o : n2859_o;
  /* T65_MCode.vhd:1013:9  */
  assign n2881_o = n2707_o ? n2771_o : 1'b0;
  /* T65_MCode.vhd:1013:9  */
  assign n2882_o = n2707_o ? n2774_o : n2861_o;
  /* T65_MCode.vhd:1013:9  */
  assign n2884_o = n2707_o ? n2776_o : 1'b0;
  /* T65_MCode.vhd:1011:7  */
  assign n2886_o = n1201_o == 5'b11100;
  /* T65_MCode.vhd:1011:20  */
  assign n2888_o = n1201_o == 5'b11101;
  /* T65_MCode.vhd:1011:20  */
  assign n2889_o = n2886_o | n2888_o;
  /* T65_MCode.vhd:1011:30  */
  assign n2891_o = n1201_o == 5'b11110;
  /* T65_MCode.vhd:1011:30  */
  assign n2892_o = n2889_o | n2891_o;
  /* T65_MCode.vhd:1011:40  */
  assign n2894_o = n1201_o == 5'b11111;
  /* T65_MCode.vhd:1011:40  */
  assign n2895_o = n2892_o | n2894_o;
  assign n2896_o = {n2895_o, n2696_o, n2582_o, n2427_o, n2306_o, n2284_o, n2151_o, n2015_o, n1892_o, n1858_o, n1818_o, n1771_o, n1759_o, n1660_o};
  /* T65_MCode.vhd:235:5  */
  always @*
    case (n2896_o)
      14'b10000000000000: n2899_o = n2864_o;
      14'b01000000000000: n2899_o = n2594_o;
      14'b00100000000000: n2899_o = n2554_o;
      14'b00010000000000: n2899_o = n2318_o;
      14'b00001000000000: n2899_o = n2287_o;
      14'b00000100000000: n2899_o = n2261_o;
      14'b00000010000000: n2899_o = n2137_o;
      14'b00000001000000: n2899_o = n1993_o;
      14'b00000000100000: n2899_o = 3'b010;
      14'b00000000010000: n2899_o = n1850_o;
      14'b00000000001000: n2899_o = 3'b001;
      14'b00000000000100: n2899_o = 3'b001;
      14'b00000000000010: n2899_o = n1672_o;
      14'b00000000000001: n2899_o = n1617_o;
      default: n2899_o = 3'b001;
    endcase
  /* T65_MCode.vhd:235:5  */
  always @*
    case (n2896_o)
      14'b10000000000000: n2901_o = n2865_o;
      14'b01000000000000: n2901_o = n2654_o;
      14'b00100000000000: n2901_o = n2555_o;
      14'b00010000000000: n2901_o = n2380_o;
      14'b00001000000000: n2901_o = n1200_o;
      14'b00000100000000: n2901_o = n2262_o;
      14'b00000010000000: n2901_o = n1200_o;
      14'b00000001000000: n2901_o = n1994_o;
      14'b00000000100000: n2901_o = n1200_o;
      14'b00000000010000: n2901_o = n1200_o;
      14'b00000000001000: n2901_o = n1812_o;
      14'b00000000000100: n2901_o = n1200_o;
      14'b00000000000010: n2901_o = n1715_o;
      14'b00000000000001: n2901_o = n1619_o;
      default: n2901_o = n1200_o;
    endcase
  /* T65_MCode.vhd:235:5  */
  always @*
    case (n2896_o)
      14'b10000000000000: n2903_o = n2866_o;
      14'b01000000000000: n2903_o = n2659_o;
      14'b00100000000000: n2903_o = n2556_o;
      14'b00010000000000: n2903_o = n2387_o;
      14'b00001000000000: n2903_o = 2'b00;
      14'b00000100000000: n2903_o = n2263_o;
      14'b00000010000000: n2903_o = n2138_o;
      14'b00000001000000: n2903_o = n1995_o;
      14'b00000000100000: n2903_o = n1880_o;
      14'b00000000010000: n2903_o = 2'b00;
      14'b00000000001000: n2903_o = 2'b00;
      14'b00000000000100: n2903_o = 2'b00;
      14'b00000000000010: n2903_o = n1722_o;
      14'b00000000000001: n2903_o = n1621_o;
      default: n2903_o = 2'b00;
    endcase
  /* T65_MCode.vhd:235:5  */
  always @*
    case (n2896_o)
      14'b10000000000000: n2905_o = n1170_o;
      14'b01000000000000: n2905_o = n1170_o;
      14'b00100000000000: n2905_o = n1170_o;
      14'b00010000000000: n2905_o = n1170_o;
      14'b00001000000000: n2905_o = n1170_o;
      14'b00000100000000: n2905_o = n1170_o;
      14'b00000010000000: n2905_o = n1170_o;
      14'b00000001000000: n2905_o = n1170_o;
      14'b00000000100000: n2905_o = n1170_o;
      14'b00000000010000: n2905_o = n1170_o;
      14'b00000000001000: n2905_o = n1170_o;
      14'b00000000000100: n2905_o = n1170_o;
      14'b00000000000010: n2905_o = n1170_o;
      14'b00000000000001: n2905_o = n1622_o;
      default: n2905_o = n1170_o;
    endcase
  /* T65_MCode.vhd:235:5  */
  always @*
    case (n2896_o)
      14'b10000000000000: n2907_o = n2867_o;
      14'b01000000000000: n2907_o = n2663_o;
      14'b00100000000000: n2907_o = n2557_o;
      14'b00010000000000: n2907_o = n2390_o;
      14'b00001000000000: n2907_o = n2298_o;
      14'b00000100000000: n2907_o = n2264_o;
      14'b00000010000000: n2907_o = n2139_o;
      14'b00000001000000: n2907_o = n1996_o;
      14'b00000000100000: n2907_o = n1883_o;
      14'b00000000010000: n2907_o = n1852_o;
      14'b00000000001000: n2907_o = n1814_o;
      14'b00000000000100: n2907_o = n1769_o;
      14'b00000000000010: n2907_o = n1725_o;
      14'b00000000000001: n2907_o = n1624_o;
      default: n2907_o = 2'b00;
    endcase
  /* T65_MCode.vhd:235:5  */
  always @*
    case (n2896_o)
      14'b10000000000000: n2910_o = n2868_o;
      14'b01000000000000: n2910_o = n2667_o;
      14'b00100000000000: n2910_o = 2'b00;
      14'b00010000000000: n2910_o = n2395_o;
      14'b00001000000000: n2910_o = 2'b00;
      14'b00000100000000: n2910_o = 2'b00;
      14'b00000010000000: n2910_o = n2141_o;
      14'b00000001000000: n2910_o = 2'b00;
      14'b00000000100000: n2910_o = 2'b00;
      14'b00000000010000: n2910_o = 2'b00;
      14'b00000000001000: n2910_o = 2'b00;
      14'b00000000000100: n2910_o = 2'b00;
      14'b00000000000010: n2910_o = n1728_o;
      14'b00000000000001: n2910_o = 2'b00;
      default: n2910_o = 2'b00;
    endcase
  /* T65_MCode.vhd:235:5  */
  always @*
    case (n2896_o)
      14'b10000000000000: n2913_o = n2870_o;
      14'b01000000000000: n2913_o = n2669_o;
      14'b00100000000000: n2913_o = 2'b00;
      14'b00010000000000: n2913_o = n2397_o;
      14'b00001000000000: n2913_o = 2'b00;
      14'b00000100000000: n2913_o = 2'b00;
      14'b00000010000000: n2913_o = 2'b00;
      14'b00000001000000: n2913_o = 2'b00;
      14'b00000000100000: n2913_o = 2'b00;
      14'b00000000010000: n2913_o = 2'b00;
      14'b00000000001000: n2913_o = 2'b00;
      14'b00000000000100: n2913_o = 2'b00;
      14'b00000000000010: n2913_o = 2'b00;
      14'b00000000000001: n2913_o = 2'b00;
      default: n2913_o = 2'b00;
    endcase
  /* T65_MCode.vhd:235:5  */
  always @*
    case (n2896_o)
      14'b10000000000000: n2916_o = n2872_o;
      14'b01000000000000: n2916_o = n2671_o;
      14'b00100000000000: n2916_o = 1'b0;
      14'b00010000000000: n2916_o = n2399_o;
      14'b00001000000000: n2916_o = 1'b0;
      14'b00000100000000: n2916_o = 1'b0;
      14'b00000010000000: n2916_o = 1'b0;
      14'b00000001000000: n2916_o = 1'b0;
      14'b00000000100000: n2916_o = 1'b0;
      14'b00000000010000: n2916_o = 1'b0;
      14'b00000000001000: n2916_o = 1'b0;
      14'b00000000000100: n2916_o = 1'b0;
      14'b00000000000010: n2916_o = 1'b0;
      14'b00000000000001: n2916_o = 1'b0;
      default: n2916_o = 1'b0;
    endcase
  /* T65_MCode.vhd:235:5  */
  always @*
    case (n2896_o)
      14'b10000000000000: n2919_o = 1'b0;
      14'b01000000000000: n2919_o = 1'b0;
      14'b00100000000000: n2919_o = n2558_o;
      14'b00010000000000: n2919_o = 1'b0;
      14'b00001000000000: n2919_o = 1'b0;
      14'b00000100000000: n2919_o = 1'b0;
      14'b00000010000000: n2919_o = 1'b0;
      14'b00000001000000: n2919_o = 1'b0;
      14'b00000000100000: n2919_o = 1'b0;
      14'b00000000010000: n2919_o = 1'b0;
      14'b00000000001000: n2919_o = 1'b0;
      14'b00000000000100: n2919_o = 1'b0;
      14'b00000000000010: n2919_o = n1731_o;
      14'b00000000000001: n2919_o = 1'b0;
      default: n2919_o = 1'b0;
    endcase
  /* T65_MCode.vhd:235:5  */
  always @*
    case (n2896_o)
      14'b10000000000000: n2922_o = 1'b0;
      14'b01000000000000: n2922_o = 1'b0;
      14'b00100000000000: n2922_o = n2560_o;
      14'b00010000000000: n2922_o = 1'b0;
      14'b00001000000000: n2922_o = 1'b0;
      14'b00000100000000: n2922_o = 1'b0;
      14'b00000010000000: n2922_o = 1'b0;
      14'b00000001000000: n2922_o = 1'b0;
      14'b00000000100000: n2922_o = 1'b0;
      14'b00000000010000: n2922_o = 1'b0;
      14'b00000000001000: n2922_o = 1'b0;
      14'b00000000000100: n2922_o = 1'b0;
      14'b00000000000010: n2922_o = 1'b0;
      14'b00000000000001: n2922_o = 1'b0;
      default: n2922_o = 1'b0;
    endcase
  /* T65_MCode.vhd:235:5  */
  always @*
    case (n2896_o)
      14'b10000000000000: n2925_o = 1'b0;
      14'b01000000000000: n2925_o = 1'b0;
      14'b00100000000000: n2925_o = 1'b0;
      14'b00010000000000: n2925_o = 1'b0;
      14'b00001000000000: n2925_o = n2301_o;
      14'b00000100000000: n2925_o = 1'b0;
      14'b00000010000000: n2925_o = 1'b0;
      14'b00000001000000: n2925_o = 1'b0;
      14'b00000000100000: n2925_o = 1'b0;
      14'b00000000010000: n2925_o = 1'b0;
      14'b00000000001000: n2925_o = 1'b0;
      14'b00000000000100: n2925_o = 1'b0;
      14'b00000000000010: n2925_o = 1'b0;
      14'b00000000000001: n2925_o = 1'b0;
      default: n2925_o = 1'b0;
    endcase
  /* T65_MCode.vhd:235:5  */
  always @*
    case (n2896_o)
      14'b10000000000000: n2928_o = 1'b0;
      14'b01000000000000: n2928_o = 1'b0;
      14'b00100000000000: n2928_o = 1'b0;
      14'b00010000000000: n2928_o = 1'b0;
      14'b00001000000000: n2928_o = 1'b0;
      14'b00000100000000: n2928_o = 1'b0;
      14'b00000010000000: n2928_o = 1'b0;
      14'b00000001000000: n2928_o = 1'b0;
      14'b00000000100000: n2928_o = 1'b0;
      14'b00000000010000: n2928_o = 1'b0;
      14'b00000000001000: n2928_o = 1'b0;
      14'b00000000000100: n2928_o = 1'b0;
      14'b00000000000010: n2928_o = 1'b0;
      14'b00000000000001: n2928_o = n1626_o;
      default: n2928_o = 1'b0;
    endcase
  /* T65_MCode.vhd:235:5  */
  always @*
    case (n2896_o)
      14'b10000000000000: n2931_o = 1'b0;
      14'b01000000000000: n2931_o = 1'b0;
      14'b00100000000000: n2931_o = 1'b0;
      14'b00010000000000: n2931_o = 1'b0;
      14'b00001000000000: n2931_o = 1'b0;
      14'b00000100000000: n2931_o = 1'b0;
      14'b00000010000000: n2931_o = 1'b0;
      14'b00000001000000: n2931_o = 1'b0;
      14'b00000000100000: n2931_o = 1'b0;
      14'b00000000010000: n2931_o = 1'b0;
      14'b00000000001000: n2931_o = 1'b0;
      14'b00000000000100: n2931_o = 1'b0;
      14'b00000000000010: n2931_o = 1'b0;
      14'b00000000000001: n2931_o = n1628_o;
      default: n2931_o = 1'b0;
    endcase
  /* T65_MCode.vhd:235:5  */
  always @*
    case (n2896_o)
      14'b10000000000000: n2933_o = n2873_o;
      14'b01000000000000: n2933_o = n2597_o;
      14'b00100000000000: n2933_o = n2561_o;
      14'b00010000000000: n2933_o = n2321_o;
      14'b00001000000000: n2933_o = n1173_o;
      14'b00000100000000: n2933_o = n2265_o;
      14'b00000010000000: n2933_o = n1173_o;
      14'b00000001000000: n2933_o = n1997_o;
      14'b00000000100000: n2933_o = n1173_o;
      14'b00000000010000: n2933_o = n1173_o;
      14'b00000000001000: n2933_o = n1815_o;
      14'b00000000000100: n2933_o = n1764_o;
      14'b00000000000010: n2933_o = n1675_o;
      14'b00000000000001: n2933_o = n1631_o;
      default: n2933_o = n1173_o;
    endcase
  /* T65_MCode.vhd:235:5  */
  always @*
    case (n2896_o)
      14'b10000000000000: n2935_o = 1'b0;
      14'b01000000000000: n2935_o = 1'b0;
      14'b00100000000000: n2935_o = 1'b0;
      14'b00010000000000: n2935_o = 1'b0;
      14'b00001000000000: n2935_o = 1'b0;
      14'b00000100000000: n2935_o = 1'b0;
      14'b00000010000000: n2935_o = 1'b0;
      14'b00000001000000: n2935_o = 1'b0;
      14'b00000000100000: n2935_o = 1'b0;
      14'b00000000010000: n2935_o = 1'b0;
      14'b00000000001000: n2935_o = 1'b0;
      14'b00000000000100: n2935_o = 1'b0;
      14'b00000000000010: n2935_o = 1'b0;
      14'b00000000000001: n2935_o = n1633_o;
      default: n2935_o = 1'b0;
    endcase
  /* T65_MCode.vhd:235:5  */
  always @*
    case (n2896_o)
      14'b10000000000000: n2937_o = n1176_o;
      14'b01000000000000: n2937_o = n1176_o;
      14'b00100000000000: n2937_o = n1176_o;
      14'b00010000000000: n2937_o = n1176_o;
      14'b00001000000000: n2937_o = n1176_o;
      14'b00000100000000: n2937_o = n1176_o;
      14'b00000010000000: n2937_o = n1176_o;
      14'b00000001000000: n2937_o = n1176_o;
      14'b00000000100000: n2937_o = n1176_o;
      14'b00000000010000: n2937_o = n1853_o;
      14'b00000000001000: n2937_o = n1816_o;
      14'b00000000000100: n2937_o = n1176_o;
      14'b00000000000010: n2937_o = n1176_o;
      14'b00000000000001: n2937_o = n1636_o;
      default: n2937_o = n1176_o;
    endcase
  /* T65_MCode.vhd:235:5  */
  always @*
    case (n2896_o)
      14'b10000000000000: n2938_o = n1179_o;
      14'b01000000000000: n2938_o = n1179_o;
      14'b00100000000000: n2938_o = n1179_o;
      14'b00010000000000: n2938_o = n1179_o;
      14'b00001000000000: n2938_o = n1179_o;
      14'b00000100000000: n2938_o = n1179_o;
      14'b00000010000000: n2938_o = n1179_o;
      14'b00000001000000: n2938_o = n1179_o;
      14'b00000000100000: n2938_o = n1179_o;
      14'b00000000010000: n2938_o = n1179_o;
      14'b00000000001000: n2938_o = n1179_o;
      14'b00000000000100: n2938_o = n1179_o;
      14'b00000000000010: n2938_o = n1179_o;
      14'b00000000000001: n2938_o = n1638_o;
      default: n2938_o = n1179_o;
    endcase
  /* T65_MCode.vhd:235:5  */
  always @*
    case (n2896_o)
      14'b10000000000000: n2939_o = n1182_o;
      14'b01000000000000: n2939_o = n1182_o;
      14'b00100000000000: n2939_o = n1182_o;
      14'b00010000000000: n2939_o = n1182_o;
      14'b00001000000000: n2939_o = n1182_o;
      14'b00000100000000: n2939_o = n1182_o;
      14'b00000010000000: n2939_o = n1182_o;
      14'b00000001000000: n2939_o = n1182_o;
      14'b00000000100000: n2939_o = n1182_o;
      14'b00000000010000: n2939_o = n1182_o;
      14'b00000000001000: n2939_o = n1182_o;
      14'b00000000000100: n2939_o = n1182_o;
      14'b00000000000010: n2939_o = n1182_o;
      14'b00000000000001: n2939_o = n1640_o;
      default: n2939_o = n1182_o;
    endcase
  /* T65_MCode.vhd:235:5  */
  always @*
    case (n2896_o)
      14'b10000000000000: n2941_o = n2875_o;
      14'b01000000000000: n2941_o = n2673_o;
      14'b00100000000000: n2941_o = n2563_o;
      14'b00010000000000: n2941_o = n2401_o;
      14'b00001000000000: n2941_o = n2304_o;
      14'b00000100000000: n2941_o = n2267_o;
      14'b00000010000000: n2941_o = n2143_o;
      14'b00000001000000: n2941_o = n1999_o;
      14'b00000000100000: n2941_o = 1'b0;
      14'b00000000010000: n2941_o = 1'b0;
      14'b00000000001000: n2941_o = 1'b0;
      14'b00000000000100: n2941_o = 1'b0;
      14'b00000000000010: n2941_o = n1733_o;
      14'b00000000000001: n2941_o = n1642_o;
      default: n2941_o = 1'b0;
    endcase
  /* T65_MCode.vhd:235:5  */
  always @*
    case (n2896_o)
      14'b10000000000000: n2944_o = n2877_o;
      14'b01000000000000: n2944_o = n2676_o;
      14'b00100000000000: n2944_o = n2565_o;
      14'b00010000000000: n2944_o = n2404_o;
      14'b00001000000000: n2944_o = 1'b0;
      14'b00000100000000: n2944_o = n2269_o;
      14'b00000010000000: n2944_o = 1'b0;
      14'b00000001000000: n2944_o = n2001_o;
      14'b00000000100000: n2944_o = 1'b0;
      14'b00000000010000: n2944_o = 1'b0;
      14'b00000000001000: n2944_o = 1'b0;
      14'b00000000000100: n2944_o = 1'b0;
      14'b00000000000010: n2944_o = n1736_o;
      14'b00000000000001: n2944_o = 1'b0;
      default: n2944_o = 1'b0;
    endcase
  /* T65_MCode.vhd:235:5  */
  always @*
    case (n2896_o)
      14'b10000000000000: n2947_o = 1'b0;
      14'b01000000000000: n2947_o = 1'b0;
      14'b00100000000000: n2947_o = n2566_o;
      14'b00010000000000: n2947_o = n2407_o;
      14'b00001000000000: n2947_o = 1'b0;
      14'b00000100000000: n2947_o = 1'b0;
      14'b00000010000000: n2947_o = 1'b0;
      14'b00000001000000: n2947_o = n2002_o;
      14'b00000000100000: n2947_o = n1886_o;
      14'b00000000010000: n2947_o = 1'b0;
      14'b00000000001000: n2947_o = 1'b0;
      14'b00000000000100: n2947_o = 1'b0;
      14'b00000000000010: n2947_o = n1739_o;
      14'b00000000000001: n2947_o = 1'b0;
      default: n2947_o = 1'b0;
    endcase
  /* T65_MCode.vhd:235:5  */
  always @*
    case (n2896_o)
      14'b10000000000000: n2950_o = n2878_o;
      14'b01000000000000: n2950_o = n2679_o;
      14'b00100000000000: n2950_o = 1'b0;
      14'b00010000000000: n2950_o = n2410_o;
      14'b00001000000000: n2950_o = 1'b0;
      14'b00000100000000: n2950_o = n2270_o;
      14'b00000010000000: n2950_o = n2144_o;
      14'b00000001000000: n2950_o = 1'b0;
      14'b00000000100000: n2950_o = 1'b0;
      14'b00000000010000: n2950_o = 1'b0;
      14'b00000000001000: n2950_o = 1'b0;
      14'b00000000000100: n2950_o = 1'b0;
      14'b00000000000010: n2950_o = n1742_o;
      14'b00000000000001: n2950_o = 1'b0;
      default: n2950_o = 1'b0;
    endcase
  /* T65_MCode.vhd:235:5  */
  always @*
    case (n2896_o)
      14'b10000000000000: n2953_o = n2879_o;
      14'b01000000000000: n2953_o = n2682_o;
      14'b00100000000000: n2953_o = 1'b0;
      14'b00010000000000: n2953_o = n2413_o;
      14'b00001000000000: n2953_o = 1'b0;
      14'b00000100000000: n2953_o = n2271_o;
      14'b00000010000000: n2953_o = n2145_o;
      14'b00000001000000: n2953_o = 1'b0;
      14'b00000000100000: n2953_o = 1'b0;
      14'b00000000010000: n2953_o = 1'b0;
      14'b00000000001000: n2953_o = 1'b0;
      14'b00000000000100: n2953_o = 1'b0;
      14'b00000000000010: n2953_o = n1745_o;
      14'b00000000000001: n2953_o = 1'b0;
      default: n2953_o = 1'b0;
    endcase
  /* T65_MCode.vhd:235:5  */
  always @*
    case (n2896_o)
      14'b10000000000000: n2956_o = n2881_o;
      14'b01000000000000: n2956_o = n2685_o;
      14'b00100000000000: n2956_o = n2568_o;
      14'b00010000000000: n2956_o = n2416_o;
      14'b00001000000000: n2956_o = 1'b0;
      14'b00000100000000: n2956_o = n2273_o;
      14'b00000010000000: n2956_o = n2147_o;
      14'b00000001000000: n2956_o = n2004_o;
      14'b00000000100000: n2956_o = n1888_o;
      14'b00000000010000: n2956_o = 1'b0;
      14'b00000000001000: n2956_o = 1'b0;
      14'b00000000000100: n2956_o = 1'b0;
      14'b00000000000010: n2956_o = n1748_o;
      14'b00000000000001: n2956_o = n1644_o;
      default: n2956_o = 1'b0;
    endcase
  /* T65_MCode.vhd:235:5  */
  always @*
    case (n2896_o)
      14'b10000000000000: n2959_o = n2882_o;
      14'b01000000000000: n2959_o = n2688_o;
      14'b00100000000000: n2959_o = n2569_o;
      14'b00010000000000: n2959_o = n2419_o;
      14'b00001000000000: n2959_o = 1'b0;
      14'b00000100000000: n2959_o = n2274_o;
      14'b00000010000000: n2959_o = n2149_o;
      14'b00000001000000: n2959_o = n2005_o;
      14'b00000000100000: n2959_o = n1890_o;
      14'b00000000010000: n2959_o = 1'b0;
      14'b00000000001000: n2959_o = 1'b0;
      14'b00000000000100: n2959_o = 1'b0;
      14'b00000000000010: n2959_o = n1751_o;
      14'b00000000000001: n2959_o = n1646_o;
      default: n2959_o = 1'b0;
    endcase
  /* T65_MCode.vhd:235:5  */
  always @*
    case (n2896_o)
      14'b10000000000000: n2962_o = n2884_o;
      14'b01000000000000: n2962_o = n2691_o;
      14'b00100000000000: n2962_o = n2571_o;
      14'b00010000000000: n2962_o = n2422_o;
      14'b00001000000000: n2962_o = 1'b0;
      14'b00000100000000: n2962_o = n2276_o;
      14'b00000010000000: n2962_o = 1'b0;
      14'b00000001000000: n2962_o = n2007_o;
      14'b00000000100000: n2962_o = 1'b0;
      14'b00000000010000: n2962_o = 1'b0;
      14'b00000000001000: n2962_o = 1'b0;
      14'b00000000000100: n2962_o = 1'b0;
      14'b00000000000010: n2962_o = n1754_o;
      14'b00000000000001: n2962_o = 1'b0;
      default: n2962_o = 1'b0;
    endcase
  /* T65_MCode.vhd:1098:12  */
  assign n2967_o = ir[1:0]; // extract
  /* T65_MCode.vhd:1100:16  */
  assign n2968_o = ir[4:2]; // extract
  /* T65_MCode.vhd:1105:18  */
  assign n2969_o = ir[7:5]; // extract
  /* T65_MCode.vhd:1106:13  */
  assign n2971_o = n2969_o == 3'b110;
  /* T65_MCode.vhd:1106:24  */
  assign n2973_o = n2969_o == 3'b111;
  /* T65_MCode.vhd:1106:24  */
  assign n2974_o = n2971_o | n2973_o;
  /* T65_MCode.vhd:1108:13  */
  assign n2976_o = n2969_o == 3'b101;
  /* T65_MCode.vhd:1110:13  */
  assign n2978_o = n2969_o == 3'b001;
  assign n2979_o = {n2978_o, n2976_o, n2974_o};
  /* T65_MCode.vhd:1105:11  */
  always @*
    case (n2979_o)
      3'b100: n2984_o = 5'b01100;
      3'b010: n2984_o = 5'b00101;
      3'b001: n2984_o = 5'b00110;
      default: n2984_o = 5'b00100;
    endcase
  /* T65_MCode.vhd:1104:9  */
  assign n2986_o = n2968_o == 3'b000;
  /* T65_MCode.vhd:1104:20  */
  assign n2988_o = n2968_o == 3'b001;
  /* T65_MCode.vhd:1104:20  */
  assign n2989_o = n2986_o | n2988_o;
  /* T65_MCode.vhd:1104:28  */
  assign n2991_o = n2968_o == 3'b011;
  /* T65_MCode.vhd:1104:28  */
  assign n2992_o = n2989_o | n2991_o;
  /* T65_MCode.vhd:1118:18  */
  assign n2993_o = ir[7:5]; // extract
  /* T65_MCode.vhd:1119:13  */
  assign n2995_o = n2993_o == 3'b111;
  /* T65_MCode.vhd:1119:24  */
  assign n2997_o = n2993_o == 3'b110;
  /* T65_MCode.vhd:1119:24  */
  assign n2998_o = n2995_o | n2997_o;
  /* T65_MCode.vhd:1121:13  */
  assign n3000_o = n2993_o == 3'b100;
  assign n3001_o = {n3000_o, n2998_o};
  /* T65_MCode.vhd:1118:11  */
  always @*
    case (n3001_o)
      2'b10: n3005_o = 5'b01101;
      2'b01: n3005_o = 5'b01110;
      default: n3005_o = 5'b00101;
    endcase
  /* T65_MCode.vhd:1117:9  */
  assign n3007_o = n2968_o == 3'b010;
  /* T65_MCode.vhd:1129:18  */
  assign n3008_o = ir[7:5]; // extract
  /* T65_MCode.vhd:1130:13  */
  assign n3010_o = n3008_o == 3'b100;
  /* T65_MCode.vhd:1129:11  */
  always @*
    case (n3010_o)
      1'b1: n3013_o = 5'b00101;
      default: n3013_o = 5'b00100;
    endcase
  /* T65_MCode.vhd:1128:9  */
  assign n3015_o = n2968_o == 3'b110;
  /* T65_MCode.vhd:1140:18  */
  assign n3016_o = ir[7:5]; // extract
  /* T65_MCode.vhd:1141:13  */
  assign n3018_o = n3016_o == 3'b101;
  /* T65_MCode.vhd:1140:11  */
  always @*
    case (n3018_o)
      1'b1: n3021_o = 5'b00101;
      default: n3021_o = 5'b00100;
    endcase
  assign n3022_o = {n3015_o, n3007_o, n2992_o};
  /* T65_MCode.vhd:1100:9  */
  always @*
    case (n3022_o)
      3'b100: n3023_o = n3013_o;
      3'b010: n3023_o = n3005_o;
      3'b001: n3023_o = n2984_o;
      default: n3023_o = n3021_o;
    endcase
  /* T65_MCode.vhd:1099:7  */
  assign n3025_o = n2967_o == 2'b00;
  /* T65_MCode.vhd:1149:36  */
  assign n3026_o = ir[7:5]; // extract
  /* T65_MCode.vhd:1149:14  */
  assign n3027_o = {28'b0, n3026_o};  //  uext
  /* T65_MCode.vhd:1150:11  */
  assign n3029_o = n3027_o == 31'b0000000000000000000000000000000;
  /* T65_MCode.vhd:1152:11  */
  assign n3031_o = n3027_o == 31'b0000000000000000000000000000001;
  /* T65_MCode.vhd:1154:11  */
  assign n3033_o = n3027_o == 31'b0000000000000000000000000000010;
  /* T65_MCode.vhd:1156:11  */
  assign n3035_o = n3027_o == 31'b0000000000000000000000000000011;
  /* T65_MCode.vhd:1158:11  */
  assign n3037_o = n3027_o == 31'b0000000000000000000000000000100;
  /* T65_MCode.vhd:1160:11  */
  assign n3039_o = n3027_o == 31'b0000000000000000000000000000101;
  /* T65_MCode.vhd:1162:11  */
  assign n3041_o = n3027_o == 31'b0000000000000000000000000000110;
  assign n3042_o = {n3041_o, n3039_o, n3037_o, n3035_o, n3033_o, n3031_o, n3029_o};
  /* T65_MCode.vhd:1149:9  */
  always @*
    case (n3042_o)
      7'b1000000: n3051_o = 5'b00110;
      7'b0100000: n3051_o = 5'b00101;
      7'b0010000: n3051_o = 5'b00100;
      7'b0001000: n3051_o = 5'b00011;
      7'b0000100: n3051_o = 5'b00010;
      7'b0000010: n3051_o = 5'b00001;
      7'b0000001: n3051_o = 5'b00000;
      default: n3051_o = 5'b00111;
    endcase
  /* T65_MCode.vhd:1148:7  */
  assign n3053_o = n2967_o == 2'b01;
  /* T65_MCode.vhd:1169:36  */
  assign n3054_o = ir[7:5]; // extract
  /* T65_MCode.vhd:1169:14  */
  assign n3055_o = {28'b0, n3054_o};  //  uext
  /* T65_MCode.vhd:1172:18  */
  assign n3056_o = ir[4:2]; // extract
  /* T65_MCode.vhd:1172:31  */
  assign n3058_o = n3056_o == 3'b110;
  /* T65_MCode.vhd:1172:47  */
  assign n3060_o = mode != 2'b00;
  /* T65_MCode.vhd:1172:39  */
  assign n3061_o = n3060_o & n3058_o;
  /* T65_MCode.vhd:1172:13  */
  assign n3064_o = n3061_o ? 5'b01110 : 5'b01000;
  /* T65_MCode.vhd:1170:11  */
  assign n3067_o = n3055_o == 31'b0000000000000000000000000000000;
  /* T65_MCode.vhd:1177:18  */
  assign n3068_o = ir[4:2]; // extract
  /* T65_MCode.vhd:1177:31  */
  assign n3070_o = n3068_o == 3'b110;
  /* T65_MCode.vhd:1177:47  */
  assign n3072_o = mode != 2'b00;
  /* T65_MCode.vhd:1177:39  */
  assign n3073_o = n3072_o & n3070_o;
  /* T65_MCode.vhd:1177:13  */
  assign n3076_o = n3073_o ? 5'b01101 : 5'b01001;
  /* T65_MCode.vhd:1175:11  */
  assign n3079_o = n3055_o == 31'b0000000000000000000000000000001;
  /* T65_MCode.vhd:1180:11  */
  assign n3081_o = n3055_o == 31'b0000000000000000000000000000010;
  /* T65_MCode.vhd:1182:11  */
  assign n3083_o = n3055_o == 31'b0000000000000000000000000000011;
  /* T65_MCode.vhd:1186:18  */
  assign n3084_o = ir[4:2]; // extract
  /* T65_MCode.vhd:1186:31  */
  assign n3086_o = n3084_o == 3'b010;
  /* T65_MCode.vhd:1186:13  */
  assign n3089_o = n3086_o ? 5'b00101 : 5'b00100;
  /* T65_MCode.vhd:1184:11  */
  assign n3092_o = n3055_o == 31'b0000000000000000000000000000100;
  /* T65_MCode.vhd:1191:11  */
  assign n3094_o = n3055_o == 31'b0000000000000000000000000000101;
  /* T65_MCode.vhd:1193:11  */
  assign n3096_o = n3055_o == 31'b0000000000000000000000000000110;
  assign n3097_o = {n3096_o, n3094_o, n3092_o, n3083_o, n3081_o, n3079_o, n3067_o};
  /* T65_MCode.vhd:1169:9  */
  always @*
    case (n3097_o)
      7'b1000000: n3103_o = 5'b01101;
      7'b0100000: n3103_o = 5'b00101;
      7'b0010000: n3103_o = n3089_o;
      7'b0001000: n3103_o = 5'b01011;
      7'b0000100: n3103_o = 5'b01010;
      7'b0000010: n3103_o = n3076_o;
      7'b0000001: n3103_o = n3064_o;
      default: n3103_o = 5'b01110;
    endcase
  /* T65_MCode.vhd:1168:7  */
  assign n3105_o = n2967_o == 2'b10;
  /* T65_MCode.vhd:1200:36  */
  assign n3106_o = ir[7:5]; // extract
  /* T65_MCode.vhd:1200:14  */
  assign n3107_o = {28'b0, n3106_o};  //  uext
  /* T65_MCode.vhd:1203:18  */
  assign n3109_o = ir == 8'b10111011;
  /* T65_MCode.vhd:1203:13  */
  assign n3112_o = n3109_o ? 5'b00001 : 5'b00101;
  /* T65_MCode.vhd:1202:11  */
  assign n3114_o = n3107_o == 31'b0000000000000000000000000000101;
  /* T65_MCode.vhd:1217:18  */
  assign n3116_o = ir == 8'b01101011;
  /* T65_MCode.vhd:1219:21  */
  assign n3118_o = ir == 8'b10001011;
  /* T65_MCode.vhd:1221:21  */
  assign n3120_o = ir == 8'b00001011;
  /* T65_MCode.vhd:1221:33  */
  assign n3122_o = ir == 8'b00101011;
  /* T65_MCode.vhd:1221:28  */
  assign n3123_o = n3120_o | n3122_o;
  /* T65_MCode.vhd:1223:21  */
  assign n3125_o = ir == 8'b11101011;
  /* T65_MCode.vhd:1226:42  */
  assign n3126_o = ir[7:5]; // extract
  /* T65_MCode.vhd:1226:20  */
  assign n3127_o = {28'b0, n3126_o};  //  uext
  /* T65_MCode.vhd:1227:17  */
  assign n3129_o = n3127_o == 31'b0000000000000000000000000000000;
  /* T65_MCode.vhd:1229:17  */
  assign n3131_o = n3127_o == 31'b0000000000000000000000000000001;
  /* T65_MCode.vhd:1231:17  */
  assign n3133_o = n3127_o == 31'b0000000000000000000000000000010;
  /* T65_MCode.vhd:1233:17  */
  assign n3135_o = n3127_o == 31'b0000000000000000000000000000011;
  /* T65_MCode.vhd:1235:17  */
  assign n3137_o = n3127_o == 31'b0000000000000000000000000000100;
  /* T65_MCode.vhd:1237:17  */
  assign n3139_o = n3127_o == 31'b0000000000000000000000000000101;
  /* T65_MCode.vhd:1239:17  */
  assign n3141_o = n3127_o == 31'b0000000000000000000000000000110;
  assign n3142_o = {n3141_o, n3139_o, n3137_o, n3135_o, n3133_o, n3131_o, n3129_o};
  /* T65_MCode.vhd:1226:15  */
  always @*
    case (n3142_o)
      7'b1000000: n3151_o = 5'b00110;
      7'b0100000: n3151_o = 5'b00101;
      7'b0010000: n3151_o = 5'b00100;
      7'b0001000: n3151_o = 5'b00011;
      7'b0000100: n3151_o = 5'b00010;
      7'b0000010: n3151_o = 5'b00001;
      7'b0000001: n3151_o = 5'b00000;
      default: n3151_o = 5'b00111;
    endcase
  /* T65_MCode.vhd:1245:42  */
  assign n3152_o = ir[7:5]; // extract
  /* T65_MCode.vhd:1245:20  */
  assign n3153_o = {28'b0, n3152_o};  //  uext
  /* T65_MCode.vhd:1246:17  */
  assign n3155_o = n3153_o == 31'b0000000000000000000000000000000;
  /* T65_MCode.vhd:1248:17  */
  assign n3157_o = n3153_o == 31'b0000000000000000000000000000001;
  /* T65_MCode.vhd:1250:17  */
  assign n3159_o = n3153_o == 31'b0000000000000000000000000000010;
  /* T65_MCode.vhd:1252:17  */
  assign n3161_o = n3153_o == 31'b0000000000000000000000000000011;
  /* T65_MCode.vhd:1254:17  */
  assign n3163_o = n3153_o == 31'b0000000000000000000000000000100;
  /* T65_MCode.vhd:1256:17  */
  assign n3165_o = n3153_o == 31'b0000000000000000000000000000101;
  /* T65_MCode.vhd:1260:24  */
  assign n3166_o = ir[4:2]; // extract
  /* T65_MCode.vhd:1260:36  */
  assign n3168_o = n3166_o == 3'b010;
  /* T65_MCode.vhd:1260:19  */
  assign n3171_o = n3168_o ? 5'b10001 : 5'b01101;
  /* T65_MCode.vhd:1258:17  */
  assign n3174_o = n3153_o == 31'b0000000000000000000000000000110;
  assign n3175_o = {n3174_o, n3165_o, n3163_o, n3161_o, n3159_o, n3157_o, n3155_o};
  /* T65_MCode.vhd:1245:15  */
  always @*
    case (n3175_o)
      7'b1000000: n3183_o = n3171_o;
      7'b0100000: n3183_o = 5'b00101;
      7'b0010000: n3183_o = 5'b01100;
      7'b0001000: n3183_o = 5'b01011;
      7'b0000100: n3183_o = 5'b01010;
      7'b0000010: n3183_o = 5'b01001;
      7'b0000001: n3183_o = 5'b01000;
      default: n3183_o = 5'b01110;
    endcase
  /* T65_MCode.vhd:1225:13  */
  assign n3184_o = alumore ? n3151_o : n3183_o;
  /* T65_MCode.vhd:1223:13  */
  assign n3186_o = n3125_o ? 5'b00111 : n3184_o;
  /* T65_MCode.vhd:1221:13  */
  assign n3188_o = n3123_o ? 5'b10000 : n3186_o;
  /* T65_MCode.vhd:1219:13  */
  assign n3190_o = n3118_o ? 5'b10010 : n3188_o;
  /* T65_MCode.vhd:1217:13  */
  assign n3192_o = n3116_o ? 5'b01111 : n3190_o;
  /* T65_MCode.vhd:1200:9  */
  always @*
    case (n3114_o)
      1'b1: n3193_o = n3112_o;
      default: n3193_o = n3192_o;
    endcase
  assign n3194_o = {n3105_o, n3053_o, n3025_o};
  /* T65_MCode.vhd:1098:5  */
  always @*
    case (n3194_o)
      3'b100: n3195_o = n3103_o;
      3'b010: n3195_o = n3051_o;
      3'b001: n3195_o = n3023_o;
      default: n3195_o = n3193_o;
    endcase
endmodule

module t65
  (input  [1:0] mode,
   input  bcd_en,
   input  res_n,
   input  pwr_n,
   input  enable,
   input  clk,
   input  rdy,
   input  abort_n,
   input  irq_n,
   input  nmi_n,
   input  so_n,
   input  [7:0] di,
   input  [63:0] savestatebus_din,
   input  [9:0] savestatebus_adr,
   input  savestatebus_wren,
   input  savestatebus_rst,
   input  savestatebus_load,
   output r_w_n,
   output sync,
   output ef,
   output mf,
   output xf,
   output ml_n,
   output vp_n,
   output vda,
   output vpa,
   output [23:0] a,
   output [7:0] dout,
   output [63:0] regs,
   output [7:0] debug_i,
   output [7:0] debug_a,
   output [7:0] debug_x,
   output [7:0] debug_y,
   output [7:0] debug_s,
   output [7:0] debug_p,
   output nmi_ack,
   output instrnew,
   output [63:0] savestatebus_dout);
  wire [7:0] n32_o;
  wire [7:0] n33_o;
  wire [7:0] n34_o;
  wire [7:0] n35_o;
  wire [7:0] n36_o;
  wire [7:0] n37_o;
  wire [15:0] abc;
  wire [15:0] x;
  wire [15:0] y;
  reg [7:0] p;
  reg [7:0] ad;
  reg [7:0] dl;
  wire [7:0] pwithb;
  wire [7:0] bah;
  wire [8:0] bal;
  wire [7:0] pbr;
  wire [7:0] dbr;
  wire [15:0] pc;
  wire [15:0] s;
  wire ef_i;
  wire mf_i;
  wire xf_i;
  wire [7:0] ir;
  wire [2:0] mcycle;
  wire [7:0] do_r;
  wire [1:0] mode_r;
  wire bcd_en_r;
  wire [4:0] alu_op_r;
  wire [3:0] write_data_r;
  wire [1:0] set_addr_to_r;
  wire [8:0] pcadder;
  wire rstcycle;
  wire irqcycle;
  wire nmicycle;
  wire so_n_o;
  wire irq_n_o;
  wire nmi_n_o;
  wire nmiact;
  wire brks;
  wire [7:0] busa;
  wire [7:0] busa_r;
  wire [7:0] busb;
  wire [7:0] busb_r;
  wire [7:0] aluqs;
  wire [7:0] p_out;
  wire [2:0] lcycle;
  wire [4:0] alu_op;
  wire [3:0] set_busa_to;
  wire [1:0] set_addr_to;
  wire [3:0] write_data;
  wire [1:0] jump;
  wire [1:0] baadd;
  wire [1:0] baquirk;
  wire breakatna;
  wire adadd;
  wire addy;
  wire pcadd;
  wire inc_s;
  wire dec_s;
  wire lda;
  wire ldp;
  wire ldx;
  wire ldy;
  wire lds;
  wire lddi;
  wire ldalu;
  wire ldad;
  wire ldbal;
  wire ldbah;
  wire savep;
  wire write;
  wire res_n_i;
  wire res_n_d;
  wire rdy_mod;
  wire really_rdy;
  wire wrn_i;
  wire nmi_entered;
  wire [63:0] ss_1;
  wire [63:0] ss_2;
  wire [63:0] ss_3;
  localparam [63:0] n47_o = 64'b0000000000000000000000000000000000000000000000000000000000000000;
  wire n48_o;
  wire n49_o;
  wire n52_o;
  wire n53_o;
  wire [1:0] n56_o;
  wire n58_o;
  wire [1:0] n59_o;
  wire n61_o;
  wire n62_o;
  wire [1:0] n63_o;
  wire n65_o;
  wire n66_o;
  wire n67_o;
  wire n71_o;
  wire n73_o;
  wire n74_o;
  wire n75_o;
  wire n76_o;
  wire n80_o;
  wire n81_o;
  wire n84_o;
  wire n85_o;
  wire n86_o;
  wire [7:0] n88_o;
  wire [7:0] n89_o;
  wire [7:0] n90_o;
  wire [7:0] n91_o;
  wire [31:0] n92_o;
  wire [39:0] n93_o;
  wire [7:0] n94_o;
  wire [47:0] n95_o;
  wire [7:0] n96_o;
  wire [55:0] n97_o;
  wire [7:0] n98_o;
  wire [63:0] n99_o;
  wire [2:0] mcode_lcycle;
  wire [4:0] mcode_alu_op;
  wire [3:0] mcode_set_busa_to;
  wire [1:0] mcode_set_addr_to;
  wire [3:0] mcode_write_data;
  wire [1:0] mcode_jump;
  wire [1:0] mcode_baadd;
  wire [1:0] mcode_baquirk;
  wire mcode_breakatna;
  wire mcode_adadd;
  wire mcode_addy;
  wire mcode_pcadd;
  wire mcode_inc_s;
  wire mcode_dec_s;
  wire mcode_lda;
  wire mcode_ldp;
  wire mcode_ldx;
  wire mcode_ldy;
  wire mcode_lds;
  wire mcode_lddi;
  wire mcode_ldalu;
  wire mcode_ldad;
  wire mcode_ldbal;
  wire mcode_ldbah;
  wire mcode_savep;
  wire mcode_write;
  wire [7:0] alu_p_out;
  wire [7:0] alu_q;
  wire n130_o;
  wire n152_o;
  wire n153_o;
  wire [15:0] n154_o;
  wire [7:0] n155_o;
  wire [7:0] n156_o;
  wire [7:0] n157_o;
  wire [1:0] n158_o;
  wire n159_o;
  wire [3:0] n160_o;
  wire [4:0] n162_o;
  wire [3:0] n163_o;
  wire [1:0] n165_o;
  wire n167_o;
  wire n168_o;
  wire n169_o;
  wire n170_o;
  wire [7:0] n172_o;
  wire n173_o;
  wire n175_o;
  wire n177_o;
  wire n179_o;
  wire n180_o;
  wire n182_o;
  wire n184_o;
  wire n185_o;
  wire n186_o;
  wire n187_o;
  wire n188_o;
  wire n190_o;
  wire n192_o;
  wire n193_o;
  wire n194_o;
  wire n196_o;
  wire n197_o;
  wire n198_o;
  wire n199_o;
  wire [15:0] n201_o;
  wire [15:0] n202_o;
  wire n203_o;
  wire [7:0] n205_o;
  wire [7:0] n206_o;
  wire [7:0] n207_o;
  wire n208_o;
  wire n210_o;
  wire [7:0] n211_o;
  wire [1:0] n212_o;
  wire n213_o;
  wire [1:0] n215_o;
  wire [15:0] n217_o;
  wire [7:0] n218_o;
  wire [15:0] n219_o;
  wire [15:0] n220_o;
  wire n221_o;
  wire n223_o;
  wire n224_o;
  wire n225_o;
  wire [15:0] n227_o;
  wire [15:0] n228_o;
  wire n230_o;
  wire n232_o;
  wire n233_o;
  wire n234_o;
  wire n235_o;
  wire n236_o;
  wire n237_o;
  wire [15:0] n239_o;
  wire [15:0] n240_o;
  wire [15:0] n242_o;
  wire n244_o;
  wire [15:0] n245_o;
  wire n247_o;
  wire n248_o;
  wire n249_o;
  wire n250_o;
  wire [7:0] n251_o;
  wire [7:0] n253_o;
  wire [7:0] n254_o;
  wire [7:0] n256_o;
  wire [7:0] n257_o;
  wire [7:0] n258_o;
  wire [7:0] n259_o;
  wire [7:0] n260_o;
  wire n262_o;
  wire [2:0] n263_o;
  wire [7:0] n264_o;
  wire [7:0] n265_o;
  wire [7:0] n266_o;
  reg [7:0] n267_o;
  wire [7:0] n268_o;
  wire [7:0] n269_o;
  wire [7:0] n270_o;
  reg [7:0] n271_o;
  wire [7:0] n274_o;
  wire [7:0] n276_o;
  wire [15:0] n277_o;
  wire [15:0] n278_o;
  wire [15:0] n279_o;
  wire n281_o;
  wire n283_o;
  wire n285_o;
  wire n286_o;
  wire n287_o;
  wire n288_o;
  wire [4:0] n289_o;
  wire [3:0] n290_o;
  wire [1:0] n291_o;
  wire n292_o;
  wire n293_o;
  wire n294_o;
  wire n295_o;
  wire n296_o;
  wire n297_o;
  wire n298_o;
  wire n299_o;
  wire n300_o;
  wire n301_o;
  wire n302_o;
  wire n303_o;
  wire n304_o;
  wire n305_o;
  wire n306_o;
  wire n307_o;
  wire [7:0] n308_o;
  wire [7:0] n309_o;
  wire [15:0] n310_o;
  wire [15:0] n311_o;
  wire [15:0] n312_o;
  wire n313_o;
  wire n314_o;
  wire n315_o;
  wire [7:0] n316_o;
  wire [1:0] n317_o;
  wire n318_o;
  wire [4:0] n319_o;
  wire [3:0] n320_o;
  wire [1:0] n321_o;
  wire n322_o;
  wire n323_o;
  wire [7:0] n324_o;
  wire [7:0] n325_o;
  wire [15:0] n326_o;
  wire [15:0] n327_o;
  wire n328_o;
  wire n329_o;
  wire n330_o;
  wire [7:0] n331_o;
  wire [1:0] n332_o;
  wire n333_o;
  wire [4:0] n334_o;
  wire [3:0] n335_o;
  wire [1:0] n336_o;
  wire n337_o;
  wire n338_o;
  wire [7:0] n339_o;
  wire [7:0] n340_o;
  wire [15:0] n341_o;
  wire [15:0] n343_o;
  wire n344_o;
  wire n345_o;
  wire n346_o;
  wire [7:0] n347_o;
  wire [1:0] n348_o;
  wire n349_o;
  wire [4:0] n350_o;
  wire [3:0] n351_o;
  wire [1:0] n352_o;
  wire n353_o;
  wire n354_o;
  wire [7:0] n371_o;
  wire [8:0] n372_o;
  wire n373_o;
  wire [8:0] n374_o;
  wire [8:0] n375_o;
  wire [8:0] n376_o;
  wire [7:0] n377_o;
  wire [8:0] n379_o;
  wire n387_o;
  wire [7:0] n388_o;
  wire [7:0] n389_o;
  wire [7:0] n390_o;
  wire [7:0] n391_o;
  wire n392_o;
  wire n393_o;
  wire n396_o;
  wire [7:0] n397_o;
  wire [7:0] n398_o;
  wire [7:0] n399_o;
  wire [7:0] n400_o;
  wire [7:0] n401_o;
  wire [7:0] n402_o;
  wire n403_o;
  wire n404_o;
  wire [7:0] n405_o;
  wire n407_o;
  wire n409_o;
  wire n411_o;
  wire n412_o;
  wire [7:0] n413_o;
  wire [7:0] n414_o;
  wire [4:0] n415_o;
  wire n417_o;
  wire [2:0] n418_o;
  wire n421_o;
  wire n424_o;
  wire n427_o;
  wire n430_o;
  wire n433_o;
  wire n436_o;
  wire n439_o;
  wire [6:0] n440_o;
  wire n441_o;
  reg n442_o;
  wire n443_o;
  reg n444_o;
  wire n445_o;
  reg n446_o;
  wire n447_o;
  reg n448_o;
  wire [1:0] n449_o;
  wire n450_o;
  wire n451_o;
  wire [1:0] n452_o;
  wire [1:0] n453_o;
  wire n454_o;
  wire n455_o;
  wire n458_o;
  wire n459_o;
  wire n464_o;
  wire n466_o;
  wire n467_o;
  wire n468_o;
  wire n469_o;
  wire n471_o;
  wire n472_o;
  wire n473_o;
  wire n476_o;
  wire n478_o;
  wire [1:0] n479_o;
  wire [1:0] n480_o;
  wire [1:0] n481_o;
  wire [7:0] n483_o;
  wire n485_o;
  wire n487_o;
  wire n489_o;
  wire [7:0] n490_o;
  wire [4:0] n493_o;
  wire n495_o;
  wire n497_o;
  wire n498_o;
  wire n499_o;
  wire n500_o;
  wire n501_o;
  wire [4:0] n502_o;
  wire n504_o;
  wire n506_o;
  wire n507_o;
  wire n508_o;
  wire n510_o;
  wire n512_o;
  wire n514_o;
  wire n515_o;
  wire n516_o;
  wire n517_o;
  wire [7:0] n519_o;
  wire [7:0] n520_o;
  wire [7:0] n521_o;
  wire [7:0] n522_o;
  wire n524_o;
  wire n525_o;
  wire [7:0] n528_o;
  wire [7:0] n530_o;
  wire [7:0] n531_o;
  wire [7:0] n533_o;
  wire [7:0] n535_o;
  wire [7:0] n536_o;
  wire [7:0] n538_o;
  wire [7:0] n540_o;
  wire [7:0] n541_o;
  wire [7:0] n543_o;
  wire n545_o;
  wire n546_o;
  wire n548_o;
  wire n549_o;
  wire n551_o;
  wire n552_o;
  wire n553_o;
  wire [5:0] n554_o;
  wire [15:0] n555_o;
  wire [15:0] n557_o;
  wire [15:0] n559_o;
  wire [7:0] n561_o;
  wire n573_o;
  wire [7:0] n574_o;
  wire [7:0] n575_o;
  wire [7:0] n576_o;
  wire [7:0] n577_o;
  wire [8:0] n578_o;
  wire [7:0] n579_o;
  wire [7:0] n580_o;
  wire n581_o;
  wire n583_o;
  wire n585_o;
  wire n586_o;
  wire [7:0] n588_o;
  wire [7:0] n589_o;
  wire [7:0] n591_o;
  wire [8:0] n593_o;
  wire n595_o;
  wire [7:0] n596_o;
  wire [8:0] n597_o;
  wire [8:0] n598_o;
  wire [8:0] n599_o;
  wire n601_o;
  wire n602_o;
  wire [7:0] n604_o;
  wire n606_o;
  wire [7:0] n608_o;
  wire [7:0] n609_o;
  wire n611_o;
  wire n613_o;
  wire [2:0] n614_o;
  reg [7:0] n615_o;
  wire [7:0] n616_o;
  wire n618_o;
  wire [2:0] n619_o;
  reg [7:0] n620_o;
  reg [7:0] n621_o;
  reg [8:0] n622_o;
  wire [7:0] n623_o;
  wire [7:0] n624_o;
  wire [7:0] n625_o;
  wire [7:0] n626_o;
  wire [7:0] n627_o;
  wire [7:0] n628_o;
  wire n630_o;
  wire n633_o;
  wire n634_o;
  wire n635_o;
  wire n636_o;
  wire n639_o;
  wire n642_o;
  wire [2:0] n644_o;
  wire n646_o;
  wire [2:0] n647_o;
  localparam [8:0] n648_o = 9'b111111111;
  wire [5:0] n649_o;
  wire n651_o;
  wire n653_o;
  wire n655_o;
  wire n656_o;
  wire [1:0] n657_o;
  wire [7:0] n659_o;
  wire [8:0] n660_o;
  wire [8:0] n661_o;
  wire n663_o;
  wire [7:0] n665_o;
  wire [7:0] n666_o;
  wire [7:0] n667_o;
  wire [7:0] n668_o;
  wire [7:0] n669_o;
  wire n670_o;
  wire [7:0] n671_o;
  wire [7:0] n672_o;
  wire [7:0] n673_o;
  wire [7:0] n674_o;
  wire [8:0] n675_o;
  wire [8:0] n676_o;
  wire [7:0] n677_o;
  wire [7:0] n678_o;
  wire n679_o;
  wire n680_o;
  wire n681_o;
  wire n682_o;
  wire n683_o;
  wire n684_o;
  wire n685_o;
  wire n686_o;
  wire n687_o;
  wire n688_o;
  wire [7:0] n689_o;
  wire [7:0] n690_o;
  wire [7:0] n691_o;
  wire [8:0] n692_o;
  wire [7:0] n693_o;
  wire [7:0] n694_o;
  wire [7:0] n695_o;
  wire n696_o;
  wire [7:0] n697_o;
  wire [7:0] n698_o;
  wire [7:0] n699_o;
  wire [8:0] n700_o;
  wire [7:0] n701_o;
  wire [7:0] n702_o;
  wire [7:0] n703_o;
  wire n704_o;
  wire n714_o;
  wire n715_o;
  wire n716_o;
  wire n717_o;
  wire n718_o;
  wire n719_o;
  wire n720_o;
  wire n722_o;
  wire [7:0] n723_o;
  wire n725_o;
  wire [7:0] n726_o;
  wire n728_o;
  wire [7:0] n729_o;
  wire n731_o;
  wire [7:0] n732_o;
  wire n734_o;
  wire n736_o;
  wire [7:0] n737_o;
  wire [7:0] n738_o;
  wire n740_o;
  wire [7:0] n741_o;
  wire [7:0] n743_o;
  wire [7:0] n744_o;
  wire n746_o;
  wire [7:0] n747_o;
  wire [7:0] n749_o;
  wire [7:0] n750_o;
  wire [7:0] n751_o;
  wire [7:0] n752_o;
  wire n754_o;
  wire [7:0] n755_o;
  wire [7:0] n756_o;
  wire [7:0] n757_o;
  wire n759_o;
  wire n762_o;
  wire [10:0] n763_o;
  reg [7:0] n765_o;
  wire [7:0] n766_o;
  wire [23:0] n768_o;
  wire n770_o;
  wire [15:0] n772_o;
  wire [23:0] n773_o;
  wire n775_o;
  wire [15:0] n777_o;
  wire [7:0] n778_o;
  wire [23:0] n779_o;
  wire n781_o;
  wire [7:0] n782_o;
  wire [15:0] n783_o;
  wire [7:0] n784_o;
  wire [23:0] n785_o;
  wire n787_o;
  wire [3:0] n788_o;
  reg [23:0] n790_o;
  wire [7:0] n792_o;
  wire n793_o;
  wire [7:0] n794_o;
  wire n796_o;
  wire [7:0] n797_o;
  wire n799_o;
  wire [7:0] n800_o;
  wire n802_o;
  wire [7:0] n803_o;
  wire n805_o;
  wire [7:0] n806_o;
  wire n808_o;
  wire n810_o;
  wire [7:0] n811_o;
  wire n813_o;
  wire [7:0] n814_o;
  wire n816_o;
  wire [7:0] n817_o;
  wire [7:0] n818_o;
  wire [7:0] n819_o;
  wire n821_o;
  wire [7:0] n822_o;
  wire [7:0] n823_o;
  wire [7:0] n824_o;
  wire [7:0] n825_o;
  wire n827_o;
  wire [7:0] n828_o;
  wire [7:0] n829_o;
  wire n831_o;
  wire [7:0] n832_o;
  wire [7:0] n833_o;
  wire n835_o;
  wire n838_o;
  wire [12:0] n839_o;
  reg [7:0] n841_o;
  wire n844_o;
  wire n845_o;
  wire n846_o;
  wire n847_o;
  wire n852_o;
  wire [2:0] n853_o;
  wire n854_o;
  wire n855_o;
  wire n856_o;
  wire n857_o;
  wire n858_o;
  wire n859_o;
  wire n861_o;
  wire n862_o;
  wire n863_o;
  wire n864_o;
  wire n865_o;
  wire n866_o;
  wire n869_o;
  wire n871_o;
  wire n875_o;
  wire n878_o;
  wire n880_o;
  wire [2:0] n882_o;
  wire [2:0] n883_o;
  wire [2:0] n885_o;
  wire n887_o;
  wire n888_o;
  wire n889_o;
  wire n890_o;
  wire [2:0] n891_o;
  wire n892_o;
  wire n893_o;
  wire n894_o;
  wire n895_o;
  wire n896_o;
  wire [4:0] n897_o;
  wire n899_o;
  wire n901_o;
  wire n902_o;
  wire n903_o;
  wire n904_o;
  wire n906_o;
  wire n908_o;
  wire n909_o;
  wire n910_o;
  wire n911_o;
  wire n912_o;
  wire n913_o;
  wire [2:0] n914_o;
  wire n915_o;
  wire n916_o;
  wire n917_o;
  wire n918_o;
  reg [15:0] n925_q;
  reg [15:0] n926_q;
  reg [15:0] n927_q;
  reg [7:0] n928_q;
  reg [7:0] n929_q;
  reg [7:0] n930_q;
  reg [7:0] n931_q;
  reg [8:0] n932_q;
  reg [7:0] n933_q;
  reg [7:0] n934_q;
  reg [15:0] n935_q;
  reg [15:0] n936_q;
  reg n937_q;
  reg n938_q;
  reg n939_q;
  reg [7:0] n940_q;
  reg [2:0] n941_q;
  reg [1:0] n942_q;
  reg n943_q;
  reg [4:0] n944_q;
  reg [3:0] n945_q;
  reg [1:0] n946_q;
  reg n947_q;
  reg n948_q;
  reg n949_q;
  reg n950_q;
  reg n951_q;
  reg n952_q;
  reg n953_q;
  reg [7:0] n954_q;
  reg [7:0] n955_q;
  reg [7:0] n956_q;
  wire n957_o;
  reg n958_q;
  wire n959_o;
  reg n960_q;
  reg n961_q;
  reg n962_q;
  reg n963_q;
  wire [47:0] n970_o;
  assign r_w_n = wrn_i; //(module output)
  assign sync = n53_o; //(module output)
  assign ef = ef_i; //(module output)
  assign mf = mf_i; //(module output)
  assign xf = xf_i; //(module output)
  assign ml_n = n67_o; //(module output)
  assign vp_n = n76_o; //(module output)
  assign vda = n81_o; //(module output)
  assign vpa = n86_o; //(module output)
  assign a = n790_o; //(module output)
  assign dout = do_r; //(module output)
  assign regs = n99_o; //(module output)
  assign debug_i = n32_o; //(module output)
  assign debug_a = n33_o; //(module output)
  assign debug_x = n34_o; //(module output)
  assign debug_y = n35_o; //(module output)
  assign debug_s = n36_o; //(module output)
  assign debug_p = n37_o; //(module output)
  assign nmi_ack = nmiact; //(module output)
  assign instrnew = n847_o; //(module output)
  assign savestatebus_dout = n47_o; //(module output)
  assign n32_o = n970_o[7:0]; // extract
  assign n33_o = n970_o[15:8]; // extract
  assign n34_o = n970_o[23:16]; // extract
  assign n35_o = n970_o[31:24]; // extract
  assign n36_o = n970_o[39:32]; // extract
  assign n37_o = n970_o[47:40]; // extract
  /* T65.vhd:179:10  */
  assign abc = n925_q; // (signal)
  /* T65.vhd:179:15  */
  assign x = n926_q; // (signal)
  /* T65.vhd:179:18  */
  assign y = n927_q; // (signal)
  /* T65.vhd:180:10  */
  always @*
    p = n928_q; // (isignal)
  initial
    p = 8'b00000000;
  /* T65.vhd:180:13  */
  always @*
    ad = n929_q; // (isignal)
  initial
    ad = 8'b00000000;
  /* T65.vhd:180:17  */
  always @*
    dl = n930_q; // (isignal)
  initial
    dl = 8'b00000000;
  /* T65.vhd:181:10  */
  assign pwithb = n794_o; // (signal)
  /* T65.vhd:182:10  */
  assign bah = n931_q; // (signal)
  /* T65.vhd:183:10  */
  assign bal = n932_q; // (signal)
  /* T65.vhd:184:10  */
  assign pbr = n933_q; // (signal)
  /* T65.vhd:185:10  */
  assign dbr = n934_q; // (signal)
  /* T65.vhd:186:10  */
  assign pc = n935_q; // (signal)
  /* T65.vhd:187:10  */
  assign s = n936_q; // (signal)
  /* T65.vhd:188:10  */
  assign ef_i = n937_q; // (signal)
  /* T65.vhd:189:10  */
  assign mf_i = n938_q; // (signal)
  /* T65.vhd:190:10  */
  assign xf_i = n939_q; // (signal)
  /* T65.vhd:192:10  */
  assign ir = n940_q; // (signal)
  /* T65.vhd:193:10  */
  assign mcycle = n941_q; // (signal)
  /* T65.vhd:195:10  */
  assign do_r = n841_o; // (signal)
  /* T65.vhd:197:10  */
  assign mode_r = n942_q; // (signal)
  /* T65.vhd:198:10  */
  assign bcd_en_r = n943_q; // (signal)
  /* T65.vhd:199:10  */
  assign alu_op_r = n944_q; // (signal)
  /* T65.vhd:200:10  */
  assign write_data_r = n945_q; // (signal)
  /* T65.vhd:201:10  */
  assign set_addr_to_r = n946_q; // (signal)
  /* T65.vhd:202:10  */
  assign pcadder = n376_o; // (signal)
  /* T65.vhd:204:10  */
  assign rstcycle = n947_q; // (signal)
  /* T65.vhd:205:10  */
  assign irqcycle = n948_q; // (signal)
  /* T65.vhd:206:10  */
  assign nmicycle = n949_q; // (signal)
  /* T65.vhd:208:10  */
  assign so_n_o = n950_q; // (signal)
  /* T65.vhd:209:10  */
  assign irq_n_o = n951_q; // (signal)
  /* T65.vhd:210:10  */
  assign nmi_n_o = n952_q; // (signal)
  /* T65.vhd:211:10  */
  assign nmiact = n953_q; // (signal)
  /* T65.vhd:213:10  */
  assign brks = n720_o; // (signal)
  /* T65.vhd:216:10  */
  assign busa = n765_o; // (signal)
  /* T65.vhd:217:10  */
  assign busa_r = n954_q; // (signal)
  /* T65.vhd:218:10  */
  assign busb = n955_q; // (signal)
  /* T65.vhd:219:10  */
  assign busb_r = n956_q; // (signal)
  /* T65.vhd:220:10  */
  assign aluqs = alu_q; // (signal)
  /* T65.vhd:221:10  */
  assign p_out = alu_p_out; // (signal)
  /* T65.vhd:224:10  */
  assign lcycle = mcode_lcycle; // (signal)
  /* T65.vhd:225:10  */
  assign alu_op = mcode_alu_op; // (signal)
  /* T65.vhd:226:10  */
  assign set_busa_to = mcode_set_busa_to; // (signal)
  /* T65.vhd:227:10  */
  assign set_addr_to = mcode_set_addr_to; // (signal)
  /* T65.vhd:228:10  */
  assign write_data = mcode_write_data; // (signal)
  /* T65.vhd:229:10  */
  assign jump = mcode_jump; // (signal)
  /* T65.vhd:230:10  */
  assign baadd = mcode_baadd; // (signal)
  /* T65.vhd:231:10  */
  assign baquirk = mcode_baquirk; // (signal)
  /* T65.vhd:232:10  */
  assign breakatna = mcode_breakatna; // (signal)
  /* T65.vhd:233:10  */
  assign adadd = mcode_adadd; // (signal)
  /* T65.vhd:234:10  */
  assign addy = mcode_addy; // (signal)
  /* T65.vhd:235:10  */
  assign pcadd = mcode_pcadd; // (signal)
  /* T65.vhd:236:10  */
  assign inc_s = mcode_inc_s; // (signal)
  /* T65.vhd:237:10  */
  assign dec_s = mcode_dec_s; // (signal)
  /* T65.vhd:238:10  */
  assign lda = mcode_lda; // (signal)
  /* T65.vhd:239:10  */
  assign ldp = mcode_ldp; // (signal)
  /* T65.vhd:240:10  */
  assign ldx = mcode_ldx; // (signal)
  /* T65.vhd:241:10  */
  assign ldy = mcode_ldy; // (signal)
  /* T65.vhd:242:10  */
  assign lds = mcode_lds; // (signal)
  /* T65.vhd:243:10  */
  assign lddi = mcode_lddi; // (signal)
  /* T65.vhd:244:10  */
  assign ldalu = mcode_ldalu; // (signal)
  /* T65.vhd:245:10  */
  assign ldad = mcode_ldad; // (signal)
  /* T65.vhd:246:10  */
  assign ldbal = mcode_ldbal; // (signal)
  /* T65.vhd:247:10  */
  assign ldbah = mcode_ldbah; // (signal)
  /* T65.vhd:248:10  */
  assign savep = mcode_savep; // (signal)
  /* T65.vhd:249:10  */
  assign write = mcode_write; // (signal)
  /* T65.vhd:251:10  */
  assign res_n_i = n958_q; // (signal)
  /* T65.vhd:252:10  */
  assign res_n_d = n960_q; // (signal)
  /* T65.vhd:254:10  */
  assign rdy_mod = n961_q; // (signal)
  /* T65.vhd:255:10  */
  assign really_rdy = n49_o; // (signal)
  /* T65.vhd:256:10  */
  assign wrn_i = n962_q; // (signal)
  /* T65.vhd:258:10  */
  assign nmi_entered = n963_q; // (signal)
  /* T65.vhd:265:16  */
  assign ss_1 = 64'b1110000001100100000000000000000000000000000000000000000000000000; // (signal)
  /* T65.vhd:267:16  */
  assign ss_2 = 64'b0000000000000000000000010010000000000000000000000000000000000001; // (signal)
  /* T65.vhd:269:16  */
  assign ss_3 = 64'b0000000000000000000000000000000000000000000000000000000000000000; // (signal)
  /* T65.vhd:281:24  */
  assign n48_o = ~wrn_i;
  /* T65.vhd:281:21  */
  assign n49_o = rdy | n48_o;
  /* T65.vhd:282:27  */
  assign n52_o = mcycle == 3'b000;
  /* T65.vhd:282:15  */
  assign n53_o = n52_o ? 1'b1 : 1'b0;
  /* T65.vhd:287:22  */
  assign n56_o = ir[7:6]; // extract
  /* T65.vhd:287:35  */
  assign n58_o = n56_o != 2'b10;
  /* T65.vhd:287:49  */
  assign n59_o = ir[2:1]; // extract
  /* T65.vhd:287:62  */
  assign n61_o = n59_o == 2'b11;
  /* T65.vhd:287:43  */
  assign n62_o = n61_o & n58_o;
  /* T65.vhd:287:79  */
  assign n63_o = mcycle[2:1]; // extract
  /* T65.vhd:287:92  */
  assign n65_o = n63_o != 2'b00;
  /* T65.vhd:287:69  */
  assign n66_o = n65_o & n62_o;
  /* T65.vhd:287:15  */
  assign n67_o = n66_o ? 1'b0 : 1'b1;
  /* T65.vhd:288:47  */
  assign n71_o = mcycle == 3'b101;
  /* T65.vhd:288:65  */
  assign n73_o = mcycle == 3'b110;
  /* T65.vhd:288:55  */
  assign n74_o = n71_o | n73_o;
  /* T65.vhd:288:35  */
  assign n75_o = n74_o & irqcycle;
  /* T65.vhd:288:15  */
  assign n76_o = n75_o ? 1'b0 : 1'b1;
  /* T65.vhd:289:33  */
  assign n80_o = set_addr_to_r != 2'b00;
  /* T65.vhd:289:14  */
  assign n81_o = n80_o ? 1'b1 : 1'b0;
  /* T65.vhd:290:23  */
  assign n84_o = jump[1]; // extract
  /* T65.vhd:290:27  */
  assign n85_o = ~n84_o;
  /* T65.vhd:290:14  */
  assign n86_o = n85_o ? 1'b1 : 1'b0;
  /* T65.vhd:294:17  */
  assign n88_o = abc[7:0]; // extract
  /* T65.vhd:295:15  */
  assign n89_o = x[7:0]; // extract
  /* T65.vhd:296:15  */
  assign n90_o = y[7:0]; // extract
  /* T65.vhd:297:32  */
  assign n91_o = s[7:0]; // extract
  /* T65.vhd:300:32  */
  assign n92_o = {pc, s};
  /* T65.vhd:300:53  */
  assign n93_o = {n92_o, p};
  /* T65.vhd:300:60  */
  assign n94_o = y[7:0]; // extract
  /* T65.vhd:300:57  */
  assign n95_o = {n93_o, n94_o};
  /* T65.vhd:300:76  */
  assign n96_o = x[7:0]; // extract
  /* T65.vhd:300:73  */
  assign n97_o = {n95_o, n96_o};
  /* T65.vhd:300:94  */
  assign n98_o = abc[7:0]; // extract
  /* T65.vhd:300:89  */
  assign n99_o = {n97_o, n98_o};
  /* T65.vhd:302:3  */
  t65_mcode mcode (
    .mode(mode_r),
    .bcd_en(bcd_en_r),
    .ir(ir),
    .mcycle(mcycle),
    .p(p),
    .rdy_mod(rdy_mod),
    .lcycle(mcode_lcycle),
    .alu_op(mcode_alu_op),
    .set_busa_to(mcode_set_busa_to),
    .set_addr_to(mcode_set_addr_to),
    .write_data(mcode_write_data),
    .jump(mcode_jump),
    .baadd(mcode_baadd),
    .baquirk(mcode_baquirk),
    .breakatna(mcode_breakatna),
    .adadd(mcode_adadd),
    .addy(mcode_addy),
    .pcadd(mcode_pcadd),
    .inc_s(mcode_inc_s),
    .dec_s(mcode_dec_s),
    .lda(mcode_lda),
    .ldp(mcode_ldp),
    .ldx(mcode_ldx),
    .ldy(mcode_ldy),
    .lds(mcode_lds),
    .lddi(mcode_lddi),
    .ldalu(mcode_ldalu),
    .ldad(mcode_ldad),
    .ldbal(mcode_ldbal),
    .ldbah(mcode_ldbah),
    .savep(mcode_savep),
    .write(mcode_write));
  /* T65.vhd:340:3  */
  t65_alu alu (
    .mode(mode_r),
    .bcd_en(bcd_en_r),
    .op(alu_op_r),
    .busa(busa_r),
    .busb(busb),
    .p_in(p),
    .p_out(alu_p_out),
    .q(alu_q));
  /* T65.vhd:356:14  */
  assign n130_o = ~res_n;
  /* T65.vhd:385:16  */
  assign n152_o = ~pwr_n;
  /* T65.vhd:387:21  */
  assign n153_o = ~res_n_i;
  /* T65.vhd:388:40  */
  assign n154_o = ss_1[15:0]; // extract
  /* T65.vhd:389:31  */
  assign n155_o = ss_1[23:16]; // extract
  /* T65.vhd:390:31  */
  assign n156_o = ss_1[39:32]; // extract
  /* T65.vhd:391:31  */
  assign n157_o = ss_1[47:40]; // extract
  /* T65.vhd:392:31  */
  assign n158_o = ss_1[49:48]; // extract
  /* T65.vhd:393:31  */
  assign n159_o = ss_1[50]; // extract
  /* T65.vhd:394:64  */
  assign n160_o = ss_1[54:51]; // extract
  /* T65.vhd:394:36  */
  assign n162_o = {1'b0, n160_o};  //  uext
  /* T65.vhd:395:68  */
  assign n163_o = ss_1[58:55]; // extract
  /* T65.vhd:396:69  */
  assign n165_o = ss_1[60:59]; // extract
  /* T65.vhd:397:31  */
  assign n167_o = ss_1[61]; // extract
  /* T65.vhd:398:31  */
  assign n168_o = ss_1[62]; // extract
  /* T65.vhd:399:31  */
  assign n169_o = ss_1[63]; // extract
  /* T65.vhd:400:31  */
  assign n170_o = ss_2[0]; // extract
  /* T65.vhd:404:40  */
  assign n172_o = ss_1[31:24]; // extract
  /* T65.vhd:405:24  */
  assign n173_o = ss_2[1]; // extract
  /* T65.vhd:409:20  */
  assign n175_o = mcycle == 3'b000;
  /* T65.vhd:411:24  */
  assign n177_o = mcycle == 3'b011;
  /* T65.vhd:411:39  */
  assign n179_o = ir != 8'b10010011;
  /* T65.vhd:411:32  */
  assign n180_o = n179_o & n177_o;
  /* T65.vhd:411:60  */
  assign n182_o = mcycle == 3'b100;
  /* T65.vhd:411:75  */
  assign n184_o = ir == 8'b10010011;
  /* T65.vhd:411:68  */
  assign n185_o = n184_o & n182_o;
  /* T65.vhd:411:49  */
  assign n186_o = n180_o | n185_o;
  /* T65.vhd:411:93  */
  assign n187_o = ~rdy;
  /* T65.vhd:411:85  */
  assign n188_o = n187_o & n186_o;
  /* T65.vhd:411:9  */
  assign n190_o = n188_o ? 1'b1 : rdy_mod;
  /* T65.vhd:409:9  */
  assign n192_o = n175_o ? 1'b0 : n190_o;
  /* T65.vhd:416:20  */
  assign n193_o = ~write;
  /* T65.vhd:416:30  */
  assign n194_o = n193_o | rstcycle;
  /* T65.vhd:424:22  */
  assign n196_o = mcycle == 3'b000;
  /* T65.vhd:428:25  */
  assign n197_o = ~irqcycle;
  /* T65.vhd:428:44  */
  assign n198_o = ~nmicycle;
  /* T65.vhd:428:31  */
  assign n199_o = n198_o & n197_o;
  /* T65.vhd:429:24  */
  assign n201_o = pc + 16'b0000000000000001;
  /* T65.vhd:424:11  */
  assign n202_o = n208_o ? n201_o : pc;
  /* T65.vhd:432:31  */
  assign n203_o = irqcycle | nmicycle;
  /* T65.vhd:432:13  */
  assign n205_o = n203_o ? 8'b00000000 : di;
  assign n206_o = s[7:0]; // extract
  /* T65.vhd:424:11  */
  assign n207_o = n210_o ? aluqs : n206_o;
  /* T65.vhd:424:11  */
  assign n208_o = n199_o & n196_o;
  /* T65.vhd:424:11  */
  assign n210_o = lds & n196_o;
  /* T65.vhd:407:7  */
  assign n211_o = n300_o ? n205_o : ir;
  /* T65.vhd:407:7  */
  assign n212_o = n301_o ? mode : mode_r;
  /* T65.vhd:407:7  */
  assign n213_o = n302_o ? bcd_en : bcd_en_r;
  /* T65.vhd:445:11  */
  assign n215_o = brks ? 2'b00 : set_addr_to;
  /* T65.vhd:452:20  */
  assign n217_o = s + 16'b0000000000000001;
  assign n218_o = s[15:8]; // extract
  assign n219_o = {n218_o, n207_o};
  /* T65.vhd:451:11  */
  assign n220_o = inc_s ? n217_o : n219_o;
  /* T65.vhd:454:40  */
  assign n221_o = ~rstcycle;
  /* T65.vhd:454:53  */
  assign n223_o = mode == 2'b00;
  /* T65.vhd:454:46  */
  assign n224_o = n221_o | n223_o;
  /* T65.vhd:454:26  */
  assign n225_o = n224_o & dec_s;
  /* T65.vhd:455:20  */
  assign n227_o = s - 16'b0000000000000001;
  /* T65.vhd:454:11  */
  assign n228_o = n225_o ? n227_o : n220_o;
  /* T65.vhd:458:17  */
  assign n230_o = ir == 8'b00000000;
  /* T65.vhd:458:41  */
  assign n232_o = mcycle == 3'b001;
  /* T65.vhd:458:30  */
  assign n233_o = n232_o & n230_o;
  /* T65.vhd:458:62  */
  assign n234_o = ~irqcycle;
  /* T65.vhd:458:49  */
  assign n235_o = n234_o & n233_o;
  /* T65.vhd:458:81  */
  assign n236_o = ~nmicycle;
  /* T65.vhd:458:68  */
  assign n237_o = n236_o & n235_o;
  /* T65.vhd:459:22  */
  assign n239_o = pc + 16'b0000000000000001;
  /* T65.vhd:458:11  */
  assign n240_o = n237_o ? n239_o : n202_o;
  /* T65.vhd:466:24  */
  assign n242_o = pc + 16'b0000000000000001;
  /* T65.vhd:465:13  */
  assign n244_o = jump == 2'b01;
  /* T65.vhd:468:33  */
  assign n245_o = {di, dl};
  /* T65.vhd:467:13  */
  assign n247_o = jump == 2'b10;
  /* T65.vhd:470:25  */
  assign n248_o = pcadder[8]; // extract
  /* T65.vhd:471:22  */
  assign n249_o = dl[7]; // extract
  /* T65.vhd:471:26  */
  assign n250_o = ~n249_o;
  /* T65.vhd:472:40  */
  assign n251_o = pc[15:8]; // extract
  /* T65.vhd:472:54  */
  assign n253_o = n251_o + 8'b00000001;
  /* T65.vhd:474:40  */
  assign n254_o = pc[15:8]; // extract
  /* T65.vhd:474:54  */
  assign n256_o = n254_o - 8'b00000001;
  /* T65.vhd:471:17  */
  assign n257_o = n250_o ? n253_o : n256_o;
  assign n258_o = n240_o[15:8]; // extract
  /* T65.vhd:470:15  */
  assign n259_o = n248_o ? n257_o : n258_o;
  /* T65.vhd:477:40  */
  assign n260_o = pcadder[7:0]; // extract
  /* T65.vhd:469:13  */
  assign n262_o = jump == 2'b11;
  assign n263_o = {n262_o, n247_o, n244_o};
  assign n264_o = n242_o[7:0]; // extract
  assign n265_o = n245_o[7:0]; // extract
  assign n266_o = n240_o[7:0]; // extract
  /* T65.vhd:464:11  */
  always @*
    case (n263_o)
      3'b100: n267_o = n260_o;
      3'b010: n267_o = n265_o;
      3'b001: n267_o = n264_o;
      default: n267_o = n266_o;
    endcase
  assign n268_o = n242_o[15:8]; // extract
  assign n269_o = n245_o[15:8]; // extract
  assign n270_o = n240_o[15:8]; // extract
  /* T65.vhd:464:11  */
  always @*
    case (n263_o)
      3'b100: n271_o = n259_o;
      3'b010: n271_o = n269_o;
      3'b001: n271_o = n268_o;
      default: n271_o = n270_o;
    endcase
  /* T65.vhd:407:7  */
  assign n274_o = n293_o ? 8'b11111111 : pbr;
  /* T65.vhd:407:7  */
  assign n276_o = n294_o ? 8'b11111111 : dbr;
  assign n277_o = {n271_o, n267_o};
  /* T65.vhd:407:7  */
  assign n278_o = n295_o ? n277_o : pc;
  /* T65.vhd:407:7  */
  assign n279_o = n296_o ? n228_o : s;
  /* T65.vhd:407:7  */
  assign n281_o = n297_o ? 1'b0 : ef_i;
  /* T65.vhd:407:7  */
  assign n283_o = n298_o ? 1'b0 : mf_i;
  /* T65.vhd:407:7  */
  assign n285_o = n299_o ? 1'b0 : xf_i;
  /* T65.vhd:415:9  */
  assign n286_o = n196_o & really_rdy;
  /* T65.vhd:415:9  */
  assign n287_o = n196_o & really_rdy;
  /* T65.vhd:415:9  */
  assign n288_o = n196_o & really_rdy;
  /* T65.vhd:407:7  */
  assign n289_o = n303_o ? alu_op : alu_op_r;
  /* T65.vhd:407:7  */
  assign n290_o = n304_o ? write_data : write_data_r;
  /* T65.vhd:407:7  */
  assign n291_o = n305_o ? n215_o : set_addr_to_r;
  /* T65.vhd:407:7  */
  assign n292_o = n307_o ? n194_o : wrn_i;
  /* T65.vhd:407:7  */
  assign n293_o = really_rdy & enable;
  /* T65.vhd:407:7  */
  assign n294_o = really_rdy & enable;
  /* T65.vhd:407:7  */
  assign n295_o = really_rdy & enable;
  /* T65.vhd:407:7  */
  assign n296_o = really_rdy & enable;
  /* T65.vhd:407:7  */
  assign n297_o = really_rdy & enable;
  /* T65.vhd:407:7  */
  assign n298_o = really_rdy & enable;
  /* T65.vhd:407:7  */
  assign n299_o = really_rdy & enable;
  /* T65.vhd:407:7  */
  assign n300_o = n286_o & enable;
  /* T65.vhd:407:7  */
  assign n301_o = n287_o & enable;
  /* T65.vhd:407:7  */
  assign n302_o = n288_o & enable;
  /* T65.vhd:407:7  */
  assign n303_o = really_rdy & enable;
  /* T65.vhd:407:7  */
  assign n304_o = really_rdy & enable;
  /* T65.vhd:407:7  */
  assign n305_o = really_rdy & enable;
  /* T65.vhd:407:7  */
  assign n306_o = enable ? n192_o : rdy_mod;
  /* T65.vhd:407:7  */
  assign n307_o = really_rdy & enable;
  /* T65.vhd:402:7  */
  assign n308_o = savestatebus_load ? pbr : n274_o;
  /* T65.vhd:402:7  */
  assign n309_o = savestatebus_load ? dbr : n276_o;
  /* T65.vhd:402:7  */
  assign n310_o = savestatebus_load ? pc : n278_o;
  assign n311_o = {8'b00000000, n172_o};
  /* T65.vhd:402:7  */
  assign n312_o = savestatebus_load ? n311_o : n279_o;
  /* T65.vhd:402:7  */
  assign n313_o = savestatebus_load ? ef_i : n281_o;
  /* T65.vhd:402:7  */
  assign n314_o = savestatebus_load ? mf_i : n283_o;
  /* T65.vhd:402:7  */
  assign n315_o = savestatebus_load ? xf_i : n285_o;
  /* T65.vhd:402:7  */
  assign n316_o = savestatebus_load ? ir : n211_o;
  /* T65.vhd:402:7  */
  assign n317_o = savestatebus_load ? mode_r : n212_o;
  /* T65.vhd:402:7  */
  assign n318_o = savestatebus_load ? bcd_en_r : n213_o;
  /* T65.vhd:402:7  */
  assign n319_o = savestatebus_load ? alu_op_r : n289_o;
  /* T65.vhd:402:7  */
  assign n320_o = savestatebus_load ? write_data_r : n290_o;
  /* T65.vhd:402:7  */
  assign n321_o = savestatebus_load ? set_addr_to_r : n291_o;
  /* T65.vhd:402:7  */
  assign n322_o = savestatebus_load ? n173_o : n306_o;
  /* T65.vhd:402:7  */
  assign n323_o = savestatebus_load ? wrn_i : n292_o;
  /* T65.vhd:387:7  */
  assign n324_o = n153_o ? n156_o : n308_o;
  /* T65.vhd:387:7  */
  assign n325_o = n153_o ? n157_o : n309_o;
  /* T65.vhd:387:7  */
  assign n326_o = n153_o ? n154_o : n310_o;
  /* T65.vhd:387:7  */
  assign n327_o = n153_o ? s : n312_o;
  /* T65.vhd:387:7  */
  assign n328_o = n153_o ? n168_o : n313_o;
  /* T65.vhd:387:7  */
  assign n329_o = n153_o ? n169_o : n314_o;
  /* T65.vhd:387:7  */
  assign n330_o = n153_o ? n170_o : n315_o;
  /* T65.vhd:387:7  */
  assign n331_o = n153_o ? n155_o : n316_o;
  /* T65.vhd:387:7  */
  assign n332_o = n153_o ? n158_o : n317_o;
  /* T65.vhd:387:7  */
  assign n333_o = n153_o ? n159_o : n318_o;
  /* T65.vhd:387:7  */
  assign n334_o = n153_o ? n162_o : n319_o;
  /* T65.vhd:387:7  */
  assign n335_o = n153_o ? n163_o : n320_o;
  /* T65.vhd:387:7  */
  assign n336_o = n153_o ? n165_o : n321_o;
  /* T65.vhd:387:7  */
  assign n337_o = n153_o ? rdy_mod : n322_o;
  /* T65.vhd:387:7  */
  assign n338_o = n153_o ? n167_o : n323_o;
  /* T65.vhd:385:7  */
  assign n339_o = n152_o ? pbr : n324_o;
  /* T65.vhd:385:7  */
  assign n340_o = n152_o ? dbr : n325_o;
  /* T65.vhd:385:7  */
  assign n341_o = n152_o ? pc : n326_o;
  /* T65.vhd:385:7  */
  assign n343_o = n152_o ? 16'b0000000000000000 : n327_o;
  /* T65.vhd:385:7  */
  assign n344_o = n152_o ? ef_i : n328_o;
  /* T65.vhd:385:7  */
  assign n345_o = n152_o ? mf_i : n329_o;
  /* T65.vhd:385:7  */
  assign n346_o = n152_o ? xf_i : n330_o;
  /* T65.vhd:385:7  */
  assign n347_o = n152_o ? ir : n331_o;
  /* T65.vhd:385:7  */
  assign n348_o = n152_o ? mode_r : n332_o;
  /* T65.vhd:385:7  */
  assign n349_o = n152_o ? bcd_en_r : n333_o;
  /* T65.vhd:385:7  */
  assign n350_o = n152_o ? alu_op_r : n334_o;
  /* T65.vhd:385:7  */
  assign n351_o = n152_o ? write_data_r : n335_o;
  /* T65.vhd:385:7  */
  assign n352_o = n152_o ? set_addr_to_r : n336_o;
  /* T65.vhd:385:7  */
  assign n353_o = n152_o ? rdy_mod : n337_o;
  /* T65.vhd:385:7  */
  assign n354_o = n152_o ? wrn_i : n338_o;
  /* T65.vhd:485:23  */
  assign n371_o = pc[7:0]; // extract
  /* T65.vhd:485:14  */
  assign n372_o = {1'b0, n371_o};  //  uext
  /* T65.vhd:485:59  */
  assign n373_o = dl[7]; // extract
  /* T65.vhd:485:63  */
  assign n374_o = {n373_o, dl};
  /* T65.vhd:485:39  */
  assign n375_o = n372_o + n374_o;
  /* T65.vhd:485:72  */
  assign n376_o = pcadd ? n375_o : n379_o;
  /* T65.vhd:486:23  */
  assign n377_o = pc[7:0]; // extract
  /* T65.vhd:486:19  */
  assign n379_o = {1'b0, n377_o};
  /* T65.vhd:500:17  */
  assign n387_o = ~pwr_n;
  /* T65.vhd:506:18  */
  assign n388_o = ss_2[9:2]; // extract
  /* T65.vhd:507:32  */
  assign n389_o = ss_2[17:10]; // extract
  /* T65.vhd:508:32  */
  assign n390_o = ss_2[25:18]; // extract
  /* T65.vhd:509:32  */
  assign n391_o = ss_2[33:26]; // extract
  /* T65.vhd:510:32  */
  assign n392_o = ss_2[34]; // extract
  /* T65.vhd:511:32  */
  assign n393_o = ss_2[35]; // extract
  /* T65.vhd:517:21  */
  assign n396_o = mcycle == 3'b000;
  assign n397_o = abc[7:0]; // extract
  /* T65.vhd:514:7  */
  assign n398_o = n510_o ? aluqs : n397_o;
  assign n399_o = x[7:0]; // extract
  /* T65.vhd:514:7  */
  assign n400_o = n512_o ? aluqs : n399_o;
  assign n401_o = y[7:0]; // extract
  /* T65.vhd:514:7  */
  assign n402_o = n514_o ? aluqs : n401_o;
  /* T65.vhd:527:21  */
  assign n403_o = lda | ldx;
  /* T65.vhd:527:28  */
  assign n404_o = n403_o | ldy;
  /* T65.vhd:517:11  */
  assign n405_o = n412_o ? p_out : p;
  /* T65.vhd:517:11  */
  assign n407_o = lda & n396_o;
  /* T65.vhd:517:11  */
  assign n409_o = ldx & n396_o;
  /* T65.vhd:517:11  */
  assign n411_o = ldy & n396_o;
  /* T65.vhd:517:11  */
  assign n412_o = n404_o & n396_o;
  /* T65.vhd:531:11  */
  assign n413_o = savep ? p_out : n405_o;
  /* T65.vhd:534:11  */
  assign n414_o = ldp ? aluqs : n413_o;
  /* T65.vhd:537:16  */
  assign n415_o = ir[4:0]; // extract
  /* T65.vhd:537:29  */
  assign n417_o = n415_o == 5'b11000;
  /* T65.vhd:538:20  */
  assign n418_o = ir[7:5]; // extract
  /* T65.vhd:539:13  */
  assign n421_o = n418_o == 3'b000;
  /* T65.vhd:541:13  */
  assign n424_o = n418_o == 3'b001;
  /* T65.vhd:543:13  */
  assign n427_o = n418_o == 3'b010;
  /* T65.vhd:545:13  */
  assign n430_o = n418_o == 3'b011;
  /* T65.vhd:547:13  */
  assign n433_o = n418_o == 3'b101;
  /* T65.vhd:549:13  */
  assign n436_o = n418_o == 3'b110;
  /* T65.vhd:551:13  */
  assign n439_o = n418_o == 3'b111;
  assign n440_o = {n439_o, n436_o, n433_o, n430_o, n427_o, n424_o, n421_o};
  assign n441_o = n414_o[0]; // extract
  /* T65.vhd:538:13  */
  always @*
    case (n440_o)
      7'b1000000: n442_o = n441_o;
      7'b0100000: n442_o = n441_o;
      7'b0010000: n442_o = n441_o;
      7'b0001000: n442_o = n441_o;
      7'b0000100: n442_o = n441_o;
      7'b0000010: n442_o = 1'b1;
      7'b0000001: n442_o = 1'b0;
      default: n442_o = n441_o;
    endcase
  assign n443_o = n414_o[2]; // extract
  /* T65.vhd:538:13  */
  always @*
    case (n440_o)
      7'b1000000: n444_o = n443_o;
      7'b0100000: n444_o = n443_o;
      7'b0010000: n444_o = n443_o;
      7'b0001000: n444_o = 1'b1;
      7'b0000100: n444_o = 1'b0;
      7'b0000010: n444_o = n443_o;
      7'b0000001: n444_o = n443_o;
      default: n444_o = n443_o;
    endcase
  assign n445_o = n414_o[3]; // extract
  /* T65.vhd:538:13  */
  always @*
    case (n440_o)
      7'b1000000: n446_o = 1'b1;
      7'b0100000: n446_o = 1'b0;
      7'b0010000: n446_o = n445_o;
      7'b0001000: n446_o = n445_o;
      7'b0000100: n446_o = n445_o;
      7'b0000010: n446_o = n445_o;
      7'b0000001: n446_o = n445_o;
      default: n446_o = n445_o;
    endcase
  assign n447_o = n414_o[6]; // extract
  /* T65.vhd:538:13  */
  always @*
    case (n440_o)
      7'b1000000: n448_o = n447_o;
      7'b0100000: n448_o = n447_o;
      7'b0010000: n448_o = 1'b0;
      7'b0001000: n448_o = n447_o;
      7'b0000100: n448_o = n447_o;
      7'b0000010: n448_o = n447_o;
      7'b0000001: n448_o = n447_o;
      default: n448_o = n447_o;
    endcase
  assign n449_o = {n446_o, n444_o};
  assign n450_o = n414_o[0]; // extract
  /* T65.vhd:537:11  */
  assign n451_o = n417_o ? n442_o : n450_o;
  assign n452_o = n414_o[3:2]; // extract
  /* T65.vhd:537:11  */
  assign n453_o = n417_o ? n449_o : n452_o;
  assign n454_o = n414_o[6]; // extract
  /* T65.vhd:537:11  */
  assign n455_o = n417_o ? n448_o : n454_o;
  assign n458_o = n414_o[1]; // extract
  assign n459_o = n414_o[7]; // extract
  /* T65.vhd:557:17  */
  assign n464_o = ir == 8'b00000000;
  /* T65.vhd:557:41  */
  assign n466_o = mcycle == 3'b100;
  /* T65.vhd:557:30  */
  assign n467_o = n466_o & n464_o;
  /* T65.vhd:557:62  */
  assign n468_o = ~rstcycle;
  /* T65.vhd:557:49  */
  assign n469_o = n468_o & n467_o;
  assign n471_o = n453_o[0]; // extract
  /* T65.vhd:557:11  */
  assign n472_o = n469_o ? 1'b1 : n471_o;
  assign n473_o = n453_o[1]; // extract
  /* T65.vhd:563:21  */
  assign n476_o = mode != 2'b00;
  /* T65.vhd:563:13  */
  assign n478_o = n476_o ? 1'b0 : n473_o;
  assign n479_o = {n478_o, 1'b1};
  assign n480_o = {n473_o, n472_o};
  /* T65.vhd:561:11  */
  assign n481_o = rstcycle ? n479_o : n480_o;
  assign n483_o = {n459_o, n455_o, 1'b1, 1'b1, n481_o, n458_o, n451_o};
  /* T65.vhd:516:9  */
  assign n485_o = n407_o & really_rdy;
  /* T65.vhd:516:9  */
  assign n487_o = n409_o & really_rdy;
  /* T65.vhd:516:9  */
  assign n489_o = n411_o & really_rdy;
  /* T65.vhd:514:7  */
  assign n490_o = n515_o ? n483_o : p;
  /* T65.vhd:574:14  */
  assign n493_o = ir[4:0]; // extract
  /* T65.vhd:574:26  */
  assign n495_o = n493_o != 5'b10000;
  /* T65.vhd:574:43  */
  assign n497_o = jump != 2'b01;
  /* T65.vhd:574:36  */
  assign n498_o = n495_o | n497_o;
  /* T65.vhd:574:64  */
  assign n499_o = ~really_rdy;
  /* T65.vhd:574:50  */
  assign n500_o = n498_o | n499_o;
  /* T65.vhd:514:7  */
  assign n501_o = n516_o ? irq_n : irq_n_o;
  /* T65.vhd:578:14  */
  assign n502_o = ir[4:0]; // extract
  /* T65.vhd:578:26  */
  assign n504_o = n502_o != 5'b10000;
  /* T65.vhd:578:43  */
  assign n506_o = jump != 2'b01;
  /* T65.vhd:578:36  */
  assign n507_o = n504_o | n506_o;
  /* T65.vhd:514:7  */
  assign n508_o = n517_o ? nmi_n : nmi_n_o;
  /* T65.vhd:514:7  */
  assign n510_o = n485_o & enable;
  /* T65.vhd:514:7  */
  assign n512_o = n487_o & enable;
  /* T65.vhd:514:7  */
  assign n514_o = n489_o & enable;
  /* T65.vhd:514:7  */
  assign n515_o = really_rdy & enable;
  /* T65.vhd:514:7  */
  assign n516_o = n500_o & enable;
  /* T65.vhd:514:7  */
  assign n517_o = n507_o & enable;
  /* T65.vhd:505:7  */
  assign n519_o = savestatebus_load ? n389_o : n398_o;
  /* T65.vhd:505:7  */
  assign n520_o = savestatebus_load ? n390_o : n400_o;
  /* T65.vhd:505:7  */
  assign n521_o = savestatebus_load ? n391_o : n402_o;
  /* T65.vhd:505:7  */
  assign n522_o = savestatebus_load ? n388_o : n490_o;
  /* T65.vhd:505:7  */
  assign n524_o = savestatebus_load ? n392_o : n501_o;
  /* T65.vhd:505:7  */
  assign n525_o = savestatebus_load ? n393_o : n508_o;
  /* T65.vhd:500:7  */
  assign n528_o = n387_o ? 8'b00000000 : n519_o;
  assign n530_o = abc[15:8]; // extract
  /* T65.vhd:500:7  */
  assign n531_o = n387_o ? 8'b00000000 : n530_o;
  /* T65.vhd:500:7  */
  assign n533_o = n387_o ? 8'b00000000 : n520_o;
  assign n535_o = x[15:8]; // extract
  /* T65.vhd:500:7  */
  assign n536_o = n387_o ? 8'b00000000 : n535_o;
  /* T65.vhd:500:7  */
  assign n538_o = n387_o ? 8'b00000000 : n521_o;
  assign n540_o = y[15:8]; // extract
  /* T65.vhd:500:7  */
  assign n541_o = n387_o ? 8'b00000000 : n540_o;
  /* T65.vhd:500:7  */
  assign n543_o = n387_o ? 8'b00000000 : n522_o;
  /* T65.vhd:500:7  */
  assign n545_o = n387_o ? irq_n_o : n524_o;
  /* T65.vhd:500:7  */
  assign n546_o = n387_o ? nmi_n_o : n525_o;
  /* T65.vhd:585:42  */
  assign n548_o = ~so_n;
  /* T65.vhd:585:33  */
  assign n549_o = n548_o & so_n_o;
  assign n551_o = n543_o[6]; // extract
  /* T65.vhd:585:17  */
  assign n552_o = n549_o ? 1'b1 : n551_o;
  assign n553_o = n543_o[7]; // extract
  assign n554_o = n543_o[5:0]; // extract
  assign n555_o = {n531_o, n528_o};
  assign n557_o = {n536_o, n533_o};
  assign n559_o = {n541_o, n538_o};
  assign n561_o = {n553_o, n552_o, n554_o};
  /* T65.vhd:611:18  */
  assign n573_o = ~res_n_i;
  /* T65.vhd:612:23  */
  assign n574_o = ss_3[7:0]; // extract
  /* T65.vhd:613:23  */
  assign n575_o = ss_3[15:8]; // extract
  /* T65.vhd:614:23  */
  assign n576_o = ss_3[23:16]; // extract
  /* T65.vhd:615:23  */
  assign n577_o = ss_3[31:24]; // extract
  /* T65.vhd:616:23  */
  assign n578_o = ss_3[40:32]; // extract
  /* T65.vhd:617:23  */
  assign n579_o = ss_3[48:41]; // extract
  /* T65.vhd:618:23  */
  assign n580_o = ss_3[56:49]; // extract
  /* T65.vhd:620:28  */
  assign n581_o = ss_3[57]; // extract
  /* T65.vhd:628:28  */
  assign n583_o = set_addr_to_r == 2'b00;
  /* T65.vhd:628:63  */
  assign n585_o = set_addr_to_r == 2'b10;
  /* T65.vhd:628:46  */
  assign n586_o = n583_o | n585_o;
  /* T65.vhd:629:65  */
  assign n588_o = di + 8'b00000001;
  /* T65.vhd:621:7  */
  assign n589_o = n687_o ? n588_o : busb_r;
  /* T65.vhd:635:49  */
  assign n591_o = ad + 8'b00000001;
  /* T65.vhd:636:51  */
  assign n593_o = bal + 9'b000000001;
  /* T65.vhd:633:11  */
  assign n595_o = baadd == 2'b01;
  /* T65.vhd:639:56  */
  assign n596_o = bal[7:0]; // extract
  /* T65.vhd:639:37  */
  assign n597_o = {1'b0, n596_o};  //  uext
  /* T65.vhd:639:75  */
  assign n598_o = {1'b0, busa};  //  uext
  /* T65.vhd:639:73  */
  assign n599_o = n597_o + n598_o;
  /* T65.vhd:637:11  */
  assign n601_o = baadd == 2'b10;
  /* T65.vhd:642:19  */
  assign n602_o = bal[8]; // extract
  /* T65.vhd:645:66  */
  assign n604_o = bah + 8'b00000001;
  /* T65.vhd:645:15  */
  assign n606_o = baquirk == 2'b00;
  /* T65.vhd:646:66  */
  assign n608_o = bah + 8'b00000001;
  /* T65.vhd:646:71  */
  assign n609_o = n608_o & do_r;
  /* T65.vhd:646:15  */
  assign n611_o = baquirk == 2'b01;
  /* T65.vhd:647:15  */
  assign n613_o = baquirk == 2'b10;
  assign n614_o = {n613_o, n611_o, n606_o};
  /* T65.vhd:644:15  */
  always @*
    case (n614_o)
      3'b100: n615_o = do_r;
      3'b010: n615_o = n609_o;
      3'b001: n615_o = n604_o;
      default: n615_o = bah;
    endcase
  /* T65.vhd:642:13  */
  assign n616_o = n602_o ? n615_o : bah;
  /* T65.vhd:640:11  */
  assign n618_o = baadd == 2'b11;
  assign n619_o = {n618_o, n601_o, n595_o};
  /* T65.vhd:632:11  */
  always @*
    case (n619_o)
      3'b100: n620_o = ad;
      3'b010: n620_o = ad;
      3'b001: n620_o = n591_o;
      default: n620_o = ad;
    endcase
  /* T65.vhd:632:11  */
  always @*
    case (n619_o)
      3'b100: n621_o = n616_o;
      3'b010: n621_o = bah;
      3'b001: n621_o = bah;
      default: n621_o = bah;
    endcase
  /* T65.vhd:632:11  */
  always @*
    case (n619_o)
      3'b100: n622_o = bal;
      3'b010: n622_o = n599_o;
      3'b001: n622_o = n593_o;
      default: n622_o = bal;
    endcase
  /* T65.vhd:657:63  */
  assign n623_o = y[7:0]; // extract
  /* T65.vhd:657:51  */
  assign n624_o = ad + n623_o;
  /* T65.vhd:659:63  */
  assign n625_o = x[7:0]; // extract
  /* T65.vhd:659:51  */
  assign n626_o = ad + n625_o;
  /* T65.vhd:656:13  */
  assign n627_o = addy ? n624_o : n626_o;
  /* T65.vhd:655:11  */
  assign n628_o = adadd ? n627_o : n620_o;
  /* T65.vhd:663:17  */
  assign n630_o = ir == 8'b00000000;
  /* T65.vhd:668:61  */
  assign n633_o = mcycle == 3'b100;
  /* T65.vhd:668:51  */
  assign n634_o = n633_o & nmiact;
  /* T65.vhd:668:34  */
  assign n635_o = nmicycle | n634_o;
  /* T65.vhd:668:69  */
  assign n636_o = n635_o | nmi_entered;
  /* T65.vhd:670:24  */
  assign n639_o = mcycle == 3'b100;
  /* T65.vhd:670:15  */
  assign n642_o = n639_o ? 1'b1 : 1'b0;
  /* T65.vhd:668:13  */
  assign n644_o = n636_o ? 3'b010 : 3'b110;
  /* T65.vhd:668:13  */
  assign n646_o = n636_o ? n642_o : 1'b0;
  /* T65.vhd:666:13  */
  assign n647_o = rstcycle ? 3'b100 : n644_o;
  assign n649_o = n648_o[8:3]; // extract
  /* T65.vhd:666:13  */
  assign n651_o = rstcycle ? 1'b0 : n646_o;
  /* T65.vhd:676:30  */
  assign n653_o = set_addr_to_r == 2'b11;
  assign n655_o = n647_o[0]; // extract
  /* T65.vhd:676:13  */
  assign n656_o = n653_o ? 1'b1 : n655_o;
  assign n657_o = n647_o[2:1]; // extract
  /* T65.vhd:663:11  */
  assign n659_o = n630_o ? 8'b11111111 : n621_o;
  assign n660_o = {n649_o, n657_o, n656_o};
  /* T65.vhd:663:11  */
  assign n661_o = n630_o ? n660_o : n622_o;
  /* T65.vhd:663:11  */
  assign n663_o = n630_o ? n651_o : 1'b0;
  /* T65.vhd:681:11  */
  assign n665_o = lddi ? di : dl;
  /* T65.vhd:684:11  */
  assign n666_o = ldalu ? aluqs : n665_o;
  /* T65.vhd:687:11  */
  assign n667_o = ldad ? di : n628_o;
  assign n668_o = n661_o[7:0]; // extract
  /* T65.vhd:690:11  */
  assign n669_o = ldbal ? di : n668_o;
  assign n670_o = n661_o[8]; // extract
  /* T65.vhd:693:11  */
  assign n671_o = ldbah ? di : n659_o;
  /* T65.vhd:621:7  */
  assign n672_o = n681_o ? n667_o : ad;
  /* T65.vhd:621:7  */
  assign n673_o = n682_o ? n666_o : dl;
  /* T65.vhd:621:7  */
  assign n674_o = n683_o ? n671_o : bah;
  assign n675_o = {n670_o, n669_o};
  /* T65.vhd:621:7  */
  assign n676_o = n684_o ? n675_o : bal;
  /* T65.vhd:621:7  */
  assign n677_o = n685_o ? busa : busa_r;
  /* T65.vhd:621:7  */
  assign n678_o = n686_o ? di : busb;
  /* T65.vhd:622:9  */
  assign n679_o = n586_o & really_rdy;
  /* T65.vhd:621:7  */
  assign n680_o = n688_o ? n663_o : nmi_entered;
  /* T65.vhd:621:7  */
  assign n681_o = really_rdy & enable;
  /* T65.vhd:621:7  */
  assign n682_o = really_rdy & enable;
  /* T65.vhd:621:7  */
  assign n683_o = really_rdy & enable;
  /* T65.vhd:621:7  */
  assign n684_o = really_rdy & enable;
  /* T65.vhd:621:7  */
  assign n685_o = really_rdy & enable;
  /* T65.vhd:621:7  */
  assign n686_o = really_rdy & enable;
  /* T65.vhd:621:7  */
  assign n687_o = n679_o & enable;
  /* T65.vhd:621:7  */
  assign n688_o = really_rdy & enable;
  /* T65.vhd:619:7  */
  assign n689_o = savestatebus_load ? ad : n672_o;
  /* T65.vhd:619:7  */
  assign n690_o = savestatebus_load ? dl : n673_o;
  /* T65.vhd:619:7  */
  assign n691_o = savestatebus_load ? bah : n674_o;
  /* T65.vhd:619:7  */
  assign n692_o = savestatebus_load ? bal : n676_o;
  /* T65.vhd:619:7  */
  assign n693_o = savestatebus_load ? busa_r : n677_o;
  /* T65.vhd:619:7  */
  assign n694_o = savestatebus_load ? busb : n678_o;
  /* T65.vhd:619:7  */
  assign n695_o = savestatebus_load ? busb_r : n589_o;
  /* T65.vhd:619:7  */
  assign n696_o = savestatebus_load ? n581_o : n680_o;
  /* T65.vhd:611:7  */
  assign n697_o = n573_o ? n577_o : n689_o;
  /* T65.vhd:611:7  */
  assign n698_o = n573_o ? n580_o : n690_o;
  /* T65.vhd:611:7  */
  assign n699_o = n573_o ? n579_o : n691_o;
  /* T65.vhd:611:7  */
  assign n700_o = n573_o ? n578_o : n692_o;
  /* T65.vhd:611:7  */
  assign n701_o = n573_o ? n574_o : n693_o;
  /* T65.vhd:611:7  */
  assign n702_o = n573_o ? n575_o : n694_o;
  /* T65.vhd:611:7  */
  assign n703_o = n573_o ? n576_o : n695_o;
  /* T65.vhd:611:7  */
  assign n704_o = n573_o ? nmi_entered : n696_o;
  /* T65.vhd:701:33  */
  assign n714_o = bal[8]; // extract
  /* T65.vhd:701:26  */
  assign n715_o = ~n714_o;
  /* T65.vhd:701:22  */
  assign n716_o = breakatna & n715_o;
  /* T65.vhd:701:63  */
  assign n717_o = pcadder[8]; // extract
  /* T65.vhd:701:52  */
  assign n718_o = ~n717_o;
  /* T65.vhd:701:48  */
  assign n719_o = pcadd & n718_o;
  /* T65.vhd:701:38  */
  assign n720_o = n716_o | n719_o;
  /* T65.vhd:705:45  */
  assign n722_o = set_busa_to == 4'b0000;
  /* T65.vhd:706:10  */
  assign n723_o = abc[7:0]; // extract
  /* T65.vhd:706:45  */
  assign n725_o = set_busa_to == 4'b0001;
  /* T65.vhd:707:8  */
  assign n726_o = x[7:0]; // extract
  /* T65.vhd:707:45  */
  assign n728_o = set_busa_to == 4'b0010;
  /* T65.vhd:708:8  */
  assign n729_o = y[7:0]; // extract
  /* T65.vhd:708:45  */
  assign n731_o = set_busa_to == 4'b0011;
  /* T65.vhd:709:25  */
  assign n732_o = s[7:0]; // extract
  /* T65.vhd:709:45  */
  assign n734_o = set_busa_to == 4'b0100;
  /* T65.vhd:710:45  */
  assign n736_o = set_busa_to == 4'b0101;
  /* T65.vhd:711:10  */
  assign n737_o = abc[7:0]; // extract
  /* T65.vhd:711:23  */
  assign n738_o = n737_o & di;
  /* T65.vhd:711:45  */
  assign n740_o = set_busa_to == 4'b0110;
  /* T65.vhd:712:11  */
  assign n741_o = abc[7:0]; // extract
  /* T65.vhd:712:24  */
  assign n743_o = n741_o | 8'b11101110;
  /* T65.vhd:712:34  */
  assign n744_o = n743_o & di;
  /* T65.vhd:712:45  */
  assign n746_o = set_busa_to == 4'b0111;
  /* T65.vhd:713:11  */
  assign n747_o = abc[7:0]; // extract
  /* T65.vhd:713:24  */
  assign n749_o = n747_o | 8'b11101110;
  /* T65.vhd:713:34  */
  assign n750_o = n749_o & di;
  /* T65.vhd:713:46  */
  assign n751_o = x[7:0]; // extract
  /* T65.vhd:713:41  */
  assign n752_o = n750_o & n751_o;
  /* T65.vhd:713:62  */
  assign n754_o = set_busa_to == 4'b1000;
  /* T65.vhd:714:10  */
  assign n755_o = abc[7:0]; // extract
  /* T65.vhd:714:28  */
  assign n756_o = x[7:0]; // extract
  /* T65.vhd:714:23  */
  assign n757_o = n755_o & n756_o;
  /* T65.vhd:714:45  */
  assign n759_o = set_busa_to == 4'b1001;
  /* T65.vhd:715:45  */
  assign n762_o = set_busa_to == 4'b1010;
  assign n763_o = {n762_o, n759_o, n754_o, n746_o, n740_o, n736_o, n734_o, n731_o, n728_o, n725_o, n722_o};
  /* T65.vhd:703:3  */
  always @*
    case (n763_o)
      11'b10000000000: n765_o = 8'bX;
      11'b01000000000: n765_o = n757_o;
      11'b00100000000: n765_o = n752_o;
      11'b00010000000: n765_o = n744_o;
      11'b00001000000: n765_o = n738_o;
      11'b00000100000: n765_o = p;
      11'b00000010000: n765_o = n732_o;
      11'b00000001000: n765_o = n729_o;
      11'b00000000100: n765_o = n726_o;
      11'b00000000010: n765_o = n723_o;
      11'b00000000001: n765_o = di;
      default: n765_o = 8'bX;
    endcase
  /* T65.vhd:719:46  */
  assign n766_o = s[7:0]; // extract
  /* T65.vhd:719:26  */
  assign n768_o = {16'b0000000000000001, n766_o};
  /* T65.vhd:719:87  */
  assign n770_o = set_addr_to_r == 2'b01;
  /* T65.vhd:720:11  */
  assign n772_o = {dbr, 8'b00000000};
  /* T65.vhd:720:24  */
  assign n773_o = {n772_o, ad};
  /* T65.vhd:720:87  */
  assign n775_o = set_addr_to_r == 2'b10;
  /* T65.vhd:721:18  */
  assign n777_o = {8'b00000000, bah};
  /* T65.vhd:721:29  */
  assign n778_o = bal[7:0]; // extract
  /* T65.vhd:721:24  */
  assign n779_o = {n777_o, n778_o};
  /* T65.vhd:721:87  */
  assign n781_o = set_addr_to_r == 2'b11;
  /* T65.vhd:722:32  */
  assign n782_o = pc[15:8]; // extract
  /* T65.vhd:722:11  */
  assign n783_o = {pbr, n782_o};
  /* T65.vhd:722:73  */
  assign n784_o = pcadder[7:0]; // extract
  /* T65.vhd:722:47  */
  assign n785_o = {n783_o, n784_o};
  /* T65.vhd:722:87  */
  assign n787_o = set_addr_to_r == 2'b00;
  assign n788_o = {n787_o, n781_o, n775_o, n770_o};
  /* T65.vhd:717:3  */
  always @*
    case (n788_o)
      4'b1000: n790_o = n785_o;
      4'b0100: n790_o = n779_o;
      4'b0010: n790_o = n773_o;
      4'b0001: n790_o = n768_o;
      default: n790_o = 24'bX;
    endcase
  /* T65.vhd:725:14  */
  assign n792_o = p & 8'b11101111;
  /* T65.vhd:725:44  */
  assign n793_o = irqcycle | nmicycle;
  /* T65.vhd:725:25  */
  assign n794_o = n793_o ? n792_o : p;
  /* T65.vhd:731:43  */
  assign n796_o = write_data_r == 4'b0000;
  /* T65.vhd:732:10  */
  assign n797_o = abc[7:0]; // extract
  /* T65.vhd:732:43  */
  assign n799_o = write_data_r == 4'b0001;
  /* T65.vhd:733:8  */
  assign n800_o = x[7:0]; // extract
  /* T65.vhd:733:43  */
  assign n802_o = write_data_r == 4'b0010;
  /* T65.vhd:734:8  */
  assign n803_o = y[7:0]; // extract
  /* T65.vhd:734:43  */
  assign n805_o = write_data_r == 4'b0011;
  /* T65.vhd:735:25  */
  assign n806_o = s[7:0]; // extract
  /* T65.vhd:735:43  */
  assign n808_o = write_data_r == 4'b0100;
  /* T65.vhd:736:43  */
  assign n810_o = write_data_r == 4'b0101;
  /* T65.vhd:737:26  */
  assign n811_o = pc[7:0]; // extract
  /* T65.vhd:737:43  */
  assign n813_o = write_data_r == 4'b0110;
  /* T65.vhd:738:26  */
  assign n814_o = pc[15:8]; // extract
  /* T65.vhd:738:43  */
  assign n816_o = write_data_r == 4'b0111;
  /* T65.vhd:739:10  */
  assign n817_o = abc[7:0]; // extract
  /* T65.vhd:739:28  */
  assign n818_o = x[7:0]; // extract
  /* T65.vhd:739:23  */
  assign n819_o = n817_o & n818_o;
  /* T65.vhd:739:43  */
  assign n821_o = write_data_r == 4'b1000;
  /* T65.vhd:740:10  */
  assign n822_o = abc[7:0]; // extract
  /* T65.vhd:740:28  */
  assign n823_o = x[7:0]; // extract
  /* T65.vhd:740:23  */
  assign n824_o = n822_o & n823_o;
  /* T65.vhd:740:41  */
  assign n825_o = n824_o & busb_r;
  /* T65.vhd:740:64  */
  assign n827_o = write_data_r == 4'b1001;
  /* T65.vhd:741:8  */
  assign n828_o = x[7:0]; // extract
  /* T65.vhd:741:21  */
  assign n829_o = n828_o & busb_r;
  /* T65.vhd:741:44  */
  assign n831_o = write_data_r == 4'b1010;
  /* T65.vhd:742:8  */
  assign n832_o = y[7:0]; // extract
  /* T65.vhd:742:21  */
  assign n833_o = n832_o & busb_r;
  /* T65.vhd:742:44  */
  assign n835_o = write_data_r == 4'b1011;
  /* T65.vhd:743:43  */
  assign n838_o = write_data_r == 4'b1100;
  assign n839_o = {n838_o, n835_o, n831_o, n827_o, n821_o, n816_o, n813_o, n810_o, n808_o, n805_o, n802_o, n799_o, n796_o};
  /* T65.vhd:729:3  */
  always @*
    case (n839_o)
      13'b1000000000000: n841_o = 8'bX;
      13'b0100000000000: n841_o = n833_o;
      13'b0010000000000: n841_o = n829_o;
      13'b0001000000000: n841_o = n825_o;
      13'b0000100000000: n841_o = n819_o;
      13'b0000010000000: n841_o = n814_o;
      13'b0000001000000: n841_o = n811_o;
      13'b0000000100000: n841_o = pwithb;
      13'b0000000010000: n841_o = n806_o;
      13'b0000000001000: n841_o = n803_o;
      13'b0000000000100: n841_o = n800_o;
      13'b0000000000010: n841_o = n797_o;
      13'b0000000000001: n841_o = dl;
      default: n841_o = 8'bX;
    endcase
  /* T65.vhd:759:32  */
  assign n844_o = mcycle == lcycle;
  /* T65.vhd:759:50  */
  assign n845_o = ~brks;
  /* T65.vhd:759:41  */
  assign n846_o = n845_o & n844_o;
  /* T65.vhd:759:19  */
  assign n847_o = n846_o ? 1'b1 : 1'b0;
  /* T65.vhd:764:18  */
  assign n852_o = ~res_n_i;
  /* T65.vhd:765:25  */
  assign n853_o = ss_2[39:37]; // extract
  /* T65.vhd:766:25  */
  assign n854_o = ss_2[40]; // extract
  /* T65.vhd:767:25  */
  assign n855_o = ss_2[41]; // extract
  /* T65.vhd:768:25  */
  assign n856_o = ss_2[42]; // extract
  /* T65.vhd:769:25  */
  assign n857_o = ss_2[43]; // extract
  /* T65.vhd:772:21  */
  assign n858_o = mcycle == lcycle;
  /* T65.vhd:772:30  */
  assign n859_o = n858_o | brks;
  /* T65.vhd:777:35  */
  assign n861_o = ir != 8'b00000000;
  /* T65.vhd:777:29  */
  assign n862_o = n861_o & nmiact;
  /* T65.vhd:780:27  */
  assign n863_o = ~irq_n_o;
  /* T65.vhd:780:38  */
  assign n864_o = p[2]; // extract
  /* T65.vhd:780:47  */
  assign n865_o = ~n864_o;
  /* T65.vhd:780:33  */
  assign n866_o = n865_o & n863_o;
  /* T65.vhd:780:13  */
  assign n869_o = n866_o ? 1'b1 : 1'b0;
  /* T65.vhd:777:13  */
  assign n871_o = n862_o ? 1'b0 : n869_o;
  /* T65.vhd:777:13  */
  assign n875_o = n862_o ? 1'b1 : 1'b0;
  /* T65.vhd:771:9  */
  assign n878_o = n895_o ? 1'b0 : nmiact;
  /* T65.vhd:783:23  */
  assign n880_o = mcycle != 3'b111;
  /* T65.vhd:784:57  */
  assign n882_o = mcycle + 3'b001;
  /* T65.vhd:783:11  */
  assign n883_o = n880_o ? n882_o : mcycle;
  /* T65.vhd:772:11  */
  assign n885_o = n859_o ? 3'b000 : n883_o;
  /* T65.vhd:770:7  */
  assign n887_o = n910_o ? 1'b0 : rstcycle;
  /* T65.vhd:770:7  */
  assign n888_o = n911_o ? n871_o : irqcycle;
  /* T65.vhd:770:7  */
  assign n889_o = n912_o ? n875_o : nmicycle;
  /* T65.vhd:772:11  */
  assign n890_o = n862_o & n859_o;
  /* T65.vhd:770:7  */
  assign n891_o = n909_o ? n885_o : mcycle;
  /* T65.vhd:771:9  */
  assign n892_o = n859_o & really_rdy;
  /* T65.vhd:771:9  */
  assign n893_o = n859_o & really_rdy;
  /* T65.vhd:771:9  */
  assign n894_o = n859_o & really_rdy;
  /* T65.vhd:771:9  */
  assign n895_o = n890_o & really_rdy;
  /* T65.vhd:788:37  */
  assign n896_o = ~nmi_n;
  /* T65.vhd:788:50  */
  assign n897_o = ir[4:0]; // extract
  /* T65.vhd:788:62  */
  assign n899_o = n897_o != 5'b10000;
  /* T65.vhd:788:79  */
  assign n901_o = jump != 2'b01;
  /* T65.vhd:788:72  */
  assign n902_o = n899_o | n901_o;
  /* T65.vhd:788:43  */
  assign n903_o = n902_o & n896_o;
  /* T65.vhd:788:26  */
  assign n904_o = n903_o & nmi_n_o;
  /* T65.vhd:788:9  */
  assign n906_o = n904_o ? 1'b1 : n878_o;
  /* T65.vhd:792:9  */
  assign n908_o = nmi_entered ? 1'b0 : n906_o;
  /* T65.vhd:770:7  */
  assign n909_o = really_rdy & enable;
  /* T65.vhd:770:7  */
  assign n910_o = n892_o & enable;
  /* T65.vhd:770:7  */
  assign n911_o = n893_o & enable;
  /* T65.vhd:770:7  */
  assign n912_o = n894_o & enable;
  /* T65.vhd:770:7  */
  assign n913_o = enable ? n908_o : nmiact;
  /* T65.vhd:764:7  */
  assign n914_o = n852_o ? n853_o : n891_o;
  /* T65.vhd:764:7  */
  assign n915_o = n852_o ? n854_o : n887_o;
  /* T65.vhd:764:7  */
  assign n916_o = n852_o ? n855_o : n888_o;
  /* T65.vhd:764:7  */
  assign n917_o = n852_o ? n856_o : n889_o;
  /* T65.vhd:764:7  */
  assign n918_o = n852_o ? n857_o : n913_o;
  /* T65.vhd:499:5  */
  always @(posedge clk)
    n925_q <= n555_o;
  /* T65.vhd:499:5  */
  always @(posedge clk)
    n926_q <= n557_o;
  /* T65.vhd:499:5  */
  always @(posedge clk)
    n927_q <= n559_o;
  /* T65.vhd:499:5  */
  always @(posedge clk)
    n928_q <= n561_o;
  initial
    n928_q = 8'b00000000;
  /* T65.vhd:610:5  */
  always @(posedge clk)
    n929_q <= n697_o;
  initial
    n929_q = 8'b00000000;
  /* T65.vhd:610:5  */
  always @(posedge clk)
    n930_q <= n698_o;
  initial
    n930_q = 8'b00000000;
  /* T65.vhd:610:5  */
  always @(posedge clk)
    n931_q <= n699_o;
  /* T65.vhd:610:5  */
  always @(posedge clk)
    n932_q <= n700_o;
  /* T65.vhd:384:5  */
  always @(posedge clk)
    n933_q <= n339_o;
  /* T65.vhd:384:5  */
  always @(posedge clk)
    n934_q <= n340_o;
  /* T65.vhd:384:5  */
  always @(posedge clk)
    n935_q <= n341_o;
  /* T65.vhd:384:5  */
  always @(posedge clk)
    n936_q <= n343_o;
  /* T65.vhd:384:5  */
  always @(posedge clk)
    n937_q <= n344_o;
  /* T65.vhd:384:5  */
  always @(posedge clk)
    n938_q <= n345_o;
  /* T65.vhd:384:5  */
  always @(posedge clk)
    n939_q <= n346_o;
  /* T65.vhd:384:5  */
  always @(posedge clk)
    n940_q <= n347_o;
  /* T65.vhd:763:5  */
  always @(posedge clk)
    n941_q <= n914_o;
  /* T65.vhd:384:5  */
  always @(posedge clk)
    n942_q <= n348_o;
  /* T65.vhd:384:5  */
  always @(posedge clk)
    n943_q <= n349_o;
  /* T65.vhd:384:5  */
  always @(posedge clk)
    n944_q <= n350_o;
  /* T65.vhd:384:5  */
  always @(posedge clk)
    n945_q <= n351_o;
  /* T65.vhd:384:5  */
  always @(posedge clk)
    n946_q <= n352_o;
  /* T65.vhd:763:5  */
  always @(posedge clk)
    n947_q <= n915_o;
  /* T65.vhd:763:5  */
  always @(posedge clk)
    n948_q <= n916_o;
  /* T65.vhd:763:5  */
  always @(posedge clk)
    n949_q <= n917_o;
  /* T65.vhd:499:5  */
  always @(posedge clk)
    n950_q <= so_n;
  /* T65.vhd:499:5  */
  always @(posedge clk)
    n951_q <= n545_o;
  /* T65.vhd:499:5  */
  always @(posedge clk)
    n952_q <= n546_o;
  /* T65.vhd:763:5  */
  always @(posedge clk)
    n953_q <= n918_o;
  /* T65.vhd:610:5  */
  always @(posedge clk)
    n954_q <= n701_o;
  /* T65.vhd:610:5  */
  always @(posedge clk)
    n955_q <= n702_o;
  /* T65.vhd:610:5  */
  always @(posedge clk)
    n956_q <= n703_o;
  /* T65.vhd:359:5  */
  assign n957_o = enable ? res_n_d : res_n_i;
  /* T65.vhd:359:5  */
  always @(posedge clk or posedge n130_o)
    if (n130_o)
      n958_q <= 1'b0;
    else
      n958_q <= n957_o;
  /* T65.vhd:359:5  */
  assign n959_o = enable ? 1'b1 : res_n_d;
  /* T65.vhd:359:5  */
  always @(posedge clk or posedge n130_o)
    if (n130_o)
      n960_q <= 1'b0;
    else
      n960_q <= n959_o;
  /* T65.vhd:384:5  */
  always @(posedge clk)
    n961_q <= n353_o;
  /* T65.vhd:384:5  */
  always @(posedge clk)
    n962_q <= n354_o;
  /* T65.vhd:610:5  */
  always @(posedge clk)
    n963_q <= n704_o;
  assign n970_o = {p, n91_o, n90_o, n89_o, n88_o, ir};
endmodule

module T65_nes
  (input  Res_n,
   input  Pwr_n,
   input  Enable,
   input  Clk,
   input  Rdy,
   input  IRQ_n,
   input  NMI_n,
   input  [7:0] DI,
   output R_W_n,
   output Instrnew,
   output [15:0] A,
   output [7:0] DOut);
  wire [23:0] a24;
  wire cpu_r_w_n;
  wire cpu_sync;
  wire cpu_ef;
  wire cpu_mf;
  wire cpu_xf;
  wire cpu_ml_n;
  wire cpu_vp_n;
  wire cpu_vda;
  wire cpu_vpa;
  wire [23:0] cpu_a;
  wire [7:0] cpu_dout;
  wire [63:0] cpu_regs;
  wire [7:0] cpu_debug_i;
  wire [7:0] cpu_debug_a;
  wire [7:0] cpu_debug_x;
  wire [7:0] cpu_debug_y;
  wire [7:0] cpu_debug_s;
  wire [7:0] cpu_debug_p;
  wire cpu_nmi_ack;
  wire cpu_instrnew;
  wire [63:0] cpu_savestatebus_dout;
  localparam [1:0] n4_o = 2'b00;
  localparam n5_o = 1'b0;
  localparam n6_o = 1'b1;
  localparam n7_o = 1'b1;
  localparam [63:0] n13_o = 64'b0000000000000000000000000000000000000000000000000000000000000000;
  localparam [9:0] n14_o = 10'b0000000000;
  localparam n15_o = 1'b0;
  localparam n16_o = 1'b0;
  localparam n17_o = 1'b0;
  wire [15:0] n18_o;
  assign R_W_n = cpu_r_w_n; //(module output)
  assign Instrnew = cpu_instrnew; //(module output)
  assign A = n18_o; //(module output)
  assign DOut = cpu_dout; //(module output)
  /* T65_nes.vhd:13:10  */
  assign a24 = cpu_a; // (signal)
  /* T65_nes.vhd:15:3  */
  t65 cpu (
    .mode(n4_o),
    .bcd_en(n5_o),
    .res_n(Res_n),
    .pwr_n(Pwr_n),
    .enable(Enable),
    .clk(Clk),
    .rdy(Rdy),
    .abort_n(n6_o),
    .irq_n(IRQ_n),
    .nmi_n(NMI_n),
    .so_n(n7_o),
    .di(DI),
    .savestatebus_din(n13_o),
    .savestatebus_adr(n14_o),
    .savestatebus_wren(n15_o),
    .savestatebus_rst(n16_o),
    .savestatebus_load(n17_o),
    .r_w_n(cpu_r_w_n),
    .sync(),
    .ef(),
    .mf(),
    .xf(),
    .ml_n(),
    .vp_n(),
    .vda(),
    .vpa(),
    .a(cpu_a),
    .dout(cpu_dout),
    .regs(),
    .debug_i(),
    .debug_a(),
    .debug_x(),
    .debug_y(),
    .debug_s(),
    .debug_p(),
    .nmi_ack(),
    .instrnew(cpu_instrnew),
    .savestatebus_dout());
  /* T65_nes.vhd:20:11  */
  assign n18_o = a24[15:0]; // extract
endmodule

