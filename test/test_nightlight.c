// Host tests for the night light's rules. Run: bash test/run.sh
#include <stdio.h>
#include <stdlib.h>

#include "../main/nightlight.h"
#include "../main/nl_art.h"

static int failures, checks;
#define CHECK(cond)                                                        \
    do {                                                                   \
        checks++;                                                          \
        if (!(cond)) {                                                     \
            failures++;                                                    \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);          \
        }                                                                  \
    } while (0)

static void run_ms(nl_t *nl, int ms) {
    for (int t = 0; t < ms; t += 40) nl_tick(nl, 40, false);
}

static nl_t lit(void) {
    nl_t nl;
    nl_init(&nl, NL_LAMP_RING);
    return nl;
}

static nl_t dark(void) {
    nl_t nl = lit();
    nl_input(&nl, NL_IN_TAP);
    return nl;
}

static nl_t always(void) {
    nl_t nl = lit();
    nl_input(&nl, NL_IN_LONG);
    return nl;
}

static void test_on_and_off(void) {
    nl_t nl = lit();
    CHECK(nl.mode == NL_LIT && nl.remaining_ms == NL_LIT_MS);  // entering the app lights the lamp
    const nl_input_t offs[] = {NL_IN_TAP, NL_IN_SIDE_TAP, NL_IN_SHAKE, NL_IN_FACE_DOWN};
    for (int i = 0; i < 4; i++) {
        nl = lit();
        nl_input(&nl, offs[i]);
        CHECK(nl.mode == NL_DARK);
        nl = always();
        nl_input(&nl, offs[i]);
        CHECK(nl.mode == NL_DARK);
    }
    nl = dark();
    nl_input(&nl, NL_IN_TAP);
    CHECK(nl.mode == NL_LIT && nl.remaining_ms == NL_LIT_MS);
    nl = dark();
    nl_input(&nl, NL_IN_SIDE_TAP);
    CHECK(nl.mode == NL_LIT);
    nl = dark();
    nl_input(&nl, NL_IN_SHAKE);
    CHECK(nl.mode == NL_DARK);
    nl_input(&nl, NL_IN_FACE_DOWN);
    CHECK(nl.mode == NL_DARK);
}

static void test_timer(void) {
    nl_t nl = lit();
    run_ms(&nl, 29000);
    CHECK(nl.mode == NL_LIT);
    run_ms(&nl, 1100);
    CHECK(nl.mode == NL_DARK);
    // A tap while it dims lights it again; before that it turns it off.
    nl = lit();
    run_ms(&nl, 28000);
    nl_input(&nl, NL_IN_TAP);
    CHECK(nl.mode == NL_LIT && nl.remaining_ms == NL_LIT_MS);
    nl = lit();
    run_ms(&nl, 26000);
    nl_input(&nl, NL_IN_TAP);
    CHECK(nl.mode == NL_DARK);
    nl = always();
    run_ms(&nl, 120000);
    CHECK(nl.mode == NL_ALWAYS);
}

static void test_long_press(void) {
    nl_t nl = lit();
    nl_input(&nl, NL_IN_LONG);
    CHECK(nl.mode == NL_ALWAYS && nl.hint == NL_HINT_ALWAYS);
    nl_input(&nl, NL_IN_LONG);
    CHECK(nl.mode == NL_LIT && nl.remaining_ms == NL_LIT_MS && nl.hint == NL_HINT_BACK_TO_30);
    nl = dark();
    nl_input(&nl, NL_IN_LONG);
    CHECK(nl.mode == NL_ALWAYS && nl.hint == NL_HINT_ALWAYS);
    run_ms(&nl, NL_HINT_MS + 80);
    CHECK(nl.hint == NL_HINT_NONE);
}

