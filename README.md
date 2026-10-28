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
2. Set the Wi-Fi, OpenWeather, weather location, Transitous contact, tram stop and walking-time fields in `.env`. Set `SNORKEL_URL` to the snorkel board's `/data` URL.
3. Build and upload firmware with `pio run -e T5_4_7Inc_Plus_V2 -t upload`.
4. Upload the generated SPIFFS settings with `pio run -e T5_4_7Inc_Plus_V2 -t uploadfs`.

Git ignores `.env`. The build script writes private settings under `.pio`. The firmware contains the configured location, service URL and Transitous contact. Share firmware images with care. HTTPS uses the ISRG Root X1 certificate.

## Debug settings

Set `DEBUG_FAKE_WEATHER=1` to use sample weather data. Set `DEBUG_SERIAL_WAKE_SECONDS` to a positive number to add timer wakes for serial access. Set each value to `0` for normal operation. These settings can also be supplied as environment variables for one build.

## Transit data

Transit data is from [Transitous sources](https://transitous.org/sources/).

## Refresh schedule

The display updates every hour from 08:00 through 21:00 local time. The first successful update each day uses a full refresh. The 08:00 update also uses a full refresh. The display uses a full refresh after five partial updates.

The estimated battery charge controls the update interval:

| Estimated charge | Interval |
| --- | --- |
| 51–100% | 1 hour |
| 26–50% | 2 hours |
| 13–25% | 4 hours |
| 7–12% | 8 hours |
| 0–6% | Once daily at 08:00 |

The longer intervals start at 08:00 each day. The charge is an estimate.

The `ntp.timezone` setting uses a POSIX time-zone string. The default `CET-1CEST,M3.5.0,M10.5.0/3` follows Central European summer time. The device applies the configured transition rules automatically.

## Fonts

The display uses [Spleen](https://github.com/fcambus/spleen) by Frederic Cambus. The licence is in `fonts/Spleen.LICENSE`. Run `python3 scripts/generate_spleen.py` to regenerate `fonts/spleen.h` from the pinned upstream revision.

