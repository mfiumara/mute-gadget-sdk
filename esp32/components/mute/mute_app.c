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

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "mute_audio.h"
#include "mute_battery.h"
#include "mute_board.h"
#include "mute_ble.h"
#include "mute_chat.h"
#include "mute_input.h"
#include "mute_settings.h"
#include "mute_state.h"
#include "mute_ui.h"
#include "mute_voice.h"
#include "mute_wifi.h"

static const char *TAG = "mute";

/* Applies saved settings to hardware; runs in whichever task changed them. */
static void on_setting(mute_setting_t what)
{
    switch (what) {
    case MUTE_SETTING_VOLUME:
        mute_audio_set_volume(mute_settings_volume());
        break;
    case MUTE_SETTING_MIC_GAIN:
        mute_audio_set_mic_gain(mute_settings_mic_gain());
        break;
    case MUTE_SETTING_WIFI:
        mute_wifi_apply();
        break;
    case MUTE_SETTING_BLE:
        mute_ble_apply();
        break;
    case MUTE_SETTING_HATCH:
        mute_hatch_config_changed();
        break;
    default:
        break;   /* brightness, sleep and the speaker are polled where they're used */
    }
}

const mute_board_t *mute_board;

void mute_app_run(const mute_board_t *board)
{
    mute_board = board;
    ESP_LOGI(TAG, "board: %s", board->name);
    ESP_ERROR_CHECK(board->init());
    ESP_ERROR_CHECK(mute_settings_init());
    mute_settings_set_listener(on_setting);
    mute_state_init();
    mute_battery_init();
    mute_state_set_caption("WAKING UP...");
    ESP_ERROR_CHECK(mute_ui_start());
    ESP_LOGI(TAG, "UI built: free internal %u", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

    QueueHandle_t q = xQueueCreate(16, sizeof(mute_input_event_t));
    ESP_ERROR_CHECK(mute_input_start(q));

    /* Let the boot animation (flame ignites, eyes open) play out. */
    vTaskDelay(pdMS_TO_TICKS(1400));
    if (mute_voice_start(q) != ESP_OK) {
        ESP_LOGE(TAG, "voice pipeline unavailable");
    } else {
        mute_state_set_mode(MUTE_MODE_IDLE);
        mute_state_set_caption("%s", "");   /* the button icons say how to talk */
    }

    mute_hatch_start();
    /* Home Link owns the radios; these just hand it the saved settings. */
    mute_wifi_apply();
    mute_ble_apply();
    ESP_LOGI(TAG, "ready: free heap %u internal, %u psram",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}
