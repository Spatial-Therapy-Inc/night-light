// 小夜灯: a lamp on the round screen. Entering the app lights it; a tap, a shake or laying it face down turns it off,
// and it goes off by itself after 30 s unless a long press keeps it on. With the lamp off the app stays, black, so
// that one tap lights it again (a return to Yay and back would be two restarts). The rules are in nightlight.c; this
// file is the template's start-up order (NVS -> yaykit_app -> board -> side key -> Yaykit Link -> screen -> console ->
// yk_app_ready()) and the drawing.
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "board.h"
#include "cjk_font.h"
#include "esp_console.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nightlight.h"
#include "nl_art.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "yaykit_app.h"
#include "yaykit_key.h"
#include "yaykit_link.h"
#include "yaykit_motion.h"
#include "yaykit_ui.h"
#include "yk_app_summary.h"
#include "yk_apps.h"
#include "yk_hint.h"

#define APP_SLUG "night-light"
static const char *TAG = "night-light";

#define NVS_NAMESPACE "yk_nightlight"
#define NVS_KEY_LAMP "lamp"
#define TICK_MS 40
#define POWER_READ_MS 30000
#define SAVE_DELAY_US (2 * 1000 * 1000)
#define RING_AREA_PERCENT 30   // of the glass: r 195..233
#define TIME_RING_Y (-6)       // the digits sit low in their line: measured on a snapshot, they centre at 233 with this
#define TIME_ON_CAP_Y (-55)    // the mushroom's time sits on its cap: digits at y 149..220, the cream ends at 245
#define DOT_Y_RING 88          // inside the ring's black, clear of its glow
#define DOT_Y_MUSHROOM 52      // on black above the cap
#define HINT_Y_RING 322
#define HINT_Y_MUSHROOM 384
// The crescent's time, hint and dot sit in the dark of its cut, right of the middle and a little up.
#define MOON_X 74
#define TIME_MOON_Y (-28)
#define DOT_Y_MOON 96
#define HINT_Y_MOON 290
#define DOT_SIZE 10
#define HINT_X 123
#define HINT_W 220

// The panel's level is changed by nl_ramp(), so the first frame is drawn at 0 and the lamp fades up.
#define T(zh, en) (s_english ? (en) : (zh))

enum {
    PENDING_SIDE_TAP = 1 << 0,
    PENDING_SHAKE = 1 << 1,
    PENDING_FACE_DOWN = 1 << 2,
    PENDING_TAP = 1 << 3,    // the rest only from `yk cmd nightlight.input`, in development builds
    PENDING_LONG = 1 << 4,
    PENDING_SWIPE = 1 << 5,
};

// mushroom.rgb565 is made from art/mushroom.svg by tools/make_art.py.
extern const uint8_t mushroom_start[] asm("_binary_mushroom_rgb565_start");

static nl_t s_nl;  // only the LVGL task changes it
static nl_touch_t s_touch;
static bool s_english;
static atomic_uint s_pending;
static atomic_bool s_leaving;
static atomic_int s_lamp_to_save;
static esp_timer_handle_t s_save_timer;

static lv_image_dsc_t art_dsc(const void *px);  // below, with the rest of the pictures

static uint16_t *s_ring_px, *s_moon_px;
static lv_image_dsc_t s_ring_dsc, s_mushroom_dsc, s_moon_dsc;
static bool s_moon_amber;
static lv_obj_t *s_root, *s_art, *s_time, *s_dot, *s_hint;

static int s_level;
static int64_t s_last_ms, s_power_at_ms = -POWER_READ_MS;
static int s_shown_lamp = -1, s_shown_hint = -1, s_shown_mode = -1, s_shown_dx, s_shown_dy;
static bool s_shown_amber;
static time_t s_shown_minute = -1;
static bool s_going_home;

#ifdef CONFIG_YAY_DEV
static atomic_int s_sim_percent = -2;  // `yk cmd nightlight.power`: -2, read the battery
static atomic_bool s_sim_usb;
#endif

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

static void pend(unsigned bit) { atomic_fetch_or(&s_pending, bit); }

