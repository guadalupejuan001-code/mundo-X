#!/usr/bin/env python3
"""Independently validate actual Android 8K and tiled PNG evidence.

The decoder uses the Python standard library and never materializes the 8K
RGBA image. It checks PNG chunk CRCs, the complete zlib stream, every scanline,
orientation against monolithic reference captures, and reported file hashes.
This is correctness evidence on the recorded GLES device, not a phone FPS test.
"""

from __future__ import annotations

import argparse
import binascii
import hashlib
import json
import struct
import zlib
from pathlib import Path
from cosmic_source_provenance import verify_integrated_core


def require(condition, reason):
    if not condition:
        raise ValueError(reason)


def exact(stream, count):
    data = stream.read(count)
    require(len(data) == count, "Truncated PNG")
    return data


def unfilter(data, previous, kind, channels):
    require(kind in range(5), "Invalid PNG scanline filter")
    if kind == 0:
        return data
    restored = bytearray(data)
    for index in range(len(restored)):
        left = restored[index - channels] if index >= channels else 0
        above = previous[index]
        upper_left = previous[index - channels] if index >= channels else 0
        if kind == 1:
            predictor = left
        elif kind == 2:
            predictor = above
        elif kind == 3:
            predictor = (left + above) // 2
        else:
            estimate = left + above - upper_left
            distances = (abs(estimate - left), abs(estimate - above), abs(estimate - upper_left))
            predictor = (left, above, upper_left)[distances.index(min(distances))]
        restored[index] = (restored[index] + predictor) & 255
    return bytes(restored)


