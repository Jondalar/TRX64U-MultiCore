#include "userinterface.h"
#include "ui_mode.h"
#include "product.h"
#include <stdio.h>

#if !defined(RUNS_ON_PC) && !defined(RECOVERYAPP)
#include "c64.h"
#endif

#ifndef NO_FILE_ACCESS
#include "FreeRTOS.h"
#include "task.h"
#include "tree_browser.h"
#include "tree_browser_state.h"
#include "path.h"
#include "keyboard_usb.h"
#include "filemanager.h"
#ifndef UPDATER
#ifndef RECOVERYAPP
#include "c1541.h"
#endif // RECOVERYAPP
#endif // UPDATER
#endif // NO_FILE_ACCESS

#ifndef portENTER_CRITICAL
#define portENTER_CRITICAL()
#define portEXIT_CRITICAL()
#endif

namespace {

IndexedList<UserInterface *> *get_user_interfaces(void)
{
    static IndexedList<UserInterface *> interfaces(4, NULL);
    return &interfaces;
}

volatile int active_user_interface_count = 0;

}

int swap_interface_type(UserInterface *ui)
{
    if (!ui || !ui->cfg) {
        return MENU_NOP;
    }

    ConfigItem *item = ui->cfg->find_item(CFG_USERIF_ITYPE);
    if (!item) {
        return MENU_NOP;
    }

    item->setValue(1 - item->getValue());
    ui->cfg->write();
    return MENU_HIDE;
}

extern "C" void u64_dispatch_usb_hid_status_refresh(void) __attribute__((weak));

// The freeze menu on the TRX64 Ultimate bitstream's pixel layer (trx_video.cc,
// linked only into the U64-II/C64U application).
bool trx_video_has_layer(void) __attribute__((weak));
void trx_video_set_look(int transparency, int background) __attribute__((weak));

/* Help */
static const char *helptext =
        "WASD:       Up/Left/Down/Right\n"
        "Cursor Keys:Up/Left/Down/Right\n"
        "  (Up/Down) Selection up/down\n"
        "  (Left)    Go one level up\n"
        "            leave directory or disk\n"
        "  (Right)   Go one level down\n"
        "            enter directory or disk\n"
        "RETURN:     Selection context menu\n"
        "RUN/STOP:   Leave menu / Back\n"
        "\n"
        "USB Mouse:\n"
        "  Wheel:    Move selection\n"
        "  Left:     Activate\n"
        "  Right:    Leave menu / Back\n"
        "\n"
        "%s"          // the four keys the two interfaces disagree about
        "\n"
        "F2:         Enter Advanced Settings\n"
    #ifndef RECOVERYAPP
        "F4:         Show System Information\n"
        "F6:         Search Assembly64\n"
    #endif
        "\n"
        "SPACE:      Select file / directory\n"
        "C= A:       Select all\n"
        "C= N:       Deselect all\n"
        "C= C:       Copy current selection\n"
        "C= V:       Paste selection here\n"
        "\n"
        "HOME:       Enter home directory\n"
        "C= HOME:    Set current dir as home\n"
        "INST:       Delete selected files\n"
        "\n"  
		"Quick seek: Use the keyboard to type\n"
		"            the name to search for:\n"
		"            You can use ? as a\n"
		"            wildcard. When WASD\n"
        "            cursors are enabled,\n"
        "            type with SHIFT pressed.\n"
        "\n"  
    #ifndef RECOVERYAPP
    #endif
        "C= L:       Show Debug Log\n"
        "C= J:       Swap joystick ports\n"
        "\nIn every menu:\n"
        "C= R:       Reset C64\n"
        "C= Z:       Reboot C64\n"
#if U64 == 2
        "C= B:       Power cycle\n"
#endif
        "C= K:       Power off\n"
#if U64 == 2
        "\nOutside menus the machine can be\n"
        "reset by holding the switch up\n"
        "for 1 second.\n"
#endif
		"\nRUN/STOP to close this window.";

/* Configuration */
// Where two of the looks sit in the table below: the one a terminal always
// takes, and the one a C64 Ultimate starts from.
#define SCHEME_TELNET     7
#define SCHEME_COMMODORE  3

static const char *colors[] = { "Standard Blue", "Ultimate Black", "C128 Style",
                                "Commodore Blue", "Commodore 1", "Commodore 2",
                                "Commodore 3" };
                          
