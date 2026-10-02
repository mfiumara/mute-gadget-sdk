/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "mute_input.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#if CONFIG_PM_ENABLE
#include "esp_pm.h"
#endif

#include "mute_battery.h"
#include "mute_ble.h"
#include "mute_board.h"
#include "mute_chat.h"
#include "mute_console.h"
#include "mute_link.h"
#include "mute_mem.h"
#include "mute_menu.h"
#include "mute_settings.h"
#include "mute_state.h"
#include "mute_ui.h"
#include "mute_voice.h"
#include "mute_wifi.h"
#if CONFIG_MUTE_WATCHER_CAMERA
#include "boards/watcher_camera.h"
#endif

static const char *TAG = "mute_input";

#define POLL_MS 10
#define REST_POLL_MS 50        /* screen off and paused: still quick to wake */
#define REST_WAIT_MS 1000      /* paused, buttons that interrupt: the rest waits this long */
#define NAP_WAIT_MS 10000      /* ... and Wi-Fi napping */
#define POWER_MS 2000          /* refresh battery */
#define REST_POWER_MS 10000    /* ... while paused */
#define WIFI_NAP_MS (2 * 60 * 1000)   /* low power this long: Wi-Fi off until it ends */
#define DOUBLE_TICKS 35        /* 350 ms: a second aux press within this toggles phone setup */

#define GOODBYE_MS 1500        /* let the goodbye animation play */
#define HINT_TICKS 60          /* 0.6 s: warn that holding powers off */
#define LONG_TICKS 150         /* 1.5 s: power off */
#define SLEEP_CHECK_MS 100

#define SERIAL_RX 1024         /* the driver drops what doesn't fit, so a console line must */
#define SERIAL_LINE 1024
#define CHAT_MAX (192 * 1024)  /* a typed message, assembled from "chat+=" lines */

static QueueHandle_t s_queue;
static TaskHandle_t s_input;
static bool s_talk_down;
static bool s_cpu_low;      /* display stopped and the CPU allowed to sleep */
static volatile bool s_power_off_requested;
static volatile bool s_nap_now;   /* ">nap": asleep, as if on battery, nap without waiting WIFI_NAP_MS */

static void post(mute_ptt_t type, bool wake)
{
    mute_input_event_t ev = { .type = type, .wake = wake };
    ESP_LOGI(TAG, "PTT %s%s", type == MUTE_PTT_DOWN ? "down" : "up", wake ? " (waking)" : "");
    xQueueSend(s_queue, &ev, 0);
}

static bool update_power(void);

static void power_off(void)
{
    ESP_LOGI(TAG, "shutting down");
    mute_state_set_asleep(false);
    update_power();
    mute_state_set_progress(0);
    mute_state_set_level(0);
    mute_state_set_mode(MUTE_MODE_OFF);
    mute_state_set_caption("GOODBYE!");
    vTaskDelay(pdMS_TO_TICKS(GOODBYE_MS));
    esp_err_t err = mute_board->power_off();
    /* Only reached if the board couldn't power off. */
    vTaskDelay(pdMS_TO_TICKS(500));
    ESP_LOGE(TAG, "power-off failed (%s)", esp_err_to_name(err));
    mute_state_set_mode(MUTE_MODE_IDLE);
    mute_state_set_caption("COULDN'T POWER OFF");
}

static void set_asleep(bool asleep, const char *why)
{
    if (asleep != mute_state_asleep()) {
        ESP_LOGI(TAG, "%s (%s)", asleep ? "sleeping" : "waking", why);
        mute_state_set_asleep(asleep);
        if (s_input) {
            xTaskNotifyGive(s_input);   /* out of wait_buttons() */
        }
        if (!asleep && !mute_wifi_connected()) {
            mute_wifi_apply();   /* the screen says reconnecting: don't sit out the backoff */
        }
    }
}

static void toggle_phone_setup(void)
{
    bool on = !mute_settings_ble_on();
    ESP_LOGI(TAG, "phone setup %s", on ? "on" : "off");
    mute_settings_set_ble_on(on);
    mute_ble_status_t b;
    mute_ble_status(&b);
    if (on && b.name[0]) {
        mute_state_set_caption("PHONE SETUP: %s", b.name);
    } else {
        mute_state_set_caption("PHONE SETUP %s", on ? "ON" : "OFF");
    }
}

/*
 * Aux button: short press sleeps, a 1.5 s hold powers off, any press wakes.
 * Two quick presses toggle BLE phone setup, so the sleep waits a moment to
 * see whether a second press follows.
 */