static void test_swipe(void) {
    nl_t nl = lit();
    run_ms(&nl, 10000);
    nl_input(&nl, NL_IN_SWIPE);
    CHECK(nl.lamp == NL_LAMP_MUSHROOM && nl.remaining_ms == NL_LIT_MS && nl.lamp_changed && nl.hint == NL_HINT_MUSHROOM);
    nl_input(&nl, NL_IN_SWIPE);
    CHECK(nl.lamp == NL_LAMP_MOON && nl.hint == NL_HINT_MOON && nl.remaining_ms == NL_LIT_MS);
    nl_input(&nl, NL_IN_SWIPE);
    CHECK(nl.lamp == NL_LAMP_RING && nl.hint == NL_HINT_RING);  // round again
    nl = dark();
    nl_input(&nl, NL_IN_SWIPE);
    CHECK(nl.lamp == NL_LAMP_RING && !nl.lamp_changed);
    nl = always();
    nl_input(&nl, NL_IN_SWIPE);
    CHECK(nl.lamp == NL_LAMP_MUSHROOM && nl.mode == NL_ALWAYS);
}

static void test_system_screens(void) {
    // The ring or the question is up: the timer waits, inputs are the system's.
    nl_t nl = lit();
    nl_tick(&nl, 40, true);
    CHECK(nl.ui_open);
    const int left = nl.remaining_ms;
    for (int i = 0; i < 100; i++) nl_tick(&nl, 40, true);
    CHECK(nl.remaining_ms == left && nl.mode == NL_LIT);
    nl_input(&nl, NL_IN_TAP);
    CHECK(nl.mode == NL_LIT);
    // It stays up a moment after the ring is gone, then the app goes on as it was.
    nl_tick(&nl, 40, false);
    CHECK(nl.ui_open);
    run_ms(&nl, NL_UI_GRACE_MS + 80);
    CHECK(!nl.ui_open && nl.mode == NL_LIT);
    // The panel comes on for them even with the lamp off.
    nl = dark();
    CHECK(nl_level(&nl, 100, 80) == 0);
    nl_tick(&nl, 40, true);
    CHECK(nl_level(&nl, 100, 80) == 80 * 255 / 100);
    run_ms(&nl, NL_UI_GRACE_MS + 80);
    CHECK(nl.mode == NL_DARK && nl_level(&nl, 100, 80) == 0);
}

static void test_power(void) {
    // On battery the lamp stays waiting until 30 %, then two readings with it off go back to Yay.
    nl_t nl = dark();
    nl_power(&nl, 80, false);
    run_ms(&nl, 60000);
    CHECK(!nl.go_home);
    nl_power(&nl, 30, false);
    CHECK(!nl.go_home);
    nl_power(&nl, 30, false);
    CHECK(nl.go_home);
    // Readings with the lamp on do not count: the lit panel pulls the cell down.
    nl = lit();
    nl_power(&nl, 28, false);
    nl_power(&nl, 28, false);
    CHECK(!nl.go_home && nl.mode == NL_LIT);
    nl_input(&nl, NL_IN_TAP);
    nl_power(&nl, 28, false);
    CHECK(!nl.go_home);
    nl_power(&nl, 28, false);
    CHECK(nl.go_home);
    // An unknown level never goes home.
    nl = dark();
    nl_power(&nl, -1, false);
    nl_power(&nl, -1, false);
    CHECK(!nl.go_home);
    // Plugged in: the battery does not matter, 8 hours untouched does.
    nl = dark();
    nl_power(&nl, 10, true);
    nl_power(&nl, 10, true);
    CHECK(!nl.go_home);
    for (int h = 0; h < 7; h++) nl_tick(&nl, 3600 * 1000, false);
    CHECK(!nl.go_home);
    nl_input(&nl, NL_IN_LONG);  // a touch starts it over
    nl_input(&nl, NL_IN_TAP);
    for (int h = 0; h < 7; h++) nl_tick(&nl, 3600 * 1000, false);
    CHECK(!nl.go_home);
    nl_tick(&nl, 3600 * 1000, false);
    CHECK(nl.go_home);
    // On battery 8 hours alone do nothing.
    nl = dark();
    nl_power(&nl, 80, false);
    for (int h = 0; h < 9; h++) nl_tick(&nl, 3600 * 1000, false);
    CHECK(!nl.go_home);
    // The flash at 15 %, once a discharge.
    nl = lit();
    nl_power(&nl, 15, false);
    CHECK(nl.amber_ms == NL_AMBER_MS && nl.hint == NL_HINT_BATTERY);
    run_ms(&nl, NL_AMBER_MS + 80);
    CHECK(nl.amber_ms == 0);
    nl_power(&nl, 14, false);
    CHECK(nl.amber_ms == 0);
    nl_power(&nl, 14, true);
    nl_power(&nl, 14, false);
    CHECK(nl.amber_ms == NL_AMBER_MS);
}

