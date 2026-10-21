# link: https://stage.nexus.mcsbackend.com/ca.crt
# the link returns the root CA certificate for the WebSocket connection as text
# this script is used to download and save the root CA certificate as a .cpp
# output file path is .\lib\3_ssl_manager\root_ca.cpp

from __future__ import annotations

from pathlib import Path
import requests

try:
	Import("env")
except NameError:
	env = None


CERTIFICATE_URL = "https://stage.nexus.mcsbackend.com/ca.crt"
if env is not None:
	PROJECT_DIR = Path(env.subst("$PROJECT_DIR")).resolve()
else:
	script_path = globals().get("__file__")
	PROJECT_DIR = Path(script_path).resolve().parents[1] if script_path else Path.cwd()
OUTPUT_PATH = PROJECT_DIR / "lib" / "3_ssl_manager" / "root_ca.cpp"

def download_certificate() -> str:
    response = requests.get(CERTIFICATE_URL)
    if response.status_code == 200:
        return response.text
    raise ValueError(f"Failed to download certificate from {CERTIFICATE_URL}")

def save_certificate_to_file(certificate: str) -> None:
    file_content = f'#include "root_ca.h"\n\n#include <Arduino.h>\n\nconst char k_default_root_ca[] PROGMEM = R"(\n{certificate}\n)";'
    OUTPUT_PATH.parent.mkdir(parents=True, exist_ok=True)
    with open(OUTPUT_PATH, "w", encoding="utf-8") as f:
        f.write(file_content)

def main():
    data = download_certificate()
    print(data)
    save_certificate_to_file(data)

main()