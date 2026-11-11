#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>

namespace mundo_x {

class EtherRenderer {
 public:
  struct Vec3 { double x=0.,y=0.,z=0.; };
  struct Body {
    std::uint32_t id=0;
    Vec3 center{};
    double radius=1.;
    std::array<double,3> color{{1.,1.,1.}};
  };
  struct Pixel {
    std::int32_t body=-1;
    double depth=std::numeric_limits<double>::infinity();
    std::uint32_t rgba=0xff000000u;
    bool operator==(const Pixel& other) const {
      return body==other.body && rgba==other.rgba &&
             ((std::isinf(depth)&&std::isinf(other.depth))||depth==other.depth);
    }
  };
  struct TileObservation {
    std::size_t tile_x=0,tile_y=0,pixel_x=0,pixel_y=0,width=0,height=0;
    std::size_t candidates=0;
    std::uint64_t pixels=0,visible_pixels=0,changed_pixels=0;
    std::uint64_t candidate_tests=0,distance_pruned_candidates=0;
    std::uint64_t intersection_hits=0,occluded_intersections=0;
    std::uint64_t ray_queries=0,reused_pixels=0;
    bool reused=false;
    bool changed() const noexcept{return changed_pixels!=0;}
  };
  struct Stats {
    std::size_t width=0,height=0,tile=0,tiles_x=0,tiles_y=0,bodies=0;
    std::uint64_t candidate_references=0,brute_references=0;
    std::uint64_t candidate_tests=0,brute_tests=0;
    std::uint64_t distance_pruned_candidates=0;
    std::size_t max_candidates=0,nonempty_regions=0;
    std::size_t exact_mismatches=0;
    std::uint64_t frame_hash=1469598103934665603ULL;
    bool observation_enabled=false,tile_reuse_enabled=false;
    std::uint64_t pixel_queries=0,visible_pixels=0,background_pixels=0;
    std::uint64_t ray_queries=0,rendered_pixels=0,reused_pixels=0;
    std::uint64_t changed_pixels=0,unchanged_pixels=0;
    std::size_t changed_tiles=0,unchanged_tiles=0;
    std::size_t rendered_tiles=0,reused_tiles=0;
    std::uint64_t intersection_hits=0,occluded_intersections=0;
    double render_ms=0.;
    double candidate_ratio() const {
      return brute_references?static_cast<double>(candidate_references)/brute_references:0.;
    }
    double test_ratio() const {
      return brute_tests?static_cast<double>(candidate_tests)/brute_tests:0.;
    }
    double changed_tile_ratio() const {
      const auto total=changed_tiles+unchanged_tiles;
      return total?static_cast<double>(changed_tiles)/static_cast<double>(total):0.;
    }
    // CPU ray/intersection visibility proxy only. It is deliberately not
    // labelled as physical GPU fragment overdraw.
    double intersection_overdraw_proxy() const {
      return ray_queries?static_cast<double>(occluded_intersections)/
          static_cast<double>(ray_queries):0.;
    }
  };
  struct EffectParticle {
    std::uint32_t id=0;
    double screen_x=.5,screen_y=.5,radius_px=1.,alpha=1.;
    std::uint32_t material=0;
  };
  struct EffectTilePlan {
    std::size_t tile_x=0,tile_y=0,pixel_x=0,pixel_y=0,width=0,height=0;
    std::uint64_t signature=0;
    std::uint64_t particle_references=0;
    std::uint64_t material_mask=0;
    std::uint32_t material_batches=0;
    std::uint8_t resolution_divisor=1;
    bool active=false,changed=false,reused=false,clear_only=false;
  };
  struct EffectStats {
    std::uint64_t revision=0;
    std::size_t particles=0,visible_particles=0,culled_particles=0;
    std::size_t active_tiles=0,changed_tiles=0,reused_tiles=0,cleared_tiles=0;
    std::uint64_t particles_scanned=0,tile_particle_references=0;
    std::uint64_t submitted_particle_references=0,material_batches=0;
    std::uint64_t full_resolution_samples=0,scheduled_samples=0;
    bool adaptive_resolution=false,temporal_reuse=false,revision_fast_path=false;
    double plan_ms=0.;
    double scheduled_sample_ratio() const {
      return full_resolution_samples?
          static_cast<double>(scheduled_samples)/static_cast<double>(full_resolution_samples):0.;
    }
  };
  struct TrainingResult {
    std::size_t chosen_tile=0;
    double median_ms=0.;
    std::uint64_t candidate_tests=0;
    std::uint64_t distance_pruned_candidates=0;
    std::uint64_t frame_hash=0;
    std::size_t exact_mismatches=0;
  };

