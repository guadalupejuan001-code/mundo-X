#include "mundo_x/ether_render_spatial.hpp"
#include <iostream>
#include <iomanip>
#include <stdexcept>
#include <cstdint>
#include <cstdlib>

int main(int argc,char** argv){
  try {
    const unsigned width=argc>1?static_cast<unsigned>(std::stoul(argv[1])):7680;
    const unsigned height=argc>2?static_cast<unsigned>(std::stoul(argv[2])):4320;
    const unsigned count=argc>3?static_cast<unsigned>(std::stoul(argv[3])):4096;
    const unsigned tile=argc>4?static_cast<unsigned>(std::stoul(argv[4])):64;
    using R=mundo_x::EtherRenderSpatial;
    R render(width,height,tile);
    render.set_camera({0.,0.,12.},{0.,0.,0.},47.);
    for(std::uint32_t i=0;i<count;++i){
      const double x=(static_cast<int>((i*37)%311)-155)*.41;
      const double y=(static_cast<int>((i*31)%211)-105)*.35;
      const double z=-20.-static_cast<double>((i*17)%97);
      render.add_body({i,{x,y,z},.08+static_cast<double>(i%11)*.03,{{.3,.6,.8}}});
    }
    auto sparse=render.render_sparse_surfaces();
    auto result=render.render();
    std::cout<<std::fixed<<std::setprecision(3)
             <<"{\"width\":"<<result.width<<",\"height\":"<<result.height
             <<",\"bodies\":"<<result.bodies<<",\"tile\":"<<result.tile
             <<",\"tiles\":"<<result.tiles<<",\"nonempty_tiles\":"<<result.nonempty_tiles
             <<",\"tile_references\":"<<result.tile_references
             <<",\"sparse_queries\":"<<sparse.sparse_queries
             <<",\"sparse_surfaces\":"<<sparse.sparse_surfaces
             <<",\"sparse_ms\":"<<sparse.sparse_ms
             <<",\"ray_queries\":"<<result.ray_queries
             <<",\"candidate_tests\":"<<result.intersection_tests
             <<",\"oracle_upper_bound\":"<<result.oracle_tests
             <<",\"plan_ms\":"<<result.planning_ms
             <<",\"render_ms\":"<<result.render_ms
             <<",\"hash\":"<<result.hash
             <<",\"frames_per_second_equivalent\":"<<(result.render_ms>0?1000./result.render_ms:0.)
             <<"}\n";
    return 0;
  }catch(const std::exception& e){std::cerr<<"SPATIAL_BENCH_FAIL: "<<e.what()<<"\n";return 1;}
}
