#include "pc_machine.hpp"
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
int main(int argc,char** argv){
  try {
    std::string path,write_path;std::uint64_t budget=100000;bool auto_key=false,long_demo=false;
    std::size_t ram_mib=16,resident_mib=1024;
    for(int i=1;i<argc;++i){const std::string a=argv[i];
      if(a=="--demo")auto_key=true;
      else if(a=="--demo64")long_demo=true;
      else if(a=="--image"&&i+1<argc)path=argv[++i];
      else if(a=="--max-instructions"&&i+1<argc){const auto s=std::string(argv[++i]);std::size_t pos=0;budget=std::stoull(s,&pos);if(pos!=s.size()||budget==0||budget>100000000)throw std::runtime_error("Presupuesto fuera de 1..100000000");}
      else if(a=="--write-demo"&&i+1<argc)write_path=argv[++i];
      else if((a=="--ram-mib"||a=="--resident-mib")&&i+1<argc){const auto s=std::string(argv[++i]);std::size_t pos=0;const auto n=std::stoull(s,&pos);if(pos!=s.size()||n<1||n>2048)throw std::runtime_error("RAM fuera de 1..2048 MiB");if(a=="--ram-mib")ram_mib=n;else resident_mib=n;}
      else throw std::runtime_error("Uso: cosmic_pc_cli [--demo | --demo64 | --image ruta] [--ram-mib N] [--resident-mib N] [--max-instructions N] [--write-demo ruta]");
    }
    if(long_demo&&(!path.empty()||auto_key))throw std::runtime_error("Elige un solo origen de arranque");
    cosmic_pc::PcMachine pc(ram_mib*1024*1024,resident_mib*1024*1024);
    if(!write_path.empty()){auto b=long_demo?cosmic_pc::long_mode_boot_disk():cosmic_pc::demo_boot_disk();std::ofstream f(write_path,std::ios::binary);f.write(reinterpret_cast<char*>(b.data()),b.size());if(!f)throw std::runtime_error("No se pudo escribir el disco de prueba");}
    if(path.empty()){if(long_demo)pc.boot_long_mode_demo();else pc.boot_demo();}else {
      auto f=std::make_shared<std::ifstream>(path,std::ios::binary);if(!*f)throw std::runtime_error("No se pudo abrir el medio");
      f->seekg(0,std::ios::end);const auto end=f->tellg();if(end<0)throw std::runtime_error("Tamaño de medio inválido");
      pc.mount([f](std::uint64_t off,std::uint8_t* p,std::size_t n){f->clear();f->seekg(off);f->read(reinterpret_cast<char*>(p),n);return std::size_t(f->gcount())==n;},std::uint64_t(end));
    }
    while(pc.cpu.instructions<budget&&pc.status()!="fault"&&!pc.cpu.halted){
      if(pc.waiting_key){if(!auto_key)break;pc.key('X',0x2d);}
      const auto before=pc.cpu.instructions;pc.run(unsigned(std::min<std::uint64_t>(5000,budget-before)));
      if(before==pc.cpu.instructions&&!pc.waiting_key)break;
    }
    std::cout<<pc.json()<<'\n'<<pc.screen()<<'\n';
    return pc.status()=="fault"?2:0;
  } catch(const std::exception& e){std::cerr<<"COSMIC X PC: "<<e.what()<<'\n';return 2;}
}
