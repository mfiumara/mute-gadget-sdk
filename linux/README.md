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

# Linux Device SDK

This turns any Linux computer, like a Raspberry Pi, into a Mute gadget.
Install it, point it at your [Mute server](../server), and the model can run
commands and move files on the machine.

Then make it your own: wire up a sensor, bridge a webhook, or give Mute a new
command.

> **Note:** Built by hackers, for hackers, just for fun. Proceed at your own
> risk! Mute gets the same access to the machine as the account you install it
> for.

## What you need

- **A Linux computer**, like a Raspberry Pi 3B+, 4, 5 or Zero 2 W, running
  Raspberry Pi OS Bullseye or later, Debian 11 or later, or Ubuntu 22.04 or
  later. 32-bit and 64-bit both work.
- **An account with sudo** on the machine.
- **A running [Mute server](../server)** and its `MUTE_DEVICE_TOKEN`.

## Install

On the machine, as the account Mute should use:

```sh
curl -fsSL https://raw.githubusercontent.com/mfiumara/mute-gadget-sdk/main/linux/install.sh -o install.sh
less install.sh     # read it first
bash install.sh --server https://mute.example.com --token YOUR_DEVICE_TOKEN
```

The installer checks your system, installs what it needs into
`/opt/mutegadget`, checks the token against your server, and starts the
`mutegadget` service. Before it gives Mute your account, it asks, and tells
you if that account can use sudo.

The device connects to your server and stays connected, across reboots. To
point it somewhere else later, run
`sudo mutegadget pair --force --server URL --token TOKEN`.

The token is checked over HTTPS, then the session is encrypted end to end with
Noise. Use `https://`; `http://` works on a network you trust.

## What Mute can do

| Command | What it does |
|---|---|
| `system.run` | Runs a shell command and returns its output and exit code |
| `file.read` | Reads a file, 64 KB at a time |
| `file.write` | Writes a file, 64 KB at a time, replacing it only when complete |
| `device.health` | Reports uptime, load, memory, disk and temperature |

Commands run as the account you installed for, with exactly that account's
permissions. If it can use sudo, so can Mute.

Ask Mute things like:

> What's using all the disk space on my Pi?

> Install Home Assistant on my Pi and tell me how to open it.

## Hack and extend it

Programs on the machine can send messages to Mute, with no credentials of
their own:

```sh
mutegadget send-user-msg "The garage door has been open for an hour."
mutegadget send-user-msg --session-id 6f1c2d4e-0b7a-4c3e-9f5d-2a8b1e0c7d93 "Posted to a side chat"
```

`--session-id` posts into a side chat: a new id starts one, and reusing it
keeps later messages there. [`examples/pebble_ring_bridge.py`](examples/pebble_ring_bridge.py)
is a complete example: a webhook listener that sends every note from a Pebble
ring to its own Mute chat.

A few other ways to build on it:

- **Let Mute do it.** Mute can run commands on the machine, so you can ask it to
  set up the rest: "Write a service that tells me when the Pi gets too hot."
- **Add a command.** Commands live in [`src/mutegadget/executor.py`](src/mutegadget/executor.py):
  add a spec to `COMMAND_SPECS` and a branch in `Executor.run`.
  [`AGENTS.md`](AGENTS.md) walks through it.
- **Change the account.** `bash install.sh --run-as someone` gives Mute a
  different account, such as one without sudo.

## Manage it

```sh
mutegadget info                          # name, node id and pairing state
sudo systemctl status mutegadget         # is it running?
sudo journalctl -u mutegadget -f         # follow the log
sudo mutegadget pair --force --server URL --token TOKEN
bash install.sh --uninstall              # remove it (add --purge to forget the pairing)
```

## Develop

From this directory, with [uv](https://docs.astral.sh/uv/):

```sh
uv run --with pytest --with . pytest
```

The tests run anywhere, with no server needed. To try a change on a
device, copy this directory to it and run `bash install.sh --from .`.

## License

Apache 2.0. See [`LICENSE`](../LICENSE).
