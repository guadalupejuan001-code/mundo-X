#include <jni.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <exception>
#include <limits>
#include <new>
#include "mundo_x/graphics_core_bridge.h"
#include "mundo_x/infinite_bridge.h"
#include "mundo_x/infinite_bridge.hpp"
#include "mundo_x/infinite_session.hpp"
#include "mundo_x/ether_core.hpp"
#include "mundo_x/infinite_program.hpp"
namespace {
constexpr std::uint64_t max_project_revision=9007199254740991ULL;
constexpr std::size_t max_project_gates=100000;
void fail(JNIEnv* e,const char* message){
    if(e->ExceptionCheck())return;
    jclass type=e->FindClass("java/lang/IllegalStateException");
    if(type){e->ThrowNew(type,message);e->DeleteLocalRef(type);}
}
void* handle(jlong p){return reinterpret_cast<void*>(static_cast<std::uintptr_t>(p));}
struct UtfChars {
    JNIEnv* env;
    jstring text;
    const char* chars;
    bool remove_local;
    UtfChars(JNIEnv* e,jstring s,bool remove=true):env(e),text(s),chars(e->GetStringUTFChars(s,nullptr)),remove_local(remove){}
    ~UtfChars(){if(chars)env->ReleaseStringUTFChars(text,chars);if(remove_local)env->DeleteLocalRef(text);}
};
}
extern "C" JNIEXPORT jlong JNICALL
Java_com_mundox_graphics_NativeCore_nCreate(JNIEnv* e,jclass,jint n){
    if(n<1||n>1000){fail(e,"El proyecto admite de 1 a 1000 qubits");return 0;}
    void* p=mx_create(0,static_cast<std::size_t>(n));
    if(!p)fail(e,mx_last_error());
    return static_cast<jlong>(reinterpret_cast<std::uintptr_t>(p));
}
extern "C" JNIEXPORT void JNICALL
Java_com_mundox_graphics_NativeCore_nDestroy(JNIEnv*,jclass,jlong p){mx_destroy(handle(p));}
extern "C" JNIEXPORT void JNICALL
Java_com_mundox_graphics_NativeCore_nApply(JNIEnv* e,jclass,jlong p,jlongArray data){
    try{
        if(!p||!data){fail(e,"Núcleo cerrado o lote nulo");return;}
        const auto size=e->GetArrayLength(data);
        if(size%3||size>static_cast<jsize>(max_project_gates*3)){fail(e,"Lote de puertas inválido");return;}
        if(size){
            const auto revision=mx_revision(handle(p));
            if(mx_last_error()[0]){fail(e,mx_last_error());return;}
            const auto history=mx_history_count(handle(p));
            if(mx_last_error()[0]){fail(e,mx_last_error());return;}
            if(revision>=max_project_revision||history>max_project_gates-static_cast<std::size_t>(size/3)){
                fail(e,"La operación excede los límites del proyecto");return;
            }
        }
        std::vector<jlong> rows(size);if(size)e->GetLongArrayRegion(data,0,size,rows.data());
        if(e->ExceptionCheck())return;
        std::vector<std::uint64_t> gates(rows.begin(),rows.end());
        if(mx_apply(handle(p),gates.data(),gates.size()/3))fail(e,mx_last_error());
    }catch(const std::exception& x){fail(e,x.what());}
}
extern "C" JNIEXPORT jfloatArray JNICALL
Java_com_mundox_graphics_NativeCore_nQuery(JNIEnv* e,jclass,jlong p,jobjectArray strings){
    try{
        if(!p||!strings){fail(e,"Núcleo cerrado o consultas nulas");return nullptr;}
        const auto count=e->GetArrayLength(strings);
        std::vector<std::string> texts;
        texts.reserve(count);
        for(jsize i=0;i<count;i++){
            auto s=static_cast<jstring>(e->GetObjectArrayElement(strings,i));
            if(!s){fail(e,"Operador nulo");return nullptr;}
            UtfChars utf(e,s);
            if(!utf.chars)return nullptr;
            texts.emplace_back(utf.chars);
        }
        std::vector<const char*> pointers;for(const auto& s:texts)pointers.push_back(s.c_str());
        std::vector<double> values(count);
        if(mx_query(handle(p),pointers.data(),count,values.data())){fail(e,mx_last_error());return nullptr;}
        std::vector<jfloat> result(values.begin(),values.end());
        auto out=e->NewFloatArray(count);
        if(out&&count)e->SetFloatArrayRegion(out,0,count,result.data());
        return out;
    }catch(const std::exception& x){fail(e,x.what());return nullptr;}
}
extern "C" JNIEXPORT jstring JNICALL
Java_com_mundox_graphics_NativeCore_nGenerator(JNIEnv* e,jclass,jlong p,jint row,jint n){
    try{
        if(!p||row<0||row>=n){fail(e,"Índice de generador inválido");return nullptr;}
        std::vector<char> buffer(static_cast<std::size_t>(n)+2);
        if(mx_generator(handle(p),row,buffer.data(),buffer.size())){fail(e,mx_last_error());return nullptr;}
        return e->NewStringUTF(buffer.data());
    }catch(const std::exception& x){fail(e,x.what());return nullptr;}
}
extern "C" JNIEXPORT jlong JNICALL
Java_com_mundox_graphics_NativeCore_nStateBytes(JNIEnv* e,jclass,jlong p){
    if(!p){fail(e,"Núcleo cerrado");return 0;}
    return static_cast<jlong>(mx_state_bytes(handle(p)));
}
extern "C" JNIEXPORT jlong JNICALL
Java_com_mundox_graphics_NativeCore_nVersion(JNIEnv* e,jclass,jlong p){
    if(!p){fail(e,"Núcleo cerrado");return 0;}
    const auto version=mx_revision(handle(p));
    if(mx_last_error()[0]){fail(e,mx_last_error());return 0;}
    if(version>max_project_revision){fail(e,"Revisión fuera del formato de proyecto");return 0;}
    return static_cast<jlong>(version);
}
extern "C" JNIEXPORT jlong JNICALL
Java_com_mundox_graphics_NativeCore_nEpoch(JNIEnv* e,jclass,jlong p){
    if(!p){fail(e,"Núcleo cerrado");return 0;}
    const auto epoch=mx_epoch(handle(p));
    if(mx_last_error()[0]){fail(e,mx_last_error());return 0;}
    if(epoch>static_cast<std::uint64_t>(std::numeric_limits<jlong>::max())){fail(e,"Época de observación fuera de rango");return 0;}
    return static_cast<jlong>(epoch);
}
extern "C" JNIEXPORT jlong JNICALL
Java_com_mundox_graphics_NativeCore_nHistoryCount(JNIEnv* e,jclass,jlong p){
    if(!p){fail(e,"Núcleo cerrado");return 0;}
    return static_cast<jlong>(mx_history_count(handle(p)));
}
extern "C" JNIEXPORT jlongArray JNICALL
Java_com_mundox_graphics_NativeCore_nSnapshot(JNIEnv* e,jclass,jlong p){
    try{
        if(!p){fail(e,"Núcleo cerrado");return nullptr;}
        std::size_t capacity=mx_history_count(handle(p));
        for(int attempt=0;attempt<8;attempt++){
            if(capacity>max_project_gates){fail(e,"Historial fuera del formato de proyecto");return nullptr;}
            std::vector<std::uint64_t> triples(capacity*3);
            std::size_t actual=0;std::uint64_t revision=0;
            int result=mx_snapshot(handle(p),triples.data(),capacity,&actual,&revision);
            if(actual>capacity){capacity=actual;continue;}
            if(result){fail(e,mx_last_error());return nullptr;}
            if(revision>max_project_revision){fail(e,"Revisión fuera del formato de proyecto");return nullptr;}
            std::vector<jlong> values(actual*3+1);values[0]=static_cast<jlong>(revision);
            for(std::size_t j=0;j<actual*3;j++)values[j+1]=static_cast<jlong>(triples[j]);
            auto out=e->NewLongArray(static_cast<jsize>(values.size()));
            if(out)e->SetLongArrayRegion(out,0,static_cast<jsize>(values.size()),values.data());
            return out;
        }
        fail(e,"El núcleo cambió durante el guardado");return nullptr;
    }catch(const std::exception& x){fail(e,x.what());return nullptr;}
}
extern "C" JNIEXPORT void JNICALL
Java_com_mundox_graphics_NativeCore_nRestore(JNIEnv* e,jclass,jlong p,jlongArray data,jlong revision){
    try{
        if(!p||!data||revision<0||static_cast<std::uint64_t>(revision)>max_project_revision){fail(e,"Snapshot del núcleo inválido");return;}
        const auto size=e->GetArrayLength(data);
        if(size%3||size>static_cast<jsize>(max_project_gates*3)){fail(e,"Historial del núcleo inválido");return;}
        std::vector<jlong> values(size);if(size)e->GetLongArrayRegion(data,0,size,values.data());
        if(e->ExceptionCheck())return;
        std::vector<std::uint64_t> triples(values.begin(),values.end());
        if(mx_restore(handle(p),triples.data(),triples.size()/3,static_cast<std::uint64_t>(revision)))
            fail(e,mx_last_error());
    }catch(const std::exception& x){fail(e,x.what());}
}

