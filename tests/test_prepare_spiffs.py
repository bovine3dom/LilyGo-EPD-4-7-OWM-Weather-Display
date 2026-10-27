#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check that the Transitous contact is included in the private build."""

import json
import os
from pathlib import Path
from unittest.mock import patch
import runpy
import tempfile

script = Path(__file__).resolve().parent.parent / "scripts" / "prepare_spiffs.py"

with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    (root / "data").mkdir()
    (root / "data" / "config.json").write_text(json.dumps({"WLAN": {}, "OpenWeather": {}}))
    (root / ".env").write_text('WIFI_SSID="network"\nWIFI_PASSWORD="password"\nOWM_API_KEY="key"\n'
                               'WEATHER_CITY="Exampleville"\nWEATHER_COUNTRY="GB"\nTRANSITOUS_CONTACT="contact@example.org"\n'
                               'TRAM_STOP_NAME="Sample Stop"\nTRAM_STOP_LATITUDE="51.5"\nTRAM_STOP_LONGITUDE="-0.12"\n'
                               'TRAM_MINIMUM_MINUTES="17"\nDEBUG_FAKE_WEATHER="1"\nDEBUG_SERIAL_WAKE_SECONDS="30"\n')

    class Env:
        def subst(self, value):
            assert value == "$PROJECT_DIR"
            return str(root)

    runpy.run_path(str(script), init_globals={"Import": lambda name: None, "env": Env()})
    config = json.loads((root / ".pio" / "fsdata" / "config.json").read_text())
    assert "Transitous" not in config
    header = (root / ".pio" / "private_config.h").read_text()
    assert '"contact@example.org"' in header and '"Sample Stop"' in header
    assert '#define TRAM_MINIMUM_SECONDS 1020' in header
    assert '#define DEBUG_FAKE_WEATHER 1' in header and '#define DEBUG_SERIAL_WAKE_SECONDS 30' in header
    assert config["OpenWeather"]["city"] == "Exampleville" and config["OpenWeather"]["country"] == "GB"
    assert "Transitous" not in json.loads((root / "data" / "config.json").read_text())
    with patch.dict(os.environ, {"DEBUG_FAKE_WEATHER": "0", "DEBUG_SERIAL_WAKE_SECONDS": "0"}):
        runpy.run_path(str(script), init_globals={"Import": lambda name: None, "env": Env()})
    normal = (root / ".pio" / "private_config.h").read_text()
    assert '#define DEBUG_FAKE_WEATHER 0' in normal and '#define DEBUG_SERIAL_WAKE_SECONDS 0' in normal

print("Private SPIFFS settings test passed")
