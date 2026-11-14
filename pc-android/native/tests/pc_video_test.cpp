#include "../pc_machine.hpp"
#include <array>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
using cosmic_pc::Cpu;
using cosmic_pc::PcMachine;
using cosmic_pc::PcVideo;
constexpr std::size_t MiB = 1024 * 1024;
unsigned checks = 0;
void need(bool value, const char* reason) {
  ++checks;
  if (!value) throw std::runtime_error(reason);
}
void call(PcMachine& pc, std::uint16_t ax, std::uint16_t bx = 0,
          std::uint16_t cx = 0, std::uint16_t di = 0x9000,
          std::uint16_t es = 0) {
  // Every service is dispatched by the actual CPU's INT opcode.
  pc.cpu.reset(0, 0x7c00);
  pc.cpu.a20_enabled = true;
  pc.cpu.memory.write_block(0x7c00, reinterpret_cast<const std::uint8_t*>("\xcd\x10\xf4"), 3);
  pc.cpu.set_reg16(Cpu::EAX, ax);
  pc.cpu.set_reg16(Cpu::EBX, bx);
  pc.cpu.set_reg16(Cpu::ECX, cx);
  pc.cpu.set_reg16(Cpu::EDI, di);
  pc.cpu.segments[Cpu::ES] = es;
  pc.cpu.regs[Cpu::EDX] = 0x89abcdef;
  pc.cpu.regs[Cpu::ESI] = 0x12345678;
  pc.cpu.eflags = 0x247;
  pc.run(10);
  need(!pc.cpu.fault && pc.cpu.halted, "BIOS call must finish without a CPU fault");
  need(pc.cpu.regs[Cpu::EDX] == 0x89abcdef && pc.cpu.regs[Cpu::ESI] == 0x12345678,
       "VBE must preserve registers it does not return");
  need(pc.cpu.eflags == 0x247, "VBE return uses AX without fabricating CPU flags");
}
std::uint64_t far_address(std::uint32_t far_pointer) {
  return std::uint64_t(far_pointer >> 16) * 16 + (far_pointer & 0xffff);
}
template<class F> void rejected(F action, const char* reason) {
  bool caught = false;
  try { action(); } catch (const std::exception&) { caught = true; }
  need(caught, reason);
}
std::array<std::uint8_t, 4> pixel(const PcMachine& pc, unsigned x, unsigned y) {
  std::array<std::uint8_t, 4> out{};
  pc.copy_video_rgba(x, y, 1, 1, out.data(), out.size());
  return out;
}
void run_demo(PcMachine& pc, unsigned width, unsigned height) {
  pc.boot_graphics_demo(width, height);
  for (unsigned slices = 0; slices < 100 && !pc.cpu.halted && !pc.cpu.fault; ++slices)
    pc.run(50000);
  if (pc.cpu.fault) throw std::runtime_error(pc.cpu.fault->describe());
  need(pc.cpu.halted, "Graphics boot guest exceeded its bounded execution budget");
  need(pc.cpu.mode_name() == std::string("protected32"), "Guest must enter protected32 itself");
  need(pc.cpu.read32(cosmic_pc::kGraphicsDemoResultAddress) == cosmic_pc::kGraphicsDemoResult,
       "Guest did not verify its own framebuffer pixels");
}
void font_information(PcMachine& pc) {
  call(pc, 0x4f02, 0x4142);
  need(pc.video_info().graphics && pc.video_info().width == 1024 && pc.video_info().height == 768,
       "font query fixture must select the mode observed in the boot report");
  auto& cpu = pc.cpu;
  cpu.write32(PcVideo::kFramebufferAddress, 0x11223344);
  const auto query = [&](std::uint8_t selector) {
    // Replay the real-mode INT at CS:IP 2000:0a0f followed by HLT.
    constexpr std::array<std::uint8_t, 3> code{0xcd, 0x10, 0xf4};
    cpu.reset(0x2000, 0x0a0f);
    cpu.a20_enabled = true;
    cpu.memory.write_block(0x20a0f, code.data(), code.size());
    cpu.set_reg16(Cpu::EAX, 0x1130);
    cpu.set_reg16(Cpu::EBX, std::uint16_t(selector) << 8);
    cpu.regs[Cpu::EBP] = 0x12340000;
    cpu.regs[Cpu::ECX] = 0x56789abc;
    cpu.regs[Cpu::EDX] = 0x9abcde00;
    cpu.eflags = 0x256;
    pc.run(3);
    need(cpu.halted && !cpu.fault && cpu.eip == 0x0a12, "INT 10h 1130 did not return");
    need(cpu.reg16(Cpu::EAX) == 0x1130 && cpu.reg16(Cpu::EBX) == (selector << 8) &&
             cpu.eflags == 0x256 && (cpu.regs[Cpu::EBP] >> 16) == 0x1234,
         "FONT INFO must preserve flags, AX, BX and the high half of EBP");
    return cpu.real_mode_segment_address(Cpu::ES, cpu.reg16(Cpu::EBP));
  };
  const auto check_alternate = [&](std::uint8_t selector, std::uint64_t base, unsigned rows) {
    const auto table = query(selector);
    need(table >= base + 256 * rows && table + 62 * (rows + 1) + 1 <= 0xd0000 &&
             cpu.regs[Cpu::ECX] == 0x56780010 && cpu.regs[Cpu::EDX] == 0x9abcde2f,
         "Alternate font must return a ROM patch table and retain the active 1024x768 geometry");
    unsigned previous = 0;
    bool expected_glyph = false;
    std::uint64_t record = table;
    for (unsigned i = 0; i < 62; ++i, record += rows + 1) {
      const auto glyph = cpu.read8(record);
      if (!glyph) break;
      need(glyph > previous, "Alternate font records must have unique, nonzero character codes");
      previous = glyph;
      if (glyph != 'M') continue;
      expected_glyph = true;
      for (unsigned row = 0; row < rows; ++row)
        need(cpu.read8(record + 1 + row) == cpu.read8(base + glyph * rows + row) >> 1,
             "Alternate M must be centered in the 9-dot cell");
    }
    need(expected_glyph && !cpu.read8(record), "Alternate font must terminate after its replacement records");

    // After returning from INT 10h, the real-mode guest reads the table
    // through its returned ES:BP, without knowing the ROM segment or offset.
    constexpr std::array<std::uint8_t, 5> read_glyph{0x26, 0x8a, 0x46, 0x00, 0xf4};
    cpu.memory.write_block(0x20a12, read_glyph.data(), read_glyph.size());
    cpu.halted = false;
    pc.run(3);
    need(cpu.halted && !cpu.fault && cpu.reg8(Cpu::EAX) == cpu.read8(table),
         "Guest could not read the alternate font through ES:BP");
    return table;
  };
  const auto fourteen = query(2);
  need(cpu.reg16(Cpu::ECX) == 16 && cpu.regs[Cpu::EDX] == 0x9abcde2f,
       "CX and DL report the active character geometry, not the requested bitmap height");
  need(fourteen >= PcVideo::kBiosTablesAddress && fourteen + 256 * 14 <= 0xd0000 &&
           cpu.read8(fourteen + 0xdb * 14 + 13) == 0xff,
       "BH=02h must return a usable full 8x14 CP437 bitmap");
  for (unsigned row = 0; row < 14; ++row)
    need(cpu.read8(fourteen + 'A' * 14 + row) == cosmic_pc::kBiosFont16['A' * 16 + (2 * row + 1) * 16 / 28],
         "FONT INFO returned the wrong glyph scanline");
  const auto alternate_fourteen = check_alternate(5, fourteen, 14);
  const auto eight = query(3);
  need(query(4) == eight + 128 * 8 && query(0) == eight + 128 * 8,
       "8x8 high-half pointer and BIOS font vector must agree");
  const auto sixteen = query(6);
  need(check_alternate(7, sixteen, 16) > alternate_fourteen,
       "The 9x14 and 9x16 tables must have separate ROM pointers");
  need(query(1) == sixteen, "active font vector must describe the selected 8x16 cells");
  cpu.write32(0x43 * 4, 0x30000120);
  need(query(1) == 0x30120, "FONT INFO must read the guest's current INT 43 pointer");
  need(query(8) == 0 && cpu.regs[Cpu::ECX] == 0x56789abc && cpu.regs[Cpu::EDX] == 0x9abcde00,
       "Reserved font selectors must preserve the input registers");
  need(pc.video_info().mode == 0x142 && cpu.read32(PcVideo::kFramebufferAddress) == 0x11223344,
       "FONT INFO must preserve the framebuffer and video mode");
  call(pc, 0x0003);
  need(query(2) == fourteen && cpu.reg16(Cpu::ECX) == 16 && cpu.reg8(Cpu::EDX) == 24,
       "VGA text mode must restore the 25-row BIOS geometry");
}
void write_string(PcMachine& pc) {
  call(pc,0x0003);
  const auto cell=[&](unsigned row,unsigned column){return pc.cpu.read16(0xb8000+(row*80+column)*2);};
  const auto run=[&](std::uint8_t mode,std::uint16_t position,std::uint16_t source,
                     std::uint16_t count,std::uint16_t segment=0x0200,bool a20=false){
    pc.cpu.reset(0,0x7c00);
    pc.cpu.a20_enabled=a20;
    pc.cpu.eflags=0x247;
    const std::array<std::uint8_t,22> guest{{
        0x68,std::uint8_t(segment),std::uint8_t(segment>>8),0x07, // PUSH segment; POP ES.
        0xbd,std::uint8_t(source),std::uint8_t(source>>8), // MOV BP, source offset.
        0xbb,0x0a,0x00,      // MOV BX,000Ah (page 0, light green).
        0xb9,std::uint8_t(count),std::uint8_t(count>>8),
        0xba,std::uint8_t(position),std::uint8_t(position>>8),
        0xb8,mode,0x13,
        0xcd,0x10,0xf4}};
    pc.cpu.memory.write_block(0x7c00,guest.data(),guest.size());
    pc.cpu.set_reg16(Cpu::ESP,0x7b00);
    pc.run(40);
  };
  const std::array<std::uint8_t,8> line{{'A','B','\r','\n','C',8,'D',7}};
  pc.cpu.memory.write_block(0x2200,line.data(),line.size());
  run(1,0x014f,0x0200,line.size());
  need(pc.cpu.halted&&!pc.cpu.fault&&pc.cpu.reg16(Cpu::EAX)==0x1301&&
       pc.cpu.reg16(Cpu::EDX)==0x014f&&pc.cpu.reg16(Cpu::EBP)==0x0200&&
       pc.cpu.reg16(Cpu::ECX)==line.size()&&pc.cpu.reg16(Cpu::EBX)==0x000a&&
       pc.cpu.eflags==0x247,"VGA string BIOS call did not preserve registers and FLAGS");
  need(cell(1,79)==0x0a41&&cell(2,0)==0x0a42&&cell(3,0)==0x0a44&&cell(3,1)==0x0720&&
       pc.cpu.read16(0x450)==0x0301,"VGA text write must wrap and honor CR, LF, BS and BEL");

  const std::array<std::uint8_t,4> attributed{{'X',0x1e,'Y',0x2c}};
  pc.cpu.memory.write_block(0x2200,attributed.data(),attributed.size());
  run(2,0x1800,0x0200,2);
  need(pc.cpu.halted&&!pc.cpu.fault&&cell(24,0)==0x1e58&&cell(24,1)==0x2c59&&
       pc.cpu.read16(0x450)==0x0301,"VGA inline attributes or cursor preservation lost");
  run(3,0x184f,0x0200,2);
  need(pc.cpu.halted&&!pc.cpu.fault&&cell(23,79)==0x1e58&&cell(24,0)==0x2c59&&
       cell(24,1)==0x0720&&pc.cpu.read16(0x450)==0x1801,
       "VGA string scroll must preserve written attributes and reposition the cursor");

  run(3,0,0xffff,1);
  need(pc.cpu.fault&&!pc.cpu.halted&&cell(0,0)==0x0720&&cell(24,0)==0x2c59&&
       pc.cpu.read16(0x450)==0x1801,"VGA string crossing ES:FFFF must leave the display unchanged");
  pc.cpu.memory[0x000010]='L';
  pc.cpu.memory[0x100010]='H';
  run(0,0,0x0020,1,0xffff,false);
  need(pc.cpu.halted&&!pc.cpu.fault&&cell(0,0)==0x0a4c,"VGA string must read wrapped A20 source bytes");
  run(0,1,0x0020,1,0xffff,true);
  need(pc.cpu.halted&&!pc.cpu.fault&&cell(0,1)==0x0a48,"VGA string must read high RAM with A20 enabled");
  std::cout<<"C03_BIOS_VGA_STRING_PASS page=0 inline_attributes=1 cursor=1 scroll=1 bounds=1 a20=1\n";
}
}

