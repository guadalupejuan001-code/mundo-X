#pragma once

#include "mundo_x/ether_renderer.hpp"
#include "mundo_x/exact_t_experiment.hpp"
#include "mundo_x/pauli_observer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

namespace mundo_x {

// A read-only, explicit link to the current universal MIL/Reduced backend.
// No copies of the quantum state, measurement, sampling, or dense 2^n vectors.
// Bindings affect material only and never change geometric visibility masks.
class MilEtherRenderBridge {
 public:
  static constexpr std::size_t kMaxBodies = 64;

  MilEtherRenderBridge(const ReducedExactTRepresentation& mil,
                       EtherRenderer& renderer)
      : mil_(mil), renderer_(renderer) {}

  void bind(std::uint32_t id,const std::string& pauli,double strength) {
    check_body(id);
    if (!std::isfinite(strength) || strength < 0. || strength > 1.)
      throw std::invalid_argument("MIL render strength must be 0..1");
    // Verify the entire multiword Pauli now; never accept a truncated q999.
    auto packed=mundo_x_bridge::Pauli::parse(pauli,mil_.qubits());
    Binding next;
    next.enabled=true;
    next.text=pauli;
    next.pauli=std::move(packed);
    next.strength=strength;
    bindings_[id]=std::move(next);
    dirty_=true;
  }

  void bind_axis(std::uint32_t id,char axis,std::size_t qubit,double strength) {
    if(axis!='X'&&axis!='Y'&&axis!='Z')
      throw std::invalid_argument("MIL render axis must be X, Y or Z");
    if(qubit>=mil_.qubits())throw std::out_of_range("MIL render qubit");
    std::string pauli(mil_.qubits(),'I');
    pauli[qubit]=axis;
    bind(id,pauli,strength);
  }

  void unbind(std::uint32_t id) {
    check_body(id);
    bindings_[id]=Binding{};
    signals_[id]=0.;
    renderer_.set_quantum_material(id,0.,0.);
    dirty_=true;
  }

  void clear() noexcept {
    bindings_={};
    signals_.fill(0.);
    renderer_.clear_quantum_materials();
    dirty_=false;
  }

  void invalidate() noexcept {dirty_=true;}
  bool dirty() const noexcept {return dirty_;}
  std::size_t binding_count() const noexcept {
    return static_cast<std::size_t>(std::count_if(
        bindings_.begin(),bindings_.end(),
        [](const Binding& b){return b.enabled;}));
  }

  // One norm and at most one query per distinct Pauli per MIL revision.
  // Calculations finish before publishing a single new material value.
  // No renderer operation here mutates the MIL state or consumes RNG.
  std::size_t refresh() {
    if(!dirty_)return 0;
    const auto count=binding_count();
    if(!count){dirty_=false;return 0;}
    for(std::size_t id=0;id<bindings_.size();++id)
      if(bindings_[id].enabled&&!renderer_.has_body(static_cast<std::uint32_t>(id)))
        throw std::logic_error("MIL render body disappeared before observation");

    const double norm=mil_.sparse_norm();
    if(!(norm>0.)||!std::isfinite(norm))
      throw std::runtime_error("invalid MIL norm for rendering");

    std::array<double,kMaxBodies> pending{};
    std::unordered_map<std::string,double> cache;
    cache.reserve(count);
    for(std::size_t id=0;id<bindings_.size();++id) {
      const auto& binding=bindings_[id];
      if(!binding.enabled)continue;
      const auto found=cache.find(binding.text);
      if(found!=cache.end()){pending[id]=found->second;continue;}
      const auto z=mil_.pauli_expectation(binding.pauli.x,binding.pauli.z,
                              static_cast<std::uint8_t>(binding.pauli.phase))/norm;
      if(!std::isfinite(z.real())||!std::isfinite(z.imag())||
         std::abs(z.imag())>1e-10||
         z.real() < -1.-1e-8||z.real()>1.+1e-8)
        throw std::runtime_error("invalid MIL Pauli expectation for rendering");
      const double value=std::clamp(z.real(),-1.,1.);
      pending[id]=value;
      cache.emplace(binding.text,value);
    }

    for(std::size_t id=0;id<bindings_.size();++id) {
      const auto& binding=bindings_[id];
      if(binding.enabled)renderer_.set_quantum_material(
          static_cast<std::uint32_t>(id),pending[id],binding.strength);
    }
    signals_=pending;
    dirty_=false;
    return count;
  }

  double signal(std::uint32_t id) const {
    if(id>=kMaxBodies||!bindings_[id].enabled)
      throw std::out_of_range("MIL render signal is not bound");
    if(dirty_)throw std::logic_error("MIL render signal needs refresh");
    return signals_[id];
  }

 private:
  struct Binding {
    bool enabled=false;
    std::string text;
    mundo_x_bridge::Pauli pauli;
    double strength=0.;
  };

  void check_body(std::uint32_t id) const {
    if(id>=kMaxBodies||!renderer_.has_body(id))
      throw std::out_of_range("MIL render binding requires an existing body");
  }

  const ReducedExactTRepresentation& mil_;
  EtherRenderer& renderer_;
  std::array<Binding,kMaxBodies> bindings_{};
  std::array<double,kMaxBodies> signals_{};
  bool dirty_=false;
};

} // namespace mundo_x
