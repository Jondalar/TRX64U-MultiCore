// TRX64 host boot app (T84): derived from software/lvgl_port/lv_conf.h of the Ultimate app (fonts 14/16/24,
// unscii 16 and the bar widget on; the rest unchanged).
// LVGL configuration for the TRX64 Ultimate pixel layer (TRX64-Ultimate
// docs/specs/T24-lvgl-ui.md §4). Only what differs from LVGL's defaults
// (software/lvgl/include/lvgl/config/lv_conf_internal.h) is set here, plus every
// widget, theme and font switch, so that nothing is built by default that the
// UI does not use: a widget is turned on in the phase that needs it.
//
// Built for rv32im without a divider and without an FPU (soft float; no FPU
// code: LV_USE_FLOAT 0, no vector graphics).
#ifndef LV_CONF_H
#define LV_CONF_H

// ---- colour and display --------------------------------------------------------
// The pixel layer is RGB565 only (pix_layer.v, PX_CTRL format 1, little endian).
#define LV_COLOR_FORMAT_DEFAULT          LV_COLOR_FORMAT_RGB565
#define LV_DPI_DEF                       130
// Invalid areas LVGL keeps per display before it falls back to the whole screen (lv_refr.c lv_inv_area; default 32, lvgl
// takes this definition because lv_display_private.h only defines it when it is missing). A page of the boot menu changes
// ~10 widgets in each of 5 rows and the Add core list 3 labels in each of 13: with 32 the 33rd area became 518,400 px
// (~1.1 s on the board). Costs 17 bytes per slot in the display (area + joined flag): 128 slots = 2,176 B instead of 544 B;
// lv_refr_join_area() is O(n^2) in the saved areas (16,384 rectangle tests at most, microseconds).
#define LV_INV_BUF_SIZE                  128
#define LV_DEF_REFR_PERIOD               10     // ms (host app: a key must not wait for the next 20 ms tick; the flush waits for the flip)

// ---- memory, strings, printf -----------------------------------------------------
// malloc/free: the app's wrapped malloc (memory_wrap.cc, FreeRTOS heap_4), so a
// UI that is shut down gives its memory back. LVGL's own string and printf
// functions: word-wide copies, and no newlib vsnprintf.
#define LV_USE_STDLIB_MALLOC             LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING             LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_SPRINTF            LV_STDLIB_BUILTIN

// One UI task touches LVGL (trx_lv_port.cc); no LVGL OS layer.
#define LV_USE_OS                        LV_OS_NONE

// ---- drawing ---------------------------------------------------------------------
#define LV_USE_DRAW_SW                   1
#define LV_DRAW_SW_DRAW_UNIT_CNT         1
#define LV_DRAW_SW_SUPPORT_RGB565        1
#define LV_DRAW_SW_SUPPORT_RGB565_SWAPPED 0
#define LV_DRAW_SW_SUPPORT_RGB565A8      1      // images with alpha (tools/lvgl_image.sh)
#define LV_DRAW_SW_SUPPORT_RGB888        0
#define LV_DRAW_SW_SUPPORT_XRGB8888      0
#define LV_DRAW_SW_SUPPORT_ARGB8888      0
#define LV_DRAW_SW_SUPPORT_ARGB8888_PREMULTIPLIED 0
#define LV_DRAW_SW_SUPPORT_L8            0
#define LV_DRAW_SW_SUPPORT_AL88          0
#define LV_DRAW_SW_SUPPORT_A8            1
#define LV_DRAW_SW_SUPPORT_I1            0
// Rounded corners, shadows, arcs, lines: off until a phase draws them.
#define LV_DRAW_SW_COMPLEX               0
#define LV_USE_DRAW_SW_ASM               LV_DRAW_SW_ASM_NONE
#define LV_USE_MATRIX                    0
#define LV_USE_FLOAT                     0
#define LV_USE_VECTOR_GRAPHIC            0
#define LV_USE_THORVG                    0
#define LV_USE_SNAPSHOT                  0