static const char *filename_overflow_squeeze[] = { "None", "Beginning", "Middle", "End" };
static const char *itype[]      = { "Freeze", "Overlay on HDMI" };
#if U64 == 2
static const char *overlay_alpha[] = { "0%", "25%", "50%", "75%" };
#endif
static const char *cfg_save[]   = { "No", "Ask", "Yes" };
static const char *navstyles[] = { "Quick Search", "WASD Cursors" };

// Which interface to present. "Automatic" is what the board says it is: the
// C64 Ultimate starts in the Commodore interface, every other machine in the
// Ultimate one. The other two settings say so regardless of the board, which
// is what lets either machine run either interface.
static const char *ui_modes[] = { "Automatic", "Ultimate", "Commodore" };

struct t_cfg_definition user_if_config[] = {
#if U64
    { CFG_USERIF_ITYPE,      CFG_TYPE_ENUM,   "Interface Type",       "%s", itype,   0,  1, 0 },
#endif
#if U64 == 2
    // Used by the TRX64 Ultimate bitstream's freeze menu; no effect elsewhere.
    { CFG_USERIF_OVERLAY_ALPHA, CFG_TYPE_ENUM, "Overlay Transparency", "%s", overlay_alpha, 0, 3, 1 },
#endif
#if U64
    { CFG_USERIF_UI_MODE,    CFG_TYPE_ENUM,   "User Interface",       "%s", ui_modes, 0,  2, 0 },
#endif
    { CFG_USERIF_NAVIGATION, CFG_TYPE_ENUM,   "Navigation Style",     "%s", navstyles, 0,  1, 0 },
    { CFG_USERIF_COLORSCHEME,CFG_TYPE_ENUM,   "Color Scheme",         "%s", colors,  0,  6, 1 },
//    { CFG_USERIF_WORDWRAP,   CFG_TYPE_ENUM,   "Wordwrap text viewer", "%s", en_dis,  0,  1, 1 },

    { CFG_USERIF_START_HOME, CFG_TYPE_ENUM,   "Enter Home on Startup", "%s", en_dis, 0,  1, 0 },
    { CFG_USERIF_HOME_DIR,   CFG_TYPE_STRING, "Home Directory",        "%s", NULL, 0, 31, (int)"" },

    { CFG_USERIF_CFG_SAVE,   CFG_TYPE_ENUM,   "Auto Save Config",      "%s", cfg_save, 0, 2, 1 },
    { CFG_USERIF_ULTICOPY_NAME, CFG_TYPE_ENUM, "Ulticopy Uses disk name", "%s", en_dis, 0, 1, 1 },
    { CFG_USERIF_FILENAME_OVERFLOW_SQUEEZE, CFG_TYPE_ENUM, "Filename overflow squeeze", "%s", filename_overflow_squeeze, 0, 3, 0 },
    { 0xFE, CFG_TYPE_SEP, "", "", NULL, 0, 0, 0 },
    { 0xFE, CFG_TYPE_SEP, "Temp Folder", "", NULL, 0, 0, 0 },
    { CFG_USERIF_TEMP_AUTO_CLEANUP, CFG_TYPE_ENUM, "Temp Auto Cleanup", "%s", en_dis, 0, 1, 1 },
    { CFG_USERIF_TEMP_USE_CACHE_SUBFOLDER, CFG_TYPE_ENUM, "Temp Subfolders", "%s", en_dis, 0, 1, 1 },
    { CFG_TYPE_END,           CFG_TYPE_END,    "", "", NULL, 0, 0, 0 }         
};

// The two interfaces, and everything they disagree about. The help lines
// name the same keys as the four codes above them; they are written out
// because a help screen has to read as words.
static const t_ui_theme themes[] = {
    {
        "Ultimate",
        BORD_LOWER_RIGHT_CORNER, BORD_LOWER_LEFT_CORNER,
        BORD_UPPER_RIGHT_CORNER, BORD_UPPER_LEFT_CORNER,
        false,   // no logo header
        false,   // browsers are framed only where they ask
        KEY_PAGEUP, KEY_HELP, KEY_TASKS, KEY_PAGEDOWN,
        "F3=HELP",
        "F1:         Page up\n"
        "F3:         Help\n"
        "F5:         Action Menu\n"
        "F7:         Page down\n",
    },
    {
        "Commodore",
        CHR_ROUNDED_LOWER_RIGHT, CHR_ROUNDED_LOWER_LEFT,
        CHR_ROUNDED_UPPER_RIGHT, CHR_ROUNDED_UPPER_LEFT,
        true,    // logo header
        true,    // every browser framed
        KEY_TASKS, KEY_PAGEUP, KEY_PAGEDOWN, KEY_HELP,
        "F7=HELP",
        "F1:         Action Menu\n"
        "F3:         Page up\n"
        "F5:         Page down\n"
        "F7:         Help\n",
    },
};