/** In the LVGL task. The line says which gesture the screen or the sensor really gave, for testing by hand. */
static void input(nl_input_t in) {
    static const char *const names[] = {"tap", "long", "swipe", "side", "shake", "down"};
    printf("NIGHT_LIGHT_INPUT %s\n", names[in]);
    nl_input(&s_nl, in);
}

/** Through yaykit_app (yk_app_on_key). A hold is not used: the system's 「回到 Yay 说？」 is the way back in the morning. */
static bool on_key(const yk_key_event_t *ev, void *ctx) {
    (void)ctx;
    if (ev->type != YK_KEY_TAP) return false;
    pend(PENDING_SIDE_TAP);
    return true;
}

static void on_motion(const yk_motion_event_t *ev, void *ctx) {
    (void)ctx;
    if (ev->kind == YK_MOTION_SHAKE_START) pend(PENDING_SHAKE);
    else if (ev->kind == YK_MOTION_FACE_DOWN) pend(PENDING_FACE_DOWN);
}

/** The whole screen is one touch area. In the LVGL task. */
static void on_touch(lv_event_t *e) {
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    switch (lv_event_get_code(e)) {
    case LV_EVENT_PRESSED:
        yk_app_input();
        nl_touch_down(&s_touch, p.x, p.y, now_ms());
        break;
    case LV_EVENT_PRESSING:
        nl_touch_move(&s_touch, p.x, p.y);
        break;
    case LV_EVENT_RELEASED:
        switch (nl_touch_up(&s_touch, p.x, p.y)) {
        case NL_GESTURE_TAP: input(NL_IN_TAP); break;
        case NL_GESTURE_SWIPE: input(NL_IN_SWIPE); break;
        case NL_GESTURE_NONE: break;
        }
        break;
    case LV_EVENT_PRESS_LOST:
        nl_touch_cancel(&s_touch);
        break;
    default:
        break;
    }
}

/** A timer's task, whose stack is in internal RAM, as an NVS write needs. A burst of swipes is one write. */
static void save_lamp(void *arg) {
    (void)arg;
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_u8(h, NVS_KEY_LAMP, (uint8_t)atomic_load(&s_lamp_to_save));
        if (err == ESP_OK) err = nvs_commit(h);
        nvs_close(h);
    }
    if (err != ESP_OK) ESP_LOGW(TAG, "lamp not saved: %s", esp_err_to_name(err));
}

static nl_lamp_t load_lamp(void) {
    nvs_handle_t h;
    uint8_t v = NL_LAMP_RING;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, NVS_KEY_LAMP, &v);
        nvs_close(h);
    }
    return v < NL_LAMP_COUNT ? (nl_lamp_t)v : NL_LAMP_RING;
}

static void home_task(void *arg) {
    (void)arg;
    yk_return_home();
    vTaskDelete(NULL);
}

static void on_leave(void *ctx) {
    (void)ctx;
    atomic_store(&s_leaving, true);  // the panel stays as it is until the restart
}

static const char *hint_text(nl_hint_t hint) {
    switch (hint) {
    case NL_HINT_ALWAYS: return T("已常开", "Always on");
    case NL_HINT_BACK_TO_30: return T("30 秒后关灯", "Off in 30 s");
    case NL_HINT_RING: return T("光圈", "Ring");
    case NL_HINT_MUSHROOM: return T("蘑菇灯", "Mushroom");
    case NL_HINT_MOON: return T("月牙", "Crescent");
    case NL_HINT_BATTERY: return T("电量 15%，记得充电", "Battery 15%: time to charge");
    case NL_HINT_NONE: break;
    }
    return "";
}

static const char *mode_name(nl_mode_t mode) {
    return mode == NL_LIT ? "lit" : mode == NL_ALWAYS ? "always" : "dark";
}

static void show_time(void) {
    const time_t now = time(NULL);
    if (now / 60 == s_shown_minute) return;
    s_shown_minute = now / 60;
    char text[8] = "--:--";
    struct tm tm;
    // By the time itself, not board_rtc_restored(): a clock Yay set survives the restart into this app in the chip's
    // absence, and a clock never set counts from 1970.
    if (now >= YK_APP_TIME_VALID_S && localtime_r(&now, &tm)) snprintf(text, sizeof(text), "%02d:%02d", tm.tm_hour, tm.tm_min);
    lv_label_set_text(s_time, text);
}

