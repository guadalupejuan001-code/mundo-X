#pragma once

#include "mundo_x/factorized_core.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace mundo_x {

// Interactive, dimensionless models: Brownian tracers and nutrient-limited
// agents. Four simulated qubits supply an optional external field. This is
// not a molecular quantum simulation or a species-calibrated biological model.
class MicroLab {
 public:
  static constexpr std::size_t max_particles = 16384, max_cells = 2048;
  static constexpr std::size_t grid = 48, stride = 9;
  static constexpr std::size_t max_vertices = max_particles;
  static constexpr double step_seconds = 1.0 / 60.0;
  struct Particle { float x, y, z; };
  struct Cell { float x, y, angle, mass; unsigned strain; };
  struct Qubit { double x, y, z, probability, entropy; };

  explicit MicroLab(std::uint64_t seed = 20261018) : seed_(seed) { reset(); }

  void reset() {
    particle_random_.seed(seed_); cell_random_.seed(seed_ ^ 0x9e3779b97f4a7c15ULL);
    quantum_random_.seed(seed_ ^ 0xd1b54a32d192ed03ULL);
    particle_gaussian_.reset(); cell_gaussian_.reset();
    elapsed_ = 0; ticks_ = divisions_ = deaths_ = measurements_ = 0;
    last_measurement_ = -1;
    particles_.clear(); particles_.reserve(max_particles);
    for (std::size_t i = 0; i < particle_target_; ++i)
      particles_.push_back({uniform(-.85f, .85f), uniform(-.85f, .85f), uniform(-.7f, .7f)});
    cells_.clear(); cells_.reserve(max_cells);
    nutrients_.fill(1.0f);
    seed_cells(-.36f, -.12f, 24, 0);
    seed_cells(.36f, .12f, 24, 1);
    preset(2);
  }
  void reseed(std::uint64_t seed) { seed_ = seed; reset(); }
  void mode(int value) {
    if (value < 0 || value > 2) throw std::invalid_argument("invalid micro mode");
    mode_ = value;
  }
  int mode() const { return mode_; }
  void coupled(bool value) { coupled_ = value; }
  void agitation(float value) {
    if (!std::isfinite(value) || value < 0 || value > .1f)
      throw std::invalid_argument("agitation outside [0,0.1]");
    diffusion_ = value;
  }
  void growth(float value) {
    if (!std::isfinite(value) || value < 0 || value > 2)
      throw std::invalid_argument("growth outside [0,2]");
    growth_ = value;
  }
  void particle_count(std::size_t value) {
    if (value < 256 || value > max_particles) throw std::invalid_argument("particle capacity");
    particle_target_ = value;
    while (particles_.size() < value)
      particles_.push_back({uniform(-.85f, .85f), uniform(-.85f, .85f), uniform(-.7f, .7f)});
    particles_.resize(value);
  }
  void preset(int value) {
    if (value < 0 || value > 3) throw std::invalid_argument("invalid preset");
    quantum_ = std::make_unique<FactorizedCore>(4, 4);
    if (value == 1) for (std::size_t q = 0; q < 4; ++q) quantum_->hadamard(q);
    if (value == 2) {
      quantum_->hadamard(0); quantum_->cnot(0, 1);
      quantum_->hadamard(2); quantum_->phase_t(2);
    }
    if (value == 3) {
      quantum_->hadamard(0); quantum_->phase_t(0); quantum_->phase_t(0);
      quantum_->hadamard(0);
    }
    update_quantum();
  }
  void gate(int gate, int target) {
    if (target < 0 || target >= 4 || gate < 0 || gate > 4)
      throw std::invalid_argument("invalid micro gate");
    switch (gate) {
      case 0: quantum_->hadamard(target); break;
      case 1: quantum_->phase_t(target); break;
      case 2: quantum_->phase_s(target); break;
      case 3: quantum_->pauli_x(target); break;
      case 4: quantum_->cnot(target, (target + 1) % 4); break;
    }
    update_quantum();
  }
  int measure_all() {
    double sample = std::uniform_real_distribution<double>(0, 1)(quantum_random_), cumulative = 0;
    std::size_t outcome = state_.size() - 1;
    for (std::size_t i = 0; i < state_.size(); ++i) {
      cumulative += std::norm(state_[i]);
      if (sample < cumulative) { outcome = i; break; }
    }
    quantum_ = std::make_unique<FactorizedCore>(4, 4);
    for (std::size_t q = 0; q < 4; ++q) if ((outcome >> q) & 1) quantum_->pauli_x(q);
    last_measurement_ = static_cast<int>(outcome); ++measurements_;
    update_quantum();
    return last_measurement_;
  }
  void inject(int kind, float x, float y) {
    if (!std::isfinite(x) || !std::isfinite(y) || kind < 0 || kind > 2)
      throw std::invalid_argument("invalid injection");
    x = std::clamp(x, -.95f, .95f); y = std::clamp(y, -.95f, .95f);
    if (kind == 1) { seed_cells(x, y, 12, (divisions_ + cells_.size()) % 2); return; }
    if (kind == 0) {
      for (std::size_t j = 0; j < grid; ++j) for (std::size_t i = 0; i < grid; ++i) {
        const float dx = coordinate(i) - x, dy = coordinate(j) - y;
        nutrients_[j * grid + i] += 2.0f * std::exp(-(dx * dx + dy * dy) / .025f);
      }
      return;
    }
    for (auto& p : particles_) {
      float dx = p.x - x, dy = p.y - y;
      const float kick = .15f * std::exp(-(dx * dx + dy * dy) / .12f);
      p.x = reflect(p.x + dx * kick); p.y = reflect(p.y + dy * kick);
      p.z = reflect(p.z + kick);
    }
  }
  void advance(unsigned steps) {
    if (steps > 120) throw std::invalid_argument("too many simulation steps");
    for (unsigned k = 0; k < steps; ++k) {
      step_particles(); step_cells(); ++ticks_; elapsed_ = ticks_ * step_seconds;
    }
  }
  std::size_t vertices(float* destination, std::size_t capacity) const {
    const auto count = mode_ == 0 ? particles_.size() : (mode_ == 1 ? grid * grid + cells_.size() : 16);
    if (!destination || capacity < count) throw std::length_error("micro vertex buffer too small");
    std::size_t cursor = 0;
    auto put = [&](float x, float y, float z, float size, float r, float g, float b, float angle, float kind) {
      const std::array<float, stride> values{x, y, z, size, r, g, b, angle, kind};
      for (float value : values) destination[cursor++] = value;
    };
    if (mode_ == 0) {
      for (const auto& p : particles_) {
        const float t = .5f + .5f * std::sin(3 * p.x + 2 * p.y + (coupled_ ? static_cast<float>(field_[0]) : 0));
        put(p.x, p.y, p.z, .018f, .2f + .6f * t, .9f - .3f * t, .96f, 0, 0);
      }
    } else if (mode_ == 1) {
      for (std::size_t j = 0; j < grid; ++j) for (std::size_t i = 0; i < grid; ++i) {
        float n = nutrients_[j * grid + i]; n = n / (1 + n);
        put(coordinate(i), coordinate(j), -.035f, .062f, .05f, .12f + .5f * n, .13f + .38f * n, 0, 3);
      }
      for (const auto& c : cells_)
        put(c.x, c.y, 0, .045f + .014f * c.mass, c.strain ? 1.0f : .68f,
            c.strain ? .48f : .94f, c.strain ? .31f : .34f, c.angle, 1);
    } else {
      for (std::size_t i = 0; i < 16; ++i) {
        const float p = static_cast<float>(std::norm(state_[i]));
        const float phase = static_cast<float>(std::arg(state_[i]));
        put(-.75f + .5f * (i % 4), -.75f + .5f * (i / 4), .8f * std::sqrt(p),
            .035f + .24f * std::sqrt(p), .55f + .45f * std::cos(phase),
            .55f + .45f * std::cos(phase - 2.0944f), .55f + .45f * std::cos(phase + 2.0944f), 0, 2);
      }
    }
    return count;
  }
  const std::array<Qubit, 4>& qubits() const { return qubits_; }
  std::size_t cell_count() const { return cells_.size(); }
  std::size_t divisions() const { return divisions_; }
  std::uint64_t ticks() const { return ticks_; }
  double nutrient_total() const { double n = 0; for (float value : nutrients_) n += value; return n; }
  const std::array<double, 4>& field() const { return field_; }
  std::string json() const {
    std::ostringstream out; out << std::setprecision(12);
    out << "{\"ok\":true,\"seed\":" << seed_ << ",\"mode\":" << mode_
        << ",\"ticks\":" << ticks_ << ",\"time\":" << elapsed_
        << ",\"particles\":" << particles_.size() << ",\"cells\":" << cells_.size()
        << ",\"divisions\":" << divisions_ << ",\"deaths\":" << deaths_
        << ",\"capacity_reached\":" << (cells_.size() >= max_cells ? "true" : "false")
        << ",\"nutrient\":" << nutrient_total() << ",\"coupled\":" << (coupled_ ? "true" : "false")
        << ",\"diffusion\":" << diffusion_ << ",\"growth\":" << growth_
        << ",\"quantum_bytes\":" << quantum_->logical_amplitude_bytes()
        << ",\"model_owned_bytes\":" << sizeof(*this) + particles_.capacity() * sizeof(Particle)
                 + cells_.capacity() * sizeof(Cell) + state_.capacity() * sizeof(Complex)
                 + quantum_->storage_estimate().estimated_reserved_bytes
        << ",\"measurement\":" << last_measurement_ << ",\"measurements\":" << measurements_
        << ",\"field\":[" << field_[0] << ',' << field_[1] << ',' << field_[2] << ',' << field_[3]
        << "],\"zz01\":" << zz_ << ",\"qubits\":[";
    for (std::size_t q = 0; q < 4; ++q) {
      if (q) out << ',';
      const auto& v = qubits_[q];
      out << "{\"x\":" << v.x << ",\"y\":" << v.y << ",\"z\":" << v.z
          << ",\"p1\":" << v.probability << ",\"entropy\":" << v.entropy << '}';
    }
    out << "],\"basis\":[";
    for (std::size_t i = 0; i < state_.size(); ++i) {
      if (i) out << ',';
      out << "{\"p\":" << std::norm(state_[i]) << ",\"phase\":" << std::arg(state_[i]) << '}';
    }
    out << "]}"; return out.str();
  }