// The one in force. A change only takes effect where effectuate_settings()
// has put it, which is also where the screen is told about the corners.
static const t_ui_theme *theme = &themes[0];

const t_ui_theme &ui_theme(void)
{
    return *theme;
}

bool ui_is_commodore(void)
{
    return (theme == &themes[1]);
}

UserInterface :: UserInterface(const char *title, bool use_logo) : title(title)
{
    get_user_interfaces()->append(this);
    initialized = false;
    focus = -1;
    host = NULL;
    keyboard = NULL;
    screen = NULL;
    doBreak = false;
    available = false;
    remote = false;
    color_sel_bg = 0;
    filename_overflow_squeeze = 0;
    menu_response_to_action = MENU_NOP;
    logo = use_logo;
    // A C64 Ultimate comes up in its own look, and moves with the cursor keys
    // rather than seeking by name. Both are settings; these are the values a
    // machine that has never been set up starts from.
#if U64
    if (isCommodoreBoard()) {
        cfg_set_default(user_if_config, CFG_USERIF_COLORSCHEME, SCHEME_COMMODORE);
        cfg_set_default(user_if_config, CFG_USERIF_NAVIGATION, 1);
    }
#endif
    register_store(CFG_USERIF_STORE_ID, "User Interface Settings", user_if_config);
    cfg->set_sort_order(SORT_ORDER_CFG_USERIF);
    effectuate_registered_settings();
}

UserInterface :: ~UserInterface()
{
	printf("Destructing user interface..\n");
    set_available(false);
    get_user_interfaces()->remove(this);
    do {
        if (ui_objects[focus]) {
            ui_objects[focus]->deinit();
            delete ui_objects[focus];
        }
        focus--;
    } while(focus>=0);
    printf(" bye UI!\n");
}

void UserInterface :: set_available(bool enable)
{
    if (available == enable) {
        return;
    }
    available = enable;
    if (remote) {
        return;
    }
    portENTER_CRITICAL();
    if (enable) {
        active_user_interface_count++;
    } else if (active_user_interface_count > 0) {
        active_user_interface_count--;
    }
    portEXIT_CRITICAL();
}

typedef struct {
    int border;
    int background;
    int foreground;
    int selected;
    int selected_bg;
    int selected_rev;
    int status;
    int inactive;
    int config;
} t_scheme_colors;

// The last one is not offered: a telnet session takes it whatever the
// setting says, because a terminal has no border and no colour of its own.
//
// The four Commodore looks come from that branch, where the structure is
// six fields rather than nine: it has no colour for the status line, for an
// entry that cannot be picked, or for a setting's value, because those were
// added here afterwards. They are filled in to match what the rest of each
// look does -- the foreground for the status line and the values, and grey
// for what is out of reach.
const t_scheme_colors schemes[] = {
    { 14, 6, 14, 1, 6,  0, 12, 12, 7 },  // Standard Blue
    { 0,  0, 12, 1, 6,  0,  6,  6, 7 },  // Ultimate Black
    { 13,11, 15,13, 0,  0, 15, 12, 7 },  // C128 Style
    { 6, 14,  1, 1, 14, 1,  1, 11, 1 },  // Commodore Blue
    { 6, 14,  1, 0, 14, 0,  1, 11, 1 },  // Commodore 1
    { 6, 14, 15, 1, 14, 0, 15, 11, 15},  // Commodore 2
    { 6, 14,  0, 1, 14, 0,  0, 11, 0 },  // Commodore 3
    { 0,  0, 15,13, 0,  0, 15, 12, 7 },  // telnet, never offered
};


