#include "mundo_x/ether_renderer.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

static double median(std::vector<double> values) {
  std::sort(values.begin(),values.end());
  return values[values.size()/2];
}

static void scenario(int width,int height,std::uint32_t count) {
  using Renderer=mundo_x::EtherRenderer;
  Renderer render(static_cast<std::size_t>(width),
                  static_cast<std::size_t>(height),32);
  render.set_camera({0.,0.,12.},{0.,0.,0.},47.);
  for(std::uint32_t i=0;i<count;++i){
    const double x=(static_cast<int>((i*37)%67)-33)*.65;
    const double y=(static_cast<int>((i*31)%47)-23)*.42;
    const double z=-4.-static_cast<double>((i*17)%37);
    const double radius=.65+static_cast<double>(i%9)*.13;
    const double red=.2+static_cast<double>(i%7)*.1;
    render.add_body({i,{x,y,z},radius,{{red,.5,.8}}});
  }
  render.plan();
  render.render(false);
  std::vector<double> times;
  for(unsigned repeat=0;repeat<7;++repeat){
    auto start=std::chrono::steady_clock::now();
    render.render(false);
    auto end=std::chrono::steady_clock::now();
    times.push_back(std::chrono::duration<double,std::milli>(end-start).count());
  }
  auto exact=render.render(true);
  if(exact.exact_mismatches!=0)
    throw std::runtime_error("renderer disagreed with its brute force reference");
  if(exact.candidate_tests>=exact.brute_tests)
    throw std::runtime_error("no work eliminated in benchmark scenario");
  std::cout<<"{\"width\":"<<width
           <<",\"height\":"<<height
           <<",\"bodies\":"<<count
           <<",\"hash\":"<<exact.frame_hash
           <<",\"candidate_tests\":"<<exact.candidate_tests
           <<",\"brute_tests\":"<<exact.brute_tests
           <<",\"mismatches\":"<<exact.exact_mismatches
           <<",\"median_ms\":"<<median(times)<<"}\n";
}

int main() {
  try {
    scenario(320,180,32);
    scenario(640,360,64);
    return 0;
  }catch(const std::exception& e){
    std::cerr<<"ether render benchmark failed: "<<e.what()<<'\n';
    return 1;
  }
}
