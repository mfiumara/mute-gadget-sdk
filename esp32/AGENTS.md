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

How to build this firmware and flash it onto your own ESP32 board. See
`README.md` for a shorter human overview.

## What this is

The ESP32 Device SDK: the ESP-IDF firmware (`mute-gadget`). It joins Wi-Fi
and holds an encrypted Noise session to the user's Mute server (`../server`), with an optional home-network tunnel. `main/main.c` is the entry
point and `main/app.c` holds most of the logic. `components/mute` adds a
avatar/voice/settings UI on boards with a display.

## Prerequisites

- **ESP-IDF v6.0.1**, installed and activated (`. $IDF_PATH/export.sh`) so
  `idf.py` is on `PATH`. Other versions are unsupported. Errors about missing
  IDF headers such as `gcm.h` or `gpio_ll.h` usually mean the wrong IDF tag.
- Python 3 and a C/C++ compiler (`cc`, `c++`) for the host tests.
- A USB data cable. Flashing needs access to the serial port. A sandboxed
  agent usually has to run flash and monitor commands outside the sandbox.

## Supported boards

Board overlays live in `devices/`, which has a `README.md` listing each board's
hardware, features, and where to buy it, and an `AGENTS.md` recipe for adding
one. Its Vendor sources table lists each vendor's code for its boards: read it
before adding a feature to one.

| Board | Target | Overlay(s) after `sdkconfig.defaults` | Helper |
|---|---|---|---|
| ESP32-C5 DevKitC-1 (default) | `esp32c5` | none | `tools/board.sh devkit` |
| ideaspark ESP32 + 1.9" ST7789 | `esp32` | `devices/sdkconfig.ideaspark` | `tools/board.sh ideaspark` |
| Seeed SenseCAP Indicator | `esp32s3` | `devices/sdkconfig.sensecap-indicator` | `tools/board.sh sensecap-indicator` |
| Seeed reTerminal E1001 | `esp32s3` | `devices/sdkconfig.reterminal-e1001` | `tools/board.sh reterminal-e1001` |
| Home Assistant Voice Preview Edition | `esp32s3` | `devices/sdkconfig.home-assistant-voice` | `tools/board.sh home-assistant-voice` |
| Waveshare ESP32-S3-Touch-AMOLED-1.75C | `esp32s3` | `devices/sdkconfig.mute;devices/sdkconfig.mute-waveshare-s3-175c` | manual (below) |
| AIPI Lite | `esp32s3` | `devices/sdkconfig.mute;devices/sdkconfig.mute-aipi` | manual |
| Waveshare ESP32-C6-Touch-AMOLED-1.8 | `esp32c6` | `devices/sdkconfig.mute;devices/sdkconfig.mute-waveshare-c6-18` | manual |
| Seeed SenseCAP Watcher | `esp32s3` | `devices/sdkconfig.mute;devices/sdkconfig.mute-sensecap-watcher` | manual |
| M5Stack StickS3 | `esp32s3` | `devices/sdkconfig.mute;devices/sdkconfig.mute-m5stack-sticks3` | manual |
| M5Stack StickC Plus2 | `esp32` | `devices/sdkconfig.mute;devices/sdkconfig.mute-m5stack-stickc-plus2` | manual |

The default profile expects the C5 DevKitC-1: an addressable status LED on
GPIO27, the BOOT button on GPIO28 (active low), 8 MB flash and quad PSRAM.
The DevKitC-1's LED takes red first (`CONFIG_HOMEHUB_LED_RGB_ORDER`, on by
default). If yours shows green as red and amber as green, it takes green first
like most WS2812s: turn the option off in `idf.py menuconfig`, or in
`build-devkit/sdkconfig`, and rebuild.

## Build

Every build needs the user's server host and device token. Ask for them,
then set `CONFIG_MUTE_SERVER_HOST="host"` (no `https://`) and
`CONFIG_MUTE_DEVICE_TOKEN="…"` in that build directory's `sdkconfig` (or with
`idf.py menuconfig`) before building, plus `CONFIG_HOMEHUB_WIFI_SSID` and
`CONFIG_HOMEHUB_WIFI_PASSWORD`. Without a token the build warns and the device
can't connect. Never commit the token or print it in full.

The firmware connects over TLS on port 443 only, with the ESP-IDF public CA
bundle, so the server needs a publicly trusted certificate.

### DevKitC-1 (default)

