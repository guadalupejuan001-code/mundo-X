#include "mundo_x/ether_surface_buffer.hpp"
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <chrono>
#include <string>

using R=mundo_x::EtherRenderSpatial;
using Buffer=mundo_x::EtherSurfaceBuffer;
using Clock=std::chrono::steady_clock;
static double ms(Clock::time_point start){return std::chrono::duration<double,std::milli>(Clock::now()-start).count();}
int main(int argc,char** argv){
  try {
    const std::size_t width=argc>1?std::stoull(argv[1]):7680;
    const std::size_t height=argc>2?std::stoull(argv[2]):4320;
    const unsigned n=argc>3?static_cast<unsigned>(std::stoul(argv[3])):4096;
    const std::size_t tile=argc>4?std::stoull(argv[4]):16;
    R renderer(width,height,tile);
    renderer.set_camera({0.,0.,12.},{0.,0.,0.},47.);
    for(std::uint32_t i=0;i<n;++i){
      const double x=(static_cast<int>((i*37)%311)-155)*.41;
      const double y=(static_cast<int>((i*31)%211)-105)*.35;
      const double z=-20.-static_cast<double>((i*17)%97);
      renderer.add_body({i,{x,y,z},.08+static_cast<double>(i%11)*.03,{{.3,.6,.8}}});
    }
    Buffer buffer(renderer);
    buffer.prepare_lazy();
    const auto lazy_ms=buffer.metrics().prepare_ms;
    std::uint64_t checksum=0;
    const auto query_start=Clock::now();
    for(unsigned i=0;i<8192;++i){
      const std::size_t x=(static_cast<std::uint64_t>(i)*7919)%width;
      const std::size_t y=(static_cast<std::uint64_t>(i)*3467)%height;
      auto p=buffer.at(x,y);
      auto ref=renderer.pixel_reference(x,y);
      if(!(p==ref))throw std::runtime_error("lazy oracle mismatch");
      checksum+=static_cast<std::uint32_t>(p.body+1);
    }
    const auto lazy_query_ms=ms(query_start);
    buffer.capture_surfaces();
    const auto packed_stats=buffer.metrics();
    const auto query_start2=Clock::now();
    for(unsigned i=0;i<8192;++i){
      const std::size_t x=(static_cast<std::uint64_t>(i)*7919)%width;
      const std::size_t y=(static_cast<std::uint64_t>(i)*3467)%height;
      auto p=buffer.at(x,y);
      auto ref=renderer.pixel_reference(x,y);
      if(!(p==ref))throw std::runtime_error("packed oracle mismatch");
      checksum+=static_cast<std::uint32_t>(p.body+1);
    }
    const auto packed_query_ms=ms(query_start2);
    const auto frame=renderer.render();
    // Validation ONLY: visit all output coordinates and verify the packed
    // buffer reconstructs the complete 8K image bit-for-bit, including depth.
    // This O(W*H) export check is NOT part of lazy buffer preparation.
    std::uint64_t packed_hash=1469598103934665603ULL;
    const auto full_check_start=Clock::now();
    for(std::size_t y=0;y<height;++y)for(std::size_t x=0;x<width;++x){
      const auto p=buffer.at(x,y);
      auto mix=[&](std::uint64_t v){for(unsigned b=0;b<8;++b){
        packed_hash^=(v>>(b*8))&0xffu;packed_hash*=1099511628211ULL;
      }};
      mix(static_cast<std::uint64_t>(static_cast<std::int64_t>(p.body)));
      mix(p.rgba);
      std::uint64_t bits=0;std::memcpy(&bits,&p.depth,sizeof(bits));mix(bits);
    }
    const auto full_check_ms=ms(full_check_start);
    if(packed_hash!=frame.hash)throw std::runtime_error("full 8K packed surface hash differs from renderer");
    std::cout<<std::fixed<<std::setprecision(3)
             <<"{\"width\":"<<width<<",\"height\":"<<height
             <<",\"bodies\":"<<n<<",\"tile\":"<<tile
             <<",\"tiles\":"<<frame.tiles
             <<",\"tile_references\":"<<frame.tile_references
             <<",\"nonempty_tiles\":"<<frame.nonempty_tiles
             <<",\"full_frame_hash\":"<<frame.hash
             <<",\"packed_full_frame_hash\":"<<packed_hash
             <<",\"packed_full_image_check_ms\":"<<full_check_ms
             <<",\"full_pixel_queries\":"<<frame.ray_queries
             <<",\"full_intersection_tests\":"<<frame.intersection_tests
             <<",\"full_frame_ms\":"<<frame.render_ms
             <<",\"lazy_setup_ms\":"<<lazy_ms
             <<",\"lazy_buffer_bytes\":0"
             <<",\"lazy_query_8192_ms\":"<<lazy_query_ms
             <<",\"packed_capture_ms\":"<<packed_stats.capture_ms
             <<",\"packed_query_8192_ms\":"<<packed_query_ms
             <<",\"packed_candidate_queries\":"<<packed_stats.candidate_queries
             <<",\"packed_visible_pixels\":"<<packed_stats.visible_pixels
             <<",\"packed_spans\":"<<packed_stats.spans
             <<",\"packed_logical_bytes\":"<<packed_stats.logical_bytes
             <<",\"packed_allocated_bytes\":"<<packed_stats.allocated_bytes
             <<",\"full_rgba_bytes\":"<<packed_stats.full_rgba_bytes
             <<",\"checksum\":"<<checksum<<"}\n";
    return 0;
  }catch(const std::exception& e){std::cerr<<"ETHER_SURFACE_BENCH_FAIL "<<e.what()<<"\n";return 1;}
}