/** The crescent, drawn the first time it is shown (a third of a second's work; most people keep one lamp), and again
 *  in the other colour when the battery's amber flash comes while it is up. False with no room for it. */
static bool moon_ready(bool amber) {
    if (!s_moon_px) {
        s_moon_px = heap_caps_malloc(NL_ART_SIZE * NL_ART_SIZE * 2, MALLOC_CAP_SPIRAM);
        if (!s_moon_px) {
            ESP_LOGE(TAG, "no room for the crescent");
            return false;
        }
        s_moon_amber = !amber;  // drawn below
    }
    if (s_moon_amber != amber) {
        const int64_t at = now_ms();
        nl_art_moon(s_moon_px, amber ? YK_AMBER : YK_CREAM);
        s_moon_amber = amber;
        printf("NIGHT_LIGHT_MOON ms=%d\n", (int)(now_ms() - at));
    }
    s_moon_dsc = art_dsc(s_moon_px);
    return true;
}

static void show_lamp(nl_lamp_t lamp) {
    if (lamp == NL_LAMP_MOON && !moon_ready(s_shown_amber)) lamp = NL_LAMP_RING;
    if (lamp == NL_LAMP_RING && !s_ring_px) lamp = NL_LAMP_MUSHROOM;
    const bool moon = lamp == NL_LAMP_MOON, ring = lamp == NL_LAMP_RING;
    lv_image_set_src(s_art, moon ? &s_moon_dsc : ring ? &s_ring_dsc : &s_mushroom_dsc);
    // Light digits on the black of the ring and of the cut; dark ones on the mushroom's cream cap.
    lv_obj_set_style_text_color(s_time, lv_color_hex(moon || ring ? YK_INK : YK_PAPER), 0);
    lv_obj_align(s_time, LV_ALIGN_CENTER, moon ? MOON_X : 0, moon ? TIME_MOON_Y : ring ? TIME_RING_Y : TIME_ON_CAP_Y);
    lv_obj_set_pos(s_dot, YK_CX - DOT_SIZE / 2 + (moon ? MOON_X : 0), moon ? DOT_Y_MOON : ring ? DOT_Y_RING : DOT_Y_MUSHROOM);
    lv_obj_set_pos(s_hint, HINT_X + (moon ? MOON_X : 0), moon ? HINT_Y_MOON : ring ? HINT_Y_RING : HINT_Y_MUSHROOM);
}

static void show_amber(bool amber) {
    if (s_ring_px) nl_art_ring(s_ring_px, RING_AREA_PERCENT, amber ? YK_AMBER : YK_CREAM);  // in place: the image reads it
    if (s_moon_px) moon_ready(amber);
    lv_obj_invalidate(s_art);
}

/** What the rules say, drawn. Only what changed: the screen stays still, and off while the lamp is off. */
static void show(int dt_ms) {
    const nl_t *nl = &s_nl;
    if ((int)nl->mode != s_shown_mode) {
        s_shown_mode = nl->mode;
        printf("NIGHT_LIGHT_STATE mode=%s lamp=%d\n", mode_name(nl->mode), nl->lamp);
        lv_obj_set_hidden(s_dot, nl->mode != NL_ALWAYS);
        yk_app_keep_screen_on(nl->mode == NL_ALWAYS);
    }
    if ((int)nl->lamp != s_shown_lamp) {
        s_shown_lamp = nl->lamp;
        show_lamp(nl->lamp);
    }
    if ((int)nl->hint != s_shown_hint) {
        s_shown_hint = nl->hint;
        lv_label_set_text(s_hint, hint_text(nl->hint));
    }
    const bool amber = nl->amber_ms > 0;
    if (amber != s_shown_amber) {
        s_shown_amber = amber;
        show_amber(amber);
    }
    if (nl->mode != NL_DARK) show_time();

    int dx = 0, dy = 0;
    if (nl->mode == NL_ALWAYS) nl_shift(now_ms() / 60000, &dx, &dy);
    if (dx != s_shown_dx || dy != s_shown_dy) {
        s_shown_dx = dx;
        s_shown_dy = dy;
        lv_obj_set_pos(s_root, dx, dy);
    }

    if (atomic_load(&s_leaving)) return;
    const int full = yk_app_battery_low() ? YK_POWER_BRIGHTNESS_MAX : 100;
    const int level = nl_ramp(s_level, nl_level(nl, full, yk_app_brightness()), dt_ms);
    if (level != s_level) {
        s_level = level;
        board_brightness_raw_set((uint8_t)level);
    }
}

