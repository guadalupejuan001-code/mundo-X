#include "mundo_x/infinite_session.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using Session=mundo_x::InfiniteSession;
unsigned checks=0;

void need(bool ok,const std::string& reason){
  ++checks;
  if(!ok)throw std::runtime_error(reason);
}

template<class F> void rejected(F action,const std::string& text){
  ++checks;
  try{action();}
  catch(const std::exception& error){
    if(std::string(error.what()).find(text)==std::string::npos)
      throw std::runtime_error("wrong error: "+std::string(error.what()));
    return;
  }
  throw std::runtime_error("expected rejection containing "+text);
}
}

int main(){
  try{
    Session memory;
    auto output=memory.execute("qload8 0\n");
    need(output.find("\"value\":0")!=std::string::npos,
         "untouched zero-background byte must read zero");
    need(memory.stats().active_qubits==0,
         "qload8 must not materialize the zero background");

    output=memory.execute("qstore8 0 165\nqload8 0\n");
    need(output.find("\"value\":165")!=std::string::npos,
         "qstore8/qload8 round trip failed");
    need(memory.stats().active_qubits==4,
         "0xA5 should materialize only four one bits");
    for(unsigned bit=0;bit<8;++bit){
      const bool expected=(0xA5U&(1U<<bit))!=0;
      need(memory.probability_one(std::to_string(bit))==(expected?1.0:0.0),
           "qstore8 bit mapping is not little-endian consecutive qubits");
    }

    memory.execute("qstore8 0 0\n");
    need(memory.execute("qload8 0\n").find("\"value\":0")!=std::string::npos,
         "qstore8 could not restore zero");
    need(memory.stats().active_qubits==4,
         "stores do not silently compact materialized state");
    memory.execute("compact\n");
    need(memory.stats().active_qubits==0,
         "compact must release an exact zero-background byte");

    const std::string huge="1"+std::string(100,'0');
    output=memory.execute("qstore8 "+huge+" 3\nqload8 "+huge+"\n");
    need(output.find("\"value\":3")!=std::string::npos,
         "large decimal byte address lost precision");
    need(memory.stats().active_qubits==2,
         "sparse huge byte should materialize only its one bits");

    memory.reset(Session::Background::Zero);
    memory.apply({{'H',"8",{}}});
    const auto quantum=memory.snapshot();
    rejected([&]{(void)memory.execute("qload8 1\n");},"bit 0 is quantum");
    need(memory.snapshot()==quantum,
         "failed qload8 must preserve quantum state");
    rejected([&]{(void)memory.execute("qstore8 1 7\n");},"bit 0 is quantum");
    need(memory.snapshot()==quantum,
         "failed qstore8 must preserve quantum state");

    memory.reset(Session::Background::Zero);
    const auto before=memory.snapshot();
    rejected([&]{(void)memory.execute("qstore8 2 1\nqstore8 3 256\n");},
             "exceeds 255");
    need(memory.snapshot()==before,
         "a later invalid qstore8 must roll back the whole request");

    output=memory.execute("qstore8 0005 128\nqload8 5\n");
    need(output.find("\"address\":\"5\"")!=std::string::npos&&
         output.find("\"value\":128")!=std::string::npos,
         "qbyte address canonicalization failed");

    std::cout<<"Ether memory: "<<checks<<" checks passed\n";
    return 0;
  }catch(const std::exception& error){
    std::cerr<<"FAIL Ether memory: "<<error.what()<<'\n';
    return 1;
  }
}