void UserInterface :: effectuate_settings(void)
{
    const t_scheme_colors *scheme = logo ? &schemes[cfg->get_value(CFG_USERIF_COLORSCHEME)] : &schemes[SCHEME_TELNET];
    color_border = scheme->border;
    color_fg     = scheme->foreground;
    color_bg     = scheme->background;
    color_sel    = scheme->selected;
    color_status = scheme->status;
    color_inactive = scheme->inactive;
    color_configitem = scheme->config;

    reverse_sel  = scheme->selected_rev;

#if U64
    color_sel_bg = scheme->selected_bg;
#endif
    config_save  = cfg->get_value(CFG_USERIF_CFG_SAVE);
    filename_overflow_squeeze = cfg->get_value(CFG_USERIF_FILENAME_OVERFLOW_SQUEEZE);
    navmode      = cfg->get_value(CFG_USERIF_NAVIGATION);
#if U64
    bool commodore;
    switch (cfg->get_value(CFG_USERIF_UI_MODE)) {
    case 1:  commodore = false;             break;
    case 2:  commodore = true;              break;
    default: commodore = isCommodoreBoard(); break;  // Automatic
    }
    const t_ui_theme *wanted = &themes[commodore ? 1 : 0];
    bool theme_changed = (wanted != theme);
    theme = wanted;
    // The Commodore interface opens on its own menu, so there is nowhere for
    // a home directory to be entered and the two settings for it have
    // nothing to act on. They are greyed rather than dropped, so that what
    // was set comes back when the other interface does.
    cfg->find_item(CFG_USERIF_START_HOME)->setEnabled(!commodore);
    cfg->find_item(CFG_USERIF_HOME_DIR)->setEnabled(!commodore);
    screen_set_border_corners(theme->corner_upper_left, theme->corner_upper_right,
                              theme->corner_lower_left, theme->corner_lower_right);
#endif

#ifndef NO_FILE_ACCESS
    FileManager *fm = FileManager::getFileManager();
    if (fm) {
        fm->set_temp_auto_cleanup_enabled(cfg->get_value(CFG_USERIF_TEMP_AUTO_CLEANUP) != 0);
        fm->set_temp_use_cache_subfolder_enabled(cfg->get_value(CFG_USERIF_TEMP_USE_CACHE_SUBFOLDER) != 0);
    }
#endif

    if(host && host->is_accessible())
        host->set_colors(color_bg, color_border);

    // The freeze menu's look on the TRX64 pixel layer (not the telnet one's).
    if (logo && trx_video_set_look) {
        int alpha = cfg->get_value(CFG_USERIF_OVERLAY_ALPHA);
        if (alpha >= 0) {
            trx_video_set_look(alpha, color_bg);
        }
    }

#if U64
    // A window's frame and the key hint are drawn once, when the object is
    // built. The overlay is shown and hidden rather than rebuilt, so nothing
    // would redraw them until the next time the machine is frozen: a change
    // of interface has to ask for it.
    if (theme_changed && host && host->is_accessible() && (focus >= 0)) {
        appear();
    }
#endif

    // push_event(e_refresh_browser); TODO
}
    
void UserInterface :: init(GenericHost *h)
{
    host = h;
    keyboard = h->getKeyboard();
	screen = h->getScreen();
    initialized = true;
    if (host->is_permanent()) {
    	appear();
    }
}

int UserInterface :: getPreferredType(void)
{
    return cfg->get_value(CFG_USERIF_ITYPE);
}

void UserInterface :: set_screen(Screen *s)
{
    screen = s;
}

void UserInterface :: run_remote(void)
{
    remote = true;
    host->take_ownership(this);
    appear();
    set_available(true);
    while(host->exists()) {
        if (pollFocussed() == MENU_EXIT) {
            set_available(false);
            break;
        }
        vTaskDelay(3);
    }
    set_available(false);
    host->release_ownership();
}

void UserInterface :: run(void)
{
    while(1) {
        if (host->exists()) {
            host->checkButton();
            if (host->buttonPush()) {
                run_once();
                continue;
            }
            switch(system_usb_keyboard.getch()) {
            case KEY_SCRLOCK:
            case KEY_F10:
                run_once();
                continue;
            case 0x04: // CTRL-D
                swapDisk();
                break;
            }
        }
        vTaskDelay(3);
    }
}

void (*UserInterface :: menu_enter_hook)(UserInterface *ui) = NULL;
UserInterface *UserInterface :: open_request_target = NULL;
bool (*UserInterface :: open_request_waiting_hook)(void) = NULL;
void (*UserInterface :: open_request_run_hook)(UserInterface *ui) = NULL;

void UserInterface :: set_open_request_hooks(UserInterface *ui, bool (*waiting)(void), void (*run)(UserInterface *ui))
{
    open_request_target = ui;
    open_request_waiting_hook = waiting;
    open_request_run_hook = run;
}

bool UserInterface :: open_request_waiting(void)
{
    return open_request_waiting_hook && open_request_run_hook && open_request_waiting_hook();
}

UserInterface *UserInterface :: open_request_ui(void)
{
    return open_request_target;
}

