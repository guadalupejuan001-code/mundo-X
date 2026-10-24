#pragma once

// Ether Surface Buffer: sparse color and 16-bit surface-reference storage.
// Lazy mode is a scene descriptor, not a conventional screen framebuffer:
// it prepares spatial indexes without stepping through every output pixel.
// Packed mode materializes only visible surfaces and leaves background implicit.
// Depth is reconstructed from the matching analytic sphere at query time.
// This is CPU-only; it does NOT present frames to the display/GPU.

#include "mundo_x/ether_render_spatial.hpp"
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace mundo_x {

class EtherSurfaceBuffer {
 public:
  using Pixel=EtherRenderSpatial::Pixel;
  enum class Mode { empty, lazy, packed };
  struct Metrics {
    Mode mode=Mode::empty;
    std::size_t visible_pixels=0,spans=0;
    std::size_t logical_bytes=0,allocated_bytes=0;
    std::size_t full_rgba_bytes=0,full_id_depth_rgba_bytes=0;
    std::size_t lazy_queries=0,packed_queries=0;
    std::size_t candidate_queries=0;
    double prepare_ms=0.,capture_ms=0.;
  };

  explicit EtherSurfaceBuffer(EtherRenderSpatial& renderer):renderer_(renderer){}
  EtherSurfaceBuffer(const EtherSurfaceBuffer&)=delete;
  EtherSurfaceBuffer& operator=(const EtherSurfaceBuffer&)=delete;

  // This operation grows with the compact scene and candidate regions, not W*H.
  // No color, depth, or screen-size surface image is materialized.
  void prepare_lazy(){
    const auto start=Clock::now();
    renderer_.plan();
    drop_packed();
    mode_=Mode::lazy;revision_=renderer_.revision();
    metrics_=Metrics{};metrics_.mode=mode_;
    metrics_.full_rgba_bytes=renderer_.width()*renderer_.height()*4;
    metrics_.full_id_depth_rgba_bytes=renderer_.width()*renderer_.height()*14;
    metrics_.prepare_ms=milliseconds(start);
    refresh_memory_stats();
  }

  // Capture only visible pixels, in compact scan-line runs; background is
  // represented by the analytic scene rule. O(visible footprints), not W*H.
  // For a truly lazy buffer, use prepare_lazy() and query individual pixels.
  void capture_surfaces(){
    const auto start=Clock::now();
    // Explicitly refresh the scene descriptor, including dirty changes.
    renderer_.plan();
    const auto version=renderer_.revision();
    const std::size_t height=renderer_.height();
    std::vector<std::uint32_t> first(height+1,0);
    std::vector<Span> spans;
    std::vector<std::uint16_t> ids;
    std::vector<std::uint32_t> colors;
    std::size_t cursor_row=0;
    std::size_t last_y=height,last_x=std::numeric_limits<std::size_t>::max();
    auto result=renderer_.render_sparse_surfaces([&](std::size_t x,std::size_t y,const Pixel& p){
      if(p.body<0||static_cast<std::uint32_t>(p.body)>=4096)
        throw std::logic_error("invalid compact surface identifier");
      if(ids.size()>=std::numeric_limits<std::uint32_t>::max())
        throw std::length_error("surface buffer exceeds 32-bit offsets");
      if(y!=last_y){
        for(;cursor_row<=y;++cursor_row)first[cursor_row]=static_cast<std::uint32_t>(spans.size());
      }
      if(!spans.empty()&&last_y==y&&last_x+1==x&&spans.back().length<65535){
        ++spans.back().length;
      }else{
        spans.push_back({static_cast<std::uint16_t>(x),1,
                         static_cast<std::uint32_t>(ids.size())});
      }
      ids.push_back(static_cast<std::uint16_t>(p.body));
      colors.push_back(p.rgba);
      last_x=x;last_y=y;
    });
    for(;cursor_row<=height;++cursor_row)first[cursor_row]=static_cast<std::uint32_t>(spans.size());
    if(renderer_.revision()!=version)throw std::logic_error("scene changed during surface capture");
    // Bound resident memory by observed surfaces, not vector growth slack.
    spans.shrink_to_fit();ids.shrink_to_fit();colors.shrink_to_fit();
    first_.swap(first);spans_.swap(spans);ids_.swap(ids);colors_.swap(colors);
    revision_=version;mode_=Mode::packed;metrics_=Metrics{};
    metrics_.mode=mode_;
    metrics_.visible_pixels=result.sparse_surfaces;
    metrics_.spans=spans_.size();
    metrics_.candidate_queries=result.sparse_queries;
    metrics_.full_rgba_bytes=renderer_.width()*renderer_.height()*4;
    metrics_.full_id_depth_rgba_bytes=renderer_.width()*renderer_.height()*14;
    metrics_.prepare_ms=result.planning_ms;
    metrics_.capture_ms=milliseconds(start);
    refresh_memory_stats();
  }

  // Exact read. In packed mode, color and 16-bit ID are loaded from the
  // compressed surface map. Only that sphere's depth is reconstructed;
  // background color is generated on demand (never stored).
  Pixel at(std::size_t x,std::size_t y){
    check(x,y);
    if(mode_==Mode::lazy){
      ++metrics_.lazy_queries;
      return renderer_.pixel(x,y);
    }
    ++metrics_.packed_queries;
    const auto from=first_[y],until=first_[y+1];
    auto lo=spans_.begin()+from,hi=spans_.begin()+until;
    const auto it=std::lower_bound(lo,hi,x,[](const Span& r,std::size_t coord){
      return static_cast<std::size_t>(r.x0)+r.length<=coord;
    });
    if(it==hi||x<it->x0)return renderer_.background(x,y);
    const auto offset=static_cast<std::size_t>(it->offset)+(x-it->x0);
    Pixel p;p.body=ids_[offset];p.rgba=colors_[offset];
    p.depth=renderer_.depth_for_body(static_cast<std::uint32_t>(p.body),x,y);
    if(!std::isfinite(p.depth))throw std::logic_error("packed surface depth cannot be recovered");
    return p;
  }

  void invalidate(){drop_packed();mode_=Mode::empty;metrics_=Metrics{};}
  Mode mode()const noexcept{return mode_;}
  const Metrics& metrics()const noexcept{return metrics_;}

 private:
  struct Span {
    std::uint16_t x0=0,length=0;
    std::uint32_t offset=0;
  };
  static_assert(sizeof(Span)==8,"packed spans must be 8 bytes");
  using Clock=std::chrono::steady_clock;
  static double milliseconds(Clock::time_point start){
    return std::chrono::duration<double,std::milli>(Clock::now()-start).count();
  }
  void check(std::size_t x,std::size_t y)const{
    if(mode_==Mode::empty)throw std::logic_error("surface buffer not prepared");
    if(renderer_.revision()!=revision_)throw std::logic_error("surface buffer invalidated by a scene edit");
    if(x>=renderer_.width()||y>=renderer_.height())throw std::out_of_range("surface buffer coordinates out of range");
  }
  void drop_packed(){
    first_={};spans_={};ids_={};colors_={};
  }
  void refresh_memory_stats(){
    metrics_.logical_bytes=first_.size()*sizeof(std::uint32_t)+spans_.size()*sizeof(Span)
          +ids_.size()*sizeof(std::uint16_t)+colors_.size()*sizeof(std::uint32_t);
    metrics_.allocated_bytes=first_.capacity()*sizeof(std::uint32_t)+spans_.capacity()*sizeof(Span)
          +ids_.capacity()*sizeof(std::uint16_t)+colors_.capacity()*sizeof(std::uint32_t);
  }
  EtherRenderSpatial& renderer_;
  Mode mode_=Mode::empty;
  std::uint64_t revision_=0;
  std::vector<std::uint32_t> first_; // start of spans for row y (height+1)
  std::vector<Span> spans_; // only contiguous visible surfaces
  std::vector<std::uint16_t> ids_; // 16-bit index, not eight-byte depth
  std::vector<std::uint32_t> colors_; // exact 32-bit RGBA per visible pixel
  Metrics metrics_{};
};

} // namespace mundo_x
