#!/usr/bin/env python3
"""Compare lossless Solar replays compiled in separate bounded-memory processes.

Require matching fixtures/driver configuration and exact internal repetitions
before comparing every RGBA byte. Warmup evidence is retained, not discarded.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import sys

os.environ.setdefault('OPENBLAS_NUM_THREADS', '1')


def compare(before, after, before_captures, after_captures):
    import numpy as np
    from PIL import Image

    for key in ['fixtures_sha256', 'driver', 'resolution', 'warmup_frames',
                'swiftshader_threads', 'driver_configuration']:
        if key not in before or key not in after or before[key] != after[key]:
            raise ValueError('Replay inputs/environment differ or are missing: ' + key)
    for label, data in [('before', before), ('after', after)]:
        if not data['results'] or data['independent_programs']:
            raise ValueError(label + ' must contain completed single-program replay scenes')
        if set(data['repeat_different_pixels']) != {'before', 'after'}:
            raise ValueError(label + ' has incomplete repeated-capture evidence')
        if any(data['repeat_different_pixels'].values()) or any(data['different_pixels'].values()):
            raise ValueError(label + ' is not internally exact after its recorded warmup')
    scene_ids = lambda data: [item['scene'] for item in data['results']]
    if scene_ids(before) != scene_ids(after) or len(set(scene_ids(before))) != len(scene_ids(before)):
        raise ValueError('Missing, reordered or duplicated replay scenes')
    width, height = before['resolution']
    cases = []
    for old, new in zip(before['results'], after['results']):
        frames = []
        for root, item in [(before_captures, old), (after_captures, new)]:
            frame = root / f"scene{item['scene']:02d}-after-{len(item['samples_ms']['after'])}.png"
            with Image.open(frame) as image:
                if image.mode != 'RGBA' or image.size != (width, height):
                    raise ValueError('Capture format/dimensions do not match the replay: ' + str(frame))
                frames.append(np.array(image))
        x, y = frames
        delta = np.abs(x.astype(np.int16) - y.astype(np.int16))
        cases.append({'scene': old['scene'], 'pixels': width * height,
                      'different_pixels': int(np.any(delta != 0, axis=2).sum()),
                      'different_channels': int(np.count_nonzero(delta)),
                      'maximum_channel_delta': int(delta.max()),
                      'mean_absolute_channel_error': float(delta.mean()),
                      'before_rgba_sha256': hashlib.sha256(x.tobytes()).hexdigest(),
                      'after_rgba_sha256': hashlib.sha256(y.tobytes()).hexdigest()})
    changed = sum(case['different_pixels'] for case in cases)
    return {'status': 'passed' if changed == 0 else 'failed', 'pixel_tolerance': 0,
            'scope': 'Optimization equivalence after correcting filtering; independent single-program processes',
            'compared_pixels': sum(case['pixels'] for case in cases), 'different_pixels': changed,
            'exact_scenes': sum(case['different_pixels'] == 0 for case in cases), 'cases': cases,
            'before_shader_sha256': before['shader_sha256'], 'after_shader_sha256': after['shader_sha256'],
            'fixtures_sha256': before['fixtures_sha256'], 'driver': before['driver'],
            'swiftshader_threads': before['swiftshader_threads'],
            'driver_configuration': before['driver_configuration'],
            'warmup_frames': before['warmup_frames'],
            'warmup_different_pixels': {label: data['warmup_different_pixels'] for label, data in [('before', before), ('after', after)]},
            'rss_peak_kib': {label: data['rss_peak_kib'] for label, data in [('before', before), ('after', after)]}}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for flag in ['before', 'after', 'before-captures', 'after-captures', 'output']:
        parser.add_argument('--' + flag, type=Path, required=True)
    args = parser.parse_args()
    try:
        result = compare(json.loads(args.before.read_text()), json.loads(args.after.read_text()),
                         args.before_captures, args.after_captures)
    except (OSError, ValueError, KeyError) as error:
        result = {'status': 'blocked_or_failed', 'error': str(error), 'pixel_tolerance': 0}
    result['report_sha256'] = {label: hashlib.sha256(path.read_bytes()).hexdigest()
                               for label, path in [('before', args.before), ('after', args.after)] if path.exists()}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({key: value for key, value in result.items() if key != 'cases'}))
    sys.exit(0 if result['status'] == 'passed' else 1)