void UserInterface :: set_menu_enter_hook(void (*hook)(UserInterface *ui))
{
    menu_enter_hook = hook;
}

void UserInterface :: run_once(void)
{
#ifndef NO_FILE_ACCESS

    if (!host->exists())
        return;

    if(buttonDownFor(1000)) {
        swapDisk();
        return;
    }

    host->take_ownership(this);
    if (!host->is_permanent()) {
        appear();
    }

    set_available(true);
    if (menu_enter_hook) {
        menu_enter_hook(this);
    }
    // A task of an app asked for the overlay: that is what comes up, not the menu. Once it has
    // run, the overlay goes away as when the menu button is pushed.
    bool served = false;
    if (open_request_waiting()) {
        open_request_run_hook(this);
        served = true;
        if (!host->is_permanent()) {
            set_available(false);
            release_host();
        }
        host->release_ownership();
    }
    while(!doBreak && !served) {
        host->checkButton();
        if (!host->exists()) {
            break;
        } else if (host->buttonPush()) {
            if (!host->is_permanent()) {
                set_available(false);
                release_host();
            }
            host->release_ownership();
            break;
        } else {
            int ret = pollFocussed();
            if (ret != 0)
                printf("Poll Focussed returned %d. DoBreak = %d.\n", ret, doBreak);
            switch(ret) {
            case MENU_NOP:
                break;
            case MENU_HIDE:
            case MENU_EXIT:
                set_available(false);
                doBreak = true;
                if (!host->is_permanent()) {
                    release_host();
                }
                host->release_ownership();
                break;
            default:
                break;
            }
        }
        vTaskDelay(3);
    }
    set_available(false);
    doBreak = false;
#endif
}


#ifndef RECOVERYAPP
bool UserInterface :: buttonDownFor(uint32_t ms)
{
    bool down = false;
#ifndef UPDATER
    uint8_t delay = 10;
    TickType_t ticks = delay / portTICK_PERIOD_MS;
    uint32_t elapsed = 0;
    
    while((ioRead8(ITU_BUTTON_REG) & ITU_BUTTONS) & ITU_BUTTON1) {
        vTaskDelay(ticks);
        elapsed+=delay;
    
        if(elapsed > ms) {
            down = true;
            break;
        }
    }
#endif
    return down;
}
#else
bool UserInterface :: buttonDownFor(uint32_t ms)
{
	return false;
}
#endif

void UserInterface :: swapDisk(void)
{
#ifndef RECOVERYAPP
#ifndef UPDATER                  
	C1541* drive = C1541::get_last_mounted_drive();

    if(drive != NULL) {
      SubsysCommand* swap =
        new SubsysCommand(this, drive->getID(), MENU_1541_SWAP, 0,
                          (const char*)NULL, (const char*)NULL);

      swap->execute();
    }
#endif
#endif                                                                
}

void UserInterface :: send_keystroke(int key)
{
    UIObject *obj = ui_objects[focus];
    if (obj) {
        obj->send_keystroke(key);
    }
}

int UserInterface :: pollInactive(void)
{
    return ui_objects[focus]->poll_inactive();
}

void UserInterface :: peel_off(void)
{
    // Peel off will always keep the first object
    if (!focus) {
        return;
    }
    // The underlying object gets focus. If the top object has the cleanup
    // flag set, we destoy it here and it can not be referenced in the
    // calling object.
    ui_objects[focus]->deinit();
    // Note that the object itself still exists, and needs to be cleaned up by the one who created
    // it, unless the auto cleanup flag is set
    if (ui_objects[focus]->needCleanup()) {
        delete ui_objects[focus];
    }
    ui_objects[focus] = NULL;
    focus--;
}

int UserInterface :: pollFocussed(void)
{
	int ret = 0;
    do {
        if (u64_dispatch_usb_hid_status_refresh) {
            u64_dispatch_usb_hid_status_refresh();
        }
        ret = ui_objects[focus]->poll(ret); // param pass chain

        // Stay in the current window configuration
        if(ret == 0)
            break;

        printf("Object level %d returned %d.\n", focus, (int)ret);

        // Pass non-root positive results to underlying object
        if ((ret > 0) && (focus)) {
            peel_off();
            continue; // pass result
        }

        switch (ret) {
        case MENU_CLOSE:
            // close single layer window and pass to caller (might be cancel code)
            peel_off();
            continue; //  pass to upper layer

        case MENU_HIDE:
        case MENU_EXIT:
            printf("MENU HIDE / EXIT.\n");
            // deinitialize everything, and roll back. Caller solves this, as what happens depends on type of UI.
            return ret;
            
        default:
            printf("ERROR: Return code %d not expected here.\n", ret); // could happen for root
            ret = 0;
            break;
        }

    } while(1);
    return ret;
}

