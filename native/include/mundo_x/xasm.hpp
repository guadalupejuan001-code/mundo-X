#pragma once

#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>

#include "mundo_x/compact_core.hpp"
#include "mundo_x/exact_t_experiment.hpp"
#include "mundo_x/ether_renderer.hpp"
#include "mundo_x/ether_mil_bridge.hpp"
#include "mundo_x/pauli_observer.hpp"
#include "mundo_x/mil_scattering.hpp"
#include "mundo_x/visual_scene.hpp"

namespace mundo_x {

struct XasmRunResult {
  std::size_t qubits = 0;
  std::size_t state_bytes = 0;
  std::size_t instructions = 0;
  std::size_t visual_instructions = 0;
  std::size_t scene_objects = 0;
  std::size_t scene_rule_bytes = 0;
  bool assertions_passed = true;
  std::string output;
  // Final scene only. Commands never emit a partial export.
  std::string scene_text;
  std::string backend = "compact";
  std::size_t logical_payload_bytes = 0;
  std::size_t components = 0;
  std::size_t t_gates = 0;
  std::size_t scattering_runs = 0;
  std::size_t render_instructions = 0;
  std::size_t render_width = 0, render_height = 0, render_bodies = 0;
  std::uint64_t render_hash = 0;
  std::uint64_t render_candidate_tests = 0, render_brute_tests = 0;
};

inline std::string trim(const std::string& s) {
  const auto first = s.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return "";
  const auto last = s.find_last_not_of(" \t\r\n");
  return s.substr(first, last - first + 1);
}

namespace xasm_detail {
template <typename UInt>
inline UInt read_unsigned(std::istringstream& input, std::size_t line,
                          const std::string& syntax) {
  std::string token;
  UInt value = 0;
  if (input >> token) {
    const auto parsed = std::from_chars(token.data(), token.data() + token.size(), value);
    if (parsed.ec == std::errc{} && parsed.ptr == token.data() + token.size())
      return value;
  }
  throw std::runtime_error("line " + std::to_string(line) + ": expected " + syntax);
}

inline void require_end(std::istringstream& input, std::size_t line) {
  std::string extra;
  if (input >> extra)
    throw std::runtime_error("line " + std::to_string(line) + ": unexpected argument '" + extra + "'");
}

inline std::int32_t read_scene_seed(std::istringstream& input, std::size_t line) {
  std::string token;
  std::int32_t value = 0;
  if (input >> token) {
    const auto parsed = std::from_chars(token.data(), token.data() + token.size(), value);
    if (parsed.ec == std::errc{} && parsed.ptr == token.data() + token.size()) return value;
  }
  throw std::runtime_error("line " + std::to_string(line) + ": expected scene <count> <int32 seed>");
}

inline double read_finite(std::istringstream& input, std::size_t line,
                          const std::string& syntax) {
  std::string token;
  double value = 0.0;
  if ((input >> token) && token.find_first_not_of("+-0123456789.eE") == std::string::npos) {
    std::istringstream number(token);
    number.imbue(std::locale::classic());
    number >> std::noskipws >> value;
    if (number && number.eof() && std::isfinite(value)) return value;
  }
  throw std::runtime_error("line " + std::to_string(line) + ": expected " + syntax);
}

inline double read_expected_value(std::istringstream& input) {
  std::string token;
  double value = 0.0;
  if ((input >> token) && token.find_first_not_of("+-0123456789.eE") == std::string::npos) {
    // Some standard libraries accept hexadecimal floats in stream extraction.
    // Restrict the token to decimal characters, then require a complete,
    // locale-independent conversion with valid syntax and a finite result.
    std::istringstream number(token);
    number.imbue(std::locale::classic());
    number >> std::noskipws >> value;
    if (number && number.eof() && std::isfinite(value)) return value;
  }
  throw std::runtime_error("expected assert expectation <Pauli> <finite value>");
}

inline std::string precise_value(double value) {
  std::ostringstream text;
  text.imbue(std::locale::classic());
  text << std::setprecision(std::numeric_limits<double>::max_digits10) << value;
  return text.str();
}
}  // namespace xasm_detail

inline XasmRunResult run_xasm(std::istream& input) {
  XasmRunResult result;
  std::unique_ptr<CompactCore> core;
  std::unique_ptr<VisualScene> scene;
  std::unique_ptr<ReducedExactTRepresentation> reduced;
  std::unique_ptr<EtherRenderer> renderer;
  std::unique_ptr<MilEtherRenderBridge> mil_bridge;
  bool rendered = false;
  std::unique_ptr<mundo_x_bridge::PauliObserver> observer;
  bool backend_declared = false;
  std::uint64_t scattering_work = 0;
  std::ostringstream out;
  // Reproducible pseudorandom sampling, not a source of physical randomness.
  std::mt19937_64 random(0);

  const auto expectation = [&](const std::string& text) {
    auto pauli = mundo_x_bridge::Pauli::parse(text, result.qubits);
    if (core) {
      // A read-only index is shared until the next state operation. In
      // particular, a long multiword Pauli never becomes a dense statevector.
      if (!observer) observer = std::make_unique<mundo_x_bridge::PauliObserver>(*core);
      return observer->expectation(std::move(pauli));
    }
    // Reduced uses the same packed multiword Pauli convention as wide
    // CompactCore observables. Its raw expectation is unnormalized.
    const double norm = reduced->sparse_norm();
    if (!(norm > 0.0) || !std::isfinite(norm))
      throw std::runtime_error("non-finite or zero reduced norm");
    const auto value = reduced->pauli_expectation(pauli.x, pauli.z,
                                                 static_cast<std::uint8_t>(pauli.phase)) / norm;
    if (!std::isfinite(value.real()) || !std::isfinite(value.imag()) ||
        std::abs(value.imag()) > 1e-10)
      throw std::runtime_error("invalid Hermitian Pauli expectation");
    return value.real();
  };

  // The MIL source is authoritative; material signals are updated only when
  // state-changing gates invalidate it, not once per ray or per pixel.
  const auto refresh_mil_materials = [&]() {
    if(mil_bridge)mil_bridge->refresh();
  };
  const auto invalidate_mil_materials = [&]() {
    if(mil_bridge)mil_bridge->invalidate();
    rendered=false;
  };

  std::string line;
  std::size_t line_number = 0;

  while (std::getline(input, line)) {
    ++line_number;

    const auto comment = line.find('#');
    if (comment != std::string::npos) line = line.substr(0, comment);
    line = trim(line);
    if (line.empty()) continue;

    std::istringstream in(line);
    std::string op;
    in >> op;

    if (op == "scene") {
      if (scene) throw std::runtime_error("line " + std::to_string(line_number) + ": scene already declared");
      const auto count = xasm_detail::read_unsigned<std::uint32_t>(in, line_number, "scene <count> <int32 seed>");
      const auto seed = xasm_detail::read_scene_seed(in, line_number);
      xasm_detail::require_end(in, line_number);
      scene = std::make_unique<VisualScene>(count, seed);
      continue;
    }

    if (op == "scene_edit" || op == "scene_clear" || op == "scene_info" || op == "scene_probz") {
      if (!scene) throw std::runtime_error("line " + std::to_string(line_number) + ": declare scene first");
      if (op == "scene_edit") {
        const auto id = xasm_detail::read_unsigned<std::uint32_t>(in, line_number, "scene_edit <id> <state 0..7>");
        const auto state = xasm_detail::read_unsigned<std::uint32_t>(in, line_number, "scene_edit <id> <state 0..7>");
        xasm_detail::require_end(in, line_number);
        scene->edit(id, state);
        ++result.visual_instructions;
      } else if (op == "scene_clear") {
        xasm_detail::require_end(in, line_number);
        scene->clear_edits();
        ++result.visual_instructions;
      } else if (op == "scene_info") {
        xasm_detail::require_end(in, line_number);
        out << "SCENE objects=" << scene->count() << " edits=" << scene->edit_count()
            << " logical_rule_bytes=" << scene->logical_rule_bytes()
            << " version=" << scene->version() << "\n";
      } else {
        const auto id = xasm_detail::read_unsigned<std::uint32_t>(in, line_number, "scene_probz <id> <q>");
        const auto q = xasm_detail::read_unsigned<std::size_t>(in, line_number, "scene_probz <id> <q>");
        xasm_detail::require_end(in, line_number);
        if (!core) throw std::runtime_error("line " + std::to_string(line_number) + ": scene_probz requires backend compact qubits");
        scene->edit_state(id);  // Validate the destination before the query.
        const double p1 = core->probability_one_z(q);
        // A snapshot of this marginal only: no measurement, RNG consumption,
        // continuous binding, or claim that heights encode the full state.
        std::uint32_t state;
        if (p1 == 0.0) state = 0;
        else if (p1 == 0.5) state = 3;
        else if (p1 == 1.0) state = 6;
        else throw std::runtime_error("unsupported stabilizer probability for scene_probz");
        scene->edit(id, state);
        ++result.visual_instructions;
      }
      continue;
    }

    if (op == "render" || op == "render_camera" || op == "render_body" ||
        op == "render_clear" || op == "render_plan" || op == "render_frame" ||
        op == "render_info" || op == "render_pixel" || op == "render_assert_exact" || op == "render_train" ||
        op == "render_bind" || op == "render_bind_pauli" || op == "render_unbind" ||
        op == "render_quantum") {
      if (op == "render") {
        if (renderer) throw std::runtime_error("line " + std::to_string(line_number) + ": render already declared");
        const auto width=xasm_detail::read_unsigned<std::size_t>(in,line_number,"render <width> <height> <tile>");
        const auto height=xasm_detail::read_unsigned<std::size_t>(in,line_number,"render <width> <height> <tile>");
        const auto tile=xasm_detail::read_unsigned<std::size_t>(in,line_number,"render <width> <height> <tile>");
        xasm_detail::require_end(in,line_number);
        renderer=std::make_unique<EtherRenderer>(width,height,tile);
      } else {
        if(!renderer)throw std::runtime_error("line " + std::to_string(line_number) + ": declare render first");
        if(op=="render_camera"){
          EtherRenderer::Vec3 eye{
            xasm_detail::read_finite(in,line_number,"render_camera <ex> <ey> <ez> <tx> <ty> <tz> <fov>"),
            xasm_detail::read_finite(in,line_number,"render_camera <ex> <ey> <ez> <tx> <ty> <tz> <fov>"),
            xasm_detail::read_finite(in,line_number,"render_camera <ex> <ey> <ez> <tx> <ty> <tz> <fov>")};
          EtherRenderer::Vec3 target{
            xasm_detail::read_finite(in,line_number,"render_camera <ex> <ey> <ez> <tx> <ty> <tz> <fov>"),
            xasm_detail::read_finite(in,line_number,"render_camera <ex> <ey> <ez> <tx> <ty> <tz> <fov>"),
            xasm_detail::read_finite(in,line_number,"render_camera <ex> <ey> <ez> <tx> <ty> <tz> <fov>")};
          const double fov=xasm_detail::read_finite(in,line_number,"render_camera <ex> <ey> <ez> <tx> <ty> <tz> <fov>");
          xasm_detail::require_end(in,line_number);renderer->set_camera(eye,target,fov);rendered=false;++result.render_instructions;
        }else if(op=="render_body"){
          EtherRenderer::Body body;
          body.id=xasm_detail::read_unsigned<std::uint32_t>(in,line_number,"render_body <id> <x> <y> <z> <radius> <r> <g> <b>");
          body.center={
            xasm_detail::read_finite(in,line_number,"render_body <id> <x> <y> <z> <radius> <r> <g> <b>"),
            xasm_detail::read_finite(in,line_number,"render_body <id> <x> <y> <z> <radius> <r> <g> <b>"),
            xasm_detail::read_finite(in,line_number,"render_body <id> <x> <y> <z> <radius> <r> <g> <b>")};
          body.radius=xasm_detail::read_finite(in,line_number,"render_body <id> <x> <y> <z> <radius> <r> <g> <b>");
          body.color={{
            xasm_detail::read_finite(in,line_number,"render_body <id> <x> <y> <z> <radius> <r> <g> <b>"),
            xasm_detail::read_finite(in,line_number,"render_body <id> <x> <y> <z> <radius> <r> <g> <b>"),
            xasm_detail::read_finite(in,line_number,"render_body <id> <x> <y> <z> <radius> <r> <g> <b>")}};
          xasm_detail::require_end(in,line_number);renderer->add_body(body);rendered=false;++result.render_instructions;
        }else if(op=="render_bind" || op=="render_bind_pauli"){
          if(!reduced)throw std::runtime_error("line "+std::to_string(line_number)+": render_bind requires MIL backend reduced qubits");
          const auto id=xasm_detail::read_unsigned<std::uint32_t>(in,line_number,
              "render_bind <id> <X|Y|Z> <qubit> <strength>");
          if(!mil_bridge)mil_bridge=std::make_unique<MilEtherRenderBridge>(*reduced,*renderer);
          if(op=="render_bind"){
            std::string axis;
            if(!(in>>axis)||axis.size()!=1||axis.find_first_not_of("XYZ")!=std::string::npos)
              throw std::runtime_error("line "+std::to_string(line_number)+": expected X, Y, or Z");
            const auto q=xasm_detail::read_unsigned<std::size_t>(in,line_number,
                "render_bind <id> <X|Y|Z> <qubit> <strength>");
            const double strength=xasm_detail::read_finite(in,line_number,"render strength");
            xasm_detail::require_end(in,line_number);
            mil_bridge->bind_axis(id,axis[0],q,strength);
          }else{
            std::string pauli;
            if(!(in>>pauli))throw std::runtime_error("line "+std::to_string(line_number)+": expected full Pauli");
            const double strength=xasm_detail::read_finite(in,line_number,"render strength");
            xasm_detail::require_end(in,line_number);
            mil_bridge->bind(id,pauli,strength);
          }
          rendered=false;++result.render_instructions;
        }else if(op=="render_unbind"){
          const auto id=xasm_detail::read_unsigned<std::uint32_t>(in,line_number,"render_unbind <id>");
          xasm_detail::require_end(in,line_number);
          if(!mil_bridge)throw std::runtime_error("line "+std::to_string(line_number)+": no MIL render binding");
          mil_bridge->unbind(id);rendered=false;++result.render_instructions;
        }else if(op=="render_quantum"){
          const auto id=xasm_detail::read_unsigned<std::uint32_t>(in,line_number,"render_quantum <id>");
          xasm_detail::require_end(in,line_number);
          if(!mil_bridge)throw std::runtime_error("line "+std::to_string(line_number)+": no MIL render binding");
          refresh_mil_materials();
          out<<"RENDER_QUANTUM id="<<id<<" value="
             <<xasm_detail::precise_value(mil_bridge->signal(id))<<"\n";
        }else if(op=="render_clear"){
          xasm_detail::require_end(in,line_number);
          if(mil_bridge){mil_bridge->clear();mil_bridge.reset();}
          renderer->clear_bodies();rendered=false;++result.render_instructions;
        }else if(op=="render_plan"){
          xasm_detail::require_end(in,line_number);renderer->plan();rendered=false;++result.render_instructions;
        }else if(op=="render_frame"){
          xasm_detail::require_end(in,line_number);
          refresh_mil_materials();renderer->render(false);
          rendered=true;++result.render_instructions;
        }else if(op=="render_assert_exact"){
          xasm_detail::require_end(in,line_number);
          refresh_mil_materials();const auto st=renderer->render(true);
          rendered=true;++result.render_instructions;
          const bool ok=st.exact_mismatches==0;result.assertions_passed=result.assertions_passed&&ok;
          out<<"ASSERT render_exact "<<(ok?"PASS":"FAIL")<<" mismatches="<<st.exact_mismatches<<"\n";
        }else if(op=="render_train"){
          xasm_detail::require_end(in,line_number);
          refresh_mil_materials();const auto trained=renderer->train_tiles();
          rendered=true;++result.render_instructions;
          out<<"RENDER_TRAIN tile="<<trained.chosen_tile
             <<" median_ms="<<xasm_detail::precise_value(trained.median_ms)
             <<" candidate_tests="<<trained.candidate_tests
             <<" distance_pruned="<<trained.distance_pruned_candidates
             <<" mismatches="<<trained.exact_mismatches
             <<" hash="<<trained.frame_hash<<"\n";
        }else if(op=="render_info"){
          xasm_detail::require_end(in,line_number);
          if(!rendered){refresh_mil_materials();renderer->render(false);}
          const auto& st=renderer->stats();
          out<<"RENDER "<<st.width<<"x"<<st.height<<" tile="<<st.tile
             <<" regions="<<st.tiles_x*st.tiles_y<<" bodies="<<st.bodies
             <<" candidate_refs="<<st.candidate_references<<" brute_refs="<<st.brute_references
             <<" candidate_tests="<<st.candidate_tests<<" brute_tests="<<st.brute_tests
             <<" distance_pruned="<<st.distance_pruned_candidates
             <<" test_ratio="<<xasm_detail::precise_value(st.test_ratio())
             <<" hash="<<st.frame_hash<<"\n";
          rendered=true;
        }else{
          const auto x=xasm_detail::read_unsigned<std::size_t>(in,line_number,"render_pixel <x> <y>");
          const auto y=xasm_detail::read_unsigned<std::size_t>(in,line_number,"render_pixel <x> <y>");
          xasm_detail::require_end(in,line_number);if(!rendered)throw std::runtime_error("line " + std::to_string(line_number) + ": render_frame first");
          const auto& p=renderer->pixel(x,y);
          out<<"PIXEL x="<<x<<" y="<<y<<" body="<<p.body<<" depth="<<xasm_detail::precise_value(p.depth)
             <<" rgba="<<p.rgba<<"\n";
        }
      }
      continue;
    }

    try {
      const auto read_size = [&](const std::string& usage) {
        std::string token;
        if (!(in >> token) || token.empty())
          throw std::runtime_error("expected " + usage);
        std::size_t value = 0;
        for (const char digit : token) {
          if (digit < '0' || digit > '9')
            throw std::runtime_error("expected " + usage + " (unsigned decimal integer)");
          const auto part = static_cast<std::size_t>(digit - '0');
          if (value > (std::numeric_limits<std::size_t>::max() - part) / 10)
            throw std::runtime_error("integer out of range in " + usage);
          value = value * 10 + part;
        }
        return value;
      };
      const auto require_end = [&]() {
        std::string extra;
        if (in >> extra)
          throw std::runtime_error("unexpected argument '" + extra + "'");
      };

      if (op == "backend") {
        if (core || reduced)
          throw std::runtime_error("backend must be declared before qubits");
        if (backend_declared) throw std::runtime_error("backend already declared");
        std::string backend;
        if (!(in >> backend) || (backend != "compact" && backend != "reduced"))
          throw std::runtime_error("expected backend <compact|reduced>");
        require_end();
        result.backend = backend;
        backend_declared = true;
        continue;
      }

      if (op == "qubits") {
        if (core || reduced) throw std::runtime_error("qubits already declared");
        const auto n = read_size("qubits <n>");
        require_end();
        if (result.backend == "reduced") {
          if (n == 0 || n > ReducedExactTRepresentation::kMaxQubits)
            throw std::runtime_error("reduced XASM supports 1..1000 qubits");
          // Width is independent of non-Clifford component growth. Stop at
          // this explicit budget; never truncate components or fall back dense.
          reduced = std::make_unique<ReducedExactTRepresentation>(n, 65536);
        } else {
          if (n == 0) throw std::runtime_error("expected qubits <n> with n > 0");
          core = std::make_unique<CompactCore>(n);
        }
        result.qubits = n;
        continue;
      }


      // Specialized coherent two-particle lattice scattering. This physics
      // backend is independent of a qubit register, so MX/XASM need not invent
      // placeholder qubits. The run stays bounded across all calls in a file.
      if (op == "scatter") {
        const auto sites = read_size("scatter <sites> <steps> <J_milli> <U_milli>");
        const auto steps = read_size("scatter <sites> <steps> <J_milli> <U_milli>");
        const auto j_milli = read_size("scatter <sites> <steps> <J_milli> <U_milli>");
        const auto u_milli = read_size("scatter <sites> <steps> <J_milli> <U_milli>");
        require_end();
        if (j_milli == 0 || j_milli > 100000 || u_milli > 100000)
          throw std::invalid_argument("scatter requires J_milli 1..100000 and U_milli 0..100000");
        const auto work = MilScattering::estimate_work(sites, steps, u_milli == 0);
        if (scattering_work > MilScattering::default_work_budget ||
            work > MilScattering::default_work_budget - scattering_work)
          throw std::length_error("scatter cumulative work budget exceeded");
        MilScattering::Input cfg;
        cfg.sites = sites;
        cfg.steps = steps;
        cfg.hopping = static_cast<double>(j_milli) / 1000.0;
        cfg.contact = static_cast<double>(u_milli) / 1000.0;
        const auto collision = MilScattering::run(cfg);
        scattering_work += work;
        ++result.scattering_runs;
        ++result.instructions;
        out << "SCATTER model=two_distinguishable_particles_1d"
            << " solver=" << (collision.factorized_free ? "free_product_fft_1d" : "interacting_fft_2d")
            << " sites=" << sites
            << " steps=" << steps
            << " J=" << xasm_detail::precise_value(cfg.hopping)
            << " U=" << xasm_detail::precise_value(cfg.contact)
            << " dt=" << xasm_detail::precise_value(cfg.dt)
            << " norm=" << xasm_detail::precise_value(collision.norm)
            << " max_norm_error=" << xasm_detail::precise_value(collision.max_norm_error)
            << " initial_contact=" << xasm_detail::precise_value(collision.initial_contact)
            << " peak_contact=" << xasm_detail::precise_value(collision.peak_contact)
            << " final_contact=" << xasm_detail::precise_value(collision.final_contact)
            << " exchanged_order=" << xasm_detail::precise_value(collision.exchanged_order)
            << " purity=" << (sites <= 128 ? xasm_detail::precise_value(
                  MilScattering::reduced_purity(collision.amplitudes, sites)) : "unavailable")
            << " state_bytes=" << collision.state_bytes
            << " scratch_bytes=" << collision.scratch_bytes
            << " estimated_work=" << collision.estimated_work << "\n";
        continue;
      }

      if (!core && !reduced) throw std::runtime_error("declare qubits first");

      if (op == "h" || op == "x" || op == "z" || op == "s") {
        const auto q = read_size(op + " <q>");
        require_end();
        const auto apply = [&](auto& state) {
          if (op == "h") state.hadamard(q);
          else if (op == "x") state.pauli_x(q);
          else if (op == "z") state.pauli_z(q);
          else state.phase_s(q);
        };
        if (reduced) apply(*reduced);
        else apply(*core);
        observer.reset();
        invalidate_mil_materials();
        ++result.instructions;
      } else if (op == "cx" || op == "cz") {
        const auto control = read_size(op + " <control> <target>");
        const auto target = read_size(op + " <control> <target>");
        require_end();
        const auto apply = [&](auto& state) {
          if (op == "cx") state.cnot(control, target);
          else state.controlled_z(control, target);
        };
        if (reduced) apply(*reduced);
        else apply(*core);
        observer.reset();
        invalidate_mil_materials();
        ++result.instructions;
      } else if (op == "t") {
        const auto q = read_size("t <q>");
        require_end();
        if (!reduced) throw std::runtime_error("t requires backend reduced before qubits");
        if (reduced->t_count() >= 128)
          throw std::runtime_error("reduced XASM safety limit is 128 T gates");
        reduced->phase_t(q);
        invalidate_mil_materials();
        ++result.instructions;
      } else if (op == "tdg" || op == "phase" || op == "cphase") {
        if(!reduced)throw std::runtime_error(op+" requires MIL backend reduced");
        if(op=="tdg"){
          const auto q=read_size("tdg <q>");
          require_end();
          if(reduced->t_count()>=128)
            throw std::runtime_error("reduced XASM safety limit is 128 T gates");
          reduced->phase_t_dagger(q);
        }else if(op=="phase"){
          const auto q=read_size("phase <q> <theta>");
          const auto angle=xasm_detail::read_finite(in,line_number,"phase <q> <theta>");
          require_end();reduced->phase(q,angle);
        }else{
          const auto c=read_size("cphase <control> <target> <theta>");
          const auto t=read_size("cphase <control> <target> <theta>");
          const auto angle=xasm_detail::read_finite(in,line_number,"cphase <control> <target> <theta>");
          require_end();reduced->controlled_phase(c,t,angle);
        }
        invalidate_mil_materials();
        ++result.instructions;
      } else if (op == "seed") {
        std::string token;
        std::uint64_t seed = 0;
        if (!(in >> token)) throw std::runtime_error("expected seed <uint64>");
        const auto parsed = std::from_chars(token.data(), token.data() + token.size(), seed);
        if (parsed.ec != std::errc{} || parsed.ptr != token.data() + token.size())
          throw std::runtime_error("expected seed <uint64>");
        require_end();
        random.seed(seed);
      } else if (op == "probz") {
        const auto q = read_size("probz <q>");
        require_end();
        if (!core) throw std::runtime_error("probz requires backend compact");
        const auto p1 = core->probability_one_z(q);
        out << "PROB_Z q=" << q << " p0=" << (1.0 - p1) << " p1=" << p1 << "\n";
      } else if (op == "measure") {
        const auto q = read_size("measure <q>");
        require_end();
        if (!core) throw std::runtime_error("measure requires backend compact");
        const auto measured = core->measure_z(q, random);
        out << "MEASURE_Z q=" << q << " outcome=" << measured.outcome_one
            << " probability=" << measured.probability << " random=" << measured.random << "\n";
        observer.reset();
        invalidate_mil_materials();
        ++result.instructions;
      } else if (op == "expect") {
        std::string pauli;
        if (!(in >> pauli)) throw std::runtime_error("expected expect <Pauli>");
        require_end();
        const double value = expectation(pauli);
        out << "EXPECT pauli=" << pauli << " value=" << xasm_detail::precise_value(value) << "\n";
      } else if (op == "show") {
        require_end();
        out << "STATE\n";
        if (reduced) {
          out << "backend=reduced\n"
              << "components=" << reduced->component_count() << "\n"
              << "t_gates=" << reduced->t_count() << "\n"
              << "logical_payload_bytes=" << reduced->logical_payload_bytes() << "\n";
        } else {
          for (std::size_t r = 0; r < core->qubits(); ++r)
            out << core->generator_string(r) << "\n";
        }
      } else if (op == "bytes") {
        require_end();
        if (reduced) out << "LOGICAL_PAYLOAD_BYTES " << reduced->logical_payload_bytes() << "\n";
        else out << "STATE_BYTES " << core->state_bytes() << "\n";
      } else if (op == "assert") {
        std::string what;
        if (!(in >> what)) throw std::runtime_error("expected assert <ghz|one|fidelity|norm|expectation>");

        bool ok = false;
        std::string details;
        if (what == "expectation") {
          std::string pauli;
          if (!(in >> pauli)) throw std::runtime_error("expected assert expectation <Pauli> <finite value>");
          const double expected = xasm_detail::read_expected_value(in);
          require_end();
          const double value = expectation(pauli);
          ok = std::abs(value - expected) <= 1e-10;
          details = " pauli=" + pauli + " value=" + xasm_detail::precise_value(value) +
                    " expected=" + xasm_detail::precise_value(expected) + " tolerance=1e-10";
        } else {
          require_end();
          if (what == "ghz" || what == "one") {
            if (!core) throw std::runtime_error("assert " + what + " requires backend compact");
            ok = what == "ghz" ? core->is_ghz_signature() : core->hello_is_one();
          } else if (what == "fidelity" || what == "norm") {
            if (!reduced) throw std::runtime_error("assert " + what + " requires backend reduced");
            if (what == "fidelity" && result.qubits > 12)
              throw std::runtime_error("assert fidelity uses a dense reference limited to 12 qubits; use assert norm and assert expectation for larger registers");
            const double value = what == "fidelity" ? reduced->fidelity() : reduced->sparse_norm();
            // Keep the established XASM v0.2 acceptance tolerance. FP64 accuracy
            // is covered independently by the native precision regressions.
            ok = std::isfinite(value) && std::abs(value - 1.0) <= 3e-5;
          } else {
            throw std::runtime_error("unknown assertion '" + what + "'");
          }
        }

        out << "ASSERT " << what << " " << (ok ? "PASS" : "FAIL") << details << "\n";
        result.assertions_passed = result.assertions_passed && ok;
      } else {
        throw std::runtime_error("unknown instruction '" + op + "'");
      }
    } catch (const std::exception& e) {
      throw std::runtime_error("line " + std::to_string(line_number) + ": " + e.what());
    }
  }

  if (input.bad() || (input.fail() && !input.eof()))
    throw std::runtime_error("failed to read XASM program");
  if (!core && !reduced && !scene && !renderer && !result.scattering_runs) throw std::runtime_error("program did not declare qubits, scene, render or scatter");

  if (reduced) {
    result.logical_payload_bytes = reduced->logical_payload_bytes();
    result.components = reduced->component_count();
    result.t_gates = reduced->t_count();
  } else if (core) {
    result.state_bytes = core->state_bytes();
  }
  if (scene) {
    result.scene_objects = scene->count();
    result.scene_rule_bytes = scene->logical_rule_bytes();
    result.scene_text = scene->export_text();
  }
  if (renderer) {
    const auto& st=renderer->stats();
    result.render_width=st.width;result.render_height=st.height;
    result.render_bodies=st.bodies;result.render_hash=st.frame_hash;
    result.render_candidate_tests=st.candidate_tests;result.render_brute_tests=st.brute_tests;
  }
  result.output = out.str();
  return result;
}

inline XasmRunResult run_xasm_file(const std::string& path) {
  std::ifstream file(path);
  if (!file) throw std::runtime_error("cannot open XASM program: " + path);
  return run_xasm(file);
}

}  // namespace mundo_x
