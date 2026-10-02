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

"""The real Linux gadget client against the real server, with a fake model.

    uv run --with pytest --with aiohttp --with ../linux pytest test_mute_server.py
"""

import asyncio
import os

os.environ["MUTE_DEVICE_TOKEN"] = "test-token"

import contextlib  # noqa: E402
import json  # noqa: E402
import urllib.request  # noqa: E402

import mute_server  # noqa: E402
from aiohttp.test_utils import TestServer  # noqa: E402

from mutegadget import mute_api  # noqa: E402
from mutegadget.link_client import DeviceDescription, LinkSession  # noqa: E402

COMMANDS = {"system.run": {"description": "run a shell command",
                           "required": {"command": {"type": "string", "description": "cmd"}}}}


@contextlib.asynccontextmanager
async def running_server():
    server = TestServer(mute_server.make_app(), host="127.0.0.1")
    await server.start_server()
    try:
        yield str(server.make_url("")).rstrip("/")
    finally:
        await server.close()


def fake_complete(messages, tools):
    """Call the device's tool once, then answer with its output."""
    if messages[-1]["role"] == "tool":
        return {"role": "assistant", "content": "It said " + messages[-1]["content"]}
    assert [t["function"]["name"] for t in tools] == ["homelink-abcdef__system_run"]
    return {"role": "assistant", "content": None, "tool_calls": [{
        "id": "call-1", "type": "function",
        "function": {"name": "homelink-abcdef__system_run", "arguments": '{"command": "uptime"}'},
    }]}


def test_chat_drives_a_device_tool(monkeypatch):
    monkeypatch.setattr(mute_server, "complete", fake_complete)

    async def scenario():
        async with running_server() as url:

            assert (await asyncio.to_thread(mute_api.fetch_vms_with_status, "wrong", url))[1] == 401
            vms, _ = await asyncio.to_thread(mute_api.fetch_vms_with_status, "test-token", url)
            assert vms[0]["vm_id"] == "mute" and vms[0]["is_default"]

            ran = []

            def run_command(command, params, timeout_ms):
                ran.append((command, params))
                return {"ok": True, "payload": {"stdout": "up 3 days"}}

            session = LinkSession(
                server=url, vm_id="mute", vm_auth_token=vms[0]["vm_auth_token"],
                device=DeviceDescription("homelink-abcdef", "pi", "0.1.0", COMMANDS),
                run_command=run_command,
            )
            stop = asyncio.Event()
            task = asyncio.ensure_future(session.run(stop))
            while session.registered_at is None:
                await asyncio.sleep(0.01)

            reply = await session.send_chat("how long has the pi been up?")
            assert reply["ok"], reply
            assert "up 3 days" in reply["response"]["text"]
            assert ran == [("system.run", {"command": "uptime"})]

            stop.set()
            await asyncio.wait_for(task, 5)

    asyncio.run(scenario())


def test_voice_note_turn_like_the_esp32(monkeypatch):
    """Subscribe, send a voice note, get the reply event, then its speech."""
    import base64

    from websockets.asyncio.client import connect

    from mutegadget.noise import Header, NoiseTransport, NoiseXXInitiator

    heard = []
    monkeypatch.setattr(mute_server, "complete",
                        lambda messages, tools: {"role": "assistant", "content": "Hello there"})
    monkeypatch.setattr(mute_server, "transcribe",
                        lambda audio, name: heard.append(audio) or "hi mute")
    monkeypatch.setattr(mute_server, "speak", lambda text: b"ID3" + b"\xff" * 20000)
    wav = b"RIFF\xff\xff\xff\xffWAVEfmt " + bytes(20) + b"data\xff\xff\xff\xff" + b"\x01\x00" * 100

    async def scenario():
        async with running_server() as url:
            ws = await connect(url.replace("http", "ws", 1) + "/v1/noise?vm_id=mute",
                               additional_headers={"Authorization": "Bearer test-token"})
            hs = NoiseXXInitiator()
            hs.initialize()
            await ws.send(hs.write_message1())
            hs.read_message2(await ws.recv())
            await ws.send(hs.write_message3())
            t = NoiseTransport(*hs.split())

            async def send(frames):
                for f in frames.frames:
                    await ws.send(f)
                return frames.stream_id

            async def collect(stream_id):
                body = b""
                while True:
                    frame = t.decrypt_frame(await ws.recv())
                    if frame is None or frame.stream_id != stream_id:
                        continue
                    data, end = (frame.value.body, frame.value.end_body) if frame.kind == "response" \
                        else (frame.value.data, frame.value.end_body)
                    body += data
                    if end or (b"\n" in body and stream_id == sub):
                        return body

            json_headers = [Header("Content-Type", "application/json")]
            sub = await send(t.encrypt_http_request("POST", "/chat/subscribe", b"{}", headers=json_headers))
            note = {"message": "", "output_modality": "voice", "items": [{
                "type": "file", "mime_type": "audio/wav", "filename": "voice_note.wav",
                "data_base64": base64.b64encode(wav).decode()}]}
            turn = await send(t.encrypt_http_request("POST", "/chat/stream", json.dumps(note).encode(),
                                                     headers=json_headers))
            ack = json.loads(await collect(turn))
            user = json.loads(await collect(sub))
            assert user["event"] == "message.user" and user["payload"]["display_text"] == "hi mute"
            event = json.loads(await collect(sub))
            assert event["event"] == "message.assistant"
            assert event["payload"]["reply_to_message_id"] == ack["result"]["message_id"]
            assert event["payload"]["display_text"] == "Hello there"
            assert int.from_bytes(heard[0][40:44], "little") == 200  # data size filled in

            tts = await send(t.encrypt_http_request(
                "GET", f"/api/voice/tts-stream?message_id={event['message_id']}", b""))
            assert (await collect(tts)).startswith(b"ID3")

            hist = await send(t.encrypt_http_request("GET", "/chat/history?limit=1", b""))
            rows = json.loads(await collect(hist))["result"]["chat_events"]
            assert rows[0]["event_name"] == "message.assistant"
            await ws.close()

    asyncio.run(scenario())


def test_token_endpoints_accept_post_like_the_esp32():
    """The firmware POSTs refresh and mint with a JSON body."""
    async def scenario():
        async with running_server() as url:
            def post(path, auth):
                req = urllib.request.Request(url + path, data=b'{"device_id":"homelink-abcdef"}',
                                             method="POST")
                req.add_header("Authorization", auth)
                req.add_header("Content-Type", "application/json")
                req.add_header("X-API-Version", "1.0.0")
                try:
                    with urllib.request.urlopen(req, timeout=5) as resp:
                        return resp.status, json.loads(resp.read())
                except urllib.error.HTTPError as exc:
                    return exc.code, None

            for path in ("/device_token/refresh", "/device_token/mint"):
                assert await asyncio.to_thread(post, path, "Bearer hatch_refresh:test-token") == (
                    200, {"access_token": "test-token", "refresh_token": "test-token"})
                assert (await asyncio.to_thread(post, path, "Bearer hatch_refresh:nope"))[0] == 401

    asyncio.run(scenario())
