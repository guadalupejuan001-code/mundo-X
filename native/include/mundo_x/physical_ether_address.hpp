#pragma once

#include "mundo_x/quantum_runtime.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace mundo_x {

// An operational physical address is NOT a memory pointer, a physical qubit,
// or evidence for a discrete universal lattice. It specifies what is asked,
// relative to which calibrated frame, and with what nominal resolution.
struct PhysicalAddress {
  enum class Observable { MagneticVector, MagneticMagnitude };
  std::string frame;
  double x_m = 0., y_m = 0., z_m = 0.;
  double time_s = 0.;
  double resolution_m = .01;
  Observable observable = Observable::MagneticVector;
};

struct PhysicalVector {
  double x = 0., y = 0., z = 0.;
};

struct PhysicalObservation {
  PhysicalAddress address;
  // Magnetometer vectors MUST be transformed to the common frame beforehand.
  // A device-frame B reading taken while rotating the watch is NOT sufficient.
  PhysicalVector magnetic_uT;
};

// A physically falsifiable, rule-first *decoder* over the existing Ether
// QuantumRuntime. All raw observations live in the REAL InverseMemory
// COW-256 byte store, not a parallel vector of authoritative samples.
//
// Calibrated hypothesis: magnet center at (0,0,0) in the named frame, and a
// static external field B(r) = B0 + [3 r (m.r)/r^5 - m/r^3].
// m is the dipole moment including the physical mu0/4pi conversion, expressed
// in microtesla * m^3. This 6-parameter model is not a universal law of space:
// the caller must test held-out measurements before relying on it.
//
// The virtual spatial field is NEVER voxelized. Inference works on <=128
// observed sites and predictions are evaluated on demand in O(1).
class PhysicalEtherDecoder {
 public:
  static constexpr unsigned kMaxSamples = 128;
  static constexpr double kMinimumRadiusM = .05;
  static constexpr std::uint64_t kBaseAddress = UINT64_C(9000000000000000000);
  static constexpr unsigned kHeaderBytes = 64, kRecordBytes = 64;

  struct Model {
    bool ready = false;
    PhysicalVector background_uT, effective_moment_uT_m3;
    double fit_rmse_uT = 0., uniform_baseline_rmse_uT = 0.;
    std::array<double, 3> min_xyz{}, max_xyz{};
    double min_time_s = 0., max_time_s = 0.;
    unsigned calibration_samples = 0;
    std::size_t ether_overrides = 0;
  };

  struct Prediction {
    PhysicalVector magnetic_uT;
    double magnitude_uT = 0., training_rmse_uT = 0.;
    unsigned calibration_samples = 0;
  };

  struct HoldoutScore {
    double model_rmse_uT = 0.;
    double uniform_rmse_uT = 0.;
    unsigned count = 0;
    bool improves_over_uniform = false;
  };

  explicit PhysicalEtherDecoder(std::string frame, double resolution_m = .01)
      : frame_(std::move(frame)), resolution_m_(resolution_m) {
    validate_frame(frame_);
    if (!finite(resolution_m_) || resolution_m_ < .001 ||
        resolution_m_ > 1.)
      throw std::invalid_argument("invalid physical frame resolution");
    store_new_header();
  }

  const std::string& frame() const noexcept { return frame_; }
  double resolution_m() const noexcept { return resolution_m_; }
  unsigned observations() const { return sample_count(); }
  const Model& model() const noexcept { return model_; }
  std::size_t ether_overrides() const noexcept {
    return runtime_.memory().stats().overrides;
  }
  std::size_t quantum_active_sites() const noexcept {
    return runtime_.core().stats().active_qubits;
  }
  std::string core_snapshot() const { return runtime_.core_snapshot(); }
  std::string memory_snapshot() const { return runtime_.memory_snapshot(); }

