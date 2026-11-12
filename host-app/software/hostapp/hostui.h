// The boot menu UI (T84, mockup .claude/trx64-site/bootmenu-mock.html): LVGL on the pixel layer, keyboard
// from the USB keyboard. Screens: boot menu with countdown, Add core (file browser over the mounted media),
// menu editor, network. Without the pixel layer it runs headless on the console log.
#ifndef HOST_UI_H
#define HOST_UI_H

// Never returns (a started core replaces this program).
void hostui_run(void) __attribute__((noreturn));

#endif
