#include "mundo_x/ether_renderer.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

static std::size_t checks=0;
static void need(bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(message);}

static std::uint32_t h32(std::uint32_t x){
  x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;x^=x>>16;return x;
}
static double rnd(std::uint32_t x){return static_cast<double>(h32(x)&0x00ffffffu)/16777215.;}

static std::vector<mundo_x::EtherRenderer::EffectParticle> dense(std::size_t count){
  using P=mundo_x::EtherRenderer::EffectParticle;
  std::vector<P> out;out.reserve(count);
  for(std::uint32_t id=0;id<count;++id){
    const double a=rnd(id*11u+1u),b=rnd(id*11u+2u);
    const double radius=2.0+6.0*rnd(id*11u+3u);
    const double alpha=.22+.72*rnd(id*11u+4u);
    out.push_back({id,a,b,radius,alpha,id%3u});
  }
  return out;
}

int main(){
  try{
    using R=mundo_x::EtherRenderer;
    R renderer(320,180,16);

    auto particles=dense(96000);
    renderer.set_effect_particles(particles,1);
    const auto baseline=renderer.plan_effects(false,false);
    need(baseline.particles==96000,"dense effect particle count mismatch");
    need(baseline.visible_particles>95000,"unexpected dense effect culling");
    need(baseline.active_tiles==240,"dense effects did not cover every tile");
    need(baseline.changed_tiles==baseline.active_tiles,"baseline did not schedule every active tile");
    need(baseline.reused_tiles==0,"baseline unexpectedly reused effect tiles");
    need(baseline.particles_scanned==96000,"baseline did not scan all effects");
    need(baseline.submitted_particle_references>96000,"tile references should conservatively overlap particles");
    need(baseline.full_resolution_samples==320u*180u,"full-resolution effect surface area mismatch");
    need(baseline.scheduled_samples==baseline.full_resolution_samples,"non-adaptive baseline changed sample resolution");

    const auto identical=renderer.plan_effects(false,true);
    need(identical.revision_fast_path,"same effect revision missed temporal fast path");
    need(identical.particles_scanned==0,"same revision rescanned effect particles");
    need(identical.changed_tiles==0,"same revision dirtied effect tiles");
    need(identical.reused_tiles==baseline.active_tiles,"same revision did not reuse all active effect tiles");
    need(identical.submitted_particle_references==0,"same revision resubmitted particles");
    need(identical.material_batches==0,"same revision scheduled effect batches");
    need(identical.full_resolution_samples==0&&identical.scheduled_samples==0,
         "same revision scheduled effect pixel work");

    auto moved=particles;
    moved[0].screen_x=std::min(.99,moved[0].screen_x+.045);
    moved[0].screen_y=std::min(.99,moved[0].screen_y+.035);
    renderer.set_effect_particles(moved,2);
    const auto local=renderer.plan_effects(false,true);
    need(!local.revision_fast_path,"new effect revision used stale fast path");
    need(local.particles_scanned==96000,"new revision did not rescan effect input");
    need(local.changed_tiles>0&&local.changed_tiles<baseline.active_tiles,
         "single moved particle did not remain tile-local");
    need(local.reused_tiles>0,"single moved particle reused no effect tiles");
    need(local.submitted_particle_references>0&&
         local.submitted_particle_references<baseline.submitted_particle_references,
         "localized effect change did not reduce submission work");
    need(local.full_resolution_samples<baseline.full_resolution_samples,
         "localized effect change scheduled a full effect surface");
    std::size_t local_changed=0;
    for(const auto& tile:renderer.effect_tile_plan())if(tile.changed)++local_changed;
    need(local_changed==local.changed_tiles,"effect tile plan/global changed count mismatch");

    const auto local_settled=renderer.plan_effects(false,true);
    need(local_settled.revision_fast_path&&local_settled.particles_scanned==0,
         "settled localized revision did not hit fast path");
    need(local_settled.changed_tiles==0&&local_settled.reused_tiles==baseline.active_tiles,
         "settled localized revision did not fully reuse");

    const auto adaptive=renderer.plan_effects(true,true);
    need(!adaptive.revision_fast_path,"adaptive policy switch incorrectly reused exact plan");
    need(adaptive.active_tiles==baseline.active_tiles,"adaptive policy changed active tile coverage");
    need(adaptive.scheduled_samples<adaptive.full_resolution_samples,
         "adaptive effect policy reduced no scheduled samples");
    need(adaptive.scheduled_sample_ratio()<.40,"dense effect policy did not materially reduce sample work");
    std::size_t quarter=0,half=0,full=0;
    for(const auto& tile:renderer.effect_tile_plan())if(tile.active){
      if(tile.resolution_divisor==4)++quarter;
      else if(tile.resolution_divisor==2)++half;
      else if(tile.resolution_divisor==1)++full;
      else need(false,"invalid effect resolution divisor");
    }
    need(quarter>0,"dense effects selected no quarter-resolution tiles");
    need(quarter+half+full==adaptive.active_tiles,"adaptive tile density partition mismatch");

    const auto adaptive_settled=renderer.plan_effects(true,true);
    need(adaptive_settled.revision_fast_path,"stable adaptive effects missed revision fast path");
    need(adaptive_settled.changed_tiles==0&&adaptive_settled.particles_scanned==0,
         "stable adaptive effects still scheduled planning work");

    std::vector<R::EffectParticle> sparse;
    for(std::uint32_t i=0;i<12;++i)
      sparse.push_back({100000u+i,.08+.07*i,.15+.025*(i%4),2.5,.65,i%2u});
    sparse.push_back({200000u,-2.,-2.,3.,.8,0});
    sparse.push_back({200001u,.5,.5,3.,0.,0});
    renderer.set_effect_particles(sparse,3);
    const auto sparse_plan=renderer.plan_effects(true,true);
    need(sparse_plan.visible_particles==12&&sparse_plan.culled_particles==2,
         "sparse effect visibility/culling mismatch");
    need(sparse_plan.active_tiles>0&&sparse_plan.active_tiles<240,
         "sparse effects unexpectedly cover full screen");
    need(sparse_plan.cleared_tiles>0,"dense-to-sparse transition did not clear old effect tiles");
    for(const auto& tile:renderer.effect_tile_plan())if(tile.active)
      need(tile.resolution_divisor==1,"sparse effect tile should stay full resolution");

    bool bad=false;
    try{renderer.set_effect_particles(sparse,3);}catch(const std::invalid_argument&){bad=true;}
    need(bad,"non-increasing effect revision accepted");
    bad=false;
    try{
      auto duplicate=sparse;duplicate.push_back(duplicate.front());
      renderer.set_effect_particles(duplicate,4);
    }catch(const std::invalid_argument&){bad=true;}
    need(bad,"duplicate effect particle id accepted");

    renderer.set_effect_particles(particles,5);
    std::vector<double> full_ms,fast_ms;
    for(int i=0;i<7;++i){
      const auto sample=renderer.plan_effects(false,false);
      full_ms.push_back(sample.plan_ms);
    }
    renderer.plan_effects(false,false);
    for(int i=0;i<7;++i){
      const auto sample=renderer.plan_effects(false,true);
      need(sample.revision_fast_path&&sample.particles_scanned==0,
           "temporal benchmark fast path scanned particles");
      fast_ms.push_back(sample.plan_ms);
    }
    std::sort(full_ms.begin(),full_ms.end());std::sort(fast_ms.begin(),fast_ms.end());
    const double full_median=full_ms[full_ms.size()/2],fast_median=fast_ms[fast_ms.size()/2];

    std::cout<<"{\"suite\":\"regraphics_c03_temporal_effects\","
             <<"\"checks\":"<<checks
             <<",\"particles\":"<<baseline.particles
             <<",\"tiles\":"<<baseline.active_tiles
             <<",\"baseline_tile_refs\":"<<baseline.submitted_particle_references
             <<",\"local_changed_tiles\":"<<local.changed_tiles
             <<",\"local_reused_tiles\":"<<local.reused_tiles
             <<",\"local_submitted_refs\":"<<local.submitted_particle_references
             <<",\"adaptive_full_samples\":"<<adaptive.full_resolution_samples
             <<",\"adaptive_scheduled_samples\":"<<adaptive.scheduled_samples
             <<",\"adaptive_ratio\":"<<adaptive.scheduled_sample_ratio()
             <<",\"quarter_tiles\":"<<quarter
             <<",\"full_plan_median_ms\":"<<full_median
             <<",\"revision_fast_median_ms\":"<<fast_median
             <<"}\n";
    return 0;
  }catch(const std::exception& e){
    std::cerr<<"REGRAPHICS_C03_FAIL "<<e.what()<<'\n';
    return 1;
  }
}