static void aux_button(bool pressed, bool edge)
{
    static int held;
    static bool swallow;
    static bool hinted;
    static int sleep_in;    /* ticks until a pending single press sleeps */
    static char saved_caption[64];

    if (sleep_in && --sleep_in == 0) {
        set_asleep(true, mute_board->aux_button);
    }
    if (edge && pressed) {
        held = 0;
        hinted = false;
        swallow = mute_state_asleep();
        if (swallow) {
            set_asleep(false, mute_board->aux_button);
        } else if (sleep_in) {
            sleep_in = 0;
            swallow = true;
            toggle_phone_setup();
        }
        return;
    }
    if (pressed && !swallow) {
        held++;
        if (held == HINT_TICKS) {
            uint32_t v = UINT32_MAX;
            mute_state_caption(saved_caption, sizeof(saved_caption), &v);
            mute_state_set_caption("HOLD TO POWER OFF");
            hinted = true;
        } else if (held == LONG_TICKS) {
            swallow = true;
            power_off();
        }
        return;
    }
    if (edge && !pressed && !swallow) {
        if (hinted) {
            mute_state_set_caption("%s", saved_caption);   /* let go early: cancel */
        } else {
            sleep_in = DOUBLE_TICKS;
        }
    }
}

/* No touch: the aux button opens the menu and steps down it; any press wakes. */
static void menu_button(bool pressed, bool edge)
{
    if (!edge || !pressed) {
        return;
    }
    if (mute_state_asleep()) {
        set_asleep(false, mute_board->aux_button);
    } else if (!s_talk_down) {
        mute_state_poke();
        mute_menu_key(MUTE_MENU_DOWN);
    }
}

/* Talk button: push-to-talk, or Select while the menu is open. Asleep, the
 * press wakes and is posted as a waking one: mute_voice records only if it's
 * still held once awake. */
static void talk_button(unsigned ev)
{
    bool talk_down = s_talk_down;
    static bool swallow;
#if CONFIG_MUTE_WATCHER_CAMERA
    static TickType_t last_release;
    if ((ev & MUTE_BTN_TALK_PRESS) && !mute_state_asleep()
        && !mute_menu_is_open() && last_release
        && xTaskGetTickCount() - last_release <= pdMS_TO_TICKS(350)) {
        ESP_LOGI(TAG, "wheel double-click: camera preview/shutter");
        last_release = 0;
        watcher_camera_preview_toggle();
        swallow = true;
        return;
    }
#endif

    /* A quick tap can latch press and release in the same poll, and a release
     * can land just before the next press; keep them ordered. */
    bool released = ev & MUTE_BTN_TALK_RELEASE;
#if CONFIG_MUTE_WATCHER_CAMERA
    bool saw_release = released;
#endif
    if ((talk_down || swallow) && released) {
        if (talk_down) {
            post(MUTE_PTT_UP, false);
        }
        talk_down = swallow = false;
        released = false;
    }
    if (!talk_down && !swallow && (ev & MUTE_BTN_TALK_PRESS)) {
        if (mute_link_talk_press()) {
            /* Confirmed a Mute app pairing (Link's setup button). */
            mute_state_poke();
            swallow = true;
        } else if (mute_state_asleep()) {
            set_asleep(false, mute_board->talk_button);
            post(MUTE_PTT_DOWN, true);
            talk_down = true;
        } else if (mute_menu_is_open()) {
            mute_state_poke();
            mute_menu_key(MUTE_MENU_SELECT);
            swallow = true;
        } else {
            post(MUTE_PTT_DOWN, false);
            talk_down = true;
        }
    }
    if ((talk_down || swallow) && released) {
        if (talk_down) {
            post(MUTE_PTT_UP, false);
        }
        talk_down = swallow = false;
    }
#if CONFIG_MUTE_WATCHER_CAMERA
    if (saw_release && !swallow) {
        last_release = xTaskGetTickCount();
    }
#endif
    s_talk_down = talk_down;
}

/* A pairing prompt wakes the screen and keeps it on; otherwise idle sleeps. */
static void check_sleep(void)
{
    mute_ble_status_t ble;
    mute_ble_status(&ble);
    bool prompt = ble.passkey || mute_link_state() == MUTE_LINK_CONFIRM;
    if (prompt) {
        set_asleep(false, "pairing");
        return;
    }
    int after = mute_settings_sleep_s();
    float mode_t;
    if (after && !mute_state_asleep() && mute_state_mode(&mode_t) == MUTE_MODE_IDLE
        && mute_state_idle_secs() > after) {
        set_asleep(true, "auto-sleep");
    }
}

