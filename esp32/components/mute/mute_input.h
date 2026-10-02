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

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/*
 * Buttons, as the board reports them. The talk button is push-to-talk. On touch
 * boards the aux button puts the screen to sleep; holding it 1.5 s powers off,
 * and two quick presses toggle BLE phone setup. Without touch it opens the
 * two-button menu instead (mute_menu.h), where talk selects. Either button
 * wakes from sleep; the talk button's press is also posted, marked `wake`, so
 * holding it on through waking records a note. Waking also retries Wi-Fi at
 * once if it's down. Also runs auto-sleep and refreshes battery status into
 * mute_state.
 */

typedef enum {
    MUTE_PTT_DOWN,
    MUTE_PTT_UP,
} mute_ptt_t;

typedef struct {
    mute_ptt_t type;
    bool wake;   /* a press that woke the screen: a tap only wakes */
} mute_input_event_t;

/* PTT events are posted to `queue` (items are mute_input_event_t). */
esp_err_t mute_input_start(QueueHandle_t queue);

/* Plays the goodbye animation and powers off (from the input task). */
void mute_input_request_power_off(void);
