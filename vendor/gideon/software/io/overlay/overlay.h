#ifndef OVERLAY_H
#define OVERLAY_H

extern "C" {
	#include "itu.h"
}

#include "integer.h"
#include "host.h"
#include "keyboard_c64.h"
#include "keyboard_usb.h"
#include "iomap.h"
#include "chargen.h"
#include "u64.h"
#include "FreeRTOS.h"
#include "task.h"

// This is a temporary fix, until there is a better build-up
// of objects. In fact the overlay unit should be a variant of
// an output device, but at this point there is no such base.
#define OVERLAY_CHARHEIGHT_8    0x08
#define OVERLAY_CHARHEIGHT_9    0x09
#define OVERLAY_CHARHEIGHT_16   0x90
#define OVERLAY_CHARHEIGHT_17   0x91
#define OVERLAY_CHARHEIGHT_24   0x5E

typedef struct
{
    int activeX;
    int activeY;
    int X_on;
    int Y_on;
    uint8_t charWidth;
    uint8_t charHeight;
} overlay_settings_t;

class Overlay : public GenericHost
{
    volatile t_chargen_registers *overlay_regs;
    char *charmap, *colormap;
    Keyboard *keyb;
    Screen_MemMappedCharMatrix *screen;
    bool enabled;
    bool buttonPushSeen;
    int activeX;
    int activeY;
    int X_on;
    int Y_on; //346
    // The wide mode (trxmon's 80 column screen): the overlay's cells per line and lines are
    // changed while an app has the screen. `base` is what the firmware asked for last; it is what
    // comes back when the wide mode ends.
    overlay_settings_t base;
    bool wide;
    int wideX, wideY;

public:
    Overlay(bool active, int addrbits, uint32_t base, overlay_settings_t &initial_settings)
    {
        overlay_regs = (volatile t_chargen_registers *)base;
        charmap = (char *)(base + (1 << addrbits));
        colormap = (char *)(base + (2 << addrbits));
        keyb = NULL;
        enabled = active;
        buttonPushSeen = false;
        screen = NULL;
        wide = false;
        wideX = wideY = 0;

        update_settings(initial_settings);
        screen = new Screen_MemMappedCharMatrix(charmap, colormap, activeX, activeY);
        screen->update_size(activeX, activeY);
        if (enabled) {
            take_ownership(0);
        } else {
            release_ownership();
        }
    }
    ~Overlay() {
        if(screen)
        	delete screen;
    }

/*
    void set_colors(int background, int border) {
        this->backgroundColor = background;
    }
*/

    bool exists(void) {
        return true;
    }
    
    bool is_accessible(void) {
        return enabled;
    }

    bool is_permanent(void) {
    	return true;
    }
    
    void take_ownership(HostClient *h) {
        overlay_regs->TRANSPARENCY = 0xC0;
        enabled = true;
        //system_usb_keyboard.enableMatrix(false);
    }

    void release_ownership() {
        // whoever gives the overlay back, the menu finds its own 40 x 25 screen again
        end_wide();
        wait_keys_up();
        overlay_regs->TRANSPARENCY = 0x00;
        // THIS IS A HACK... This is not part of the overlay function, but it is the best place to perform this function
        // to make the 'gap' the smallest possible between a write to the CIA and one from the system CPU.
        *C64_PLD_PORTA = C64_PLD_STATE0;
        *C64_PLD_PORTB = C64_PLD_STATE1;
        enabled = false;
        //system_usb_keyboard.enableMatrix(true);
    }

    // A key of the C64U's keyboard is down: the hardware matrix the menu scans (KEYB_COL / KEYB_ROW read the
    // physical keys; the own-keyboard bit only zeroes what reaches the C64, app_input.v).
    static bool physical_key_down(void) {
#if U64 == 2
        U64II_KEYB_COL = 0; // all columns
        return U64II_KEYB_ROW != 0xFF;
#else
        return false;
#endif
    }

