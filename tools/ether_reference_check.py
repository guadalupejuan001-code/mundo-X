#!/usr/bin/env python3
"""Compare signed pullbacks against the pinned pre-audit edge algorithm."""
import argparse
from pathlib import Path
import subprocess
import tempfile

BASE='7b028db2344912b2ab86a0affa2a1fe56180922b'
SOURCE=r'''
#include "ether_reference.hpp"
#include "mundo_x/ether_core.hpp"
#include <random>
#include <iostream>
int main(){
 std::mt19937_64 rng(42);std::size_t checks=0;
 for(int trial=0;trial<2500;trial++){
  mundo_x::EtherReference old;mundo_x::EtherCore now;
  unsigned depth=rng()%40;
  for(unsigned d=0;d<depth;d++){if(rng()%2){old.h_all();now.h_all();}else{old.cz_neighbors();now.cz_neighbors();}}
  std::vector<mundo_x::EtherReference::Term> a;std::vector<mundo_x::EtherCore::Term>b;
  for(unsigned j=0;j<32;j++)if(rng()%3==0){
   auto id=std::to_string(j);if(trial%3==0)id="1"+std::string(100,'0')+std::to_string(100+j);
   char axis="IXYZ"[rng()%4];a.push_back({id,axis});b.push_back({id,axis});
  }
  int sign=rng()%2?1:-1;auto x=old.pullback(a,sign);auto y=now.pullback(b,sign);
  if(x.sign!=y.sign||x.terms.size()!=y.terms.size())return 1;
  for(std::size_t i=0;i<x.terms.size();i++)if(x.terms[i].address!=y.terms[i].address||x.terms[i].axis!=y.terms[i].axis)return 2;
  checks++;
 }
 std::cout<<"{\"signed_pullbacks_compared\":"<<checks<<",\"mismatches\":0,\"seed\":42}\n";
}
'''

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--cxx',default='g++');args=p.parse_args()
    root=Path(__file__).resolve().parents[1]
    old=subprocess.check_output(['git','show',BASE+':native/include/mundo_x/ether_core.hpp'],cwd=root,text=True)
    with tempfile.TemporaryDirectory(prefix='ether-reference-') as td:
        d=Path(td);(d/'ether_reference.hpp').write_text(old.replace('EtherCore','EtherReference'))
        (d/'check.cpp').write_text(SOURCE)
        subprocess.run([args.cxx,'-std=c++17','-O3','-I',str(root/'native/include'),str(d/'check.cpp'),'-o',str(d/'check')],check=True)
        subprocess.run([str(d/'check')],check=True,timeout=60)

if __name__=='__main__':main()