  EtherRenderer(std::size_t width,std::size_t height,std::size_t tile)
      : width_(width),height_(height),tile_(tile) {
    if(width<1||height<1||width>8192||height>8192)
      throw std::invalid_argument("render dimensions must be 1..8192");
    if(tile<8||tile>1024)throw std::invalid_argument("render tile must be 8..1024");
    tiles_x_=1+(width_-1)/tile_;tiles_y_=1+(height_-1)/tile_;
    masks_.assign(tiles_x_*tiles_y_,0);
    dirty_tiles_.assign(masks_.size(),1);
    frame_.assign(width_*height_,Pixel{});
    quantum_gain_.fill(1.);
    set_camera({0,0,10},{0,0,0},47.);
  }

  void set_camera(Vec3 eye,Vec3 target,double fov_deg) {
    if(!finite(eye)||!finite(target)||!std::isfinite(fov_deg)||fov_deg<=1.||fov_deg>=170.)
      throw std::invalid_argument("invalid render camera");
    eye_=eye;forward_=sub(target,eye);
    const double flen=length(forward_);
    if(!(flen>1e-12))throw std::invalid_argument("camera eye equals target");
    forward_=mul(forward_,1./flen);
    Vec3 world_up{0,1,0};
    if(std::abs(dot(forward_,world_up))>.999)world_up={0,0,1};
    right_=normalize(cross(forward_,world_up));
    up_=normalize(cross(right_,forward_));
    tan_half_fov_=std::tan(fov_deg*std::acos(-1.)/360.);
    aspect_=static_cast<double>(width_)/static_cast<double>(height_);
    mark_all_tiles_dirty();dirty_=true;
  }

  void add_body(const Body& body) {
    if(body.id>=64)throw std::invalid_argument("render body id must be 0..63");
    if(!finite(body.center)||!std::isfinite(body.radius)||!(body.radius>0.))
      throw std::invalid_argument("invalid render body geometry");
    for(double c:body.color)if(!std::isfinite(c)||c<0.||c>1.)
      throw std::invalid_argument("render body color must be 0..1");
    for(const auto& existing:bodies_)if(existing.id==body.id)
      throw std::invalid_argument("duplicate render body id");
    if(bodies_.size()>=64)throw std::length_error("render supports at most 64 bodies");
    bodies_.push_back(body);
    std::sort(bodies_.begin(),bodies_.end(),[](const Body& a,const Body& b){return a.id<b.id;});
    mark_all_tiles_dirty();dirty_=true;
  }

  void clear_bodies(){bodies_.clear();quantum_gain_.fill(1.);mark_all_tiles_dirty();dirty_=true;}

  bool has_body(std::uint32_t id) const noexcept {
    return std::any_of(bodies_.begin(),bodies_.end(),
                       [id](const Body& body){return body.id==id;});
  }

  // Visual material only. Neither quantum measurement nor changes to geometry,
  // candidate masks, intersection order, or the authoritative MIL state.
  // Values are prepared once per state revision, never inside the pixel loop.
  void set_quantum_material(std::uint32_t id,double expectation,double strength) {
    if(id>=quantum_gain_.size()||!has_body(id))
      throw std::out_of_range("quantum material body does not exist");
    if(!std::isfinite(expectation)||expectation < -1.-1e-8||expectation > 1.+1e-8||
       !std::isfinite(strength)||strength<0.||strength>1.)
      throw std::invalid_argument("invalid quantum material signal or strength");
    const double next=1.+strength*std::clamp(expectation,-1.,1.);
    if(quantum_gain_[id]==next)return;
    quantum_gain_[id]=next;
    mark_body_tiles_dirty(id);
  }

  void clear_quantum_materials() noexcept {
    bool changed=false;
    for(double gain:quantum_gain_)if(gain!=1.){changed=true;break;}
    quantum_gain_.fill(1.);
    if(changed)mark_all_tiles_dirty();
  }

  void set_tile(std::size_t tile) {
    if(tile<8||tile>1024)throw std::invalid_argument("render tile must be 8..1024");
    tile_=tile;tiles_x_=1+(width_-1)/tile_;tiles_y_=1+(height_-1)/tile_;
    masks_.assign(tiles_x_*tiles_y_,0);dirty_tiles_.assign(masks_.size(),1);
    dirty_=true;planned_=false;
  }

  const std::vector<Body>& bodies() const noexcept{return bodies_;}
  const Stats& stats() const noexcept{return stats_;}
  const std::vector<Pixel>& frame() const noexcept{return frame_;}
  const std::vector<TileObservation>& tile_observations() const noexcept{return tile_observations_;}
  void set_observation_enabled(bool enabled) noexcept {
    observation_enabled_=enabled;
    if(!enabled)tile_observations_.clear();
  }
  bool observation_enabled() const noexcept{return observation_enabled_;}

