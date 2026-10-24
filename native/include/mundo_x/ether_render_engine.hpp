#pragma once
// Ether Render X unified CPU scene facade, stage 1.
// The only image observer in this facade is EtherSurfaceBuffer.
// No second rasterizer, dense framebuffer or duplicate sphere tracing engine.
#include "mundo_x/ether_surface_buffer.hpp"
#include <cstdint>
#include <stdexcept>

namespace mundo_x {
class EtherRenderEngine {
 public:
  using Vec3=EtherRenderSpatial::Vec3;
  using Body=EtherRenderSpatial::Body;
  using Pixel=EtherRenderSpatial::Pixel;
  using Mode=EtherSurfaceBuffer::Mode;
  using Metrics=EtherSurfaceBuffer::Metrics;
  struct FrameInfo {
    std::uint64_t revision=0;
    std::size_t width=0,height=0,objects=0;
    Metrics buffer{};
  };
  explicit EtherRenderEngine(std::size_t width,std::size_t height,std::size_t tile=16)
      : spatial_(width,height,tile),surface_(spatial_) {}
  void camera(Vec3 eye,Vec3 target,double fov){spatial_.set_camera(eye,target,fov);surface_.invalidate();}
  void add(Body b){spatial_.add_body(b);surface_.invalidate();}
  void clear(){spatial_.clear_bodies();surface_.invalidate();}
  void material(std::uint32_t id,double gain){spatial_.set_material_gain(id,gain);surface_.invalidate();}
  void tile(std::size_t size){spatial_.set_tile(size);surface_.invalidate();}
  void prepare(){surface_.prepare_lazy();}
  void capture(){surface_.capture_surfaces();}
  Pixel observe(std::size_t x,std::size_t y){return surface_.at(x,y);}
  FrameInfo info() const {
    return {spatial_.revision(),spatial_.width(),spatial_.height(),
            spatial_.bodies().size(),surface_.metrics()};
  }
  // Explicit full-image export. Output size is necessarily width*height.
  // Consumer receives pixels in row-major order, without allocating a dense
  // frame inside the engine. Full export is not a non-linear-time operation.
  template<class Consumer>
  void export_image(Consumer&& consumer){
    if(surface_.mode()==Mode::empty)surface_.prepare_lazy();
    for(std::size_t y=0;y<spatial_.height();++y)
      for(std::size_t x=0;x<spatial_.width();++x)
        consumer(x,y,surface_.at(x,y));
  }
 private:
  EtherRenderSpatial spatial_;
  EtherSurfaceBuffer surface_;
};
} // namespace mundo_x
