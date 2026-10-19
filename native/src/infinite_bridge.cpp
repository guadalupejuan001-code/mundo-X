#include "mundo_x/infinite_bridge.hpp"
#include "mundo_x/infinite_session.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>

namespace {
struct Handle { std::shared_ptr<mundo_x::InfiniteSession> session; };
thread_local char last_error[512] = {};

void clear_error() noexcept { last_error[0] = '\0'; }
void error_text(const char* message) noexcept {
  std::snprintf(last_error, sizeof(last_error), "%s", message);
}

mundo_x::InfiniteSession& session(void* handle) {
  if (!handle) throw std::invalid_argument("null infinite session handle");
  return *static_cast<Handle*>(handle)->session;
}

mundo_x::InfiniteCore::Background background(int value) {
  if (value == 0) return mundo_x::InfiniteCore::Background::Zero;
  if (value == 1) return mundo_x::InfiniteCore::Background::BellPairs;
  throw std::invalid_argument("infinite background id must be 0 or 1");
}

std::size_t bounded_length(const char* text, std::size_t maximum) {
  if (!text) throw std::invalid_argument("null input string");
  std::size_t length = 0;
  while (length <= maximum && text[length] != '\0') ++length;
  if (length > maximum) throw std::length_error("input exceeds transport text budget");
  return length;
}

std::unique_ptr<char[]> owned_string(const std::string& text) {
  auto output = std::make_unique<char[]>(text.size() + 1);
  std::memcpy(output.get(), text.c_str(), text.size() + 1);
  return output;
}

template<class Operation>
int status(Operation operation) noexcept {
  clear_error();
  try { operation(); return 0; }
  catch (const std::exception& error) { error_text(error.what()); }
  catch (...) { error_text("unknown infinite-session error"); }
  return -1;
}
}  // namespace

namespace mundo_x {
void* share_infinite_session(std::shared_ptr<InfiniteSession> value) {
  if (!value) throw std::invalid_argument("null shared infinite session");
  return new Handle{std::move(value)};
}

std::shared_ptr<InfiniteSession> infinite_session_from_handle(void* handle) {
  if (!handle) throw std::invalid_argument("null infinite session handle");
  return static_cast<Handle*>(handle)->session;
}
}  // namespace mundo_x

extern "C" {
const char* mxi_last_error(void) { return last_error; }

void* mxi_create(int value) {
  clear_error();
  try {
    return mundo_x::share_infinite_session(
        std::make_shared<mundo_x::InfiniteSession>(background(value)));
  } catch (const std::exception& error) { error_text(error.what()); }
  catch (...) { error_text("unknown infinite-session creation error"); }
  return nullptr;
}

void mxi_destroy(void* handle) { delete static_cast<Handle*>(handle); }

int mxi_execute(void* handle, const char* program, char** out) {
  return status([&] {
    if (!out) throw std::invalid_argument("null output string pointer");
    const auto length = bounded_length(program, mundo_x::infinite_program_detail::max_program_bytes);
    const std::string request(program, length);
    auto result = session(handle).execute_prepared(request, owned_string);
    *out = result.release();
  });
}

int mxi_snapshot(void* handle, char** out) {
  return status([&] {
    if (!out) throw std::invalid_argument("null output string pointer");
    auto result = owned_string(session(handle).snapshot());
    *out = result.release();
  });
}

int mxi_restore(void* handle, const char* saved) {
  return status([&] {
    const auto length = bounded_length(saved, mundo_x::InfiniteCore::max_snapshot_bytes);
    session(handle).restore(std::string(saved, length));
  });
}

int mxi_reset(void* handle, int value) {
  return status([&] { session(handle).reset(background(value)); });
}

int mxi_query(void* handle, const char* const* texts, std::size_t count,
              double* out, std::uint64_t* revision, std::uint64_t* epoch) {
  return status([&] {
    if (!revision || !epoch || (count && (!texts || !out)))
      throw std::invalid_argument("null query input or output");
    if (count > mundo_x::infinite_program_detail::max_observables)
      throw std::length_error("batch exceeds 4096 observables");
    auto& core = session(handle);
    std::vector<mundo_x::InfiniteSession::Observable> observables;
    observables.reserve(count);
    std::size_t bytes = 0;
    for (std::size_t i = 0; i < count; ++i) {
      const auto length = bounded_length(texts[i], mundo_x::infinite_program_detail::max_line_bytes);
      if (length > mundo_x::infinite_program_detail::max_program_bytes - bytes)
        throw std::length_error("query text exceeds 16 MiB");
      bytes += length;
      observables.push_back(mundo_x::parse_observable(std::string(texts[i], length),
                                                     core.limits().max_query_terms));
    }
    const auto result = core.query_batch(observables);
    if (!result.values.empty()) std::copy(result.values.begin(), result.values.end(), out);
    *revision = result.revision;
    *epoch = result.epoch;
  });
}

int mxi_get_stats(void* handle, mx_infinite_stats* out) {
  return status([&] {
    if (!out) throw std::invalid_argument("null statistics output");
    const auto result = session(handle).metadata();
    const auto& stats = result.stats;
    const mx_infinite_stats converted = {
      static_cast<std::uint64_t>(stats.active_qubits),
      static_cast<std::uint64_t>(stats.components),
      static_cast<std::uint64_t>(stats.largest_component),
      static_cast<std::uint64_t>(stats.tableau_bytes),
      static_cast<std::uint64_t>(stats.address_bytes),
      stats.revision, result.epoch,
      result.background == mundo_x::InfiniteCore::Background::Zero ? 0U : 1U
    };
    *out = converted;
  });
}

void mxi_free_string(char* text) { delete[] text; }
}  // extern "C"
