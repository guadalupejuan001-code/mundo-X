  // MM0–MM7 alias the low 64 bits of the physical x87 registers, independent
  // of TOP. A successful MMX instruction enters MMX state even if it only
  // reads an MM register. Only a written MM destination changes its exponent.
  void require_mmx_state() const {
    if(cr0&0x4U)fail("MMX disabled by CR0.EM",FaultCode::InvalidInstruction);
    if(cr0&0x8U)fail("MMX task-switched CR0.TS",FaultCode::DeviceNotAvailable);
    require_no_pending_x87();
  }

  void enter_mmx_state() noexcept {
    fpu_status=std::uint16_t(fpu_status&~0x3800U);
    fpu_tags=0;
  }

  std::uint64_t mmx_read(unsigned reg) const { return ext_significand(x87[reg&7U]); }
  void mmx_write(unsigned reg,std::uint64_t value) {
    auto& slot=x87[reg&7U];
    for(unsigned i=0;i<8;++i)slot[i]=std::uint8_t(value>>(8*i));
    slot[8]=slot[9]=0xff;
  }

  std::uint64_t mmx_source(const Operand& operand,unsigned bytes=8) {
    return operand.is_register ? mmx_read(operand.index) : get(operand,bytes);
  }

  static std::array<std::uint8_t,16> mmx_vector(std::uint64_t value) {
    std::array<std::uint8_t,16> result{};
    packed_set(result,0,8,value);
    return result;
  }

  void mmx_convert(unsigned opcode) {
    require_sse_state();
    const auto m=modrm();
    std::uint32_t status=0;
    if(opcode==0x2a) { // CVTPI2PS / CVTPI2PD, and m64 forms.
      require_xmm(m.reg);
      if(m.rm.is_register)require_mmx_state();
      const auto source=mmx_source(m.rm);
      auto result=xmm[m.reg];
      const unsigned bytes=operand_override_?8:4;
      {
        HostRound rounding(mxcsr);
        for(unsigned i=0;i<2;++i) {
          volatile std::int32_t integer=std::int32_t(signed_value(source>>(32*i),4));
          if(bytes==8)packed_set(result,i*8,8,fp64_bits(static_cast<double>(integer)));
          else {
            volatile float converted=static_cast<float>(integer);
            packed_set(result,i*4,4,fp32_bits(converted));
          }
        }
        status=rounding.status();
      }
      simd_status(status);
      xmm[m.reg]=result;
      if(m.rm.is_register)enter_mmx_state();
      return;
    }
    // CVT(T)PS2PI and CVT(T)PD2PI are MMX even with a memory source.
    require_mmx_state();
    const auto source=operand_override_ ? read_xmm_operand(m.rm,true) :
                                       mmx_vector(sse64_source(m.rm));
    const bool truncate=opcode==0x2c;
    std::uint64_t out=0;
    for(unsigned i=0;i<2;++i) {
      const auto value=operand_override_ ?
          sse2_fp64_to_integer(packed_lane(source,i*8,8),4,truncate,status) :
          sse_fp32_to_integer(std::uint32_t(packed_lane(source,i*4,4)),4,truncate,status);
      out|=(value&0xffffffffU)<<(32*i);
    }
    simd_status(status);
    mmx_write(m.reg,out);
    enter_mmx_state();
  }

  void mmx_xmm_move() { // MOVDQ2Q / MOVQ2DQ.
    require_sse_state();
    require_mmx_state();
    const auto m=modrm();
    if(!m.rm.is_register)fail("MMX/XMM transfer requires registers",FaultCode::InvalidInstruction);
    if(repeat_==0xf2) {
      require_xmm(m.rm.index);
      mmx_write(m.reg,packed_lane(xmm[m.rm.index],0,8));
    }else {
      require_xmm(m.reg);
      xmm[m.reg]=mmx_vector(mmx_read(m.rm.index));
    }
    enter_mmx_state();
  }

  bool try_mmx(unsigned opcode) {
    if(!repeat_&&(opcode==0x2a||opcode==0x2c||opcode==0x2d)) {
      mmx_convert(opcode);
      return true;
    }
    if(opcode==0xd6&&repeat_&&!operand_override_) {
      mmx_xmm_move();
      return true;
    }
    if(operand_override_||repeat_)return false;

    switch(opcode) {
      case 0x77: // EMMS.
        require_mmx_state();
        fpu_status=std::uint16_t(fpu_status&~0x3800U);
        fpu_tags=0xffff;
        return true;
      case 0x6e: case 0x7e: case 0x6f: case 0x7f: { // MOVD / MOVQ.
        require_mmx_state();
        const auto m=modrm();
        const bool qword=(opcode&1U)||(is_long_mode()&&(rex_&8U));
        const unsigned bytes=qword?8:4;
        if(opcode==0x6e)mmx_write(m.reg,get(m.rm,bytes));
        else if(opcode==0x6f)mmx_write(m.reg,mmx_source(m.rm));
        else if(opcode==0x7f&&m.rm.is_register)mmx_write(m.rm.index,mmx_read(m.reg));
        else put(m.rm,bytes,mmx_read(m.reg));
        break;
      }
      case 0x70: { // PSHUFW.
        require_mmx_state();
        const auto m=modrm();
        const auto order=immediate(1);
        const auto source=mmx_source(m.rm);
        std::uint64_t out=0;
        for(unsigned i=0;i<4;++i)out|=((source>>(16*((order>>(2*i))&3U)))&0xffffU)<<(16*i);
        mmx_write(m.reg,out);
        break;
      }
      case 0x71: case 0x72: case 0x73: { // PSRL/PSRA/PSLL immediate.
        require_mmx_state();
        const auto m=modrm();
        const bool valid=m.group==2||m.group==6||(opcode!=0x73&&m.group==4);
        if(!m.rm.is_register||!valid)fail("invalid MMX shift encoding",FaultCode::InvalidInstruction);
        const auto count=immediate(1);
        auto vector=mmx_vector(mmx_read(m.rm.index));
        shift_packed_lanes(vector,2U<<(opcode-0x71U),count,m.group,8);
        mmx_write(m.rm.index,packed_lane(vector,0,8));
        break;
      }
      case 0xc4: case 0xc5: { // PINSRW / PEXTRW.
        require_mmx_state();
        const auto m=modrm();
        if(opcode==0xc5&&!m.rm.is_register)
          fail("PEXTRW source must be MMX",FaultCode::InvalidInstruction);
        const auto shift=(immediate(1)&3U)*16;
        if(opcode==0xc4) {
          const auto word=get(m.rm,2);
          mmx_write(m.reg,(mmx_read(m.reg)&~(0xffffULL<<shift))|(word<<shift));
        }else put_reg(m.reg,4,(mmx_read(m.rm.index)>>shift)&0xffffU);
        break;
      }
      case 0xd7: { // PMOVMSKB.
        require_mmx_state();
        const auto m=modrm();
        if(!m.rm.is_register)fail("PMOVMSKB source must be MMX",FaultCode::InvalidInstruction);
        const auto source=mmx_read(m.rm.index);
        unsigned bits=0;
        for(unsigned i=0;i<8;++i)bits|=unsigned((source>>(8*i+7))&1U)<<i;
        put_reg(m.reg,4,bits);
        break;
      }
      case 0xe7: { // MOVNTQ; this CPU has no write-back cache.
        require_mmx_state();
        const auto m=modrm();
        require_memory(m.rm);
        put(m.rm,8,mmx_read(m.reg));
        break;
      }
      case 0xf7: { // MASKMOVQ.
        require_mmx_state();
        const auto m=modrm();
        if(!m.rm.is_register)fail("MASKMOVQ requires MMX registers",FaultCode::InvalidInstruction);
        masked_store(mmx_vector(mmx_read(m.reg)),mmx_vector(mmx_read(m.rm.index)),8);
        break;
      }
      case 0x60: case 0x61: case 0x62: case 0x63:
      case 0x64: case 0x65: case 0x66: case 0x67:
      case 0x68: case 0x69: case 0x6a: case 0x6b:
      case 0x74: case 0x75: case 0x76:
      case 0xd1: case 0xd2: case 0xd3: case 0xd4: case 0xd5:
      case 0xd8: case 0xd9: case 0xda: case 0xdb:
      case 0xdc: case 0xdd: case 0xde: case 0xdf:
      case 0xe0: case 0xe1: case 0xe2: case 0xe3: case 0xe4: case 0xe5:
      case 0xe8: case 0xe9: case 0xea: case 0xeb:
      case 0xec: case 0xed: case 0xee: case 0xef:
      case 0xf1: case 0xf2: case 0xf3: case 0xf4: case 0xf5: case 0xf6:
      case 0xf8: case 0xf9: case 0xfa: case 0xfb:
      case 0xfc: case 0xfd: case 0xfe: {
        require_mmx_state();
        const auto m=modrm();
        // The low unpack encodings consume only 32 bits of a memory source.
        const auto lhs=mmx_vector(mmx_read(m.reg));
        const auto rhs=mmx_vector(mmx_source(m.rm,opcode<0x63?4:8));
        const auto result=packed_integer_result<8>(opcode,lhs,rhs);
        mmx_write(m.reg,packed_lane(result,0,8));
        break;
      }
      default:return false;
    }
    enter_mmx_state();
    return true;
  }