// ---- features ---------------------------------------------------------------------
#define LV_USE_LOG                       0
#define LV_USE_ASSERT_NULL               1
#define LV_USE_ASSERT_MALLOC             1
#define LV_USE_ASSERT_STYLE              0
#define LV_USE_ASSERT_MEM_INTEGRITY      0
#define LV_USE_ASSERT_OBJ                0
#define LV_USE_OBSERVER                  0
#define LV_USE_SYSMON                    0
#define LV_USE_PERF_MONITOR              0
#define LV_USE_MEM_MONITOR               0
#define LV_USE_PROFILER                  0
#define LV_USE_OBJ_ID                    0
#define LV_USE_OBJ_NAME                  0
#define LV_USE_OBJ_PROPERTY              0
#define LV_USE_TRANSLATION               0
#define LV_USE_GRIDNAV                   0
#define LV_USE_GESTURE_RECOGNITION       0
#define LV_USE_FRAGMENT                  0
#define LV_USE_FILE_EXPLORER             0
#define LV_USE_IMGFONT                   0
#define LV_USE_FONT_MANAGER              0
#define LV_USE_MONKEY                    0
#define LV_USE_TEST                      0
#define LV_BUILD_EXAMPLES                0
#define LV_BUILD_DEMOS                   0

// Images are converted at build time (LVGLImage.py), never decoded at run time.
#define LV_USE_SVG                       0
#define LV_USE_LODEPNG                   0
#define LV_USE_LIBPNG                    0
#define LV_USE_BMP                       0
#define LV_USE_TJPGD                     0
#define LV_USE_GIF                       0
#define LV_USE_QRCODE                    0
#define LV_USE_BARCODE                   0
#define LV_USE_RLE                       0
#define LV_USE_LZ4                       0
#define LV_USE_FREETYPE                  0
#define LV_USE_TINY_TTF                  0
#define LV_USE_FS_STDIO                  0
#define LV_USE_FS_POSIX                  0
#define LV_USE_FS_FATFS                  0
#define LV_USE_FS_MEMFS                  0

// ---- themes and layouts -----------------------------------------------------------
#define LV_USE_THEME_DEFAULT             0
#define LV_USE_THEME_SIMPLE              0
#define LV_USE_THEME_MONO                0
#define LV_USE_FLEX                      0
#define LV_USE_GRID                      0

// ---- fonts -------------------------------------------------------------------------
// Phase 1 (splash): one small pixel font for the version line (8 px, 16 px on
// the 2x layer). LV_FONT_DEFAULT must be an enabled font.
#define LV_FONT_MONTSERRAT_14            1
#define LV_FONT_MONTSERRAT_16            1
#define LV_FONT_MONTSERRAT_24            1
#define LV_FONT_UNSCII_16                1
#define LV_FONT_MONTSERRAT_48            0
#define LV_FONT_UNSCII_8                 1
#define LV_FONT_DEFAULT                  &lv_font_montserrat_14
#define LV_USE_FONT_PLACEHOLDER          1

// ---- widgets -----------------------------------------------------------------------
#define LV_USE_LABEL                     1
#define LV_LABEL_TEXT_SELECTION          0
#define LV_LABEL_LONG_TXT_HINT           0
#define LV_USE_IMAGE                     1
#define LV_USE_ANIMIMG                   0
#define LV_USE_ARC                       0
#define LV_USE_ARCLABEL                  0
#define LV_USE_BAR                       1
#define LV_USE_BUTTON                    0
#define LV_USE_BUTTONMATRIX              0
#define LV_USE_CALENDAR                  0
#define LV_USE_CANVAS                    0
#define LV_USE_CHART                     0
#define LV_USE_CHECKBOX                  0
#define LV_USE_DROPDOWN                  0
#define LV_USE_IMAGEBUTTON               0
#define LV_USE_KEYBOARD                  0
#define LV_USE_LED                       0
#define LV_USE_LINE                      0
#define LV_USE_LIST                      0
#define LV_USE_LOTTIE                    0
#define LV_USE_MENU                      0
#define LV_USE_MSGBOX                    0
#define LV_USE_ROLLER                    0
#define LV_USE_SCALE                     0
#define LV_USE_SLIDER                    0
#define LV_USE_SPAN                      0
#define LV_USE_SPINBOX                   0
#define LV_USE_SPINNER                   0
#define LV_USE_SWITCH                    0
#define LV_USE_TEXTAREA                  0
#define LV_USE_TABLE                     0
#define LV_USE_TABVIEW                   0
#define LV_USE_TILEVIEW                  0
#define LV_USE_WIN                       0
#define LV_USE_3DTEXTURE                 0
#define LV_USE_GLTF                      0

#endif
