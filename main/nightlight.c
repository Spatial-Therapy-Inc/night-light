#include "nightlight.h"

#include <stdlib.h>
#include <string.h>

static void light(nl_t *nl) {
    nl->mode = NL_LIT;
    nl->remaining_ms = NL_LIT_MS;
}

static void off(nl_t *nl) {
    nl->mode = NL_DARK;
    nl->hint = NL_HINT_NONE;
    nl->hint_ms = 0;
    nl->amber_ms = 0;
}

static void say(nl_t *nl, nl_hint_t hint) {
    nl->hint = hint;
    nl->hint_ms = NL_HINT_MS;
}

void nl_init(nl_t *nl, nl_lamp_t lamp) {
    memset(nl, 0, sizeof(*nl));
    nl->lamp = lamp;
    nl->percent = -1;
    light(nl);
}

void nl_input(nl_t *nl, nl_input_t input) {
    if (nl->ui_open) return;  // the system's screen takes the input itself
    nl->idle_ms = 0;
    const bool lit = nl->mode != NL_DARK;
    switch (input) {
    case NL_IN_TAP:
    case NL_IN_SIDE_TAP:
        if (!lit || (nl->mode == NL_LIT && nl->remaining_ms <= NL_FADE_MS)) light(nl);
        else off(nl);
        break;
    case NL_IN_LONG:
        if (nl->mode == NL_ALWAYS) {
            light(nl);
            say(nl, NL_HINT_BACK_TO_30);
        } else {
            nl->mode = NL_ALWAYS;
            say(nl, NL_HINT_ALWAYS);
        }
        break;
    case NL_IN_SWIPE:
        if (!lit) break;
        nl->lamp = nl->lamp == NL_LAMP_RING ? NL_LAMP_MUSHROOM : NL_LAMP_RING;
        nl->lamp_changed = true;
        if (nl->mode == NL_LIT) nl->remaining_ms = NL_LIT_MS;
        say(nl, nl->lamp == NL_LAMP_RING ? NL_HINT_RING : NL_HINT_MUSHROOM);
        break;
    case NL_IN_SHAKE:
    case NL_IN_FACE_DOWN:
        if (lit) off(nl);
        break;
    }
}

void nl_tick(nl_t *nl, int dt_ms, bool ui_open) {
    if (nl->hint_ms > 0) {
        nl->hint_ms -= dt_ms;
        if (nl->hint_ms <= 0) {
            nl->hint_ms = 0;
            nl->hint = NL_HINT_NONE;
        }
    }
    if (nl->amber_ms > 0) {
        nl->amber_ms -= dt_ms;
        if (nl->amber_ms < 0) nl->amber_ms = 0;
    }
    if (ui_open) {
        nl->ui_open = true;
        nl->ui_grace_ms = NL_UI_GRACE_MS;
        return;
    }
    if (nl->ui_open) {
        nl->ui_grace_ms -= dt_ms;
        if (nl->ui_grace_ms > 0) return;
        nl->ui_open = false;
        nl->idle_ms = 0;
    }
    if (nl->mode == NL_LIT) {
        nl->remaining_ms -= dt_ms;
        if (nl->remaining_ms <= 0) {
            nl->remaining_ms = 0;
            off(nl);
        }
    } else if (nl->mode == NL_DARK && nl->usb) {
        nl->idle_ms += dt_ms;
        if (nl->idle_ms >= NL_IDLE_HOME_MS) nl->go_home = true;
    }
}

void nl_power(nl_t *nl, int percent, bool usb) {
    if (usb && !nl->usb) nl->idle_ms = 0;
    nl->usb = usb;
    if (percent >= 0) nl->percent = percent;
    if (usb) {
        nl->low_said = false;
        nl->low_reads = 0;
        return;
    }
    if (nl->percent < 0) return;
    if (nl->percent <= NL_POWER_LOW && !nl->low_said) {
        nl->low_said = true;
        if (nl->mode != NL_DARK && !nl->ui_open) {
            nl->amber_ms = NL_AMBER_MS;
            say(nl, NL_HINT_BATTERY);
        }
    }
    if (nl->mode == NL_DARK && !nl->ui_open && nl->percent <= NL_POWER_HOME) {
        if (++nl->low_reads >= NL_POWER_HOME_READS) nl->go_home = true;
    } else {
        nl->low_reads = 0;
    }
}

int nl_level(const nl_t *nl, int full_percent, int shared_percent) {
    if (nl->ui_open) return shared_percent * 255 / 100;
    if (nl->mode == NL_DARK) return 0;
    const int full = full_percent * 255 / 100;
    if (nl->mode == NL_LIT && nl->remaining_ms < NL_FADE_MS) return full * nl->remaining_ms / NL_FADE_MS;
    return full;
}

int nl_ramp(int level, int target, int dt_ms) {
    int step = 255 * dt_ms / NL_RAMP_MS;
    if (step < 1) step = 1;
    if (level < target) return level + step > target ? target : level + step;
    return level - step < target ? target : level - step;
}

void nl_shift(int64_t minutes, int *dx, int *dy) {
    static const int8_t path[8][2] = {{0, 0}, {2, 0}, {2, 2}, {0, 2}, {-2, 2}, {-2, 0}, {-2, -2}, {0, -2}};
    const int i = (int)((minutes / 2) % 8);
    *dx = path[i][0];
    *dy = path[i][1];
}

void nl_touch_down(nl_touch_t *t, int x, int y, int64_t now_ms) {
    memset(t, 0, sizeof(*t));
    if (y < NL_TOP_EDGE_PX) return;
    t->down = true;
    t->x = x;
    t->y = y;
    t->at_ms = now_ms;
}

void nl_touch_move(nl_touch_t *t, int x, int y) {
    if (t->down && (abs(x - t->x) > NL_SLOP_PX || abs(y - t->y) > NL_SLOP_PX)) t->moved = true;
}

bool nl_touch_poll(nl_touch_t *t, int64_t now_ms) {
    if (!t->down || t->moved || t->long_done || now_ms - t->at_ms < NL_LONG_MS) return false;
    t->long_done = true;
    return true;
}

nl_gesture_t nl_touch_up(nl_touch_t *t, int x, int y) {
    if (!t->down) return NL_GESTURE_NONE;
    t->down = false;
    if (t->long_done) return NL_GESTURE_NONE;
    const int dx = abs(x - t->x), dy = abs(y - t->y);
    if (dx >= NL_SWIPE_PX && dx > dy) return NL_GESTURE_SWIPE;
    return t->moved || dx > NL_SLOP_PX || dy > NL_SLOP_PX ? NL_GESTURE_NONE : NL_GESTURE_TAP;
}

void nl_touch_cancel(nl_touch_t *t) { t->down = false; }
