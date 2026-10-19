#pragma once
#include <algorithm>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace cosmic_pc {
// This guest is authored for this project. It is x86 machine code, not a host
// drawing script. Every visible character is produced by the emulated INT 10h.
inline std::vector<std::uint8_t> demo_boot_disk() {
  struct Fix { std::size_t at; std::string label; unsigned kind; };
  std::vector<std::uint8_t> b;
  std::map<std::string,std::size_t> labels;
  std::vector<Fix> fix;
  auto emit=[&](std::initializer_list<std::uint8_t> v){b.insert(b.end(),v);};
  auto mark=[&](const std::string& s){labels[s]=b.size();};
  auto abs=[&](const std::string& s){fix.push_back({b.size(),s,0});emit({0,0});};
  auto call=[&](const std::string& s){emit({0xe8});fix.push_back({b.size(),s,1});emit({0,0});};
  auto branch=[&](std::uint8_t op,const std::string& s){emit({op});fix.push_back({b.size(),s,2});emit({0});};
  auto print=[&](const std::string& s){emit({0xbe});abs(s);call("puts");};
  emit({0xfa,0x31,0xc0,0x8e,0xd8,0x8e,0xc0,0x8e,0xd0,0xbc,0x00,0x7c,0xfb,0xfc});
  print("title");
  emit({0xb8,0x34,0x12,0x05,0x02,0x01,0x3d,0x36,0x13});branch(0x75,"failed");
  emit({0xa3,0x00,0x90,0x8b,0x1e,0x00,0x90,0x81,0xfb,0x36,0x13});branch(0x75,"failed");
  // Read sector 2 through BIOS CHS, then inspect its actual bytes in guest RAM.
  emit({0xb8,0x01,0x02,0xbb,0x00,0x80,0xb9,0x02,0x00,0xba,0x80,0x00,0xcd,0x13});
  branch(0x72,"failed");
  emit({0x81,0x3e,0x00,0x80,0x43,0x58});branch(0x75,"failed");
  print("passed");print("prompt");
  emit({0x31,0xc0,0xcd,0x16,0xb4,0x0e,0xcd,0x10});
  print("done");emit({0xfa,0xf4});
  mark("failed");print("error");emit({0xfa,0xf4});
  mark("puts");emit({0xac,0x84,0xc0});branch(0x74,"ret");
  emit({0xb4,0x0e,0xbb,0x07,0x00,0xcd,0x10});branch(0xeb,"puts");mark("ret");emit({0xc3});
  auto text=[&](const std::string& name,const std::string& s){mark(name);b.insert(b.end(),s.begin(),s.end());b.push_back(0);};
  text("title","COSMIC X PC / CPU x86 propia\r\n");
  text("passed","ALU + RAM + lectura de disco: OK\r\n");
  text("prompt","Pulsa una tecla: ");
  text("done","\r\nEntrada BIOS: OK. CPU detenida con HLT.\r\n");
  text("error","FALLO en el programa de arranque.\r\n");
  for(const auto& f:fix){
    const auto dest=labels.at(f.label);
    if(f.kind==2){const auto d=static_cast<long>(dest)-static_cast<long>(f.at+1);if(d< -128||d>127)throw std::logic_error("demo branch range");b[f.at]=static_cast<std::uint8_t>(d);}
    else {const auto v=static_cast<std::uint16_t>(f.kind==0?0x7c00+dest:dest-(f.at+2));b[f.at]=v&255;b[f.at+1]=v>>8;}
  }
  if(b.size()>510)throw std::logic_error("demo boot sector too large");
  b.resize(1024,0);b[510]=0x55;b[511]=0xaa;
  const std::string sector="CX / sector leido desde el disco virtual";
  std::copy(sector.begin(),sector.end(),b.begin()+512);
  return b;
}
}
