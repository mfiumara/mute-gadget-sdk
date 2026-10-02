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

#include "mute_link.h"

#include <string.h>

#include "esp_wifi.h"

#include "mute_state.h"

static const mute_link_ops_t *s_ops;
static volatile mute_link_state_t s_state = MUTE_LINK_BOOT;

void mute_link_register(const mute_link_ops_t *ops)
{
    s_ops = ops;
}

void mute_link_set_state(mute_link_state_t state)
{
    if (state != s_state) {
        s_state = state;
        if (state == MUTE_LINK_CONFIRM) {
            mute_state_poke();      /* keep the screen awake for the prompt */
        }
    }
}

mute_link_state_t mute_link_state(void)
{
    return s_state;
}

const char *mute_link_state_name(mute_link_state_t state)
{
    switch (state) {
    case MUTE_LINK_BOOT: return "Starting";
    case MUTE_LINK_UNPAIRED: return "Ready to pair";
    case MUTE_LINK_PAIRING: return "App connected";
    case MUTE_LINK_CONFIRM: return "Confirm pairing";
    case MUTE_LINK_CONNECTING: return "Connecting";
    case MUTE_LINK_ONLINE: return "Online";
    case MUTE_LINK_OFFLINE: return "Offline";
    case MUTE_LINK_ERROR: return "Error";
    }
    return "";
}

bool mute_link_hatch_linked(void)
{
    return s_ops && s_ops->hatch_linked && s_ops->hatch_linked();
}

bool mute_link_hatch_vm(const char *want_vm, char *vm_id, size_t id_cap, char *vm_name, size_t name_cap,
                        char **vm_token)
{
    return s_ops && s_ops->hatch_vm && s_ops->hatch_vm(want_vm, vm_id, id_cap, vm_name, name_cap, vm_token);
}

bool mute_link_talk_press(void)
{
    return s_ops && s_ops->talk_press && s_ops->talk_press();
}

void mute_link_reset_setup(void)
{
    if (s_ops && s_ops->reset_setup) {
        s_ops->reset_setup();
    }
}

bool mute_link_wifi_get(char *ssid, char *pass)
{
    if (!s_ops || !s_ops->wifi_get) {
        return false;
    }
    s_ops->wifi_get(ssid, pass);
    return true;
}

bool mute_link_wifi_set(const char *ssid, const char *pass)
{
    if (!s_ops || !s_ops->wifi_set) {
        return false;
    }
    s_ops->wifi_set(ssid, pass);
    return true;
}

bool mute_link_ble_started(void)
{
    return s_ops && s_ops->ble_started && s_ops->ble_started();
}

bool mute_link_req_ready(void)
{
    return s_ops && s_ops->req_ready && s_ops->req_ready();
}

int64_t mute_link_req_open(const char *verb, const char *path, const char *const *headers, bool end_body,
                           mute_link_req_cb cb, void *ctx)
{
    return s_ops && s_ops->req_open ? s_ops->req_open(verb, path, headers, end_body, cb, ctx) : 0;
}

bool mute_link_req_send(int64_t id, const void *data, size_t len, bool end_body, int wait_ms)
{
    return s_ops && s_ops->req_send && s_ops->req_send(id, data, len, end_body, wait_ms);
}

void mute_link_req_cancel(int64_t id)
{
    if (s_ops && s_ops->req_cancel) {
        s_ops->req_cancel(id);
    }
}

void mute_link_ble_apply(void)
{
    if (s_ops && s_ops->ble_apply) {
        s_ops->ble_apply();
    }
}

/* ---- mute_wifi.h, backed by Link's wifi_mgr ---- */

esp_err_t mute_wifi_start(void)
{
    mute_wifi_apply();
    return ESP_OK;
}

void mute_wifi_apply(void)
{
    if (s_ops && s_ops->wifi_apply) {
        s_ops->wifi_apply();
    }
}

void mute_wifi_status(mute_wifi_status_t *out)
{
    if (s_ops && s_ops->wifi_status) {
        s_ops->wifi_status(out);
        return;
    }
    memset(out, 0, sizeof(*out));
    out->state = MUTE_WIFI_OFF;
    strlcpy(out->detail, "Starting...", sizeof(out->detail));
}

bool mute_wifi_connected(void)
{
    mute_wifi_status_t w;
    mute_wifi_status(&w);
    return w.state == MUTE_WIFI_CONNECTED;
}

void mute_wifi_power(mute_wifi_power_t level)
{
    static mute_wifi_power_t now = MUTE_WIFI_FULL;
    static wifi_ps_type_t before;   /* none, or modem sleep for BLE coexistence */
    if (level == now) {
        return;
    }
    if (s_ops && s_ops->power_save) {
        s_ops->power_save(level == MUTE_WIFI_REST);
    }
    if (now == MUTE_WIFI_FULL && esp_wifi_get_ps(&before) != ESP_OK) {
        before = WIFI_PS_NONE;   /* as wifi_mgr starts it */
    }
    now = level;
    /* Resting, the radio wakes for every tenth beacon (the listen interval
     * wifi_mgr joins with, about 1 s), not every DTIM; replies can wait that
     * long. */
    esp_wifi_set_ps(level == MUTE_WIFI_REST   ? WIFI_PS_MAX_MODEM
                    : level == MUTE_WIFI_DOZE ? WIFI_PS_MIN_MODEM
                                              : before);
}

void mute_wifi_nap(bool nap)
{
    if (s_ops && s_ops->wifi_nap) {
        s_ops->wifi_nap(nap);
    }
}

esp_err_t mute_wifi_scan(void)
{
    return s_ops && s_ops->wifi_scan ? s_ops->wifi_scan() : ESP_ERR_INVALID_STATE;
}

bool mute_wifi_scanning(void)
{
    return s_ops && s_ops->wifi_scanning && s_ops->wifi_scanning();
}

int mute_wifi_scan_results(mute_wifi_ap_t *out, int max, uint32_t *gen)
{
    if (s_ops && s_ops->wifi_scan_results) {
        return s_ops->wifi_scan_results(out, max, gen);
    }
    *gen = 0;
    return 0;
}

int mute_wifi_saved(mute_wifi_saved_t *out, int max)
{
    return s_ops && s_ops->wifi_saved ? s_ops->wifi_saved(out, max) : 0;
}

void mute_wifi_forget(const char *ssid)
{
    if (s_ops && s_ops->wifi_forget) {
        s_ops->wifi_forget(ssid);
    }
}
