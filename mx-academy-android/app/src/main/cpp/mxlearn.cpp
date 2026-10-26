#include <jni.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include "mundo_x/mxlang.hpp"
#include "mundo_x/xasm.hpp"

// Real compiler and evaluator owned by Mundo X. No fake interpreter.
static std::string grade(const std::string& source, int lesson) {
    if (lesson < 0 || lesson > 7) return "ERROR\nLección inválida.";
    if (source.size() > 16384) return "ERROR\nEl código supera 16 KiB.";
    std::istringstream code(source);
    auto program = mundo_x::compile_mx(code);
    if (program.qubits > 6 || program.instructions > 256 ||
        program.t_gates > 32 || program.xasm.size() > 32768)
        return "ERROR\nLímite: 6 qubits, 256 operaciones, 32 puertas T.";
    const int widths[] = {1, 1, 1, 2, 3, 2, 1, 3};
    if ((int) program.qubits != widths[lesson])
        return "REINTENTAR\nEste ejercicio requiere " + std::to_string(widths[lesson]) + " qubit(s).";
    if (program.backend != (lesson == 6 ? "reduced" : "compact"))
        return std::string("REINTENTAR\nSelecciona motor ") + (lesson == 6 ? "reduced." : "compact.");
    if (lesson == 6 && !program.t_gates)
        return "REINTENTAR\nTodavía falta una puerta T.";
    static const char* tests[] = {
        "assert expectation Z 1\n",
        "assert expectation X 1\nassert expectation Z 0\n",
        "assert expectation Z -1\n",
        "assert ghz\n",
        "assert expectation ZII -1\nassert expectation IZI -1\nassert expectation IIZ -1\n",
        "assert ghz\n",
        "assert fidelity\nassert norm\n",
        "assert ghz\n"
    };
    // Appended after compilation, so deleting assertions cannot bypass grading.
    std::istringstream circuit(program.xasm + "\n" + tests[lesson]);
    const auto result = mundo_x::run_xasm(circuit);
    std::ostringstream out;
    if (result.assertions_passed)
        out << "APROBADO\nEstado verificado. Desafío superado.";
    else
        out << "REINTENTAR\nEl archivo compila, pero el estado final no cumple el objetivo.";
    out << "\n\nMotor: " << program.backend
        << "\nQubits: " << program.qubits
        << "\nOperaciones: " << program.instructions
        << "\nPuertas T: " << program.t_gates;
    if (!result.output.empty())
        out << "\n\nNúcleo Mundo X:\n" << result.output.substr(0, 7000);
    return out.str();
}
extern "C" JNIEXPORT jstring JNICALL
Java_com_mundox_academy_MainActivity_evaluate(JNIEnv* env, jobject, jstring input, jint lesson) {
    if (!input) return env->NewStringUTF("ERROR\nCódigo vacío.");
    const char* raw = env->GetStringUTFChars(input, nullptr);
    if (!raw) return nullptr;
    std::string source(raw);
    env->ReleaseStringUTFChars(input, raw);
    try {
        auto result = grade(source, (int)lesson);
        return env->NewStringUTF(result.c_str());
    } catch (const std::exception& e) {
        auto message = std::string("ERROR\n") + e.what() +
                "\nComprueba la sintaxis, los tipos y los índices.";
        return env->NewStringUTF(message.c_str());
    }
}
