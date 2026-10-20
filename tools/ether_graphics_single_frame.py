#!/usr/bin/env python3
"""Separate one actual GLES frame/readback from the multi-frame stress campaign."""
import argparse
import json
from pathlib import Path
import resource
import sys
import time

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--width',type=int,default=7680)
p.add_argument('--height',type=int,default=4320)
p.add_argument('--objects',type=int,default=1024)
a=p.parse_args()
resource.setrlimit(resource.RLIMIT_AS,(2*1024**3,2*1024**3))
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'graphics-desktop'))
from engine import build_native, QuantumScene, QuantumGraphicsEngine, street
from infinite_runtime import InfiniteCore
build_native()
with InfiniteCore() as core:
    core.execute('h_all\ncz_neighbors\n')
    scene=QuantumScene.from_infinite_core(core,count=a.objects,observables=['X:0 Z:1','Z:0 X:1 Z:2'])
    engine=QuantumGraphicsEngine(scene,a.width,a.height)
    try:
        saved=core.snapshot();start=time.perf_counter()
        frame,metrics=engine.observe(street(a.width,a.height))
        assert core.snapshot()==saved
        print(json.dumps({'status':'passed','width':a.width,'height':a.height,'objects':a.objects,
            'frames':1,'elapsed_ms':(time.perf_counter()-start)*1000,'metrics':metrics,
            'frame_bytes':frame.nbytes,'channel_min':int(frame.min()),'channel_max':int(frame.max()),
            'rss_peak_kib':resource.getrusage(resource.RUSAGE_SELF).ru_maxrss,
            'driver':engine.renderer.gl.info,'scope':'host Mesa single frame and readback; no physical phone'}))
    finally:engine.close();scene.close()
