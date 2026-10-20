#!/usr/bin/env python3
"""Pinned differential checks and alternating before/after rule benchmarks (Linux)."""
import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import resource
import signal
import statistics
import subprocess
import tarfile
import tempfile
import time

BASE = 'aab0930378796ebfd42f9629e2575108ca94fcff'
BENCH = r'''
#include "mundo_x/infinite_core.hpp"
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <new>
namespace allocation {
struct alignas(std::max_align_t) Header { std::size_t size; };
std::size_t live=0, peak=0, calls=0, bytes=0;
void* get(std::size_t n) {
 if(n>SIZE_MAX-sizeof(Header))throw std::bad_alloc();
 auto* p=static_cast<Header*>(std::malloc(n+sizeof(Header)));
 if(!p)throw std::bad_alloc();p->size=n;live+=n;peak=std::max(peak,live);++calls;bytes+=n;return p+1;
}
void release(void* p)noexcept{if(p){auto*h=static_cast<Header*>(p)-1;live-=h->size;std::free(h);}}
}
void* operator new(std::size_t n){return allocation::get(n);}
void* operator new[](std::size_t n){return allocation::get(n);}
void operator delete(void*p)noexcept{allocation::release(p);}
void operator delete[](void*p)noexcept{allocation::release(p);}
void operator delete(void*p,std::size_t)noexcept{allocation::release(p);}
void operator delete[](void*p,std::size_t)noexcept{allocation::release(p);}
using E=mundo_x::EtherCore;using C=mundo_x::InfiniteCore;
using Clock=std::chrono::steady_clock;
volatile std::uint64_t sink=0;
std::string address(std::size_t q,std::size_t digits){
 auto s=std::to_string(q);if(digits>s.size())s="1"+std::string(digits-s.size()-1,'0')+s;return s;
}
template<class Core> void prepare(Core& e,std::size_t depth){for(std::size_t i=0;i<depth;++i)if(i%2)e.cz_neighbors();else e.h_all();}
int main(int argc,char**argv){
 if(argc<3)return 2;std::string mode=argv[1];auto depth=std::stoull(argv[2]);
 if(mode=="contract"){
  C c(C::Background::BellPairs);c.seed(42);c.apply({{'H',"0",{}},{'S',"1",{}},{'C',"1","2"}});prepare(c,depth);
  c.apply({{'X',"0",{}},{'H',"1",{}},{'S',"2",{}},{'C',"2","3"}});
  auto saved=c.snapshot();auto restored=C::from_snapshot(saved,c.limits());
  std::cout<<saved<<'\n';
  for(std::size_t q=0;q<8;++q){
   for(char axis:{'X','Y','Z'})std::cout<<c.expectation({{std::to_string(q),axis}})<<' ';
   auto a=c.measure_z(std::to_string(q)),b=restored.measure_z(std::to_string(q));
   if(a.outcome_one!=b.outcome_one||a.probability!=b.probability||a.random!=b.random)return 3;
   std::cout<<a.outcome_one<<' '<<a.probability<<' '<<a.random<<'\n';
  }
  if(c.snapshot()!=restored.snapshot())return 4;std::cout<<c.snapshot()<<'\n';return 0;
 }
 auto count=std::stoull(argv[3]);auto qbase=std::stoull(argv[4]);auto digits=std::stoull(argv[5]);
 E e;C owner;std::vector<E::Term> query;
 if(mode!="storage"&&mode!="owner")prepare(e,depth);
 if(mode=="query")query.push_back({address(qbase,digits),'X'});
 if(mode=="mixed")for(std::size_t j=0;j<8;++j)query.push_back({address(qbase+j,digits),"XYZ"[j%3]});
 auto calls=allocation::calls,bytes=allocation::bytes,live=allocation::live;allocation::peak=live;
 std::uint64_t checksum=1469598103934665603ULL;std::size_t terms=0;
 auto hash=[&](unsigned char v){checksum^=v;checksum*=1099511628211ULL;};
 const auto start=Clock::now();
 if(mode=="storage")for(std::size_t i=0;i<count;++i){E next;prepare(next,depth);e=std::move(next);}
 else if(mode=="owner")prepare(owner,depth);
 else if(mode=="copy")for(std::size_t i=0;i<count;++i){auto copy=e;sink=copy.rule_count();}
 else if(mode=="query"||mode=="mixed")for(std::size_t i=0;i<count;++i){
  if(mode=="query")query[0].address=address(qbase+(i%5),digits);
  auto result=e.pullback(query,(i%2)?-1:1);terms+=result.terms.size();hash(result.sign==1?1:255);
  for(const auto&t:result.terms){for(unsigned char c:t.address)hash(c);hash(t.axis);}
 }else return 5;
 const double ms=std::chrono::duration<double,std::milli>(Clock::now()-start).count();
 const auto logical_rules=mode=="owner"?owner.ether_rule_count():e.rule_count();
 checksum^=logical_rules;
 const auto used_calls=allocation::calls-calls,used_bytes=allocation::bytes-bytes;
 const auto peak=allocation::peak-live,retained=allocation::live-live;
 std::size_t process_peak_rss=0;
 {std::ifstream status("/proc/self/status");std::string line;while(std::getline(status,line))
  if(line.compare(0,6,"VmHWM:")==0)process_peak_rss=std::stoull(line.substr(6));}
 std::cout<<std::setprecision(12)<<"{\"ms\":"<<ms<<",\"new_calls\":"<<used_calls
 <<",\"new_bytes\":"<<used_bytes<<",\"peak_new_bytes\":"<<peak<<",\"retained_new_bytes\":"<<retained
 <<",\"frame_object_bytes\":"<<sizeof(E)<<",\"frame_without_stats_bytes\":"<<(sizeof(E)-sizeof(E::Stats))
 <<",\"process_peak_rss_kib\":"<<process_peak_rss<<",\"logical_rules\":"<<logical_rules
 <<",\"checksum\":"<<checksum<<",\"terms\":"<<terms<<"}\n";
}
'''
DIFFERENTIAL = r'''
#include "ether_reference.hpp"
#include "mundo_x/ether_core.hpp"
#include <iostream>
#include <random>
using Old=mundo_x::EtherReference;using Now=mundo_x::EtherCore;
int main(){
 std::mt19937_64 rng(42);std::size_t comparisons=0;
 for(std::size_t trial=0;trial<4000;++trial){
  Old old;Now now;auto depth=rng()%129;bool first=rng()&1;
  if(trial%4==0)depth%=17;
  for(std::size_t i=0;i<depth;++i)if((i%2)==first){old.h_all();now.h_all();}else{old.cz_neighbors();now.cz_neighbors();}
  auto q=rng()%64;auto id=std::to_string(q);
  if(trial%4==0)id=std::string(4096,'9');
  else if(trial%4==1)id="1"+std::string(127,'0');
  char axis="XYZ"[rng()%3];int sign=(rng()&1)?-1:1;
  std::vector<Old::Term> qa{{id,axis}};std::vector<Now::Term> qb{{id,axis}};
  if(trial%4==3){
   qa.clear();qb.clear();
   for(std::size_t j=0;j<8;++j){auto at=std::to_string(q+2*j);char a="IXYZ"[rng()%4];qa.push_back({at,a});qb.push_back({at,a});}
  }
  auto a=old.pullback(qa,sign);auto b=now.pullback(qb,sign);
  if(a.sign!=b.sign||a.terms.size()!=b.terms.size())return 1;
  for(std::size_t i=0;i<a.terms.size();++i)if(a.terms[i].address!=b.terms[i].address||a.terms[i].axis!=b.terms[i].axis)return 2;
  ++comparisons;
 }
 std::cout<<"{\"signed_pullbacks_compared\":"<<comparisons
 <<",\"long_address_queries\":2000,\"mixed_queries\":1000,\"mismatches\":0,\"seed\":42}\n";
}
'''


