// Base 3.3 (the members `exit_to_machine` and `goto_screen`: base 3.4), group `overlay` (T256, T239.3 D23, D29), the overlay service: a library shows pixels over the machine's picture, and only the base drives the pixel layer. It joins
// LibApi as `const SvcOverlay *overlay`, after `board`; a library built before the group never reads it (its LibApi is smaller), a loader without a core leaves it null.
//
// What the group is, in the words of the decisions (T256 D3-D10):
//   - An element is a rectangle of the machine's picture a library paints itself: an RGB565 surface the library owns. Its name, anchor, offset and size come from the core's UI TOML
//     ([[element]], descrec.h section 25); the call hands over the surface and redraws it. The surface's pixels are opaque, except the colour key of the layer (TRX_LIVE_KEY_565,
//     0x0841), which lets the picture through.
//   - An element shows only while the menu is closed: on a C64-type core over the running picture, on a core that draws its picture on the pixel layer only while the machine stands.
//     While the menu is open no element shows, except the still.
//   - The still is the element the TOML marks `still`. The base fills it with the picture of the machine when the machine stops; a library may show its own surface as the still
//     (Timewarp's frame). It lies under the open menu, which is blended over it in software.
//   - Several libraries may show elements at once; they are drawn in the order of the TOML. When a library is released or its core is left, its elements go at once.
//
// Contracts of the whole group:
//   - Any task may call. A call checks its arguments at once and answers with the result below; its effect (a redraw, text in a surface, the menu hidden or shown) is taken over by the
//     UI task at its next frame, in the order of the calls. The surface stays the library's: it is read by the UI task until `remove`, the end of the library or the end of the core (the call or
//     the end waits until the UI task has let go of it).
//   - No call takes a CoreCtx. None is allowed from an interrupt.
#ifndef COREAPP_SVC_OVERLAY_H
#define COREAPP_SVC_OVERLAY_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// A surface: w x h RGB565 pixels (stride w), in a buffer of `bytes` bytes. Both sizes are the library's; the call checks them against the element (OVL_E_SIZE, OVL_E_BYTES).
typedef struct SvcSurface {
    uint16_t *px;
    uint32_t bytes;                       // the size of the buffer the library allocated: it has to be w * h * 2
    uint16_t w, h;
} SvcSurface;

// Results. A handle (the result of show) is positive; every error is negative; error() says it in words.
enum {
    OVL_E_NONE = 0,
    OVL_E_NAME = -1,                      // the core's UI TOML has no element of that name
    OVL_E_SIZE = -2,                      // the surface's w x h is not the element's size in the TOML
    OVL_E_BYTES = -3,                     // the surface has no buffer, or its `bytes` is not w * h * 2
    OVL_E_TAKEN = -4,                     // another library shows that element
    OVL_E_HANDLE = -5,                    // not a handle of this library's element
    OVL_E_NOSERVICE = -6,                 // the service is not up (no core runs, or the loader has no core)
    OVL_E_NOFRAME = -7,                   // there is no complete frame of the machine's picture to take
    OVL_E_ARG = -8                        // a flag that exit_to_machine does not know, or a target that goto_screen cannot read (null, empty, or OVL_TARGET_LEN or more characters)
};

// Flags of exit_to_machine (base 3.4, T256 D13).
enum {
    OVL_EXIT_PAUSE = 1,                   // the machine stays stopped: Pause is set; without the flag the machine runs
    OVL_EXIT_KEEP_APP = 2                 // a resident application stays in the background; without the flag it ends (a transient application ends either way)
};
#define OVL_TARGET_LEN 48                 // the longest target of goto_screen, with its end byte