static void set_cpu_low(bool low)
{
#if CONFIG_PM_ENABLE
    esp_pm_config_t pm = {
        .max_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
        .min_freq_mhz = low ? CONFIG_XTAL_FREQ : CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
        .light_sleep_enable = low,
    };
    esp_err_t err = esp_pm_configure(&pm);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "power management: %s", esp_err_to_name(err));
    }
#else
    (void)low;
#endif
}

/*
 * On battery with the screen dark and voice resting: stop the display and let
 * the CPU drop to the crystal clock and light-sleep between polls, or until a
 * button interrupts (wait_buttons). Returns true while the display is stopped.
 * With a USB host attached (">nap" on the bench) the CPU stays at full speed:
 * at the crystal clock a long line of serial output can stall the USB console
 * until the host reopens the port.
 */
static bool update_power(void)
{
    static bool paused;
    bool pause = mute_board->display_pause && mute_state_on_battery() && mute_state_asleep()
                 && mute_ui_dark() && mute_voice_resting();
    bool want_low = pause && !mute_console_host();
    if (pause == paused && want_low == s_cpu_low) {
        return paused;
    }
    if (pause && !paused) {
        mute_board->display_pause(true);
    }
    if (want_low != s_cpu_low) {
        set_cpu_low(want_low);
    }
    if (!pause && paused) {
        mute_board->display_pause(false);
    }
    paused = pause;
    s_cpu_low = want_low;
    ESP_LOGI(TAG, "%s", s_cpu_low ? "low power: display paused"
                        : paused  ? "display paused (USB host: CPU at full speed)"
                                  : "full power");
    return paused;
}

/*
 * Low power for WIFI_NAP_MS: Wi-Fi off, since keeping it associated costs
 * more than the rest of the chip. It rejoins when the screen wakes or USB
 * power arrives; meanwhile nothing reaches the device over the network.
 * Not while a voice note recorded offline waits to go (for a while). Returns
 * true while napping.
 */
static bool update_wifi_nap(TickType_t now, bool paused)
{
    static TickType_t low_since;
    static bool napping;
    if (!s_cpu_low) {
        low_since = now;
    }
    if (!mute_state_asleep() && s_nap_now) {
        s_nap_now = false;
        mute_state_set_as_if_battery(false);
    }
    bool nap = paused && !mute_voice_notes_waiting()
               && (s_nap_now || (s_cpu_low && now - low_since >= pdMS_TO_TICKS(WIFI_NAP_MS)));
    if (nap != napping) {
        napping = nap;
        ESP_LOGI(TAG, "Wi-Fi %s", nap ? "napping" : "waking");
        mute_wifi_nap(nap);
    }
    return napping;
}

static void input_task(void *arg)
{
    (void)arg;
    bool aux_down = false;
    bool paused = false;
    TickType_t checked = xTaskGetTickCount() - pdMS_TO_TICKS(SLEEP_CHECK_MS);
    TickType_t powered = xTaskGetTickCount() - pdMS_TO_TICKS(POWER_MS);

    for (;;) {
        unsigned ev = mute_board->poll_buttons();
        if (ev & (MUTE_BTN_TALK_PRESS | MUTE_BTN_TALK_RELEASE)) {
            ESP_LOGI(TAG, "talk key:%s%s", ev & MUTE_BTN_TALK_PRESS ? " press" : "",
                     ev & MUTE_BTN_TALK_RELEASE ? " release" : "");
            talk_button(ev);
        }
        bool aux_edge = false;
        if ((ev & MUTE_BTN_AUX_PRESS) && !aux_down) {
            aux_down = aux_edge = true;
        } else if ((ev & MUTE_BTN_AUX_RELEASE) && aux_down) {
            aux_down = false;
            aux_edge = true;
        }
        if (mute_board->touch) {
            aux_button(aux_down, aux_edge);
        } else {
            menu_button(aux_down, aux_edge);
        }

        if (s_power_off_requested) {
            s_power_off_requested = false;
            power_off();
        }

        TickType_t now = xTaskGetTickCount();
        if (now - checked >= pdMS_TO_TICKS(SLEEP_CHECK_MS)) {
            checked = now;
            check_sleep();
        }

        if (now - powered >= pdMS_TO_TICKS(paused ? REST_POWER_MS : POWER_MS)) {
            powered = now;
            mute_power_t p = { .battery_pct = -1 };
            if (mute_board->read_power && mute_board->read_power(&p) == ESP_OK) {
                mute_state_set_power(&p);
                mute_battery_note_power(&p, mute_state_on_battery());
            }
        }

        paused = update_power();
        bool napping = update_wifi_nap(now, paused);
        mute_battery_note_state(mute_state_asleep(), s_cpu_low);
        /* Paused, anything but a button (a pairing prompt, an image) is
         * noticed within REST_WAIT_MS. Napping, nothing comes over the
         * network; USB power arriving is noticed within NAP_WAIT_MS. */
        if (paused && mute_board->wait_buttons) {
            mute_board->wait_buttons(napping ? NAP_WAIT_MS : REST_WAIT_MS);
        } else {
            vTaskDelay(pdMS_TO_TICKS(paused ? REST_POLL_MS : POLL_MS));
        }
    }
}

