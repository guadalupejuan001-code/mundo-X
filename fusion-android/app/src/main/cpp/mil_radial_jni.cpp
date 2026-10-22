#include <jni.h>

#include "mundo_x/mil_radial_evolution.hpp"

#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void throw_java(JNIEnv* env,const char* type,const std::string& what) {
    jclass error=env->FindClass(type);
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
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_mundox_fusion3d_MilRadialEvolver_evolveNative(
        JNIEnv* env,jclass,jdoubleArray diagonalArray,jdouble offDiagonal,
        jint initialSite,jdouble tauInverseMev,jint trotterSteps) {
    try {
        if(!diagonalArray)
            throw std::invalid_argument("Missing Hamiltonian diagonal");
        const jsize n=env->GetArrayLength(diagonalArray);
        if(n!=4&&n!=8)
            throw std::invalid_argument("MIL radial requires 4 or 8 grid points");
        if(initialSite<0||trotterSteps<1)
            throw std::invalid_argument("Invalid MIL evolution parameters");
        std::vector<double> diagonal(static_cast<std::size_t>(n));
        env->GetDoubleArrayRegion(diagonalArray,0,n,diagonal.data());
        if(env->ExceptionCheck())return nullptr;

        // ReducedExactTRepresentation executes all evolution gates.
        // The independent dense RK4 is invoked only for validation.
        const auto result=mundo_x::MilRadialEvolution::execute(
                diagonal,offDiagonal,
                static_cast<std::size_t>(initialSite),
                tauInverseMev,
                static_cast<std::size_t>(trotterSteps));

        std::ostringstream json;
        json<<std::setprecision(16)<<'{'
            <<"\"backend\":\"MIL ReducedExactTRepresentation\","
            <<"\"method\":\"Strang-2 Pauli rotations\","
            <<"\"oracle\":\"independent sparse-matrix RK4\",";
        field(json,"qubits",result.qubits);
        field(json,"sites",result.sites);
        field(json,"initial_site",result.initial_site);
        field(json,"steps",result.trotter_steps);
        field(json,"pauli_terms",result.pauli_terms);
        field(json,"gates",result.mil_gates);
        field(json,"peak_components",result.peak_components);
        field(json,"tau_inverse_mev",result.tau_inverse_mev);
        field(json,"physical_seconds",result.time_seconds);
        field(json,"norm",result.norm);
        field(json,"reference_norm",result.reference_norm);
        field(json,"fidelity",result.fidelity);
        field(json,"probability_l1",result.probability_l1);
        field(json,"initial_probability",result.initial_probability);
        array(json,"probabilities",result.probabilities,true);
        array(json,"reference_probabilities",result.reference_probabilities,false);
        json<<'}';
        return env->NewStringUTF(json.str().c_str());
    } catch(const std::exception& ex) {
        throw_java(env,"java/lang/IllegalStateException",ex.what());
        return nullptr;
    }
}
