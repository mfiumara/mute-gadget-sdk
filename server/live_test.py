#!/usr/bin/env python3
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

"""Smoke test a running Mute server with a real model, as real gadgets would.

    MUTE_DEVICE_TOKEN=... uv run --with ../linux live_test.py https://your-host [voice.wav]

Checks auth, the device API, a Linux gadget whose command the model runs, a
voice gadget's note (speech to text, reply, text to speech), and a voice
gadget driving the Linux gadget's command. Exits non-zero on the first failure.
"""

import asyncio
import base64
import json
import os
import ssl
import struct
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

from websockets.asyncio.client import connect

from mutegadget import mute_api
from mutegadget.link_client import DeviceDescription, LinkSession, noise_url
from mutegadget.noise import Header, NoiseTransport, NoiseXXInitiator

SERVER = sys.argv[1].rstrip("/")
TOKEN = os.environ["MUTE_DEVICE_TOKEN"]
SECRET = f"zebra-{os.getpid()}"
COMMANDS = {"system.run": {"description": "Run a shell command on this Linux machine",
                           "required": {"command": {"type": "string", "description": "the command"}}}}


def step(name):
    print(f"--- {name}", flush=True)


def http(path, method="GET", auth=TOKEN, body=None):
    req = urllib.request.Request(SERVER + path, method=method, data=body)
    if auth:
        req.add_header("Authorization", f"Bearer {auth}")
    try:
        with urllib.request.urlopen(req, timeout=30) as resp:
            return resp.status, json.loads(resp.read() or b"{}")
    except urllib.error.HTTPError as exc:
        return exc.code, None


class VoiceGadget:
    """The ESP32 voice path: its own Noise session, no link.register."""

    async def open(self):
        self.ws = await connect(noise_url(SERVER, "mute"), max_size=None,
                                additional_headers={"Authorization": f"Bearer {TOKEN}"})
        hs = NoiseXXInitiator()
        hs.initialize()
        await self.ws.send(hs.write_message1())
        hs.read_message2(await self.ws.recv())
        await self.ws.send(hs.write_message3())
        self.t = NoiseTransport(*hs.split())
        self.bodies, self.done, self.lines = {}, {}, asyncio.Queue()
        self.reader = asyncio.ensure_future(self._read())
        self.sub = await self.request("POST", "/chat/subscribe", b"{}")

    async def _read(self):
        async for raw in self.ws:
            frame = self.t.decrypt_frame(raw)
            if frame is None or frame.kind == "reset":
                continue
            v = frame.value
            data, end = (v.body, v.end_body) if frame.kind == "response" else (v.data, v.end_body)
            if frame.stream_id == self.sub:
                for line in data.split(b"\n"):
                    if line.strip():
                        await self.lines.put(json.loads(line))
                continue
            self.bodies.setdefault(frame.stream_id, bytearray()).extend(data)
            if end and frame.stream_id in self.done:
                self.done[frame.stream_id].set_result(bytes(self.bodies.pop(frame.stream_id)))

    async def request(self, verb, path, body=b""):
        frames = self.t.encrypt_http_request(verb, path, body,
                                             headers=[Header("Content-Type", "application/json")])
        self.done[frames.stream_id] = asyncio.get_running_loop().create_future()
        for f in frames.frames:
            await self.ws.send(f)
        return frames.stream_id

    async def call(self, verb, path, body=b"", timeout=60):
        sid = await self.request(verb, path, body)
        return await asyncio.wait_for(self.done[sid], timeout)

    async def turn(self, note: dict):
        ack = json.loads(await self.call("POST", "/chat/stream", json.dumps(note).encode()))
        user_id = ack["result"]["message_id"]
        while True:
            line = await asyncio.wait_for(self.lines.get(), 120)
            payload = line["payload"]
            if line["event"] == "message.assistant" and payload["reply_to_message_id"] == user_id:
                return line


