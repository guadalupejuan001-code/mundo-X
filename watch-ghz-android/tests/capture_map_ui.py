"""Capture the installed phone app after instrumentation has removed its copy."""
import pathlib
import struct
import subprocess
import sys
import time
import xml.etree.ElementTree as ET

destination = pathlib.Path(sys.argv[1])
destination.mkdir(parents=True, exist_ok=True)


def adb(*arguments):
    return subprocess.check_output(["adb", *arguments], timeout=20)


launch = adb("shell", "am", "start", "-W", "-n", "com.mundox.ghzwatch/.MapActivity")
if b"Error" in launch:
    raise RuntimeError(launch.decode())

for attempt in range(10):
    adb("shell", "uiautomator", "dump", "/sdcard/map-window.xml")
    raw = adb("shell", "cat", "/sdcard/map-window.xml")
    root = ET.fromstring(raw)
    text = " ".join(node.get("text", "") + " " + node.get("content-desc", "")
                    for node in root.iter("node"))
    if "24 muestras" in text or "Mapa 3D, datos sintéticos, captura 1" in text:
        image = adb("exec-out", "screencap", "-p")
        assert image[:8] == b"\x89PNG\r\n\x1a\n", "Screenshot output is not PNG"
        width, height = struct.unpack(">II", image[16:24])
        assert width >= 1000 and height >= 2000, "Unexpected phone screenshot dimensions"
        (destination / "map-control.png").write_bytes(image)
        (destination / "map-control.xml").write_bytes(raw)
        print(f"Verified loaded phone map screenshot: {width}x{height}")
        break
    time.sleep(1)
else:
    (destination / "map-control-error.xml").write_bytes(raw)
    raise AssertionError("The phone UI did not expose the loaded map: " + text)
