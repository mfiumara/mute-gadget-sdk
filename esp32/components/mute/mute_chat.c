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

#include "mute_chat_priv.h"

#include <string.h>

#include "freertos/FreeRTOS.h"

#include "mute_link.h"
#include "mute_settings.h"
#include "mute_wifi.h"

/* Connection state as last reported by the session task (mute_chat_session.cpp). */
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static mute_hatch_state_t s_result = MUTE_HATCH_UNTESTED;
static char s_detail[48];

void mute_hatch_report(mute_hatch_state_t state, const char *detail)
{
    portENTER_CRITICAL(&s_lock);
    s_result = state;
    strlcpy(s_detail, detail, sizeof(s_detail));
    portEXIT_CRITICAL(&s_lock);
}

bool mute_hatch_configured(void)
{
    return mute_settings_hatch_token_len() > 0 || mute_link_hatch_linked();
}

void mute_hatch_test(void)
{
    if (!mute_hatch_configured() || !mute_wifi_connected()) {
        return;
    }
    mute_hatch_report(MUTE_HATCH_TESTING, "Connecting...");
    mute_hatch_chat_connect();
}

void mute_hatch_config_changed(void)
{
    mute_hatch_report(MUTE_HATCH_UNTESTED, "");
    mute_hatch_chat_forget();
}

void mute_hatch_status(mute_hatch_status_t *out)
{
    if (!mute_hatch_configured()) {
        out->state = MUTE_HATCH_NOT_SET;
        strlcpy(out->detail, "Set a device token", sizeof(out->detail));
        return;
    }
    portENTER_CRITICAL(&s_lock);
    out->state = s_result;
    strlcpy(out->detail, s_detail, sizeof(out->detail));
    portEXIT_CRITICAL(&s_lock);
    if (!mute_wifi_connected() && out->state != MUTE_HATCH_TESTING) {
        out->state = MUTE_HATCH_OFFLINE;
        strlcpy(out->detail, "Waiting for Wi-Fi", sizeof(out->detail));
    } else if (out->state == MUTE_HATCH_UNTESTED && !out->detail[0]) {
        strlcpy(out->detail, "Connects when you talk", sizeof(out->detail));
    }
}

const char *mute_hatch_state_name(mute_hatch_state_t state)
{
    switch (state) {
    case MUTE_HATCH_NOT_SET: return "Not set up";
    case MUTE_HATCH_OFFLINE: return "Offline";
    case MUTE_HATCH_UNTESTED: return "Saved";
    case MUTE_HATCH_TESTING: return "Connecting";
    case MUTE_HATCH_REACHABLE: return "Connected";
    case MUTE_HATCH_UNREACHABLE: return "Can't connect";
    }
    return "";
}
