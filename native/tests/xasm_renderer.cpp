#include "mundo_x/xasm.hpp"
#include <iostream>
#include <cmath>
#include <cstdint>
#include <vector>
#include <sstream>
#include <stdexcept>
#include <string>

static std::size_t checks=0;
static void need(bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(message);}
static mundo_x::XasmRunResult run(const std::string& source){std::istringstream in(source);return mundo_x::run_xasm(in);}
static void rejects(const std::string& source){bool ok=false;try{(void)run(source);}catch(const std::exception&){ok=true;}need(ok,"invalid render XASM accepted");}

int main(){
  try{
    const auto result=run(
      "render 160 90 16\n"
      "render_camera 0 0 10 0 0 0 47\n"
      "render_body 0 0 0 0 1.5 0.2 0.6 1\n"
      "render_body 1 3 0 -4 1 1 0.4 0.1\n"
      "render_plan\n"
      "render_train\n"
      "render_frame\n"
      "render_pixel 80 45\n"
      "render_assert_exact\n"
      "render_info\n");
    need(result.qubits==0&&result.scene_objects==0,"render-only XASM allocated another domain");
    need(result.render_width==160&&result.render_height==90&&result.render_bodies==2,"render metadata wrong");
    need(result.render_instructions==7,"render mutation/execution count wrong");
    need(result.render_candidate_tests<result.render_brute_tests,"XASM renderer did not reduce tests");
    need(result.assertions_passed,"render exact assertion failed");
    need(result.output.find("PIXEL x=80 y=45 body=0")!=std::string::npos,"center render pixel wrong");
    need(result.output.find("ASSERT render_exact PASS mismatches=0")!=std::string::npos,"exact render assertion missing");
    need(result.output.find("RENDER_TRAIN tile=")!=std::string::npos,"render training report missing");
    need(result.output.find("RENDER 160x90")!=std::string::npos,"render info missing");

    rejects("render 0 90 16\n");
    rejects("render 160 90 2\n");
    rejects("render_body 0 0 0 0 1 1 1 1\n");
    rejects("render 160 90 16\nrender_body 64 0 0 0 1 1 1 1\n");
    rejects("render 160 90 16\nrender_camera 0 0 0 0 0 0 47\n");
    rejects("render 160 90 16\nrender_pixel 0 0\n");

    // Live MIL/Reduced state (not the obsolete PR #36): q999 non-Clifford
    // observable is connected to material without measurement or dense vectors.
    const auto mil=run(
      "backend reduced\n"
      "qubits 1000\n"
      "h 999\n"
      "render 96 54 16\n"
      "render_body 0 0 0 0 1.8 0.35 0.55 0.75\n"
      "render_bind 0 X 999 0.75\n"
      "render_quantum 0\n"
      "render_frame\n"
      "render_info\n"
      "t 999\n"
      "render_quantum 0\n"
      "render_frame\n"
      "render_assert_exact\n"
      "render_info\n"
      "tdg 999\n"
      "render_frame\n"
      "render_info\n"
      "phase 999 0.37\n"
      "cphase 999 0 0.23\n"
      "render_frame\n"
      "render_assert_exact\n"
      "assert norm\n");
    need(mil.backend=="reduced"&&mil.qubits==1000,
         "MIL rendering did not use canonical 1000q Reduced");
    need(mil.t_gates==2&&mil.assertions_passed,
         "MIL non-Clifford + render exact contracts failed");
    need(mil.output.find("RENDER_QUANTUM id=0 value=1\n")!=std::string::npos,
         "initial q999 material signal missing");
    need(mil.output.find("RENDER_QUANTUM id=0 value=0.707")!=std::string::npos,
         "T q999 signal did not reach Ether Render X");
    std::vector<std::uint64_t> hashes;
    std::istringstream rows(mil.output);
    std::string row;
    while(std::getline(rows,row)){
      if(row.rfind("RENDER 96x54 ",0)!=0)continue;
      const auto at=row.find(" hash=");
      need(at!=std::string::npos,"render info hash absent");
      hashes.push_back(std::stoull(row.substr(at+6)));
    }
    need(hashes.size()==3,"expected initial, T and inverse frames");
    need(hashes[0]!=hashes[1],"MIL T did not change material pixels");
    need(hashes[0]==hashes[2],"MIL T inverse did not restore material pixels");
    need(mil.output.find("ASSERT render_exact FAIL")==std::string::npos,
         "MIL material broke optimized-versus-oracle equality");

    const auto paired=run(
      "backend reduced\nqubits 2\nh 0\ncx 0 1\n"
      "render 64 36 8\nrender_body 0 0 0 0 1.8 .4 .6 .8\n"
      "cphase 0 1 0.37\n"
      "render_bind_pauli 0 XX 0.5\n"
      "render_quantum 0\n"
      "render_frame\nrender_assert_exact\n"
      "render_unbind 0\n"
      "render_frame\nrender_assert_exact\n");
    need(paired.assertions_passed&&paired.render_bodies==1,
         "entangled Pauli binding and unbinding failed");
    need(paired.output.find("RENDER_QUANTUM id=0 value=0.932")!=std::string::npos,
         "controlled-phase correlation failed");

    rejects("render 64 36 8\nrender_body 0 0 0 0 1 .4 .6 .8\nrender_bind 0 X 0 .5\n");
    rejects("backend reduced\nqubits 1000\nrender 64 36 8\nrender_body 0 0 0 0 1 .4 .6 .8\nrender_bind 0 X 1000 .5\n");
    rejects("backend reduced\nqubits 1000\nrender 64 36 8\nrender_body 0 0 0 0 1 .4 .6 .8\nrender_bind_pauli 0 X .5\n");
    rejects("backend reduced\nqubits 2\nrender 64 36 8\nrender_body 0 0 0 0 1 .4 .6 .8\nrender_bind 0 X 1 2\n");
    rejects("backend reduced\nqubits 2\nphase 0 nan\n");


    std::cout<<"checks="<<checks<<" failures=0 hash="<<result.render_hash<<"\n";
    return 0;
  }catch(const std::exception& e){std::cerr<<"xasm_renderer: "<<e.what()<<'\n';return 1;}
}