```sh
idf.py build                    # -> build/mute-gadget.bin, config in build/sdkconfig
```

### Other boards, with the helper

`tools/board.sh BOARD [build|flash|monitor|flash-monitor] [PORT]` sets the
target and overlays and gives each board its own `build-<board>/` directory
and `build-<board>/sdkconfig`:

```sh
tools/board.sh sensecap-indicator build
tools/board.sh ideaspark flash-monitor /dev/cu.usbserial-110
```

Without a port it uses the only matching serial port, and fails if it finds
none or more than one. If `idf.py` is not on `PATH`, it sources `export.sh`
from `$IDF_PATH`, `~/esp/esp-idf-v6.0.1`, `~/esp/esp-idf-v6` or `~/esp/esp-idf`.

### Home Assistant Voice Preview Edition

`tools/board.sh home-assistant-voice build` builds a status-and-voice gadget:
the LED ring shows the status colours, holding the centre button records a
voice note that Mute answers out loud, and the dial sets the speaker volume
(shown on the ring, kept across restarts). It advertises as
`MuteGadget-ha-voice-XXXXXX`.

- The console and flashing go through the S3's own USB-Serial-JTAG, which
  shows up as `/dev/cu.usbmodem*` like a DevKitC-1. With both plugged in, pass
  the port, and check it with `python -m esptool read-mac` before flashing.
- Flashing replaces the vendor firmware on the S3 only. The XMOS audio chip
  keeps its own firmware; never reflash it.
- While voice chat is ready, the centre button is push-to-talk. Turn the mute
  switch on to get its setup role back (pairing confirmation, the 5 s
  factory-reset hold).

### Boards with the full UI, by hand

`tools/mute/board.sh build|flash <s3|aipi|c6|watcher|sticks3|plus2> [SERIAL|PORT]`
builds one board in `build-mute-<profile>/`, logs to
`/tmp/mute_build_<board>.log`, and clears `managed_components/` before and
after so it doesn't clash with other boards. When flashing, it finds the
board's port with `tools/mute/ports.py`, by the USB device behind it rather
than the port name, which changes when boards are re-cabled. With several
boards of one kind attached, pass the device's USB serial number (on the chip's
own USB serial port, its MAC) or the port; `tools/mute/ports.py --list` shows
them. It finds ESP-IDF the way `tools/board.sh` does, trying
`~/.espressif/esp-idf-v6.0.1` first. For an install anywhere else, set
`IDF_EXPORT` to its `export.sh`, e.g. in your shell profile:
`export IDF_EXPORT=/path/to/esp-idf/export.sh`. `tools/mute/avatar.py` builds
through `board.sh`, so it needs the same.

For bench testing, `MUTE_BENCH=1 tools/mute/board.sh build|flash ...` adds
`devices/sdkconfig.mute-bench` and uses `build-mute-<profile>-bench/`. That
turns on screenshots: `tools/mute/snap.py PORT KEYS OUT.png` sends bench keys
and saves the screen, and `>face=thinking` (or `idle`, `listening`,
`speaking`, `error`, `boot`, `off`, `happy`) in KEYS picks the avatar mode first.
Screenshots are off in normal builds because each one takes a buffer the size
of the screen. `>face=` works in any build. Or run `idf.py` directly:

```sh
idf.py -B build-mute-aipi -DIDF_TARGET=esp32s3 \
  -DSDKCONFIG=build-mute-aipi/sdkconfig \
  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;devices/sdkconfig.mute;devices/sdkconfig.mute-aipi" build
```

To flash, add `-p PORT flash` with the same `-B`, target and defaults
arguments, except on the SenseCAP Watcher: `idf.py flash` runs plain esptool,
which fails on its USB bridge (see "Flash it"). Build the Watcher with
`idf.py`, flash it with `tools/mute/board.sh flash watcher`, then
`idf.py … -p PORT monitor` as usual; reading from the bridge works. Mute builds use `partitions_mute.csv` and need 16 MB of flash or
more, except the StickS3 and StickC Plus2, which have 8 MB and use
`partitions_mute_8mb.csv`.

All boards share `managed_components/` and `dependencies.lock` in this
directory. If the component manager fails after you switch between a board
with the full UI and one without (lvgl is the usual offender), delete both and build again.

### A new board

