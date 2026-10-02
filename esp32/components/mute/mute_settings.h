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

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

/*
 * User settings, persisted in NVS. Setters save immediately and then notify the
 * change listener (from the caller's task), which applies the setting to the
 * hardware. Brightness, auto-sleep and the speaker are polled where they're used.
 */

#define MUTE_SSID_MAX 32
#define MUTE_PASS_MAX 64
#define MUTE_HOST_MAX 63
#define MUTE_VM_MAX 63
#define MUTE_TOKEN_MAX 1023

#define MUTE_MIC_GAIN_MAX 36      /* dB; ES7210 PGA, applied in 3 dB steps */

typedef enum {
    MUTE_SETTING_VOLUME,
    MUTE_SETTING_SPEAKER,
    MUTE_SETTING_MIC_GAIN,
    MUTE_SETTING_BRIGHTNESS,
    MUTE_SETTING_SLEEP,
    MUTE_SETTING_WIFI,          /* on/off or credentials */
    MUTE_SETTING_BLE,
    MUTE_SETTING_HATCH,
} mute_setting_t;

typedef void (*mute_setting_cb_t)(mute_setting_t what);

esp_err_t mute_settings_init(void);
void mute_settings_set_listener(mute_setting_cb_t cb);

int mute_settings_volume(void);         /* 0..100 */
bool mute_settings_speaker_on(void);    /* off: replies are shown, not played */
int mute_settings_mic_gain(void);       /* dB, 0..MUTE_MIC_GAIN_MAX */
int mute_settings_brightness(void);     /* 10..100 */
int mute_settings_sleep_s(void);        /* 0 = never */
bool mute_settings_wifi_on(void);
bool mute_settings_ble_on(void);

void mute_settings_wifi(char ssid[MUTE_SSID_MAX + 1], char pass[MUTE_PASS_MAX + 1]);
void mute_settings_hatch_host(char out[MUTE_HOST_MAX + 1]);
void mute_settings_hatch_vm(char out[MUTE_VM_MAX + 1]);
void mute_settings_hatch_token(char out[MUTE_TOKEN_MAX + 1]);
size_t mute_settings_hatch_token_len(void);

void mute_settings_set_volume(int pct);
void mute_settings_set_speaker_on(bool on);
void mute_settings_set_mic_gain(int db);
void mute_settings_set_brightness(int pct);
void mute_settings_set_sleep_s(int secs);
void mute_settings_set_wifi_on(bool on);
void mute_settings_set_ble_on(bool on);
/* A network name is remembered first among the saved ones and joined now;
 * an empty ssid forgets every saved network. */
void mute_settings_set_wifi(const char *ssid, const char *pass);
void mute_settings_set_hatch_host(const char *host);
void mute_settings_set_hatch_vm(const char *vm);
/* append=true adds to the stored token (for chunked BLE writes). */
esp_err_t mute_settings_set_hatch_token(const char *token, bool append);
