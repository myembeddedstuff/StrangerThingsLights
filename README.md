# Stranger Things Lights

A Stranger Things style letter wall running on a XIAO ESP32-S3. The board creates its own Wi-Fi network and serves a web page to light letters, send messages and tweak the settings.

Full build write-up, schematic and photos: **[https://myembeddedstuff.com/stranger-things-lights-esp32-s3]**

## Hardware

- Seeed Studio XIAO ESP32-S3
- WS2812B strip, data on **A0** (GPIO1) through a series resistor
- 5V and GND from the XIAO to the strip

The firmware is built for up to 100 LEDs, so LED numbers in the settings go from 0 to 99.

## Flashing

Download `stranger-things-lights.bin` from the releases page and flash it at address `0x0`:

```
esptool.py --chip esp32s3 --port COM3 write_flash 0x0 stranger-things-lights.bin
```

Change `COM3` to your port (`/dev/ttyACM0` on Linux). If it doesn't enter download mode, hold BOOT while plugging the board in.

## Using it

1. Connect to the Wi-Fi network `XIAO-LED-Control` (password `led12345`)
2. Open `http://192.168.4.1/`

Letter to LED mapping, colors and timings are set in the Settings tab and kept after a reboot.


<img src="apk_screenshoot.png" alt="Web interface" width="300">

---

Designed by [My Embedded Stuff](https://www.myembeddedstuff.com)
