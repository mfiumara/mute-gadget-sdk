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

"""Keep a paired device connected to its Mute.

Each round fetches the VMs with the device token (which also yields a
per-VM bearer), connects to the default VM and serves commands until the
connection ends. Failures back off exponentially. A session that stayed up
for a while resets the backoff.
"""

from __future__ import annotations

import asyncio
import json
import logging
import os
import re
import signal
import socket
import time
from dataclasses import dataclass, field

from mutegadget import __version__, config, mute_api
from mutegadget.executor import COMMAND_SPECS, Executor
from mutegadget.identity import Identity
from mutegadget.link_client import DeviceDescription, LinkSession, Outcome

log = logging.getLogger(__name__)

BACKOFF_BASE_S = 2.0
BACKOFF_MAX_S = 60.0
AUTH_BACKOFF_MIN_S = 15.0
HEALTHY_SESSION_S = 30.0
UNPAIRED_POLL_S = 30.0
MAX_LOCAL_REQUEST = 64 * 1024
_SESSION_ID_RE = re.compile(r"[A-Za-z0-9-]{1,64}")
TOKEN_RETRY_S = 300


@dataclass
class Backoff:
    failures: int = 0
    floor: float = 0.0

    def next_delay(self) -> float:
        delay = min(BACKOFF_BASE_S * (2 ** self.failures), BACKOFF_MAX_S)
        self.failures += 1
        return max(delay, self.floor)

    def reset(self) -> None:
        self.failures = 0
        self.floor = 0.0


@dataclass
class Service:
    identity: Identity
    executor: Executor
    display_name: str = field(default_factory=socket.gethostname)
    _stop: asyncio.Event = field(default_factory=asyncio.Event)
    _current: LinkSession | None = None

    def stop(self) -> None:
        self._stop.set()

    async def run(self) -> None:
        backoff = Backoff()
        while not self._stop.is_set():
            pairing = config.load_json(config.PAIRING_FILE)
            if not pairing:
                log.info("not paired; run `mutegadget pair` to set up")
                await self._sleep(UNPAIRED_POLL_S)
                continue
            vms, status = await asyncio.to_thread(
                mute_api.fetch_vms_with_status, pairing["access_token"], pairing["server"],
            )
            if status == 401:
                log.warning("device token rejected by the server; run `mutegadget pair` again")
                await self._sleep(TOKEN_RETRY_S)
                continue
            vm = next((v for v in vms if v["is_default"]), vms[0] if vms else None)
            if vm is None:
                await self._sleep(backoff.next_delay())
                continue

            outcome, lasted = await self._session(vm, pairing)
            if outcome is Outcome.STOPPED:
                return
            if outcome is Outcome.UNPAIRED:
                config.delete_json(config.PAIRING_FILE)
                log.warning("pairing removed; run `mutegadget pair` to set up again")
                continue
            if lasted >= HEALTHY_SESSION_S:
                backoff.reset()
            if outcome in (Outcome.AUTH_REJECTED, Outcome.FORBIDDEN):
                backoff.floor = AUTH_BACKOFF_MIN_S
            delay = backoff.next_delay()
            log.info("reconnecting in %.0fs", delay)
            await self._sleep(delay)

    async def _session(self, vm: dict, pairing: dict) -> tuple[Outcome, float]:
        device = DeviceDescription(
            node_id=self.identity.node_id,
            display_name=self.display_name,
            version=__version__,
            commands=COMMAND_SPECS,
        )
        session = LinkSession(
            server=pairing["server"],
            vm_id=vm["vm_id"] or vm["vm_name"],
            vm_auth_token=vm["vm_auth_token"],
            device=device,
            run_command=self.executor.run,
        )
        log.info("connecting to %s", vm["vm_name"] or vm["vm_id"])
        started = time.monotonic()
        self._current = session
        try:
            outcome = await session.run(self._stop)
        except Exception as exc:
            log.warning("session failed: %s: %s", type(exc).__name__, exc)
            outcome = Outcome.CLOSED
        finally:
            self._current = None
        lasted = time.monotonic() - (session.registered_at or time.monotonic())
        log.info("session ended: %s after %.0fs", outcome.value, time.monotonic() - started)
        return outcome, lasted

    # -- Local socket -----------------------------------------------------------

    async def serve_local(self, path) -> asyncio.AbstractServer:
        """Accept messages for the Mute from programs on this device.

        Each connection sends one JSON line, ``{"message": "..."}`` plus an
        optional ``"session_id"`` naming a side chat, and gets one JSON line
        back. Only root and the command account's group can
        connect.
        """
        path.parent.mkdir(mode=0o755, parents=True, exist_ok=True)
        if path.exists():
            path.unlink()
        server = await asyncio.start_unix_server(self._handle_local, str(path),
                                                 limit=MAX_LOCAL_REQUEST)
        if os.geteuid() == 0:
            os.chown(path, 0, self.executor.account.gid)
        os.chmod(path, 0o660)
        log.info("accepting messages on %s", path)
        return server

    async def _handle_local(self, reader, writer) -> None:
        try:
            line = await asyncio.wait_for(reader.readline(), 10)
            reply = await self._local_request(line)
        except Exception as exc:
            reply = {"ok": False, "error": f"{type(exc).__name__}: {exc}"}
        writer.write(json.dumps(reply).encode() + b"\n")
        try:
            await writer.drain()
        finally:
            writer.close()

    async def _local_request(self, line: bytes) -> dict:
        request = json.loads(line)
        message = request.get("message") if isinstance(request, dict) else None
        if not isinstance(message, str) or not message.strip():
            return {"ok": False, "error": "expected {\"message\": \"...\"}"}
        session_id = request.get("session_id")
        if session_id is not None and not (
            isinstance(session_id, str) and _SESSION_ID_RE.fullmatch(session_id)
        ):
            return {"ok": False, "error": "session_id must be letters, digits and dashes"}
        session = self._current
        if session is None or session.registered_at is None:
            return {"ok": False, "error": "not connected to the Mute"}
        log.info("forwarding a %d-character message to the Mute", len(message))
        return await session.send_chat(message, session_id)

    async def _sleep(self, seconds: float) -> None:
        try:
            await asyncio.wait_for(self._stop.wait(), seconds)
        except asyncio.TimeoutError:
            pass


def run_service(identity: Identity, executor: Executor) -> None:
    async def main() -> None:
        service = Service(identity=identity, executor=executor)
        loop = asyncio.get_running_loop()
        for signum in (signal.SIGTERM, signal.SIGINT):
            loop.add_signal_handler(signum, service.stop)
        server = await service.serve_local(config.socket_path())
        try:
            await service.run()
        finally:
            server.close()

    asyncio.run(main())