  // Complete observation is committed by ONE QuantumRuntime::execute batch.
  // Bad samples, out-of-budget storage or parse errors leave Ether unchanged.
  // This session owns its own namespace, avoiding ambiguous shared addresses.
  void observe(const PhysicalObservation& observation) {
    validate_observation(observation);
    const unsigned n = sample_count();
    if (n == kMaxSamples)
      throw std::length_error("physical observation budget exceeded");
    std::ostringstream script;
    const auto base = sample_base(n);
    const std::array<double, 7> data{{
        observation.address.x_m, observation.address.y_m,
        observation.address.z_m, observation.address.time_s,
        observation.magnetic_uT.x, observation.magnetic_uT.y,
        observation.magnetic_uT.z
    }};
    for (unsigned f=0; f<data.size(); ++f) {
      const auto bits = encode_double(data[f]);
      for (unsigned b=0; b<8; ++b) {
        const unsigned byte = static_cast<unsigned>((bits >> (8*b)) & 255U);
        script << "mem_store " << (base + f*8 + b)
               << " " << byte << '\n';
      }
    }
    script << "mem_store " << (base + 56) << " 165\n";
    script << "mem_store " << (kBaseAddress + 56) << " " << ((n+1)&255U) << '\n';
    script << "mem_store " << (kBaseAddress + 57) << " " << ((n+1)>>8U) << '\n';
    runtime_.execute(script.str());
    model_ = Model{};
  }

  // Decode authoritative raw samples directly from Ether Memory. No
  // heavyweight all-space copy or implicit physical/qubit measurement.
  std::vector<PhysicalObservation> observed_samples() const {
    const unsigned count=sample_count();
    std::vector<PhysicalObservation> out;
    out.reserve(count);
    for (unsigned i=0; i<count; ++i) out.push_back(load(i));
    return out;
  }

  // A numerical inverse problem, not a magical lookup into unknown reality.
  // At least eight distinct positions are needed; rank-deficient geometry
  // fails closed instead of inventing unique hidden physical sources.
  const Model& infer() {
    const auto readings=observed_samples();
    if(readings.size()<8)
      throw std::logic_error("need at least eight independent measurement positions");
    std::array<std::array<double,6>,6> gram{};
    std::array<double,6> rhs{};
    PhysicalVector mean{};
    Model candidate;
    candidate.calibration_samples=static_cast<unsigned>(readings.size());
    candidate.min_xyz={{std::numeric_limits<double>::infinity(),
                        std::numeric_limits<double>::infinity(),
                        std::numeric_limits<double>::infinity()}};
    candidate.max_xyz={{-std::numeric_limits<double>::infinity(),
                        -std::numeric_limits<double>::infinity(),
                        -std::numeric_limits<double>::infinity()}};
    candidate.min_time_s=std::numeric_limits<double>::infinity();
    candidate.max_time_s=-std::numeric_limits<double>::infinity();

    for(const auto& observation:readings) {
      const PhysicalVector r{observation.address.x_m,observation.address.y_m,
                             observation.address.z_m};
      const auto rows=basis(r);
      const double b[3]={observation.magnetic_uT.x,observation.magnetic_uT.y,
                         observation.magnetic_uT.z};
      const double pos[3]={r.x,r.y,r.z};
      mean.x+=b[0];mean.y+=b[1];mean.z+=b[2];
      for(unsigned j=0;j<3;++j) {
        candidate.min_xyz[j]=std::min(candidate.min_xyz[j],pos[j]);
        candidate.max_xyz[j]=std::max(candidate.max_xyz[j],pos[j]);
      }
      candidate.min_time_s=std::min(candidate.min_time_s,observation.address.time_s);
      candidate.max_time_s=std::max(candidate.max_time_s,observation.address.time_s);
      for(unsigned row=0;row<3;++row) {
        for(unsigned a=0;a<6;++a) {
          rhs[a]+=rows[row][a]*b[row];
          for(unsigned c=0;c<6;++c)
            gram[a][c]+=rows[row][a]*rows[row][c];
        }
      }
    }

    // Normalize the six columns so the field basis and background have
    // comparable numeric scales, then solve with pivoting and a rank guard.
    double scale[6]{};
    for(unsigned i=0;i<6;++i) {
      scale[i]=std::sqrt(gram[i][i]);
      if(!finite(scale[i]) || scale[i]<1e-12)
        throw std::runtime_error("physical sampling geometry is rank deficient");
    }
    double augmented[6][7]{};
    for(unsigned i=0;i<6;++i) {
      for(unsigned j=0;j<6;++j)
        augmented[i][j]=gram[i][j]/(scale[i]*scale[j]);
      augmented[i][6]=rhs[i]/scale[i];
    }
    for(unsigned pivot=0;pivot<6;++pivot) {
      unsigned best=pivot;
      for(unsigned row=pivot+1;row<6;++row)
        if(std::abs(augmented[row][pivot])>std::abs(augmented[best][pivot]))
          best=row;
      if(std::abs(augmented[best][pivot])<1e-10)
        throw std::runtime_error("measurements cannot identify a magnetic dipole");
      if(best!=pivot)
        for(unsigned j=0;j<7;++j)
          std::swap(augmented[best][j],augmented[pivot][j]);
      for(unsigned row=pivot+1;row<6;++row) {
        const double factor=augmented[row][pivot]/augmented[pivot][pivot];
        for(unsigned j=pivot;j<7;++j)
          augmented[row][j]-=factor*augmented[pivot][j];
      }
    }
    double parameter[6]{};
    for(int i=5;i>=0;--i) {
      double n=augmented[i][6];
      for(unsigned j=static_cast<unsigned>(i)+1;j<6;++j)
        n-=augmented[i][j]*(parameter[j]*scale[j]);
      parameter[i]=n/augmented[i][i]/scale[i];
      if(!finite(parameter[i]))
        throw std::runtime_error("numerical inverse field overflow");
    }
    candidate.background_uT={parameter[0],parameter[1],parameter[2]};
    candidate.effective_moment_uT_m3={parameter[3],parameter[4],parameter[5]};
    mean={mean.x/readings.size(),mean.y/readings.size(),mean.z/readings.size()};
    double residual=0.,null_residual=0.;
    for(const auto& observation:readings) {
      const PhysicalVector r{observation.address.x_m,observation.address.y_m,
                             observation.address.z_m};
      const auto calculated=field(r,candidate.background_uT,
                                  candidate.effective_moment_uT_m3);
      residual+=squared_difference(calculated,observation.magnetic_uT);
      null_residual+=squared_difference(mean,observation.magnetic_uT);
    }
    candidate.fit_rmse_uT=std::sqrt(residual/(3.*readings.size()));
    candidate.uniform_baseline_rmse_uT=std::sqrt(null_residual/(3.*readings.size()));
    candidate.ether_overrides=ether_overrides();
    candidate.ready=true;
    model_=candidate;
    return model_;
  }