/* Reads the rest of a line into buf; false if it was too long (the rest is dropped). */
static bool read_line(char *buf, size_t cap)
{
    size_t len = 0;
    bool whole = true;
    uint8_t c;
    while (mute_console_getc(&c) && c != '\n') {
        if (c == '\r') {
            continue;
        }
        if (len < cap - 1) {
            buf[len++] = (char)c;
        } else {
            whole = false;
        }
    }
    buf[len] = '\0';
    return whole;
}

#if CONFIG_MUTE_HATCH
#define CHAT_OVER_SERIAL "true"

/*
 * "chat+=TEXT" adds a piece of a message and "chat=TEXT" adds the last piece
 * and sends it (TEXT escaped: \n \r \t \\). Every line is acknowledged, and the
 * host waits for that before the next: the driver drops what it has no room for.
 */
static char *s_chat;        /* the message so far */
static size_t s_chat_len;

static void chat_line(char *piece, bool last, bool whole)
{
    size_t n = mute_hatch_unescape(piece);
    if (!s_chat) {
        s_chat = heap_caps_malloc(CHAT_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        s_chat_len = 0;
    }
    const char *err = !whole ? "LINE TOO LONG" : !s_chat ? "OUT OF MEMORY" : s_chat_len + n >= CHAT_MAX ? "TOO LONG" : NULL;
    if (err) {
        free(s_chat);
        s_chat = NULL;
        mute_hatch_console("error", err, NULL);
        return;
    }
    memcpy(s_chat + s_chat_len, piece, n);
    s_chat_len += n;
    s_chat[s_chat_len] = '\0';
    mute_hatch_console("ack", NULL, "\"bytes\":%u", (unsigned)s_chat_len);
    if (last) {
        mute_hatch_text_turn(s_chat);   /* frees it */
        s_chat = NULL;
    }
}

/* Drops a half-sent message and ends a typed turn. */
static void chat_cancel(void)
{
    free(s_chat);
    s_chat = NULL;
    mute_hatch_text_cancel();
}
#else
#define CHAT_OVER_SERIAL "false"   /* no PSRAM: replies come over Link, and only short ones */
#endif

/*
 * Bench: puts the face in a mode ("face=thinking") until the voice path or
 * another "face=" moves it on. "face=happy" goes back to idle with the happy
 * hop, as a finished turn does.
 */
static void set_face(const char *name)
{
    static const char *const modes[MUTE_MODE_COUNT] = {
        [MUTE_MODE_BOOT] = "boot",
        [MUTE_MODE_IDLE] = "idle",
        [MUTE_MODE_LISTENING] = "listening",
        [MUTE_MODE_THINKING] = "thinking",
        [MUTE_MODE_SPEAKING] = "speaking",
        [MUTE_MODE_ERROR] = "error",
        [MUTE_MODE_OFF] = "off",
    };
    mute_state_poke();
    bool happy = !strcmp(name, "happy");
    if (happy) {
        name = "idle";   /* where a finished turn hops to */
    }
    for (int m = 0; m < MUTE_MODE_COUNT; m++) {
        if (!strcmp(name, modes[m])) {
            mute_state_set_mode(MUTE_MODE_IDLE);   /* only idle leaves "off" */
            mute_state_set_mode((mute_mode_t)m);
            if (happy) {
                mute_state_make_happy();
            }
            return;
        }
    }
    printf("@face.error unknown face \"%s\": boot idle listening thinking speaking error off happy\n", name);
    fflush(stdout);
}

/*
 * Console-only commands; false for setup commands. Their buffers are taken
 * per command: without PSRAM, static ones would hold internal RAM for good.
 */
static bool console_command(char *line, bool whole)
{
    if (!strcmp(line, "status")) {
        size_t cap = 1024;   /* long SSID, host and VM names escaped: past 512 */
        char *json = heap_caps_malloc(cap, MUTE_BIG_CAPS);
        if (json) {
            mute_ble_status_json(json, cap);
            printf("@status {\"board\":\"%s\",\"chat\":%s,\"device\":%s}\n", mute_board->name,
                   CHAT_OVER_SERIAL, json);
            fflush(stdout);
            free(json);
        }
        return true;
    }
    bool reset = !strcmp(line, "power.reset");
    if (reset || !strcmp(line, "power")) {
        if (reset) {
            mute_battery_reset();
        }
        const char *saved = mute_battery_saved_json();
        if (saved) {
            printf("@power.saved %s\n", saved);
        }
        size_t cap = 2048;   /* two dozen power locks */
        char *json = heap_caps_malloc(cap, MUTE_BIG_CAPS);
        if (json) {
            mute_battery_json(json, cap);
            printf("@power %s\n", json);
            free(json);
        }
        fflush(stdout);
        return true;
    }
    if (!strcmp(line, "nap")) {
        mute_state_set_as_if_battery(true);
        set_asleep(true, "serial");
        s_nap_now = true;
        return true;
    }
    if (!strncmp(line, "face=", 5)) {
        set_face(line + 5);
        return true;
    }
    if (strncmp(line, "chat", 4) != 0) {
        return false;
    }
#if CONFIG_MUTE_HATCH
    if (!strcmp(line, "chat.cancel")) {
        chat_cancel();
        return true;
    }
    bool last = !strncmp(line, "chat=", 5);
    if (last || !strncmp(line, "chat+=", 6)) {
        chat_line(line + (last ? 5 : 6), last, whole);
        return true;
    }
    return false;
#else
    (void)whole;
    mute_hatch_console("error", "THIS BOARD CAN'T CHAT OVER SERIAL", NULL);
    return true;
#endif
}

/*
 * Bench testing over the USB cable: 'd' / 'u' act as the talk button going
 * down / up, so the voice path can be driven without a finger on the button;
 * 'm' plays a built-in MP3 through the reply decoder; 'a' / 's' press the
 * menu's Down / Select; 'p' sends a screenshot; 'z' / 'w' sleep and wake.
 * A line starting with '>' is a setup command, the same "key=value" text as
 * the BLE CMD characteristic, or one of the console's own: "status" prints
 * the device's state, "power" the battery meter (mute_battery.h) and
 * "power.reset" starts it over, "nap" sleeps and leaves Wi-Fi at once (as
 * two minutes asleep on battery would; 'w' rejoins), "face=" shows a face
 * (see set_face), and "chat=" sends a typed message to Hatch (see chat_line
 * and tools/mute/chat.py).
 */
static void serial_task(void *arg)
{
    if (mute_console_install(SERIAL_RX) != ESP_OK) {
        vTaskDelete(NULL);
    }
    for (;;) {
        uint8_t c;
        if (!mute_console_getc(&c)) {
            continue;
        }
        if (c == 'm') {
            mute_voice_request_mp3test();
        } else if (c == 'a' || c == 's') {
            mute_state_poke();
            mute_menu_key(c == 'a' ? MUTE_MENU_DOWN : MUTE_MENU_SELECT);
        } else if (c == 'p') {
            mute_ui_request_snapshot();
        } else if (c == 'z' || c == 'w') {
            set_asleep(c == 'z', "serial");
        } else if (c == '>') {
            char *line = heap_caps_malloc(SERIAL_LINE, MUTE_BIG_CAPS);
            char none[1];   /* no room: the line is read and dropped */
            bool whole = read_line(line ? line : none, line ? SERIAL_LINE : sizeof(none));
            if (line && !console_command(line, whole)) {
                mute_ble_command(line);
            }
            free(line);
        } else if (c == 'd' || c == 'u') {
            mute_state_poke();
            post(c == 'd' ? MUTE_PTT_DOWN : MUTE_PTT_UP, false);
        }
    }
}

esp_err_t mute_input_start(QueueHandle_t queue)
{
    s_queue = queue;
    if (xTaskCreate(input_task, "mute_input", 4096, NULL, 6, &s_input) != pdPASS) {
        return ESP_FAIL;
    }
    /* Bench-test and setup console; the input still works if it can't start. */
    xTaskCreate(serial_task, "mute_serial", 3584, NULL, 5, NULL);
    return ESP_OK;
}

void mute_input_request_power_off(void)
{
    s_power_off_requested = true;
}
