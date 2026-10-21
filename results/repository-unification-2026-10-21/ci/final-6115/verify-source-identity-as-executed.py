#!/usr/bin/env python3
"""Compare a published fixture-only successor to b9ec, without builds or tests.

Without --after, write a pending manifest identifying the fixed baseline.
With --after FULL_SHA, compare all non-documentation/evidence tracked inputs.
The only permitted executable changes are the Solar 8K workflow and its runner.
"""
import argparse
import collections
import datetime
import hashlib
import io
import json
import re
import subprocess
from pathlib import Path

ROOT = Path('/workspace/scratch/d0e1d0a0e3a1')
REPO = ROOT / 'mundo-X'
WORK = ROOT / 'closure/unification'
BASE = 'b9ec7452307c610448adbb1217e1566aa88fa79b'
ALLOWED = {'.github/workflows/solar-8k.yml', 'tools/solar-emulator-test.sh'}
PRODUCT_PREFIXES = ('native/', 'src/', 'app/', 'android-experiments/', 'graphics-android/',
                    'graphics-desktop/', 'pc-android/', 'cosmic-android/', 'solar-android/',
                    'tests/', 'experiments/', 'examples/')


def sha(data):
    return hashlib.sha256(data).hexdigest()


def git(*args):
    return subprocess.check_output(['git', *args], cwd=REPO)


def inventory(commit):
    result = {}
    for raw in git('ls-tree', '-r', '-z', commit).split(b'\0'):
        if raw:
            metadata, path = raw.split(b'\t', 1)
            mode, kind, blob = metadata.decode().split()
            result[path.decode()] = {'mode': mode, 'kind': kind, 'git_blob': blob}
    return result


def category(path):
    if path.startswith('results/'):
        return 'evidence'
    if path.startswith('docs/') or path.lower().endswith(('.md', '.rst')):
        return 'documentation'
    if path in ALLOWED:
        return 'allowed_solar_8k_fixture'
    if path.startswith(PRODUCT_PREFIXES) or '/' not in path and path != '.gitignore':
        return 'product_build_test'
    return 'other_protected_validation_or_configuration'


