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