Follow `devices/AGENTS.md`, which covers boards with a status light, a status
screen or the full UI from the overlay to verification. In short: copy the
closest overlay to `devices/sdkconfig.<yourboard>`, load it last, and add the
board to `devices/README.md`. At minimum set `CONFIG_IDF_TARGET`,
`CONFIG_HOMEHUB_BUTTON_GPIO`, the LED backend (`CONFIG_HOMEHUB_LED_BACKEND_*`,
or `..._NONE`), and the flash size and mode. Without PSRAM, also set
`CONFIG_SPIRAM=n`, `CONFIG_HOMEHUB_TUNNEL=n` and the mbedtls
internal-allocation options, as `devices/sdkconfig.ideaspark` does. Signed
apps on a classic ESP32 need chip revision 3.0 or later
(`CONFIG_ESP32_REV_MIN_3=y`).

## Flash

### Identify the board first

When you're asked to flash, work out which board is attached instead of asking
which one it is or assuming the default. Say which board you found and what told
you, then flash that profile. Getting it wrong writes another board's pin map,
flash size and status backend.

1. **Ask the running firmware.** A board already running this firmware names
   itself, and this needs no guessing:

   ```sh
   python3 tools/mute/chat.py --status     # {"board": "M5Stack StickS3", ...}
   ```

   It also logs the name once at startup, as `mute: board: <name>`, which
   `tools/mute/monitor.py PORT` captures because it resets the board first.
   `components/mute/mute_app.c` logs it; each `components/mute/boards/board_*.c`
   sets its `.name`. Boards with the full UI only: the other overlays don't log a name.

2. **Read the USB descriptor.** This works with no write access to the port and
   whatever the board is running, including vendor firmware or nothing.
   `ioreg -p IOUSB -l -w 0` on macOS, `lsusb -v` or `udevadm info /dev/ttyACM0`
   on Linux:

   | Descriptor | Board |
   |---|---|
   | Espressif `303a:1001`, "USB JTAG/serial debug unit" | the chip's own USB: C5, C6, S3 and the S3 boards with the full UI. Its serial number is the MAC |
   | CH340 (`1a86:7523`) | ideaspark, SenseCAP Indicator, reTerminal E1001 |
   | CH9102 | M5Stack StickC Plus2 |
   | CH342, two `usbmodem` ports | SenseCAP Watcher: the S3 console is the one ending in `3`, the other is the Himax camera |

3. **Ask the chip.** When you can write to the port (this resets the board):

   ```sh
   python -m esptool -p PORT chip-id      # read-mac for the MAC
   ```

   The target narrows it a long way: `esp32c5` is the DevKitC-1, `esp32c6` the
   Waveshare C6, `esp32` the ideaspark or the StickC Plus2.

4. **Fall back to a read-only capture.** If the board is mid-run and you can't
   write to the port, the `## Monitor` recipe below reads it without resetting,
   and a talk press logs the board's own button name as `mute_input: waking
   (<hint>)`:

   | Hint | Board |
   |---|---|
   | `front` | M5Stack StickS3, or StickC Plus2 — tell them apart by the port: the StickS3 is native USB, the Plus2 is a CH9102 `usbserial` |
   | `top` | Waveshare ESP32-S3-Touch-AMOLED-1.75C |
   | `bottom right` | AIPI Lite |
   | `wheel` | Seeed SenseCAP Watcher |
   | `boot` | Waveshare ESP32-C6-Touch-AMOLED-1.8 |

Ask the user only when these come up empty or contradict each other, and say
what you found and what's ambiguous rather than asking from scratch.

### Flash it

```sh
idf.py -p PORT flash                           # default DevKitC-1 build
tools/board.sh devkit flash PORT               # same, in build-devkit/
cd build && python -m esptool --chip esp32c5 -b 460800 \
  --before default-reset --after hard-reset write-flash "@flash_args"
