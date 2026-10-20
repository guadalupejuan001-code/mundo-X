#!/usr/bin/env python3
"""Random Clifford states, full-support queries and independent finite reference.

The 8-site case also uses DenseReference's full state vector. Larger cases use
the finite Compact backend and its stabilizer generators; that is a separate
session path, not an independent tableau implementation. Seed and circuit are
identical before/after. Run each case with an external time budget.
"""
import argparse
import hashlib
import json
from pathlib import Path
import random
import resource
import sys
import time

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--source', type=Path, default=Path(__file__).resolve().parents[1])
p.add_argument('--sites', type=int, required=True)
p.add_argument('--duration', type=float, default=0, help='seconds of sustained mutation/query/restore')
args = p.parse_args()
resource.setrlimit(resource.RLIMIT_AS, (2*1024**3, 2*1024**3))
sys.path.insert(0, str(args.source.resolve()/'graphics-desktop'))
from engine import build_native
from infinite_runtime import InfiniteCore
from core_runtime import Core
import numpy as np

build_native()
n=args.sites
assert 2 <= n <= 1024
rng=random.Random(42)
gates=[('H', i) for i in range(n)] + [('CX', i, i+1) for i in range(n-1)]
for _ in range(16*n):
    op=rng.choice(('H','S','X','Y','Z','CX','CZ'))
    a,b=rng.sample(range(n),2)
    gates.append((op,a,b) if op in ('CX','CZ') else (op,a))
with Core(n) as reference, InfiniteCore() as ether:
    reference.apply(gates)
    ether.apply([(g[0], *map(str,g[1:])) for g in gates])
    ether.h_all()
    reference.apply([('H',i) for i in range(n)])
    queries=reference.generators()[:16]
    queries += [rng.choice(('','-'))+''.join(rng.choice('IXYZ') for _ in range(n)) for _ in range(16)]
    def sparse(q):
        sign='- ' if q.startswith('-') else ''
        body=q.lstrip('+-')
        return sign+' '.join(f'{axis}:{i}' for i,axis in enumerate(body) if axis != 'I')
    expected=reference.query(queries)
    if n <= 8:
        with Core(n,'dense') as dense:
            dense.apply(gates);dense.apply([('H',i) for i in range(n)])
            assert np.max(np.abs(dense.query(queries)-expected)) < 1e-12
    saved=ether.snapshot()
    t=time.perf_counter(); actual=ether.query(list(map(sparse,queries)));elapsed=time.perf_counter()-t
    assert np.array_equal(actual,expected)
    assert np.all(actual[:min(16,n)]==1)
    assert ether.snapshot()==saved
    ether.restore(saved);assert np.array_equal(ether.query(list(map(sparse,queries))),actual)
    def rss_now():
        for line in Path('/proc/self/status').read_text().splitlines():
            if line.startswith('VmRSS:'): return int(line.split()[1])
    memory=[rss_now()];cycles=0;latencies=[];started=time.perf_counter()
    flipped=expected.copy()
    for i,q in enumerate(queries):
        if q.lstrip('+-')[0] in 'XY': flipped[i] *= -1
    while time.perf_counter()-started < args.duration:
        cycle=time.perf_counter()
        ether.apply([('Z','0')])
        assert np.array_equal(ether.query(list(map(sparse,queries))),flipped)
        ether.apply([('Z','0')])
        assert np.array_equal(ether.query(list(map(sparse,queries))),expected)
        if cycles%100==0:
            checkpoint=ether.snapshot();ether.restore(checkpoint)
            assert ether.snapshot()==checkpoint
            memory.append(rss_now())
        latencies.append((time.perf_counter()-cycle)*1000);cycles+=1
    memory.append(rss_now())
    print(json.dumps({'status':'passed','seed':42,'sites':n,'gates':len(gates)+n,
        'queries':len(queries),'reference':'dense vector + finite Compact' if n<=8 else 'finite Compact',
        'mismatches':0,'query_ms':elapsed*1000,'stats':ether.stats(),
        'snapshot_sha256':hashlib.sha256(saved.encode()).hexdigest(),
        'sustained':{'seconds':time.perf_counter()-started,'cycles':cycles,
            'rss_samples_kib':memory,'cycle_ms_median':float(np.median(latencies)) if latencies else None,
            'cycle_ms_p95':float(np.quantile(latencies,.95)) if latencies else None},
        'rss_peak_kib':resource.getrusage(resource.RUSAGE_SELF).ru_maxrss}))