    // Before the C64 gets its keyboard back: a key still held (the RETURN that ended the monitor line) would
    // be a new key press for the KERNAL. Waits until the matrix is idle, at most 1 s (a stuck key does not
    // hold the UI task). The keys typed while the C64U owned the keyboard never reached the C64.
    static void wait_keys_up(void) {
        for (int i = 0; i < 200 && physical_key_down(); i++) {
            vTaskDelay(1);
        }
    }

    // Freeze mode (the menu is on the C64's screen): the overlay is shown for the 80 column monitor only. On
    // WITH the own-keyboard bit (0xC0): the C64 must not see the keys typed at the monitor (it runs after `g`,
    // and the KERNAL reads the matrix). The freeze menu's own scan is not affected: on the slim core it reads the
    // physical matrix through KEYB_COL / KEYB_ROW, which `own` does not mask (app_input.v, top.v).
    // Nothing else changes (no `enabled`, no PLD write: release_ownership does those).
    void show_in_freeze() {
        overlay_regs->TRANSPARENCY = 0xC0;
    }

    void hide_in_freeze() {
        if (!enabled) {
            wait_keys_up();
        }
        overlay_regs->TRANSPARENCY = enabled ? 0xC0 : 0x00;
    }

    bool hasButton(void) {
    	return true;
    }

    void checkButton(void) {
    	static uint8_t button_prev;

    	uint8_t buttons = ioRead8(ITU_BUTTON_REG) & ITU_BUTTONS;
    	if((buttons & ~button_prev) & ITU_BUTTON1) {
    		buttonPushSeen = true;
    	}
    	button_prev = buttons;
    }

    bool buttonPush(void)
    {
    	bool ret = buttonPushSeen;
    	buttonPushSeen = false;
    	return ret;
    }

    void setButtonPushed(void)
    {
    	buttonPushSeen = true;
    }

    Screen *getScreen(void) {
    	return screen;
    }

    /* We should actually just return an input device type */
    Keyboard *getKeyboard(void) {
        return keyb;
    }

    void setKeyboard(Keyboard *kb) {
        keyb = kb;
    }


    void update_settings(overlay_settings_t &settings)
    {
        base = settings;
        apply();
    }

    bool is_wide(void) {
        return wide;
    }

    // cols x rows cells instead of the firmware's. The cells of the screen it had (what the menu
    // drew) are kept and come back with end_wide(); the new screen is empty. False: no screen,
    // or the 4 KB of character RAM do not hold cols x rows.
    bool begin_wide(int cols, int rows) {
        if (!screen || cols < 1 || rows < 1 || cols > 255 || rows > 63 || cols * rows > 4096) {
            return false;
        }
        if (!wide) {
            screen->cursor_visible(0); // a cursor cell would be toggled on the wrong cell when it comes back
            screen->backup();
        }
        wide = true;
        wideX = cols;
        wideY = rows;
        apply();
        screen->clear();
        return true;
    }

    void end_wide(void) {
        if (!wide) {
            return;
        }
        wide = false;
        apply();
        if (screen) {
            screen->restore();
        }
    }

private:
    void apply(void)
    {
        activeX = wide ? wideX : base.activeX;
        activeY = wide ? wideY : base.activeY;
        X_on    = base.X_on;
        Y_on    = base.Y_on;

        overlay_regs->CHARS_PER_LINE   = activeX;
        overlay_regs->ACTIVE_LINES     = activeY;
        overlay_regs->X_ON_HI          = X_on >> 8;
        overlay_regs->X_ON_LO          = X_on & 0xFF;
        overlay_regs->Y_ON_HI          = Y_on >> 8;
        overlay_regs->Y_ON_LO          = Y_on & 0xFF;
        overlay_regs->CHAR_WIDTH       = base.charWidth;
        overlay_regs->CHAR_HEIGHT      = base.charHeight;
        overlay_regs->POINTER_HI       = 0;
        overlay_regs->POINTER_LO       = 0;
        overlay_regs->PERFORM_SYNC     = 0;
        if(screen) {
            screen->update_size(activeX, activeY);
        }
    }
};

#endif
