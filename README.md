# claude-desktop-buddy — FNK0104B fork

A fork of [anthropics/claude-desktop-buddy](https://github.com/anthropics/claude-desktop-buddy)
ported to the [Freenove FNK0104B](https://store.freenove.com/products/fnk0104)
(ESP32-S3 + 2.4″ capacitive touch panel + ES8311 audio). The upstream
firmware targets the M5StickC Plus; this fork drops the M5 dependency and
rebuilds the UI for the FNK010B's larger landscape touchscreen.

Claude for macOS and Windows can connect Claude Cowork and Claude Code to
maker devices over BLE, so developers and makers can build hardware that
displays permission prompts, recent messages, and other interactions. This
firmware turns the FNK0104B into a desk pet that lives off permission
approvals and interaction with Claude — sleeping when nothing's happening,
waking when sessions start, getting visibly impatient when an approval
prompt is waiting, and letting you approve or deny right from the device.

> **Building your own device?** You don't need any of the code here. See
> **[REFERENCE.md](REFERENCE.md)** (inherited from upstream) for the wire
> protocol: Nordic UART Service UUIDs, JSON schemas, and the folder push
> transport.

## Hardware

The firmware targets the **[Freenove FNK0104B](https://store.freenove.com/products/fnk0104)**
— an ESP32-S3 dev board with:

- 2.4″ 320×240 ILI9341 LCD (used in landscape, `setRotation(3)`)
- FT6336U capacitive touch overlay
- ES8311 audio codec + small speaker (driven by an I²S feeder task on
  core 0 so beeps stay glitch-free under render load)
- PSRAM (used to back the 320×240×16bpp sprite)

The upstream M5StickC Plus build target has been removed from this fork; if
you need it, work from
[anthropics/claude-desktop-buddy](https://github.com/anthropics/claude-desktop-buddy).

## Flashing

Install
[PlatformIO Core](https://docs.platformio.org/en/latest/core/installation/),
then:

```bash
pio run -e fnk010b -t upload
```

If you're starting from a previously-flashed device, wipe it first:

```bash
pio run -e fnk010b -t erase && pio run -e fnk010b -t upload
```

## Pairing

To pair your device with Claude, first enable developer mode (**Help →
Troubleshooting → Enable Developer Mode**). Then, open the Hardware Buddy
window in **Developer → Open Hardware Buddy…**, click **Connect**, and pick
your device from the list. macOS will prompt for Bluetooth permission on
first connect; grant it.

<p align="center">
  <img src="docs/menu.png" alt="Developer → Open Hardware Buddy… menu item" width="420">
  <img src="docs/hardware-buddy-window.png" alt="Hardware Buddy window with Connect button and folder drop target" width="420">
</p>

Once paired, the bridge auto-reconnects whenever both sides are awake.

If discovery isn't finding the device:

- Make sure it's awake (tap the screen)
- Check **Menu → settings → BT** is on

## Controls

The FNK010B is touch-native — there are no physical buttons aside from the
power button. The bottom 36 px of the screen is a persistent taskbar:

| Icon | Tap | Notes |
| --- | --- | --- |
| **HOME** | Go to the home screen (pet + stats pane + transcript) | Closes any open modal |
| **PET** | Go to the pet screen (pet + visual status bars on the left, text stats + how-to on the right) | Closes any open modal |
| **MENU** | Open / close the menu modal (settings · turn off · demo · close) | Tapping outside the panel also closes it |

Approval prompts override the taskbar: when a Claude prompt is pending, the
entire screen below a short header turns into two giant tap zones — green
**APPROVE** on the left half, red **DENY** on the right half.

Other interactions:

- **Power** button (left side, short press): toggle screen off / on
- **Power** button (~6 s hold): hard power off via AXP
- **Shake**: pet goes dizzy
- **Face-down**: nap (energy refills, screen dims)
- **Idle 30 s**: screen sleeps; tap anywhere to wake

## ASCII pets

Eighteen pets, each with seven animations (sleep, idle, busy, attention,
celebrate, dizzy, heart). **Menu → settings → Pet** cycles them with a
counter. Choice persists to NVS.

## GIF pets

If you want a custom GIF character instead of an ASCII buddy, drag a
character pack folder onto the drop target in the Hardware Buddy window.
The app streams it over BLE and the device switches to GIF mode live.

A character pack is a folder with `manifest.json` and 96 px-wide GIFs:

```json
{
  "name": "bufo",
  "colors": {
    "body": "#6B8E23",
    "bg": "#000000",
    "text": "#FFFFFF",
    "textDim": "#808080",
    "ink": "#000000"
  },
  "states": {
    "sleep": "sleep.gif",
    "idle": ["idle_0.gif", "idle_1.gif", "idle_2.gif"],
    "busy": "busy.gif",
    "attention": "attention.gif",
    "celebrate": "celebrate.gif",
    "dizzy": "dizzy.gif",
    "heart": "heart.gif"
  }
}
```

State values can be a single filename or an array. Arrays rotate: each
loop-end advances to the next GIF, useful for an idle activity carousel so
the home screen doesn't loop one clip forever.

GIFs are 96 px wide; the pet pane on this board is 160 px wide so there's
extra horizontal room compared to the upstream M5StickC Plus build.
`tools/prep_character.py` handles resizing: feed it source GIFs at any
sizes and it produces a 96 px-wide set where the character is the same
scale in every state.

The whole folder must fit under 1.8 MB —
`gifsicle --lossy=80 -O3 --colors 64` typically cuts 40–60 %.

See `characters/bufo/` for a working example.

If you're iterating on a character and would rather skip the BLE
round-trip, `tools/flash_character.py characters/bufo` stages it into
`data/` and runs `pio run -t uploadfs` directly over USB.

## The seven states

| State       | Trigger                     | Feel                        |
| ----------- | --------------------------- | --------------------------- |
| `sleep`     | bridge not connected        | eyes closed, slow breathing |
| `idle`      | connected, nothing urgent   | blinking, looking around    |
| `busy`      | sessions actively running   | sweating, working           |
| `attention` | approval pending            | alert, **screen border flashes red** (toggleable via settings → Alert) |
| `celebrate` | level up (every 50 K tokens)| confetti, bouncing          |
| `dizzy`     | you shook the device        | spiral eyes, wobbling       |
| `heart`     | approved in under 5 s       | floating hearts             |

## Project layout

```
src/
  main.cpp           — loop, state machine, UI screens, touch dispatch
  buddy.cpp          — ASCII species dispatch + render helpers
  buddies/           — one file per species, seven anim functions each
  ble_bridge.cpp     — Nordic UART service, line-buffered TX/RX
  character.cpp      — GIF decode + render
  data.h             — wire protocol, JSON parse
  xfer.h             — folder push receiver
  stats.h            — NVS-backed stats, settings, owner, species choice
  board/             — FNK0104B HAL (display, touch, audio, RTC, power)
    board.h          — HAL shim re-exporting an M5-like API
    display.cpp      — attention border + cover detection
    touch.cpp/.h     — FT6336U gesture state machine (tap / swipe / long-press)
    audio.cpp        — ES8311 init + FreeRTOS audio feeder task
    es8311/          — ES8311 codec driver
    rtc.cpp          — software RTC (no hw RTC on this board)
    power.cpp        — backlight PWM + soft power-off
    imu_stub.cpp     — IMU stub (face-down via touch coverage instead)
characters/          — example GIF character packs
tools/               — generators and converters
```

## Availability

The BLE API is only available when the desktop apps are in developer mode
(**Help → Troubleshooting → Enable Developer Mode**). It's intended for
makers and developers and isn't an officially supported product feature.
