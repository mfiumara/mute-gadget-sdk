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

#include "sim_services.h"

#include <stdio.h>
#include <string.h>

#include "mute_console.h"
#include "mute_menu.h"
#include "mute_settings.h"
#include "mute_settings_ui.h"

#define SIM_DEFAULT_NAME "MuteGadget-SIM001"
#define SIM_DEFAULT_SSID "Mute Simulator"

static mute_wifi_status_t s_wifi = {
    .state = MUTE_WIFI_CONNECTED,
    .ssid = SIM_DEFAULT_SSID,
    .ip = "192.0.2.2",
    .rssi = -45,
};
static mute_ble_status_t s_ble = {
    .state = MUTE_BLE_CONNECTED,
    .secure = true,
    .name = SIM_DEFAULT_NAME,
};
static mute_hatch_status_t s_chat = {
    .state = MUTE_HATCH_REACHABLE,
};
static mute_link_state_t s_link = MUTE_LINK_ONLINE;
static int s_brightness = 75;
static bool s_speaker = true;

static void copy_text(char *out, size_t cap, const char *text)
{
    if (!cap) {
        return;
    }
    if (!text) {
        text = "";
    }
    size_t n = strlen(text);
    if (n >= cap) {
        n = cap - 1;
    }
    memcpy(out, text, n);
    out[n] = '\0';
}

void sim_services_reset(void)
{
    s_wifi = (mute_wifi_status_t){
        .state = MUTE_WIFI_CONNECTED,
        .ssid = SIM_DEFAULT_SSID,
        .ip = "192.0.2.2",
        .rssi = -45,
    };
    s_ble = (mute_ble_status_t){
        .state = MUTE_BLE_CONNECTED,
        .secure = true,
        .name = SIM_DEFAULT_NAME,
    };
    s_chat = (mute_hatch_status_t){
        .state = MUTE_HATCH_REACHABLE,
    };
    s_link = MUTE_LINK_ONLINE;
    s_brightness = 75;
    s_speaker = true;
}

void sim_services_set_wifi(mute_wifi_state_t state, const char *ssid)
{
    s_wifi.state = state;
    copy_text(s_wifi.ssid, sizeof(s_wifi.ssid), ssid);
    copy_text(s_wifi.ip, sizeof(s_wifi.ip), state == MUTE_WIFI_CONNECTED ? "192.0.2.2" : "");
    s_wifi.rssi = state == MUTE_WIFI_CONNECTED ? -45 : 0;
    s_wifi.detail[0] = '\0';
}

void sim_services_set_ble(mute_ble_state_t state, const char *name, uint32_t passkey)
{
    s_ble.state = state;
    s_ble.passkey = passkey;
    s_ble.secure = state == MUTE_BLE_CONNECTED;
    copy_text(s_ble.name, sizeof(s_ble.name), name);
}

void sim_services_set_paired(bool paired)
{
    if (!paired) {
        s_chat.state = MUTE_HATCH_NOT_SET;
        s_chat.detail[0] = '\0';
    } else if (s_chat.state == MUTE_HATCH_NOT_SET) {
        s_chat.state = MUTE_HATCH_REACHABLE;
    }
}

void sim_services_set_chat_status(mute_hatch_state_t state, const char *detail)
{
    s_chat.state = state;
    copy_text(s_chat.detail, sizeof(s_chat.detail), detail);
}

void sim_services_set_link_state(mute_link_state_t state)
{
    s_link = state;
}

void sim_services_set_brightness(int pct)
{
    if (pct < 10) {
        pct = 10;
    } else if (pct > 100) {
        pct = 100;
    }
    s_brightness = pct;
}

void sim_services_set_speaker(bool on)
{
    s_speaker = on;
}

int mute_settings_brightness(void)
{
    return s_brightness;
}

bool mute_settings_speaker_on(void)
{
    return s_speaker;
}

void mute_settings_set_brightness(int pct)
{
    sim_services_set_brightness(pct);
}

void mute_settings_set_speaker_on(bool on)
{
    sim_services_set_speaker(on);
}

void mute_wifi_status(mute_wifi_status_t *out)
{
    if (out) {
        *out = s_wifi;
    }
}

bool mute_wifi_connected(void)
{
    return s_wifi.state == MUTE_WIFI_CONNECTED;
}

void mute_ble_status(mute_ble_status_t *out)
{
    if (out) {
        *out = s_ble;
    }
}

void mute_hatch_status(mute_hatch_status_t *out)
{
    if (out) {
        *out = s_chat;
    }
}

mute_link_state_t mute_link_state(void)
{
    return s_link;
}

void mute_settings_ui_build(lv_obj_t *tile)
{
    lv_obj_t *label = lv_label_create(tile);
    lv_label_set_text(label, "Settings unavailable in preview");
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);
}

void mute_settings_ui_tick(bool visible)
{
    (void)visible;
}

bool mute_settings_ui_in_subpage(void)
{
    return false;
}

void mute_menu_key(mute_menu_key_t key)
{
    (void)key;
}

bool mute_menu_is_open(void)
{
    return false;
}

void mute_menu_build(lv_obj_t *parent, int w, int h)
{
    (void)parent;
    (void)w;
    (void)h;
}

bool mute_menu_tick(float now)
{
    (void)now;
    return false;
}

void mute_menu_close(void)
{
}

void mute_console_write(const void *buf, size_t n)
{
    if (buf && n) {
        (void)fwrite(buf, 1, n, stdout);
        (void)fflush(stdout);
    }
}
