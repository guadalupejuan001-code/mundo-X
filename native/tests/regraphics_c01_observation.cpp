#include "mundo_x/ether_renderer.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <vector>

static std::size_t checks=0;
static void need(bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(message);}

static double median(std::vector<double> values){
  std::sort(values.begin(),values.end());
  return values[values.size()/2];
}

int main(){
  try{
    using R=mundo_x::EtherRenderer;
    R renderer(320,180,32);
    renderer.set_camera({0,0,12},{0,0,0},47);
    renderer.add_body({0,{0,0,0},2.1,{{.2,.6,1.}}});
    renderer.add_body({1,{3.2,.3,-2.5},1.4,{{1.,.45,.1}}});
    renderer.add_body({2,{-3.8,-.5,-5.},1.8,{{.55,.8,.3}}});
    renderer.add_body({3,{0,0,-8.},3.0,{{.8,.2,.7}}});
    renderer.plan();

    const auto baseline=renderer.render(true);
    need(!baseline.observation_enabled,"observation must be opt-in");
    need(baseline.exact_mismatches==0,"baseline differs from brute-force oracle");
    const auto reference_hash=baseline.frame_hash;

    renderer.set_observation_enabled(true);
    const auto first=renderer.render(true);
    need(first.observation_enabled,"observation flag missing");
    need(first.frame_hash==reference_hash,"observation changed the rendered frame");
    need(first.exact_mismatches==0,"observed render differs from brute-force oracle");
    need(first.pixel_queries==first.width*first.height,"pixel query count mismatch");
    need(first.visible_pixels+first.background_pixels==first.pixel_queries,"pixel visibility partition mismatch");
    need(first.changed_pixels==0,"identical first observed frame should reuse the existing baseline frame");
    need(first.unchanged_pixels==first.pixel_queries,"unchanged pixel count mismatch");
    need(first.changed_tiles==0,"identical first observed frame should not dirty tiles");
    need(first.unchanged_tiles==first.tiles_x*first.tiles_y,"unchanged tile count mismatch");

    std::uint64_t map_pixels=0,map_visible=0,map_changed=0,map_tests=0,map_hits=0,map_occluded=0,map_pruned=0;
    for(const auto& tile:renderer.tile_observations()){
      need(tile.width>0&&tile.height>0,"tile has empty dimensions");
      need(tile.pixels==tile.width*tile.height,"tile pixel area mismatch");
      need(tile.candidates<=renderer.bodies().size(),"tile candidates exceed bodies");
      map_pixels+=tile.pixels;map_visible+=tile.visible_pixels;map_changed+=tile.changed_pixels;
      map_tests+=tile.candidate_tests;map_hits+=tile.intersection_hits;
      map_occluded+=tile.occluded_intersections;map_pruned+=tile.distance_pruned_candidates;
    }
    need(map_pixels==first.pixel_queries,"tile map does not cover framebuffer");
    need(map_visible==first.visible_pixels,"tile visible pixel sum mismatch");
    need(map_changed==first.changed_pixels,"tile changed pixel sum mismatch");
    need(map_tests==first.candidate_tests,"tile candidate test sum mismatch");
    need(map_hits==first.intersection_hits,"tile intersection hit sum mismatch");
    need(map_occluded==first.occluded_intersections,"tile occluded intersection sum mismatch");
    need(map_pruned==first.distance_pruned_candidates,"tile pruned candidate sum mismatch");

    renderer.set_quantum_material(0,.8,.5);
    const auto material_change=renderer.render(true);
    need(material_change.frame_hash!=reference_hash,"material change did not alter frame");
    need(material_change.changed_pixels>0,"material change produced no changed pixels");
    need(material_change.changed_pixels<material_change.pixel_queries,"material change dirtied the entire frame");
    need(material_change.changed_tiles>0,"material change produced no dirty tiles");
    need(material_change.changed_tiles<=material_change.tiles_x*material_change.tiles_y,"dirty tile count out of range");
    need(material_change.intersection_hits>=material_change.visible_pixels,"visible pixels exceed exact intersection hits");
    need(material_change.intersection_overdraw_proxy()>=0.,"intersection visibility proxy invalid");

    const auto settled=renderer.render(true);
    need(settled.changed_pixels==0&&settled.changed_tiles==0,"settled frame still reports changes");
    need(settled.unchanged_pixels==settled.pixel_queries,"settled frame reuse count mismatch");

    std::vector<double> baseline_ms,observed_ms;
    renderer.set_observation_enabled(false);
    renderer.render(false);
    for(int i=0;i<7;++i)baseline_ms.push_back(renderer.render(false).render_ms);
    const auto hash_without_observer=renderer.stats().frame_hash;
    renderer.set_observation_enabled(true);
    renderer.render(false);
    for(int i=0;i<7;++i)observed_ms.push_back(renderer.render(false).render_ms);
    const auto hash_with_observer=renderer.stats().frame_hash;
    need(hash_without_observer==hash_with_observer,"observer timing path changed framebuffer hash");
    need(renderer.tile_observations().size()==renderer.stats().tiles_x*renderer.stats().tiles_y,
         "observer tile map size mismatch");

    const double base=median(baseline_ms),observed=median(observed_ms);
    need(std::isfinite(base)&&std::isfinite(observed)&&base>=0.&&observed>=0.,"invalid render timing");
    const double overhead_ms=observed-base;
    const double overhead_ratio=base>0.?observed/base:0.;

    std::cout<<"{\"suite\":\"regraphics_c01_pixel_observation\","
             <<"\"checks\":"<<checks
             <<",\"frame_hash\":"<<hash_with_observer
             <<",\"pixels\":"<<renderer.stats().pixel_queries
             <<",\"tiles\":"<<renderer.stats().tiles_x*renderer.stats().tiles_y
             <<",\"visible_pixels\":"<<renderer.stats().visible_pixels
             <<",\"intersection_hits\":"<<renderer.stats().intersection_hits
             <<",\"occluded_intersections\":"<<renderer.stats().occluded_intersections
             <<",\"distance_pruned_candidates\":"<<renderer.stats().distance_pruned_candidates
             <<",\"intersection_overdraw_proxy\":"<<renderer.stats().intersection_overdraw_proxy()
             <<",\"baseline_median_ms\":"<<base
             <<",\"observed_median_ms\":"<<observed
             <<",\"observer_overhead_ms\":"<<overhead_ms
             <<",\"observer_overhead_ratio\":"<<overhead_ratio
             <<"}\n";
    return 0;
  }catch(const std::exception& e){
    std::cerr<<"REGRAPHICS_C01_FAIL "<<e.what()<<'\n';
    return 1;
  }
}
