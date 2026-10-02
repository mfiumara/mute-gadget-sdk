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

"""Mute server: the backend Mute gadgets connect to.

Speaks the gadget protocol (a device API, then Noise XX over a WebSocket with
HTTP-like streams inside) and answers with any OpenAI-compatible model. Every
connected gadget registers its commands; the model gets them all as tools, so
you can talk to one gadget and have it drive another.

Run it with the gadget package next to it:

    MUTE_DEVICE_TOKEN=... MUTE_LLM_API_KEY=... \\
        uv run --with ../linux server/mute_server.py

See server/README.md for the settings.
"""

from __future__ import annotations

import asyncio
import base64
import hmac
import http
import json
import logging
import os
import urllib.parse
import urllib.request
import uuid

from websockets.asyncio.server import serve
from websockets.datastructures import Headers
from websockets.http11 import Response

from mutegadget.link_client import MessageDecoder, encode_message
from mutegadget.noise import (
    ApplicationResponse, BodyChunk, Header, NoiseFrameDecoder, NoiseXXResponder, ServiceFrame,
    encode_noise_frames,
)
from mutegadget.noise.transport import decode_request_envelope, encode_response_envelope

log = logging.getLogger("mute")

TOKEN = os.environ.get("MUTE_DEVICE_TOKEN", "")
LLM_URL = os.environ.get("MUTE_LLM_BASE_URL", "https://api.openai.com/v1").rstrip("/")
LLM_KEY = os.environ.get("MUTE_LLM_API_KEY", "")
LLM_MODEL = os.environ.get("MUTE_LLM_MODEL", "gpt-4o-mini")
STT_MODEL = os.environ.get("MUTE_STT_MODEL", "whisper-1")
TTS_MODEL = os.environ.get("MUTE_TTS_MODEL", "tts-1")
TTS_VOICE = os.environ.get("MUTE_TTS_VOICE", "alloy")
SYSTEM_PROMPT = os.environ.get("MUTE_SYSTEM_PROMPT", (
    "You are Mute, a helpful assistant that lives in the user's home gadgets. "
    "Answer briefly: replies are often spoken aloud or shown on a tiny screen. "
    "Use the device tools when the user asks you to do something on a device."
))
AGENT_NAME = os.environ.get("MUTE_AGENT_NAME", "Mute")
VM_ID = "mute"
INVOKE_TIMEOUT_S = 120
# The ESP32 reassembles at most ~10 KB per message on its control session.
CHUNK = 8 * 1024
MAX_TOOL_ROUNDS = 8
HISTORY_TURNS = 20

# node_id -> Device; every connected gadget, for cross-device tools.
DEVICES: dict[str, "Device"] = {}
# session key -> chat messages. ponytail: in memory, lost on restart.
HISTORY: dict[str, list] = {}
# Chat events for voice gadgets: /chat/subscribe pushes them, /chat/history
# serves them, and /api/voice/tts-stream speaks the assistant ones.
EVENTS: list[dict] = []
SUBSCRIBERS: set[tuple["Device", int]] = set()


# -- Model provider (OpenAI-compatible, stdlib only) ---------------------------

def _post(path: str, body: bytes, content_type: str) -> bytes:
    req = urllib.request.Request(LLM_URL + path, data=body, method="POST")
    req.add_header("Content-Type", content_type)
    if LLM_KEY:
        req.add_header("Authorization", f"Bearer {LLM_KEY}")
    with urllib.request.urlopen(req, timeout=120) as resp:
        return resp.read()


def complete(messages: list, tools: list) -> dict:
    body = {"model": LLM_MODEL, "messages": messages}
    if tools:
        body["tools"] = tools
    data = json.loads(_post("/chat/completions", json.dumps(body).encode(), "application/json"))
    return data["choices"][0]["message"]


def transcribe(audio: bytes, filename: str) -> str:
    boundary = uuid.uuid4().hex
    parts = [
        f'--{boundary}\r\nContent-Disposition: form-data; name="model"\r\n\r\n{STT_MODEL}\r\n'.encode(),
        f'--{boundary}\r\nContent-Disposition: form-data; name="file"; filename="{filename}"\r\n'
        "Content-Type: application/octet-stream\r\n\r\n".encode() + audio + b"\r\n",
        f"--{boundary}--\r\n".encode(),
    ]
    data = _post("/audio/transcriptions", b"".join(parts), f"multipart/form-data; boundary={boundary}")
    return json.loads(data).get("text", "").strip()


def speak(text: str) -> bytes:
    body = {"model": TTS_MODEL, "voice": TTS_VOICE, "input": text, "response_format": "mp3"}
    return _post("/audio/speech", json.dumps(body).encode(), "application/json")


# -- Tools: every registered command on every connected gadget ----------------

def _tool_name(node_id: str, command: str) -> str:
    return f"{node_id}__{command}".replace(".", "_")[:64]


