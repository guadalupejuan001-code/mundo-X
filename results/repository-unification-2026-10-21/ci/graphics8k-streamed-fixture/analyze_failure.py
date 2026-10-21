#!/usr/bin/env python3
"""Diagnose the saved Android failure using image bytes and independent ray/box intersections.

This reads an existing CI artifact. It does not run GLES, an emulator or a benchmark.
The 64 offsets cover every source-pixel position in an 8x8 preview sampling block.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import resource
import shutil

os.environ.setdefault('OPENBLAS_NUM_THREADS', '1')
resource.setrlimit(resource.RLIMIT_AS, (2 * 1024**3, 2 * 1024**3))
import numpy as np
from PIL import Image


def analyze(artifact):
    source = artifact / 'ultra-8k-streamed.png'
    with Image.open(source) as image:
        assert image.mode == 'RGBA' and image.size == (7680, 4320)
        pixels = np.asarray(image)
        background = pixels[0, 0].copy()
        mask = np.any(pixels != background, axis=2)
        histogram = image.getcolors(maxcolors=32)
        assert histogram is not None
    ys, xs = np.nonzero(mask)
    report = {
        'scope': 'Saved Android PNG plus independent CPU ray/box geometry; no new Android execution',
        'run_id': 39523264180, 'job_id': 118972460953, 'artifact_id': 12311638267,
        'head_sha': (artifact / 'build-commit.txt').read_text().strip(),
        'source_png_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
        'width': 7680, 'height': 4320, 'background_rgba': background.tolist(),
        'different_pixels_from_background': int(mask.sum()),
        'geometry_bbox_exclusive': [int(xs.min()), int(ys.min()), int(xs.max()) + 1, int(ys.max()) + 1],
        'colors': [{'rgba': list(color), 'pixels': count} for count, color in sorted(histogram)],
        'original_assert': 'different > 100', 'preview': [960, 540], 'sample_stride': 7,
        'camera_eye': [5, 4, 7], 'camera_target': [0, 1, 0], 'camera_fov_degrees': 48,
        'camera_extent': [7680, 4320], 'time': .875, 'quantum_strength': .25,
        'first_observable_after_Z0': -1,
        'derivation': 'scene resets graph state; Z0 anticommutes with its first graph stabilizer; export computes its camera from the requested image extent, not the UI surface',
    }
    del pixels, xs, ys
    eye = np.array(report['camera_eye'], dtype=np.float64)
    target = np.array(report['camera_target'], dtype=np.float64)
    forward = (target - eye) / np.linalg.norm(target - eye)
    right = np.cross(forward, [0, 1, 0]); right /= np.linalg.norm(right)
    up = np.cross(right, forward)
    angle = report['time'] * .12
    c, s = math.cos(angle), math.sin(angle)
    rotation = np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]])
    local_eye = eye @ rotation
    y, x = np.meshgrid(np.arange(0, 540, 7), np.arange(0, 960, 7), indexing='ij')
    cases = []
    for seed in [2026, 2027]:
        hashed = seed ^ 0x9e3779b9
        hashed ^= hashed >> 16; hashed = hashed * 0x7feb352d & 0xffffffff
        hashed ^= hashed >> 15; hashed = hashed * 0x846ca68b & 0xffffffff
        hashed ^= hashed >> 16
        base_height = .7 + (hashed & 255) / 128
        height = base_height * (1 + .08 * math.sin(report['time'] * .8)) * .75
        samples = []
        for oy in range(8):
            for ox in range(8):
                ndcx = 2 * (x * 8 + ox + .5) / 7680 - 1
                ndcy = 1 - 2 * (y * 8 + oy + .5) / 4320
                ray = forward + ndcx[..., None] * (7680 / 4320) * math.tan(math.radians(24)) * right
                ray = ray + ndcy[..., None] * math.tan(math.radians(24)) * up
                ray /= np.linalg.norm(ray, axis=-1)[..., None]
                local_ray = ray @ rotation
                a = (np.array([-.38, 0, -.38]) - local_eye) / local_ray
                b = (np.array([.38, height, .38]) - local_eye) / local_ray
                enter, leave = np.minimum(a, b).max(axis=-1), np.maximum(a, b).min(axis=-1)
                hit = leave >= np.maximum(enter, .1)
                observed = mask[oy::56, ox::56]
                samples.append({'offset': [ox, oy], 'predicted_geometry': int(hit.sum()),
                                'observed_geometry': int(observed.sum()),
                                'mask_differences': int(np.count_nonzero(hit != observed))})
        counts = [item['predicted_geometry'] for item in samples]
        cases.append({'seed': seed, 'hash_u32': hashed, 'height': height, 'base_height': base_height,
                      'predicted_sample_count_range': [min(counts), max(counts)],
                      'all_offsets_above_unchanged_threshold': min(counts) > 100,
                      'samples': samples})
    report['seeds'] = cases
    report['conclusion'] = 'The inherited seed 2027 explains the small valid image; canonical seed 2026 clears the unchanged threshold for all 64 pixel sampling offsets.'
    report['rss_peak_kib'] = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
    assert report['different_pixels_from_background'] == 234465
    assert cases[0]['all_offsets_above_unchanged_threshold']
    assert not cases[1]['all_offsets_above_unchanged_threshold']
    assert max(item['mask_differences'] for item in cases[1]['samples']) == 0
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--artifact', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = analyze(args.artifact)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    for name in ['ultra-8k-streamed.png', 'failure-ultra8k_streamed_png_dimensions_and_rows.png']:
        shutil.copy2(args.artifact / name, args.output.parent / ('2be39f7-' + name))
    print(json.dumps({key: value for key, value in result.items() if key != 'seeds'}, indent=2))
    print(json.dumps([{key: value for key, value in case.items() if key != 'samples'} for case in result['seeds']], indent=2))
