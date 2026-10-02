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
#include <stdint.h>

#include "mute_ble.h"
#include "mute_chat.h"
#include "mute_link.h"
#include "mute_wifi.h"

/*
 * In-memory services used by the desktop UI preview. Setters are intended to
 * run on the simulator's LVGL/event-loop thread before the next timer tick.
 */
void sim_services_reset(void);

void sim_services_set_wifi(mute_wifi_state_t state, const char *ssid);
void sim_services_set_ble(mute_ble_state_t state, const char *name, uint32_t passkey);
void sim_services_set_paired(bool paired);
void sim_services_set_chat_status(mute_hatch_state_t state, const char *detail);
void sim_services_set_link_state(mute_link_state_t state);
void sim_services_set_brightness(int pct);
void sim_services_set_speaker(bool on);
