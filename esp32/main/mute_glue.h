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

#include "led_status.h"

// Runs the UI (components/mute) on top of Home Link: Link owns Wi-Fi, BLE,
// setup credentials and the Hatch account; the UI is the avatar, voice and
// settings. Only built with CONFIG_MUTE_ENABLED.

// Before app_run(): registers Mute's BLE service and Link operations.
void mute_glue_start(void);
// From app_run(): NVS and identity are up, so Mute can start its UI.
void mute_glue_storage_ready(void);
// From app_run(): boot Wi-Fi/VM connect is done; Mute may drive the radios.
void mute_glue_link_ready(void);
// The LED backend for Mute builds: Link's status, shown on the display.
void mute_glue_led_state(led_state_t state);