def tools() -> tuple[list, dict]:
    specs, routes = [], {}
    for device in DEVICES.values():
        for command, spec in device.commands.items():
            spec = spec if isinstance(spec, dict) else {}
            props = {}
            for group in ("required", "optional"):
                for name, param in (spec.get(group) or {}).items():
                    param = param if isinstance(param, dict) else {}
                    props[name] = {"type": param.get("type") if param.get("type") in (
                        "string", "integer", "number", "boolean", "object", "array") else "string",
                        "description": param.get("description", "")}
            name = _tool_name(device.node_id, command)
            routes[name] = (device, command)
            specs.append({"type": "function", "function": {
                "name": name,
                "description": f"[{device.display_name}] {spec.get('description', command)}",
                "parameters": {"type": "object", "properties": props,
                               "required": list(spec.get("required") or {})},
            }})
    return specs, routes


async def chat(key: str, text: str, origin: str) -> str:
    """One user turn: the model, plus as many tool rounds as it needs."""
    history = HISTORY.setdefault(key, [])
    history.append({"role": "user", "content": text})
    system = SYSTEM_PROMPT + f"\nThe user is talking to you through the gadget {origin!r}."
    reply = ""
    for _ in range(MAX_TOOL_ROUNDS):
        specs, routes = tools()
        message = await asyncio.to_thread(complete, [{"role": "system", "content": system}] + history, specs)
        history.append({k: v for k, v in message.items() if k in ("role", "content", "tool_calls")})
        reply = message.get("content") or ""
        if not message.get("tool_calls"):
            break
        for call in message["tool_calls"]:
            name = call["function"]["name"]
            try:
                args = json.loads(call["function"].get("arguments") or "{}")
            except json.JSONDecodeError:
                args = {}
            if name in routes:
                device, command = routes[name]
                result = await device.invoke(command, args)
            else:
                result = {"ok": False, "error": f"no such tool: {name}"}
            history.append({"role": "tool", "tool_call_id": call["id"],
                            "content": json.dumps(result)[:32000]})
    # Keep whole turns: never start the history on a tool result.
    while len(history) > HISTORY_TURNS * 4 or (history and history[0]["role"] != "user"):
        history.pop(0)
    return reply


def fix_wav(audio: bytes) -> bytes:
    """Fill in the RIFF and data sizes the ESP32 leaves as 0xFFFFFFFF while streaming."""
    if len(audio) >= 44 and audio[:4] == b"RIFF" and audio[36:40] == b"data":
        n = len(audio)
        audio = audio[:4] + (n - 8).to_bytes(4, "little") + audio[8:40] + (n - 44).to_bytes(4, "little") + audio[44:]
    return audio


async def publish(name: str, message_id: str, reply_to: str | None, text: str) -> None:
    seq = EVENTS[-1]["seq"] + 1 if EVENTS else 1
    EVENTS.append({"seq": seq, "event_name": name, "message_id": message_id,
                   "reply_to_message_id": reply_to, "display_text": text, "display_text_ready": True})
    del EVENTS[:-100]  # ponytail: the last 100 events are plenty for polling and TTS
    line = json.dumps({"type": "event", "seq": seq, "event": name, "message_id": message_id, "payload": {
        "message_id": message_id, "reply_to_message_id": reply_to, "display_text": text, "content": text,
    }}).encode() + b"\n"
    for device, sid in list(SUBSCRIBERS):
        try:
            await device.send_frame(ServiceFrame.body_chunk(sid, BodyChunk(data=line)))
        except Exception:
            SUBSCRIBERS.discard((device, sid))


# -- One gadget connection ----------------------------------------------------

