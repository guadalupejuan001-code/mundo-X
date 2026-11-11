#include "mundo_x/ether_renderer.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

static std::size_t checks=0;
static void need(bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(message);}
static double median(std::vector<double> v){std::sort(v.begin(),v.end());return v[v.size()/2];}

int main(){
  try{
    using R=mundo_x::EtherRenderer;
    R renderer(320,180,16);
    renderer.set_camera({0,0,14},{0,0,-4},47);
    renderer.add_body({0,{0,0,0},1.2,{{.25,.7,1.}}});
    renderer.add_body({1,{4.0,1.0,-4.},1.0,{{1.,.45,.15}}});
    renderer.add_body({2,{-4.5,-1.0,-7.},1.6,{{.55,.8,.3}}});
    renderer.add_body({3,{0,3.0,-10.},1.5,{{.8,.25,.7}}});
    renderer.add_body({4,{0,-3.5,-13.},1.8,{{.35,.8,.75}}});
    renderer.plan();
    renderer.set_observation_enabled(true);

    renderer.set_tile_reuse_enabled(false);
    const auto full=renderer.render(true);
    need(full.exact_mismatches==0,"full baseline differs from brute-force oracle");
    need(full.ray_queries==full.width*full.height,"full baseline did not trace every pixel");
    need(full.reused_pixels==0&&full.reused_tiles==0,"full baseline unexpectedly reused work");
    const auto reference_hash=full.frame_hash;
    const auto full_tests=full.candidate_tests;
    const auto total_tiles=full.tiles_x*full.tiles_y;
    const auto total_pixels=full.width*full.height;

    renderer.set_tile_reuse_enabled(true);
    const auto identical=renderer.render(true);
    need(identical.exact_mismatches==0,"identical reuse differs from brute-force oracle");
    need(identical.frame_hash==reference_hash,"identical reuse changed framebuffer hash");
    need(identical.reused_tiles==total_tiles&&identical.rendered_tiles==0,
         "identical frame did not reuse all tiles");
    need(identical.reused_pixels==total_pixels&&identical.rendered_pixels==0,
         "identical frame did not reuse all pixels");
    need(identical.ray_queries==0,"identical frame still launched optimized rays");
    need(identical.candidate_tests==0,"identical frame still tested candidates");

    renderer.set_quantum_material(0,.8,.5);
    const auto local=renderer.render(true);
    need(local.exact_mismatches==0,"localized dirty render differs from brute-force oracle");
    need(local.frame_hash!=reference_hash,"localized material edit did not change image");
    need(local.rendered_tiles>0&&local.rendered_tiles<total_tiles,
         "localized material edit did not remain tile-local");
    need(local.reused_tiles>0,"localized material edit reused no tiles");
    need(local.ray_queries>0&&local.ray_queries<total_pixels,
         "localized material edit did not reduce ray queries");
    need(local.candidate_tests>0&&local.candidate_tests<full_tests,
         "localized material edit did not reduce candidate tests");
    need(local.changed_tiles>0&&local.changed_tiles<=local.rendered_tiles,
         "changed tiles exceed conservative dirty region");
    const auto local_hash=local.frame_hash;

    renderer.set_tile_reuse_enabled(false);
    const auto local_full=renderer.render(true);
    need(local_full.exact_mismatches==0,"full redraw after local edit differs from oracle");
    need(local_full.frame_hash==local_hash,"dirty-tile result differs from full redraw");

    renderer.set_tile_reuse_enabled(true);
    const auto settled=renderer.render(true);
    need(settled.frame_hash==local_hash,"settled reuse changed image");
    need(settled.ray_queries==0&&settled.candidate_tests==0,
         "settled frame still performed render work");

    renderer.set_camera({1.5,.4,14},{0,0,-4},47);
    const auto camera=renderer.render(true);
    need(camera.exact_mismatches==0,"camera invalidation differs from brute-force oracle");
    need(camera.rendered_tiles==total_tiles&&camera.reused_tiles==0,
         "camera change did not conservatively invalidate every tile");
    need(camera.ray_queries==total_pixels,"camera change did not retrace every pixel");

    renderer.add_body({5,{2.5,-1.5,-6.},.7,{{.9,.8,.2}}});
    const auto geometry=renderer.render(true);
    need(geometry.exact_mismatches==0,"geometry invalidation differs from brute-force oracle");
    need(geometry.rendered_tiles==total_tiles&&geometry.reused_tiles==0,
         "geometry change did not conservatively invalidate every tile");
    need(geometry.ray_queries==total_pixels,"geometry change did not retrace every pixel");

    renderer.set_tile_reuse_enabled(false);
    renderer.render(false);
    std::vector<double> full_ms,reuse_ms;
    for(int i=0;i<9;++i)full_ms.push_back(renderer.render(false).render_ms);
    const auto performance_hash=renderer.stats().frame_hash;
    renderer.set_tile_reuse_enabled(true);
    renderer.render(false);
    for(int i=0;i<9;++i){
      const auto sample=renderer.render(false);
      need(sample.ray_queries==0&&sample.candidate_tests==0,
           "clean performance sample executed avoided ray work");
      reuse_ms.push_back(sample.render_ms);
    }
    need(renderer.stats().frame_hash==performance_hash,"performance reuse changed image");

    const double full_median=median(full_ms),reuse_median=median(reuse_ms);
    std::cout<<"{\"suite\":\"regraphics_c02_dirty_tiles\","
             <<"\"checks\":"<<checks
             <<",\"frame_hash\":"<<renderer.stats().frame_hash
             <<",\"tiles\":"<<total_tiles
             <<",\"pixels\":"<<total_pixels
             <<",\"full_candidate_tests\":"<<full_tests
             <<",\"local_rendered_tiles\":"<<local.rendered_tiles
             <<",\"local_reused_tiles\":"<<local.reused_tiles
             <<",\"local_ray_queries\":"<<local.ray_queries
             <<",\"local_candidate_tests\":"<<local.candidate_tests
             <<",\"full_median_ms\":"<<full_median
             <<",\"clean_reuse_median_ms\":"<<reuse_median
             <<",\"median_ratio\":"<<(full_median>0?reuse_median/full_median:0.)
             <<"}\n";
    return 0;
  }catch(const std::exception& e){
    std::cerr<<"REGRAPHICS_C02_FAIL "<<e.what()<<'\n';
    return 1;
  }
}