// Detect a menu-button push (hardware or REST) while a nested blocking screen
// (machine monitor / help) owns the loop. The push is re-armed so that the
// outer run_once() loop also tears the menu down, closing both layers at once
// instead of leaving the nested screen open over a dismissed menu.
void UserInterface :: discardPendingMenuButton(void)
{
    // A press is latched until read. One made before the modal opened is not
    // meant for it, and would close it the instant it appeared.
    host->checkButton();
    host->buttonPush();
}

bool UserInterface :: pollMenuButtonPush(void)
{
    host->checkButton();
    if (host->buttonPush()) {
        host->setButtonPushed();
        return true;
    }
    return false;
}

void UserInterface :: appear(void)
{
	host->set_colors(color_bg, color_border);
	set_screen_title();
	for(int i=0;i<=focus;i++) {  // build up
		//printf("Going to (re)init objects %d.\n", i);
		ui_objects[i]->init();
		ui_objects[i]->redraw();
	}
}

void UserInterface :: release_host(void)
{
    for(int i=focus;i>=0;i--) {  // tear down
        ui_objects[i]->deinit();
    }
    doBreak = true;
}

bool UserInterface :: is_available(void)
{
    return available;
}

bool UserInterface :: anyMenuActive(void)
{
    bool active;
    portENTER_CRITICAL();
    active = active_user_interface_count > 0;
    portEXIT_CRITICAL();
    return active;
}

static bool active_screen_read_safe(UserInterface *ui)
{
    if (!ui || !ui->screen) {
        return false;
    }
#if !defined(RUNS_ON_PC) && !defined(RECOVERYAPP)
    C64 *machine = C64::getMachine();
    if (machine && ui->host == (GenericHost *)machine && !machine->is_accessible()) {
        return false;
    }
#endif
    return true;
}

bool UserInterface :: copy_active_screen_matrix(uint8_t *dest, int dest_len)
{
    bool copied = false;
    if (!dest || (dest_len < ACTIVE_SCREEN_MATRIX_BYTES)) {
        return false;
    }

    IndexedList<UserInterface *> *interfaces = get_user_interfaces();
    // Keep the UI pointer stable while walking the shared list and making the
    // bounded matrix copy; IndexedList mutations use the same critical section.
    portENTER_CRITICAL();
    for (int i = 0; i < interfaces->get_elements(); i++) {
        UserInterface *ui = (*interfaces)[i];
        if (!ui || !ui->is_available() || !active_screen_read_safe(ui)) {
            continue;
        }

        int screen_width = 0;
        int screen_height = 0;
        if (!ui->screen->copy_matrix(dest, dest + ACTIVE_SCREEN_MATRIX_CELLS,
                ACTIVE_SCREEN_MATRIX_CELLS, &screen_width, &screen_height)) {
            continue;
        }
        if ((screen_width != ACTIVE_SCREEN_MATRIX_WIDTH) ||
                (screen_height != ACTIVE_SCREEN_MATRIX_HEIGHT)) {
            continue;
        }
        copied = true;
        break;
    }
    portEXIT_CRITICAL();
    return copied;
}

extern "C" bool userinterface_any_menu_active(void)
{
    return UserInterface::anyMenuActive();
}

int UserInterface :: activate_uiobject(UIObject *obj)
{
    if(focus < (MAX_UI_OBJECTS-1)) {
        focus++;
        ui_objects[focus] = obj;
        return 0;
    }

    return -1;
}

int UserInterface :: uiobject_modal(UIObject *obj)
{
    int ret = 0;
    discardPendingMenuButton();
    while(!ret && host->exists()) {
        ret = obj->poll(0);
        if (!ret && pollMenuButtonPush()) {
            break;
        }
    }
    if (obj->needCleanup()) {
        obj->deinit();
        delete obj;
    }
    if (!host->exists()) {
        ret = MENU_CLOSE;
    }
    return ret;
}

bool UserInterface :: has_focus(UIObject *obj)
{
    return (ui_objects[focus] == obj);
}