namespace {
struct InfiniteText {
    char* value=nullptr;
    ~InfiniteText(){mxi_free_string(value);}
};
// Infinite commands and snapshots are ASCII. Validate Java UTF-16 before JNI's
// modified UTF-8 conversion so embedded NULs cannot change a native request.
bool infinite_ascii(JNIEnv* e,jstring text){
    if(!text){fail(e,"Falta el texto infinito");return false;}
    const auto size=e->GetStringLength(text);
    if(size>16*1024*1024){fail(e,"El texto infinito supera 16 MiB");return false;}
    const jchar* chars=e->GetStringChars(text,nullptr);if(!chars)return false;
    bool valid=true;
    for(jsize i=0;i<size;i++)if(chars[i]==0||chars[i]>127){valid=false;break;}
    e->ReleaseStringChars(text,chars);
    if(!valid)fail(e,"El texto infinito requiere ASCII sin caracteres nulos");
    return valid;
}
}
extern "C" JNIEXPORT jlong JNICALL
Java_com_mundox_graphics_NativeCore_nInfiniteCreate(JNIEnv* e,jclass,jint background){
    void* p=mxi_create(background);if(!p)fail(e,mxi_last_error());
    return static_cast<jlong>(reinterpret_cast<std::uintptr_t>(p));
}
extern "C" JNIEXPORT void JNICALL
Java_com_mundox_graphics_NativeCore_nInfiniteDestroy(JNIEnv*,jclass,jlong p){mxi_destroy(handle(p));}
extern "C" JNIEXPORT jstring JNICALL
Java_com_mundox_graphics_NativeCore_nInfiniteExecute(JNIEnv* e,jclass,jlong p,jstring script){
    try{
        if(!infinite_ascii(e,script))return nullptr;
        UtfChars utf(e,script,false);if(!utf.chars)return nullptr;
        auto session=mundo_x::infinite_session_from_handle(handle(p));
        // Prepare the Java response before commit as well. Java allocation
        // failure must not consume a measurement or advance the shared RNG.
        return session->execute_prepared(std::string(utf.chars),[&](const std::string& output){
            jstring result=e->NewStringUTF(output.c_str());
            if(!result||e->ExceptionCheck())throw std::bad_alloc();
            return result;
        });
    }catch(const std::exception& x){fail(e,x.what());return nullptr;}
}
extern "C" JNIEXPORT jstring JNICALL
Java_com_mundox_graphics_NativeCore_nInfiniteSnapshot(JNIEnv* e,jclass,jlong p){
    InfiniteText out;
    if(mxi_snapshot(handle(p),&out.value)){fail(e,mxi_last_error());return nullptr;}
    return e->NewStringUTF(out.value?out.value:"");
}
extern "C" JNIEXPORT void JNICALL
Java_com_mundox_graphics_NativeCore_nInfiniteRestore(JNIEnv* e,jclass,jlong p,jstring snapshot){
    if(!infinite_ascii(e,snapshot))return;
    UtfChars utf(e,snapshot,false);if(!utf.chars)return;
    if(mxi_restore(handle(p),utf.chars))fail(e,mxi_last_error());
}
extern "C" JNIEXPORT void JNICALL
Java_com_mundox_graphics_NativeCore_nInfiniteReset(JNIEnv* e,jclass,jlong p,jint background){
    if(mxi_reset(handle(p),background))fail(e,mxi_last_error());
}
extern "C" JNIEXPORT jfloatArray JNICALL
Java_com_mundox_graphics_NativeCore_nInfiniteQuery(JNIEnv* e,jclass,jlong p,jobjectArray strings,jlongArray stamp){
    try{
        if(!p||!strings||!stamp||e->GetArrayLength(stamp)!=2){fail(e,"Consulta infinita inválida");return nullptr;}
        const auto count=e->GetArrayLength(strings);
        if(count>4096){fail(e,"La consulta admite hasta 4096 observables");return nullptr;}
        std::vector<std::string> texts;texts.reserve(count);std::size_t total=0;
        for(jsize i=0;i<count;i++){
            auto s=static_cast<jstring>(e->GetObjectArrayElement(strings,i));
            if(!infinite_ascii(e,s)){if(s)e->DeleteLocalRef(s);return nullptr;}
            UtfChars utf(e,s);if(!utf.chars)return nullptr;
            texts.emplace_back(utf.chars);total+=texts.back().size();
            if(total>16*1024*1024){fail(e,"El programa visual supera 16 MiB");return nullptr;}
        }
        std::vector<const char*> pointers;for(const auto& s:texts)pointers.push_back(s.c_str());
        std::vector<double> values(count);std::uint64_t revision=0,epoch=0;
        if(mxi_query(handle(p),pointers.data(),count,values.data(),&revision,&epoch)){
            fail(e,mxi_last_error());return nullptr;
        }
        std::vector<jfloat> result(values.begin(),values.end());
        auto out=e->NewFloatArray(count);if(!out)return nullptr;
        if(count)e->SetFloatArrayRegion(out,0,count,result.data());
        if(e->ExceptionCheck())return nullptr;
        const jlong metadata[]={static_cast<jlong>(revision),static_cast<jlong>(epoch)};
        e->SetLongArrayRegion(stamp,0,2,metadata);
        return out;
    }catch(const std::exception& x){fail(e,x.what());return nullptr;}
}
extern "C" JNIEXPORT jlongArray JNICALL
Java_com_mundox_graphics_NativeCore_nInfiniteStats(JNIEnv* e,jclass,jlong p){
    mx_infinite_stats s{};
    if(mxi_get_stats(handle(p),&s)){fail(e,mxi_last_error());return nullptr;}
    const jlong values[]={static_cast<jlong>(s.active_qubits),static_cast<jlong>(s.components),
        static_cast<jlong>(s.largest_component),static_cast<jlong>(s.tableau_bytes),
        static_cast<jlong>(s.address_bytes),static_cast<jlong>(s.revision),static_cast<jlong>(s.epoch),
        static_cast<jlong>(s.background)};
    auto out=e->NewLongArray(8);if(out)e->SetLongArrayRegion(out,0,8,values);return out;
}

