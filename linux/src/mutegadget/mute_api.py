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

"""Client for the Mute server API: VM lookup."""

from __future__ import annotations

import json
import logging
import platform
import urllib.error
import urllib.request
from pathlib import Path

from mutegadget import __version__

log = logging.getLogger(__name__)

FETCH_PATH = "/fetch_vms"


def user_agent() -> str:
    details = []
    try:
        model = Path("/proc/device-tree/model").read_text(errors="replace")
        model = model.replace("\x00", "").strip()
        if model:
            details.append(model)
    except OSError:
        pass
    details.append(f"{platform.system()} {platform.machine()}".strip())
    return (
        f"mutegadget/{__version__} ({'; '.join(d for d in details if d)}) "
        f"Python/{platform.python_version()}"
    )


def fetch_vms_with_status(
    access_token: str, root: str
) -> tuple[list[dict], int | None]:
    """Leased VMs for the device token, plus the HTTP status if one arrived.

    Returns ``(vms, status)``; ``status`` is ``None`` when no HTTP response
    was obtained. A 401 means the device token was rejected, which a retry
    won't fix; a ``None`` status is a transport failure worth retrying.
    """
    req = urllib.request.Request(root + FETCH_PATH, method="GET")
    req.add_header("Authorization", f"Bearer {access_token}")
    req.add_header("X-API-Version", "1.0.0")
    req.add_header("User-Agent", user_agent())
    try:
        with urllib.request.urlopen(req, timeout=15) as resp:
            status = resp.getcode()
            data = json.loads(resp.read())
    except urllib.error.HTTPError as exc:
        log.error("VM fetch failed: HTTP %d", exc.code)
        return [], exc.code
    except (urllib.error.URLError, json.JSONDecodeError, OSError) as exc:
        log.error("VM fetch failed: %s", exc)
        return [], None

    if not isinstance(data, dict):
        log.error("VM fetch: unexpected response type %s", type(data).__name__)
        return [], status
    if data.get("error_title") or data.get("backend_error_code"):
        log.error("VM fetch error: %s %s",
                  data.get("error_title") or "", data.get("backend_error_code") or "")
        return [], status
    vm_list = data.get("vm_list")
    if not isinstance(vm_list, list):
        log.error("VM fetch: missing vm_list")
        return [], status

    vms = []
    for entry in vm_list:
        if not isinstance(entry, dict):
            continue
        vm_url = entry.get("vm_ws_url") or entry.get("vm_url")
        vm_token = entry.get("vm_auth_token")
        if vm_url and vm_token:
            vms.append({
                "vm_url": vm_url,
                "vm_auth_token": vm_token,
                "vm_name": entry.get("vm_name", ""),
                "vm_id": entry.get("vm_id", ""),
                "is_default": bool(entry.get("default", False)),
            })
    log.info("VM fetch: %d VMs", len(vms))
    return vms, status
