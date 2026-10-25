Import("env")

import json
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
required = ("WIFI_SSID", "WIFI_PASSWORD", "OWM_API_KEY")
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
config_path.write_text(json.dumps(config, indent="\t") + "\n", encoding="utf-8")
print("Prepared SPIFFS files from .env.")
