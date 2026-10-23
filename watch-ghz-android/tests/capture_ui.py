"""Capture the actual prepared app, rejecting a launcher/error screenshot."""
import pathlib
import re
import subprocess
import sys
import xml.etree.ElementTree as ET

destination = pathlib.Path(sys.argv[1])
destination.mkdir(parents=True, exist_ok=True)


def adb(*arguments):
    return subprocess.check_output(["adb", *arguments], timeout=20)


def ui():
    adb("shell", "uiautomator", "dump", "/sdcard/ghz-window.xml")
    raw = adb("shell", "cat", "/sdcard/ghz-window.xml")
    return raw, ET.fromstring(raw)


launch = adb("shell", "am", "start", "-W", "-n", "com.mundox.ghzwatch/.MainActivity")
if b"Error" in launch:
    raise RuntimeError(launch.decode())

for attempt in range(8):
    raw, root = ui()
    button = next((node for node in root.iter("node")
                   if node.get("resource-id") == "com.mundox.ghzwatch:id/prepare"
                   and node.get("enabled") == "true"), None)
    if button is not None:
        x1, y1, x2, y2 = map(int, re.findall(r"\d+", button.get("bounds")))
        adb("shell", "input", "tap", str((x1+x2)//2), str((y1+y2)//2))
        break
else:
    raise AssertionError("The visible app did not expose its preparation button")

for attempt in range(8):
    raw, root = ui()
    status = next((node.get("text", "") for node in root.iter("node")
                   if node.get("resource-id") == "com.mundox.ghzwatch:id/status"), "")
    if "GHZ preparado" in status:
        (destination / "ui.xml").write_bytes(raw)
        (destination / "ui.png").write_bytes(adb("exec-out", "screencap", "-p"))
        print("Verified app screenshot:", status)
        break
else:
    raise AssertionError("The captured app did not reach a prepared GHZ state: " + status)