  void set_effect_particles(std::vector<EffectParticle> particles,std::uint64_t revision) {
    if(revision==0||revision<=effect_revision_)
      throw std::invalid_argument("effect revision must increase and be nonzero");
    for(const auto& p:particles){
      if(!std::isfinite(p.screen_x)||!std::isfinite(p.screen_y)||
         !std::isfinite(p.radius_px)||p.radius_px<=0.||
         !std::isfinite(p.alpha)||p.alpha<0.||p.alpha>1.||p.material>=64)
        throw std::invalid_argument("invalid effect particle");
    }
    std::sort(particles.begin(),particles.end(),[](const EffectParticle& a,const EffectParticle& b){
      return a.id<b.id;
    });
    for(std::size_t i=1;i<particles.size();++i)
      if(particles[i-1].id==particles[i].id)
        throw std::invalid_argument("duplicate effect particle id");
    effect_particles_=std::move(particles);
    effect_revision_=revision;
  }

  void clear_effect_particles(std::uint64_t revision) {
    set_effect_particles({},revision);
  }

  const std::vector<EffectTilePlan>& effect_tile_plan() const noexcept{return effect_tile_plan_;}

  EffectStats plan_effects(bool adaptive_resolution=false,bool temporal_reuse=true) {
    const auto begin=std::chrono::steady_clock::now();
    EffectStats out;
    out.revision=effect_revision_;out.particles=effect_particles_.size();
    out.adaptive_resolution=adaptive_resolution;out.temporal_reuse=temporal_reuse;

    const bool same_revision=effect_plan_valid_&&planned_effect_revision_==effect_revision_&&
        planned_effect_adaptive_==adaptive_resolution;
    if(temporal_reuse&&same_revision){
      out.revision_fast_path=true;
      for(auto& tile:effect_tile_plan_){
        tile.changed=false;tile.clear_only=false;
        tile.reused=tile.active;
        if(tile.active){++out.active_tiles;++out.reused_tiles;}
      }
      out.visible_particles=planned_visible_effect_particles_;
      out.culled_particles=out.particles-out.visible_particles;
      out.plan_ms=std::chrono::duration<double,std::milli>(
          std::chrono::steady_clock::now()-begin).count();
      return out;
    }

    std::vector<EffectTilePlan> next(masks_.size());
    for(std::size_t ty=0;ty<tiles_y_;++ty)for(std::size_t tx=0;tx<tiles_x_;++tx){
      const auto index=ty*tiles_x_+tx;
      auto& tile=next[index];
      tile.tile_x=tx;tile.tile_y=ty;tile.pixel_x=tx*tile_;
      tile.pixel_y=ty*tile_;tile.width=std::min(tile_,width_-tile.pixel_x);
      tile.height=std::min(tile_,height_-tile.pixel_y);
    }

    out.particles_scanned=effect_particles_.size();
    for(const auto& particle:effect_particles_){
      if(particle.alpha==0.){++out.culled_particles;continue;}
      const double cx=particle.screen_x*static_cast<double>(width_);
      const double cy=particle.screen_y*static_cast<double>(height_);
      const double r=particle.radius_px;
      if(cx+r<0.||cy+r<0.||cx-r>=static_cast<double>(width_)||
         cy-r>=static_cast<double>(height_)){
        ++out.culled_particles;continue;
      }
      ++out.visible_particles;
      int tx0=static_cast<int>(std::floor((cx-r)/static_cast<double>(tile_)));
      int tx1=static_cast<int>(std::floor((cx+r)/static_cast<double>(tile_)));
      int ty0=static_cast<int>(std::floor((cy-r)/static_cast<double>(tile_)));
      int ty1=static_cast<int>(std::floor((cy+r)/static_cast<double>(tile_)));
      tx0=std::max(0,tx0);ty0=std::max(0,ty0);
      tx1=std::min(static_cast<int>(tiles_x_)-1,tx1);
      ty1=std::min(static_cast<int>(tiles_y_)-1,ty1);
      const auto particle_hash=effect_particle_hash(particle);
      for(int ty=ty0;ty<=ty1;++ty)for(int tx=tx0;tx<=tx1;++tx){
        auto& tile=next[static_cast<std::size_t>(ty)*tiles_x_+static_cast<std::size_t>(tx)];
        tile.active=true;++tile.particle_references;
        tile.material_mask|=std::uint64_t{1}<<particle.material;
        tile.signature=effect_mix(tile.signature,particle_hash);
      }
    }

    if(previous_effect_signatures_.size()!=next.size()){
      previous_effect_signatures_.assign(next.size(),0);
      previous_effect_active_.assign(next.size(),0);
      effect_plan_valid_=false;
    }

    for(std::size_t index=0;index<next.size();++index){
      auto& tile=next[index];
      if(tile.active){
        tile.material_batches=static_cast<std::uint32_t>(popcount(tile.material_mask));
        if(adaptive_resolution){
          if(tile.particle_references>256)tile.resolution_divisor=4;
          else if(tile.particle_references>32)tile.resolution_divisor=2;
        }
        tile.signature=effect_mix(tile.signature,tile.material_mask);
        tile.signature=effect_mix(tile.signature,tile.resolution_divisor);
        ++out.active_tiles;
      }
      const bool was_active=index<previous_effect_active_.size()&&previous_effect_active_[index];
      const bool unchanged=effect_plan_valid_&&
          previous_effect_signatures_[index]==tile.signature&&was_active==tile.active;
      tile.reused=temporal_reuse&&unchanged&&tile.active;
      tile.changed=!temporal_reuse||!unchanged;
      tile.clear_only=tile.changed&&!tile.active&&was_active;
      if(tile.reused){++out.reused_tiles;continue;}
      if(!tile.changed)continue;
      if(tile.clear_only){++out.cleared_tiles;++out.changed_tiles;continue;}
      if(!tile.active)continue;
      ++out.changed_tiles;
      out.tile_particle_references+=tile.particle_references;
      out.submitted_particle_references+=tile.particle_references;
      out.material_batches+=tile.material_batches;
      const auto full=static_cast<std::uint64_t>(tile.width)*tile.height;
      out.full_resolution_samples+=full;
      const auto div=static_cast<std::size_t>(tile.resolution_divisor);
      const auto sw=(tile.width+div-1)/div,sh=(tile.height+div-1)/div;
      out.scheduled_samples+=static_cast<std::uint64_t>(sw)*sh;
    }

    previous_effect_signatures_.resize(next.size());
    previous_effect_active_.resize(next.size());
    for(std::size_t index=0;index<next.size();++index){
      previous_effect_signatures_[index]=next[index].signature;
      previous_effect_active_[index]=next[index].active?1:0;
    }
    effect_tile_plan_=std::move(next);
    effect_plan_valid_=true;planned_effect_revision_=effect_revision_;
    planned_effect_adaptive_=adaptive_resolution;
    planned_visible_effect_particles_=out.visible_particles;
    out.plan_ms=std::chrono::duration<double,std::milli>(
        std::chrono::steady_clock::now()-begin).count();
    return out;
  }

