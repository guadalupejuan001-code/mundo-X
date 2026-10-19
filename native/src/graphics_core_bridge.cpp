#include "mundo_x/engine_session.hpp"
#include <cstring>

namespace {
using Session = mundo_x::EngineSession;
// Diagnostics cannot themselves allocate or throw through the C/JNI boundary.
thread_local char last_error[512] = {};

void save_error(const char* message) noexcept {
  if (!message) message = "Unknown session error";
  const auto size = std::min(std::strlen(message), sizeof(last_error) - 1);
  std::memcpy(last_error, message, size);
  last_error[size] = '\0';
}

template<class Result, class Action>
Result checked(Action&& action, Result failure) noexcept {
  try {
    last_error[0] = '\0';
    return action();
  } catch (const std::exception& error) {
    save_error(error.what());
  } catch (...) {
    save_error("Unknown session error");
  }
  return failure;
}

Session& session(void* pointer) {
  if (!pointer) throw std::invalid_argument("Null session handle");
  return *static_cast<Session*>(pointer);
}

void write_history(const Session::Snapshot& saved, std::uint64_t* out,
                    std::size_t capacity) {
  if (capacity < saved.gates.size()) throw std::length_error("Small history output buffer");
  if (!saved.gates.empty() && !out) throw std::invalid_argument("Null history output buffer");
  for (std::size_t i = 0; i < saved.gates.size(); ++i) {
    out[3*i] = saved.gates[i].operation;
    out[3*i+1] = saved.gates[i].first;
    out[3*i+2] = saved.gates[i].second;
  }
}
} // namespace

extern "C" {

const char* mx_last_error() { return last_error; }
const char* mx_backend_name(int kind) {
  return checked<const char*>([&] { return Session::backend_name(static_cast<Session::Backend>(kind)); }, nullptr);
}
std::uint64_t mx_capabilities(int kind) {
  return checked<std::uint64_t>([&] { return Session::capabilities(static_cast<Session::Backend>(kind)); }, 0);
}
std::uint64_t mx_gate_mask(int kind) {
  return checked<std::uint64_t>([&] { return Session::gate_mask(static_cast<Session::Backend>(kind)); }, 0);
}
void* mx_create(int kind, std::size_t qubits) {
  return checked<void*>([&] { return new Session(static_cast<Session::Backend>(kind), qubits); }, nullptr);
}
void mx_destroy(void* pointer) {
  (void)checked<int>([&] { delete static_cast<Session*>(pointer); return 0; }, -1);
}
int mx_apply(void* pointer, const std::uint64_t* triples, std::size_t count) {
  return checked<int>([&] {
    auto& owner = session(pointer);
    owner.apply(Session::gates_from_triples(triples, count));
    return 0;
  }, -1);
}
int mx_query(void* pointer, const char* const* strings, std::size_t count, double* out) {
  return checked<int>([&] {
    auto& owner = session(pointer);
    if (count && (!strings || !out)) throw std::invalid_argument("Null query buffer");
    std::vector<std::string> paulis;
    paulis.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
      if (!strings[i]) throw std::invalid_argument("Null Pauli operator");
      paulis.emplace_back(strings[i]);
    }
    const auto values = owner.query(paulis);
    // Query parsing and computation have completed before any output is written.
    if (count) std::copy(values.begin(), values.end(), out);
    return 0;
  }, -1);
}
int mx_generator(void* pointer, std::size_t row, char* out, std::size_t capacity) {
  return checked<int>([&] {
    if (!out) throw std::invalid_argument("Null generator buffer");
    const auto text = session(pointer).generator(row);
    if (capacity <= text.size()) throw std::length_error("Small generator output buffer");
    std::memcpy(out, text.c_str(), text.size() + 1);
    return 0;
  }, -1);
}
std::size_t mx_qubits(void* pointer) {
  return checked<std::size_t>([&] { return session(pointer).qubits(); }, 0);
}
int mx_backend(void* pointer) {
  return checked<int>([&] { return static_cast<int>(session(pointer).backend()); }, -1);
}
std::uint64_t mx_revision(void* pointer) {
  return checked<std::uint64_t>([&] { return session(pointer).revision(); }, 0);
}
std::uint64_t mx_epoch(void* pointer) {
  return checked<std::uint64_t>([&] { return session(pointer).epoch(); }, 0);
}
std::uint64_t mx_rebuilds(void* pointer) {
  return checked<std::uint64_t>([&] { return session(pointer).rebuilds(); }, 0);
}
std::size_t mx_state_bytes(void* pointer) {
  return checked<std::size_t>([&] { return session(pointer).state_bytes(); }, 0);
}
std::size_t mx_observer_bytes(void* pointer) {
  return checked<std::size_t>([&] { return session(pointer).observer_bytes(); }, 0);
}
std::size_t mx_history_bytes(void* pointer) {
  return checked<std::size_t>([&] { return session(pointer).history_bytes(); }, 0);
}
std::size_t mx_history_count(void* pointer) {
  return checked<std::size_t>([&] { return session(pointer).history_count(); }, 0);
}
int mx_history(void* pointer, std::uint64_t* triples, std::size_t capacity) {
  return checked<int>([&] {
    write_history(session(pointer).snapshot(), triples, capacity);
    return 0;
  }, -1);
}
int mx_snapshot(void* pointer, std::uint64_t* triples, std::size_t capacity,
                 std::size_t* actual_count, std::uint64_t* revision) {
  return checked<int>([&] {
    if (!actual_count || !revision) throw std::invalid_argument("Null snapshot metadata buffer");
    const auto saved = session(pointer).snapshot();
    *actual_count = saved.gates.size();
    *revision = saved.revision;
    if (!triples && capacity == 0) return 0;
    write_history(saved, triples, capacity);
    return 0;
  }, -1);
}
int mx_restore(void* pointer, const std::uint64_t* triples, std::size_t count,
                std::uint64_t revision) {
  return checked<int>([&] {
    auto& owner = session(pointer);
    Session::Snapshot saved;
    saved.backend = owner.backend();
    saved.qubits = owner.qubits();
    saved.revision = revision;
    saved.gates = Session::gates_from_triples(triples, count);
    owner.restore(saved);
    return 0;
  }, -1);
}
} // extern C