```

Typical ports: `/dev/cu.usbmodem*` or `/dev/ttyACM*` for native USB (C5, S3 and
C6 boards), and `/dev/cu.usbserial-*`, `/dev/cu.wchusbserial*` or `/dev/ttyUSB*`
for CH340 bridges (ideaspark, SenseCAP Indicator, reTerminal E1001). On Linux,
add yourself to the `dialout` (or `uucp`) group. If the chip won't enter the
bootloader, hold BOOT, tap RESET, release BOOT, and flash again.

The M5Stack StickC Plus2's CH9102 bridge shows up as `/dev/cu.usbserial-*` or
`/dev/ttyACM*`. It drops out above 230400 baud, so `tools/mute/board.sh`
flashes it at 230400.

The SenseCAP Watcher's bottom USB-C port has a CH342 bridge with two ports,
both `/dev/cu.usbmodem*` on macOS. The ESP32-S3 console is the second (ending
in `3`); the first is the Himax camera chip. Mute overwrites the Watcher's
factory data, so back up its `nvsfactory` partition first (see
`devices/README.md`).

The CH342 has no flow control and drops bytes when esptool sends a whole
packet at once, at any baud and on any USB port. Plain esptool then fails with
`Failed to write to target RAM (result was 0107: Checksum error)` while
uploading its stub, or `0105: The format of the received message is invalid`
on the first flash block, by which point it has erased the bootloader. That
leaves the Watcher unable to boot until a flash succeeds; the ROM loader still
answers, so it isn't bricked. `tools/mute/board.sh flash watcher` goes through
`tools/mute/paced_esptool.py`, which takes esptool's arguments and sends 64
bytes at a time at the line rate, at 115200 baud. Use it in place of
`python -m esptool` for anything that writes to the Watcher. A lower baud or
another USB port doesn't help. Pacing works with macOS's built-in driver, so
WCH's driver isn't needed.

`board.sh` prints only esptool's last three lines, so a Watcher flash shows
nothing for about three minutes. Don't interrupt it, which leaves the board
unbootable again. To watch the progress, run the wrapper from the build
directory:

```sh
cd build-mute-sensecap-watcher
python ../tools/mute/paced_esptool.py --chip esp32s3 -p PORT -b 115200 \
  --before default-reset --after hard-reset write-flash "@flash_args"
```

- `flash` writes the bootloader, the partition table (at `0x10000`, not the IDF
  default `0x8000`), otadata, phy_init and the app to `ota_0`. NVS is left
  alone, so pairing and Wi-Fi credentials survive a reflash.
- `idf.py -p PORT erase-flash` wipes everything, including pairing. Do this
  only when you mean to start from scratch.
- The app is signed with the committed `dev_signing_key.pem`, and Secure Boot
  is **not** enabled, so flashing never burns eFuses. Keep it that way: don't
  enable `CONFIG_SECURE_BOOT`, flash encryption, or
  `CONFIG_HOMEHUB_PAIRING_EFUSE_AUTH` on a board you want to keep reflashing.

## Monitor

`idf.py -p PORT monitor` (Ctrl-] to quit) resets the board and needs an
interactive terminal. An agent without a TTY can do a read-only capture that
doesn't reset the board:

```py
import os, termios, time
fd = os.open("/dev/cu.usbmodem1101", os.O_RDONLY | os.O_NOCTTY | os.O_NONBLOCK)
attrs = termios.tcgetattr(fd)
attrs[2] &= ~termios.HUPCL   # don't toggle DTR/RTS (which resets) on close
termios.tcsetattr(fd, termios.TCSANOW, attrs)
deadline = time.monotonic() + 10
while time.monotonic() < deadline:
    try:
        chunk = os.read(fd, 4096)
        if chunk:
            os.write(1, chunk)
    except BlockingIOError:
        time.sleep(0.05)
