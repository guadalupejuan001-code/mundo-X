#!/usr/bin/env python3
"""Compare mixed Ether evaluation with its immutable compact-rule baseline."""
import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import statistics
import subprocess
import tarfile
import tempfile

from ether_rule_compaction_check import BENCH as CONTRACT_SOURCE, DIFFERENTIAL, run_bounded

BASE = 'f096930749a8945ebb85678ed61f8273e01a0ad7'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--repeats', type=int, default=5)
    args = parser.parse_args()
    if args.repeats < 1:
        parser.error('positive repetitions required')
    root = Path(__file__).resolve().parents[1]
    paths = ['native/include/mundo_x/ether_core.hpp', 'native/include/mundo_x/infinite_core.hpp', 'native/tests/ether_rules.cpp',
             'tools/ether_evaluation_benchmark.cpp']
    evidence = {'baseline': BASE, 'seed': 42, 'repeats': args.repeats,
                'cpu_affinity': [min(os.sched_getaffinity(0))],
                'memory_budget': '2 GiB RLIMIT_AS for each child; host CPU, not Android',
                'time_budget': '28 CPU seconds; 30 wall seconds per child',
                'compiler': subprocess.check_output(['g++', '--version'], text=True).splitlines()[0],
                'flags': ['-std=c++17', '-O3', '-DNDEBUG', '-pthread'],
                'source_sha256': {p: hashlib.sha256((root/p).read_bytes()).hexdigest() for p in paths},
                'cases': [], 'frontiers': []}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    def save():
        args.output.write_text(json.dumps(evidence, indent=2)+'\n')
    with tempfile.TemporaryDirectory(prefix='ether-evaluation-') as directory:
        d = Path(directory)
        archive = subprocess.check_output(['git', 'archive', BASE, 'native/include'], cwd=root)
        with tarfile.open(fileobj=io.BytesIO(archive)) as tar:
            tar.extractall(d/'baseline', filter='data')
        includes = {'before': d/'baseline/native/include', 'after': root/'native/include'}
        def compile_source(source, name):
            binaries = {}
            for label, include in includes.items():
                binary = d/(name+'-'+label)
                subprocess.run(['g++', *evidence['flags'], '-I', str(include), str(source), '-o', str(binary)], check=True)
                binaries[label] = binary
            return binaries
        bins = compile_source(root/'tools/ether_evaluation_benchmark.cpp', 'bench')
        (d/'contract.cpp').write_text(CONTRACT_SOURCE)
        contracts = compile_source(d/'contract.cpp', 'contract')
        evidence['contracts'] = []
        for depth in [0, 8, 31, 64, 4096]:
            answers = [run_bounded(contracts[label], ['contract', depth], d)[0] for label in includes]
            if answers[0] != answers[1]:
                raise AssertionError(f'state/RNG/measurement differs at {depth}')
            evidence['contracts'].append({'depth': depth, 'identical_bytes': len(answers[0]),
                                          'sha256': hashlib.sha256(answers[0].encode()).hexdigest()})
        old = (includes['before']/'mundo_x/ether_core.hpp').read_text()
        (d/'ether_reference.hpp').write_text(old.replace('EtherCore', 'EtherReference'))
        (d/'differential.cpp').write_text(DIFFERENTIAL)
        subprocess.run(['g++', *evidence['flags'], '-I', str(includes['after']),
                        str(d/'differential.cpp'), '-o', str(d/'differential')], check=True)
        out, metrics = run_bounded(d/'differential', [], d)
        evidence['differential'] = {**json.loads(out), **metrics}
        # mode, logical layers, distinct queries, sites/query, address digits, spacing
        cases = [('core',2,1000,8,1,1), ('core',2,1000,64,1,1),
                 ('core',8,1000,8,1,1), ('core',16,1000,64,1,1), ('core',128,10,8,1,1),
                 ('core',512,10,64,1,1), ('core',256,3,32,512,1),
                 ('session',128,128,32,1,1), ('core',4096,2,64,1,1),
                 ('core',128,2,128,1,128)]
        for case in cases:
            item = {'arguments': case, 'samples': []}
            for repeat in range(args.repeats):
                pair = {}
                for label in (['before','after'] if repeat%2==0 else ['after','before']):
                    out, metrics = run_bounded(bins[label], case, d)
                    pair[label] = {**json.loads(out), **metrics}
                    item['samples'].append({'repeat': repeat, 'version': label, **pair[label]})
                for field in ['checksum', 'terms']:
                    if pair['before'][field] != pair['after'][field]:
                        raise AssertionError(f'{case}: {field} differs')
            item['median_ms'] = {label: statistics.median(s['ms'] for s in item['samples'] if s['version']==label)
                                 for label in includes}
            evidence['cases'].append(item);save()
            print(json.dumps({'case': case, 'median_ms': item['median_ms']}), flush=True)
        # Progressive logical depth and support, with a separate explicit-output
        # budget (65,536 sites) and the same process RAM limit. No core defaults change.
        for depth, sites, digits in [(4096,256,1),(4096,1024,1),(4096,4096,1),
                                     (4096,4096,512),(4096,4096,4096),(10**12+1,4096,1)]:
            case = ['core', depth, 1, sites, digits, 1]
            try:
                out, metrics = run_bounded(bins['after'], case, d)
                result = {'status': 'target_reached', **json.loads(out), **metrics}
            except RuntimeError as error:
                result = {'status': 'budget_or_failure', 'error': str(error)}
            evidence['frontiers'].append({'arguments': case, **result});save()
            print(json.dumps({'frontier': case, **result}), flush=True)
    save()


if __name__ == '__main__':
    main()