def png(path, size=None, retain=False):
    """Return canonical, top-first RGBA hashes; retain pixels only for small references."""
    path = Path(path)
    compressed_hash = hashlib.sha256()
    with path.open("rb") as source:
        for piece in iter(lambda: source.read(65536), b""):
            compressed_hash.update(piece)

    width = height = channels = scanline = None
    previous = b""
    pending = bytearray()
    retained = bytearray()
    row_hash = hashlib.sha256()
    inflater = zlib.decompressobj()
    rows = decompressed_bytes = idat_chunks = compressed_bytes = opaque_pixels = 0
    samples = []
    observed_chunks = []
    idat_finished = saw_idat = False

    def output(piece):
        nonlocal previous, rows, decompressed_bytes, opaque_pixels
        decompressed_bytes += len(piece)
        require(decompressed_bytes <= height * scanline, "Extra decompressed rows or bytes")
        pending.extend(piece)
        consumed = 0
        while len(pending) - consumed >= scanline:
            require(rows < height, "Too many PNG rows")
            row = unfilter(bytes(pending[consumed + 1:consumed + scanline]), previous,
                           pending[consumed], channels)
            previous = row
            if channels == 3:
                rgba = bytearray(width * 4)
                rgba[0::4], rgba[1::4], rgba[2::4] = row[0::3], row[1::3], row[2::3]
                rgba[3::4] = b"\xff" * width
                row = bytes(rgba)
            row_hash.update(row)
            opaque_pixels += row[3::4].count(255)
            if retain:
                retained.extend(row)
            if rows in {(height - 1) * sample // 8 for sample in range(9)}:
                for column in range(9):
                    x = (width - 1) * column // 8
                    samples.append({"x": x, "y": rows, "rgba": list(row[x * 4:x * 4 + 4])})
            rows += 1
            consumed += scanline
        if consumed:
            del pending[:consumed]

    with path.open("rb") as source:
        require(exact(source, 8) == b"\x89PNG\r\n\x1a\n", "Invalid PNG signature")
        while True:
            length, kind = struct.unpack(">I4s", exact(source, 8))
            require(length <= 64 * 1024 * 1024, "Unreasonably large PNG chunk")
            require(all(65 <= char <= 90 or 97 <= char <= 122 for char in kind), "Invalid chunk name")
            observed_chunks.append(kind.decode("ascii"))
            require(width is not None or kind == b"IHDR", "IHDR must be first")
            crc = binascii.crc32(kind)
            if kind == b"IHDR":
                require(width is None and length == 13, "Invalid or duplicate IHDR")
                header = exact(source, length)
                crc = binascii.crc32(header, crc)
                width, height, depth, color, compression, filtering, interlace = struct.unpack(">IIBBBBB", header)
                require(width > 0 and height > 0 and width <= 16384 and height <= 16384,
                        "Invalid image extent")
                require(size is None or (width, height) == tuple(size), "PNG has the wrong exact dimensions")
                require(depth == 8 and color in (2, 6) and compression == filtering == interlace == 0,
                        "Expected non-interlaced 8-bit RGB/RGBA PNG")
                require(not retain or width * height * 4 <= 4 * 1024 * 1024,
                        "Reference image exceeds bounded decoder storage")
                channels = 4 if color == 6 else 3
                scanline = width * channels + 1
                previous = bytes(width * channels)
            elif kind == b"IDAT":
                require(not idat_finished, "Non-contiguous IDAT chunks")
                saw_idat = True
                idat_chunks += 1
                compressed_bytes += length
                remaining = length
                while remaining:
                    piece = exact(source, min(remaining, 65536))
                    remaining -= len(piece)
                    crc = binascii.crc32(piece, crc)
                    while piece:
                        decoded = inflater.decompress(piece, 65536)
                        output(decoded)
                        require(not inflater.unused_data, "Data follows the PNG zlib stream")
                        piece = inflater.unconsumed_tail
            else:
                if saw_idat:
                    idat_finished = True
                require(kind[0] & 32 or kind in (b"IEND", b"PLTE"), "Unknown critical PNG chunk")
                remaining = length
                while remaining:
                    piece = exact(source, min(remaining, 65536))
                    remaining -= len(piece)
                    crc = binascii.crc32(piece, crc)
            stored_crc, = struct.unpack(">I", exact(source, 4))
            require((crc & 0xffffffff) == stored_crc, "PNG chunk CRC mismatch: " + kind.decode("ascii"))
            if kind == b"IEND":
                require(length == 0 and saw_idat, "Invalid IEND or missing pixels")
                require(source.read(1) == b"", "Trailing data after IEND")
                break
    require(inflater.eof and not inflater.unused_data, "Truncated PNG zlib stream")
    require(not pending and rows == height and decompressed_bytes == height * scanline,
            "Incomplete PNG scanlines")
    result = {"width": width, "height": height, "rows": rows,
              "decompressedBytes": decompressed_bytes, "idatChunks": idat_chunks,
              "compressedBytes": compressed_bytes, "opaquePixels": opaque_pixels,
              "sha256": compressed_hash.hexdigest(), "rgbaTopFirstSha256": row_hash.hexdigest(),
              "crcVerified": True, "samples": samples}
    if retain:
        result["rgba"] = bytes(retained)
    return result


ULTRA_TESTS = {
    "ultra8k_real_framebuffer_and_budget_limits",
    "ultra8k_tiles_match_monolithic_color_and_ids",
    "ultra8k_streamed_png_dimensions_and_rows",
    "ultra8k_failure_preserves_scene_and_native_state",
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    arguments = parser.parse_args()
    directory = arguments.directory
    report = json.loads((directory / "report.json").read_text())
    metrics = report["metrics"]
    require(metrics["ultra8kSuite"] is True and metrics["uhd4kSuite"] is True, "Missing 8K/UHD instrumentation profile")
    inventory = {test["name"]: test for test in report["tests"]}
    require(report["testsRun"] == 28 and report["testsFailed"] == 0
            and len(report["tests"]) == len(inventory) == 28
            and all(test["passed"] is True for test in inventory.values()),
            "Missing shared 1000-qubit, UHD or 8K cases")
    require(ULTRA_TESTS <= inventory.keys() and all(inventory[name]["passed"] for name in ULTRA_TESTS),
            "Incomplete or failed 8K device tests")
    provenance = json.loads((directory / "build-provenance.json").read_text())
    expected_provenance = verify_integrated_core(Path(__file__).resolve().parents[1])
    require(provenance["passed"] is True
            and provenance["commit"] == (directory / "build-commit.txt").read_text().strip()
            and provenance.get("applicationId") == "com.mundox.graphics.ultra"
            and all(provenance.get(name) == value for name, value in expected_provenance.items()),
            "8K evidence does not match the integrated source inventory")
    require((metrics["ultra8kNativeWidth"], metrics["ultra8kNativeHeight"]) == (7680, 4320),
            "Real native GLES framebuffer was not 8K")
    require(metrics["ultra8kLegacyReturnChangedPixels"] == 0,
            "8K mode changed the previous view on return")
    require(metrics["ultra8kCullExactChangedPixels"] == 0,
            "Exact spatial work culling changed the reference pixels")
    baseline = metrics["ultra8kBaselineDrawMs"]
    optimized = metrics["ultra8kOptimizedDrawMs"]
    require(baseline > optimized > 0 and metrics["ultra8kSpeedup"] > 1.0,
            "8K optimization did not improve synchronized completion time")
    require(metrics["ultra8kFullPixels"] == 7680 * 4320
            and 0 < metrics["ultra8kShadedPixels"] < metrics["ultra8kFullPixels"]
            and 0 < metrics["ultra8kFragmentCoverage"] < 1,
            "8K spatial work accounting is invalid")
    require(abs(metrics["ultra8kEquivalentCompletedFpsBefore"] - 1000.0 / baseline) < 1e-9
            and abs(metrics["ultra8kEquivalentCompletedFpsAfter"] - 1000.0 / optimized) < 1e-9,
            "8K equivalent completed-frame rate does not match synchronized timing")
    infinite = metrics["ultra8kInfinite"]
    require(infinite["domain"] == "∞" and infinite["addressDigits"] == 101
            and (infinite["width"], infinite["height"]) == (7680, 4320)
            and infinite["readOnlyBackgroundDraw"] is True
            and infinite["activeBefore"] == infinite["activeAfterCompact"] == 0
            and infinite["changedRevisionObserved"] is True
            and infinite["initialDrawMs"] > 0 and infinite["changedDrawMs"] > 0,
            "8K framebuffer was not verified with the rule-defined InfiniteCore owner")

    comparisons = metrics["ultra8kDifferential"]
    require(len(comparisons) == 4 and {(item["width"], item["height"], item["ids"]) for item in comparisons}
            == {(321, 181, False), (321, 181, True), (181, 321, False), (181, 321, True)},
            "Missing odd-size color/ID comparisons in both orientations")
    verified = {}
    for item in comparisons:
        expected_size = (item["width"], item["height"])
        tiled = png(directory / item["file"], expected_size, retain=True)
        reference = png(directory / item["reference"], expected_size, retain=True)
        require(tiled.pop("rgba") == reference.pop("rgba"), "Tiled pixels differ from independent monolithic capture")
        require(item["differentPixels"] == 0 and item["seamDifferentPixels"] == 0,
                "Android differential check disagrees with exported pixels")
        require(item["tileWidth"] < item["width"] and item["tileRows"] < item["height"],
                "Differential test did not cross tile and strip boundaries")
        verified[item["file"]] = tiled

    export = metrics["ultra8kStreamed"]
    require((export["width"], export["height"]) == (7680, 4320), "8K streaming export used another resolution")
    require(0 < export["workingBytes"] < 16 * 1024 * 1024 and export["tiles"] > 100,
            "8K export did not report a bounded tiled working set")
    large = png(directory / export["file"], (7680, 4320))
    require(large["sha256"] == export["sha256"], "8K PNG hash changed after device export")
    require(large["opaquePixels"] == 7680 * 4320 and large["rows"] == 4320,
            "PNG lost opaque pixels or complete rows")
    require(len({tuple(point["rgba"]) for point in large["samples"]}) > 1,
            "8K file is a blank constant-color image")
    verified[export["file"]] = large
    failures = metrics["ultra8kFailure"]
    require(failures["invalidRejected"] >= 3 and failures["cancelledBeforeWrite"] is True
            and failures["cancelledDuringExport"] is True and failures["ioFailurePropagated"] is True
            and failures["stateUnchanged"] is True and failures["selectionUnchanged"] is True
            and failures["recoveredChangedPixels"] == 0 and failures["partialFileRemoved"] is True,
            "Failed or cancelled export did not preserve its active scene")
    result = {"passed": True, "scope": "Actual recorded GLES device; no physical-phone performance claim",
              "baselineDrawMs": baseline, "optimizedDrawMs": optimized,
              "speedup": metrics["ultra8kSpeedup"], "fragmentCoverage": metrics["ultra8kFragmentCoverage"],
              "images": verified}
    (directory / "ultra-verified.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({"passed": True, "pngs": len(verified), "real8kPixels": 7680 * 4320,
                      "exactDifferentialCases": len(comparisons)}))


if __name__ == "__main__":
    main()
