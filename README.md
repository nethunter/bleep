# CrowPanel 1.28 ESP32 LVGL Demo

Small Arduino/PlatformIO firmware demo for the ESP32-C3 CrowPanel 1.28 round
display.

## What it does

- Brings up the 240x240 GC9A01 display with LovyanGFX.
- Runs a simple LVGL UI with a battery arc, status text, and three menu rows.
- Reads the 1100 mAh LiPo as voltage and estimates state of charge.
- Uses the custom button on GPIO 1:
  - Short press: select the highlighted menu row, then advance the highlight.
  - Long press: soft screen on/off.
- Uses the CST816D touch controller when it is present, so menu rows can also be
  tapped directly.

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
| Expander panel power | pin 4 |
| Expander backlight | pin 2 |

The original vendor demo did not include a battery read example. This firmware
defaults to `BATTERY_ADC_PIN=3` and `BATTERY_DIVIDER=2.0`. If the first flash
shows the wrong voltage, change those in `platformio.ini`:

```ini
build_flags =
  -D ARDUINO_USB_MODE=1
  -D ARDUINO_USB_CDC_ON_BOOT=1
  -D LV_CONF_INCLUDE_SIMPLE
  -D BATTERY_ADC_PIN=3
  -D BATTERY_DIVIDER=2.0
  -I include
```

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
