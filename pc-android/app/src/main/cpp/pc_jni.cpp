#include <jni.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cerrno>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <vector>
#include <limits>
#include "pc_machine.hpp"
#include "windows_iso_probe.hpp"
#include <sstream>
#include "mundo_x/infinite_bridge.hpp"

namespace {
std::mutex lock_;
std::map<jlong,std::unique_ptr<cosmic_pc::PcMachine>> machines;
jlong next_id=1;
constexpr std::size_t snapshot_budget=256ULL*1024*1024;
std::size_t snapshot_bytes=0;
jlong next_frame_id=1;
// A frame owns one BGRX copy, not a Java bitmap or a second full RGBA buffer.
// All ownership changes and accounting are protected by lock_.
struct VideoSnapshot {
    jlong id=0;
    bool graphics=false;
    unsigned width=80,height=25,pitch=160;
    std::vector<std::uint8_t> bytes;
    std::size_t accounted=0;
    ~VideoSnapshot(){snapshot_bytes-=accounted;}
};
struct VideoLease {std::shared_ptr<VideoSnapshot> frame;unsigned borrowers=0;};
std::map<jlong,std::shared_ptr<VideoSnapshot>> frame_cache;
std::map<jlong,VideoLease> frame_leases;
struct File {int fd;explicit File(int f):fd(f){}~File(){if(fd>=0)close(fd);}};
cosmic_pc::PcMachine& machine(jlong h){auto it=machines.find(h);if(it==machines.end())throw std::runtime_error("Identificador de máquina no válido");return *it->second;}
jstring java(JNIEnv* e,const std::string& s){return e->NewStringUTF(s.c_str());}
void exception(JNIEnv* e,const std::exception& ex){e->ThrowNew(e->FindClass("java/lang/IllegalStateException"),ex.what());}
std::string state_json(const cosmic_pc::PcMachine& m){
    std::string s=m.json();s.pop_back();
    return s+",\"video_snapshot_bytes\":"+std::to_string(snapshot_bytes)
        +",\"video_snapshot_budget_bytes\":"+std::to_string(snapshot_budget)+"}";
}
template<class F> jstring action(JNIEnv* e,jlong h,F fn,bool mutates=true){std::lock_guard<std::mutex> guard(lock_);try{auto& m=machine(h);try{fn(m);}catch(const std::exception& ex){m.host_error=ex.what();}if(mutates)frame_cache.erase(h);return java(e,state_json(m));}catch(const std::exception& ex){exception(e,ex);return nullptr;}}
std::shared_ptr<VideoSnapshot> capture_frame(jlong h){
    auto& m=machine(h);
    auto found=frame_cache.find(h);
    if(found!=frame_cache.end())return found->second;
    const auto info=m.video_info();
    auto frame=std::make_shared<VideoSnapshot>();
    frame->graphics=info.graphics;
    frame->width=info.graphics?info.width:80;
    frame->height=info.graphics?info.height:25;
    frame->pitch=info.graphics?info.pitch:160;
    const std::uint64_t pixels=std::uint64_t(frame->width)*frame->height;
    if(frame->width==0||frame->height==0||frame->width>7680||frame->height>7680||pixels>33177600)
        throw std::runtime_error("Dimensiones de vídeo fuera del límite 8K");
    const std::size_t bytes=info.graphics?std::size_t(pixels*4):4000;
    if(info.graphics&&(info.pitch!=frame->width*4||info.byte_size!=bytes))
        throw std::runtime_error("Geometría de framebuffer BGRX inconsistente");
    if(bytes>snapshot_budget-snapshot_bytes)
        throw std::runtime_error("Presupuesto de instantáneas de vídeo agotado; se conserva el cuadro anterior");
    frame->bytes.resize(bytes);
    if(info.graphics)m.copy_video_bgrx(frame->bytes.data(),frame->bytes.size());
    else m.copy_vga_text(frame->bytes.data(),frame->bytes.size());
    frame->accounted=bytes;snapshot_bytes+=bytes;
    if(next_frame_id==std::numeric_limits<jlong>::max())throw std::runtime_error("Identificadores de vídeo agotados");
    frame->id=next_frame_id++;
    frame_cache[h]=frame;
    return frame;
}
const VideoSnapshot& leased_frame(jlong id){
    const auto found=frame_leases.find(id);
    if(found==frame_leases.end()||found->second.borrowers==0)
        throw std::invalid_argument("La instantánea de vídeo ya no está adquirida");
    return *found->second.frame;
}
std::uint8_t* direct_buffer(JNIEnv* e,jobject buffer,std::size_t bytes){
    if(!buffer)throw std::invalid_argument("Falta el buffer directo de vídeo");
    const jclass type=e->GetObjectClass(buffer);
    if(!type)throw std::runtime_error("No se pudo comprobar el buffer de vídeo");
    const jmethodID read_only=e->GetMethodID(type,"isReadOnly","()Z");
    e->DeleteLocalRef(type);
    if(!read_only||e->CallBooleanMethod(buffer,read_only)==JNI_TRUE)
        throw std::invalid_argument("El buffer de vídeo debe permitir escritura");
    const jlong capacity=e->GetDirectBufferCapacity(buffer);
    auto* data=static_cast<std::uint8_t*>(e->GetDirectBufferAddress(buffer));
    if(capacity<0||std::uint64_t(capacity)<bytes||!data)
        throw std::invalid_argument("Buffer directo de vídeo insuficiente");
    return data;
}
std::string ascii(JNIEnv* e,jstring source,std::size_t maximum){
  if(!source)throw std::invalid_argument("Falta texto del núcleo");
  const auto length=e->GetStringLength(source);
  if(std::size_t(length)>maximum)throw std::length_error("Texto del núcleo excede el límite de bytes");
  std::string result(std::size_t(length),'\0');
  const jchar* chars=e->GetStringChars(source,nullptr);
  if(!chars)throw std::runtime_error("No se pudo leer el texto del núcleo");
  bool valid=true;
  for(jsize i=0;i<length;++i){if(chars[i]==0||chars[i]>127){valid=false;break;}result[std::size_t(i)]=char(chars[i]);}
  e->ReleaseStringChars(source,chars);
  if(!valid)throw std::invalid_argument("El núcleo acepta texto ASCII sin caracteres NUL");
  return result;
}
// Validation failures from this device never poison the x86 host_error state.
template<class F> jstring quantum_action(JNIEnv* e,jlong h,F fn){
  std::lock_guard<std::mutex> guard(lock_);
  try{auto& m=machine(h);try{fn(m);}catch(const std::exception& ex){m.quantum_reject(ex.what());}
    if(e->ExceptionCheck())return nullptr;return java(e,state_json(m));
  }catch(const std::exception& ex){if(!e->ExceptionCheck())exception(e,ex);return nullptr;}
}
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
JNIEXPORT void JNICALL Java_com_cosmicx_pc_NativeMachine_destroy(JNIEnv*,jclass,jlong h){std::lock_guard<std::mutex> guard(lock_);frame_cache.erase(h);machines.erase(h);}
JNIEXPORT jstring JNICALL Java_com_cosmicx_pc_NativeMachine_bootDemo(JNIEnv* e,jclass,jlong h){return action(e,h,[](auto& m){m.boot_demo();});}
JNIEXPORT jstring JNICALL Java_com_cosmicx_pc_NativeMachine_bootLongModeDemo(JNIEnv* e,jclass,jlong h){return action(e,h,[](auto& m){m.boot_long_mode_demo();});}
JNIEXPORT jstring JNICALL Java_com_cosmicx_pc_NativeMachine_bootQuantumDemo(JNIEnv* e,jclass,jlong h,jboolean long_mode){return action(e,h,[&](auto& m){m.boot_quantum_demo(long_mode==JNI_TRUE);});}
JNIEXPORT jstring JNICALL Java_com_cosmicx_pc_NativeMachine_bootQuantumPhaseDemo(JNIEnv* e,jclass,jlong h){return action(e,h,[](auto& m){m.boot_quantum_demo(false,true);});}
JNIEXPORT jstring JNICALL Java_com_cosmicx_pc_NativeMachine_quantumCommand(JNIEnv* e,jclass,jlong h,jstring program){return quantum_action(e,h,[&](auto& m){m.quantum_command(ascii(e,program,cosmic_pc::PcQuantumDevice::max_request_bytes));});}
JNIEXPORT jstring JNICALL Java_com_cosmicx_pc_NativeMachine_quantumReset(JNIEnv* e,jclass,jlong h,jstring background){return quantum_action(e,h,[&](auto& m){m.quantum_reset(ascii(e,background,4));});}
JNIEXPORT jstring JNICALL Java_com_cosmicx_pc_NativeMachine_quantumProbe(JNIEnv* e,jclass,jlong h,jstring first,jstring second){return quantum_action(e,h,[&](auto& m){m.quantum_probe(ascii(e,first,4096),ascii(e,second,4096));});}
JNIEXPORT jstring JNICALL Java_com_cosmicx_pc_NativeMachine_quantumRestore(JNIEnv* e,jclass,jlong h,jstring snapshot){return quantum_action(e,h,[&](auto& m){m.quantum_restore(ascii(e,snapshot,16*1024*1024));});}
JNIEXPORT jstring JNICALL Java_com_cosmicx_pc_NativeMachine_quantumSnapshot(JNIEnv* e,jclass,jlong h){std::lock_guard<std::mutex> guard(lock_);try{return java(e,machine(h).quantum_snapshot());}catch(const std::exception& ex){exception(e,ex);return nullptr;}}
JNIEXPORT jlong JNICALL Java_com_cosmicx_pc_NativeMachine_quantumGraphicsHandle(JNIEnv* e,jclass,jlong h){std::lock_guard<std::mutex> guard(lock_);try{return reinterpret_cast<jlong>(mundo_x::share_infinite_session(machine(h).quantum_session));}catch(const std::exception& ex){exception(e,ex);return 0;}}
JNIEXPORT jstring JNICALL Java_com_cosmicx_pc_NativeMachine_bootGraphicsDemo(JNIEnv* e,jclass,jlong h,jint width,jint height){return action(e,h,[&](auto& m){if(width<1||height<1)throw std::invalid_argument("Resolución de invitado inválida");m.boot_graphics_demo(unsigned(width),unsigned(height));});}
JNIEXPORT jstring JNICALL Java_com_cosmicx_pc_NativeMachine_reset(JNIEnv* e,jclass,jlong h){return action(e,h,[](auto& m){m.reset();});}
JNIEXPORT jstring JNICALL Java_com_cosmicx_pc_NativeMachine_inspect(JNIEnv* e,jclass,jlong h){return action(e,h,[](auto&){},false);}
JNIEXPORT jstring JNICALL Java_com_cosmicx_pc_NativeMachine_run(JNIEnv* e,jclass,jlong h,jint budget){return action(e,h,[&](auto& m){if(budget<1||budget>50000)throw std::runtime_error("Presupuesto de ejecución inválido");m.run(budget);});}
JNIEXPORT jstring JNICALL Java_com_cosmicx_pc_NativeMachine_screen(JNIEnv* e,jclass,jlong h){std::lock_guard<std::mutex> guard(lock_);try{return java(e,machine(h).screen());}catch(const std::exception& ex){exception(e,ex);return nullptr;}}
JNIEXPORT void JNICALL Java_com_cosmicx_pc_NativeMachine_key(JNIEnv* e,jclass,jlong h,jint ascii,jint scan){std::lock_guard<std::mutex> guard(lock_);try{if(ascii<0||ascii>255||scan<0||scan>255)throw std::runtime_error("Tecla fuera de rango");machine(h).key(ascii,scan);frame_cache.erase(h);}catch(const std::exception& ex){exception(e,ex);}}
JNIEXPORT jstring JNICALL Java_com_cosmicx_pc_NativeMachine_acquireVideoFrame(JNIEnv* e,jclass,jlong h){
    std::lock_guard<std::mutex> guard(lock_);
    try{
        const auto frame=capture_frame(h);
        if(frame_leases.size()>=8&&!frame_leases.count(frame->id))throw std::runtime_error("Demasiadas instantáneas de vídeo activas");
        const auto existing=frame_leases.find(frame->id);
        if(existing!=frame_leases.end()&&existing->second.borrowers>=32)
            throw std::runtime_error("Demasiadas lecturas de una instantánea");
        const std::string json="{\"kind\":"+std::to_string(frame->graphics?1:0)
            +",\"width\":"+std::to_string(frame->width)+",\"height\":"+std::to_string(frame->height)
            +",\"revision\":"+std::to_string(frame->id)+",\"snapshot_bytes\":"+std::to_string(frame->bytes.size())+"}";
        jstring result=java(e,json);
        if(!result)return nullptr;
        auto& lease=frame_leases[frame->id];
        lease.frame=frame;++lease.borrowers;
        return result;
    }catch(const std::exception& ex){exception(e,ex);return nullptr;}
}
JNIEXPORT jint JNICALL Java_com_cosmicx_pc_NativeMachine_copyVideoText(JNIEnv* e,jclass,jlong revision,jobject target){
    std::lock_guard<std::mutex> guard(lock_);
    try{const auto& frame=leased_frame(revision);if(frame.graphics)throw std::invalid_argument("La instantánea contiene píxeles, no texto");std::memcpy(direct_buffer(e,target,4000),frame.bytes.data(),4000);return 4000;}
    catch(const std::exception& ex){exception(e,ex);return -1;}
}
JNIEXPORT jint JNICALL Java_com_cosmicx_pc_NativeMachine_copyVideoRows(JNIEnv* e,jclass,jlong revision,jint first,jint rows,jobject target){
    std::lock_guard<std::mutex> guard(lock_);
    try{
        const auto& frame=leased_frame(revision);
        if(!frame.graphics||first<0||rows<1||unsigned(first)>frame.height||unsigned(rows)>frame.height-unsigned(first))
            throw std::invalid_argument("Filas de vídeo fuera de rango");
        const std::size_t bytes=std::size_t(frame.width)*unsigned(rows)*4;
        if(bytes>2ULL*1024*1024)throw std::invalid_argument("Cada transferencia de vídeo se limita a 2 MiB");
        auto* dst=direct_buffer(e,target,bytes);
        const auto* src=frame.bytes.data()+std::size_t(first)*frame.width*4;
        for(std::size_t i=0;i<bytes;i+=4){dst[i]=src[i+2];dst[i+1]=src[i+1];dst[i+2]=src[i];dst[i+3]=255;}
        return jint(bytes);
    }catch(const std::exception& ex){exception(e,ex);return -1;}
}
JNIEXPORT void JNICALL Java_com_cosmicx_pc_NativeMachine_releaseVideoFrame(JNIEnv*,jclass,jlong revision){
    std::lock_guard<std::mutex> guard(lock_);
    const auto found=frame_leases.find(revision);
    if(found!=frame_leases.end()&&found->second.borrowers>0&&--found->second.borrowers==0)
        frame_leases.erase(found);
}
JNIEXPORT jstring JNICALL Java_com_cosmicx_pc_NativeMachine_probeIso(JNIEnv* e,jclass,jint fd,jlong size){
    try {
        if(fd<0 || size<19*2048)
            throw std::invalid_argument("Selecciona una ISO local válida de al menos 19 sectores");
        auto file=std::make_shared<File>(dup(fd));
        if(file->fd<0)throw std::runtime_error("No se pudo duplicar el descriptor de ISO");
        struct stat info{};
        if(fstat(file->fd,&info)!=0 || !S_ISREG(info.st_mode) || info.st_size<size)
            throw std::runtime_error("La ISO necesita archivo local con acceso aleatorio");
        auto reader=[file](std::uint64_t offset,std::uint8_t* target,std::size_t bytes){
            if(offset>std::uint64_t(INT64_MAX) ||
               bytes>std::uint64_t(INT64_MAX)-offset)return false;
            std::size_t done=0;
            while(done<bytes){
                const auto n=pread(file->fd,target+done,bytes-done,off_t(offset+done));
                if(n<0&&errno==EINTR)continue;
                if(n<=0)return false;
                done+=std::size_t(n);
            }
            return true;
        };
        cosmic_pc::WindowsIsoProbe inspector(std::uint64_t(size),std::move(reader));
        const auto report=inspector.inspect();
        std::ostringstream out;
        out<<"{\"format\":\"iso9660-eltorito\""
           <<",\"file_bytes\":"<<report.image_bytes
           <<",\"bytes_examined\":"<<report.bytes_read
           <<",\"volume_id\":"<<cosmic_pc::PcMachine::quote(report.volume_id)
           <<",\"catalog_lba\":"<<report.catalog_lba
           <<",\"entries_examined\":"<<report.catalog_entries
           <<",\"checksum_valid\":"<<(report.valid_catalog?"true":"false")
           <<",\"bios_entry\":"<<(report.bios_entry?"true":"false")
           <<",\"bios_image_lba\":"<<report.bios_lba
           <<",\"bios_initial_sectors\":"<<report.bios_sectors
           <<",\"uefi_entry\":"<<(report.uefi_entry?"true":"false")
           <<",\"uefi_image_lba\":"<<report.uefi_lba
           <<",\"uefi_initial_sectors\":"<<report.uefi_sectors
           <<",\"can_attempt_bios_boot\":"
              <<(report.bios_supported_by_emulator?"true":"false")
           <<",\"uefi_implemented\":false"
           <<",\"windows_installer_implemented\":false"
           <<",\"user_iso_copied\":false"
           <<",\"engine\":\"MundoX-own-x86+Ether\""
           <<"}";
        return java(e,out.str());
    }catch(const std::exception& ex){
        exception(e,ex);return nullptr;
    }
}
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
