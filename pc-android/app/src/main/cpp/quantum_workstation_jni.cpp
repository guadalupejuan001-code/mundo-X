#include <jni.h>

#include "mundo_x/quantum_runtime.hpp"
#include "mundo_x/xasm.hpp"

#include <cstddef>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
using Runtime = mundo_x::QuantumRuntime;
std::mutex workstation_mutex;
std::map<jlong, std::unique_ptr<Runtime>> workstations;
jlong next_workstation_id = 1;

constexpr std::size_t kMaxProgram = 1024 * 1024;
constexpr std::size_t kMaxSnapshotPart = 24 * 1024 * 1024;
constexpr std::size_t kMaxConsole = 1024 * 1024;

// JNI strings are converted deliberately to plain ASCII. XI and both
// snapshot formats are ASCII: rejecting a non-ASCII character is safer than
// silently changing a quantum address or executing a truncated prefix.
std::string ascii(JNIEnv* env, jstring source, std::size_t limit) {
    if (!source) throw std::invalid_argument("missing text");
    const jsize length = env->GetStringLength(source);
    if (static_cast<std::size_t>(length) > limit)
        throw std::length_error("input exceeds workstation size limit");
    const jchar* chars = env->GetStringChars(source, nullptr);
    if (!chars) throw std::bad_alloc();
    std::string result;
    try {
        result.reserve(static_cast<std::size_t>(length));
        for (jsize i = 0; i < length; ++i) {
            const jchar ch = chars[i];
            if (ch == 0 || ch > 127)
                throw std::invalid_argument("XI and state snapshots require ASCII text");
            result.push_back(static_cast<char>(ch));
        }
    } catch (...) {
        env->ReleaseStringChars(source, chars);
        throw;
    }
    env->ReleaseStringChars(source, chars);
    return result;
}

Runtime& runtime(jlong id) {
    const auto found = workstations.find(id);
    if (found == workstations.end())
        throw std::runtime_error("quantum session is closed");
    return *found->second;
}

void raise(JNIEnv* env, const std::exception& error) {
    jclass cls = env->FindClass("java/lang/IllegalStateException");
    if (cls) env->ThrowNew(cls, error.what());
}

jstring java_text(JNIEnv* env, const std::string& text) {
    if (text.size() > kMaxConsole)
        throw std::length_error("console output exceeds 1 MiB");
    if (text.find('\0') != std::string::npos)
        throw std::runtime_error("console output contains a null byte");
    return env->NewStringUTF(text.c_str());
}
} // namespace

