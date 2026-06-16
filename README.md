# CrowPanel 1.28 ESP32 LVGL Demo

Small Arduino/PlatformIO firmware demo for the ESP32-C3 CrowPanel 1.28 round
display.

## What it does

- Brings up the 240x240 GC9A01 display with LovyanGFX.
- Runs a simple LVGL UI with status text and three menu rows
  (Display test / I2C / Touch).
- Uses the custom button on GPIO 1:
  - Short press: select the highlighted menu row, then advance the highlight.
  - Long press: enter ESP32-C3 deep sleep; press again to wake.
- Initializes the CST816D touch controller at `0x15`, while recognizing `0x51`
  on the I2C bus as the onboard BM8563 RTC.

## Pin assumptions

These pin assumptions were carried over from the original vendor demo before
the project was cleaned up into a PlatformIO firmware project:

| Function | GPIO / address |
| --- | --- |
| LCD DC | GPIO 2 |
| LCD CS | GPIO 10 |
| LCD SCK | GPIO 6 |
| LCD MOSI | GPIO 7 |
| I2C SDA | GPIO 4 |
| I2C SCL | GPIO 5 |
| Custom button | GPIO 1, active low |
| Touch INT | GPIO 0 |
| PI4IOE5V6408 | I2C `0x43` |
| CST816D touch | I2C `0x15` |
| BM8563 RTC | I2C `0x51` |
| Expander panel power | pin 4 |
| Expander touch/display enable | pin 3 |
| Expander backlight | pin 2 |

## Battery monitoring

This board has **no onboard battery-sense circuit**: the SH1.0 battery socket is
power-only, there is no voltage divider or fuel-gauge IC, and there is no ADC
line wired to the battery. On the ESP32-C3 every ADC-capable pin (GPIO0-GPIO5)
is already used for other functions on this board (note GPIO3 is the buzzer, not
a battery line). For that reason the firmware does **not** display a battery
percentage. Reading the battery would require a hardware modification: solder an
external voltage divider from BAT+ to a freed-up ADC GPIO and add the read code
back.

## Build and flash

Install PlatformIO, then run:

```sh
pio run
pio run -t upload
pio device monitor
```

This workspace now has PlatformIO installed in `.venv`, so these equivalents
also work here:

```sh
./.venv/bin/platformio run
./.venv/bin/platformio run -t upload
./.venv/bin/platformio device monitor
```

If PlatformIO cannot write to your global `~/.platformio` directory, use a local
core directory:

```sh
PLATFORMIO_CORE_DIR="$PWD/.platformio-core" ./.venv/bin/platformio run
```

The demo was compile-verified with PlatformIO, LVGL 8.4.0, LovyanGFX 1.2.21,
and the `esp32-c3-devkitm-1` PlatformIO board target.
