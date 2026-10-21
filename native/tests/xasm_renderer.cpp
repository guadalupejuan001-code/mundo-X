#include "mundo_x/xasm.hpp"
#include <iostream>
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
      "render_frame\n"
      "render_pixel 80 45\n"
      "render_assert_exact\n"
      "render_info\n");
    need(result.qubits==0&&result.scene_objects==0,"render-only XASM allocated another domain");
    need(result.render_width==160&&result.render_height==90&&result.render_bodies==2,"render metadata wrong");
    need(result.render_instructions==6,"render mutation/execution count wrong");
    need(result.render_candidate_tests<result.render_brute_tests,"XASM renderer did not reduce tests");
    need(result.assertions_passed,"render exact assertion failed");
    need(result.output.find("PIXEL x=80 y=45 body=0")!=std::string::npos,"center render pixel wrong");
    need(result.output.find("ASSERT render_exact PASS mismatches=0")!=std::string::npos,"exact render assertion missing");
    need(result.output.find("RENDER 160x90")!=std::string::npos,"render info missing");

    rejects("render 0 90 16\n");
    rejects("render 160 90 2\n");
    rejects("render_body 0 0 0 0 1 1 1 1\n");
    rejects("render 160 90 16\nrender_body 64 0 0 0 1 1 1 1\n");
    rejects("render 160 90 16\nrender_camera 0 0 0 0 0 0 47\n");
    rejects("render 160 90 16\nrender_pixel 0 0\n");

    std::cout<<"checks="<<checks<<" failures=0 hash="<<result.render_hash<<"\n";
    return 0;
  }catch(const std::exception& e){std::cerr<<"xasm_renderer: "<<e.what()<<'\n';return 1;}
}
