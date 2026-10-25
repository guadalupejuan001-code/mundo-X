#pragma once
// El Torito read-only inspection for official Windows ISO media.
// This deliberately does not pretend to implement UEFI or boot Windows.
// Only bounded sectors are read; the multi-GB ISO remains in its Android fd.
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace cosmic_pc {

struct WindowsIsoReport {
    std::string volume_id;
    std::uint64_t image_bytes=0;
    std::uint64_t bytes_read=0;
    std::uint32_t catalog_lba=0;
    std::uint32_t bios_lba=0;
    std::uint32_t uefi_lba=0;
    std::uint16_t bios_sectors=0;
    std::uint16_t uefi_sectors=0;
    unsigned catalog_entries=0;
    bool valid_catalog=false;
    bool bios_entry=false;
    bool uefi_entry=false;
    // Existing BootMedia supports ONLY default BIOS x86 no-emulation entry.
    bool bios_supported_by_emulator=false;
    bool uefi_supported_by_emulator=false;
};

class WindowsIsoProbe {
 public:
    using Reader=std::function<bool(std::uint64_t,std::uint8_t*,std::size_t)>;
    static constexpr std::size_t Sector=2048;
    static constexpr unsigned MaxDescriptors=256;
    static constexpr std::size_t MaxCatalogBytes=16*Sector;

    WindowsIsoProbe(std::uint64_t size,Reader reader)
        :size_(size),reader_(std::move(reader)) {
        if(!reader_)throw std::invalid_argument("ISO reader is missing");
        if(size_<19*Sector)throw std::invalid_argument("ISO is too short for ISO9660 boot descriptors");
    }

    WindowsIsoReport inspect() {
        WindowsIsoReport result;
        result.image_bytes=size_;
        std::array<std::uint8_t,Sector> descriptor{};
        read_exact(16*Sector,descriptor.data(),descriptor.size());
        if(!iso_descriptor(descriptor))
            throw std::runtime_error("Not an ISO9660/UDF-compatible CD001 volume");
        if(descriptor[0]==1 && descriptor[6]==1) {
            for(unsigned i=40;i<72;++i) {
                const unsigned c=descriptor[i];
                result.volume_id.push_back(c==0?' ':((c>=32&&c<=126)?char(c):'?'));
            }
            while(!result.volume_id.empty()&&result.volume_id.back()==' ')
                result.volume_id.pop_back();
        }

        bool boot_record=false;
        for(unsigned i=17;i<17+MaxDescriptors;++i) {
            const std::uint64_t offset=std::uint64_t(i)*Sector;
            read_exact(offset,descriptor.data(),descriptor.size());
            if(!iso_descriptor(descriptor) || descriptor[6]!=1)
                throw std::runtime_error("Invalid ISO descriptor while locating El Torito record");
            if(descriptor[0]==255)break;
            if(descriptor[0]==0 && std::memcmp(descriptor.data()+7,"EL TORITO SPECIFICATION",23)==0) {
                // The boot descriptor contains one catalog LBA, not a virtual CPU.
                result.catalog_lba=le32(descriptor.data()+0x47);
                boot_record=true;
                break;
            }
        }
        if(!boot_record)throw std::runtime_error("No El Torito boot catalog in this ISO");
        if(result.catalog_lba==0)throw std::runtime_error("El Torito catalog LBA is zero");
        const auto catalog_start=std::uint64_t(result.catalog_lba)*Sector;

        std::vector<std::uint8_t> catalog;
        catalog.reserve(Sector);
        auto available=[&](std::size_t count) {
            if(count>MaxCatalogBytes)throw std::runtime_error("El Torito catalog exceeds research scan budget");
            while(catalog.size()<count){
                std::array<std::uint8_t,Sector> block{};
                read_exact(catalog_start+catalog.size(),block.data(),block.size());
                catalog.insert(catalog.end(),block.begin(),block.end());
            }
        };
        available(64);
        if(catalog[0]!=1||catalog[30]!=0x55||catalog[31]!=0xaa)
            throw std::runtime_error("Invalid El Torito validation header or signature");
        unsigned checksum=0;
        for(std::size_t i=0;i<32;i+=2)
            checksum=(checksum+le16(catalog.data()+i))&0xffffu;
        if(checksum)throw std::runtime_error("Invalid El Torito catalog checksum");
        result.valid_catalog=true;
        auto entry=[&](const std::uint8_t* e,unsigned platform,bool is_default){
            ++result.catalog_entries;
            if(e[0]!=0x88 || (e[1]&15)!=0)
                return;  // nonbootable or emulated floppy/hard disk, not an EFI/BIOS no-emulation entry
            const auto lba=le32(e+8);
            if(lba==0 || std::uint64_t(lba)*Sector >= size_)
                throw std::runtime_error("El Torito boot image points outside ISO");
            if(platform==0x00 && !result.bios_entry){
                result.bios_entry=true;
                result.bios_lba=lba;
                result.bios_sectors=le16(e+6);
                result.bios_supported_by_emulator=is_default && le16(e+6)>=1
                    && le16(e+6)<=128 && catalog[1]==0;
            }else if(platform==0xef && !result.uefi_entry){
                result.uefi_entry=true;
                result.uefi_lba=lba;
                result.uefi_sectors=le16(e+6);
            }
        };

        entry(catalog.data()+32,catalog[1],true);
        std::size_t cursor=64;
        for(unsigned section=0;section<64;++section){
            available(cursor+32);
            const auto header=catalog.data()+cursor;
            if(header[0]!=0x90 && header[0]!=0x91)break;
            const bool last=(header[0]==0x91);
            const unsigned platform=header[1];
            const unsigned count=le16(header+2);
            if(count>512)throw std::runtime_error("Unreasonable El Torito section entry count");
            cursor+=32;
            for(unsigned i=0;i<count;++i){
                available(cursor+32);
                entry(catalog.data()+cursor,platform,false);
                cursor+=32;
            }
            if(last)break;
        }
        result.bytes_read=bytes_read_;
        return result;
    }

 private:
    std::uint64_t size_;
    Reader reader_;
    std::uint64_t bytes_read_=0;

    static std::uint16_t le16(const std::uint8_t* p) {
        return std::uint16_t(p[0]) | (std::uint16_t(p[1])<<8);
    }
    static std::uint32_t le32(const std::uint8_t* p) {
        return std::uint32_t(p[0]) | (std::uint32_t(p[1])<<8)
             | (std::uint32_t(p[2])<<16) | (std::uint32_t(p[3])<<24);
    }
    static bool iso_descriptor(const std::array<std::uint8_t,Sector>& d) {
        return std::memcmp(d.data()+1,"CD001",5)==0;
    }
    void read_exact(std::uint64_t offset,std::uint8_t* target,std::size_t count) {
        if(offset>size_ || count>size_-offset)
            throw std::runtime_error("ISO descriptor/catalog reaches beyond file size");
        if(!reader_(offset,target,count))
            throw std::runtime_error("Read-only ISO provider could not provide a seekable sector");
        bytes_read_+=count;
    }
};

} // namespace cosmic_pc
