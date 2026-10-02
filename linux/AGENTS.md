<!--
Copyright (c) Meta Platforms, Inc. and affiliates.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
-->

# AGENTS.md

How to work on this Linux client, test it, and deploy it to a device. See
`README.md` for a shorter human overview.

## What this is

A Python package (`mutegadget`) that makes any Linux computer into a Mute
gadget. It holds an encrypted Noise session to the user's Mute server
(`../server`) and runs the commands the model sends it. It is the Linux
counterpart of the ESP32 firmware in `../esp32` and speaks the same control
protocol. The server imports its `noise` package and `link_client` framing.

| Module | Role |
|---|---|
| `cli.py` | `mutegadget pair`, `run`, `send-user-msg`, `info`, `unpair` |
| `identity.py` | Persistent identity: `homelink-xxxxxx` node id |
| `mute_api.py` | `fetch_vms`: checks the token and gets the VM bearer |
| `link_client.py` | One session: `/v1/noise` upgrade, Noise XX, `/link-control`, `/chat/stream` |
| `service.py` | `mutegadget run`: reconnect loop, local socket |
| `executor.py`, `fileops.py` | The commands Mute can run, as the chosen account |
| `noise/` | Noise XX handshake, framing and service envelopes |
| `data/` | The systemd unit and the hash-pinned `requirements.lock`, shipped in the package |

## Prerequisites

- Python 3.9 or later. The package supports Debian 11 / Raspberry Pi OS
  Bullseye (Python 3.9, cryptography 3.3.2) and later, and Ubuntu 22.04 and
  later.
- [uv](https://docs.astral.sh/uv/) for tests and builds.
- On a real device, `cryptography` comes from apt (`python3-cryptography`), so
  the venv uses the system interpreter with `--system-site-packages`. Don't
  point it at a uv-managed Python.

## Tests

The tests need no device or network:

```sh
uv run --with pytest --with . pytest
```

To check the oldest supported versions, pin them. cryptography 3.3.2 has no
Apple Silicon wheel, so run it under an x86-64 Python on a Mac:

```sh
uv run --no-project --isolated --python cpython-3.9-macos-x86_64 \
  --with pytest --with cryptography==3.3.2 --with websockets==13.1 \
  env PYTHONPATH=src python -m pytest -q
```

`tests/test_link_client.py` runs the client against an in-process fake VM
that does a real Noise handshake, and `../server/test_mute_server.py` runs it
against the real server.

## Deploy to a device

Copy this directory to the device and install from it:

```sh
bash install.sh --from .            # asks before granting access
bash install.sh --from . --yes      # doesn't ask
```

Reinstalling keeps the pairing. It restarts the service, which kills any
command Mute is running at that moment, so check the log for recent `invoke`
lines first.

For a quicker loop, build a wheel and install it into the existing venv:

```sh
uv build --wheel
# on the device:
sudo /opt/mutegadget/venv/bin/pip install --no-deps --force-reinstall mutegadget-*.whl
sudo systemctl restart mutegadget
```

Installer flags: `--server URL`, `--token TOKEN`, `--run-as USER`, `--no-pair`, `--yes`, `--from SOURCE`,
`--uninstall`, `--purge`. The installer is ShellCheck-clean; keep it that way.

## Run and debug

```sh
sudo journalctl -u mutegadget -f     # service log
sudo mutegadget -v pair --force --server URL --token TOKEN
mutegadget info                      # identity and pairing state
```

State lives in `/var/lib/mutegadget` (mode 0700): `identity.json` survives
unpairing, `pairing.json` holds the server URL and token. The local socket is
`/run/mutegadget/mutegadget.sock`, owned by root and the run-as account's group.

A healthy start logs `commands run as <user>`, `Noise session established`,
`sent link.register` and `registered with the server`.

## Talking to the server

- Connect to `wss://<server host>/v1/noise?vm_id=<vm_id>` (`ws://` for an
  `http://` server) with the per-VM bearer
  from `fetch_vms` in an `Authorization` header. A 401 or 403 on the upgrade
  means fetch fresh VM credentials, not retry the same bearer.
- After the Noise handshake, open `POST /link-control` and leave the body open.
  Both directions carry JSON messages, each prefixed with a little-endian u32
  length. The device sends `link.register`, then a `link.result` for each
  `link.invoke`. `link.unpaired` means the server removed the device.
- Register as `platform: "linux"`, `device_family: "homehub"`.
- Messages from the device to the Mute (`mutegadget send-user-msg`) go as separate
  `POST /chat/stream` requests on the same session, with `device_id` set to the
  node id. `session_id` picks the chat; `chat_id` is not an API field and is
  ignored.
- Command output is cut at 96 KB per stream to keep messages small.

## Adding a command

1. Add a spec to `COMMAND_SPECS` in `executor.py`: `description`, `required`
   and `optional` parameters (each with `type` and `description`), and
   `timeout_ms` if the default 30 seconds is too short.
2. Handle it in `Executor.run`. Return `ok(payload)` or `error(message)`.
3. Anything that touches the machine should run in a child process with
   `self._child_options()`, so it runs as the chosen account and not as root.
4. Add a test in `tests/test_executor.py`.

Mute sees the new command after the service restarts and re-registers.

## Protocol names

`hatch` survives only in wire identifiers the ESP32 firmware shares with this
client, such as the `hatch_refresh:` auth prefix. Don't use it in new names.

## Before you hand back work

1. The tests pass, on the newest Python and on Python 3.9 with cryptography
   3.3.2 if you touched the Noise code, and the server tests pass.
2. If you changed `install.sh`, it passes ShellCheck.
3. If you deployed, the log shows `registered with the server` and no tracebacks.
