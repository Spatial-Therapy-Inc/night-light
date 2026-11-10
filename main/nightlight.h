// The night light's rules in plain C (no ESP-IDF, no LVGL): test/run.sh tests them on a computer. main.c feeds in
// the inputs and the time, and shows what comes out.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define NL_LIT_MS 30000                // a lit lamp goes off by itself after this
#define NL_FADE_MS 3000                // and dims over the last of it; a tap in this time lights it again
#define NL_HINT_MS 1600
#define NL_AMBER_MS 2500               // the lamp flashes amber this long when the battery reaches NL_POWER_LOW
#define NL_RAMP_MS 240                 // the panel's brightness goes on and off over this
#define NL_UI_GRACE_MS 300             // the app ring gone: wait this long before going dark, as it may be leaving
#define NL_POWER_LOW 15                // percent: the flash; the brightness cap is yaykit_app's
#define NL_POWER_HOME 30               // percent, on battery, lamp off: back to Yay
#define NL_POWER_HOME_READS 2          // readings in a row, taken with the lamp off (a lit panel pulls the cell down)
#define NL_IDLE_HOME_MS (8 * 3600 * 1000)  // plugged in, lamp off, untouched: back to Yay

#define NL_LONG_MS 500                 // a press this long on the screen is a long press
#define NL_SLOP_PX 12                  // a finger moving more than this is not a tap or a long press
#define NL_SWIPE_PX 48                 // moving this far sideways is a swipe
#define NL_TOP_EDGE_PX 24              // a press starting up here is the system's pull-down, never the app's

typedef enum { NL_LIT, NL_ALWAYS, NL_DARK } nl_mode_t;
// A swipe goes round them in this order. The numbers are saved: only ever add one at the end.
typedef enum { NL_LAMP_RING = 0, NL_LAMP_MUSHROOM = 1, NL_LAMP_MOON = 2, NL_LAMP_COUNT } nl_lamp_t;
typedef enum { NL_HINT_NONE, NL_HINT_ALWAYS, NL_HINT_BACK_TO_30, NL_HINT_RING, NL_HINT_MUSHROOM, NL_HINT_BATTERY, NL_HINT_MOON } nl_hint_t;
typedef enum { NL_IN_TAP, NL_IN_LONG, NL_IN_SWIPE, NL_IN_SIDE_TAP, NL_IN_SHAKE, NL_IN_FACE_DOWN } nl_input_t;

typedef struct {
    nl_mode_t mode;
    nl_lamp_t lamp;
    int remaining_ms;      // NL_LIT: until the lamp goes off
    bool ui_open;          // the system's app ring or 「回到 Yay 说？」 is up: the app waits
    int ui_grace_ms;
    nl_hint_t hint;
    int hint_ms;
    int amber_ms;
    int percent;           // the battery, -1 when not known
    bool usb;
    bool low_said;         // the flash, once a discharge
    int low_reads;
    int64_t idle_ms;       // lamp off and plugged in, without an input
    bool go_home;          // main.c leaves for Yay
    bool lamp_changed;     // main.c saves the lamp and clears this
} nl_t;

/** Entering the app lights the lamp. */
void nl_init(nl_t *nl, nl_lamp_t lamp);
void nl_input(nl_t *nl, nl_input_t input);
/** Time passing. ui_open: the system's app ring or question is on the screen now. */
void nl_tick(nl_t *nl, int dt_ms, bool ui_open);
/** A battery reading: percent (-1 when not known) and whether USB is there. */
void nl_power(nl_t *nl, int percent, bool usb);
/** The panel's level now, 0..255. full_percent: the lamp's (less while the battery is low); shared_percent: what the
 *  system's own screens come on at. */
int nl_level(const nl_t *nl, int full_percent, int shared_percent);
/** One step of the panel's level towards the target, dt_ms later. */
int nl_ramp(int level, int target, int dt_ms);
/** How far the picture sits off centre after this many minutes: it moves slowly while always on, against burn-in. */
void nl_shift(int64_t minutes, int *dx, int *dy);

// ---------------------------------------------------------------- the finger on the screen

typedef enum { NL_GESTURE_NONE, NL_GESTURE_TAP, NL_GESTURE_SWIPE } nl_gesture_t;

typedef struct {
    bool down;
    bool long_done;
    bool moved;
    int x, y;
    int64_t at_ms;
} nl_touch_t;

void nl_touch_down(nl_touch_t *t, int x, int y, int64_t now_ms);
void nl_touch_move(nl_touch_t *t, int x, int y);
/** True once, when the press has become a long press. */
bool nl_touch_poll(nl_touch_t *t, int64_t now_ms);
/** The finger came up here: what that press was, unless it was already a long press. */
nl_gesture_t nl_touch_up(nl_touch_t *t, int x, int y);
void nl_touch_cancel(nl_touch_t *t);