async def esp32_upgrade_and_frames():
    """Replay the firmware's exact upgrade, then check every server frame is one
    unfragmented binary frame: the ESP32 cannot reassemble fragments."""
    u = urllib.parse.urlsplit(SERVER)
    port = u.port or (443 if u.scheme == "https" else 80)
    reader, writer = await asyncio.open_connection(
        u.hostname, port, ssl=ssl.create_default_context() if u.scheme == "https" else None,
        server_hostname=u.hostname if u.scheme == "https" else None)
    writer.write((f"GET /v1/noise?vm_id=mute HTTP/1.1\r\nHost: {u.netloc}\r\n"
                  f"Authorization: Bearer {TOKEN}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                  "Sec-WebSocket-Version: 13\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n").encode())
    head = await reader.readuntil(b"\r\n\r\n")
    assert head.startswith(b"HTTP/1.1 101"), head[:80]
    assert len(head) < 4096, "the firmware reads at most 4 KB of upgrade response"

    def send(payload):
        mask = os.urandom(4)
        n = len(payload)
        hdr = bytes([0x82]) + (bytes([0x80 | n]) if n < 126 else bytes([0x80 | 126]) + struct.pack(">H", n))
        writer.write(hdr + mask + bytes(b ^ mask[i % 4] for i, b in enumerate(payload)))

    async def recv():
        while True:
            b0, b1 = await reader.readexactly(2)
            n = b1 & 0x7F
            if n == 126:
                n = struct.unpack(">H", await reader.readexactly(2))[0]
            elif n == 127:
                n = struct.unpack(">Q", await reader.readexactly(8))[0]
            data = await reader.readexactly(n)
            if b0 & 0x0F in (0x9, 0xA):  # ping/pong
                continue
            assert b0 == 0x82, f"fragmented or non-binary frame: {b0:#x}"
            return data

    hs = NoiseXXInitiator()
    hs.initialize()
    send(hs.write_message1())
    hs.read_message2(await recv())
    send(hs.write_message3())
    t = NoiseTransport(*hs.split())
    req = t.encrypt_http_request("GET", "/identity", b"")
    for f in req.frames:
        send(f)
    frame = t.decrypt_frame(await recv())
    assert frame.kind == "response" and frame.value.status == 200, frame
    writer.close()


async def main():
    step("auth is required")
    assert http("/fetch_vms", auth=None)[0] == 401
    assert http("/fetch_vms", auth="wrong")[0] == 401
    try:
        await connect(noise_url(SERVER, "mute"), additional_headers={"Authorization": "Bearer wrong"})
        raise AssertionError("WebSocket accepted a wrong token")
    except Exception as exc:
        assert "401" in str(exc), exc

    step("ESP32 upgrade request and unfragmented frames")
    await esp32_upgrade_and_frames()

    step("device API")
    vms, status = mute_api.fetch_vms_with_status(TOKEN, SERVER)
    assert status == 200 and vms and vms[0]["is_default"], (status, vms)
    status, tokens = http("/device_token/refresh", "POST", auth=f"hatch_refresh:{TOKEN}", body=b"{}")
    assert status == 200 and tokens["access_token"] == TOKEN

    step("Linux gadget registers and the model runs its command")
    ran = []

    def run_command(command, params, timeout_ms):
        ran.append((command, params))
        return {"ok": True, "payload": {"stdout": f"{SECRET}\n", "exit_code": 0}}

    pi = LinkSession(server=SERVER, vm_id=vms[0]["vm_id"], vm_auth_token=vms[0]["vm_auth_token"],
                     device=DeviceDescription(f"homelink-{os.getpid() % 0xffffff:06x}", "test-pi", "0.1.0", COMMANDS),
                     run_command=run_command)
    stop = asyncio.Event()
    pi_task = asyncio.ensure_future(pi.run(stop))
    deadline = time.monotonic() + 60
    while pi.registered_at is None:
        assert time.monotonic() < deadline and not pi_task.done(), "Linux gadget never registered"
        await asyncio.sleep(0.1)
    started = time.monotonic()
    reply = await pi.send_chat("Run `cat /etc/secret-word` on test-pi and tell me the word it prints.",
                               f"live-{os.getpid()}")
    print(f"    {time.monotonic() - started:.1f}s: {reply}")
    assert reply["ok"] and ran, (reply, ran)
    assert SECRET in reply["response"]["text"], reply

    step("voice gadget: identity, tunnel keepalive, history")
    voice = VoiceGadget()
    await voice.open()
    ident = json.loads(await voice.call("GET", "/identity"))
    assert ident["ok"] and ident["result"]["name"], ident
    tunnel = voice.t.start_stream_request("POST", "/link-tunnel")
    for f in tunnel.frames:
        await voice.ws.send(f)
    for f in voice.t.encrypt_body_chunk(tunnel.stream_id, b"\x00"):
        await voice.ws.send(f)

    step("voice gadget: text turn")
    started = time.monotonic()
    event = await voice.turn({"message": "Say hello in five words or fewer.", "output_modality": "text"})
    print(f"    {time.monotonic() - started:.1f}s: {event['payload']['display_text']!r}")
    assert event["payload"]["display_text"]

    step("voice gadget: speech for the reply")
    started = time.monotonic()
    mp3 = await voice.call("GET", f"/api/voice/tts-stream?message_id={event['message_id']}")
    print(f"    {time.monotonic() - started:.1f}s: {len(mp3)} bytes")
    assert len(mp3) > 1000 and (mp3[:3] == b"ID3" or mp3[0] == 0xFF), mp3[:16]

    if len(sys.argv) > 2:
        step("voice gadget: voice note")
        wav = open(sys.argv[2], "rb").read()
        started = time.monotonic()
        event = await voice.turn({"message": "", "output_modality": "voice", "items": [{
            "type": "file", "mime_type": "audio/wav", "filename": "voice_note.wav",
            "data_base64": base64.b64encode(wav).decode()}]})
        print(f"    {time.monotonic() - started:.1f}s: {event['payload']['display_text']!r}")

    step("voice gadget drives the Linux gadget")
    ran.clear()
    event = await voice.turn({"message": "Ask test-pi for its secret word by running `cat /etc/secret-word`, "
                                         "then tell me the word.", "output_modality": "voice"})
    print(f"    {event['payload']['display_text']!r}")
    assert ran and SECRET in event["payload"]["display_text"], (ran, event)

    history = json.loads(await voice.call("GET", "/chat/history?limit=1"))
    assert history["result"]["chat_events"][0]["event_name"] == "message.assistant"

    stop.set()
    await asyncio.wait_for(pi_task, 10)
    await voice.ws.close()
    print("ALL PASSED")


if __name__ == "__main__":
    asyncio.run(main())