  Prediction predict(const PhysicalAddress& address) const {
    if(!model_.ready)throw std::logic_error("call infer before physical address query");
    validate_address(address);
    if(address.frame!=frame_)
      throw std::invalid_argument("physical frame mismatch");
    if(address.resolution_m+1e-12<resolution_m_)
      throw std::invalid_argument("requested resolution exceeds measured evidence");
    if(address.time_s<model_.min_time_s-15. ||
       address.time_s>model_.max_time_s+15.)
      throw std::out_of_range("static field was not calibrated at this time");
    const double r[3]={address.x_m,address.y_m,address.z_m};
    for(unsigned i=0;i<3;++i) {
      const double span=model_.max_xyz[i]-model_.min_xyz[i];
      const double margin=std::max(.025,.1*span);
      if(r[i]<model_.min_xyz[i]-margin || r[i]>model_.max_xyz[i]+margin)
        throw std::out_of_range("requested physical site is outside calibration");
    }
    const auto v=field({r[0],r[1],r[2]},model_.background_uT,
                       model_.effective_moment_uT_m3);
    return {v,std::sqrt(norm2(v)),model_.fit_rmse_uT,model_.calibration_samples};
  }

  // Holdouts MUST be entered only here, never in the training Ether memory.
  // The returned result is a falsifiable comparison, not a declaration that
  // a magnetic dipole actually generated the observed physical field.
  HoldoutScore evaluate(const std::vector<PhysicalObservation>& holdouts) const {
    if(holdouts.empty())throw std::invalid_argument("empty holdout set");
    double model_error=0.,baseline_error=0.;
    if(!model_.ready)throw std::logic_error("infer before holdout evaluation");
    for(const auto& sample:holdouts) {
      validate_observation(sample);
      const auto guessed=predict(sample.address);
      model_error+=squared_difference(guessed.magnetic_uT,sample.magnetic_uT);
      baseline_error+=squared_difference(model_.background_uT,sample.magnetic_uT);
    }
    HoldoutScore score;
    score.count=static_cast<unsigned>(holdouts.size());
    score.model_rmse_uT=std::sqrt(model_error/(3.*holdouts.size()));
    score.uniform_rmse_uT=std::sqrt(baseline_error/(3.*holdouts.size()));
    score.improves_over_uniform=score.model_rmse_uT < .8*score.uniform_rmse_uT;
    return score;
  }

