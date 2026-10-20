#!/usr/bin/env python3
"""Alternate identical bounded native workloads; retain all samples and medians."""
import argparse
import json
from pathlib import Path
import resource
import statistics
import subprocess

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--before',type=Path,required=True)
p.add_argument('--after',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--repeat',type=int,default=5)
a=p.parse_args()
cases=[('batch',256,256),('rules',256,4096),('rules',1024,1000),
       ('noop',1024,1),('operations',65536,1),('lifecycle',1000,1),('threads',8,1000)]
def limits():
    resource.setrlimit(resource.RLIMIT_AS,(2*1024**3,2*1024**3))
    resource.setrlimit(resource.RLIMIT_CPU,(15,16))
results={'repetitions':a.repeat,'memory_profile':'2 GiB host virtual address limit','cases':[]}
for case in cases:
    record={'case':case,'before':[],'after':[]}
    for i in range(a.repeat):
        for label in (('before','after') if i%2==0 else ('after','before')):
            run=subprocess.run([str(getattr(a,label).resolve()),*map(str,case)],
                text=True,capture_output=True,check=True,timeout=17,preexec_fn=limits)
            sample=json.loads(run.stdout);assert sample['status']=='ok',sample
            record[label].append(sample)
    for label in ('before','after'):
        values=[s['work_ms'] for s in record[label]]
        record[label+'_ms']={'median':statistics.median(values),'min':min(values),'max':max(values)}
    record['ratio']=record['before_ms']['median']/record['after_ms']['median']
    results['cases'].append(record);a.output.write_text(json.dumps(results,indent=2)+'\n')
    print(case,record['before_ms'],record['after_ms'],flush=True)
