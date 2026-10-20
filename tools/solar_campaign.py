#!/usr/bin/env python3
"""Sequential Solar CPU/GLES campaign, with per-process RAM/CPU/wall budgets.
Build classes and the real JNI first with solar_host_checks.sh. Do not run an
emulator or compiler concurrently. A limit/failure is recorded, then the next
independent case is attempted. Software rendering is not device FPS.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import resource
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]


def limits():
    resource.setrlimit(resource.RLIMIT_AS, (2 * 1024**3, 2 * 1024**3))
    resource.setrlimit(resource.RLIMIT_CPU, (180, 181))
    os.sched_setaffinity(0, {min(os.sched_getaffinity(0))})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--java', default='java')
    parser.add_argument('--classes', type=Path, default=Path('build/solar-host/classes'))
    parser.add_argument('--jni', type=Path, default=Path('build/solar-host/jni'))
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, OPENBLAS_NUM_THREADS='1', LP_NUM_THREADS='1', OMP_NUM_THREADS='1')
    common = [args.java, '-Xmx256m', '-Xms32m', '-XX:+UseSerialGC',
              '-XX:ActiveProcessorCount=1', '-XX:CompressedClassSpaceSize=64m',
              '-XX:ReservedCodeCacheSize=64m', '-Djava.library.path=' + str(args.jni.resolve()),
              '-cp', str(args.classes.resolve())]
    paths = ['solar-android/app/src/main/assets/shaders/solar.frag',
             'solar-android/app/src/main/java/com/mundox/solar/SolarData.java',
             'solar-android/tests/SolarOcclusion.java',
             'solar-android/tests/SolarRenderFixtures.java',
             'tools/solar_shader_check.py', 'tools/solar_campaign.py']
    hashes = lambda: {p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in paths}
    report = {'source_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
              'source_sha256': hashes(), 'platform': platform.platform(), 'python': sys.version,
              'affinity_cpu': min(os.sched_getaffinity(0)), 'ram_budget_bytes': 2 * 1024**3,
              'cpu_budget_seconds': 180, 'wall_budget_seconds': 210, 'sequential': True, 'cases': []}
    start = time.monotonic()
    def save():
        report['elapsed_seconds'] = time.monotonic() - start
        (out / 'campaign-status.json').write_text(json.dumps(report, indent=2) + '\n')
    def run(command, log):
        begin = time.monotonic()
        with open(out / log, 'w') as stream:
            try:
                result = subprocess.run(command, cwd=ROOT, env=env, stdout=stream,
                                        stderr=subprocess.STDOUT, preexec_fn=limits, timeout=210)
                code = result.returncode
            except subprocess.TimeoutExpired:
                code = 'wall_timeout_210s'
        return {'returncode': code, 'wall_seconds': time.monotonic() - begin}
    report['ephemeris'] = run(common + ['com.mundox.solar.SolarOptimizationChecks', 'bench'], 'ephemeris-benchmark.jsonl')
    save()
    print('Ephemeris:', report['ephemeris'], flush=True)
    for width, height, scenes, repeats in [(320, 180, [], 5), (961, 541, [], 3),
            (1920, 1080, [0, 4, 7, 24, 26, 29], 3), (3840, 2160, [4, 24], 2),
            (7680, 4320, [4], 1)]:
        fixture = out / f'fixtures-native-{width}.json'
        item = {'resolution': [width, height], 'repeats': repeats, 'scenes': scenes or 'all 32'}
        item['fixture'] = run(common + ['com.mundox.solar.SolarRenderFixtures', str(width), str(height), str(fixture)],
                              f'fixtures-{width}.log')
        if item['fixture']['returncode'] == 0:
            command = [sys.executable, str(ROOT / 'tools/solar_shader_check.py'), '--fixtures', str(fixture),
                       '--repeats', str(repeats), '--output', str(out / f'shader-{width}.json')]
            if scenes:
                command += ['--scenes', *map(str, scenes)]
            item.update(run(command, f'shader-{width}.log'))
        else:
            item['returncode'] = 'fixture_failed'
        report['cases'].append(item)
        save()
        print(json.dumps(item), flush=True)
    report['sources_unchanged'] = hashes() == report['source_sha256']
    report['passed'] = report['sources_unchanged'] and report['ephemeris']['returncode'] == 0 and all(c['returncode'] == 0 for c in report['cases'])
    save()
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