  // Full exact Ether state is sufficient for reconstruction; no model
  // coefficients are serialized separately. Restore is transactional.
  void restore(const std::string& core_saved,const std::string& memory_saved) {
    QuantumRuntime candidate;
    candidate.restore(core_saved,memory_saved);
    validate_header(candidate);
    // Validate every persisted observation before publishing anything.
    const unsigned count=sample_count(candidate);
    for(unsigned i=0;i<count;++i)
      (void)load(i,candidate);
    runtime_=std::move(candidate);
    model_=Model{};
  }

 private:
  QuantumRuntime runtime_;             // sole owner of the observational bytes
  const std::string frame_;
  const double resolution_m_;
  Model model_;                         // invalidatable derived inference

  static bool finite(double value) {return std::isfinite(value);}
  static double norm2(PhysicalVector a) {
    return a.x*a.x+a.y*a.y+a.z*a.z;
  }
  static double squared_difference(PhysicalVector a,PhysicalVector b) {
    return norm2({a.x-b.x,a.y-b.y,a.z-b.z});
  }
  static std::uint64_t sample_base(unsigned index) {
    return kBaseAddress+kHeaderBytes+kRecordBytes*index;
  }
  static std::uint64_t encode_double(double value) {
    static_assert(sizeof(double)==sizeof(std::uint64_t),
                  "Physical Ether requires binary64");
    static_assert(std::numeric_limits<double>::is_iec559,
                  "Physical Ether requires IEEE-754");
    std::uint64_t bits=0;
    std::memcpy(&bits,&value,sizeof(bits));
    return bits;
  }
  static double decode_double(std::uint64_t bits) {
    double output=0.;
    std::memcpy(&output,&bits,sizeof(output));
    return output;
  }
  static void validate_frame(const std::string& frame) {
    if(frame.empty()||frame.size()>32)
      throw std::invalid_argument("frame label must have 1..32 chars");
    for(const char c:frame)
      if(!((c>='0'&&c<='9')||(c>='A'&&c<='Z')||
           (c>='a'&&c<='z')||c=='_'||c=='-'))
        throw std::invalid_argument("invalid physical reference frame label");
  }
  void validate_address(const PhysicalAddress& a) const {
    validate_frame(a.frame);
    if(!finite(a.x_m)||!finite(a.y_m)||!finite(a.z_m)||
       !finite(a.time_s)||!finite(a.resolution_m)||
       a.resolution_m<.001||a.resolution_m>1. ||
       a.time_s<0.||a.time_s>1e10)
      throw std::invalid_argument("invalid physical address");
    if(std::max({std::abs(a.x_m),std::abs(a.y_m),std::abs(a.z_m)})>6.)
      throw std::out_of_range("site beyond local six-meter study boundary");
    if(norm2({a.x_m,a.y_m,a.z_m}) < kMinimumRadiusM*kMinimumRadiusM)
      throw std::out_of_range("inside dipole source model singularity");
  }
  void validate_observation(const PhysicalObservation& sample) const {
    validate_address(sample.address);
    if(sample.address.frame!=frame_)
      throw std::invalid_argument("observed physical frame mismatch");
    if(std::abs(sample.address.resolution_m-resolution_m_)>1e-12)
      throw std::invalid_argument("observation resolution does not match calibration");
    const auto b=sample.magnetic_uT;
    if(!finite(b.x)||!finite(b.y)||!finite(b.z)||
       std::max({std::abs(b.x),std::abs(b.y),std::abs(b.z)})>200000.)
      throw std::invalid_argument("magnetic field must be finite and in microtesla");
  }
  std::uint8_t read_byte(const QuantumRuntime& rt,std::uint64_t address) const {
    const auto found=rt.memory().read(std::to_string(address),rt.core());
    if(!found.deterministic)
      throw std::runtime_error("physical observations require deterministic bytes");
    return found.value;
  }
  std::uint64_t read_bits(const QuantumRuntime& rt,std::uint64_t address) const {
    std::uint64_t value=0;
    for(unsigned b=0;b<8;++b)
      value|=std::uint64_t{read_byte(rt,address+b)} << (8*b);
    return value;
  }
  unsigned sample_count(const QuantumRuntime& rt) const {
    const unsigned count=read_byte(rt,kBaseAddress+56)|
                         (unsigned(read_byte(rt,kBaseAddress+57))<<8);
    if(count>kMaxSamples)
      throw std::runtime_error("physical Ether sample count exceeds budget");
    return count;
  }
  unsigned sample_count() const {return sample_count(runtime_);}
  void store_new_header() {
    std::ostringstream script;
    const auto write=[&](std::uint64_t a,unsigned value){
      script << "mem_store " << a << " " << value << '\n';
    };
    write(kBaseAddress+0,'P');write(kBaseAddress+1,'X');
    write(kBaseAddress+2,1);
    write(kBaseAddress+3,static_cast<unsigned>(frame_.size()));
    for(unsigned i=0;i<frame_.size();++i)
      write(kBaseAddress+4+i,static_cast<unsigned>(frame_[i]));
    const auto resolution=encode_double(resolution_m_);
    for(unsigned b=0;b<8;++b)
      write(kBaseAddress+40+b,static_cast<unsigned>((resolution>>(8*b))&255U));
    runtime_.execute(script.str());
  }
  void validate_header(const QuantumRuntime& rt) const {
    if(read_byte(rt,kBaseAddress)!='P'||read_byte(rt,kBaseAddress+1)!='X'||
       read_byte(rt,kBaseAddress+2)!=1)
      throw std::invalid_argument("snapshot is not a physical Ether address session");
    const unsigned n=read_byte(rt,kBaseAddress+3);
    if(n!=frame_.size())throw std::invalid_argument("snapshot physical frame differs");
    for(unsigned i=0;i<n;++i)
      if(read_byte(rt,kBaseAddress+4+i)!=static_cast<unsigned char>(frame_[i]))
        throw std::invalid_argument("snapshot physical frame differs");
    const double resolution=decode_double(read_bits(rt,kBaseAddress+40));
    if(!finite(resolution)||resolution!=resolution_m_)
      throw std::invalid_argument("snapshot physical resolution differs");
  }
  PhysicalObservation load(unsigned index,const QuantumRuntime& rt) const {
    if(index>=sample_count(rt))
      throw std::out_of_range("physical observation index out of range");
    const auto base=sample_base(index);
    if(read_byte(rt,base+56)!=165)
      throw std::runtime_error("physical sample marker was not committed");
    PhysicalObservation sample;
    sample.address.frame=frame_;
    sample.address.resolution_m=resolution_m_;
    sample.address.x_m=decode_double(read_bits(rt,base+0));
    sample.address.y_m=decode_double(read_bits(rt,base+8));
    sample.address.z_m=decode_double(read_bits(rt,base+16));
    sample.address.time_s=decode_double(read_bits(rt,base+24));
    sample.magnetic_uT.x=decode_double(read_bits(rt,base+32));
    sample.magnetic_uT.y=decode_double(read_bits(rt,base+40));
    sample.magnetic_uT.z=decode_double(read_bits(rt,base+48));
    validate_observation(sample);
    return sample;
  }
  PhysicalObservation load(unsigned index) const {return load(index,runtime_);}

