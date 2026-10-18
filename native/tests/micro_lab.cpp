#include "mundo_x/micro_lab.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using Lab = mundo_x::MicroLab;
unsigned checks = 0;
void require(bool value, const char* message) { ++checks; if (!value) throw std::runtime_error(message); }
void near(double a, double b, const char* message) { require(std::abs(a-b) < 1e-10, message); }
std::vector<float> vertices(Lab& lab) {
  std::vector<float> result(Lab::max_vertices * Lab::stride);
  const auto count = lab.vertices(result.data(), Lab::max_vertices);
  result.resize(count * Lab::stride); return result;
}
void quantum() {
  Lab lab;
  lab.preset(0);
  lab.gate(0,0);
  near(lab.qubits()[0].x, 1, "H gives X eigenstate");
  near(lab.qubits()[0].probability, .5, "H probability");
  lab.gate(1,0);
  near(lab.qubits()[0].x, std::sqrt(.5), "T real coherence");
  near(lab.qubits()[0].y, std::sqrt(.5), "T imaginary coherence sign");
  near(lab.qubits()[0].entropy, 0, "pure singleton entropy");
  lab.preset(0); lab.gate(0,0); lab.gate(4,0);
  near(lab.qubits()[0].entropy, 1, "Bell entropy q0");
  near(lab.qubits()[1].entropy, 1, "Bell entropy q1");
  for (unsigned i=0;i<128;++i) {
    lab.preset(0); lab.gate(0,0); lab.gate(4,0);
    const int measured=lab.measure_all();
    require(measured==0 || measured==3, "joint Bell outcomes");
    near(lab.qubits()[0].entropy, 0, "measurement collapses pure basis");
    near(lab.qubits()[0].probability, measured==3 ? 1 : 0, "measurement selected basis");
  }
}
void deterministic_and_coupling() {
  Lab a(42), b(42);
  a.particle_count(256); b.particle_count(256);
  a.advance(30); b.advance(30);
  require(vertices(a)==vertices(b), "same seed and ticks reproduce geometry");
  require(a.json()==b.json(), "same seed and ticks reproduce observables");
  const auto before=a.json(); a.advance(0); require(before==a.json(), "paused time is unchanged");
  Lab c(43), d(43); c.particle_count(256); d.particle_count(256);
  c.coupled(false); d.coupled(false); c.preset(0); d.preset(2);
  c.advance(10); d.advance(10);
  // With coupling disabled, quantum edits must not change even the colors.
  auto cv=vertices(c), dv=vertices(d);
  require(cv==dv,"disabled coupling removes all quantum influence on tracers");
  for(std::size_t i=0;i<cv.size();i+=Lab::stride)
    for(unsigned k=0;k<3;++k) near(cv[i+k],dv[i+k],"disabled coupling preserves dynamics");
  Lab e(43), f(43); e.particle_count(256); f.particle_count(256);
  e.preset(0); f.preset(2); e.advance(10); f.advance(10);
  auto ev=vertices(e), fv=vertices(f); double distance=0;
  for(std::size_t i=0;i<ev.size();i+=Lab::stride) distance+=std::abs(ev[i]-fv[i]);
  require(distance>.01,"quantum state measurably changes coupled dynamics");
  Lab g(91), h(91); g.coupled(false); h.coupled(false);
  g.measure_all(); g.advance(12); h.advance(12);
  require(vertices(g)==vertices(h),"quantum measurements do not consume tracer randomness");
  g.mode(1); h.mode(1);
  require(vertices(g)==vertices(h),"quantum measurements do not consume cell randomness");
  Lab j(66), k(66); j.particle_count(256); k.particle_count(8192);
  j.advance(30); k.advance(30); j.mode(1); k.mode(1);
  require(vertices(j)==vertices(k),"particle budget does not alter biological randomness");
}
void biology_and_bounds() {
  Lab lab(77); lab.particle_count(256); lab.mode(1);
  const auto initial_cells=lab.cell_count();
  const double initial_food=lab.nutrient_total();
  for(unsigned i=0;i<20;++i) lab.advance(60);
  require(lab.divisions()>0 && lab.cell_count()>initial_cells,"nutrient agents grow and divide");
  require(lab.nutrient_total()<initial_food,"colony consumes finite nutrient");
  require(lab.cell_count()<=Lab::max_cells,"colony stays within declared capacity");
  const auto data=vertices(lab);
  for(float v:data) require(std::isfinite(v),"finite rendered values");
  const auto food=lab.nutrient_total(); lab.inject(0,0,0);
  require(lab.nutrient_total()>food,"food brush adds nutrient");
  const auto cells=lab.cell_count(); lab.inject(1,.5f,.5f);
  require(lab.cell_count()==std::min(cells+12,Lab::max_cells),"seed brush creates agents");
  bool rejected=false;
  try { float tiny[9]; lab.vertices(tiny,1); } catch(const std::length_error&) { rejected=true; }
  require(rejected,"short renderer buffer rejected");
  rejected=false;
  try { lab.advance(121); } catch(const std::invalid_argument&) { rejected=true; }
  require(rejected,"unbounded work rejected");
}
}
int main() {
  try { quantum(); deterministic_and_coupling(); biology_and_bounds();
    std::cout<<"micro_lab PASS checks="<<checks<<'\n'; return 0;
  } catch(const std::exception& e) { std::cerr<<"micro_lab FAIL: "<<e.what()<<'\n'; return 1; }
}
