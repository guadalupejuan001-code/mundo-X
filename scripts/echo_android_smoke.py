"""Exercise the real Android UI/JNI path on a connected test emulator.

Run from the repository root after building dist/Mundo-X-Ecos-debug.apk.
Screenshots and native/UI diagnostics are collected even if a check fails.
"""

import json
from pathlib import Path
import re
import subprocess
import time
import xml.etree.ElementTree as ET


DIST = Path("dist")
PACKAGE = "com.mundox.echoes"
COMPONENT = PACKAGE + "/com.mundox.echoes.MainActivity"
WIDTH, HEIGHT = 1080, 1920
OUTCOME = {"status": "running", "package": PACKAGE, "steps": []}


def adb(*args, timeout=30):
    result = subprocess.run(
        ["adb", *args], text=True, capture_output=True, timeout=timeout, check=True
    )
    return result.stdout


def dump_ui():
    adb("shell", "uiautomator", "dump", "/sdcard/quantum-echo-window.xml")
    xml = adb("exec-out", "cat", "/sdcard/quantum-echo-window.xml")
    DIST.joinpath("android-ui.xml").write_text(xml, encoding="utf-8")
    return ET.fromstring(xml)


def bounds(node):
    return list(map(int, re.findall(r"\d+", node.get("bounds", ""))))


def wait_node(description, contains=None, enabled=False, seconds=90):
    deadline = time.monotonic() + seconds
    last_problem = ""
    missing = 0
    while time.monotonic() < deadline:
        visible_target = False
        try:
            tree = dump_ui()
            for node in tree.iter("node"):
                if node.get("content-desc") != description:
                    continue
                coordinates = bounds(node)
                if len(coordinates) != 4:
                    continue
                x1, y1, x2, y2 = coordinates
                if not (
                    x2 > x1
                    and y2 > y1
                    and 0 <= (x1 + x2) // 2 < WIDTH
                    and 0 <= (y1 + y2) // 2 < HEIGHT
                ):
                    continue
                visible_target = True
                if enabled and node.get("enabled") != "true":
                    continue
                if contains is not None and contains not in node.get("text", ""):
                    continue
                return node
        except (subprocess.SubprocessError, ET.ParseError) as exc:
            last_problem = str(exc)
        if not visible_target:
            # Search both directions; the app scrolls to results after running.
            upward = (missing // 5) % 2 == 1
            y_from, y_to = int(HEIGHT * 0.78), int(HEIGHT * 0.28)
            if upward:
                y_from, y_to = y_to, y_from
            adb(
                "shell", "input", "swipe", str(WIDTH // 2), str(y_from),
                str(WIDTH // 2), str(y_to), "300"
            )
            missing += 1
        time.sleep(1)
    raise AssertionError(
        f"UI node not ready: {description}; required text={contains!r}; {last_problem}"
    )


def tap(node):
    coordinates = bounds(node)
    assert len(coordinates) == 4, node.attrib
    x1, y1, x2, y2 = coordinates
    assert x2 > x1 and y2 > y1, node.attrib
    adb("shell", "input", "tap", str((x1 + x2) // 2), str((y1 + y2) // 2))


def capture(name):
    screenshot = subprocess.check_output(
        ["adb", "exec-out", "screencap", "-p"], timeout=30
    )
    DIST.joinpath(name + ".png").write_bytes(screenshot)
    tree = dump_ui()
    DIST.joinpath(name + ".xml").write_bytes(ET.tostring(tree, encoding="utf-8"))


def launch():
    result = adb("shell", "am", "start", "-W", "-n", COMPONENT)
    assert "Status: ok" in result, result
    wait_node("Ejecutar eco", enabled=True)
    assert adb("shell", "pidof", PACKAGE).strip(), "App process is not running"
    return result


def main():
    global WIDTH, HEIGHT
    DIST.mkdir(parents=True, exist_ok=True)
    try:
        OUTCOME["device"] = {
            "sdk": adb("shell", "getprop", "ro.build.version.sdk").strip(),
            "abi": adb("shell", "getprop", "ro.product.cpu.abi").strip(),
            "page_size": adb("shell", "getconf", "PAGE_SIZE").strip(),
        }
        adb("logcat", "-c")
        OUTCOME["install"] = adb("install", "-r", "dist/Mundo-X-Ecos-debug.apk")
        sizes = re.findall(r"(\d+)x(\d+)", adb("shell", "wm", "size"))
        if sizes:
            WIDTH, HEIGHT = map(int, sizes[-1])
        adb("shell", "input", "keyevent", "KEYCODE_WAKEUP")
        adb("shell", "wm", "dismiss-keyguard")
        adb("shell", "am", "force-stop", PACKAGE)
        OUTCOME["launch"] = launch()
        OUTCOME["steps"].append("Installed and launched the standalone app")
        capture("android-launch")

        tap(wait_node("Ejecutar eco", enabled=True))
        result = wait_node("Resultado del eco", contains="VERIFICADO", seconds=180)
        OUTCOME["echo"] = {"title": result.get("text", "")}
        OUTCOME["echo"]["detail"] = wait_node("Detalle del resultado").get("text", "")
        OUTCOME["echo"]["signal_z"] = wait_node("Señal Z").get("text", "")
        # Version-one defaults have an independently checked signal of -1/2.
        signal = float(OUTCOME["echo"]["signal_z"])
        assert abs(signal + 0.5) <= 1e-6, f"Unexpected default echo signal: {signal}"
        OUTCOME["steps"].append("Ran the default experiment through Java/JNI with dense verification")
        capture("android-echo-verified")

        tap(wait_node("Comprobar simulación", enabled=True))
        result = wait_node("Resultado del eco", contains="PRUEBAS CORRECTAS", seconds=180)
        OUTCOME["self_tests"] = {
            "title": result.get("text", ""),
            "detail": wait_node("Detalle del resultado").get("text", ""),
        }
        assert "13 de 13 pruebas correctas." in OUTCOME["self_tests"]["detail"], OUTCOME["self_tests"]
        OUTCOME["steps"].append("Ran native self-tests through Java/JNI")
        capture("android-self-tests")

        adb("shell", "am", "force-stop", PACKAGE)
        OUTCOME["relaunch"] = launch()
        OUTCOME["steps"].append("Force-stopped and relaunched the app successfully")
        capture("android-relaunch")
        OUTCOME["status"] = "passed"
    except BaseException as exc:
        OUTCOME["status"] = "failed"
        OUTCOME["error"] = str(exc)
        raise
    finally:
        DIST.joinpath("android-smoke.json").write_text(
            json.dumps(OUTCOME, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
        )
        try:
            DIST.joinpath("android-logcat.txt").write_text(
                adb("logcat", "-d", "-v", "threadtime"), encoding="utf-8"
            )
            capture("android-smoke-final")
        except Exception as exc:
            DIST.joinpath("android-capture-error.txt").write_text(str(exc), encoding="utf-8")
        print(json.dumps(OUTCOME, ensure_ascii=False))


if __name__ == "__main__":
    main()
