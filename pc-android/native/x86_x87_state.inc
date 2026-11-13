// Included within Cpu to keep the legacy x87 decoder and its architectural
// state operations separate from the integer decoder and numeric backend.
  void require_no_pending_x87() const {
    if (!(fpu_status & 0x0080U)) return;
    if (cr0 & 0x20U) fail("pending unmasked x87 exception (#MF)", FaultCode::X87FloatingPoint);
    if (!x87_ignne_) throw WaitingForX87{};
  }

  bool x87_raise(unsigned flags) {
    const bool unmasked = (flags & ~fpu_control & 0x3fU) != 0;
    fpu_status |= std::uint16_t(flags | (unmasked ? 0x8080U : 0U));
    return unmasked;
  }
  void x87_set_c1(bool raised) {
    fpu_status = std::uint16_t((fpu_status & ~0x0200U) | (raised ? 0x0200U : 0U));
  }
  void fpu_retire_pop() {
    const auto top = fpu_top();
    fpu_tag_set(top,3);
    fpu_status = std::uint16_t((fpu_status & ~0x3800U) | (((top+1U)&7U)<<11));
  }
  bool x87_operand(unsigned slot, Extended80& value) {
    if (fpu_tag(slot) != 3) {
      value = x87[slot];
      return true;
    }
    fpu_stack_fault(false);
    value = x87_indefinite();
    return (fpu_control & 1) != 0;
  }
  void x87_assign(unsigned slot, const Extended80& value) {
    x87[slot] = value;
    fpu_tag_set(slot,classify_ext(value));
  }

  static Extended80 x87_bits(std::uint64_t significand, unsigned exponent, bool negative) {
    Extended80 value{};
    for (unsigned i=0;i<8;++i) value[i] = std::uint8_t(significand>>(8*i));
    value[8] = std::uint8_t(exponent);
    value[9] = std::uint8_t((exponent>>8)|(negative?0x80U:0U));
    return value;
  }
  static Extended80 x87_integer(std::uint64_t magnitude,bool negative) {
    if (!magnitude) return {};
    unsigned exponent = 16383 + 63;
    while (!(magnitude>>63)) { magnitude <<= 1; --exponent; }
    return x87_bits(magnitude, exponent, negative);
  }
  static Extended80 x87_integer_operand(std::uint64_t bits,unsigned size) {
    const bool negative = (bits & sign_bit(size)) != 0;
    return x87_integer(negative ? (-bits)&mask(size) : bits,negative);
  }
  static bool x87_unsupported(const Extended80& value) {
    return ext_biased_exp(value)!=0 && !(ext_significand(value)>>63);
  }
  static bool x87_nan(const Extended80& value) {
    return ext_biased_exp(value)==0x7fff && (ext_significand(value)&0x7fffffffffffffffULL)!=0;
  }
  static bool x87_snan(const Extended80& value) {
    return x87_nan(value) && !(value[7]&0x40U);
  }
  static bool x87_denormal(const Extended80& value) {
    return ext_biased_exp(value)==0 && ext_significand(value)!=0;
  }
  struct X87Rounded { std::uint64_t magnitude; bool inexact, incremented; };
  static X87Rounded x87_round_shift(std::uint64_t significand, int shift,
                                    bool negative, unsigned rounding) {
    if (shift<=0) return {significand,false,false};
    const auto magnitude = shift>=64 ? 0 : significand>>shift;
    const auto discarded = shift>=64 ? significand : significand&((std::uint64_t{1}<<shift)-1);
    if (!discarded) return {magnitude,false,false};
    bool increment = (rounding==1&&negative) || (rounding==2&&!negative);
    if (rounding==0&&shift<=64) {
      const auto half = std::uint64_t{1}<<(shift-1);
      increment = discarded>half || (discarded==half&&(magnitude&1));
    }
    return {magnitude+increment,true,increment};
  }
  unsigned x87_rc() const { return (fpu_control>>10)&3U; }
  void x87_record(unsigned op,unsigned modrm_byte,const Operand* memory_operand=nullptr) {
    fpu_ip = is_real_mode() ? real_segment_base(CS)+start_ip_ : start_ip_;
    fpu_cs = is_real_mode() ? 0 : start_cs_;
    if (memory_operand) {
      const auto offset = operand_offset(*memory_operand);
      const auto segment = memory_operand->segment;
      if (is_real_mode()) fpu_dp = real_segment_base(segment)+offset;
      else if (is_long_mode()) fpu_dp = offset+(segment==FS ? fs_base : segment==GS ? gs_base : 0);
      else fpu_dp = offset;
      fpu_ds = is_real_mode() ? 0 : segments[segment];
    }
    // Use legacy Fopcode compatibility semantics, also exposed by FNSTENV
    // on modern hardware. Control instructions leave all three fields alone.
    fpu_opcode=std::uint16_t(((op&7U)<<8)|modrm_byte);
  }

  std::array<std::uint8_t,108> x87_read_bytes(const Operand& operand,unsigned count) {
    const auto linear = segment_linear(operand.segment,operand_offset(operand),count,Access::Read);
    std::array<std::uint8_t,108> data{};
    for (unsigned i=0;i<count;++i) data[i] = std::uint8_t(read_linear(linear+i,1));
    return data;
  }
  void x87_write_bytes(const Operand& operand,const std::uint8_t* data,unsigned count) {
    const auto linear = segment_linear(operand.segment,operand_offset(operand),count,Access::Write);
    for (unsigned i=0;i<count;++i) {
      const auto physical=translate(linear+i,Access::Write);
      if (mmio_at(physical))
        fail("x87 multi-byte transfer to MMIO is unsupported",FaultCode::UnsupportedState);
      (void)physical_index(physical);
    }
    for (unsigned i=0;i<count;++i) write_linear(linear+i,1,data[i]);
  }
  static unsigned x87_word(const std::array<std::uint8_t,108>& data,unsigned offset,unsigned size=2) {
    unsigned word=0;
    for (unsigned i=0;i<size;++i) word|=unsigned(data[offset+i])<<(8*i);
    return word;
  }
  static void x87_word(std::array<std::uint8_t,108>& data,unsigned offset,unsigned size,unsigned word) {
    for (unsigned i=0;i<size;++i) data[offset+i]=std::uint8_t(word>>(8*i));
  }
  void x87_environment(const Operand& operand,bool restore,bool registers) {
    const unsigned word=op32_?4:2, env=word*7;
    if (restore) {
      const auto data=x87_read_bytes(operand,env+(registers?80:0));
      fpu_control=fpu_normalize_control(std::uint16_t(x87_word(data,0)));
      fpu_status=std::uint16_t(x87_word(data,word));
      fpu_tags=std::uint16_t(x87_word(data,word*2));
      fpu_ip=x87_word(data,word*3,word);
      fpu_dp=x87_word(data,word*5,word);
      const auto cs=x87_word(data,word*4,word),ds=x87_word(data,word*6,word);
      if (is_real_mode()) {
        const unsigned upper=word==4?0x0ffff000U:0xf000U;
        fpu_ip=(fpu_ip&0xffffU)|(std::uint64_t(cs&upper)<<4);
        fpu_dp=(fpu_dp&0xffffU)|(std::uint64_t(ds&upper)<<4);
        fpu_opcode=std::uint16_t(cs&0x7ffU);
        fpu_cs=fpu_ds=0;
      } else {
        fpu_cs=std::uint16_t(cs);fpu_ds=std::uint16_t(ds);
        fpu_opcode=word==4?std::uint16_t((cs>>16)&0x7ffU):0;
      }
      if (registers)
        for (unsigned i=0;i<8;++i) std::copy_n(data.begin()+env+i*10,10,x87[fpu_slot(i)].begin());
      for (unsigned slot=0;slot<8;++slot)
        if (fpu_tag(slot)!=3) fpu_tag_set(slot,classify_ext(x87[slot]));
      fpu_recompute_summary();
      return;
    }
    std::array<std::uint8_t,108> data{};
    if (word==4)
      for (unsigned i:{0U,4U,8U,24U}) { data[i+2]=0xff;data[i+3]=0xff; }
    x87_word(data,0,2,fpu_control);
    x87_word(data,word,2,fpu_status);
    x87_word(data,word*2,2,fpu_tags);
    if (is_real_mode()) {
      const unsigned upper=word==4?0x0ffff000U:0xf000U;
      x87_word(data,word*3,word,unsigned(fpu_ip&0xffffU));
      x87_word(data,word*4,word,(unsigned(fpu_ip>>4)&upper)|fpu_opcode);
      x87_word(data,word*5,word,unsigned(fpu_dp&0xffffU));
      x87_word(data,word*6,word,unsigned(fpu_dp>>4)&upper);
    } else {
      x87_word(data,word*3,word,unsigned(fpu_ip));
      x87_word(data,word*4,word,unsigned(fpu_cs)|(word==4?unsigned(fpu_opcode)<<16:0U));
      x87_word(data,word*5,word,unsigned(fpu_dp));
      x87_word(data,word*6,2,fpu_ds);
    }
    if (registers)
      for (unsigned i=0;i<8;++i) std::copy(x87[fpu_slot(i)].begin(),x87[fpu_slot(i)].end(),data.begin()+env+i*10);
    x87_write_bytes(operand,data.data(),env+(registers?80:0));
    if (registers) fpu_init();
    else { fpu_control|=0x3fU;fpu_recompute_summary(); }
  }

  bool x87_ieee_operand(std::uint64_t raw,unsigned size,Extended80& value,bool allow_denorm=false) {
    const unsigned fraction_bits=size==4?23:52;
    const unsigned exponent_mask=size==4?0xff:0x7ff;
    const auto exponent=(raw>>fraction_bits)&exponent_mask;
    const auto fraction=raw&((std::uint64_t{1}<<fraction_bits)-1);
    if (fraction && !exponent && x87_raise(2U) && !allow_denorm) return false;
    if (exponent==exponent_mask && fraction && !(fraction>>(fraction_bits-1))) {
      if (x87_raise(1U)) return false;
      raw|=std::uint64_t{1}<<(fraction_bits-1);
    }
    value=extended_from_ieee(raw,size);
    return true;
  }
  bool x87_narrow(const Extended80& value,unsigned size,std::uint64_t& result) {
    const unsigned fraction_bits=size==4?23:52;
    const int max_exponent=size==4?127:1023, min_exponent=1-max_exponent;
    const std::uint64_t exp_mask=size==4?0xff:0x7ff;
    const auto infinity=exp_mask<<fraction_bits;
    const auto sig=ext_significand(value);
    const auto encoded=ext_biased_exp(value);
    const bool negative=(value[9]&0x80U)!=0;
    const auto sign=negative?sign_bit(size):0;
    x87_set_c1(false);
    if (x87_unsupported(value)) {
      result=sign_bit(size)|infinity|(1ULL<<(fraction_bits-1));
      return !x87_raise(1U);
    }
    if (encoded==0x7fff) {
      result=sign|infinity;
      if (x87_nan(value)) {
        if (x87_snan(value)&&x87_raise(1U)) return false;
        result|=(1ULL<<(fraction_bits-1))|((sig&0x3fffffffffffffffULL)>>(63-fraction_bits));
      }
      return true;
    }
    if (!sig) { result=sign;return true; }
    auto normalized=sig;
    int exponent=int(encoded?encoded:1)-16383;
    while (!(normalized>>63)) { normalized<<=1;--exponent; }
    const int precision_shift=63-int(fraction_bits);
    const auto regular=x87_round_shift(normalized,precision_shift,negative,x87_rc());
    const auto carry=regular.magnitude>>(fraction_bits+1);
    if (exponent+int(carry)>max_exponent) {
      if (!(fpu_control&8U)) { x87_raise(8U);return false; }
      const bool infinite=x87_rc()==0 || (x87_rc()==1&&negative) || (x87_rc()==2&&!negative);
      result=sign|(infinite?infinity:infinity-1);
      x87_set_c1(infinite);
      x87_raise(8U|32U);
      return true;
    }
    if (exponent+int(carry)<min_exponent) {
      if (!(fpu_control&16U)) { x87_raise(16U);return false; }
      const auto tiny=x87_round_shift(normalized,precision_shift+min_exponent-exponent,negative,x87_rc());
      result=sign|tiny.magnitude;
      x87_set_c1(tiny.incremented);
      if (tiny.inexact) x87_raise(16U|32U);
      return true;
    }
    const auto magnitude=regular.magnitude>>carry;
    result=sign|(std::uint64_t(exponent+int(carry)+max_exponent)<<fraction_bits)|
           (magnitude&((1ULL<<fraction_bits)-1));
    x87_set_c1(regular.incremented);
    if (regular.inexact) x87_raise(32U);
    return true;
  }
  bool x87_store_integer(const Extended80& value,unsigned size,bool truncate,std::uint64_t& result) {
    const bool negative=(value[9]&0x80U)!=0;
    const auto sig=ext_significand(value);
    const auto encoded=ext_biased_exp(value);
    const int exponent=int(encoded?encoded:1)-16383;
    x87_set_c1(false);
    const auto invalid=[&] { result=sign_bit(size);return !x87_raise(1U); };
    if (x87_unsupported(value)||encoded==0x7fff||exponent>63) return invalid();
    const auto rounded=x87_round_shift(sig,63-exponent,negative,truncate?3U:x87_rc());
    const auto limit=sign_bit(size)-!negative;
    if (rounded.magnitude>limit) return invalid();
    result=negative?-rounded.magnitude:rounded.magnitude;
    x87_set_c1(rounded.incremented);
    if (rounded.inexact) x87_raise(32U);
    return true;
  }
  void x87_bcd(const Operand& operand,bool store) {
    if (!store) {
      const auto data=x87_read_bytes(operand,10);
      std::uint64_t magnitude=0;
      for (unsigned i=9;i>0;--i) {
        const auto digits=data[i-1];
        magnitude=magnitude*100U+(digits>>4)*10U+(digits&15U);
      }
      auto value=x87_integer(magnitude,(data[9]&0x80U)!=0);
      value[9]|=data[9]&0x80U; // Preserve negative zero.
      fpu_push(value);
      return;
    }
    Extended80 value{};
    if (!x87_operand(fpu_slot(0),value)) return;
    const bool negative=(value[9]&0x80U)!=0;
    const auto exp=ext_biased_exp(value);
    const int exponent=int(exp?exp:1)-16383;
    const auto rounded=x87_round_shift(ext_significand(value),63-exponent,negative,x87_rc());
    auto magnitude=rounded.magnitude;
    std::array<std::uint8_t,10> data{};
    x87_set_c1(false);
    if (x87_unsupported(value)||exp==0x7fff||exponent>=60||magnitude>999999999999999999ULL) {
      if (x87_raise(1U)) return;
      data[7]=0xc0; data[8]=data[9]=0xff; // packed BCD indefinite
    } else {
      x87_set_c1(rounded.incremented);
      if (rounded.inexact) x87_raise(32U);
      for (unsigned i=0;i<9;++i) {
        const auto digits=magnitude%100U;
        data[i]=std::uint8_t(((digits/10U)<<4)|(digits%10U));
        magnitude/=100U;
      }
      data[9]=negative?0x80:0;
    }
    x87_write_bytes(operand,data.data(),data.size());
    fpu_retire_pop();
  }
  void x87_extract(unsigned top) {
    const unsigned next=(top+7U)&7U;
    if (fpu_tag(next)!=3) {
      fpu_stack_fault(true);
      if (fpu_control&1U) {
        x87_assign(top,x87_indefinite());
        fpu_push(x87_indefinite());
      }
      return;
    }
    Extended80 significand{};
    if (!x87_operand(top,significand)) return;
    auto magnitude=ext_significand(significand);
    const auto exp=ext_biased_exp(significand);
    const bool negative=(significand[9]&0x80U)!=0;
    Extended80 exponent{};
    if (x87_unsupported(significand)) {
      if (x87_raise(1U)) return;
      exponent=significand=x87_indefinite();
    } else if (exp==0x7fff) {
      if (x87_nan(significand)) {
        if (x87_snan(significand)&&x87_raise(1U)) return;
        significand[7]|=0x40;
        exponent=significand;
      } else exponent=x87_bits(1ULL<<63,0x7fff,false);
    } else if (!magnitude) {
      if (x87_raise(4U)) return;
      exponent=x87_bits(1ULL<<63,0x7fff,true);
    } else {
      int power=int(exp?exp:1)-16383;
      if (!exp&&x87_raise(2U)) return;
      while (!(magnitude>>63)) { magnitude<<=1;--power; }
      significand=x87_bits(magnitude,0x3fff,negative);
      exponent=x87_integer(power<0?unsigned(-power):unsigned(power),power<0);
    }
    x87_assign(top,exponent);
    fpu_push(significand);
  }

  void x87_compare(const Extended80& left,const Extended80& right,bool quiet,
                   bool flags,unsigned pops,bool memory_denormal=false) {
    const bool unsupported=x87_unsupported(left)||x87_unsupported(right);
    const bool nan=x87_nan(left)||x87_nan(right);
    const bool unordered=unsupported||nan;
    x87_set_c1(false);
    bool pending=false;
    if (unsupported||(nan&&(!quiet||x87_snan(left)||x87_snan(right)))) {
      pending=x87_raise(1U);
    } else if (!unordered&&(memory_denormal||x87_denormal(left)||x87_denormal(right))) {
      pending=x87_raise(2U);
    }
    auto le=ext_biased_exp(left),re=ext_biased_exp(right);
    const auto l=ext_significand(left),r=ext_significand(right);
    if (!le&&(l>>63)) le=1;
    if (!re&&(r>>63)) re=1;
    const bool lneg=l&&(left[9]&0x80U),rneg=r&&(right[9]&0x80U);
    const bool equal=!unordered&&lneg==rneg&&le==re&&l==r;
    const bool less=!unordered&&!equal&&(lneg!=rneg?lneg:
        (lneg?(le>re||(le==re&&l>r)):(le<re||(le==re&&l<r))));
    if (flags) {
      eflags&=~std::uint32_t(CF|PF|AF|ZF|SF|OF);
      set_flag(CF,unordered||less);set_flag(PF,unordered);set_flag(ZF,unordered||equal);
    } else {
      fpu_status=std::uint16_t((fpu_status&~0x4500U)|
          ((unordered||less)?0x0100U:0U)|(unordered?0x0400U:0U)|((unordered||equal)?0x4000U:0U));
    }
    if (!pending)
      for (unsigned i=0;i<pops;++i) fpu_retire_pop();
  }
  void x87_binary(unsigned group,unsigned slot,const Extended80& left,const Extended80& right,
                  bool pop,bool memory_denormal=false) {
    if (group==2||group==3) x87_compare(left,right,false,false,group==3,memory_denormal);
    else fpu_arithmetic(group,left,right,slot,pop,memory_denormal);
  }

  void x87_constant(unsigned encoding) {
    struct Constant { std::uint64_t floor; unsigned exponent; bool nearest_up; };
    constexpr Constant constants[]{
        {0x8000000000000000ULL,0x3fff,false},  // 1
        {0xd49a784bcd1b8afeULL,0x4000,false},  // log2(10)
        {0xb8aa3b295c17f0bbULL,0x3fff,true},   // log2(e)
        {0xc90fdaa22168c234ULL,0x4000,true},   // pi
        {0x9a209a84fbcff798ULL,0x3ffd,true},   // log10(2)
        {0xb17217f7d1cf79abULL,0x3ffe,true},   // ln(2)
        {0,0,false}
    };
    const auto& value=constants[encoding-0xe8];
    const bool inexact=encoding!=0xe8&&encoding!=0xee;
    const bool increment=inexact&&(x87_rc()==2||(x87_rc()==0&&value.nearest_up));
    fpu_push(x87_bits(value.floor+increment,value.exponent,false));
  }
  void x87_round_to_integer(unsigned top,Extended80 value) {
    x87_set_c1(false);
    if (x87_unsupported(value)) {
      if (!x87_raise(1)) x87_assign(top,x87_indefinite());
      return;
    }
    if (x87_snan(value)) {
      if (!x87_raise(1)) { value[7]|=0x40; x87_assign(top,value); }
      return;
    }
    // FRNDINT also signals a denormal operand for the exp=0/J=1 encoding.
    if (x87_denormal(value)&&x87_raise(2)) return;
    const auto exponent=int(ext_biased_exp(value)?ext_biased_exp(value):1)-16383;
    const bool negative=(value[9]&0x80U)!=0;
    if (exponent>=63||!ext_significand(value)) return;
    const auto rounded=x87_round_shift(ext_significand(value),63-exponent,negative,x87_rc());
    auto result=x87_integer(rounded.magnitude,negative);
    if (!rounded.magnitude&&negative) result[9]=0x80;
    x87_set_c1(rounded.incremented);
    if (rounded.inexact) x87_raise(32U);
    x87_assign(top,result);
  }

  void x87_register(unsigned op,unsigned encoding) {
    const auto group=(encoding>>3)&7U;
    const auto index=encoding&7U, top=fpu_slot(0), slot=fpu_slot(index);
    Extended80 a{},b{};
    if (op==0xd8 || ((op==0xdc||op==0xde)&&(group<2||group>=4))) {
      if (!x87_operand(top,a)||!x87_operand(slot,b)) return;
      if (op==0xd8) x87_binary(group,top,a,b,false);
      else x87_binary(group>=4?group^1U:group,slot,b,a,op==0xde);
      return;
    }
    if ((op==0xda&&encoding==0xe9)||(op==0xde&&encoding==0xd9)) {
      if (x87_operand(top,a)&&x87_operand(fpu_slot(1),b)) x87_compare(a,b,op==0xda,false,2);
      return;
    }
    if ((op==0xdb||op==0xdf)&&(group==5||group==6)) {
      if (x87_operand(top,a)&&x87_operand(slot,b)) x87_compare(a,b,group==5,true,op==0xdf);
      return;
    }
    if ((op==0xda||op==0xdb)&&group<=3) { // FCMOVcc
      const bool underflow=fpu_tag(top)==3||fpu_tag(slot)==3;
      if (underflow) {
        fpu_stack_fault(false);
        if (fpu_control&1U) x87_assign(top,x87_indefinite());
        return;
      }
      const bool condition=group==0?flag(CF):group==1?flag(ZF):group==2?(flag(CF)||flag(ZF)):flag(PF);
      if (condition==(op==0xda)) x87_assign(top,x87[slot]);
      return;
    }
    if (op==0xdf&&group==0) { // FFREEP on processors exposing this alias.
      fpu_tag_set(slot,3);
      fpu_retire_pop();
      return;
    }
    if (op==0xdd) {
      if (group==0) { fpu_tag_set(slot,3);return; }
      if (group==2||group==3) {
        if (!x87_operand(top,a)) return;
        x87_set_c1(false);
        x87_assign(slot,a);
        if (group==3) fpu_retire_pop();
        return;
      }
      if (group==4||group==5) {
        if (x87_operand(top,a)&&x87_operand(slot,b)) x87_compare(a,b,true,false,group==5);
        return;
      }
    }
    if (op==0xd9) {
      if (group==0) {
        if (x87_operand(slot,a)) fpu_push(a);
        return;
      }
      if (group==1) {
        if (!x87_operand(top,a)||!x87_operand(slot,b)) return;
        x87_set_c1(false);
        x87_assign(top,b);x87_assign(slot,a);
        return;
      }
      if (encoding==0xd0) return; // FNOP is waiting despite the spelling.
      if (encoding>=0xe8&&encoding<=0xee) { x87_constant(encoding);return; }
      if (encoding==0xf4) { x87_extract(top);return; }
      if (encoding==0xfd) {
        if (x87_operand(top,a)&&x87_operand(fpu_slot(1),b))
          fpu_arithmetic_result(x87_numeric::scale(a,b,fpu_control),top,false);
        return;
      }
      if (encoding==0xf5||encoding==0xf8) {
        if (!x87_operand(top,a)||!x87_operand(fpu_slot(1),b)) return;
        const auto prior=fpu_status;
        const auto result=x87_numeric::remainder(a,b,encoding==0xf5,fpu_control);
        fpu_arithmetic_result(result.number,top,false);
        fpu_status=std::uint16_t((fpu_status&~0x4700U)|(prior&(0x4700U&~result.condition_mask))|
                                result.condition);
        return;
      }
      if (encoding==0xf6||encoding==0xf7) {
        const auto next=(top+(encoding==0xf6?7U:1U))&7U;
        fpu_status=std::uint16_t((fpu_status&~0x3a00U)|(next<<11));
        return;
      }
      if (encoding==0xe5) { // FXAM classifies the current tag, even if empty.
        const auto& v=x87[top];
        const auto sig=ext_significand(v);
        const auto exp=ext_biased_exp(v);
        unsigned code=0;
        if (fpu_tag(top)==3) code=0x4100U;
        else if (x87_unsupported(v)) code=0;
        else if (x87_nan(v)) code=0x0100U;
        else if (exp==0x7fff) code=0x0500U;
        else if (!sig) code=0x4000U;
        else if (!exp) code=0x4400U;
        else code=0x0400U;
        fpu_status=std::uint16_t((fpu_status&~0x4700U)|code|((v[9]&0x80U)?0x0200U:0U));
        return;
      }
      if (encoding==0xfa) {
        if (!x87_operand(top,a)) return;
        if (fpu_tag(top)==3) x87_assign(top,a);
        else fpu_sqrt();
        return;
      }
      if (encoding==0xe0||encoding==0xe1||encoding==0xe4||encoding==0xfc) {
        if (!x87_operand(top,a)) return;
        if (encoding==0xe4) x87_compare(a,{},false,false,0);
        else if (fpu_tag(top)==3) x87_assign(top,a);
        else if (encoding==0xfc) x87_round_to_integer(top,a);
        else {
          x87_set_c1(false);
          if (encoding==0xe0) a[9]^=0x80;
          else a[9]&=0x7f;
          x87_assign(top,a);
        }
        return;
      }
    }
    fail("x87 register opcode not implemented",FaultCode::UnsupportedOpcode);
  }

  bool x87_memory(unsigned op,const ModRM& m) {
    const auto group=m.group,top=fpu_slot(0);
    if (op==0xd9 && group==5) {
      fpu_control=fpu_normalize_control(std::uint16_t(get(m.rm,2)));
      fpu_recompute_summary();
      return false;
    }
    if (op==0xd9 && group==7) { put(m.rm,2,fpu_control);return false; }
    if (op==0xdd && group==7) { put(m.rm,2,fpu_status);return false; }
    if ((op==0xd9||op==0xdd)&&(group==4||group==6)) {
      x87_environment(m.rm,group==4,op==0xdd);
      return false;
    }

    Extended80 value{},left{};
    if (op==0xd8||op==0xda||op==0xdc||op==0xde) {
      const unsigned size=op==0xde?2:op==0xdc?8:4;
      const auto raw=get(m.rm,size);
      if (!x87_operand(top,left)) return true;
      const bool integer=op==0xda||op==0xde;
      // Widening a memory denormal is exact and normalizes its encoding.
      // Keep its original classification for the combined operation.
      const auto magnitude=raw&~sign_bit(size);
      const bool denormal=!integer&&magnitude&&magnitude<(std::uint64_t{1}<<(size==4?23:52));
      value=integer?x87_integer_operand(raw,size):extended_from_ieee(raw,size);
      x87_binary(group,top,left,value,false,denormal);
      return true;
    }
    if (op==0xdb && group==5) {
      const auto data=x87_read_bytes(m.rm,10);
      std::copy_n(data.begin(),value.size(),value.begin());
      fpu_push(value);
      return true;
    }
    if (op==0xdb && group==7) {
      if (x87_operand(top,value)) {
        x87_write_bytes(m.rm,value.data(),value.size());
        x87_set_c1(false);fpu_retire_pop();
      }
      return true;
    }
    if (op==0xdf&&(group==4||group==6)) {
      x87_bcd(m.rm,group==6);
      return true;
    }
    if ((op==0xd9||op==0xdd)&&(group==0||group==2||group==3)) {
      const unsigned size=op==0xd9?4:8;
      if (group==0) {
        if (x87_ieee_operand(get(m.rm,size),size,value,true)) fpu_push(value);
      } else if (x87_operand(top,value)) {
        std::uint64_t encoded=0;
        if (x87_narrow(value,size,encoded)) {
          put(m.rm,size,encoded);
          if (group==3) fpu_retire_pop();
        }
      }
      return true;
    }
    const bool integer=(op==0xdb||op==0xdd||op==0xdf);
    const bool load=(op!=0xdd&&group==0)||(op==0xdf&&group==5);
    const bool store=(group>=1&&group<=3&&op!=0xdd)||(op==0xdd&&group==1)||(op==0xdf&&group==7);
    if (integer&&(load||store)) {
      const unsigned size=op==0xdb?4:op==0xdd||group>=5?8:2;
      if (load) fpu_push(x87_integer_operand(get(m.rm,size),size));
      else if (x87_operand(top,value)) {
        std::uint64_t encoded=0;
        if (x87_store_integer(value,size,group==1,encoded)) {
          put(m.rm,size,encoded);
          if (group!=2) fpu_retire_pop();
        }
      }
      return true;
    }
    fail("x87 memory opcode not implemented",FaultCode::UnsupportedOpcode);
  }
  void execute_x87(unsigned opcode) {
    require_fpu();
    const auto encoding=fetch8();
    const unsigned group=(encoding>>3)&7U;
    if (encoding>=0xc0) {
      if (opcode==0xdb&&encoding==0xe3) { fpu_init();return; }
      if (opcode==0xdb&&encoding==0xe2) { fpu_status=std::uint16_t(fpu_status&~0x80ffU);return; }
      if (opcode==0xdf&&encoding==0xe0) { set_reg16(EAX,fpu_status);return; }
      require_no_pending_x87();
      x87_register(opcode,encoding);
      x87_record(opcode,encoding);
      return;
    }
    const bool no_wait=(opcode==0xd9||opcode==0xdd)&&(group==6||group==7);
    if (!no_wait) require_no_pending_x87();
    // Let the normal decoder consume effective address and displacement.
    --cursor_;decoded_.pop_back();
    const auto m=modrm();
    require_memory(m.rm);
    if (x87_memory(opcode,m)) x87_record(opcode,encoding,&m.rm);
  }
