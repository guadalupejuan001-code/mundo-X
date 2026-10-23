#include "mundo_x/ether_world.hpp"
#include "mundo_x/ether_world_renderer.hpp"

#include <cmath>
#include <cstddef>
#include <fstream>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

static std::size_t checks=0;
static void need(bool ok,const char* explanation) {
  ++checks;
  if(!ok)throw std::runtime_error(explanation);
}
template<class Action>
static void rejects(Action action,const char* explanation){
  ++checks;
  try{action();}catch(const std::exception&){return;}
  throw std::runtime_error(explanation);
}
static void write_ppm(const std::string& file,
                      const std::vector<std::uint32_t>& image,int width,int height) {
  if(file.empty())return;
  if(image.size()!=static_cast<std::size_t>(width*height))
    throw std::runtime_error("PPM dimensions incorrect");
  std::ofstream out(file,std::ios::binary);
  if(!out)throw std::runtime_error("preview image could not be created");
  out<<"P6\\n"<<width<<' '<<height<<"\\n255\\n";
  for(const auto pixel:image) {
    const char rgb[]={static_cast<char>((pixel>>16)&0xff),
                      static_cast<char>((pixel>>8)&0xff),
                      static_cast<char>(pixel&0xff)};
    out.write(rgb,sizeof(rgb));
  }
  if(!out)throw std::runtime_error("PPM write failed");
}

int main(int argc,char** argv) {
  try {
    using World=mundo_x::EtherWorld;
    using Render=mundo_x::EtherWorldRenderer;
    World world(1000);
    need(world.rules().count()==1048576,"Mundo X did not preserve million logical cells");
    need(World::id(-512,-512)==0,"negative world mapping wrong");
    need(World::id(511,511)==1048575,"last world position mapping wrong");
    need(world.stats().edits==0,"world should not materialize cells to initialize");
    need(world.stats().scans==1&&world.stats().explored>100,
         "initial observed region not mapped");
    need(world.stats().quantum_active>=2,
         "canonical Ether quantum state did not participate");
    need(world.simulated_quantum_field()==0.,"initial quantum bit not prepared |0>");
    auto distant=world.cell(511,511);
    need(!distant.revealed,"a region outside the first scan should stay hidden");
    need(world.stats().edits==0,
         "reading a distant logical cell materialized the world");

    const auto initial=world.cell(0,0);
    need(initial.revealed,"origin was not scanned");
    world.edit(0,0,1);
    need(world.cell(0,0).edit==1&&world.stats().edits==1,
         "Ether COW must be the sole authority of a spatial edit");
    need(world.cell(1,0).edit==0,"neighbor edit bled across logical addresses");
    rejects([&] {world.edit(50,50,2);},
            "unobserved cell modification succeeded");
    rejects([&] {world.move(7,0);},"unbounded movement should be rejected");

    Render display;
    const auto before=display.render(world,true);
    need(before.renderer.exact_mismatches==0,
         "Ether Renderer X culling does not match exact oracle");
    need(before.view.size()==Render::kWidth*Render::kHeight,
         "viewport dimensions changed");
    need(before.map.size()==Render::kMapWidth*Render::kMapWidth,
         "topological map dimensions changed");
    need(before.renderer.candidate_tests<=before.renderer.brute_tests,
         "region planning did more sphere intersection tests");
    need(before.renderer.bodies>0,"the observed world has no actual rendered bodies");
    need(before.visible_cells>100,"floor tile cache isn't revealing the mapped area");
    const auto baseline=before.renderer.frame_hash;

    world.emit_pulse();
    need(world.simulated_quantum_field()==1.0,
         "simulated Ether q2 operation did not affect the spatial field");
    need(world.wave(0,0)>.99,"an emitted energy wave did not affect its origin");
    const auto pulse=display.render(world,false);
    need(pulse.view!=before.view,"the quantum/wave event had no visual effect");
    world.step(6);
    need(world.stats().ticks==6,"time did not advance");
    need(world.stats().probes==6,"autonomous probes disappeared");
    need(world.stats().pulses==1,"pulse disappeared too early");
    const auto previous=world.physics().bodies().front().position;
    world.step(16);
    const auto following=world.physics().bodies().front().position;
    need(std::hypot(previous.x-following.x,previous.z-following.z)>1e-6,
         "native RulePhysics probe did not evolve");
    need(world.stats().ticks==22,"physics tick accounting failed");

    world.move(6,0);
    const auto gained=world.scan_center();
    need(gained>0&&world.stats().explored>before.visible_cells,
         "mapping a new region did not expand the observed world");
    world.select(6,0);
    world.edit_selected(4);
    need(world.cell(6,0).edit==4,"selected cell edit not committed");
    world.orbit(.28);
    world.magnify(-1);
    const auto saved=world.save();
    need(saved.size()<World::kMaxSaveBytes,"compact save exceeded budget");
    auto recovered=World::load(saved);
    need(recovered->save()==saved,"world snapshot lost exact state/observer data");
    need(recovered->stats().edits==world.stats().edits,
         "Ether memory was not restored");
    need(recovered->simulated_quantum_field()==world.simulated_quantum_field(),
         "quantum background was not restored");
    need(recovered->cell(6,0).edit==4,"mapped cell was not persisted");
    const auto a=display.render(world,true);
    Render duplicate;
    const auto b=duplicate.render(*recovered,true);
    need(a.renderer.exact_mismatches==0&&b.renderer.exact_mismatches==0,
         "oracle found a renderer regression after restore");
    need(a.view==b.view&&a.map==b.map,
         "restored scene no longer maps to identical pixels");
    rejects([&] {World::load(saved+"malicious trailing data");},
            "corrupt snapshot was accepted");

    const auto training=display.optimize();
    need(training.exact_mismatches==0,"training changed Ether Renderer exactness");
    need(training.chosen_tile>=8&&training.chosen_tile<=64,
         "invalid calibrated tile");
    const auto post_training=display.render(world,false);
    need(post_training.renderer.exact_mismatches==0,
         "optimized renderer returned mismatched pixels");

    world.edit(6,0,0);
    need(world.cell(6,0).edit==0,"restored base rule did not clear override");
    world.origin();
    world.select(0,0);
    world.emit_pulse();
    need(world.simulated_quantum_field()==0.,"second Ether X did not restore Z0");
    need(world.rules().logical_rule_bytes()==32,"base generated rule became a million-cell array");
    if(argc>=3) {
      const auto preview=display.render(world,false);
      write_ppm(argv[1],preview.view,Render::kWidth,Render::kHeight);
      write_ppm(argv[2],preview.map,Render::kMapWidth,Render::kMapWidth);
    }
    std::cout<<"ETHER_WORLD_NATIVE_PASS checks="<<checks
             <<" logical_cells="<<world.rules().count()
             <<" ether_overrides="<<world.stats().edits
             <<" observed="<<world.stats().explored
             <<" render_oracle_mismatches="<<a.renderer.exact_mismatches
             <<" tile="<<training.chosen_tile
             <<" frame_hash="<<baseline
             <<"\n";
    return 0;
  }catch(const std::exception& e){
    std::cerr<<"ETHER_WORLD_NATIVE_FAILURE "<<e.what()<<'\n';
    return 1;
  }
}
