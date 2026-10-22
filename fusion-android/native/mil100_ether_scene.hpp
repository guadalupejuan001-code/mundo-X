#pragma once

#include "mundo_x/ether_mil_bridge.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace mundo_x {

/**
 * 100 logical QUBITS, 100 VISUAL OBJECTS, actual MIL Reduced state.
 *
 * The canonical EtherRenderer has a 64-body/tile-mask limit. We preserve
 * that engine intact by rendering two disjoint 50-body passes with an
 * identical 3D camera, composing by physical ray hit DEPTH. Objects 0..49
 * and 50..99 correspond to qubits of a genuine 100-qubit GHZ register.
 *
 * Every object is bound to the same 100-body global Pauli correlation
 * X0 X1 ... X99 (NOT to a fabricated independent local expectation).
 * Changing an arbitrary phase at q99 updates <X^100>=cos(theta) and thus
 * the material of every visible sphere; |GHZ_100> stays normalized and
 * the Z0Z99 correlation remains +1. Only two MIL Pauli expectation queries
 * are required per quantum revision (one per pass), cached by the bridge.
 *
 * No measurement, statevector 2^100, pixel-wise quantum evaluation, or
 * quantum-accelerated rendering is performed. The 100 spheres illustrate
 * logical qubits, NOT nucleons, wavefunction amplitudes, or 100 particles
 * that can be treated as a real nuclear interaction.
 */
class Mil100EtherScene {
 public:
  static constexpr std::size_t kQubits=100;
  static constexpr std::size_t kObjects=100;
  static constexpr std::size_t kPassObjects=50;
  static constexpr std::size_t kMaxComponents=32;
  static constexpr double kPi=3.141592653589793238462643383279502884;

  using Complex=std::complex<double>;
  using Core=ReducedExactTRepresentation;
  using Render=EtherRenderer;
  using Bridge=MilEtherRenderBridge;

  struct FramePixel {
    std::int32_t logical_qubit=-1;
    double depth=std::numeric_limits<double>::infinity();
    std::uint32_t argb=0xff000000u;
  };
  struct Summary {
    std::size_t qubits=0,visible_objects=0,passes=0;
    std::size_t components=0,state_bytes=0;
    double norm=0.,local_z0=0.,distant_zz=0.,global_x=0.,phase=0.;
    double quantum_refresh_ms=0.,raster_ms=0.;
    std::uint64_t candidate_tests=0,brute_tests=0;
    std::size_t exact_mismatches=0;
    std::uint64_t framebuffer_hash=0;
  };

  Mil100EtherScene(std::size_t width,std::size_t height)
     : width_(width),height_(height),mil_(kQubits,kMaxComponents),
       first_(width,height,32),second_(width,height,32),
       first_link_(mil_,first_),second_link_(mil_,second_),
       frame_(width*height) {
    if(width<96||height<64||width>640||height>480||width*height>180000)
      throw std::invalid_argument("100q render dimension exceeds bounded budget");
    mil_.hadamard(0);
    for(std::size_t q=1;q<kQubits;q++)mil_.cnot(0,q);
    if(mil_.component_count()>kMaxComponents)
      throw std::length_error("MIL 100q initial component budget exceeded");

    std::string full_x(kQubits,'X');
    for(std::size_t q=0;q<kObjects;q++){
      Render::Body body{};
      body.id=static_cast<std::uint32_t>(q%kPassObjects);
      // Fibonacci sphere: visually distinguish 100 qubit markers with a
      // fixed 3D layout. These ARE NOT quantum positions of the state.
      const double yf=1.-2.*(static_cast<double>(q)+.5)/kObjects;
      const double ring=std::sqrt(std::max(0.,1.-yf*yf));
      const double az=kPi*(3.-std::sqrt(5.))*q;
      body.center={2.65*std::cos(az)*ring,
                   2.65*yf,
                   2.65*std::sin(az)*ring};
      body.radius=.23;
      body.color={
         .47+.46*(.5+.5*std::sin(.23*q)),
         .28+.61*(.5+.5*std::sin(.23*q+2.1)),
         .33+.60*(.5+.5*std::sin(.23*q+4.2))
      };
      Render& renderer=q<kPassObjects?first_:second_;
      Bridge& bridge=q<kPassObjects?first_link_:second_link_;
      renderer.add_body(body);
      bridge.bind(body.id,full_x,.68);
      objects_[q]=body;
    }
    summary_.qubits=kQubits;
    summary_.visible_objects=kObjects;
    summary_.passes=2;
    summary_.state_bytes=mil_.logical_payload_bytes();
    summary_.components=mil_.component_count();
    summary_.norm=mil_.sparse_norm();
    if(std::abs(summary_.norm-1.)>1e-10)
      throw std::runtime_error("initial GHZ100 state has incorrect norm");
    // Validate NONLOCAL genuine 100q correlations, including q99 in
    // the second 64-bit Pauli word.
    summary_.local_z0=expectation(single_pauli('Z',0));
    summary_.distant_zz=expectation(double_pauli('Z',0,99));
    if(std::abs(summary_.local_z0)>1e-10||
       std::abs(summary_.distant_zz-1.)>1e-10)
      throw std::runtime_error("GHZ100 correlation checks failed");
  }

