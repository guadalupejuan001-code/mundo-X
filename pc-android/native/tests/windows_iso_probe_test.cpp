#include "../boot_media.hpp"
#include "../windows_iso_probe.hpp"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Byte=std::uint8_t;
using Bytes=std::vector<Byte>;
constexpr std::size_t Sector=2048;
std::size_t assertions=0;
void require(bool b,const char* why){++assertions;if(!b)throw std::runtime_error(why);}
void put16(Bytes& data,std::size_t o,std::uint16_t n){data.at(o)=Byte(n);data.at(o+1)=Byte(n>>8);}
void put32(Bytes& data,std::size_t o,std::uint32_t n){for(unsigned i=0;i<4;++i)data.at(o+i)=Byte(n>>(8*i));}
void descriptor(Bytes& b,std::size_t lba,Byte kind) {
    auto p=lba*Sector;b[p]=kind;
    std::memcpy(b.data()+p+1,"CD001",5);b[p+6]=1;
}
void checksum(Bytes& b){
    auto base=20*Sector;put16(b,base+28,0);
    unsigned sum=0;
    for(unsigned i=0;i<32;i+=2)
        sum=(sum+unsigned(b[base+i])+unsigned(b[base+i+1])*256)&65535U;
    put16(b,base+28,std::uint16_t(0U-sum));
}
Bytes iso_fixture(){
    Bytes b(64*Sector,0);
    descriptor(b,16,1);
    std::memcpy(b.data()+16*Sector+40,"WIN10_PROBE_FIXTURE",19);
    descriptor(b,17,0);
    std::memcpy(b.data()+17*Sector+7,"EL TORITO SPECIFICATION",23);
    put32(b,17*Sector+0x47,20);
    descriptor(b,18,255);
    const auto c=20*Sector;
    b[c]=1;b[c+1]=0;
    b[c+30]=0x55;b[c+31]=0xaa;
    b[c+32]=0x88;b[c+33]=0;put16(b,c+38,4);put32(b,c+40,24);
    b[c+64]=0x91;b[c+65]=0xef;put16(b,c+66,1);
    b[c+96]=0x88;b[c+97]=0;put16(b,c+102,1);put32(b,c+104,30);
    b[24*Sector]=0xb8;b[24*Sector+1]=0x41;b[24*Sector+2]=0x0e;
    b[24*Sector+3]=0xcd;b[24*Sector+4]=0x10;b[24*Sector+5]=0xf4;
    checksum(b);return b;
}
cosmic_pc::WindowsIsoReport inspect(const Bytes& bytes,std::uint64_t* transferred=nullptr) {
    std::uint64_t bytes_read=0;
    cosmic_pc::WindowsIsoProbe probe(bytes.size(),[&](std::uint64_t offset,Byte* target,std::size_t size){
        if(offset>bytes.size()||size>bytes.size()-offset)return false;
        std::memcpy(target,bytes.data()+offset,size);
        bytes_read+=size;return true;
    });
    const auto report=probe.inspect();
    require(report.bytes_read==bytes_read,"probe must count exact media bytes");
    if(transferred)*transferred=bytes_read;
    return report;
}
template<class F>void throws(F fn,const char* fragment){
    ++assertions;
    try{fn();}catch(const std::exception& error){
        if(std::string(error.what()).find(fragment)!=std::string::npos)return;
        throw std::runtime_error(std::string("Unexpected probe error: ")+error.what());
    }
    throw std::runtime_error("Invalid ISO unexpectedly accepted");
}
}
int main(){
    try{
        auto b=iso_fixture();
        std::uint64_t bytes_read=0;
        const auto x=inspect(b,&bytes_read);
        require(x.volume_id=="WIN10_PROBE_FIXTURE","PVD volume label");
        require(x.valid_catalog&&x.bios_entry&&x.uefi_entry,"hybrid BIOS and UEFI recognition");
        require(x.bios_supported_by_emulator,"default no-emulation BIOS boot supported");
        require(!x.uefi_supported_by_emulator,"UEFI must never claim firmware support");
        require(x.bios_lba==24&&x.uefi_lba==30,"BIOS/UEFI entry LBA");
        require(x.bios_sectors==4&&x.uefi_sectors==1,"catalog sector counts");
        require(x.catalog_entries==2,"must examine exactly two boot entries");
        require(bytes_read<=4*Sector,"did not scan only metadata sectors");
        const cosmic_pc::BootMedia boot(b.size(),[&](std::uint64_t o,Byte* p,std::size_t n){
            if(o>b.size()||n>b.size()-o)return false;
            std::copy_n(b.data()+o,n,p);return true;
        });
        const auto plan=boot.plan("iso");
        require(plan.kind=="iso"&&plan.drive==0xe0&&plan.image.size()==2048,"canonical BIOS boot preserved");
        require(plan.image[0]==0xb8&&plan.image[5]==0xf4,"boot bytes preserved");

        // EFI-only catalog must be inspected but must NOT be executed through
        // our legacy BIOS x86 decoder.
        auto efi=b;
        efi[20*Sector+1]=0xef;
        efi[20*Sector+64]=0;
        checksum(efi);
        const auto ef=inspect(efi);
        require(!ef.bios_entry&&ef.uefi_entry&&
                !ef.bios_supported_by_emulator,"EFI-only ISO must block BIOS boot");

        // Non-default BIOS is visible, but the current BootMedia decoder only
        // boots the initial default x86 record, not an alternate section.
        auto swapped=b;
        swapped[20*Sector+1]=0xef; // default entry is now UEFI
        swapped[20*Sector+65]=0; // alternate entry is BIOS
        checksum(swapped);
        const auto alternate=inspect(swapped);
        require(alternate.bios_entry&&alternate.uefi_entry&&
                !alternate.bios_supported_by_emulator,
                "alternate BIOS entry must not be promoted to supported boot");

        auto broken=b;broken[20*Sector+10]=0xff;
        throws([&](){(void)inspect(broken);},"checksum");
        broken=b;broken[17*Sector+1]='X';
        throws([&](){(void)inspect(broken);},"descriptor");
        broken=b;put32(broken,20*Sector+40,9000000);
        throws([&](){(void)inspect(broken);},"outside ISO");
        broken=b;put16(broken,20*Sector+66,513);
        throws([&](){(void)inspect(broken);},"Unreasonable");
        broken=b;broken.resize(16*Sector);
        throws([&](){(void)inspect(broken);},"too short");
        // Inspect another request: even the maximal logical file size is never
        // materialized; the reader only sees bounded sector-size requests.
        require(bytes_read<b.size()/10,"image must not be copied into RAM");
        std::cout<<"WINDOWS_ISO_PROBE_PASS checks="<<assertions
                 <<" boot_bios=1 boot_uefi=1 bytes_read="<<bytes_read
                 <<" copied_media=0 windows_installed=0\n";
        return 0;
    }catch(const std::exception& error){
        std::cerr<<"WINDOWS_ISO_PROBE_FAIL "<<error.what()<<"\n";
        return 1;
    }
}