 private:
  using Complex = FactorizedCore::Complex;
  std::uint64_t seed_, ticks_ = 0;
  double elapsed_ = 0, zz_ = 0;
  std::size_t particle_target_ = 4096, divisions_ = 0, deaths_ = 0, measurements_ = 0;
  int mode_ = 0, last_measurement_ = -1;
  bool coupled_ = true;
  float diffusion_ = .012f, growth_ = .8f;
  std::mt19937_64 particle_random_, cell_random_, quantum_random_;
  std::normal_distribution<float> particle_gaussian_{0, 1}, cell_gaussian_{0, 1};
  std::vector<Particle> particles_;
  std::vector<Cell> cells_;
  std::array<float, grid * grid> nutrients_{}, scratch_{};
  std::unique_ptr<FactorizedCore> quantum_;
  std::vector<Complex> state_;
  std::array<Qubit, 4> qubits_{};
  std::array<double, 4> field_{};
  float uniform(float a, float b) { return std::uniform_real_distribution<float>(a, b)(particle_random_); }
  static float coordinate(std::size_t i) { return -1.0f + 2.0f * i / (grid - 1); }
  static float reflect(float x) {
    // Fold arbitrary finite overshoots into the reflecting interval.
    float t = x + 1;
    // Preserve the old float additions/subtractions and arbitrary overshoot
    // semantics. The common interval needs no remainder operation.
    if (!(t >= 0 && t < 4.0f)) t = std::fmod(t, 4.0f);
    if (t < 0) t += 4;
    return t <= 2 ? t - 1 : 3 - t;
  }
  double pauli(unsigned x, unsigned z, Complex phase = {1, 0}) const {
    Complex answer{0, 0};
    for (unsigned i = 0; i < 16; ++i) {
      unsigned parity = 0, bits = i & z;
      while (bits) { parity ^= bits & 1; bits >>= 1; }
      answer += std::conj(state_[i ^ x]) * state_[i] * phase * (parity ? -1.0 : 1.0);
    }
    return answer.real();
  }
  void update_quantum() {
    state_ = quantum_->dense_state(4);
    double norm = 0; for (auto a : state_) norm += std::norm(a);
    if (!(norm > 0) || !std::isfinite(norm)) throw std::runtime_error("invalid quantum norm");
    for (auto& a : state_) a /= std::sqrt(norm);
    for (unsigned q = 0; q < 4; ++q) {
      auto& v = qubits_[q]; const unsigned bit = 1u << q;
      v.x = pauli(bit, 0); v.y = pauli(bit, bit, {0, 1}); v.z = pauli(0, bit);
      v.probability = std::clamp((1 - v.z) / 2, 0.0, 1.0);
      const double radius = std::clamp(std::sqrt(v.x*v.x + v.y*v.y + v.z*v.z), 0.0, 1.0);
      const double eigenvalue = (1 + radius) / 2;
      v.entropy = eigenvalue >= 1 ? 0 : -eigenvalue * std::log2(eigenvalue)
          - (1 - eigenvalue) * std::log2(1 - eigenvalue);
    }
    const double xx = pauli(3, 0), xy = pauli(3, 2, {0, 1});
    zz_ = pauli(0, 3);
    field_ = {.65 * qubits_[2].x + .45 * xx, .65 * qubits_[2].y + .45 * xy,
              .4 * (qubits_[0].z - qubits_[1].z) + .2 * qubits_[3].z,
              std::atan2(xy, xx)};
  }
  void seed_cells(float x, float y, unsigned count, unsigned strain) {
    for (unsigned i = 0; i < count && cells_.size() < max_cells; ++i)
      cells_.push_back({reflect(x + .06f * cell_gaussian_(cell_random_)), reflect(y + .06f * cell_gaussian_(cell_random_)),
                        std::uniform_real_distribution<float>(-3.14159f, 3.14159f)(cell_random_),
                        std::uniform_real_distribution<float>(.7f, 1.3f)(cell_random_), strain});
  }
  void step_particles() {
    const float dt = static_cast<float>(step_seconds), noise = std::sqrt(2 * diffusion_ * dt);
    const float f0 = coupled_ ? static_cast<float>(field_[0]) : 0;
    const float f1 = coupled_ ? static_cast<float>(field_[1]) : 0;
    const float f2 = coupled_ ? static_cast<float>(field_[2]) : 0;
    for (auto& p : particles_) {
      const float fx = -.15f * p.x + .42f * f0 * std::cos(3 * p.x) * std::sin(3 * p.y) + .18f * f1 * std::cos(3 * p.z);
      const float fy = -.15f * p.y + .42f * f1 * std::sin(3 * p.x) * std::cos(3 * p.y) - .18f * f0 * std::sin(3 * p.z);
      const float fz = -.3f * p.z + .2f * f2 * std::cos(3 * p.x + 3 * p.y);
      p.x = reflect(p.x + dt * fx + noise * particle_gaussian_(particle_random_));
      p.y = reflect(p.y + dt * fy + noise * particle_gaussian_(particle_random_));
      p.z = reflect(p.z + dt * fz + noise * particle_gaussian_(particle_random_));
    }
  }
  void step_cells() {
    const float dt = static_cast<float>(step_seconds);
    for (std::size_t j = 0; j < grid; ++j) for (std::size_t i = 0; i < grid; ++i) {
      const std::size_t k = j * grid + i;
      const float neighbors = nutrients_[j*grid + (i ? i-1 : i)] + nutrients_[j*grid + std::min(i+1,grid-1)]
          + nutrients_[(j ? j-1 : j)*grid+i] + nutrients_[std::min(j+1,grid-1)*grid+i];
      scratch_[k] = nutrients_[k] + .8f * dt * (neighbors - 4 * nutrients_[k]);
    }
    nutrients_.swap(scratch_);
    const std::size_t original_count = cells_.size();
    for (std::size_t k = 0; k < original_count; ++k) {
      auto& c = cells_[k];
      auto ix = static_cast<std::size_t>(std::clamp((c.x+1)*.5f*(grid-1), 0.0f, float(grid-1)));
      auto iy = static_cast<std::size_t>(std::clamp((c.y+1)*.5f*(grid-1), 0.0f, float(grid-1)));
      float& food = nutrients_[iy * grid + ix];
      const float consumed = c.mass >= 2 && cells_.size() >= max_cells ? 0 :
          std::min(food, dt * growth_ * c.mass * food / (.25f + food));
      food -= consumed;
      c.mass += .8f * consumed - .006f * dt * c.mass;
      const float gx = nutrients_[iy*grid+std::min(ix+1,grid-1)] - nutrients_[iy*grid+(ix ? ix-1 : ix)];
      const float gy = nutrients_[std::min(iy+1,grid-1)*grid+ix] - nutrients_[(iy ? iy-1 : iy)*grid+ix];
      const float bx = gx + (coupled_ ? .18f * static_cast<float>(field_[0]) : 0);
      const float by = gy + (coupled_ ? .18f * static_cast<float>(field_[1]) : 0);
      c.angle += dt * 2 * (by * std::cos(c.angle) - bx * std::sin(c.angle)) + .6f * std::sqrt(dt) * cell_gaussian_(cell_random_);
      c.x = reflect(c.x + dt * .08f * std::cos(c.angle));
      c.y = reflect(c.y + dt * .08f * std::sin(c.angle));
      if (c.mass >= 2 && cells_.size() < max_cells) {
        c.mass *= .5f;
        Cell daughter = c;
        daughter.x = reflect(c.x + .02f * std::cos(c.angle));
        daughter.y = reflect(c.y + .02f * std::sin(c.angle));
        c.x = reflect(c.x - .02f * std::cos(c.angle));
        c.y = reflect(c.y - .02f * std::sin(c.angle));
        daughter.angle += .12f * cell_gaussian_(cell_random_);
        cells_.push_back(daughter); ++divisions_;
      }
    }
    const auto end = std::remove_if(cells_.begin(), cells_.end(), [](const Cell& c) { return c.mass < .15f; });
    deaths_ += static_cast<std::size_t>(cells_.end() - end);
    cells_.erase(end, cells_.end());
  }
};
} // namespace mundo_x