namespace {
mundo_x::EtherCore* ether_handle(jlong p){
    return reinterpret_cast<mundo_x::EtherCore*>(static_cast<std::uintptr_t>(p));
}
}
extern "C" JNIEXPORT jlong JNICALL
Java_com_mundox_graphics_EtherCoreNative_nCreate(JNIEnv* e,jclass){
    try{
        auto* core=new mundo_x::EtherCore();
        return static_cast<jlong>(reinterpret_cast<std::uintptr_t>(core));
    }catch(const std::exception& x){fail(e,x.what());return 0;}
}
extern "C" JNIEXPORT void JNICALL
Java_com_mundox_graphics_EtherCoreNative_nDestroy(JNIEnv*,jclass,jlong p){
    delete ether_handle(p);
}
extern "C" JNIEXPORT void JNICALL
Java_com_mundox_graphics_EtherCoreNative_nHAll(JNIEnv* e,jclass,jlong p){
    try{if(!p){fail(e,"EtherCore cerrado");return;}ether_handle(p)->h_all();}
    catch(const std::exception& x){fail(e,x.what());}
}
extern "C" JNIEXPORT void JNICALL
Java_com_mundox_graphics_EtherCoreNative_nCzNeighbors(JNIEnv* e,jclass,jlong p){
    try{if(!p){fail(e,"EtherCore cerrado");return;}ether_handle(p)->cz_neighbors();}
    catch(const std::exception& x){fail(e,x.what());}
}
extern "C" JNIEXPORT jfloatArray JNICALL
Java_com_mundox_graphics_EtherCoreNative_nQuery(JNIEnv* e,jclass,jlong p,jobjectArray strings){
    try{
        if(!p||!strings){fail(e,"EtherCore cerrado o consultas nulas");return nullptr;}
        const auto count=e->GetArrayLength(strings);
        std::vector<jfloat> values(static_cast<std::size_t>(count));
        for(jsize i=0;i<count;++i){
            auto raw=static_cast<jstring>(e->GetObjectArrayElement(strings,i));
            if(!raw){fail(e,"Observable EtherCore nulo");return nullptr;}
            UtfChars utf(e,raw);
            if(!utf.chars)return nullptr;
            const auto observable=mundo_x::parse_observable(utf.chars);
            std::vector<mundo_x::EtherCore::Term> terms;
            terms.reserve(observable.terms.size());
            for(const auto& term:observable.terms)terms.push_back({term.address,term.axis});
            values[static_cast<std::size_t>(i)]=static_cast<jfloat>(
                ether_handle(p)->expectation(terms,observable.sign));
        }
        auto out=e->NewFloatArray(count);
        if(out&&count)e->SetFloatArrayRegion(out,0,count,values.data());
        return out;
    }catch(const std::exception& x){fail(e,x.what());return nullptr;}
}
extern "C" JNIEXPORT jlongArray JNICALL
Java_com_mundox_graphics_EtherCoreNative_nStats(JNIEnv* e,jclass,jlong p){
    try{
        if(!p){fail(e,"EtherCore cerrado");return nullptr;}
        const auto st=ether_handle(p)->stats();
        const jlong data[4]={
            static_cast<jlong>(st.rules),
            static_cast<jlong>(st.last_query_sites),
            static_cast<jlong>(st.last_query_peak_sites),
            static_cast<jlong>(st.last_query_address_bytes)
        };
        auto out=e->NewLongArray(4);
        if(out)e->SetLongArrayRegion(out,0,4,data);
        return out;
    }catch(const std::exception& x){fail(e,x.what());return nullptr;}
}
