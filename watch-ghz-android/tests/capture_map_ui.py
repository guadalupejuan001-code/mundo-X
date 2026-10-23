"""Capture the installed phone app after instrumentation has removed its copy."""
import pathlib
import re
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
        (destination / "map-control.png").write_bytes(image)
        (destination / "map-control.xml").write_bytes(raw)
        assert image[:8] == b"\x89PNG\r\n\x1a\n", "Screenshot output is not PNG"
        width, height = struct.unpack(">II", image[16:24])
        display = adb("shell", "wm", "size").decode()
        sizes = {tuple(map(int, pair)) for pair in re.findall(r"(\d+)x(\d+)", display)}
        assert (width, height) in sizes, f"PNG {width}x{height} does not match Android display: {display}"
        assert height > width > 0, "Expected a portrait phone display"
        print(f"Verified loaded phone map screenshot: {width}x{height}")
        break
    time.sleep(1)
else:
    (destination / "map-control-error.xml").write_bytes(raw)
    raise AssertionError("The phone UI did not expose the loaded map: " + text)


# Inspect the actual controls below the 3D canvas and the sensor status panel.
width, height = list(re.findall(r"(\d+)x(\d+)", adb("shell", "wm", "size").decode()))[-1]
width, height = int(width), int(height)
for target, basename in [("Campos + objetos", "field-controls"), ("Espacio físico", "field-sensors")]:
    for attempt in range(14):
        adb("shell", "uiautomator", "dump", "/sdcard/fields-window.xml")
        raw = adb("shell", "cat", "/sdcard/fields-window.xml")
        root = ET.fromstring(raw)
        candidates = [node for node in root.iter("node") if target in node.get("text", "")]
        visible = False
        for node in candidates:
            coords = list(map(int, re.findall(r"\d+", node.get("bounds", ""))))
            if len(coords) == 4 and coords[2] > coords[0] and 0 < coords[3] <= height * .55:
                visible = True
        if visible:
            (destination / (basename + ".xml")).write_bytes(raw)
            (destination / (basename + ".png")).write_bytes(adb("exec-out", "screencap", "-p"))
            print("Verified visible phone panel:", target)
            break
        # The canvas consumes drags. Scroll in the page margin beside it.
        adb("shell", "input", "swipe", str(width - 10), str(int(height * .82)), str(width - 10), str(int(height * .42)), "400")
    else:
        raise AssertionError("Phone panel not reachable by scrolling: " + target)
