#include <jni.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cerrno>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include "pc_machine.hpp"

namespace {
std::mutex lock_;
std::map<jlong,std::unique_ptr<cosmic_pc::PcMachine>> machines;
jlong next_id=1;
struct File {int fd;explicit File(int f):fd(f){}~File(){if(fd>=0)close(fd);}};
cosmic_pc::PcMachine& machine(jlong h){auto it=machines.find(h);if(it==machines.end())throw std::runtime_error("Identificador de máquina no válido");return *it->second;}
jstring java(JNIEnv* e,const std::string& s){return e->NewStringUTF(s.c_str());}
void exception(JNIEnv* e,const std::exception& ex){e->ThrowNew(e->FindClass("java/lang/IllegalStateException"),ex.what());}
template<class F> jstring action(JNIEnv* e,jlong h,F fn){std::lock_guard<std::mutex> guard(lock_);try{auto& m=machine(h);try{fn(m);}catch(const std::exception& ex){m.host_error=ex.what();}return java(e,m.json());}catch(const std::exception& ex){exception(e,ex);return nullptr;}}
jlong create_machine(JNIEnv* e,jint mb,jint resident_mb){
  std::lock_guard<std::mutex> guard(lock_);
  try{
    if(mb<2||mb>2048)throw std::runtime_error("RAM virtual permitida: 2..2048 MiB");
    if(resident_mb<1||resident_mb>1024||resident_mb>mb)throw std::runtime_error("Límite de RAM ocupada: 1..1024 MiB, sin exceder la RAM virtual");
    const jlong h=next_id++;
    machines[h]=std::make_unique<cosmic_pc::PcMachine>(std::size_t(mb)*1024*1024,std::size_t(resident_mb)*1024*1024);
    return h;
  }catch(const std::exception& ex){exception(e,ex);return 0;}
}
}
extern "C" {
JNIEXPORT jlong JNICALL Java_com_cosmicx_pc_NativeMachine_create(JNIEnv* e,jclass,jint mb){return create_machine(e,mb,std::min(mb,1024));}
JNIEXPORT jlong JNICALL Java_com_cosmicx_pc_NativeMachine_createWithBudget(JNIEnv* e,jclass,jint mb,jint resident_mb){return create_machine(e,mb,resident_mb);}
JNIEXPORT void JNICALL Java_com_cosmicx_pc_NativeMachine_destroy(JNIEnv*,jclass,jlong h){std::lock_guard<std::mutex> guard(lock_);machines.erase(h);}
JNIEXPORT jstring JNICALL Java_com_cosmicx_pc_NativeMachine_bootDemo(JNIEnv* e,jclass,jlong h){return action(e,h,[](auto& m){m.boot_demo();});}
JNIEXPORT jstring JNICALL Java_com_cosmicx_pc_NativeMachine_bootLongModeDemo(JNIEnv* e,jclass,jlong h){return action(e,h,[](auto& m){m.boot_long_mode_demo();});}
JNIEXPORT jstring JNICALL Java_com_cosmicx_pc_NativeMachine_reset(JNIEnv* e,jclass,jlong h){return action(e,h,[](auto& m){m.reset();});}
JNIEXPORT jstring JNICALL Java_com_cosmicx_pc_NativeMachine_inspect(JNIEnv* e,jclass,jlong h){return action(e,h,[](auto&){});}
JNIEXPORT jstring JNICALL Java_com_cosmicx_pc_NativeMachine_run(JNIEnv* e,jclass,jlong h,jint budget){return action(e,h,[&](auto& m){if(budget<1||budget>50000)throw std::runtime_error("Presupuesto de ejecución inválido");m.run(budget);});}
JNIEXPORT jstring JNICALL Java_com_cosmicx_pc_NativeMachine_screen(JNIEnv* e,jclass,jlong h){std::lock_guard<std::mutex> guard(lock_);try{return java(e,machine(h).screen());}catch(const std::exception& ex){exception(e,ex);return nullptr;}}
JNIEXPORT void JNICALL Java_com_cosmicx_pc_NativeMachine_key(JNIEnv* e,jclass,jlong h,jint ascii,jint scan){std::lock_guard<std::mutex> guard(lock_);try{if(ascii<0||ascii>255||scan<0||scan>255)throw std::runtime_error("Tecla fuera de rango");machine(h).key(ascii,scan);}catch(const std::exception& ex){exception(e,ex);}}
JNIEXPORT jstring JNICALL Java_com_cosmicx_pc_NativeMachine_mount(JNIEnv* e,jclass,jlong h,jint fd,jlong size,jstring mode){
    if(!mode){e->ThrowNew(e->FindClass("java/lang/IllegalArgumentException"),"Falta modo de medio");return nullptr;}
    const char* p=e->GetStringUTFChars(mode,nullptr);if(!p)return nullptr;std::string selected(p);e->ReleaseStringUTFChars(mode,p);
    return action(e,h,[&](auto& m){
      if(fd<0||size<512)throw std::runtime_error("Se necesita un archivo local con tamaño conocido y al menos 512 bytes");
      auto file=std::make_shared<File>(dup(fd));if(file->fd<0)throw std::runtime_error("No se pudo conservar el archivo seleccionado");
      struct stat info{};if(fstat(file->fd,&info)!=0||!S_ISREG(info.st_mode))throw std::runtime_error("El medio debe permitir acceso aleatorio; descarga primero el archivo en el teléfono");
      if(info.st_size<size)throw std::runtime_error("El tamaño del medio cambió durante la selección");
      auto reader=[file](std::uint64_t offset,std::uint8_t* dst,std::size_t n){
        if(offset>std::uint64_t(INT64_MAX)||n>std::uint64_t(INT64_MAX)-offset)return false;
        std::size_t done=0;while(done<n){const auto got=pread(file->fd,dst+done,n-done,off_t(offset+done));if(got<0&&errno==EINTR)continue;if(got<=0)return false;done+=std::size_t(got);}return true;
      };
      m.mount(reader,std::uint64_t(size),selected);
    });
}
}
