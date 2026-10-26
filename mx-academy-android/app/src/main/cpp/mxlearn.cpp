#include <jni.h>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include "mundo_x/mxlang.hpp"
#include "mundo_x/xasm.hpp"

// MX Visual calls the real Mundo X compiler and the real quantum runtime.
// Native readout reconstructs basis probabilities from Pauli-Z expectations,
// never from preset visual examples or from random sampled measurements.

namespace {
std::string quote_json(const std::string& value) {
    std::ostringstream out;
    out << '"';
    const char* hex = "0123456789abcdef";
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') { out << '\\' << char(c); }
        else if (c == '\n') out << "\\n";
        else if (c == '\r') out << "\\r";
        else if (c == '\t') out << "\\t";
        else if (c < 0x20) out << "\\u00" << hex[(c >> 4) & 15] << hex[c & 15];
        else out << char(c);
    }
    out << '"';
    return out.str();
}

mundo_x::MxCompiledProgram compile(const std::string& source) {
    if (source.size() > 16384) throw std::runtime_error("Código demasiado largo.");
    std::istringstream input(source);
    auto compiled = mundo_x::compile_mx(input);
    if (compiled.qubits < 1 || compiled.qubits > 3 ||
        compiled.instructions > 64 || compiled.t_gates > 16 ||
        compiled.xasm.size() > 8192)
        throw std::runtime_error("En el curso visual: máximo 3 qubits y 64 operaciones.");
    return compiled;
}

std::string inspect(const std::string& source) {
    const auto program = compile(source);
    const unsigned n = static_cast<unsigned>(program.qubits);
    const unsigned states = 1U << n;
    std::ostringstream script;
    script << program.xasm;
    for (unsigned mask = 1; mask < states; ++mask) {
        std::string p(n, 'I');
        for (unsigned q = 0; q < n; ++q)
            if ((mask >> q) & 1U) p[q] = 'Z';
        script << "expect " << p << "\n";
    }
    std::istringstream input(script.str());
    const auto result = mundo_x::run_xasm(input);
    if (!result.assertions_passed)
        throw std::runtime_error("Una validación del programa no pasó.");

    std::vector<double> z(states, 0.0);
    std::vector<bool> seen(states, false);
    z[0] = 1.0;
    seen[0] = true;
    std::istringstream lines(result.output);
    std::string line;
    while (std::getline(lines, line)) {
        if (line.rfind("EXPECT pauli=", 0) != 0) continue;
        const auto end = line.find(" value=");
        if (end == std::string::npos) continue;
        const std::string pauli = line.substr(13, end - 13);
        if (pauli.size() != n) throw std::runtime_error("Observable con longitud inválida.");
        unsigned mask = 0;
        for (unsigned q = 0; q < n; ++q) {
            if (pauli[q] == 'Z') mask |= (1U << q);
            else if (pauli[q] != 'I') throw std::runtime_error("Observable inesperado.");
        }
        const std::string value = line.substr(end + 7);
        std::size_t parsed = 0;
        double number = std::stod(value, &parsed);
        if (parsed != value.size() || !std::isfinite(number))
            throw std::runtime_error("Observable no numérico.");
        z[mask] = number;
        seen[mask] = true;
    }
    for (unsigned mask = 0; mask < states; ++mask)
        if (!seen[mask]) throw std::runtime_error("Falta una medición de diagnóstico.");

    std::vector<double> probs(states);
    double sum = 0.0;
    for (unsigned basis = 0; basis < states; ++basis) {
        double value = 0.0;
        for (unsigned mask = 0; mask < states; ++mask) {
            unsigned intersect = mask & basis;
            unsigned parity = 0;
            while (intersect) { parity ^= (intersect & 1U); intersect >>= 1; }
            value += parity ? -z[mask] : z[mask];
        }
        value /= states;
        if (value < -0.00001 || value > 1.00001 || !std::isfinite(value))
            throw std::runtime_error("Distribución cuántica inválida.");
        probs[basis] = std::max(0.0, std::min(1.0, value));
        sum += probs[basis];
    }
    if (sum < .9999 || sum > 1.0001)
        throw std::runtime_error("Las probabilidades no suman uno.");
    std::ostringstream json;
    json << std::setprecision(12) << "{\"ok\":true,\"qubits\":" << n
         << ",\"operations\":" << program.instructions
         << ",\"backend\":" << quote_json(program.backend)
         << ",\"probabilities\":[";
    for (unsigned basis = 0; basis < states; ++basis) {
        if (basis) json << ",";
        json << (probs[basis] / sum);
    }
    json << "]}";
    return json.str();
}

