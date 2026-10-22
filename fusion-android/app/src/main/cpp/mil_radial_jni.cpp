#include <jni.h>

#include "mundo_x/mil_radial_evolution.hpp"
#include "mil_radial_scaling.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void throw_java(JNIEnv* env,const std::string& what) {
    jclass error=env->FindClass("java/lang/IllegalStateException");
    if(error)env->ThrowNew(error,what.c_str());
}
void field(std::ostringstream& stream,const char* key,double value) {
    if(!std::isfinite(value))
        throw std::runtime_error(std::string("non-finite MIL result: ")+key);
    stream<<'"'<<key<<"\":"<<value<<',';
}
void field(std::ostringstream& stream,const char* key,std::size_t value) {
    stream<<'"'<<key<<"\":"<<value<<',';
}
void array(std::ostringstream& stream,const char* name,
           const std::vector<double>& values,bool comma) {
    stream<<'"'<<name<<"\":[";
    for(std::size_t i=0;i<values.size();i++){
        if(i)stream<<',';
        if(!std::isfinite(values[i]))
            throw std::runtime_error("non-finite MIL probability");
        stream<<values[i];
    }
    stream<<']';
    if(comma)stream<<',';
}
struct Output {
    std::size_t qubits=0,sites=0,initial_site=0,steps=0,pauli_terms=0;
    std::size_t gates=0,peak_components=0;
    double tau=0.,physical_seconds=0.,norm=0.,reference_norm=0.;
    double fidelity=0.,l1=0.,initial_probability=0.,execution_ms=0.;
    double pauli_preparation_ms=0.,reference_ms=0.;
    bool optimized_decomposition=false;
    std::vector<double> probabilities,reference;
};

} // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_mundox_fusion3d_MilRadialEvolver_evolveNative(
        JNIEnv* env,jclass,jdoubleArray diagonalArray,jdouble offDiagonal,
        jint initialSite,jdouble tauInverseMev,jint trotterSteps) {
    try {
        if(!diagonalArray)
            throw std::invalid_argument("Missing Hamiltonian diagonal");
        const jsize n=env->GetArrayLength(diagonalArray);
        if(n<4||n>256||(n&(n-1))!=0)
            throw std::invalid_argument("MIL radial requires 4..256 grid points (power of two)");
        if(initialSite<0||initialSite>=n||trotterSteps<1)
            throw std::invalid_argument("Invalid MIL evolution parameters");
        if(!std::isfinite(tauInverseMev)||tauInverseMev<0||tauInverseMev>.12)
            throw std::invalid_argument("Unbounded MIL radial time");

        std::vector<double> diagonal(static_cast<std::size_t>(n));
        env->GetDoubleArrayRegion(diagonalArray,0,n,diagonal.data());
        if(env->ExceptionCheck())return nullptr;

        Output state;
        if(n<=8) {
            // Preserve the original 2/3-qubit implementation exactly, so
            // existing 1.2 results remain comparable and reproducible.
            const auto r=mundo_x::MilRadialEvolution::execute(
                diagonal,offDiagonal,
                static_cast<std::size_t>(initialSite),
                tauInverseMev,static_cast<std::size_t>(trotterSteps));
            state.qubits=r.qubits;state.sites=r.sites;
            state.initial_site=r.initial_site;
            state.steps=r.trotter_steps;state.pauli_terms=r.pauli_terms;
            state.gates=r.mil_gates;state.peak_components=r.peak_components;
            state.tau=r.tau_inverse_mev;
            state.physical_seconds=r.time_seconds;
            state.norm=r.norm;state.reference_norm=r.reference_norm;
            state.fidelity=r.fidelity;state.l1=r.probability_l1;
            state.initial_probability=r.initial_probability;
            state.probabilities=r.probabilities;
            state.reference=r.reference_probabilities;
        } else {
            // This path executes actual native MIL Reduced gates through a
            // symbolic Pauli decomposition of the uniform tridiagonal H.
            // RK4 integrates the same sparse H INDEPENDENTLY; it cannot
            // substitute the MIL state or hide resource exhaustion.
            const auto r=mundo_x::MilRadialScaling::evolve(
                diagonal,offDiagonal,
                static_cast<std::size_t>(initialSite),
                tauInverseMev,static_cast<std::size_t>(trotterSteps));
            state.qubits=r.profile.qubits;state.sites=r.profile.sites;
            state.initial_site=r.initial_site;state.steps=r.steps;
            state.pauli_terms=r.profile.total_terms;
            state.gates=r.gates;state.peak_components=r.peak_components;
            state.tau=r.tau_inverse_mev;
            state.physical_seconds=r.tau_inverse_mev*
                mundo_x::MilRadialEvolution::hbar_mev_s;
            state.norm=r.norm;state.reference_norm=r.reference_norm;
            state.fidelity=r.fidelity;state.l1=r.probability_l1;
            state.initial_probability=r.mil.at(state.initial_site);
            state.probabilities=r.mil;state.reference=r.reference;
            state.execution_ms=r.time_ms;
            state.pauli_preparation_ms=r.decomposition_ms;
            state.reference_ms=r.reference_ms;
            state.optimized_decomposition=true;
        }

        std::ostringstream json;
        json<<std::setprecision(16)<<'{'
            <<"\"backend\":\"MIL ReducedExactTRepresentation\","
            <<"\"method\":\"Strang-2 Pauli rotations\","
            <<"\"oracle\":\"independent sparse-matrix RK4\","
            <<"\"pauli_decomposition\":\""
            <<(state.optimized_decomposition?
                "structured-Walsh-binary-carry":"exhaustive-small-grid")
            <<"\",";
        field(json,"qubits",state.qubits);
        field(json,"sites",state.sites);
        field(json,"initial_site",state.initial_site);
        field(json,"steps",state.steps);
        field(json,"pauli_terms",state.pauli_terms);
        field(json,"gates",state.gates);
        field(json,"peak_components",state.peak_components);
        field(json,"tau_inverse_mev",state.tau);
        field(json,"physical_seconds",state.physical_seconds);
        field(json,"execution_ms",state.execution_ms);
        field(json,"pauli_preparation_ms",state.pauli_preparation_ms);
        field(json,"reference_ms",state.reference_ms);
        field(json,"norm",state.norm);
        field(json,"reference_norm",state.reference_norm);
        field(json,"fidelity",state.fidelity);
        field(json,"probability_l1",state.l1);
        field(json,"initial_probability",state.initial_probability);
        array(json,"probabilities",state.probabilities,true);
        array(json,"reference_probabilities",state.reference,false);
        json<<'}';
        return env->NewStringUTF(json.str().c_str());
    } catch(const std::exception& ex) {
        throw_java(env,ex.what());
        return nullptr;
    }
}
