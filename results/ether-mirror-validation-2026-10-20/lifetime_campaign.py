#!/usr/bin/env python3
"""Repeat the verified eight-lifetime fixture in one bounded process."""
import gc,json,os,resource,sys
from pathlib import Path
root=Path(__file__).resolve().parents[2]
os.environ.update(OPENBLAS_NUM_THREADS='1',LP_NUM_THREADS='1',LIBGL_ALWAYS_SOFTWARE='1')
resource.setrlimit(resource.RLIMIT_AS,(2*1024**3,2*1024**3))
resource.setrlimit(resource.RLIMIT_CPU,(55,56))
os.sched_setaffinity(0,{min(os.sched_getaffinity(0))})
sys.path.insert(0,str(root/'tools'))
from ether_mirror_check import lifetime_check

def rss():
    return int(next(line.split()[1] for line in Path('/proc/self/status').read_text().splitlines() if line.startswith('VmRSS:')))
records=[]
for batch in range(8):
    report=lifetime_check()
    records.append({'batch':batch,'lifetimes':report['core_and_gl_lifetimes'],'rss_after_close_kib':rss(),
                    'rss_peak_kib':resource.getrusage(resource.RUSAGE_SELF).ru_maxrss})
before_gc=rss();gc.collect()
print(json.dumps({'status':'passed','lifetimes':64,'records':records,'rss_before_gc_kib':before_gc,
                  'rss_after_gc_kib':rss(),'address_space_limit_bytes':2*1024**3},indent=2))