  void set_tile_reuse_enabled(bool enabled) noexcept { tile_reuse_enabled_=enabled; }
  bool tile_reuse_enabled() const noexcept{return tile_reuse_enabled_;}

  void plan() {
    std::fill(masks_.begin(),masks_.end(),std::uint64_t{0});
    rebuild_distance_order();
    for(const auto& body:bodies_)project_body(body);
    stats_=Stats{};
    stats_.width=width_;stats_.height=height_;stats_.tile=tile_;
    stats_.tiles_x=tiles_x_;stats_.tiles_y=tiles_y_;stats_.bodies=bodies_.size();
    for(const auto mask:masks_){
      const auto count=popcount(mask);
      stats_.candidate_references+=count;
      stats_.max_candidates=std::max(stats_.max_candidates,count);
      if(mask)++stats_.nonempty_regions;
    }
    stats_.brute_references=static_cast<std::uint64_t>(masks_.size())*bodies_.size();
    planned_=true;dirty_=false;
  }

  Stats render(bool compare_bruteforce=false) {
    if(dirty_||!planned_)plan();
    const auto render_begin=std::chrono::steady_clock::now();
    stats_.candidate_tests=0;stats_.brute_tests=0;stats_.exact_mismatches=0;
    stats_.distance_pruned_candidates=0;
    stats_.frame_hash=1469598103934665603ULL;
    stats_.observation_enabled=observation_enabled_;
    stats_.tile_reuse_enabled=tile_reuse_enabled_;
    stats_.pixel_queries=0;stats_.visible_pixels=0;stats_.background_pixels=0;
    stats_.ray_queries=0;stats_.rendered_pixels=0;stats_.reused_pixels=0;
    stats_.changed_pixels=0;stats_.unchanged_pixels=0;
    stats_.changed_tiles=0;stats_.unchanged_tiles=0;
    stats_.rendered_tiles=0;stats_.reused_tiles=0;
    stats_.intersection_hits=0;stats_.occluded_intersections=0;
    stats_.render_ms=0.;
    if(observation_enabled_){
      tile_observations_.assign(masks_.size(),TileObservation{});
      for(std::size_t ty=0;ty<tiles_y_;++ty)for(std::size_t tx=0;tx<tiles_x_;++tx){
        const auto index=ty*tiles_x_+tx;
        auto& observed=tile_observations_[index];
        observed.tile_x=tx;observed.tile_y=ty;
        observed.pixel_x=tx*tile_;observed.pixel_y=ty*tile_;
        observed.width=std::min(tile_,width_-observed.pixel_x);
        observed.height=std::min(tile_,height_-observed.pixel_y);
        observed.pixels=static_cast<std::uint64_t>(observed.width)*observed.height;
        observed.candidates=popcount(masks_[index]);
      }
    }else tile_observations_.clear();

    if(dirty_tiles_.size()!=masks_.size())dirty_tiles_.assign(masks_.size(),1);
    const bool have_previous=frame_valid_;
    for(std::size_t index=0;index<masks_.size();++index){
      const bool reused=tile_reuse_enabled_&&have_previous&&!dirty_tiles_[index];
      if(reused)++stats_.reused_tiles;else ++stats_.rendered_tiles;
      if(observation_enabled_)tile_observations_[index].reused=reused;
    }
    for(std::size_t y=0;y<height_;++y)for(std::size_t x=0;x<width_;++x){
      const auto tile_index=(y/tile_)*tiles_x_+(x/tile_);
      const bool reuse=tile_reuse_enabled_&&have_previous&&!dirty_tiles_[tile_index];
      const auto frame_index=y*width_+x;
      Pixel optimized;
      Vec3 ray{};
      if(reuse){
        optimized=frame_[frame_index];
        ++stats_.reused_pixels;
        if(observation_enabled_)++tile_observations_[tile_index].reused_pixels;
        if(compare_bruteforce)ray=ray_for(x,y);
      }else{
        ray=ray_for(x,y);
        ++stats_.ray_queries;++stats_.rendered_pixels;
        const auto mask=masks_[tile_index];
        const auto tests_before=stats_.candidate_tests;
        const auto pruned_before=stats_.distance_pruned_candidates;
        const auto hits_before=stats_.intersection_hits;
        const auto occluded_before=stats_.occluded_intersections;
        optimized=trace_ordered(
            ray,mask,&stats_.candidate_tests,&stats_.distance_pruned_candidates,
            observation_enabled_?&stats_.intersection_hits:nullptr,
            observation_enabled_?&stats_.occluded_intersections:nullptr);
        if(observation_enabled_){
          auto& observed=tile_observations_[tile_index];
          ++observed.ray_queries;
          observed.candidate_tests+=stats_.candidate_tests-tests_before;
          observed.distance_pruned_candidates+=stats_.distance_pruned_candidates-pruned_before;
          observed.intersection_hits+=stats_.intersection_hits-hits_before;
          observed.occluded_intersections+=stats_.occluded_intersections-occluded_before;
        }
      }
      if(observation_enabled_){
        ++stats_.pixel_queries;
        if(optimized.body>=0)++stats_.visible_pixels;else ++stats_.background_pixels;
        const bool changed=!have_previous||!(frame_[frame_index]==optimized);
        if(changed)++stats_.changed_pixels;else ++stats_.unchanged_pixels;
        auto& observed=tile_observations_[tile_index];
        if(optimized.body>=0)++observed.visible_pixels;
        if(changed)++observed.changed_pixels;
      }
      frame_[frame_index]=optimized;
      hash_pixel(optimized);
      if(compare_bruteforce){
        const Pixel brute=trace(ray,body_mask(),&stats_.brute_tests);
        if(!(optimized==brute))++stats_.exact_mismatches;
      }
    }
    if(observation_enabled_){
      for(const auto& observed:tile_observations_){
        if(observed.changed())++stats_.changed_tiles;else ++stats_.unchanged_tiles;
      }
    }
    frame_valid_=true;
    std::fill(dirty_tiles_.begin(),dirty_tiles_.end(),std::uint8_t{0});
    if(!compare_bruteforce)
      stats_.brute_tests=static_cast<std::uint64_t>(width_)*height_*bodies_.size();
    stats_.render_ms=std::chrono::duration<double,std::milli>(
        std::chrono::steady_clock::now()-render_begin).count();
    return stats_;
  }

