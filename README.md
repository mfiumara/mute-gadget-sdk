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

# Mute Gadgets

Mute gadgets are open source devices you build yourself, talking to **your
own backend** and **any model provider you choose**. Program an off-the-shelf
ESP32 board or set up a Raspberry Pi, point it at your Mute server, and talk
to it. The model can drive every connected gadget's commands as tools.

Mute is a fork of Meta's
[muse-gadget-sdk](https://github.com/facebookincubator/muse-gadget-sdk) with
Meta's cloud, app, accounts and branding taken out. Nothing here talks to
Meta.

| | |
|---|---|
| [**Server**](server) | The backend gadgets connect to. One Python file; works with OpenAI, OpenRouter, Groq, Ollama, LM Studio, vLLM, or anything OpenAI-compatible. |
| [**ESP32 Device SDK**](esp32) | Firmware for ESP32 boards: status lights, screens, push-to-talk voice, images. |
| [**Linux Device SDK**](linux) | Turn a Raspberry Pi or any Linux box into a gadget the model can run commands on. |
| [**Skills**](skills) | Notes for the model on driving common home devices through a gadget. |

## Quick start

1. Run the [server](server/README.md) somewhere your gadgets can reach over
   HTTPS, with a long random `MUTE_DEVICE_TOKEN`.
2. Linux: `bash linux/install.sh --server https://your-host --token TOKEN`.
3. ESP32: set the server host, the token and your Wi-Fi in `idf.py menuconfig`
   (ESP32 Device SDK), then build and flash. See [`esp32/`](esp32).

Each directory has a `README.md` to get started and an `AGENTS.md` for coding
agents.

Built for hackers, just for fun. Side effects of tinkering may include bricked
boards, voided warranties, brownouts, or bankruptcies. Proceed at your own
risk!

## License

Mute Gadgets is licensed under the Apache License, Version 2.0, found in
[`LICENSE`](LICENSE). Files that came from the upstream project keep Meta's
copyright notice, as the license requires; see [`NOTICE`](NOTICE). These
third-party files keep their own licenses:

| Path | Upstream | License |
|---|---|---|
| [`esp32/components/minimp3/include/minimp3.h`](esp32/components/minimp3) | [lieff/minimp3](https://github.com/lieff/minimp3) | CC0-1.0, see [`LICENSE`](esp32/components/minimp3/LICENSE) |
| [`esp32/main/pixel_font.c`](esp32/main/pixel_font.c) | Adafruit GFX `glcdfont.c` | BSD-2-Clause, in the file header |

Dependencies fetched at build time are under their own licenses: ESP-IDF
components (into `esp32/managed_components/`), and the simulator's LVGL and
SDL (listed in [`esp32/simulator/THIRD_PARTY.md`](esp32/simulator/THIRD_PARTY.md)).
