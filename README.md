# frank-gamepad

A USB gamepad button-mapping tool for the RP2350 (M2 board layout).

Plug any USB HID gamepad — or an Xbox 360 / Xbox One / Original Xbox pad
over XInput — in and it walks you through pressing each of the 12
SNES-style buttons. It diffs the raw reports against a quiescent baseline
to figure out which byte (or bit) changes for each button, and writes the
result to the SD card as `gamepad_VID_PID.txt`.

The reason this exists: every USB pad encodes its buttons differently, and
the firmwares that share the Murmulator/FRANK HDMI+USB stack (murmsnes,
frank-wolf3d, frank-doom, ...) need a portable way to discover per-pad
mappings without attaching a PC.

## Supported boards

M2-only. RP2350 boards with integrated HDMI, SD card, and native USB:

- **[FRANK](https://rh1.tech/projects/frank?area=about)** — RP Pico 2
  dev board with HDMI and extra I/O
- **[Murmulator](https://murmulator.ru)** — RP Pico 2 board with HDMI and SD

No extra wiring needed.

## Features

- 640x480 HDMI output (256x224 frame doubled)
- On-screen SNES-style controller schematic, with the button you should press
  next highlighted
- 12-step guided capture
- Supports generic USB HID gamepads AND XInput controllers
  (Xbox 360 wired/wireless, Xbox One, Original Xbox). XInput reports are
  normalised to a canonical 8-byte frame so the output format is identical
- Writes a plain-text `gamepad_VID_PID.txt` to the SD card root
- Loops after each capture so you can enumerate several pads in one session
- HDMI runs on Core 1, USB host and capture state machine on Core 0, so HDMI
  sync is not disturbed by USB traffic
- Two build flavors:
  - `./build.sh ON` — release. Native USB is the HID host; UART0 is the
    debug console.
  - `./build.sh` — debug. Native USB is USB-CDC stdio; gamepad capture is
    compiled out. Handy for checking HDMI and SD without a pad.

## Hardware

- Raspberry Pi Pico 2 (RP2350) or a compatible M2 board
- HDMI connector wired via 270 Ohm resistors
- SD card module, SPI mode
- Any USB HID gamepad
- USB-to-TTL UART adapter (9600 8-N-1) for the release build's console;
  optional, connects to GPIO 0 TX / GPIO 1 RX / GND

Unlike murmsnes, PSRAM is not required.

## Pin assignment (M2)

### HDMI

Wired via 270 Ohm resistors.

| Signal | GPIO |
|--------|------|
| CLK-   | 12   |
| CLK+   | 13   |
| D0-    | 14   |
| D0+    | 15   |
| D1-    | 16   |
| D1+    | 17   |
| D2-    | 18   |
| D2+    | 19   |

### SD card (SPI)

| Signal  | GPIO |
|---------|------|
| CLK     | 6    |
| CMD     | 7    |
| DAT0    | 4    |
| DAT3/CS | 5    |

### UART console (release build)

| Signal | GPIO |
|--------|------|
| TX     | 0    |
| RX     | 1    |

Baud: 9600, 8-N-1. The release build re-runs `uart_init()` at 9600 explicitly
so the console is readable on a garden-variety USB-TTL adapter.

### USB HID

Native RP2350 USB port. No GPIO assignment — the pad plugs into the board's
USB socket.

## How to use

### SD card setup

1. Format an SD card as FAT32.
2. Put it in the board. Nothing needs to be on the card — the tool writes
   output files at the root.

### Boot and capture

1. Power on with the gamepad plugged in (hot-plug also works).
2. The HDMI output shows the controller schematic, the title
   `FRANK-GAMEPAD CAPTURE`, and a status line.
3. Once the pad enumerates, the status line becomes
   `CAPTURING BASELINE... HANDS OFF`. Don't touch the pad for about a second
   while the tool records the quiescent report.
4. The tool prompts you to press each button in order:
   UP, DOWN, LEFT, RIGHT, SELECT, START, Y, B, A, X, L, R.
   The current button is highlighted amber on the schematic. When you press
   it, it flashes green and the tool moves on.
5. After all 12 buttons, the tool writes `gamepad_VID_PID.txt` to the SD
   card root and goes back to the `PLUG IN A USB GAMEPAD` screen.

### Output format

Example for a DragonRise USB joystick (VID 0x0079, PID 0x0011):

```
# frank-gamepad capture log
Source=HID
VID=0x0079
PID=0x0011
Manufacturer=(none)
Product=(none)
Serial=(none)
ReportLen=8
Baseline=01 7F 7F 7F 7F 0F 00 00

UP=byte[4]:-0x7F
#   raw: 01 7F 7F 7F 00 0F 00 00
DOWN=byte[4]:+0x7F
#   raw: 01 7F 7F 7F FF 0F 00 00
LEFT=byte[3]:-0x7F
#   raw: 01 7F 7F 00 7F 0F 00 00
RIGHT=byte[3]:+0x7F
#   raw: 01 7F 7F FF 7F 0F 00 00
SELECT=byte[6]:+0x10
#   raw: 01 7F 7F 7F 7F 0F 10 00
START=byte[6]:+0x20
...
```

Each captured line is one of:

- `byte[N]:+0xMM` — bits went from 0 to 1 (button pressed)
- `byte[N]:-0xMM` — bits went from 1 to 0 (e.g. a D-pad axis resting at 0x7F
  dropping to 0x00)
- `byte[N]:=0xVV` — mixed change; raw value is shown

The `#   raw:` line always shows the full report observed when the button
was pressed, so downstream firmware can use whichever form is easier.

For XInput pads, the captured report is a canonical 8-byte frame rather
than the raw USB packet:

```
byte[0] = wButtons low  (DPAD U/D/L/R, START, BACK, LS, RS)
byte[1] = wButtons high (LB, RB, GUIDE, SHARE, A, B, X, Y)
byte[2] = left trigger  (0..255)
byte[3] = right trigger (0..255)
byte[4] = left stick X, high byte
byte[5] = left stick Y, high byte
byte[6] = right stick X, high byte
byte[7] = right stick Y, high byte
```

This keeps the `byte[N]:+0xMM` output format uniform across both transports,
so downstream firmware doesn't need to know whether the pad was an HID
joystick or an Xbox controller. A `Source=HID` or `Source=XINPUT` line at
the top of the log records which path captured the data.

## Building

### Prerequisites

1. Install the [Pico SDK](https://github.com/raspberrypi/pico-sdk) 2.0 or
   later.
2. `export PICO_SDK_PATH=/path/to/pico-sdk`
3. Install the ARM GCC toolchain.

### Release build (capture tool)

```bash
git clone https://github.com/rh1tech/frank-gamepad.git
cd frank-gamepad
./build.sh ON          # USB_HID=ON
```

Output: `build/frank-gamepad.uf2`.

### Debug build (CDC stdio, no gamepad)

```bash
./build.sh             # USB_HID=OFF, default
```

In this mode the native USB port is a USB-CDC serial device; open it at any
baud and you get the boot log. The capture state machine is compiled out
and the UI idles on the `PLUG IN A USB GAMEPAD` screen. Useful for checking
HDMI and SD without USB host concerns.

### Flashing

Hold BOOTSEL, plug in, copy `build/frank-gamepad.uf2` to the mounted
`RPI-RP2` drive. Or:

```bash
./flash.sh
# equivalent to:
picotool load -f build/frank-gamepad.uf2 && picotool reboot -f
```

## Troubleshooting

### No HDMI signal

- Make sure you flashed the M2 build. Other board variants use different
  HDMI GPIOs.
- The driver is tuned for `sys_clk` = 252 MHz. If you've stripped the clock
  init, HDMI will stay dark.
- If the image rolls vertically every few seconds, the pixel clock is
  drifting. The firmware calls `set_sys_clock_khz(252000, true)` which was
  stable on the M2 boards I tested.

### Capture hardfaults or HDMI drops partway through a session

The capture arrays live in `.bss`, not on the stack. An earlier revision
stack-allocated `button_capture_t caps[12]` — about 4 KB — which overflowed
the default 2 KB core stack and trashed the scratch-SRAM-resident HDMI DMA
state. That looks like "HDMI died" but it's really a hardfault. If you see
similar symptoms after changing the capture logic, check that any new large
arrays are `static`.

### UART console shows garbage

The release build re-inits UART at 9600 baud. Anything else on the adapter
side (115200, 57600, ...) will show as line noise.

### Gamepad enumerates but button presses do nothing

- Check that baseline captured cleanly — UART log should have
  `[cap] baseline settled len=N`.
- Some pads only emit HID reports on state change. The capture logic
  handles that, but if the pad is fully silent at rest, wiggle a stick first
  so it sends at least one report.
- To double-check the mapping, press the button again and compare raw bytes
  against the `#   raw:` line in the output file.

## License

Copyright (c) 2026 Mikhail Matveev <<xtreme@rh1.tech>>

frank-gamepad is released under the GNU General Public License v3.0 or
later. See [LICENSE](LICENSE) for full text and for the third-party
components bundled with this repository.

## Acknowledgments

| Project | Author(s) | License | Used for |
|---------|-----------|---------|----------|
| [FatFS](http://elm-chan.org/fsw/ff/) | ChaN | Custom permissive | FAT32 filesystem |
| [pico_fatfs_test](https://github.com/elehobica/pico_fatfs_test) | Elehobica | BSD-2-Clause | SD card PIO-SPI driver |
| [TinyUSB](https://github.com/hathach/tinyusb) | Ha Thach | MIT | USB HID host |
| [tusb_xinput](https://github.com/Ryzee119/tusb_xinput) | Ryan Wendland (usb64) | MIT | TinyUSB XInput class driver (vendored via [SpeccyP](https://github.com/billgilbert7000/SpeccyP)) |
| [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk) | Raspberry Pi Foundation | BSD-3-Clause | Hardware abstraction |
| [murmsnes](https://github.com/rh1tech/frank-snes) | Mikhail Matveev | GPL-3.0 | HDMI, SD, USB HID drivers |

Thanks to the Murmulator community for the HDMI, USB HID, and PSRAM drivers
this project reuses.