  TrainingResult train_tiles(const std::vector<std::size_t>& candidates={8,16,32,64,128,256}) {
    if(candidates.empty())throw std::invalid_argument("render training requires tile candidates");
    const bool saved_reuse=tile_reuse_enabled_;tile_reuse_enabled_=false;
    TrainingResult best;bool have=false;std::uint64_t reference_hash=0;
    for(const auto tile:candidates){
      set_tile(tile);plan();
      // One warm-up keeps allocation/cache setup outside the measured samples.
      render(false);
      std::array<double,3> samples{};
      std::uint64_t tests=0,pruned=0,hash=0;
      for(std::size_t repeat=0;repeat<samples.size();++repeat){
        const auto begin=std::chrono::steady_clock::now();
        const auto measured=render(false);
        const auto end=std::chrono::steady_clock::now();
        samples[repeat]=std::chrono::duration<double,std::milli>(end-begin).count();
        tests=measured.candidate_tests;pruned=measured.distance_pruned_candidates;hash=measured.frame_hash;
      }
      std::sort(samples.begin(),samples.end());
      if(!have)reference_hash=hash;
      else if(hash!=reference_hash)throw std::runtime_error("render training changed the frame across tile sizes");
      if(!have||samples[1]<best.median_ms||
         (samples[1]==best.median_ms&&tests<best.candidate_tests)){
        have=true;best.chosen_tile=tile;best.median_ms=samples[1];
        best.candidate_tests=tests;best.distance_pruned_candidates=pruned;best.frame_hash=hash;
      }
    }
    set_tile(best.chosen_tile);plan();
    const auto exact=render(true);
    best.exact_mismatches=exact.exact_mismatches;
    best.candidate_tests=exact.candidate_tests;
    best.distance_pruned_candidates=exact.distance_pruned_candidates;
    best.frame_hash=exact.frame_hash;
    if(best.exact_mismatches)throw std::runtime_error("trained render path differs from brute-force oracle");
    tile_reuse_enabled_=saved_reuse;
    return best;
  }