// The header of the Commodore interface: two rows of solid bars with the
// logo pieces $14..$1F between them and a reversed ULTIMATE underneath.
// Those glyphs are in the font already, so this tree draws them as they are.
// It writes no rule into the last row -- the key hint lands on an empty one.
static void draw_logo_title(Screen *screen)
{
    screen->clear();
    screen->output("\e6\x12\x12\x12\x12\x12\x12\x12\x12\x12\x12\x1c");
    screen->output("\x14\x15\x17");
    screen->output("\e1 COMMODORE 64 ");
    screen->output("\e6\x1e\x12\x12\x12\x12\x12\x12\x12\x12\x12\x12\x12");
    screen->move_cursor(0, 1);
    screen->output("\e2\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x1d");
    screen->output("\e6\x18\x16\e2\x19");
    screen->output(" \eR\e1\x1a ULTIMATE \x1a\er ");
    screen->output("\e2\x1f\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b");
    screen->scroll_mode(false);
}

void UserInterface :: set_screen_title()
{
    int width = screen->get_size_x();
    int height = screen->get_size_y();

    // The header is 40 columns of logo, so it is only drawn where there are
    // 40 columns and a font that has the pieces: the machine's own screen
    // and the overlay, not a terminal.
    if (theme->logo_title && logo && (width == 40)) {
        draw_logo_title(screen);
        return;
    }

    int len = title.length();
    int hpos = (width - len) / 2;

    screen->clear();
    screen->move_cursor(hpos, 0);
    screen->output("\eA");
    screen->output(title.c_str());
    screen->output("\eO");
    screen->move_cursor(0, 1);
	screen->repeat('\002', width);
    screen->move_cursor(0, height-1);
	screen->scroll_mode(false);
	screen->repeat('\002', width);
}

/* Blocking variants of our simple objects follow: */
int  UserInterface :: popup(const char *msg, uint8_t flags)
{
    const char *c_button_names[] = { " Ok ", " Yes ", " No ", " All ", " Cancel " };
    const char c_button_keys[] = { 'o', 'y', 'n', 'a', 'c' };

    UIPopup *pop = new UIPopup(this, msg, flags, 5, c_button_names, c_button_keys);
    pop->init();
    int ret = 0;
    discardPendingMenuButton();
    while(!ret && host->exists()) {
        ret = pop->poll(0);
        if (!ret && pollMenuButtonPush()) {
            break;
        }
    }
    pop->deinit();
    if ((ret > 0) && keyboard) {
        keyboard->wait_free();
    }
    delete pop;
    return ret;
}
    
int  UserInterface :: popup(const char *msg, int count, const char **names, const char *keys)
{
    UIPopup *pop = new UIPopup(this, msg, (1 << (count + 1))-1, count, names, keys);
    pop->init();
    int ret = 0;
    discardPendingMenuButton();
    while(!ret && host->exists()) {
        ret = pop->poll(0);
        if (!ret && pollMenuButtonPush()) {
            break;
        }
    }
    pop->deinit();
    if ((ret > 0) && keyboard) {
        keyboard->wait_free();
    }
    delete pop;
    return ret;
}

int UserInterface :: string_box(const char *msg, char *buffer, int maxlen)
{
    return string_box(msg, buffer, maxlen, false);
}

int UserInterface :: string_box(const char *msg, char *buffer, int maxlen, bool template_mode)
{
    return string_box(msg, buffer, maxlen, template_mode, false);
}

int UserInterface :: string_box(const char *msg, char *buffer, int maxlen, bool template_mode, bool uppercase)
{
    return string_box(msg, buffer, maxlen, template_mode, uppercase, 0);
}

int UserInterface :: string_box(const char *msg, char *buffer, int maxlen, bool template_mode, bool uppercase,
                                const UIStringEditPolicy *policy)
{
    UIStringBox *box = new UIStringBox(this, msg, buffer, maxlen, template_mode);
    box->set_uppercase(uppercase);
    box->set_policy(policy);
    box->init();
    screen->cursor_visible(1);
    int ret = 0;
    discardPendingMenuButton();
    while(!ret && host->exists()) {
        ret = box->poll(0);
        if (!ret && pollMenuButtonPush()) {
            break;
        }
    }
    screen->cursor_visible(0);
    box->deinit();
    delete box;
    return ret;
}

int UserInterface :: string_edit(char *buffer, int maxlen, Window *w, int x, int y, int max_chars)
{
    UIStringEdit *edit = new UIStringEdit(buffer, maxlen);
    if (max_chars <= 0) {
        max_chars = w->get_size_x()-x;
    }
    edit->init(w, keyboard, x, y, max_chars); 
    screen->cursor_visible(1);
    int ret = 0;
    discardPendingMenuButton();
    while(!ret && host->exists()) {
        ret = edit->poll(0);
        if (!ret && pollMenuButtonPush()) {
            break;
        }
    }
    screen->cursor_visible(0);
    delete edit;
    return ret;
}

