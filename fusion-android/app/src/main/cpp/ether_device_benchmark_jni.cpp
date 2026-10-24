#include <jni.h>
#include "mundo_x/ether_render_engine.hpp"
#include "mundo_x/ether_renderer.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Modern=mundo_x::EtherRenderEngine;
using Classic=mundo_x::EtherRenderer;
using Clock=std::chrono::steady_clock;

void fail(JNIEnv* env,const char* what){
    auto type=env->FindClass("java/lang/IllegalStateException");
    if(type)env->ThrowNew(type,what);
}
double milliseconds(Clock::time_point start){
    return std::chrono::duration<double,std::milli>(Clock::now()-start).count();
}
double median(std::vector<double> samples){
    std::sort(samples.begin(),samples.end());
    return samples[samples.size()/2];
}
Classic::Body body(unsigned i,bool ultra){
    const double x=ultra ? (static_cast<int>((i*37)%311)-155)*.41
                         : (static_cast<int>((i*37)%41)-20)*.15;
    const double y=ultra ? (static_cast<int>((i*31)%211)-105)*.35
                         : (static_cast<int>((i*31)%29)-14)*.15;
    const double z=ultra ? -20.-static_cast<double>((i*17)%97)
                         : -3.-static_cast<double>((i*17)%19)*.23;
    const double radius=ultra ? .08+static_cast<double>(i%11)*.03
                              : .17+static_cast<double>(i%5)*.035;
    return {i,{x,y,z},radius,{{.3,.6,.8}}};
}
std::string comparable(int width,int height,unsigned bodies){
    Classic classic(width,height,16);
    Modern modern(width,height,16);
    classic.set_camera({0,0,12},{0,0,0},47);
    modern.camera({0,0,12},{0,0,0},47);
    for(unsigned i=0;i<bodies;++i){
        const auto b=body(i,false);
        classic.add_body(b);
        modern.add({b.id,{b.center.x,b.center.y,b.center.z},b.radius,b.color});
    }
    modern.prepare();
    std::vector<std::uint32_t> oldOutput(static_cast<std::size_t>(width)*height);
    std::vector<std::uint32_t> newOutput(oldOutput.size());
    auto oldPass=[&](){
        classic.render(false);
        const auto& pixels=classic.frame();
        for(std::size_t i=0;i<pixels.size();++i)
            oldOutput[i]=pixels[i].rgba;
    };
    auto newPass=[&](){
        modern.export_image([&](std::size_t x,std::size_t y,const Modern::Pixel& p){
            newOutput[y*static_cast<std::size_t>(width)+x]=p.rgba;
        });
    };
    oldPass();newPass();
    std::uint64_t wrong=0;
    const auto& pixels=classic.frame();
    for(std::size_t i=0;i<pixels.size();++i){
        const auto p=modern.observe(i%width,i/width);
        const auto& q=pixels[i];
        if(p.body!=q.body||p.rgba!=q.rgba||
           !(p.depth==q.depth||(std::isinf(p.depth)&&std::isinf(q.depth))))
            ++wrong;
    }
    std::vector<double> oldTimes,newTimes;
    for(int sample=0;sample<5;++sample){
        if((sample&1)==0){
            auto start=Clock::now();oldPass();oldTimes.push_back(milliseconds(start));
            start=Clock::now();newPass();newTimes.push_back(milliseconds(start));
        }else{
            auto start=Clock::now();newPass();newTimes.push_back(milliseconds(start));
            start=Clock::now();oldPass();oldTimes.push_back(milliseconds(start));
        }
    }
    auto packedStart=Clock::now();
    modern.capture();
    const double captureMs=milliseconds(packedStart);
    auto packedOutStart=Clock::now();
    modern.export_image([&](std::size_t x,std::size_t y,const Modern::Pixel& p){
        newOutput[y*static_cast<std::size_t>(width)+x]=p.rgba;
    });
    const double packedExport=milliseconds(packedOutStart);
    for(std::size_t i=0;i<newOutput.size();++i)
        if(oldOutput[i]!=newOutput[i])++wrong;
    const auto info=modern.info();
    const double classicMs=median(oldTimes),modernMs=median(newTimes);
    std::ostringstream out;
    out<<std::fixed<<std::setprecision(4)<<'{'
       <<"\"type\":\"equal_scene_cpu\""
       <<",\"width\":"<<width<<",\"height\":"<<height
       <<",\"bodies\":"<<bodies<<",\"samples\":5"
       <<",\"old_native_ms\":"<<classicMs
       <<",\"new_native_ms\":"<<modernMs
       <<",\"speedup\":"<<(modernMs>0?classicMs/modernMs:0.)
       <<",\"old_cpu_fps_equivalent\":"<<(classicMs>0?1000./classicMs:0.)
       <<",\"new_cpu_fps_equivalent\":"<<(modernMs>0?1000./modernMs:0.)
       <<",\"packed_capture_ms\":"<<captureMs
       <<",\"packed_full_export_ms\":"<<packedExport
       <<",\"packed_buffer_bytes\":"<<info.buffer.allocated_bytes
       <<",\"new_lazy_buffer_bytes\":0"
       <<",\"android_output_rgba_bytes\":"<<newOutput.size()*4
       <<",\"full_old_pixel_struct_bytes\":"<<pixels.size()*sizeof(Classic::Pixel)
       <<",\"pixel_mismatches\":"<<wrong
       <<",\"real_display_fps_measured\":false"
       <<'}';
    return out.str();
}
std::string high8k(){
    constexpr std::size_t width=7680,height=4320;
    constexpr unsigned n=4096;
    Modern engine(width,height,16);
    engine.camera({0,0,12},{0,0,0},47.);
    for(unsigned i=0;i<n;++i){
        const auto b=body(i,true);
        engine.add({b.id,{b.center.x,b.center.y,b.center.z},b.radius,b.color});
    }
    auto start=Clock::now();engine.prepare();
    const double lazyMs=milliseconds(start);
    // Sample the lazy observer first; all 8K pixels remain implicit.
    std::array<Modern::Pixel,256> samples{};
    for(std::size_t i=0;i<samples.size();++i)
        samples[i]=engine.observe(i*7919%width,i*3467%height);
    start=Clock::now();engine.capture();
    const double packedMs=milliseconds(start);
    std::size_t wrong=0;
    for(std::size_t i=0;i<samples.size();++i)
        if(!(samples[i]==engine.observe(i*7919%width,i*3467%height)))++wrong;
    const auto s=engine.info();
    std::ostringstream out;
    out<<std::fixed<<std::setprecision(4)<<'{'
       <<"\"type\":\"8k_sparse_cpu\",\"width\":"<<width
       <<",\"height\":"<<height<<",\"bodies\":"<<n
       <<",\"lazy_prepare_ms\":"<<lazyMs
       <<",\"packed_capture_ms\":"<<packedMs
       <<",\"visible_pixels\":"<<s.buffer.visible_pixels
       <<",\"candidate_queries\":"<<s.buffer.candidate_queries
       <<",\"spans\":"<<s.buffer.spans
       <<",\"packed_buffer_bytes\":"<<s.buffer.allocated_bytes
       <<",\"full_rgba_bytes\":"<<s.buffer.full_rgba_bytes
       <<",\"pixel_mismatches\":"<<wrong
       <<",\"full_8k_frame_generated\":false"
       <<",\"real_display_fps_measured\":false"
       <<'}';
    return out.str();
}
} // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_mundox_fusion3d_EtherBenchmarkActivity_benchmarkNative(
        JNIEnv* env,jclass,jint preset){
    try{
        std::string text;
        switch(preset){
            case 0: text=comparable(320,180,32);break;
            case 1: text=comparable(344,224,60);break;
            case 2: text=comparable(640,360,64);break;
            case 3: text=high8k();break;
            default: throw std::invalid_argument("invalid Ether device benchmark preset");
        }
        return env->NewStringUTF(text.c_str());
    }catch(const std::exception& e){
        fail(env,e.what());return nullptr;
    }
}