  const Pixel& pixel(std::size_t x,std::size_t y) const {
    if(x>=width_||y>=height_)throw std::out_of_range("render pixel outside framebuffer");
    return frame_[y*width_+x];
  }

  std::uint64_t mask_at(std::size_t x,std::size_t y) const {
    if(x>=width_||y>=height_)throw std::out_of_range("render mask query outside framebuffer");
    return masks_[(y/tile_)*tiles_x_+(x/tile_)];
  }

 private:
  static bool finite(const Vec3& v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
  static Vec3 sub(Vec3 a,Vec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
  static Vec3 add(Vec3 a,Vec3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
  static Vec3 mul(Vec3 a,double s){return {a.x*s,a.y*s,a.z*s};}
  static double dot(Vec3 a,Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
  static Vec3 cross(Vec3 a,Vec3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
  static double length(Vec3 a){return std::sqrt(dot(a,a));}
  static Vec3 normalize(Vec3 a){const double n=length(a);if(!(n>1e-15))throw std::invalid_argument("zero render vector");return mul(a,1./n);}
  static std::size_t popcount(std::uint64_t v){std::size_t n=0;while(v){v&=v-1;++n;}return n;}
  static std::uint64_t effect_mix(std::uint64_t hash,std::uint64_t value){
    if(hash==0)hash=1469598103934665603ULL;
    for(unsigned i=0;i<8;++i){hash^=(value>>(i*8))&0xffu;hash*=1099511628211ULL;}
    return hash;
  }
  static std::uint64_t effect_bits(double value){
    std::uint64_t bits=0;std::memcpy(&bits,&value,sizeof(bits));return bits;
  }
  static std::uint64_t effect_particle_hash(const EffectParticle& particle){
    std::uint64_t hash=0;
    hash=effect_mix(hash,particle.id);hash=effect_mix(hash,particle.material);
    hash=effect_mix(hash,effect_bits(particle.screen_x));
    hash=effect_mix(hash,effect_bits(particle.screen_y));
    hash=effect_mix(hash,effect_bits(particle.radius_px));
    hash=effect_mix(hash,effect_bits(particle.alpha));
    return hash;
  }
  void mark_all_tiles_dirty(){
    if(dirty_tiles_.size()!=masks_.size())dirty_tiles_.assign(masks_.size(),1);
    else std::fill(dirty_tiles_.begin(),dirty_tiles_.end(),std::uint8_t{1});
  }
  void mark_body_tiles_dirty(std::uint32_t id){
    if(!planned_||dirty_||dirty_tiles_.size()!=masks_.size()){mark_all_tiles_dirty();return;}
    const auto bit=std::uint64_t{1}<<id;
    for(std::size_t index=0;index<masks_.size();++index)
      if(masks_[index]&bit)dirty_tiles_[index]=1;
  }
  std::uint64_t body_mask() const {
    std::uint64_t mask=0;for(const auto& b:bodies_)mask|=std::uint64_t{1}<<b.id;return mask;
  }
  void fill(std::uint32_t id,int x0,int x1,int y0,int y1){
    const auto bit=std::uint64_t{1}<<id;
    x0=std::max(0,x0);y0=std::max(0,y0);
    x1=std::min(static_cast<int>(tiles_x_)-1,x1);
    y1=std::min(static_cast<int>(tiles_y_)-1,y1);
    if(x0>x1||y0>y1)return;
    for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x)masks_[static_cast<std::size_t>(y)*tiles_x_+x]|=bit;
  }
  void project_body(const Body& b){
    const Vec3 relative=sub(b.center,eye_);
    const double x=dot(relative,right_),y=dot(relative,up_),z=dot(relative,forward_),r=b.radius;
    constexpr double near=1e-6;
    if(z+r<=near)return;
    if(z<=r+near){fill(b.id,0,static_cast<int>(tiles_x_)-1,0,static_cast<int>(tiles_y_)-1);return;}
    const double zn=z-r,zf=z+r;
    const double ax=(x-r)/zn,bx=(x-r)/zf,cx=(x+r)/zn,dx=(x+r)/zf;
    const double ay=(y-r)/zn,by=(y-r)/zf,cy=(y+r)/zn,dy=(y+r)/zf;
    const double sx=tan_half_fov_*aspect_,sy=tan_half_fov_;
    const double margin_x=2.*static_cast<double>(tile_)/static_cast<double>(width_);
    const double margin_y=2.*static_cast<double>(tile_)/static_cast<double>(height_);
    double left=std::min(std::min(ax,bx),std::min(cx,dx))/sx-margin_x;
    double right=std::max(std::max(ax,bx),std::max(cx,dx))/sx+margin_x;
    double bottom=std::min(std::min(ay,by),std::min(cy,dy))/sy-margin_y;
    double top=std::max(std::max(ay,by),std::max(cy,dy))/sy+margin_y;
    if(right<-1.||left>1.||top<-1.||bottom>1.)return;
    left=std::max(-1.,left);right=std::min(1.,right);
    bottom=std::max(-1.,bottom);top=std::min(1.,top);
    const auto px0=(left*.5+.5)*static_cast<double>(width_);
    const auto px1=(right*.5+.5)*static_cast<double>(std::max<std::size_t>(1,width_-1));
    const auto py0=(bottom*.5+.5)*static_cast<double>(height_);
    const auto py1=(top*.5+.5)*static_cast<double>(std::max<std::size_t>(1,height_-1));
    fill(b.id,static_cast<int>(std::floor(px0/tile_)),static_cast<int>(std::floor(px1/tile_)),
              static_cast<int>(std::floor(py0/tile_)),static_cast<int>(std::floor(py1/tile_)));
  }
  Vec3 ray_for(std::size_t x,std::size_t y) const {
    const double nx=2.*(static_cast<double>(x)+.5)/static_cast<double>(width_)-1.;
    const double ny=2.*(static_cast<double>(y)+.5)/static_cast<double>(height_)-1.;
    return normalize(add(forward_,add(mul(right_,nx*aspect_*tan_half_fov_),mul(up_,ny*tan_half_fov_))));
  }
  Pixel shade(Pixel best,const Body* hit,const Vec3& ray) const {
    if(!hit){
      const double v=std::max(0.,std::min(1.,.025+.08*(ray.y*.5+.5)));
      const auto c=static_cast<std::uint32_t>(std::lround(v*255.));
      best.rgba=(255u<<24)|(c<<16)|(c<<8)|(std::min<std::uint32_t>(255,c+5));
      return best;
    }
    const Vec3 point=add(eye_,mul(ray,best.depth));
    const Vec3 normal=normalize(sub(point,hit->center));
    static const Vec3 light=normalize(Vec3{.37,.74,.56});
    const double l=.12+.88*std::max(0.,dot(normal,light));
    const double gain=quantum_gain_[hit->id];
    auto channel=[&](double c){return static_cast<std::uint32_t>(
        std::lround(std::max(0.,std::min(1.,c*l*gain))*255.));};
    const auto red=channel(hit->color[0]),green=channel(hit->color[1]),blue=channel(hit->color[2]);
    best.rgba=(255u<<24)|(red<<16)|(green<<8)|blue;
    return best;
  }

  Pixel trace(const Vec3& ray,std::uint64_t mask,std::uint64_t* tests) const {
    Pixel best;const Body* hit=nullptr;
    for(const auto& b:bodies_){
      if((mask&(std::uint64_t{1}<<b.id))==0)continue;
      if(tests)++*tests;
      const Vec3 c=sub(b.center,eye_);
      const double along=dot(c,ray);
      const double h2=b.radius*b.radius-(dot(c,c)-along*along);
      if(h2<0.)continue;
      const double root=std::sqrt(h2);
      double t=along-root;if(t<=1e-6)t=along+root;
      if(t<=1e-6||t>=best.depth)continue;
      best.depth=t;best.body=static_cast<std::int32_t>(b.id);hit=&b;
    }
    return shade(best,hit,ray);
  }
  void rebuild_distance_order(){
    distance_order_.clear();distance_lower_bound_.assign(bodies_.size(),0.);
    cached_center_.resize(bodies_.size());
    cached_center_squared_.resize(bodies_.size());
    cached_radius_squared_.resize(bodies_.size());
    distance_order_.reserve(bodies_.size());
    for(std::size_t i=0;i<bodies_.size();++i){
      const Vec3 offset=sub(bodies_[i].center,eye_);
      cached_center_[i]=offset;
      cached_center_squared_[i]=dot(offset,offset);
      cached_radius_squared_[i]=bodies_[i].radius*bodies_[i].radius;
      distance_lower_bound_[i]=std::max(0.,length(offset)-bodies_[i].radius);
      distance_order_.push_back(i);
    }
    std::stable_sort(distance_order_.begin(),distance_order_.end(),[&](std::size_t a,std::size_t b){
      return distance_lower_bound_[a]<distance_lower_bound_[b];
    });
  }
  Pixel trace_ordered(const Vec3& ray,std::uint64_t mask,std::uint64_t* tests,
                      std::uint64_t* pruned,std::uint64_t* hits=nullptr,
                      std::uint64_t* occluded=nullptr) const {
    Pixel best;const Body* hit=nullptr;std::size_t order_pos=0;
    for(;order_pos<distance_order_.size();++order_pos){
      const auto index=distance_order_[order_pos];
      if(best.body>=0&&distance_lower_bound_[index]>best.depth)break;
      const auto& b=bodies_[index];
      if((mask&(std::uint64_t{1}<<b.id))==0)continue;
      if(tests)++*tests;
      const Vec3& c=cached_center_[index];
      const double along=dot(c,ray);
      const double h2=cached_radius_squared_[index]-
          (cached_center_squared_[index]-along*along);
      if(h2<0.)continue;
      const double root=std::sqrt(h2);
      double t=along-root;if(t<=1e-6)t=along+root;
      if(t<=1e-6)continue;
      if(hits)++*hits;
      const auto id=static_cast<std::int32_t>(b.id);
      if(t>best.depth||(t==best.depth&&best.body>=0&&id>=best.body)){
        if(occluded)++*occluded;
        continue;
      }
      best.depth=t;best.body=id;hit=&b;
    }
    if(pruned&&best.body>=0){
      for(;order_pos<distance_order_.size();++order_pos){
        const auto index=distance_order_[order_pos];
        if(mask&(std::uint64_t{1}<<bodies_[index].id))++*pruned;
      }
    }
    return shade(best,hit,ray);
  }

  void hash_pixel(const Pixel& p){
    auto mix=[&](std::uint64_t value){for(unsigned i=0;i<8;++i){stats_.frame_hash^=(value>>(i*8))&0xffu;stats_.frame_hash*=1099511628211ULL;}};
    mix(static_cast<std::uint64_t>(static_cast<std::int64_t>(p.body)));mix(p.rgba);
  }

  std::size_t width_,height_,tile_,tiles_x_,tiles_y_;
  Vec3 eye_{},forward_{},right_{},up_{};
  double tan_half_fov_=0.,aspect_=1.;
  std::vector<Body> bodies_;
  std::vector<std::uint64_t> masks_;
  std::vector<std::uint8_t> dirty_tiles_;
  std::vector<std::size_t> distance_order_;
  std::vector<double> distance_lower_bound_;
  std::vector<Vec3> cached_center_;
  std::vector<double> cached_center_squared_,cached_radius_squared_;
  std::array<double,64> quantum_gain_{};
  std::vector<Pixel> frame_;
  std::vector<TileObservation> tile_observations_;
  std::vector<EffectParticle> effect_particles_;
  std::vector<EffectTilePlan> effect_tile_plan_;
  std::vector<std::uint64_t> previous_effect_signatures_;
  std::vector<std::uint8_t> previous_effect_active_;
  std::uint64_t effect_revision_=0,planned_effect_revision_=0;
  std::size_t planned_visible_effect_particles_=0;
  bool effect_plan_valid_=false,planned_effect_adaptive_=false;
  Stats stats_{};
  bool dirty_=true,planned_=false;
  bool observation_enabled_=false,tile_reuse_enabled_=false,frame_valid_=false;
};

}  // namespace mundo_x
