"""Alternate identical legacy workloads between old and candidate headers."""
import argparse
import json
import pathlib
import statistics
import subprocess
import tempfile


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--old-repo',required=True)
    ap.add_argument('--new-repo',required=True)
    ap.add_argument('--output',required=True)
    args=ap.parse_args()
    results=[]
    with tempfile.TemporaryDirectory(prefix='mx-regression-') as tmp:
        bins={}
        worker=pathlib.Path(__file__).with_name('regression_worker.cpp')
        for label,repo in [('old',args.old_repo),('new',args.new_repo)]:
            binary=pathlib.Path(tmp)/label
            subprocess.run(['g++','-O3','-DNDEBUG','-std=c++17','-I',str(pathlib.Path(repo)/'native/include'),str(worker),'-o',str(binary)],check=True)
            bins[label]=binary
        cases=[('ghz',24,0,0),('ghz',1024,0,0),('cluster',1024,0,0),('structured',20,64,4),('product',16,0,0)]
        for kind,n,t,k in cases:
            measurements={'old':[],'new':[]}
            for group in range(9):
                for label in (('old','new') if group%2 else ('new','old')):
                    r=subprocess.run([str(bins[label]),kind,str(n),str(t),str(k),'15','timing'],text=True,capture_output=True,check=True,timeout=60)
                    measurements[label].append(json.loads(r.stdout))
            medians={label:statistics.median(v for r in rows for v in r['total_us']) for label,rows in measurements.items()}
            ratio=medians['new']/medians['old']
            memory_same=measurements['new'][-1]['logical_bytes']==measurements['old'][-1]['logical_bytes']
            result={'case':kind+'_'+str(n),'old_median_us':medians['old'],'new_median_us':medians['new'],
                    'new_over_old':ratio,'logical_payload_unchanged':memory_same,'passed':ratio<=1.15 and memory_same,
                    'measurements':measurements}
            results.append(result)
            print(json.dumps({k:v for k,v in result.items() if k!='measurements'}),flush=True)
    pathlib.Path(args.output).write_text(json.dumps({'all_passed':all(r['passed'] for r in results),'results':results},indent=2)+'\n')


if __name__=='__main__': main()