int main() {
  try {
    PcMachine pc(std::size_t(2048) * MiB);
    pc.boot_demo();
    need(!pc.video_info().graphics && pc.video_info().width == 80 && pc.video_info().height == 25,
         "Initial mode must retain VGA text geometry");
    font_information(pc);

    // VBE2 requests receive 512 bytes; legacy callers receive exactly 256.
    pc.cpu.memory.fill(0x9000, 0xa5, 520);
    pc.cpu.memory.write_block(0x9000, reinterpret_cast<const std::uint8_t*>("VBE2"), 4);
    call(pc, 0x4f00);
    need(pc.cpu.reg16(Cpu::EAX) == 0x004f, "VBE discovery failed");
    need(pc.cpu.read32(0x9000) == 0x41534556 && pc.cpu.read16(0x9004) == 0x0200,
         "VBE signature/version layout");
    need(pc.cpu.read32(0x900a) == 2 && pc.cpu.read16(0x9012) == 2048,
         "Controller must expose a 128-MiB LFB without VGA port compatibility");
    need(pc.cpu.read8(0x91ff) == 0 && pc.cpu.read8(0x9200) == 0xa5,
         "VBE2 query must write exactly 512 bytes");
    const auto mode_list = far_address(pc.cpu.read32(0x900e));
    for (unsigned i = 0; i < PcVideo::kModes.size(); ++i)
      need(pc.cpu.read16(mode_list + i * 2) == PcVideo::kModes[i].id, "Advertised mode list mismatch");
    need(pc.cpu.read16(mode_list + PcVideo::kModes.size() * 2) == 0xffff, "Mode list terminator");
    need(pc.cpu.read8(far_address(pc.cpu.read32(0x9006))) == 'C', "OEM far pointer must refer to real guest bytes");
    need(pc.cpu.read8(far_address(pc.cpu.read32(0x9016))) == 'X', "Vendor far pointer");
    pc.cpu.memory.fill(0x9000, 0xa5, 520);
    call(pc, 0x4f00);
    need(pc.cpu.read8(0x90ff) == 0 && pc.cpu.read8(0x9100) == 0xa5,
         "Legacy query must not overwrite a caller's 256-byte buffer");

    for (const auto& mode : PcVideo::kModes) {
      call(pc, 0x4f01, 0, mode.id);
      need(pc.cpu.reg16(Cpu::EAX) == 0x004f, "Enumerated mode query failed");
      need(pc.cpu.read16(0x9000) == 0x00fb, "Mode attributes must declare LFB only and no TTY/3D");
      need(pc.cpu.read16(0x9010) == mode.width * 4 && pc.cpu.read16(0x9012) == mode.width &&
           pc.cpu.read16(0x9014) == mode.height, "Mode dimensions and byte pitch");
      need(pc.cpu.read8(0x9018) == 1 && pc.cpu.read8(0x9019) == 32 && pc.cpu.read8(0x901b) == 6,
           "Mode must expose one direct-color 32-bit plane");
      need(pc.cpu.read32(0x9028) == PcVideo::kFramebufferAddress, "ModeInfoBlock PhysBasePtr");
      need(pc.cpu.read8(0x901f) == 8 && pc.cpu.read8(0x9020) == 16 &&
           pc.cpu.read8(0x9021) == 8 && pc.cpu.read8(0x9022) == 8 &&
           pc.cpu.read8(0x9023) == 8 && pc.cpu.read8(0x9024) == 0 &&
           pc.cpu.read8(0x9025) == 8 && pc.cpu.read8(0x9026) == 24 && pc.cpu.read8(0x9027) == 0,
           "Guest BGRX channel layout must be explicit, without alpha semantics");
      need(pc.cpu.read8(0x901d) == 0 && pc.cpu.read8(0x901e) == 1 &&
           pc.cpu.read32(0x902c) == 0 && pc.cpu.read16(0x9032) == 0,
           "Unused image-page and VBE3 fields must not advertise unsupported features");
    }

    call(pc, 0x4f02, 0x4147);
    need(pc.cpu.reg16(Cpu::EAX) == 0x004f, "8K LFB mode selection");
    const auto eight_k = pc.video_info();
    need(eight_k.graphics && eight_k.width == 7680 && eight_k.height == 4320 &&
         eight_k.pitch == 30720 && eight_k.byte_size == 132710400,
         "8K mode must contain all 33,177,600 guest pixels");
    need(pc.cpu.memory.allocated_bytes() < MiB, "Selecting zero-filled 8K must not allocate a dense guest image");
    pc.cpu.write32(PcVideo::kFramebufferAddress, 0x11223344);
    pc.cpu.write32(PcVideo::kFramebufferAddress + eight_k.byte_size - 4, 0x88776655);
    call(pc, 0x4f02, 0xc147);
    need(pc.cpu.read32(PcVideo::kFramebufferAddress) == 0x11223344 &&
         pc.cpu.read32(PcVideo::kFramebufferAddress + eight_k.byte_size - 4) == 0x88776655,
         "Preserve-memory mode must retain first and last full pixel");
    call(pc, 0x4f03);
    need(pc.cpu.reg16(Cpu::EBX) == 0xc147, "Get mode must preserve the requested LFB and memory-clear bits");
    need(pixel(pc, 0, 0) == std::array<std::uint8_t,4>{{0x22,0x33,0x44,255}} &&
         pixel(pc, 7679, 4319) == std::array<std::uint8_t,4>{{0x77,0x66,0x55,255}},
         "RGBA transfer must swap B/R and force X to opaque alpha at high addresses");
    for (const auto mode : {0x0147, 0x4347, 0x4947, 0x5147, 0x6147, 0x41ff}) {
      call(pc, 0x4f02, std::uint16_t(mode));
      need(pc.cpu.reg16(Cpu::EAX) == 0x014f && pc.video_info().mode == 0x147 &&
           pc.cpu.read32(PcVideo::kFramebufferAddress) == 0x11223344,
           "Unsupported set-mode request changed the current graphics state");
    }
    call(pc, 0x4f04);
    need(pc.cpu.reg16(Cpu::EAX) == 0x014f, "Unimplemented VBE functions must report failure");
    pc.cpu.memory.fill(0xff80, 0x9a, 256);
    call(pc, 0x4f01, 0, 0x147, 0xff80);
    need(pc.cpu.reg16(Cpu::EAX) == 0x014f && pc.cpu.read8(0xff80) == 0x9a &&
         pc.cpu.read8(0x1007f) == 0x9a, "Wrapped BIOS output buffer must fail without a partial write");
    call(pc, 0x4f02, 0x4147);
    need(pc.cpu.read32(PcVideo::kFramebufferAddress) == 0 &&
         pc.cpu.read32(PcVideo::kFramebufferAddress + eight_k.byte_size - 4) == 0,
         "Normal set-mode must clear the complete visible image");

    // Transfers cover arbitrary tiles and padded rows, with no partial result
    // on invalid coordinates, null buffers, insufficient capacity or overflow.
    pc.cpu.write32(PcVideo::kFramebufferAddress + 30720 + 8, 0x00112233);
    pc.cpu.write32(PcVideo::kFramebufferAddress + 61440 + 8, 0x00445566);
    std::array<std::uint8_t,20> tile; tile.fill(0xa5);
    pc.copy_video_rgba(2,1,1,2,tile.data(),tile.size(),12);
    need(tile[0] == 0x11 && tile[1] == 0x22 && tile[2] == 0x33 && tile[3] == 255 &&
         tile[12] == 0x44 && tile[13] == 0x55 && tile[14] == 0x66 && tile[15] == 255 &&
         tile[4] == 0xa5 && tile[11] == 0xa5 && tile[16] == 0xa5,
         "Tile transfer must honor guest pitch and preserve destination padding");
    const auto saved = tile;
    rejected([&]{pc.copy_video_rgba(7679,0,2,1,tile.data(),tile.size());}, "Horizontal overflow accepted");
    rejected([&]{pc.copy_video_rgba(0,4319,1,2,tile.data(),tile.size());}, "Vertical overflow accepted");
    rejected([&]{pc.copy_video_rgba(std::numeric_limits<std::uint32_t>::max(),0,1,1,tile.data(),tile.size());}, "Wrapped x accepted");
    rejected([&]{pc.copy_video_rgba(0,0,0,1,tile.data(),tile.size());}, "Empty tile accepted");
    rejected([&]{pc.copy_video_rgba(0,0,2,2,tile.data(),7);}, "Short buffer accepted");
    rejected([&]{pc.copy_video_rgba(0,0,1,2,tile.data(),tile.size(),std::numeric_limits<std::size_t>::max());}, "Overflowing stride accepted");
    rejected([&]{pc.copy_video_rgba(0,0,1,1,nullptr,4);}, "Null output accepted");
    rejected([&]{pc.copy_video_bgrx(tile.data(),tile.size());}, "Short full-frame buffer accepted");
    need(tile == saved, "A rejected video transfer modified destination bytes");

    // Real guest execution at both the normal resolution and the actual 8K
    // address range. None of the validation pixels are injected by the host.
    for (const auto& dimensions : {std::pair<unsigned,unsigned>{1280,720}, {7680,4320}}) {
      const auto width = dimensions.first, height = dimensions.second;
      run_demo(pc, width, height);
      need(pixel(pc,0,0) == std::array<std::uint8_t,4>{{0xff,0x40,0x40,255}}, "Guest top-left red marker");
      need(pixel(pc,width-1,0) == std::array<std::uint8_t,4>{{0x32,0xe8,0x75,255}}, "Guest top-right green marker");
      need(pixel(pc,0,height-1) == std::array<std::uint8_t,4>{{0x47,0x7b,0xff,255}}, "Guest bottom-left blue marker");
      need(pixel(pc,width-1,height-1) == std::array<std::uint8_t,4>{{0xf5,0xf7,0xfa,255}}, "Guest bottom-right white marker");
      need(pixel(pc,width/2,height/2) == std::array<std::uint8_t,4>{{0x37,0xd5,0xff,255}}, "Guest central cyan bar");
      need(pixel(pc,width/2,height/4) == std::array<std::uint8_t,4>{{0,0,0,255}}, "Untouched VRAM must remain opaque black");
      need(pc.cpu.instructions >= std::uint64_t(width) * (height/16), "Pixel work must be executed by guest instructions");
      need(pc.cpu.memory.allocated_bytes() < 12 * MiB, "Sparse 8K pattern should only back the pages it touches");
      const auto before = pc.cpu.memory.allocated_bytes();
      std::vector<std::uint8_t> raw(static_cast<std::size_t>(pc.video_info().byte_size));
      pc.copy_video_bgrx(raw.data(),raw.size());
      need(raw[0] == 0x40 && raw[1] == 0x40 && raw[2] == 0xff && raw[3] == 0 &&
           raw[raw.size()-4] == 0xfa && raw[raw.size()-3] == 0xf7 && raw[raw.size()-2] == 0xf5,
           "Raw snapshot must copy actual BGRX first and last pixels");
      need(pc.cpu.memory.allocated_bytes() == before, "Copying zero VRAM must not materialize guest pages");
    }
    need(pc.json().find("\"video_width\":7680") != std::string::npos &&
         pc.json().find("\"video_bytes\":132710400") != std::string::npos &&
         pc.json().find("\"graphics_3d_ready\":false") != std::string::npos,
         "Diagnostics must report real graphics geometry without a 3D claim");

    call(pc, 0x4f02, 3);
    need(!pc.video_info().graphics && pc.video_info().mode == 3 && pc.cpu.read8(0x449) == 3,
         "VBE text-mode fallback must synchronize video metadata and BIOS state");
    pc.cpu.write16(0xb8000, 0x1e41);
    pc.cpu.write16(0xb8000 + 3998, 0x2f5a);
    std::array<std::uint8_t,4001> text{};
    text.back() = 0xa5;
    pc.copy_vga_text(text.data(),text.size());
    need(text[0] == 'A' && text[1] == 0x1e && text[3998] == 'Z' && text[3999] == 0x2f && text[4000] == 0xa5,
         "VGA transfer must preserve all 2000 character/attribute pairs exactly");
    rejected([&]{pc.copy_vga_text(text.data(),3999);}, "Short VGA buffer accepted");
    rejected([&]{pc.copy_video_rgba(0,0,1,1,text.data(),text.size());}, "Text mode must not fabricate graphics pixels");
    call(pc, 0x0082);
    need(pc.video_info().mode == 2 && pc.cpu.read16(0xb8000) == 0x1e41, "VGA preserve bit must retain text");
    call(pc, 0x0003);
    need(pc.cpu.read16(0xb8000) == 0x0720 && !pc.video_info().graphics, "VGA clear must retain old 80x25 behavior");
    write_string(pc);

    PcMachine small(16 * MiB);
    small.boot_demo();
    call(small, 0x4f00);
    need(small.cpu.reg16(Cpu::EAX) == 0x014f, "Small RAM profile must not advertise an unavailable LFB");
    rejected([&]{small.boot_graphics_demo();}, "Small RAM profile should reject the graphics demo before replacement");
    rejected([&]{pc.boot_graphics_demo(12000,8000);}, "Unadvertised resolution accepted");

    PcMachine limited(std::size_t(2048) * MiB, 32 * 4096);
    limited.boot_graphics_demo(7680,4320);
    for (unsigned i=0; i<100 && !limited.cpu.fault && !limited.cpu.halted; ++i) limited.run(50000);
    need(limited.cpu.fault && limited.cpu.fault->code == cosmic_pc::FaultCode::MemoryBudget,
         "VRAM guest writes must obey the existing resident memory budget");
    need(limited.cpu.memory.allocated_bytes() <= limited.cpu.memory.budget_bytes(), "VRAM exceeded the guest memory budget");
    std::cout << "PC video: " << checks << " checks passed (guest 1280x720 + 7680x4320, VBE, tiles, memory budget)\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL PC video: " << error.what() << '\n';
    return 1;
  }
}
