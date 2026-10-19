#include <jni.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <exception>
extern "C" {
const char* mx_last_error();
void* mx_create(int,std::size_t);
void mx_destroy(void*);
int mx_apply(void*,const std::uint64_t*,std::size_t);
int mx_query(void*,const char* const*,std::size_t,double*);
int mx_generator(void*,std::size_t,char*,std::size_t);
std::size_t mx_state_bytes(void*);
}
namespace {
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
    UtfChars(JNIEnv* e,jstring s):env(e),text(s),chars(e->GetStringUTFChars(s,nullptr)){}
    ~UtfChars(){if(chars)env->ReleaseStringUTFChars(text,chars);env->DeleteLocalRef(text);}
};
}
extern "C" JNIEXPORT jlong JNICALL
Java_com_mundox_graphics_NativeCore_nCreate(JNIEnv* e,jclass,jint n){
    if(n<1){fail(e,"El registro necesita qubits");return 0;}
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
        if(size%3){fail(e,"Lote de puertas inválido");return;}
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
