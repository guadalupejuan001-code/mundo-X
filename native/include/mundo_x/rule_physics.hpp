#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <vector>

namespace mundo_x {

// Deterministic finite-body physics driven by compact global rules.
// The uniform gravity field is represented once for the whole world; bodies
// store only their local deviations (position/velocity/material parameters).
// Pair gravity is optional and intentionally O(n^2); it is for correctness
// experiments, not a claim of infinite physical execution resources.
class RulePhysics {
 public:
  struct Vec3 {
    double x = 0, y = 0, z = 0;
    Vec3& operator+=(const Vec3& b) { x += b.x; y += b.y; z += b.z; return *this; }
    Vec3& operator-=(const Vec3& b) { x -= b.x; y -= b.y; z -= b.z; return *this; }
    Vec3& operator*=(double s) { x *= s; y *= s; z *= s; return *this; }
  };
  struct Body {
    Vec3 position, velocity;
    double inverse_mass = 1.0;
    double radius = 0.5;
    double restitution = 0.2;
  };
  struct Rules {
    Vec3 uniform_acceleration{0.0, -9.80665, 0.0};
    double pair_gravity = 0.0;
    double softening = 1e-6;
    double max_substep = 1.0 / 120.0;
    unsigned max_substeps = 4096;
  };

  explicit RulePhysics(Rules rules = Rules{}) : rules_(rules) { validate_rules(); }

  const Rules& rules() const { return rules_; }
  const std::vector<Body>& bodies() const { return bodies_; }
  std::vector<Body>& bodies() { return bodies_; }

  std::size_t add(Body body) {
    validate_body(body);
    bodies_.push_back(body);
    return bodies_.size() - 1;
  }

  void clear() { bodies_.clear(); }

  void step(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0)
      throw std::invalid_argument("physics step must be finite and nonnegative");
    if (seconds == 0 || bodies_.empty()) return;
    const auto needed = static_cast<unsigned>(std::ceil(seconds / rules_.max_substep));
    if (needed == 0 || needed > rules_.max_substeps)
      throw std::length_error("physics step exceeds substep budget");
    const double dt = seconds / needed;
    for (unsigned i = 0; i < needed; ++i) substep(dt);
  }

 private:
  static Vec3 add(Vec3 a, const Vec3& b) { return a += b; }
  static Vec3 sub(Vec3 a, const Vec3& b) { return a -= b; }
  static Vec3 mul(Vec3 a, double s) { return a *= s; }
  static double dot(const Vec3& a, const Vec3& b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
  static double norm2(const Vec3& a) { return dot(a, a); }

  void validate_rules() const {
    const auto finite3 = [](const Vec3& v) {
      return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    };
    if (!finite3(rules_.uniform_acceleration) || !std::isfinite(rules_.pair_gravity) ||
        rules_.pair_gravity < 0 || !std::isfinite(rules_.softening) || rules_.softening <= 0 ||
        !std::isfinite(rules_.max_substep) || rules_.max_substep <= 0 || rules_.max_substeps == 0)
      throw std::invalid_argument("invalid physics rules");
  }

  static void validate_body(const Body& b) {
    const auto finite3 = [](const Vec3& v) {
      return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    };
    if (!finite3(b.position) || !finite3(b.velocity) || !std::isfinite(b.inverse_mass) ||
        b.inverse_mass < 0 || !std::isfinite(b.radius) || b.radius <= 0 ||
        !std::isfinite(b.restitution) || b.restitution < 0 || b.restitution > 1)
      throw std::invalid_argument("invalid physics body");
  }

  void substep(double dt) {
    std::vector<Vec3> acceleration(bodies_.size(), rules_.uniform_acceleration);

    if (rules_.pair_gravity > 0) {
      const double soft2 = rules_.softening * rules_.softening;
      for (std::size_t i = 0; i < bodies_.size(); ++i) {
        if (bodies_[i].inverse_mass == 0) continue;
        for (std::size_t j = i + 1; j < bodies_.size(); ++j) {
          if (bodies_[j].inverse_mass == 0) continue;
          const Vec3 d = sub(bodies_[j].position, bodies_[i].position);
          const double r2 = norm2(d) + soft2;
          const double inv_r = 1.0 / std::sqrt(r2);
          const double inv_r3 = inv_r * inv_r * inv_r;
          const double mi = 1.0 / bodies_[i].inverse_mass;
          const double mj = 1.0 / bodies_[j].inverse_mass;
          const Vec3 direction = mul(d, rules_.pair_gravity * inv_r3);
          acceleration[i] += mul(direction, mj);
          acceleration[j] -= mul(direction, mi);
        }
      }
    }

    // Semi-implicit Euler: stable enough for the small fixed substeps used by
    // the interactive engine, deterministic across a fixed IEEE-754 platform.
    for (std::size_t i = 0; i < bodies_.size(); ++i) {
      auto& b = bodies_[i];
      if (b.inverse_mass == 0) continue;
      b.velocity += mul(acceleration[i], dt);
      b.position += mul(b.velocity, dt);
    }

    // Sphere-sphere impulse collisions. The same pair order is always used.
    for (std::size_t i = 0; i < bodies_.size(); ++i)
      for (std::size_t j = i + 1; j < bodies_.size(); ++j)
        collide(bodies_[i], bodies_[j]);
  }

  static void collide(Body& a, Body& b) {
    const Vec3 delta = sub(b.position, a.position);
    const double minimum = a.radius + b.radius;
    const double d2 = norm2(delta);
    if (d2 >= minimum * minimum) return;

    const double distance = std::sqrt(std::max(d2, 1e-24));
    const Vec3 normal = distance > 1e-12 ? mul(delta, 1.0 / distance) : Vec3{1,0,0};
    const double inv_sum = a.inverse_mass + b.inverse_mass;
    if (inv_sum == 0) return;

    const double penetration = minimum - distance;
    const Vec3 correction = mul(normal, penetration / inv_sum);
    if (a.inverse_mass > 0) a.position -= mul(correction, a.inverse_mass);
    if (b.inverse_mass > 0) b.position += mul(correction, b.inverse_mass);

    const Vec3 relative = sub(b.velocity, a.velocity);
    const double closing = dot(relative, normal);
    if (closing >= 0) return;
    const double restitution = std::min(a.restitution, b.restitution);
    const double impulse = -(1.0 + restitution) * closing / inv_sum;
    if (a.inverse_mass > 0) a.velocity -= mul(normal, impulse * a.inverse_mass);
    if (b.inverse_mass > 0) b.velocity += mul(normal, impulse * b.inverse_mass);
  }

  Rules rules_;
  std::vector<Body> bodies_;
};

}  // namespace mundo_x