extern "C" {
JNIEXPORT jlong JNICALL
Java_com_cosmicx_pc_QuantumWorkstation_create(JNIEnv* env, jclass) {
    try {
        std::lock_guard<std::mutex> guard(workstation_mutex);
        if (next_workstation_id == std::numeric_limits<jlong>::max())
            throw std::overflow_error("workstation handle space exhausted");
        const jlong id = next_workstation_id++;
        workstations.emplace(id, std::make_unique<Runtime>());
        return id;
    } catch (const std::exception& error) {
        raise(env, error);
        return 0;
    }
}

JNIEXPORT void JNICALL
Java_com_cosmicx_pc_QuantumWorkstation_close(JNIEnv*, jclass, jlong id) {
    std::lock_guard<std::mutex> guard(workstation_mutex);
    workstations.erase(id);
}

JNIEXPORT jstring JNICALL
Java_com_cosmicx_pc_QuantumWorkstation_execute(JNIEnv* env, jclass, jlong id,
                                               jstring program) {
    try {
        const std::string source = ascii(env, program, kMaxProgram);
        std::lock_guard<std::mutex> guard(workstation_mutex);
        // Work on a separate candidate so even a rejected/oversized console
        // response cannot silently commit an operation the user never saw.
        Runtime candidate = runtime(id);
        std::string output = candidate.execute(source);
        jstring response = java_text(env, output);
        if (response && !env->ExceptionCheck())
            runtime(id) = std::move(candidate);
        return response;
    } catch (const std::exception& error) {
        raise(env, error);
        return nullptr;
    }
}

// XASM runs as an independent circuit. Its Reduced Exact T backend may use
// non-Clifford gates; it does not mutate the persistent XI workspace.
JNIEXPORT jstring JNICALL
Java_com_cosmicx_pc_QuantumWorkstation_executeXasm(JNIEnv* env, jclass,
                                                    jstring program) {
    try {
        std::string text = ascii(env, program, kMaxProgram);
        std::istringstream input(text);
        const auto result = mundo_x::run_xasm(input);
        std::ostringstream output;
        output << "XASM backend=" << result.backend
               << " qubits=" << result.qubits
               << " instructions=" << result.instructions
               << " T=" << result.t_gates
               << " components=" << result.components
               << " bytes=" << result.state_bytes << '\n'
               << "assertions=" << (result.assertions_passed ? "PASS" : "FAIL") << '\n'
               << result.output;
        return java_text(env, output.str());
    } catch (const std::exception& error) {
        raise(env, error);
        return nullptr;
    }
}

JNIEXPORT jstring JNICALL
Java_com_cosmicx_pc_QuantumWorkstation_status(JNIEnv* env, jclass, jlong id) {
    try {
        std::lock_guard<std::mutex> guard(workstation_mutex);
        return java_text(env, runtime(id).execute("stats\nmem_stats\n"));
    } catch (const std::exception& error) {
        raise(env, error);
        return nullptr;
    }
}

JNIEXPORT void JNICALL
Java_com_cosmicx_pc_QuantumWorkstation_reset(JNIEnv* env, jclass, jlong id) {
    try {
        std::lock_guard<std::mutex> guard(workstation_mutex);
        auto fresh = std::make_unique<Runtime>();
        runtime(id) = std::move(*fresh);
    } catch (const std::exception& error) {
        raise(env, error);
    }
}

JNIEXPORT jobjectArray JNICALL
Java_com_cosmicx_pc_QuantumWorkstation_snapshot(JNIEnv* env, jclass, jlong id) {
    try {
        std::lock_guard<std::mutex> guard(workstation_mutex);
        const Runtime& session = runtime(id);
        std::string core = session.core_snapshot();
        std::string memory = session.memory_snapshot();
        if (core.size() > kMaxSnapshotPart || memory.size() > kMaxSnapshotPart)
            throw std::length_error("quantum state exceeds workstation export limit");
        if (core.find('\0') != std::string::npos || memory.find('\0') != std::string::npos)
            throw std::runtime_error("unexpected binary snapshot");
        jclass string_class = env->FindClass("java/lang/String");
        if (!string_class) return nullptr;
        jobjectArray values = env->NewObjectArray(2, string_class, nullptr);
        if (!values) return nullptr;
        jstring core_string = env->NewStringUTF(core.c_str());
        if (!core_string) return nullptr;
        env->SetObjectArrayElement(values, 0, core_string);
        env->DeleteLocalRef(core_string);
        jstring memory_string = env->NewStringUTF(memory.c_str());
        if (!memory_string) return nullptr;
        env->SetObjectArrayElement(values, 1, memory_string);
        env->DeleteLocalRef(memory_string);
        return values;
    } catch (const std::exception& error) {
        raise(env, error);
        return nullptr;
    }
}

JNIEXPORT void JNICALL
Java_com_cosmicx_pc_QuantumWorkstation_restore(JNIEnv* env, jclass, jlong id,
                                               jstring core, jstring memory) {
    try {
        std::string saved_core = ascii(env, core, kMaxSnapshotPart);
        std::string saved_memory = ascii(env, memory, kMaxSnapshotPart);
        std::lock_guard<std::mutex> guard(workstation_mutex);
        runtime(id).restore(saved_core, saved_memory);
    } catch (const std::exception& error) {
        raise(env, error);
    }
}
}