std::string grade(const std::string& source, int lesson) {
    if (lesson < 0 || lesson > 8) return "ERROR\nLección no reconocida.";
    const auto program = compile(source);
    const int qubits[9] = {1,1,1,2,1,3,3,2,1};
    if ((int)program.qubits != qubits[lesson])
        return "REINTENTAR\nNecesitas " + std::to_string(qubits[lesson]) + " qubit(s).";
    const bool reduced = lesson == 8;
    if (program.backend != (reduced ? "reduced" : "compact"))
        return "REINTENTAR\nEl motor seleccionado no corresponde a esta lección.";
    if (lesson == 4 &&
        source.find("h(q[0]);\nz(q[0]);\nh(q[0]);") == std::string::npos)
        return "REINTENTAR\nPrueba el orden H → Z → H. La fase se revela con interferencia.";
    if (lesson == 6 && source.find("repeat 3 as i") == std::string::npos)
        return "REINTENTAR\nUtiliza la tarjeta Repetir para aprender el bloque repeat.";
    if (lesson == 7 && source.find("circuit pareja(") == std::string::npos)
        return "REINTENTAR\nUsa la tarjeta Crear receta reutilizable.";
    if (lesson == 8 &&
        (program.t_gates == 0 || source.find("h(q[0]);\nt(q[0]);") == std::string::npos))
        return "REINTENTAR\nEn esta misión debes aplicar H y después T.";
    const char* tests[9] = {
        "assert expectation Z 1\n",
        "assert expectation Z -1\n",
        "assert expectation X 1\nassert expectation Z 0\n",
        "assert ghz\n",
        "assert expectation Z -1\n",
        "assert ghz\n",
        "assert expectation ZII -1\nassert expectation IZI -1\n"
        "assert expectation IIZ -1\n",
        "assert ghz\n",
        "assert fidelity\nassert norm\n"
    };
    std::istringstream circuit(program.xasm + "\n" + tests[lesson]);
    const auto result = mundo_x::run_xasm(circuit);
    if (!result.assertions_passed)
        return "REINTENTAR\nEl estado todavía no coincide. Prueba otra secuencia.";
    return "APROBADO\n¡Lo lograste! El núcleo Mundo X verificó el estado final.";
}
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_mundox_academy_MainActivity_inspectNative(JNIEnv* env, jobject, jstring source) {
    if (!source) return env->NewStringUTF("{\"ok\":false,\"error\":\"Código vacío.\"}");
    const char* raw = env->GetStringUTFChars(source, nullptr);
    if (!raw) return nullptr;
    std::string program(raw);
    env->ReleaseStringUTFChars(source, raw);
    try {
        const auto output = inspect(program);
        return env->NewStringUTF(output.c_str());
    } catch (const std::exception& e) {
        const auto output = std::string("{\"ok\":false,\"error\":") + quote_json(e.what()) + "}";
        return env->NewStringUTF(output.c_str());
    }
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_mundox_academy_MainActivity_evaluate(JNIEnv* env, jobject, jstring source, jint lesson) {
    if (!source) return env->NewStringUTF("ERROR\nNo hay programa.");
    const char* raw = env->GetStringUTFChars(source, nullptr);
    if (!raw) return nullptr;
    std::string program(raw);
    env->ReleaseStringUTFChars(source, raw);
    try {
        const auto output = grade(program, (int)lesson);
        return env->NewStringUTF(output.c_str());
    } catch (const std::exception& e) {
        const auto output = std::string("ERROR\n") + e.what();
        return env->NewStringUTF(output.c_str());
    }
}