typedef struct SvcOverlay {
    uint32_t size;
    // Shows the element `name` with `surface`: the handle (> 0) or an OVL_E_* code. The place and size are the TOML's. Shown again by the library that shows it already, it takes the
    // new surface. Whether the element is on the screen now follows the state of the machine and the menu (above); the call is valid at any time.
    int (*show)(const char *name, const SvcSurface *surface);
    // The surface was painted again: the element is drawn again at the next frame. 0 or an OVL_E_* code.
    int (*update)(int el);
    // The element goes. The surface is the library's again when the call returns: the call waits for the UI task's next frame, 200 ms at most (a call from the UI task does not wait). 0 or an
    // OVL_E_* code. The same holds for show() with another surface than the element has, and for the end of a library.
    int (*remove)(int el);
    // Writes `s` into the surface with the base's fonts (the font of the menu's captions), its top left at x, y, in the RGB565 colour; clipped to the surface. Done by the UI task at
    // the next frame, before the redraw an update asks for. No result; a string longer than 47 characters is cut, a surface that is not valid is ignored, and so is a call while 16 others wait for the frame.
    void (*text)(const SvcSurface *surface, int x, int y, const char *s, uint16_t colour);
    // The person's close of the menu / the menu key: the menu closes (the machine runs unless the library set Pause first, the keys go to the machine), and opens again. Taken over by
    // the main loop at its next pass; hide while the menu is closed and show while it is open do nothing.
    void (*menu_hide)(void);
    void (*menu_show)(void);
    // The words of an OVL_E_* code.
    const char *(*error)(int code);
    // Members after `error` (T256 D11): a library checks `api->overlay->size` before it uses them.
    // Paints the frame the machine drew last into the surface, laid out as the element `name` lies on the canvas: only the part of the machine's picture under the element's rectangle, at the
    // picture's place and Scale, in the colours of the still; pixels the picture does not cover are not written. The library need not show the element first. The checks are those of show(),
    // and OVL_E_NOFRAME when there is no complete frame (the surface is then unchanged). It paints in the calling task and may wait for one grab of the base's still (a lock), never for the UI task's frame; not from an interrupt. 0 or an OVL_E_* code.
    int (*frame)(const char *name, const SvcSurface *surface);
    // The source of the 16 colours the person sees (RGB, 8 bits each) for the base's still and for frame(): f writes them and returns true, or returns false when it has none; both call f each
    // time they take a frame, on the task that takes it. The order of the sources: this one, the base's own, Colodore. null gives the source back to the base. One source per core, the last call
    // counts; it goes with the end of the library, and f is never called after that. f runs inside the grab lock: it must not call api->overlay (it would wait for itself). 0 or OVL_E_NOSERVICE.
    int (*still_palette)(bool (*f)(uint8_t rgb[16][3]));
    // The size of the canvas the elements are placed on now (960 x 540 above 576p; the canvas of the video mode below): the geometry of the places, of show() and of frame(). Writes w and h and
    // returns 0, or returns OVL_E_NOSERVICE and leaves w and h untouched. A library checks `api->overlay->size` before it calls it.
    int (*canvas)(int *w, int *h);
    // Members after `canvas` (T256 D13, base 3.4): a library or an application checks `api->overlay->size` before it uses them. Both only file a request; the main loop takes it at its next
    // pass. One request waits for the pass, shared with menu_hide and menu_show: of the four calls made before the pass the last one counts (exit then goto in one frame goes, goto then exit
    // exits). A call returns 0 or an OVL_E_* code (OVL_E_ARG, OVL_E_NOSERVICE); what the pass does is not an answer to the call.
    // Leaves for the machine in one pass: the open application's session ends, the menu closes, and Pause is set to the OVL_EXIT_PAUSE flag. A resident application ends unless OVL_EXIT_KEEP_APP;
    // a transient one ends in any case. Refused as the person's close is, with the status of the menu (needs_program: the machine cannot run yet), when the core waits for a program. While the menu is
    // closed there is no session to end and nothing to close: only a resident application is ended (without KEEP_APP), the machine is not touched. The application's own task and jobs are waited for
    // (4 s): the image is freed after its task has returned, not at the call, so the caller returns from its task entry after the call and touches nothing of the image afterwards.
    int (*exit_to_machine)(uint32_t flags);
    // Opens the menu if it is closed, ends another application's session, and goes to `target`: a place of the menu "F3" or "F3/online" (area and tab id, as `menu.screen` names them), or an application
    // "app:name" or "app:name:action", which the base opens as `app.open` does. A target the menu does not have is not an error of the call; the status of the menu says it. Not while the core has not
    // started. The caller of an application that is not resident returns from its task entry after the call, as for exit_to_machine.
    int (*goto_screen)(const char *target);
} SvcOverlay;

#ifdef __cplusplus
}
#endif

#endif
