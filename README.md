# LilyGo-EPD-4-7-OWM-Weather-Display

[How to get started quickly](https://youtu.be/bTdW0QTEK70)

# FAQ

1. After flash is completed, burning cannot continue. Please put the device into download mode.
   1. Connect the board via the USB cable
   2. Press and hold the BOOT(**IO0**) button , While still pressing the BOOT(**IO0**) button, press **REST**
   3. Release the **REST**
   4. Release the BOOT(**IO0**) button
   5. Upload sketch

## Private build settings

1. Copy `.env.example` to `.env`.
2. Set `WIFI_SSID`, `WIFI_PASSWORD`, and `OWM_API_KEY` in `.env`.
3. Build and upload firmware with `pio run -e T5_4_7Inc_Plus_V2 -t upload`.
4. Upload the generated SPIFFS settings with `pio run -e T5_4_7Inc_Plus_V2 -t uploadfs`.

Git ignores `.env`. The tracked `data/config.json` is a safe template. The build script writes a private SPIFFS copy under `.pio/fsdata`.

## Refresh schedule

The display updates every hour from 08:00 through 21:00 local time. The first successful update each day uses a full refresh. The 08:00 update also uses a full refresh. The display uses a full refresh after five partial updates.

The battery voltage controls the update interval:

| Battery voltage | Interval |
| --- | --- |
| 4.15 V or higher | 1 hour |
| 3.95 V to less than 4.15 V | 6 hours |
| 3.75 V to less than 3.95 V | 12 hours |
| Less than 3.75 V | 24 hours |

The longer intervals start at 08:00 each day. The voltage is an estimate.

The `ntp.timezone` setting uses a POSIX time-zone string. The default `CET-1CEST,M3.5.0,M10.5.0/3` follows Central European summer time. The device applies the configured transition rules automatically.

## Fonts

The display uses [Spleen](https://github.com/fcambus/spleen) by Frederic Cambus. The licence is in `fonts/Spleen.LICENSE`. Run `python3 scripts/generate_spleen.py` to regenerate `fonts/spleen.h` from the pinned upstream revision.