static void test_level(void) {
    nl_t nl = lit();
    CHECK(nl_level(&nl, 100, 80) == 255);
    CHECK(nl_level(&nl, 60, 60) == 60 * 255 / 100);  // the battery's cap
    run_ms(&nl, NL_LIT_MS - 1500);                   // half way through the dimming
    const int half = nl_level(&nl, 100, 80);
    CHECK(half > 100 && half < 155);
    nl = always();
    run_ms(&nl, 60000);
    CHECK(nl_level(&nl, 100, 80) == 255);
    // The ramp reaches its target in about NL_RAMP_MS, and never passes it.
    int level = 0;
    int ms = 0;
    while (level != 255 && ms < 1000) {
        level = nl_ramp(level, 255, 40);
        ms += 40;
    }
    CHECK(level == 255 && ms >= NL_RAMP_MS - 40 && ms <= NL_RAMP_MS + 40);
    CHECK(nl_ramp(10, 12, 40) == 12);
    CHECK(nl_ramp(12, 10, 40) == 10);
    CHECK(nl_ramp(100, 100, 40) == 100);
}

static void test_shift(void) {
    int dx, dy, moved = 0;
    for (int m = 0; m < 64; m++) {
        nl_shift(m, &dx, &dy);
        CHECK(abs(dx) <= 2 && abs(dy) <= 2);
        if (dx || dy) moved++;
    }
    CHECK(moved > 0);
    nl_shift(0, &dx, &dy);
    CHECK(dx == 0 && dy == 0);
    nl_shift(1000000000000LL, &dx, &dy);  // after however long
    CHECK(abs(dx) <= 2 && abs(dy) <= 2);
}

static void test_gestures(void) {
    nl_touch_t t;
    nl_touch_down(&t, 200, 200, 1000);
    CHECK(!nl_touch_poll(&t, 1400));
    CHECK(nl_touch_up(&t, 203, 198) == NL_GESTURE_TAP);
    // Held: one long press, and letting go is nothing more.
    nl_touch_down(&t, 200, 200, 1000);
    CHECK(nl_touch_poll(&t, 1000 + NL_LONG_MS));
    CHECK(!nl_touch_poll(&t, 2000));
    CHECK(nl_touch_up(&t, 200, 200) == NL_GESTURE_NONE);
    // Across the screen: a swipe either way, and never a long press.
    nl_touch_down(&t, 100, 200, 1000);
    nl_touch_move(&t, 180, 204);
    CHECK(!nl_touch_poll(&t, 2000));
    CHECK(nl_touch_up(&t, 180, 204) == NL_GESTURE_SWIPE);
    nl_touch_down(&t, 300, 200, 1000);
    CHECK(nl_touch_up(&t, 230, 210) == NL_GESTURE_SWIPE);
    // Moved, but not far enough or the wrong way: nothing.
    nl_touch_down(&t, 200, 200, 1000);
    nl_touch_move(&t, 225, 200);
    CHECK(nl_touch_up(&t, 225, 200) == NL_GESTURE_NONE);
    nl_touch_down(&t, 200, 100, 1000);
    CHECK(nl_touch_up(&t, 220, 300) == NL_GESTURE_NONE);
    // From the top edge it is the system's pull-down.
    nl_touch_down(&t, 233, NL_TOP_EDGE_PX - 1, 1000);
    CHECK(!nl_touch_poll(&t, 2000));
    CHECK(nl_touch_up(&t, 233, NL_TOP_EDGE_PX - 1) == NL_GESTURE_NONE);
    // The finger slid off: nothing.
    nl_touch_down(&t, 200, 200, 1000);
    nl_touch_cancel(&t);
    CHECK(!nl_touch_poll(&t, 2000));
    CHECK(nl_touch_up(&t, 200, 200) == NL_GESTURE_NONE);
}