class Device:
    def __init__(self, ws) -> None:
        self.ws = ws
        self.node_id = ""
        self.display_name = ""
        self.commands: dict = {}
        self.control = None
        self.tunnel = None
        self.pending: dict[str, asyncio.Future] = {}
        self.streams: dict[int, dict] = {}
        self.lock = asyncio.Lock()

    async def handshake(self) -> None:
        responder = NoiseXXResponder()
        responder.initialize()
        await self.ws.send(responder.read_message1_and_write_message2(await self.ws.recv()))
        responder.read_message3(await self.ws.recv())
        self.send_cipher, self.recv_cipher = responder.split()

    async def send_frame(self, frame: ServiceFrame) -> None:
        async with self.lock:
            for chunk in encode_noise_frames(encode_response_envelope(frame)):
                await self.ws.send(self.send_cipher.encrypt_with_ad(b"", chunk))

    async def respond(self, stream_id: int, status: int, body: bytes = b"",
                      content_type: str = "application/json", end: bool = True) -> None:
        await self.send_frame(ServiceFrame.response(stream_id, ApplicationResponse(
            status=status, headers=[Header("Content-Type", content_type)], body=body, end_body=end)))

    async def respond_stream(self, stream_id: int, body: bytes, content_type: str) -> None:
        """A 200 whose body goes out in small BodyChunks."""
        await self.respond(stream_id, 200, content_type=content_type, end=not body)
        for i in range(0, len(body), CHUNK):
            await self.send_frame(ServiceFrame.body_chunk(stream_id, BodyChunk(
                data=body[i:i + CHUNK], end_body=i + CHUNK >= len(body))))

    async def send(self, message: dict) -> None:
        await self.send_frame(ServiceFrame.body_chunk(self.control, BodyChunk(data=encode_message(message))))

    async def invoke(self, command: str, params: dict) -> dict:
        invoke_id = str(uuid.uuid4())
        future = asyncio.get_running_loop().create_future()
        self.pending[invoke_id] = future
        log.info("%s: invoke %s", self.node_id, command)
        try:
            await self.send({"type": "req", "method": "link.invoke", "id": invoke_id,
                             "command": command, "params": params,
                             "timeout_ms": INVOKE_TIMEOUT_S * 1000})
            return await asyncio.wait_for(future, INVOKE_TIMEOUT_S + 5)
        except (asyncio.TimeoutError, ConnectionError) as exc:
            return {"ok": False, "error": f"{type(exc).__name__}"}
        finally:
            self.pending.pop(invoke_id, None)

    async def run(self) -> None:
        await self.handshake()
        decoder = NoiseFrameDecoder()
        messages = MessageDecoder()
        async for raw in self.ws:
            assembled = decoder.decode(self.recv_cipher.decrypt_with_ad(b"", bytes(raw)))
            if assembled is None:
                continue
            frame = decode_request_envelope(assembled)
            sid = frame.stream_id
            if frame.kind == "request":
                req = frame.value
                path = req.path.split("?")[0]
                if path == "/link-control":
                    self.control = sid
                    await self.respond(sid, 200, end=False)
                    data = req.body
                elif path == "/link-tunnel":
                    # ponytail: keepalive only, no packet routing to the home network.
                    self.tunnel = sid
                    await self.respond(sid, 200, content_type="application/octet-stream", end=False)
                    continue
                elif path == "/chat/subscribe":
                    SUBSCRIBERS.add((self, sid))
                    await self.respond(sid, 200, content_type="application/x-ndjson", end=False)
                    continue
                else:
                    self.streams[sid] = {"req": req, "body": bytearray(req.body)}
                    if req.end_body:
                        self._spawn(sid, self.streams.pop(sid))
                    continue
            elif frame.kind == "body_chunk":
                if sid == self.tunnel:
                    if frame.value.data == b"\x00":
                        await self.send_frame(ServiceFrame.body_chunk(sid, BodyChunk(data=b"\x00")))
                    continue
                if sid != self.control:
                    stream = self.streams.get(sid)
                    if stream is not None:
                        stream["body"] += frame.value.data
                        if frame.value.end_body:
                            self._spawn(sid, self.streams.pop(sid))
                    continue
                data = frame.value.data
            else:
                self.streams.pop(sid, None)
                continue
            for message in messages.feed(data):
                await self._control_message(message)

    def _spawn(self, sid: int, stream: dict) -> None:
        asyncio.ensure_future(self._request(sid, stream))

    async def _control_message(self, message: dict) -> None:
        if message.get("method") == "link.register":
            params = message.get("params") or {}
            self.node_id = params.get("node_id") or "gadget"
            self.display_name = params.get("display_name") or self.node_id
            self.commands = params.get("commands_v2") or {}
            DEVICES[self.node_id] = self
            log.info("%s (%s) registered %d commands", self.node_id, self.display_name, len(self.commands))
            await self.send({"type": "res", "id": message.get("id"), "result": {"status": "registered"}})
        elif message.get("method") == "link.result":
            future = self.pending.get(message.get("id"))
            if future and not future.done():
                future.set_result({k: v for k, v in message.items() if k not in ("method", "id")})

    async def _request(self, sid: int, stream: dict) -> None:
        req = stream["req"]
        path = req.path.split("?")[0]
        try:
            if path == "/chat/stream":
                await self._chat(sid, bytes(stream["body"]))
            elif path == "/identity":
                await self.respond(sid, 200, json.dumps({"ok": True, "result": {"name": AGENT_NAME}}).encode())
            elif path == "/chat/history":
                await self._history(sid, req.path)
            elif path in ("/api/voice/tts-stream", "/voice/tts-stream"):
                await self._tts(sid, req.path)
            else:
                await self.respond(sid, 404, b'{"error":"not found"}')
        except Exception as exc:
            log.exception("%s %s failed", req.verb, path)
            await self.respond(sid, 502, json.dumps({"error": str(exc)}).encode())

    async def _chat(self, sid: int, body: bytes) -> None:
        request = json.loads(body or b"{}")
        text = request.get("message") or ""
        key = request.get("session_id") or "main"
        origin = self.display_name or "a voice gadget"
        if "output_modality" not in request:
            # Linux gadgets and scripts: answer on the request itself.
            reply = await chat(key, text, origin)
            await self.respond(sid, 200, json.dumps({"text": reply}).encode())
            return
        # ESP32 voice/text turns: ack now, deliver the reply as chat events.
        user_id = str(uuid.uuid4())
        await self.respond(sid, 200, json.dumps({"ok": True, "result": {"message_id": user_id}}).encode())
        try:
            for item in request.get("items") or []:
                if item.get("type") == "file" and str(item.get("mime_type", "")).startswith("audio/"):
                    audio = fix_wav(base64.b64decode(item.get("data_base64") or ""))
                    heard = await asyncio.to_thread(transcribe, audio, item.get("filename") or "note.wav")
                    text = f"{text} {heard}".strip()
            await publish("message.user", user_id, None, text or "[Voice note]")
            reply = await chat(key, text, origin) if text else "Sorry, I didn't catch that."
        except Exception as exc:
            log.exception("voice turn failed")
            reply = f"Something went wrong: {exc}"
        await publish("message.assistant", str(uuid.uuid4()), user_id, reply or "Done.")

    async def _history(self, sid: int, path: str) -> None:
        query = urllib.parse.parse_qs(urllib.parse.urlsplit(path).query)
        after = int(query.get("after_seq", ["0"])[0] or 0)
        limit = int(query.get("limit", ["20"])[0] or 20)
        rows = [e for e in EVENTS if e["seq"] > after] if "after_seq" in query else EVENTS[-limit:]
        body = json.dumps({"ok": True, "result": {"chat_events": rows[:limit]}}).encode()
        await self.respond_stream(sid, body, "application/json")

    async def _tts(self, sid: int, path: str) -> None:
        message_id = urllib.parse.parse_qs(urllib.parse.urlsplit(path).query).get("message_id", [""])[0]
        event = next((e for e in EVENTS if e["message_id"] == message_id), None)
        if event is None:
            await self.respond(sid, 410, b'{"error":"unknown message"}')
            return
        audio = await asyncio.to_thread(speak, event["display_text"])
        await self.respond_stream(sid, audio[:512 * 1024], "audio/mpeg")


