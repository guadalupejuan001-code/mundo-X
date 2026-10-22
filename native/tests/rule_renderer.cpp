#include "mundo_x/ether_renderer.hpp"
#include <cmath>
#include <cstdint>
#include <iostream>
#include <random>
#include <stdexcept>

static std::size_t checks=0;
static void need(bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(message);}

int main(){
  try{
    using R=mundo_x::EtherRenderer;
    R renderer(320,180,32);
    renderer.set_camera({0,0,10},{0,0,0},47);
    renderer.add_body({0,{0,0,0},1.4,{{.2,.6,1.}}});
    renderer.add_body({1,{3.4,.7,-2.},.9,{{1.,.45,.1}}});
    renderer.add_body({2,{-4.,-1.,-8.},1.8,{{.55,.8,.3}}});
    renderer.plan();
    auto stats=renderer.render(true);
    need(stats.exact_mismatches==0,"region renderer differs from brute-force frame");
    need(stats.candidate_tests<stats.brute_tests,"region planning did not reduce body tests");
    need(renderer.pixel(160,90).body==0,"central body is not visible");
    need(stats.max_candidates<=3,"candidate count exceeds body count");
    const auto before_hash=stats.frame_hash;
    const auto trained=renderer.train_tiles({8,16,32,64});
    need(trained.exact_mismatches==0,"trained tile path differs from brute-force frame");
    need(trained.frame_hash==before_hash,"tile training changed the rendered image");
    need(trained.chosen_tile==8||trained.chosen_tile==16||trained.chosen_tile==32||trained.chosen_tile==64,
         "tile training selected a non-candidate size");

    std::mt19937_64 random(20261021);
    for(int scene=0;scene<100;++scene){
      R sample(96,54,16);
      sample.set_camera({0,0,12},{0,0,0},47);
      for(std::uint32_t id=0;id<20;++id){
        const auto u=[&](){return static_cast<double>(random()%100000)/100000.;};
        R::Body b;
        b.id=id;b.center={(u()-.5)*24.,(u()-.5)*14.,-2.-u()*34.};
        b.radius=.15+u()*2.2;b.color={{u(),u(),u()}};
        sample.add_body(b);
      }
      sample.plan();const auto s=sample.render(true);
      need(s.exact_mismatches==0,"random region planner omitted an exact hit");
    }

    std::cout<<"{\"suite\":\"ether_renderer\",\"checks\":"<<checks
             <<",\"pixels\":"<<stats.width*stats.height
             <<",\"candidate_tests\":"<<stats.candidate_tests
             <<",\"brute_tests\":"<<stats.brute_tests
             <<",\"test_ratio\":"<<stats.test_ratio()
             <<",\"frame_hash\":"<<stats.frame_hash
             <<",\"exact_mismatches\":"<<stats.exact_mismatches<<"}\n";
    return 0;
  }catch(const std::exception& e){std::cerr<<"ether_renderer: "<<e.what()<<'\n';return 1;}
}