def contents_for(trees, paths):
    objects = sorted({tree[path]['git_blob'] for tree in trees for path in paths
                      if path in tree and tree[path]['kind'] == 'blob'})
    data = subprocess.run(['git', 'cat-file', '--batch'], cwd=REPO,
                          input=('\n'.join(objects) + '\n').encode(), stdout=subprocess.PIPE, check=True).stdout
    stream, result = io.BytesIO(data), {}
    for expected in objects:
        blob, kind, length = stream.readline().decode().strip().split()
        assert blob == expected and kind == 'blob'
        payload = stream.read(int(length))
        assert len(payload) == int(length) and stream.read(1) == b'\n'
        result[blob] = {'sha256': sha(payload), 'bytes': len(payload)}
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--after', help='Full published successor SHA, supplied by root')
    parser.add_argument('--output', type=Path, default=WORK / 'solar-8k-fixture-source-identity.json')
    args = parser.parse_args()
    if args.after and not re.fullmatch('[0-9a-f]{40}', args.after):
        parser.error('--after must be the full 40-character commit SHA')
    before = inventory(BASE)
    after = inventory(args.after) if args.after else None
    paths = sorted(before.keys() | after.keys()) if after is not None else sorted(before)
    protected = [path for path in paths if category(path) not in ('documentation', 'evidence', 'allowed_solar_8k_fixture')]
    changed = [path for path in paths if after is not None and before.get(path) != after.get(path)]
    metas = contents_for([before, after] if after is not None else [before], set(protected) | set(changed) | ALLOWED)

    def meta(tree, path):
        return dict(tree[path], **metas.get(tree[path]['git_blob'], {})) if path in tree else None

    def digest(tree, selected):
        return sha(json.dumps({path: meta(tree, path) for path in selected if path in tree}, sort_keys=True).encode())

    products = [path for path in protected if category(path) == 'product_build_test']
    report = {
        'schema_version': 1, 'created_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'status': 'compared' if after is not None else 'awaiting_published_successor_sha',
        'scope': 'Read-only source/APK identity; no builds, tests, CI polls, staging, commits or remote writes',
        'baseline': {'commit': BASE, 'tree': git('rev-parse', BASE + '^{tree}').decode().strip()},
        'successor': {'commit': args.after, 'tree': git('rev-parse', args.after + '^{tree}').decode().strip()} if args.after else None,
        'method': 'Exact tracked path/mode/type/blob comparison plus SHA256 of every protected input; docs/results listed separately',
        'allowed_executable_changes': sorted(ALLOWED),
        'protected_input_count': len(protected), 'product_build_test_count': len(products),
        'baseline_protected_manifest_sha256': digest(before, protected),
        'baseline_product_build_test_manifest_sha256': digest(before, products),
        'historical_ci_statement': {'reported_by_parent': '17 campaigns SUCCESS at b9ec; Solar 8K failed twice due to its runtime fixture',
                                    'independently_polled_by_this_script': False},
        'attribution_limits': [
            'Identical inputs preserve attribution of historical results to the tested product/test bytes; those runs remain executions of b9ec.',
            'The AOSP 8K environment and effective-memory recording need their own runtime evidence; this audit cannot certify runtime success.',
            'The shared runner changes diagnostic collection as well as the 8K image. Unchanged product tests do not mean that the new diagnostic code ran in the previous 17 campaigns.',
            'No APK is rebuilt or re-signed by this script; APK identity is checked against the existing verified files.'
        ],
        'all_identity_assertions_passed': None,
    }
    if after is not None:
        ancestor = subprocess.run(['git', 'merge-base', '--is-ancestor', BASE, args.after], cwd=REPO).returncode == 0
        protected_changes = [path for path in changed if path in protected]
        product_changes = [path for path in protected_changes if category(path) == 'product_build_test']
        report.update(baseline_is_ancestor=ancestor,
                      changed_files=[{'path': path, 'category': category(path), 'before': meta(before, path), 'after': meta(after, path)} for path in changed],
                      changed_category_counts=dict(collections.Counter(category(path) for path in changed)),
                      protected_input_changes=protected_changes, product_build_test_changes=product_changes,
                      successor_protected_manifest_sha256=digest(after, protected),
                      successor_product_build_test_manifest_sha256=digest(after, products))
        runner = 'tools/solar-emulator-test.sh'
        old_text, new_text = (git('show', commit + ':' + runner).decode() for commit in (BASE, args.after))
        assignments = r'^\s*(?:solar_profile|solar_package|solar_apk|solar_tests|solar_instrument_timeout|solar_instrument_args|solar_verify_args)=.*$'
        selected = lambda text: [line.strip() for line in re.findall(assignments, text, re.M)]
        command = lambda text: [line.strip() for line in text.replace('\\\n', ' ').splitlines() if ' shell am instrument -w -r ' in line]
        branch = re.search(r'if \[\[ "\$solar_8k" == true \]\]; then\n(.*?)\nelif \[\[ "\$solar_4k" == true \]\]; then', new_text, re.S)
        profile_checks = {
            'test_selection_assignments_unchanged': selected(old_text) == selected(new_text),
            'instrumentation_command_unchanged': command(old_text) == command(new_text) and bool(command(old_text)),
            'aosp_image_assignment_inside_8k_branch': bool(branch and "solar_system_image='system-images;android-35;default;x86_64'" in branch.group(1)),
            'normal_4k_google_apis_default_retained': "solar_system_image='system-images;android-35;google_apis;x86_64'" in new_text[:new_text.find('if [[ "$solar_8k"')],
            'requested_ram_2048_cores_2_retained': 'solar_requested_ram_mib=2048' in new_text and 'solar_requested_cores=2' in new_text,
            'limitation': 'Static checks of selection/arguments only; not shell execution or a memory-cap measurement',
        }
        report['shared_runner_static_contract'] = profile_checks
        apk_records = {}
        for variant, name in (('candidate-allbody', 'COSMIC-X-Solar-1.3.0.apk'), ('candidate-review-allbody', 'COSMIC-X-Solar-Pruebas-1.3.0.apk')):
            source = json.loads((WORK / 'solar' / (variant + '-source-manifest.json')).read_text())
            verification = json.loads((WORK / 'solar' / (variant + '-verification.json')).read_text())
            mismatches = [path for path, expected in source['source_sha256'].items()
                          if path not in after or meta(after, path)['sha256'] != expected]
            apk = WORK / 'solar/apks' / name
            actual = sha(apk.read_bytes())
            apk_records[variant] = {'source_count': len(source['source_sha256']), 'source_mismatches': mismatches,
                                    'path': str(apk), 'sha256': actual,
                                    'unchanged_from_verified_apk': actual == verification['apks']['debug']['sha256']}
        report['existing_apks'] = apk_records
        fixture_diff = args.output.with_suffix('.fixture.diff')
        fixture_diff.write_bytes(git('diff', BASE, args.after, '--', *sorted(ALLOWED)))
        report['fixture_diff'] = {'path': str(fixture_diff), 'sha256': sha(fixture_diff.read_bytes())}
        passed = (ancestor and not protected_changes and bool(set(changed) & ALLOWED)
                  and all(value for key, value in profile_checks.items() if key != 'limitation')
                  and all(not row['source_mismatches'] and row['unchanged_from_verified_apk'] for row in apk_records.values()))
        report['all_identity_assertions_passed'] = passed
    args.output.write_text(json.dumps(report, indent=2, ensure_ascii=False) + '\n')
    print(json.dumps({'path': str(args.output), 'sha256': sha(args.output.read_bytes()), 'status': report['status'],
                      'protected_inputs': len(protected), 'product_build_test_files': len(products),
                      'passed': report['all_identity_assertions_passed']}, indent=2))
    if report['all_identity_assertions_passed'] is False:
        raise SystemExit('Unexpected source/fixture change; inspect the manifest')


if __name__ == '__main__':
    main()