# -- HTTP device API and WebSocket entry ---------------------------------------

def _authorized(headers) -> bool:
    auth = headers.get("Authorization", "")
    token = auth.split(" ", 1)[1] if auth.startswith("Bearer ") else ""
    token = token.removeprefix("hatch_refresh:")
    return bool(TOKEN) and hmac.compare_digest(token, TOKEN)


def _json(status: int, obj: dict) -> Response:
    body = json.dumps(obj).encode()
    return Response(status, http.HTTPStatus(status).phrase,
                    Headers({"Content-Type": "application/json", "Content-Length": str(len(body))}), body)


def process_request(connection, request):
    path = request.path.split("?")[0]
    if not _authorized(request.headers):
        return _json(401, {"error": "unauthorized"})
    if path == "/v1/noise":
        return None  # go on with the WebSocket upgrade
    if path == "/fetch_vms":
        host = request.headers.get("Host", "")
        return _json(200, {"vm_list": [{
            "vm_id": VM_ID, "vm_name": VM_ID, "default": True, "vm_auth_token": TOKEN,
            "vm_ws_url": f"wss://{host}/v1/noise",
        }]})
    if path in ("/device_token/refresh", "/device_token/mint"):
        # One shared token that never expires: hand it straight back.
        return _json(200, {"access_token": TOKEN, "refresh_token": TOKEN})
    return _json(404, {"error": "not found"})


async def handler(ws) -> None:
    device = Device(ws)
    try:
        await device.run()
    except Exception as exc:
        log.info("connection ended: %s", exc)
    finally:
        SUBSCRIBERS.difference_update({s for s in SUBSCRIBERS if s[0] is device})
        if DEVICES.get(device.node_id) is device:
            del DEVICES[device.node_id]
        for future in device.pending.values():
            if not future.done():
                future.set_exception(ConnectionError("gadget disconnected"))


async def main() -> None:
    if not TOKEN:
        raise SystemExit("set MUTE_DEVICE_TOKEN (any long random string; gadgets use it to connect)")
    host = os.environ.get("MUTE_HOST", "0.0.0.0")
    port = int(os.environ.get("MUTE_PORT", "8080"))
    async with serve(handler, host, port, process_request=process_request, max_size=None) as server:
        log.info("Mute server on http://%s:%d, model %s at %s", host, port, LLM_MODEL, LLM_URL)
        await server.serve_forever()


if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
    asyncio.run(main())