static void read_power(int64_t now) {
    if (now - s_power_at_ms < POWER_READ_MS) return;
    s_power_at_ms = now;
#ifdef CONFIG_YAY_DEV
    const int sim = atomic_load(&s_sim_percent);
    if (sim != -2) {
        nl_power(&s_nl, sim, atomic_load(&s_sim_usb));
        return;
    }
#endif
    board_battery_t battery;
    if (board_battery(&battery) == ESP_OK) nl_power(&s_nl, battery.percent, battery.usb);
}

/** LVGL timer: runs with the board lock held, so it may touch the screen. */
static void tick(lv_timer_t *timer) {
    (void)timer;
    const int64_t now = now_ms();
    const int dt = (int)(now - s_last_ms);
    s_last_ms = now;

    const unsigned pending = atomic_exchange(&s_pending, 0);
    if (pending & PENDING_SIDE_TAP) input(NL_IN_SIDE_TAP);
    if (pending & PENDING_SHAKE) input(NL_IN_SHAKE);
    if (pending & PENDING_FACE_DOWN) input(NL_IN_FACE_DOWN);
    if (pending & PENDING_TAP) input(NL_IN_TAP);
    if (pending & PENDING_LONG) input(NL_IN_LONG);
    if (pending & PENDING_SWIPE) input(NL_IN_SWIPE);
    if (nl_touch_poll(&s_touch, now)) input(NL_IN_LONG);

    // The system's screens open in their own time; asking each tick brings the panel up for them, lamp off or not.
    nl_tick(&s_nl, dt, yk_apps_is_open() || yk_apps_ask_is_open());
    read_power(now);

    if (s_nl.lamp_changed) {
        s_nl.lamp_changed = false;
        atomic_store(&s_lamp_to_save, s_nl.lamp);
        esp_timer_stop(s_save_timer);
        esp_timer_start_once(s_save_timer, SAVE_DELAY_US);
    }
    if (s_nl.go_home && !s_going_home) {
        s_going_home = true;
        printf("NIGHT_LIGHT_HOME percent=%d usb=%d\n", s_nl.percent, s_nl.usb);
        xTaskCreate(home_task, "nl_home", 4096, NULL, 5, NULL);  // it waits for this task: not from here
    }
    show(dt);
}

static esp_err_t on_command(const char *name, const cJSON *args, cJSON *reply, const char **code, void *ctx) {
    (void)ctx;
    (void)args;
    yk_app_input();
    esp_err_t err = yk_app_command(name, args, reply, code);  // app.exit, app.recovery, app.status
    if (err != ESP_ERR_NOT_SUPPORTED) return err;
    if (!strcmp(name, "nightlight.status")) {
        cJSON_AddStringToObject(reply, "mode", mode_name(s_nl.mode));
        cJSON_AddNumberToObject(reply, "lamp", s_nl.lamp);
        cJSON_AddNumberToObject(reply, "remainingMs", s_nl.remaining_ms);
        cJSON_AddNumberToObject(reply, "level", s_level);
        cJSON_AddNumberToObject(reply, "percent", s_nl.percent);
        cJSON_AddBoolToObject(reply, "usb", s_nl.usb);
        cJSON_AddBoolToObject(reply, "systemScreen", s_nl.ui_open);
        return ESP_OK;
    }
#ifdef CONFIG_YAY_DEV
    // Testing without a finger: the same inputs the screen and the sensor give, and a battery to try the rules on.
    if (!strcmp(name, "nightlight.input")) {
        const char *in = cJSON_GetStringValue(cJSON_GetObjectItem(args, "in"));
        static const struct { const char *name; unsigned bit; } inputs[] = {
            {"tap", PENDING_TAP}, {"long", PENDING_LONG}, {"swipe", PENDING_SWIPE},
            {"side", PENDING_SIDE_TAP}, {"shake", PENDING_SHAKE}, {"down", PENDING_FACE_DOWN},
        };
        for (size_t i = 0; in && i < sizeof(inputs) / sizeof(inputs[0]); i++) {
            if (strcmp(in, inputs[i].name)) continue;
            pend(inputs[i].bit);
            return ESP_OK;
        }
        *code = "BAD_INPUT";
        return ESP_ERR_INVALID_ARG;
    }
    if (!strcmp(name, "nightlight.power")) {
        const cJSON *percent = cJSON_GetObjectItem(args, "percent");
        atomic_store(&s_sim_usb, cJSON_IsTrue(cJSON_GetObjectItem(args, "usb")));
        atomic_store(&s_sim_percent, cJSON_IsNumber(percent) ? percent->valueint : -2);
        s_power_at_ms = -POWER_READ_MS;
        return ESP_OK;
    }
    // A time to see the digits by, on a round screen that has not been online. This run only: not the clock chip.
    if (!strcmp(name, "nightlight.clock")) {
        const cJSON *unix_s = cJSON_GetObjectItem(args, "unix");
        if (!cJSON_IsNumber(unix_s)) {
            *code = "BAD_INPUT";
            return ESP_ERR_INVALID_ARG;
        }
        const struct timeval tv = {.tv_sec = (time_t)unix_s->valuedouble};
        settimeofday(&tv, NULL);
        return ESP_OK;
    }
#endif
    *code = "UNKNOWN_COMMAND";
    return ESP_ERR_NOT_SUPPORTED;
}