  Summary render(double yaw,double pitch,double distance,double phase,
                 bool check_exact=false){
    if(!std::isfinite(yaw)||!std::isfinite(pitch)||
       !std::isfinite(distance)||!std::isfinite(phase)||
       distance<4.||distance>18.||phase<0.||phase>2.*kPi+1e-5)
      throw std::invalid_argument("invalid 100q scene camera or phase");

    // Persist the 100-qubit quantum state across camera orbit events; do NOT
    // recompute the GHZ preparation for every rendered frame.
    const auto start_quantum=std::chrono::steady_clock::now();
    phase=std::clamp(phase,0.,2.*kPi);
    if(!phase_initialized_||phase!=previous_phase_){
      mil_.phase(99,phase-previous_phase_);
      previous_phase_=phase;
      phase_initialized_=true;
      first_link_.invalidate();second_link_.invalidate();
    }
    first_link_.refresh();
    second_link_.refresh();
    summary_.quantum_refresh_ms=elapsed_ms(start_quantum);
    summary_.global_x=first_link_.signal(0);
    if(std::abs(summary_.global_x-second_link_.signal(0))>1e-10)
      throw std::runtime_error("quantum material differs between visual passes");
    if(std::abs(summary_.global_x-std::cos(phase))>1e-9)
      throw std::runtime_error("GHZ100 coherence disagrees with analytic phase");
    summary_.phase=phase;
    summary_.norm=mil_.sparse_norm();
    summary_.components=mil_.component_count();
    summary_.state_bytes=mil_.logical_payload_bytes();
    if(summary_.components>kMaxComponents||
       std::abs(summary_.norm-1.)>1e-9)
      throw std::runtime_error("MIL 100q exceeded norm/component budget");

    const auto begin=std::chrono::steady_clock::now();
    if(!camera_initialized_||yaw!=previous_yaw_||
       pitch!=previous_pitch_||distance!=previous_distance_){
      const double p=std::clamp(pitch,-1.32,1.32);
      const Render::Vec3 eye{
        distance*std::cos(p)*std::sin(yaw),
        distance*std::sin(p),
        distance*std::cos(p)*std::cos(yaw)
      };
      first_.set_camera(eye,{0.,0.,0.},47.);
      second_.set_camera(eye,{0.,0.,0.},47.);
      camera_initialized_=true;
      previous_yaw_=yaw;previous_pitch_=pitch;
      previous_distance_=distance;
    }
    const auto s0=first_.render(check_exact);
    const auto s1=second_.render(check_exact);
    summary_.candidate_tests=s0.candidate_tests+s1.candidate_tests;
    summary_.brute_tests=s0.brute_tests+s1.brute_tests;
    summary_.exact_mismatches=s0.exact_mismatches+s1.exact_mismatches;
    const auto& f0=first_.frame();
    const auto& f1=second_.frame();
    if(f0.size()!=frame_.size()||f1.size()!=frame_.size())
      throw std::runtime_error("100q framebuffer dimension mismatch");
    summary_.framebuffer_hash=1469598103934665603ULL;
    for(std::size_t i=0;i<frame_.size();i++){
      const bool firstHit=f0[i].body>=0;
      const bool secondHit=f1[i].body>=0;
      const bool chooseSecond=secondHit &&
            (!firstHit || f1[i].depth<f0[i].depth);
      const auto& source=chooseSecond?f1[i]:f0[i];
      auto& destination=frame_[i];
      destination.logical_qubit=source.body<0?-1:
           static_cast<std::int32_t>(source.body+
                (chooseSecond?kPassObjects:0));
      destination.depth=source.depth;
      destination.argb=source.rgba;
      // Deterministic hash of the observed visual output.
      summary_.framebuffer_hash^=static_cast<std::uint64_t>(destination.argb);
      summary_.framebuffer_hash*=1099511628211ULL;
      summary_.framebuffer_hash^=static_cast<std::uint64_t>(
          static_cast<std::int64_t>(destination.logical_qubit+1));
      summary_.framebuffer_hash*=1099511628211ULL;
    }
    summary_.raster_ms=elapsed_ms(begin);
    return summary_;
  }

  const std::vector<FramePixel>& frame() const noexcept{return frame_;}
  const std::array<Render::Body,kObjects>& objects() const noexcept{
    return objects_;
  }
  std::size_t width() const noexcept{return width_;}
  std::size_t height() const noexcept{return height_;}
  const Summary& summary() const noexcept{return summary_;}

 private:
  static double elapsed_ms(std::chrono::steady_clock::time_point t){
    return std::chrono::duration<double,std::milli>(
       std::chrono::steady_clock::now()-t).count();
  }
  std::string single_pauli(char axis,std::size_t q) const {
    std::string pauli(kQubits,'I');pauli.at(q)=axis;return pauli;
  }
  std::string double_pauli(char axis,std::size_t a,std::size_t b) const {
    std::string pauli(kQubits,'I');
    pauli.at(a)=axis;pauli.at(b)=axis;return pauli;
  }
  double expectation(const std::string& text) const {
    auto parsed=mundo_x_bridge::Pauli::parse(text,kQubits);
    const auto value=mil_.pauli_expectation(parsed.x,parsed.z,
           static_cast<std::uint8_t>(parsed.phase))/mil_.sparse_norm();
    if(!std::isfinite(value.real())||
       !std::isfinite(value.imag())||std::abs(value.imag())>1e-10)
      throw std::runtime_error("MIL GHZ100 observable is not real");
    return value.real();
  }

  std::size_t width_,height_;
  Core mil_;
  Render first_,second_;
  Bridge first_link_,second_link_;
  std::vector<FramePixel> frame_;
  std::array<Render::Body,kObjects> objects_{};
  Summary summary_{};
  double previous_phase_=0.;
  bool phase_initialized_=false;
  double previous_yaw_=0.,previous_pitch_=0.,previous_distance_=8.;
  bool camera_initialized_=false;
};

} // namespace mundo_x
