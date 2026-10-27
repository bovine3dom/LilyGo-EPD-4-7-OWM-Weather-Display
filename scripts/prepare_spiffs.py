# SPDX-License-Identifier: GPL-3.0-only
Import("env")

import json
import math
import os
import shutil
from pathlib import Path

project_dir = Path(env.subst("$PROJECT_DIR"))
source_dir = project_dir / "data"
build_data_dir = project_dir / ".pio" / "fsdata"
env_file = project_dir / ".env"

if not env_file.is_file():
    raise RuntimeError("Missing .env. Copy .env.example and set its values.")


def read_dotenv(path):
    values = {}
    for line_number, raw_line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        key, separator, raw_value = line.partition("=")
        if not separator:
            raise RuntimeError("Invalid .env entry on line {}.".format(line_number))
        raw_value = raw_value.strip()
        if raw_value.startswith('"'):
            try:
                value = json.loads(raw_value)
            except json.JSONDecodeError as error:
                raise RuntimeError("Invalid quoted value in .env line {}.".format(line_number)) from error
        elif raw_value.startswith("'") and raw_value.endswith("'"):
            value = raw_value[1:-1]
        else:
            value = raw_value.split("#", 1)[0].strip()
        values[key.strip()] = value
    return values


secrets = read_dotenv(env_file)
required = ("WIFI_SSID", "WIFI_PASSWORD", "OWM_API_KEY", "WEATHER_CITY", "WEATHER_COUNTRY",
            "TRAM_STOP_NAME", "TRAM_STOP_LATITUDE", "TRAM_STOP_LONGITUDE")
missing = [key for key in required if not secrets.get(key)]
if missing:
    raise RuntimeError("Set {} in .env before building the SPIFFS image.".format(", ".join(missing)))

if build_data_dir.exists():
    shutil.rmtree(build_data_dir)
shutil.copytree(source_dir, build_data_dir)

config_path = build_data_dir / "config.json"
config = json.loads(config_path.read_text(encoding="utf-8"))
config["WLAN"]["ssid"] = secrets["WIFI_SSID"]
config["WLAN"]["password"] = secrets["WIFI_PASSWORD"]
config["OpenWeather"]["apikey"] = secrets["OWM_API_KEY"]
config["OpenWeather"]["city"] = secrets["WEATHER_CITY"]
config["OpenWeather"]["country"] = secrets["WEATHER_COUNTRY"]

contact = secrets.get("TRANSITOUS_CONTACT", "")
stop = secrets["TRAM_STOP_NAME"]
if any("\n" in value or "\r" in value for value in (contact, stop)):
    raise RuntimeError("Transitous settings must be single lines.")
try:
    lat = float(secrets["TRAM_STOP_LATITUDE"])
    lon = float(secrets["TRAM_STOP_LONGITUDE"])
    minutes = int(secrets.get("TRAM_MINIMUM_MINUTES", "15"))
    fake = int(os.environ.get("DEBUG_FAKE_WEATHER", secrets.get("DEBUG_FAKE_WEATHER", "0")))
    wake = int(os.environ.get("DEBUG_SERIAL_WAKE_SECONDS", secrets.get("DEBUG_SERIAL_WAKE_SECONDS", "0")))
except ValueError as error:
    raise RuntimeError("Invalid numeric setting in .env.") from error
if not (math.isfinite(lat) and math.isfinite(lon) and -89.995 <= lat <= 89.995 and -179.995 <= lon <= 179.995 and
        0 <= minutes <= 1440 and fake in (0, 1) and 0 <= wake <= 3600):
    raise RuntimeError("A numeric setting in .env is outside its permitted range.")

title = stop.upper()
if len(title) > 32:
    title = title[:29] + "..."
bbox = f"min={lat - 0.005:.6f}%2C{lon - 0.005:.6f}&max={lat + 0.005:.6f}%2C{lon + 0.005:.6f}"
strings = {"TRANSITOUS_CONTACT": contact, "TRAM_STOP_NAME": stop, "TRAM_STOP_TITLE": title, "TRAM_STOP_BBOX": bbox,
           "WEATHER_CITY": secrets["WEATHER_CITY"], "WEATHER_COUNTRY": secrets["WEATHER_COUNTRY"]}
header = "#pragma once\n" + "".join(f"#define {key} {json.dumps(value, ensure_ascii=False)}\n" for key, value in strings.items())
header += (f"#define TRAM_STOP_LATITUDE {lat:.8f}\n#define TRAM_STOP_LONGITUDE {lon:.8f}\n"
           f"#define TRAM_MINIMUM_SECONDS {minutes * 60}\n#define DEBUG_FAKE_WEATHER {fake}\n"
           f"#define DEBUG_SERIAL_WAKE_SECONDS {wake}\n")
header_path = project_dir / ".pio" / "private_config.h"
if not header_path.exists() or header_path.read_text(encoding="utf-8") != header:
    header_path.write_text(header, encoding="utf-8")
config_path.write_text(json.dumps(config, indent="\t") + "\n", encoding="utf-8")
print("Prepared SPIFFS files from .env.")