static int heap_cmd(int argc, char **argv) {
    (void)argc, (void)argv;
    printf("HEAP internal=%u internal_min=%u psram=%u\n", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    return 0;
}

static void start_console(void) {
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    cfg.prompt = APP_SLUG ">";
    cfg.task_stack_size = 8192;
    esp_console_dev_usb_serial_jtag_config_t usb = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_serial_jtag(&usb, &cfg, &repl));
    esp_console_register_help_command();
    ESP_ERROR_CHECK(yk_link_register_console());
    ESP_ERROR_CHECK(yk_app_register_console());
    yk_ui_register_snapshot();
    const esp_console_cmd_t heap = {.command = "heap", .help = "available memory", .func = heap_cmd};
    ESP_ERROR_CHECK(esp_console_cmd_register(&heap));
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
}

static lv_image_dsc_t art_dsc(const void *px) {
    return (lv_image_dsc_t){
        .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565, .w = NL_ART_SIZE, .h = NL_ART_SIZE,
                   .stride = NL_ART_SIZE * 2},
        .data_size = NL_ART_SIZE * NL_ART_SIZE * 2,
        .data = px,
    };
}

/** The ring is drawn here, in PSRAM, so its width is one number; the mushroom comes from flash as it is. */
static void make_art(void) {
    s_mushroom_dsc = art_dsc(mushroom_start);
    s_ring_px = heap_caps_malloc(NL_ART_SIZE * NL_ART_SIZE * 2, MALLOC_CAP_SPIRAM);
    if (!s_ring_px) {
        ESP_LOGE(TAG, "no room for the ring; the mushroom only");
        return;
    }
    const int64_t at = now_ms();
    nl_art_ring(s_ring_px, RING_AREA_PERCENT, YK_CREAM);
    printf("NIGHT_LIGHT_RING ms=%d\n", (int)(now_ms() - at));
    s_ring_dsc = art_dsc(s_ring_px);
}

