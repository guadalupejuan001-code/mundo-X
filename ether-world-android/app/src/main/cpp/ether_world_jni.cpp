#include <jni.h>

#include <cstdint>
#include <iomanip>
#include <locale>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>

#include "mundo_x/ether_world.hpp"
#include "mundo_x/ether_world_renderer.hpp"

namespace {
using World=mundo_x::EtherWorld;
using Render=mundo_x::EtherWorldRenderer;

struct Session {
  std::unique_ptr<World> world;
  Render renderer;
  explicit Session(std::unique_ptr<World> input) : world(std::move(input)) {}
};

std::mutex registry_lock;
std::unordered_map<jlong,std::unique_ptr<Session>> sessions;
jlong next_id=0;
static_assert(sizeof(jint)==sizeof(std::uint32_t),"JNI pixels need ARGB32");

Session& lookup(jlong id) {
  const auto it=sessions.find(id);
  if(it==sessions.end()||!it->second||!it->second->world)
    throw std::runtime_error("La sesión Ether ya no existe");
  return *it->second;
}
void throw_java(JNIEnv* env,const std::exception& e) {
  const jclass type=env->FindClass("java/lang/IllegalStateException");
  if(type)env->ThrowNew(type,e.what());
}
std::string utf8(JNIEnv* env,jstring value){
  if(!value)throw std::invalid_argument("Texto obligatorio");
  const char* chars=env->GetStringUTFChars(value,nullptr);
  if(!chars)throw std::runtime_error("No se pudo leer texto Android");
  std::string out(chars);
  env->ReleaseStringUTFChars(value,chars);
  return out;
}
std::string report(const World& world,const Render::Output* frame=nullptr) {
  const auto st=world.stats();
  std::ostringstream out;
  out.imbue(std::locale::classic());
  out<<std::fixed<<std::setprecision(3);
  const auto selected=world.cell(st.selected_x,st.selected_z);
  const char* kind="desconocido";
  if(selected.revealed) {
    switch(selected.edit) {
      case 1:kind="faro";break;
      case 2:kind="cristal_construido";break;
      case 3:kind="vacio";break;
      case 4:kind="barrera";break;
      default:
        if(selected.tone<.16f)kind="agua";
        else if(selected.tone<.36f)kind="vegetacion";
        else if(selected.tone<.56f)kind="pradera";
        else if(selected.tone<.76f)kind="piedra";
        else if(selected.tone<.92f)kind="mineral";
        else kind="cristal_natural";
    }
  }
  out<<"{\"ok\":true"
     <<",\"selected_material\":\""<<kind<<"\""
     <<",\"selected_observed\":"<<(selected.revealed?"true":"false")
     <<",\"selected_height\":"<<selected.height
     <<",\"selected_tone\":"<<selected.tone
     <<",\"selected_field\":"<<world.wave(st.selected_x,st.selected_z)
     <<",\"seed\":"<<st.seed
     <<",\"center_x\":"<<st.center_x<<",\"center_z\":"<<st.center_z
     <<",\"selected_x\":"<<st.selected_x<<",\"selected_z\":"<<st.selected_z
     <<",\"angle\":"<<st.angle<<",\"zoom\":"<<st.zoom
     <<",\"ticks\":"<<st.ticks<<",\"revision\":"<<st.revision
     <<",\"logical_cells\":"<<World::kLogicalCells
     <<",\"scans\":"<<st.scans<<",\"explored\":"<<st.explored
     <<",\"ether_edits\":"<<st.edits
     <<",\"probes\":"<<st.probes<<",\"pulses\":"<<st.pulses
     <<",\"simulated_qubits\":"<<st.quantum_active
     <<",\"quantum_components\":"<<st.quantum_components
     <<",\"quantum_field\":"<<st.quantum_field
     <<",\"virtual_world\":true"
     <<",\"external_room_mapped\":false";
  if(frame){
    const auto& p=frame->renderer;
    out<<",\"frame_ms\":"<<frame->render_ms
       <<",\"rendered_bodies\":"<<p.bodies
       <<",\"ether_tile\":"<<p.tile
       <<",\"candidate_tests\":"<<p.candidate_tests
       <<",\"brute_tests\":"<<p.brute_tests
       <<",\"candidate_ratio\":"<<p.test_ratio()
       <<",\"frame_hash\":\""<<p.frame_hash<<"\""
       <<",\"reconstructed_cells\":"<<frame->reconstructed_cells
       <<",\"visible_cells\":"<<frame->visible_cells
       <<",\"exact_mismatches\":"<<p.exact_mismatches;
  }
  out<<"}";
  return out.str();
}
jstring respond(JNIEnv* env,const std::string& text){
  return env->NewStringUTF(text.c_str());
}
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_mundox_etherworld_WorldNative_nativeCreate(
    JNIEnv* env,jclass,jint seed){
  try{
    auto candidate=std::make_unique<Session>(
        std::make_unique<World>(static_cast<std::int32_t>(seed)));
    std::lock_guard<std::mutex> guard(registry_lock);
    const jlong id=++next_id;
    sessions.emplace(id,std::move(candidate));
    return id;
  }catch(const std::exception& e){throw_java(env,e);return 0;}
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_mundox_etherworld_WorldNative_nativeLoad(
    JNIEnv* env,jclass,jstring text) {
  try{
    if(!text||env->GetStringUTFLength(text)>static_cast<jsize>(World::kMaxSaveBytes))
      throw std::invalid_argument("World save over limit");
    auto candidate=std::make_unique<Session>(World::load(utf8(env,text)));
    std::lock_guard<std::mutex> guard(registry_lock);
    const jlong id=++next_id;
    sessions.emplace(id,std::move(candidate));
    return id;
  }catch(const std::exception& e){throw_java(env,e);return 0;}
}

extern "C" JNIEXPORT void JNICALL
Java_com_mundox_etherworld_WorldNative_nativeClose(
    JNIEnv*,jclass,jlong handle){
  std::lock_guard<std::mutex> guard(registry_lock);
  sessions.erase(handle);
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_mundox_etherworld_WorldNative_nativeAction(
    JNIEnv* env,jclass,jlong handle,jstring command,jint a,jint b,jint c){
  try{
    const auto op=utf8(env,command);
    std::lock_guard<std::mutex> guard(registry_lock);
    auto& world=*lookup(handle).world;
    if(op=="move")world.move(a,b);
    else if(op=="scan")world.scan_center();
    else if(op=="scan_selected")world.scan(a,b);
    else if(op=="select")world.select(a,b);
    else if(op=="focus") {
      world.select(a,b);
      // Keep movement bounded and explicit, no instant teleport outside the map.
      const auto st=world.stats();
      const int dx=a-st.center_x,dz=b-st.center_z;
      if(std::abs(dx)>6||std::abs(dz)>6)
        throw std::invalid_argument("Para llegar lejos, avanza por etapas.");
      world.move(dx,dz);
    }
    else if(op=="edit")world.edit_selected(static_cast<unsigned>(c));
    else if(op=="pulse")world.emit_pulse();
    else if(op=="step")world.step(static_cast<unsigned>(c));
    else if(op=="orbit")world.orbit(c*.01);
    else if(op=="zoom")world.magnify(c);
    else if(op=="origin")world.origin();
    else if(op=="follow")world.follow_probe();
    else throw std::invalid_argument("Unknown Ether world action");
    return respond(env,report(world));
  }catch(const std::exception& e){throw_java(env,e);return nullptr;}
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_mundox_etherworld_WorldNative_nativeRender(
    JNIEnv* env,jclass,jlong handle,jintArray viewport,jintArray map,jboolean exact){
  try{
    if(!viewport||!map||
       env->GetArrayLength(viewport)!=Render::kWidth*Render::kHeight||
       env->GetArrayLength(map)!=Render::kMapWidth*Render::kMapWidth)
      throw std::invalid_argument("Image arrays have incorrect dimensions");
    std::lock_guard<std::mutex> guard(registry_lock);
    auto& session=lookup(handle);
    const auto result=session.renderer.render(*session.world,exact==JNI_TRUE);
    env->SetIntArrayRegion(viewport,0,
        static_cast<jsize>(result.view.size()),
        reinterpret_cast<const jint*>(result.view.data()));
    env->SetIntArrayRegion(map,0,
        static_cast<jsize>(result.map.size()),
        reinterpret_cast<const jint*>(result.map.data()));
    return respond(env,report(*session.world,&result));
  }catch(const std::exception& e){throw_java(env,e);return nullptr;}
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_mundox_etherworld_WorldNative_nativeOptimize(
    JNIEnv* env,jclass,jlong handle){
  try{
    std::lock_guard<std::mutex> guard(registry_lock);
    auto& session=lookup(handle);
    const auto t=session.renderer.optimize();
    std::ostringstream out;
    out<<"{\"ok\":true,\"tile\":"<<t.chosen_tile
       <<",\"median_ms\":"<<t.median_ms
       <<",\"candidate_tests\":"<<t.candidate_tests
       <<",\"oracle_mismatches\":"<<t.exact_mismatches
       <<",\"frame_hash\":\""<<t.frame_hash<<"\"}";
    return respond(env,out.str());
  }catch(const std::exception& e){throw_java(env,e);return nullptr;}
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_mundox_etherworld_WorldNative_nativeSave(
    JNIEnv* env,jclass,jlong handle){
  try{
    std::lock_guard<std::mutex> guard(registry_lock);
    return respond(env,lookup(handle).world->save());
  }catch(const std::exception& e){throw_java(env,e);return nullptr;}
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_mundox_etherworld_WorldNative_nativeStatus(
    JNIEnv* env,jclass,jlong handle){
  try{
    std::lock_guard<std::mutex> guard(registry_lock);
    return respond(env,report(*lookup(handle).world));
  }catch(const std::exception& e){throw_java(env,e);return nullptr;}
}
