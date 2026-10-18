#!/usr/bin/env python3
"""Exercise XASM CLI/export boundaries using only the Python standard library."""

import argparse
from pathlib import Path
import re
import subprocess
import sys
import tempfile


class Checks:
    def __init__(self, binary, directory):
        self.binary = str(binary)
        self.directory = directory
        self.count = 0
        self.serial = 0

    def require(self, condition, message):
        self.count += 1
        if not condition:
            raise AssertionError(message)

    def execute(self, source, *options):
        self.serial += 1
        path = self.directory / f"program {self.serial}.x"
        path.write_bytes(source.encode("utf-8"))
        return path, self.command("run", str(path), *options)

    def command(self, *arguments):
        # Capture bytes: universal-newline decoding would hide a CRLF export
        # that Android's canonical scene importer cannot read.
        return subprocess.run(
            [self.binary, *arguments], capture_output=True, timeout=30, check=False
        )

    def successful(self, process):
        self.require(
            process.returncode == 0 and not process.stderr,
            f"CLI failed ({process.returncode}): {process.stderr!r}",
        )

    def rejected_without_output(self, process):
        self.require(process.returncode != 0, "invalid CLI command succeeded")
        self.require(process.stdout == b"", f"failed command emitted partial output: {process.stdout!r}")
        self.require(bool(process.stderr.strip()), "failed command omitted its diagnostic")


def normalized_report(process):
    """Normal reports are text; canonical scene streams intentionally are not."""
    return process.stdout.decode("utf-8").replace("\r\n", "\n")


def exercise(checks):
    legacy = "qubits 1\nx 0\nprobz 0\nassert one\n"
    path, result = checks.execute(legacy)
    checks.successful(result)
    lines = normalized_report(result).splitlines()
    checks.require(lines[0] == f"XASM program={path}", "ordinary XASM report lost its program header")
    checks.require(
        re.fullmatch(r"qubits=1 instructions=1 state_bytes=17 ms=[0-9]+\.[0-9]{3}", lines[1])
        is not None,
        "ordinary XASM accounting format changed",
    )
    checks.require(
        lines[2:] == ["PROB_Z q=0 p0=0 p1=1", "ASSERT one PASS"],
        "legacy quantum output changed or gained visual metadata",
    )

    source = (
        "scene 1048576 -2147483648\nqubits 1\nh 0\n"
        "scene_probz 65536 0\nscene_edit 1048575 7\n"
        "h 0\nx 0\nassert one\nprobz 0\nscene_info\n"
    )
    expected = (
        b"MUNDOX_OBJECTS_V1\ncount=1048576\nseed=-2147483648\n"
        b"version=2\nedits=2\n65536:3\n1048575:7\n"
    )
    _, report = checks.execute(source)
    checks.successful(report)
    report_text = normalized_report(report)
    checks.require(
        "qubits=1 instructions=3 state_bytes=17 ms=" in report_text,
        "mixed report lost independent quantum accounting",
    )
    checks.require(
        report_text.endswith(
            "ASSERT one PASS\nPROB_Z q=0 p0=0 p1=1\n"
            "SCENE objects=1048576 edits=2 logical_rule_bytes=48 version=2\n"
            "visual_instructions=2 scene_objects=1048576 scene_logical_rule_bytes=48\n"
        ),
        "mixed report must retain quantum output and describe its final visual scene",
    )
    checks.require("MUNDOX_OBJECTS_V1" not in report_text, "normal report unexpectedly dumped a raw scene")

    _, exported = checks.execute(source, "--scene-stdout")
    checks.successful(exported)
    checks.require(
        exported.stdout == expected,
        "raw scene export contains incorrect state, diagnostics, timing, or noncanonical newlines",
    )
    _, repeated = checks.execute(source, "--scene-stdout")
    checks.successful(repeated)
    checks.require(repeated.stdout == exported.stdout, "scene stream depends on program path or timing")

    _, empty = checks.execute("scene 1 0\nscene_info\n", "--scene-stdout")
    checks.successful(empty)
    checks.require(
        empty.stdout == b"MUNDOX_OBJECTS_V1\ncount=1\nseed=0\nversion=0\nedits=0\n",
        "scene-only raw output must be a canonical scene without report text",
    )
    _, cleared = checks.execute(
        "scene 2 1\nscene_edit 1 7\nscene_info\nscene_clear\n", "--scene-stdout"
    )
    checks.successful(cleared)
    checks.require(
        cleared.stdout == b"MUNDOX_OBJECTS_V1\ncount=2\nseed=1\nversion=2\nedits=0\n",
        "scene export must represent final state after clearing, not an intermediate query",
    )

    # Parsing and evaluation complete before stdout is written. Earlier show,
    # scene_info, or successful edits may not leak a partial export on failure.
    for invalid in (
        "scene 4 0\nscene_info\nscene_edit 0 7\nscene_edit 4 1\n",
        "qubits 1\nscene 4 0\nshow\nscene_probz 0 1\n",
        "scene 4 0\nscene_edit 0 3\nunknown\n",
        "scene 4 0\nscene 4 0\n",
        "scene 4 2147483648\n",
    ):
        for options in ((), ("--scene-stdout",)):
            _, failed = checks.execute(invalid, *options)
            checks.rejected_without_output(failed)

    # Failed assertions still produce the existing diagnostic report normally,
    # but they may not export a scene as a successful computational result.
    assertion = "qubits 1\nscene 2 0\nscene_edit 1 6\nassert one\nx 0\nassert one\n"
    _, failed_report = checks.execute(assertion)
    checks.require(failed_report.returncode != 0, "a failed assertion must produce nonzero exit status")
    checks.require(
        "ASSERT one FAIL\nASSERT one PASS\n" in normalized_report(failed_report),
        "ordinary failed-assertion diagnostics were lost",
    )
    _, failed_export = checks.execute(assertion, "--scene-stdout")
    checks.rejected_without_output(failed_export)

    _, no_scene = checks.execute(legacy, "--scene-stdout")
    checks.rejected_without_output(no_scene)
    for options in ((), ("--scene-stdout",)):
        missing = checks.command("run", str(checks.directory / "missing program.x"), *options)
        checks.rejected_without_output(missing)
    for options in (("--unknown",), ("--scene-stdout", "unexpected")):
        _, malformed = checks.execute("scene 1 0\n", *options)
        checks.rejected_without_output(malformed)
    checks.rejected_without_output(checks.command("run"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=Path, help="compiled mundo_x CLI")
    arguments = parser.parse_args()
    binary = arguments.binary.resolve()
    if not binary.is_file():
        parser.error(f"binary not found: {binary}")
    with tempfile.TemporaryDirectory(prefix="mundo x visual cli ") as directory:
        checks = Checks(binary, Path(directory))
        try:
            exercise(checks)
        except (AssertionError, OSError, subprocess.SubprocessError) as error:
            print(f"FAIL after {checks.count} checks: {error}", file=sys.stderr)
            return 1
        print(f"visual CLI checks={checks.count} failures=0")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
