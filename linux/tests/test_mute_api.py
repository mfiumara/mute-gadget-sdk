# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

from __future__ import annotations

import io
import json

from mutegadget import mute_api


def test_fetch_vms_parses_the_vm_list(monkeypatch):
    seen = []

    def urlopen(req, timeout):
        seen.append((req.full_url, req.get_header("Authorization")))
        body = {"vm_list": [{"vm_id": "mute", "vm_ws_url": "wss://h/v1/noise",
                             "vm_auth_token": "t", "default": True}, {"vm_id": "no-token"}]}
        resp = io.BytesIO(json.dumps(body).encode())
        resp.getcode = lambda: 200
        return resp

    monkeypatch.setattr(mute_api.urllib.request, "urlopen", urlopen)
    vms, status = mute_api.fetch_vms_with_status("tok", "https://h")
    assert status == 200 and [v["vm_id"] for v in vms] == ["mute"] and vms[0]["is_default"]
    assert seen == [("https://h/fetch_vms", "Bearer tok")]


def test_a_rejected_token_reports_401(monkeypatch):
    def urlopen(req, timeout):
        raise mute_api.urllib.error.HTTPError(req.full_url, 401, "", {}, io.BytesIO(b""))

    monkeypatch.setattr(mute_api.urllib.request, "urlopen", urlopen)
    assert mute_api.fetch_vms_with_status("tok", "https://h") == ([], 401)