static void make_screen(void) {
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_scrollable(screen, false);
    lv_obj_set_style_bg_color(screen, lv_color_hex(YK_NIGHT), 0);

    s_root = lv_obj_create(screen);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, NL_ART_SIZE, NL_ART_SIZE);
    lv_obj_set_clickable(s_root, false);
    lv_obj_set_scrollable(s_root, false);

    s_art = lv_image_create(s_root);
    s_time = yk_ui_label(s_root, "", 0, 0, NL_ART_SIZE, YK_SIZE_HERO_MIN, YK_INK);
    s_hint = yk_ui_label(s_root, "", HINT_X, HINT_Y_RING, HINT_W, YK_SIZE_STATUS, YK_INK);

    s_dot = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_dot);
    lv_obj_set_size(s_dot, DOT_SIZE, DOT_SIZE);
    lv_obj_set_x(s_dot, YK_CX - DOT_SIZE / 2);
    lv_obj_set_style_radius(s_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_dot, lv_color_hex(YK_SAGE), 0);
    lv_obj_set_style_bg_opa(s_dot, LV_OPA_COVER, 0);
    lv_obj_set_clickable(s_dot, false);
    lv_obj_set_hidden(s_dot, true);

    lv_obj_t *touch = lv_obj_create(screen);
    lv_obj_remove_style_all(touch);
    lv_obj_set_size(touch, NL_ART_SIZE, NL_ART_SIZE);
    lv_obj_set_clickable(touch, true);
    lv_obj_set_scrollable(touch, false);
    lv_obj_set_gesture_bubble(touch, false);
    lv_obj_add_event_cb(touch, on_touch, LV_EVENT_ALL, NULL);

    lv_screen_load(screen);
    show_time();
    show_lamp(s_nl.lamp);
    s_shown_lamp = s_nl.lamp;
    lv_refr_now(NULL);

    // The first time in this app: an arrow at the side key, 「双击：应用和回家」, until the app ring was opened once.
    if (yk_hint_should(YK_HINT_APPS)) yk_hint_show(screen, YK_HINT_APPS, NULL);
}

void app_main(void) {
    // 1. NVS: never erase it (it is shared with every app and holds the Wi-Fi credentials).
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) ESP_LOGE(TAG, "NVS unavailable (%s); running without saved settings", esp_err_to_name(err));
    // 2. yaykit_app first, so that a crash anywhere below counts towards the crash loop guard.
    ESP_ERROR_CHECK(yk_app_start(&(yk_app_config_t){.slug = APP_SLUG, .version = APP_VERSION}));
    yk_app_on_leave(on_leave, NULL);
    s_english = yk_app_english();
    nl_init(&s_nl, load_lamp());
    const esp_timer_create_args_t save = {.callback = save_lamp, .name = "nl_save"};
    ESP_ERROR_CHECK(esp_timer_create(&save, &s_save_timer));
    // 3. Board and screen (dark until the first frame), the clock, then the side key.
    if (!board_display_start_opts(&(board_display_opts_t){.start_dark = true})) {
        ESP_LOGE(TAG, "display failed");
        return;
    }
    board_rtc_init();
    // The clock holds UTC and the kit gives apps no time zone yet: UTC+8 until board_pref_tz_apply() is in the kit.
    setenv("TZ", "CST-8", 1);
    tzset();
    // delay_tap: the first press of a double click (the app ring) is not a tap; a tap comes 0.45 s after its release.
    ESP_ERROR_CHECK(yk_key_init(&(yk_key_config_t){.delay_tap = true}));
    ESP_ERROR_CHECK(yk_app_on_key(on_key, NULL));
    // 4. Yaykit Link: `yk info` and `yk cmd` over USB (BLE stays off until asked for).
    const yk_link_config_t link = {.board = "tikpal-r1", .app_slug = APP_SLUG, .app_version = APP_VERSION,
                                   .layout = "yaykit-v1", .idle_seconds = 90, .on_command = on_command};
    ESP_ERROR_CHECK(yk_link_init(&link));
    // 5. Launch parameters: none are declared, the lamp just comes on. Read so the record does not linger.
    yk_launch_t launch;
    yk_app_launch(&launch);
    // Events only, at the lower rate: a shake and face down are all it listens for. Without the sensor the rest works.
    const esp_err_t motion = yk_motion_start(&(yk_motion_config_t){.odr_hz = 56, .on_event = on_motion});
    if (motion != ESP_OK) ESP_LOGW(TAG, "no motion: %s", esp_err_to_name(motion));

    // 6. The screen; then confirm the image (the bootloader would roll back an app that never gets here).
    make_art();
    board_lock(-1);
    ESP_ERROR_CHECK(cjk_font_init());
    make_screen();
    s_last_ms = now_ms();
    lv_timer_create(tick, TICK_MS, NULL);
    board_unlock();
    // 7. The console, then yk_app_ready(): its banner tells tools the app is up, so the console must already listen.
    start_console();
    yk_app_ready();
}