def run_bounded(binary, arguments, directory, wall_seconds=30):
    """wait4 gives this child's peak RSS, without including compiler RSS."""
    out = directory / 'child.stdout'
    err = directory / 'child.stderr'
    start = time.monotonic()
    pid = os.fork()
    if pid == 0:
        try:
            os.sched_setaffinity(0, {min(os.sched_getaffinity(0))})
            resource.setrlimit(resource.RLIMIT_AS, (2 * 1024**3, 2 * 1024**3))
            resource.setrlimit(resource.RLIMIT_CPU, (28, 29))
            with out.open('wb') as stdout, err.open('wb') as stderr:
                os.dup2(stdout.fileno(), 1)
                os.dup2(stderr.fileno(), 2)
                os.execv(str(binary), [str(binary), *map(str, arguments)])
        finally:
            os._exit(127)
    expired = False
    while True:
        done, status, usage = os.wait4(pid, os.WNOHANG)
        if done:
            break
        if time.monotonic() - start > wall_seconds:
            os.kill(pid, signal.SIGKILL)
            _, status, usage = os.wait4(pid, 0)
            expired = True
            break
        time.sleep(.005)
    if status:
        raise RuntimeError(f'child failed: {arguments}, status={status}, timeout={expired}, {err.read_text()}')
    return out.read_text(), {'wait4_peak_rss_kib': usage.ru_maxrss, 'wall_ms': (time.monotonic()-start)*1000}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--cxx', default='g++')
    p.add_argument('--repeats', type=int, default=5)
    args = p.parse_args()
    if args.repeats < 1:
        p.error('--repeats must be positive')
    root = Path(__file__).resolve().parents[1]
    evidence = {'baseline': BASE, 'seed': 42, 'repeats': args.repeats,
                'cpu_affinity': [min(os.sched_getaffinity(0))],
                'memory_budget': '2 GiB RLIMIT_AS; not a phone measurement',
                'compiler': subprocess.check_output([args.cxx, '--version'], text=True).splitlines()[0],
                'flags': ['-std=c++17', '-O3', '-DNDEBUG'],
                'source_sha256': {f: hashlib.sha256((root/f).read_bytes()).hexdigest() for f in
                    ['native/include/mundo_x/ether_core.hpp', 'native/include/mundo_x/infinite_core.hpp']},
                'samples': []}
    with tempfile.TemporaryDirectory(prefix='ether-rule-compact-') as tmp:
        d = Path(tmp)
        archive = subprocess.check_output(['git', 'archive', BASE, 'native/include'], cwd=root)
        with tarfile.open(fileobj=io.BytesIO(archive)) as tar:
            tar.extractall(d/'baseline', filter='data')
        (d/'bench.cpp').write_text(BENCH)
        binaries = {}
        for label, includes in [('before', d/'baseline/native/include'), ('after', root/'native/include')]:
            binary = d/label
            subprocess.run([args.cxx, *evidence['flags'], '-I', str(includes), str(d/'bench.cpp'), '-o', str(binary)], check=True)
            binaries[label] = binary
        old = (d/'baseline/native/include/mundo_x/ether_core.hpp').read_text()
        (d/'ether_reference.hpp').write_text(old.replace('EtherCore', 'EtherReference'))
        (d/'differential.cpp').write_text(DIFFERENTIAL)
        subprocess.run([args.cxx, *evidence['flags'], '-I', str(root/'native/include'), str(d/'differential.cpp'), '-o', str(d/'differential')], check=True)
        output, metrics = run_bounded(d/'differential', [], d)
        evidence['differential'] = {**json.loads(output), **metrics}
        evidence['contracts'] = []
        for depth in [0, 8, 31, 64, 4096]:
            outputs = [run_bounded(binaries[label], ['contract', depth], d)[0] for label in binaries]
            if outputs[0] != outputs[1]:
                raise AssertionError(f'full snapshot/measurement output differs at depth {depth}')
            evidence['contracts'].append({'depth': depth, 'identical_bytes': len(outputs[0]),
                                          'sha256': hashlib.sha256(outputs[0].encode()).hexdigest()})
        cases = [('storage', 4096, 1000, 0, 1), ('copy', 4096, 10000, 0, 1),
                 ('owner', 4096, 1, 0, 1), ('query', 4096, 10, 0, 1),
                 ('query', 1024, 3, 10000, 4096), ('mixed', 128, 10, 100, 3)]
        for case in cases:
            for repeat in range(args.repeats):
                values = {}
                for label in (['before', 'after'] if repeat%2==0 else ['after', 'before']):
                    output, metrics = run_bounded(binaries[label], case, d)
                    value = {**json.loads(output), **metrics}
                    values[label] = value
                    evidence['samples'].append({'case': case, 'repeat': repeat, 'version': label, **value})
                for field in ['checksum', 'terms', 'logical_rules']:
                    if values['before'][field] != values['after'][field]:
                        raise AssertionError(f'{case}: {field} differs')
            medians = {label: statistics.median(s['ms'] for s in evidence['samples'] if s['case']==case and s['version']==label) for label in binaries}
            print(json.dumps({'case': case, 'median_ms': medians}), flush=True)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(evidence, indent=2)+'\n')


if __name__ == '__main__':
    main()