int UserInterface :: choice(const char *msg, const char **choices, int count)
{
    UIChoiceBox *box = new UIChoiceBox(this, msg, choices, count);
    box->init();
    screen->cursor_visible(0);
    int ret = 0;
    discardPendingMenuButton();
    while(!ret && host->exists()) {
        ret = box->poll(0);
        if (!ret && pollMenuButtonPush()) {
            break;
        }
    }
    box->deinit(); // restores the screen under the box and frees its window
    delete box;
    // Return values are 1 based, unless it's an error
    if (!ret && !host->exists()) {
        return MENU_CLOSE;
    }
    return (ret > 0) ? (ret - 1) : ret;
}

void UserInterface :: show_progress(const char *msg, int steps)
{
    status_box = new UIStatusBox(this, msg, steps);
    status_box->init();
}

void UserInterface :: update_progress(const char *msg, int steps)
{
    status_box->update(msg, steps);
}

void UserInterface :: hide_progress(void)
{
    status_box->deinit();
    delete status_box;
}

void UserInterface :: run_editor(Editor *editor)
{
    editor->init(screen, keyboard);
    int ret = 0;
    discardPendingMenuButton();
    while(!ret && host->exists()) {
        ret = editor->poll(0);
        if (!ret && pollMenuButtonPush()) {
            break;
        }
    }
    editor->deinit();
    delete editor;
}

void UserInterface :: run_editor(const char *text_buf, int max_len)
{
    run_editor(new Editor(this, text_buf, max_len));
}

void UserInterface :: run_hex_editor(const char *text_buf, int max_len)
{
    run_editor(new HexEditor(this, text_buf, max_len));
}

QueueHandle_t userMessageQueue = 0;

void UserInterface :: postMessage(const char *msg)
{
    if (!userMessageQueue) {
        userMessageQueue = xQueueCreate(4, sizeof(void *));
    }
    if (!userMessageQueue) {
        return;
    }
    // Message handler becomes owner of object
    mstring *message = new mstring(msg);

    xQueueSend(userMessageQueue, &message, 0);
}

mstring *UserInterface :: getMessage(void)
{
    mstring *msg = NULL;

    if (!userMessageQueue) {
        userMessageQueue = xQueueCreate(4, sizeof(void *));
    }
    if (userMessageQueue) {
        xQueueReceive(userMessageQueue, &msg, 0);
    }
    return msg;
}

int UserInterface :: keymapper(int c, keymap_options_t map)
{
    if ((navmode == 1) && (map != e_keymap_monitor)) { // WASD cursors enabled
        if (c >= 'A' && c <= 'Z') {
            c |= 0x20; // make uppercase lowercase
        } else {
            switch(c) {
            case 'w': c = KEY_UP; break;
            case 'a': c = KEY_LEFT; break;
            case 's': c = KEY_DOWN; break;
            case 'd': c = KEY_RIGHT; break;
            }
        }
    }
    // F2, F4 and F6 mean the same in both interfaces. The other four are laid
    // out differently, so they come from the theme.
    switch(c) {
    case KEY_F1: c = theme->key_f1; break;
    case KEY_F3: c = theme->key_f3; break;
    case KEY_F5: c = theme->key_f5; break;
    case KEY_F7: c = theme->key_f7; break;
    case KEY_F2: c = KEY_CONFIG; break;
    case KEY_F4: c = KEY_SYSINFO; break;
    case KEY_F6: c = KEY_SEARCH; break;
    }
    return c;
}

const char *UserInterface :: function_key_for(int action) const
{
    static const int keys[] = { KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6, KEY_F7, KEY_F8 };
    static const char *const names[] = { "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8" };

    for (unsigned int i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        if (const_cast<UserInterface *>(this)->keymapper(keys[i], e_keymap_default) == action) {
            return names[i];
        }
    }
    return "";
}

void UserInterface :: help()
{
    // One buffer, filled with the help for the interface in force. Static
    // rather than on the stack: this runs on the menu task, whose stack is
    // not the place for two kilobytes, and run_editor() does not return
    // until the editor is closed, so there is only ever one of these.
    static char text[2048];
    int len = sprintf(text, helptext,
                      theme->help_fkeys);
    run_editor(text, len);
}