  static std::array<std::array<double,6>,3> basis(PhysicalVector r) {
    const double rr=norm2(r);
    if(rr<kMinimumRadiusM*kMinimumRadiusM)
      throw std::out_of_range("inside dipole source singularity");
    const double r3=rr*std::sqrt(rr),r5=r3*rr;
    const double v[3]={r.x,r.y,r.z};
    std::array<std::array<double,6>,3> a{};
    for(unsigned i=0;i<3;++i) {
      a[i][i]=1.;
      for(unsigned j=0;j<3;++j)
        a[i][3+j]=3.*v[i]*v[j]/r5-(i==j ? 1./r3:0.);
    }
    return a;
  }
  static PhysicalVector field(PhysicalVector r,PhysicalVector background,
                              PhysicalVector moment) {
    const auto a=basis(r);
    const double beta[6]={background.x,background.y,background.z,
                          moment.x,moment.y,moment.z};
    double b[3]{};
    for(unsigned i=0;i<3;++i)
      for(unsigned j=0;j<6;++j)b[i]+=a[i][j]*beta[j];
    if(!finite(b[0])||!finite(b[1])||!finite(b[2]))
      throw std::runtime_error("physical field evaluation overflow");
    return {b[0],b[1],b[2]};
  }
};

}  // namespace mundo_x