```

To capture from boot, use `tools/mute/monitor.py PORT [secs]`, which resets the
board first (it needs `pyserial`). A healthy boot logs
`link.main: Mute Gadget starting`. For a board with the full UI, `$(tools/mute/ports.py BOARD)`
gives the port, with BOARD as in `tools/mute/board.sh`.

## Update my avatar

When someone asks to put their own avatar on their board (or to update or
change it), run this from `esp32/` with the sandbox off (it uses the serial
port) and let it finish. It takes several minutes, mostly waiting for the model:

```sh
python3 tools/mute/avatar.py                    # draw their avatar
python3 tools/mute/avatar.py --edit "CHANGE"    # change the one they have
```

It checks the board, asks the model for the renderer through the board,
saves it to `components/mute/avatar/mute_pixel.c` (gitignored), checks it on
the host, then builds and flashes. Progress and errors go to stderr. Relay the
line that starts with `Mute drew:` and the GIF paths. On failure, pass the
message on. The exit status says what kind of failure it was:

- **2**: no board, or it can't do this. If it says the board doesn't answer,
  rerun with `--board s3` or `--board aipi` (from the board's name, or ask) to
  flash firmware that can. On the C6 or Watcher, follow the manual steps in
  `tools/mute/AVATAR_RECIPE.md`.
- **3**: the board isn't on Wi-Fi, or has no device token, so it can't reach
  the server. Tell them, and point them to the build settings above or
  `tools/mute/ble_setup.html`.
- **1**: the model's file still fails after two fix rounds, or the build or flash
  failed. The previous avatar is in `mute_pixel.c.prev`, next to the new one.

Don't commit anything in `components/mute/avatar/`. To see what the board
says, run `python3 tools/mute/chat.py --status`. To ask the model something,
run `python3 tools/mute/chat.py "question"`.

The default avatar is in `avatar/`: its renderer (`mute_pixel.c`) and
its status-screen animation (`happy_anim.c/.h`, drawn by
`tools/gen_happy_anim.py`).

Third-party code keeps its upstream license and header: `minimp3.h` (CC0) and
`main/pixel_font.c` (BSD-2-Clause, Adafruit). Don't restyle them or replace
their headers with the Apache one; `components/minimp3/README.md` says how to
update minimp3.

## First boot

With `CONFIG_MUTE_SERVER_HOST`, `CONFIG_MUTE_DEVICE_TOKEN` and the Wi-Fi
settings built in, the device stores the token on first boot, joins Wi-Fi and
connects to the server. It needs no app and no pairing.

The status LED (or the edge bars or avatar on display boards) shows the state:

| Colour | Meaning |
|---|---|
| orange, breathing | no Wi-Fi or token built in; BLE setup is open |
| blue | Wi-Fi or server session coming up |
| green | connected (tunnel up, or control session up when the tunnel is off) |
| yellow, blinking | connection lost, reconnecting |
| purple | no device token |
| red, blinking | error |

Button (BOOT on the dev boards):

- **hold for 5 s**: forget saved Wi-Fi and tokens. Settings built into the
  firmware come back on the next boot.

The BLE pairing code (community pairing v5, `main/link_pairing.c`) is still in
the firmware, but no app speaks it any more; boards with the full UI can also
be set up over BLE from `tools/mute/ble_setup.html`.

## Configuration gotchas

- Each build's generated `sdkconfig` lives in its build directory
  (`build/sdkconfig`, `build-<board>/sdkconfig`). Once it exists, it overrides
  later edits to `sdkconfig.defaults` or the overlays. After changing those,
  delete the generated `sdkconfig` (or the whole build directory) and rebuild.
  `cmake/validate_config.cmake` stops the build if a stale config has the old
  TCP buffer sizes or cJSON nesting limit.
- Edit per-build settings with `idf.py -B <dir> menuconfig` (under "ESP32
  Device SDK"). Don't commit a generated `sdkconfig`: it's gitignored for a reason.
- OTA is off by default (`CONFIG_HOMEHUB_OTA_ENABLED=n`, version `999.0.0`),
  except on boards with the full UI. Set a version with `-DPROJECT_VER=1.0.0` or
  `version.txt`.
- Don't move offsets in `partitions.csv`. `prod_data` and `prod_bak` are fixed
  manufacturing locations, and the table offset of `0x10000` leaves room for a
  larger Secure Boot bootloader. Check the `check_sizes` line in the build
  output: app slots are 2 MB (4 MB on Mute).

## Protocol names

`hatch` survives only in wire identifiers the server and the Linux client
share with this firmware: the `hatch_refresh:` auth prefix, the `hatch-web`
app id, the `HatchLink/` user agent, pairing labels such as
`hatch-link-pairing-v%d`, and the setup commands (`hatch.token` and the rest).
Some older identifiers still carry it (`mute_hatch_*`, `CONFIG_MUTE_HATCH`).
Don't use it in new names.

## Tests

These are host-side Python unittest harnesses that compile the firmware C/C++
against fakes, with no board or IDF environment required:

```sh
python3 -m unittest discover -s tests -p 'test_*.py'
```

Run one `idf.py build` first: `test_link_discovery` compiles cJSON from
`managed_components/`, and that directory only exists after a build. Set `CC`
or `CXX` to change compilers.

## Before you hand back work

1. `idf.py build` (and the board build you touched) passes, with the size check
   under the slot limit.
2. The host tests pass.
3. If you flashed, the boot log reaches `starting`, and the log shows no
   panic or reboot loop.