static int channel(uint16_t px, int bits, int shift) { return (px >> shift) & ((1 << bits) - 1); }

static void test_ring_art(void) {
    enum { N = NL_ART_SIZE };
    uint16_t *px = malloc(N * N * sizeof(uint16_t));
    CHECK(px != NULL);
    nl_art_ring(px, 30, 0xF3E6C8);
    const int inner = nl_art_ring_inner(30);
    CHECK(inner >= 194 && inner <= 196);  // 30 % of the area: r 195..233
    CHECK(px[N / 2 * N + N / 2] == 0);    // the middle is black, it does not glow
    const uint16_t solid = (0xF3 >> 3) << 11 | (0xE6 >> 2) << 5 | (0xC8 >> 3);
    CHECK(px[N / 2 * N + N / 2 + inner + 2] == solid);  // the ring itself
    CHECK(px[N / 2 * N + N / 2 - inner - 2] == solid);
    CHECK(px[N / 2 * N + N / 2 + inner - 60] == 0);     // black again well inside it
    // In between it gets brighter going out, and it is the same all the way round.
    const int a = channel(px[N / 2 * N + N / 2 + inner - 30], 5, 11);
    const int b = channel(px[N / 2 * N + N / 2 + inner - 8], 5, 11);
    CHECK(a < b);
    int off_row = 0;
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++) {
            const int dx = x - N / 2, dy = y - N / 2;
            if (dx * dx + dy * dy > (inner + 1) * (inner + 1) && px[y * N + x] != solid) off_row++;
        }
    CHECK(off_row == 0);
    CHECK(nl_art_ring_inner(20) >= 207 && nl_art_ring_inner(20) <= 209);
    free(px);
}

static void test_moon_art(void) {
    enum { N = NL_ART_SIZE };
    uint16_t *px = malloc(N * N * sizeof(uint16_t));
    CHECK(px != NULL);
    nl_art_moon(px, 0xF3E6C8);
    const uint16_t solid = (0xF3 >> 3) << 11 | (0xE6 >> 2) << 5 | (0xC8 >> 3);
    const int cx = N / 2 + NL_MOON_CUT_DX, cy = N / 2 + NL_MOON_CUT_DY;
    CHECK(px[cy * N + cx] == 0);                 // the cut is dark: the time goes there
    CHECK(px[(N / 2) * N + N / 2 - 150] == solid);  // the thick of the crescent, on the left
    CHECK(channel(px[(N / 2) * N + N / 2 - 230], 5, 11) <= 4);  // its soft outer edge, faint by the glass
    // Into the cut from the crescent's inner edge the light falls away, and is gone well inside.
    const int edge = cx - NL_MOON_CUT_R;          // where the cut begins on the row through its middle
    const int a = channel(px[cy * N + edge + 6], 5, 11), b = channel(px[cy * N + edge + 30], 5, 11);
    CHECK(channel(px[cy * N + edge - 3], 5, 11) == (0xF3 >> 3) && a > b && b > 0);
    CHECK(px[cy * N + edge + 50] == 0);
    // About a third of the glass is lit, as much light as the ring's 30 %.
    int lit = 0, glass = 0;
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++) {
            const int dx = x - N / 2, dy = y - N / 2;
            if (dx * dx + dy * dy > 233 * 233) continue;
            glass++;
            lit += px[y * N + x] == solid;
        }
    CHECK(lit * 100 / glass >= 30 && lit * 100 / glass <= 38);
    free(px);
}

int main(void) {
    test_on_and_off();
    test_timer();
    test_long_press();
    test_swipe();
    test_system_screens();
    test_power();
    test_level();
    test_shift();
    test_gestures();
    test_ring_art();
    test_moon_art();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
