#pragma once

#include "mundo_x/compact_core.hpp"
#include "mundo_x/pauli_observer.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace mundo_x {

// Multiword counterpart of the existing reduced Pauli-coset representation.
// It keeps one shared CompactCore stabilizer state and sparse Pauli-difference
// coefficients. Unlike ReducedExactTRepresentation, Pauli labels are not
// limited to one uint64_t; 1000 qubits use 16 words per X/Z mask.
//
// "Exact" here has the same meaning as the existing reduced engine: no branch
// pruning or approximate coefficient cutoff. Coefficients use complex<double>.
class WideReducedExactTRepresentation {
 public:
  struct Key {
    std::vector<std::uint64_t> x, z;
    bool operator==(const Key& other) const { return x==other.x && z==other.z; }
  };
  struct KeyHash {
    std::size_t operator()(const Key& key) const {
      std::uint64_t h=0x9E3779B185EBCA87ULL;
      auto mix=[&](std::uint64_t v){
        h^=v+0xC2B2AE3D27D4EB4FULL+(h<<6U)+(h>>2U);
        h=(h^(h>>27U))*0x94D049BB133111EBULL;
      };
      for(auto v:key.x)mix(v);
      mix(0xA0761D6478BD642FULL);
      for(auto v:key.z)mix(v);
      return static_cast<std::size_t>(h^(h>>32U));
    }
  };

  explicit WideReducedExactTRepresentation(
      std::size_t qubits,
      std::size_t max_components=65536)
      : n_(validated(qubits)), words_((n_+63)/64),
        max_components_(positive(max_components)), base_(n_) {
    components_.emplace(zero_key(),std::complex<double>{1.,0.});
    basis_=make_basis(base_);
  }

  WideReducedExactTRepresentation(
      const CompactCore& stabilizer_state,
      std::size_t max_components=65536)
      : n_(validated(stabilizer_state.qubits())), words_((n_+63)/64),
        max_components_(positive(max_components)), base_(stabilizer_state) {
    components_.emplace(zero_key(),std::complex<double>{1.,0.});
    basis_=make_basis(base_);
  }

  std::size_t qubits() const { return n_; }
  std::size_t words() const { return words_; }
  std::size_t component_count() const { return components_.size(); }
  std::size_t t_count() const { return t_count_; }
  std::size_t base_tableau_bytes() const { return base_.state_bytes(); }

  std::size_t logical_payload_bytes() const {
    const std::size_t key_bytes=2*words_*sizeof(std::uint64_t);
    return base_.state_bytes()+components_.size()*
        (key_bytes+sizeof(std::complex<double>));
  }

  void hadamard(std::size_t q){ apply_clifford(Op::H,q,0); }
  void phase_s(std::size_t q){ apply_clifford(Op::S,q,0); }
  void pauli_x(std::size_t q){ apply_clifford(Op::X,q,0); }
  void pauli_z(std::size_t q){ apply_clifford(Op::Z,q,0); }
  void cnot(std::size_t c,std::size_t t){
    check_pair(c,t);apply_clifford(Op::CX,c,t);
  }
  void controlled_z(std::size_t a,std::size_t b){
    check_pair(a,b);apply_clifford(Op::CZ,a,b);
  }

  void phase_t(std::size_t q){
    apply_phase(q,pi()/4.);++t_count_;
  }
  void phase_t_dagger(std::size_t q){
    apply_phase(q,-pi()/4.);++t_count_;
  }
  void phase(std::size_t q,double theta){ apply_phase(q,theta); }

  // diag(1,1,1,e^{i theta}) using exact phase/CNOT identities.
  void controlled_phase(std::size_t c,std::size_t t,double theta){
    check_pair(c,t);
    finite(theta);
    apply_phase(c,theta*.5);
    apply_phase(t,theta*.5);
    apply_clifford(Op::CX,c,t);
    apply_phase(t,-theta*.5);
    apply_clifford(Op::CX,c,t);
  }

  double coefficient_norm_squared() const {
    double total=0.,correction=0.;
    for(const auto& entry:components_){
      const double term=std::norm(entry.second)-correction;
      const double next=total+term;
      correction=(next-total)-term;
      total=next;
    }
    return total;
  }

  std::complex<double> expectation_raw(const std::string& text) const {
    const auto observable=parse_pauli(text);
    std::complex<double> total{0.,0.};
    for(const auto& entry:components_){
      Pauli p{entry.first.x,entry.first.z,0};
      const auto acted=canonicalize(multiply(observable,p),basis_);
      Key key{acted.x,acted.z};
      const auto found=components_.find(key);
      if(found!=components_.end())
        total+=std::conj(found->second)*phase_unit(acted.phase)*entry.second;
    }
    return total;
  }

  double expectation(const std::string& text) const {
    const double norm=coefficient_norm_squared();
    if(!(norm>0.))throw std::runtime_error("zero state norm");
    const auto value=expectation_raw(text)/norm;
    if(std::abs(value.imag())>1e-10)
      throw std::runtime_error("Hermitian Pauli expectation became complex");
    return value.real();
  }

  double pauli_expectation(char axis,std::size_t q) const {
    check(q);
    std::string text(n_,'I');text[q]=axis;
    if(axis!='X'&&axis!='Y'&&axis!='Z')
      throw std::invalid_argument("axis must be X, Y or Z");
    return expectation(text);
  }

 private:
  enum class Op { H,S,X,Z,CX,CZ };
  struct Pauli {
    std::vector<std::uint64_t> x,z;
    std::uint8_t phase=0;
  };
  struct Basis {
    std::vector<Pauli> rows;
    std::vector<std::size_t> pivots;
  };
  using ComponentMap=std::unordered_map<Key,std::complex<double>,KeyHash>;

  static std::size_t validated(std::size_t n){
    if(n==0||n>1000)throw std::invalid_argument("wide reduced supports 1..1000 qubits");
    return n;
  }
  static std::size_t positive(std::size_t n){
    if(n==0)throw std::invalid_argument("max_components must be positive");
    return n;
  }
  static double pi(){return std::acos(-1.);}
  static void finite(double x){
    if(!std::isfinite(x))throw std::invalid_argument("non-finite phase");
  }
  void check(std::size_t q) const {
    if(q>=n_)throw std::out_of_range("qubit");
  }
  void check_pair(std::size_t a,std::size_t b) const {
    check(a);check(b);if(a==b)throw std::invalid_argument("same control and target");
  }
  Key zero_key() const {
    return {std::vector<std::uint64_t>(words_),std::vector<std::uint64_t>(words_)};
  }
  Pauli zero_pauli() const {
    return {std::vector<std::uint64_t>(words_),std::vector<std::uint64_t>(words_),0};
  }

  static unsigned parity64(std::uint64_t x){
#if defined(__GNUG__) || defined(__clang__)
    return static_cast<unsigned>(__builtin_parityll(x));
#else
    unsigned p=0;while(x){p^=1U;x&=x-1;}return p;
#endif
  }
  unsigned dot_parity(const std::vector<std::uint64_t>& a,
                      const std::vector<std::uint64_t>& b) const {
    unsigned p=0;
    for(std::size_t i=0;i<words_;++i)p^=parity64(a[i]&b[i]);
    return p&1U;
  }
  static std::complex<double> phase_unit(std::uint8_t phase){
    switch(phase&3U){
      case 0:return {1.,0.};case 1:return {0.,1.};
      case 2:return {-1.,0.};default:return {0.,-1.};
    }
  }
  bool get(const std::vector<std::uint64_t>& a,std::size_t q) const {
    return ((a[q/64]>>(q%64))&1ULL)!=0;
  }
  void toggle(std::vector<std::uint64_t>& a,std::size_t q) const {
    a[q/64]^=std::uint64_t{1}<<(q%64);
  }
  void set(std::vector<std::uint64_t>& a,std::size_t q) const {
    a[q/64]|=std::uint64_t{1}<<(q%64);
  }
  bool bit_at(const Pauli& p,std::size_t col) const {
    return col<n_?get(p.x,col):get(p.z,col-n_);
  }

  Pauli multiply(const Pauli& a,const Pauli& b) const {
    Pauli out=zero_pauli();
    for(std::size_t i=0;i<words_;++i){
      out.x[i]=a.x[i]^b.x[i];
      out.z[i]=a.z[i]^b.z[i];
    }
    out.phase=static_cast<std::uint8_t>(
        (a.phase+b.phase+2U*dot_parity(a.z,b.x))&3U);
    return out;
  }

  Pauli pauli_z_op(std::size_t q) const {
    auto p=zero_pauli();set(p.z,q);return p;
  }

  Pauli parse_generator(const std::string& g) const {
    if(g.size()!=n_+1)throw std::runtime_error("unexpected stabilizer generator length");
    Pauli p=zero_pauli();
    if(g[0]=='-')p.phase=2;
    else if(g[0]!='+')throw std::runtime_error("invalid generator sign");
    for(std::size_t q=0;q<n_;++q){
      switch(g[q+1]){
        case 'I':break;
        case 'X':set(p.x,q);break;
        case 'Z':set(p.z,q);break;
        case 'Y':set(p.x,q);set(p.z,q);p.phase=(p.phase+1U)&3U;break;
        default:throw std::runtime_error("invalid generator Pauli");
      }
    }
    return p;
  }

  Pauli parse_pauli(const std::string& text) const {
    if(text.size()!=n_)throw std::invalid_argument("Pauli length does not match register");
    Pauli p=zero_pauli();
    for(std::size_t q=0;q<n_;++q){
      switch(text[q]){
        case 'I':break;
        case 'X':set(p.x,q);break;
        case 'Z':set(p.z,q);break;
        case 'Y':set(p.x,q);set(p.z,q);p.phase=(p.phase+1U)&3U;break;
        default:throw std::invalid_argument("Pauli requires I/X/Y/Z");
      }
    }
    return p;
  }

  Basis make_basis(const CompactCore& state) const {
    Basis basis;basis.rows.reserve(n_);basis.pivots.reserve(n_);
    for(std::size_t r=0;r<n_;++r)
      basis.rows.push_back(parse_generator(state.generator_string(r)));
    std::size_t rank=0;
    for(std::size_t col=0;col<2*n_&&rank<n_;++col){
      std::size_t pivot=rank;
      while(pivot<basis.rows.size()&&!bit_at(basis.rows[pivot],col))++pivot;
      if(pivot==basis.rows.size())continue;
      std::swap(basis.rows[rank],basis.rows[pivot]);
      for(std::size_t r=0;r<basis.rows.size();++r)
        if(r!=rank&&bit_at(basis.rows[r],col))
          basis.rows[r]=multiply(basis.rows[r],basis.rows[rank]);
      basis.pivots.push_back(col);++rank;
    }
    if(rank!=n_)throw std::runtime_error("stabilizer basis lost rank");
    basis.rows.resize(rank);
    return basis;
  }

  Pauli canonicalize(Pauli p,const Basis& basis) const {
    for(std::size_t r=0;r<basis.pivots.size();++r)
      if(bit_at(p,basis.pivots[r]))p=multiply(p,basis.rows[r]);
    return p;
  }

  void accumulate_canonical(ComponentMap& map,Pauli p,
                            std::complex<double> coefficient) const {
    coefficient*=phase_unit(p.phase);p.phase=0;
    Key key{std::move(p.x),std::move(p.z)};
    auto it=map.find(key);
    if(it==map.end()){
      if(map.size()>=max_components_)
        throw std::length_error("wide reduced component budget exceeded");
      map.emplace(std::move(key),coefficient);
    }else it->second+=coefficient;
  }
  void accumulate(ComponentMap& map,Pauli p,std::complex<double> coefficient,
                  const Basis& basis) const {
    accumulate_canonical(map,canonicalize(std::move(p),basis),coefficient);
  }

  Pauli conjugate(Pauli p,Op op,std::size_t a,std::size_t b) const {
    const bool xa=get(p.x,a),za=get(p.z,a);
    switch(op){
      case Op::H:
        if(xa&&za)p.phase+=2;
        if(xa!=za){toggle(p.x,a);toggle(p.z,a);}
        break;
      case Op::S:
        if(xa){++p.phase;toggle(p.z,a);}
        break;
      case Op::X:if(za)p.phase+=2;break;
      case Op::Z:if(xa)p.phase+=2;break;
      case Op::CX:
        if(xa)toggle(p.x,b);
        if(get(p.z,b))toggle(p.z,a);
        break;
      case Op::CZ:{
        const bool xb=get(p.x,b);
        if(xa&&xb)p.phase+=2;
        if(xa)toggle(p.z,b);
        if(xb)toggle(p.z,a);
        break;
      }
    }
    p.phase&=3U;return p;
  }

  static void update_base(CompactCore& state,Op op,std::size_t a,std::size_t b){
    switch(op){
      case Op::H:state.hadamard(a);break;
      case Op::S:state.phase_s(a);break;
      case Op::X:state.pauli_x(a);break;
      case Op::Z:state.pauli_z(a);break;
      case Op::CX:state.cnot(a,b);break;
      case Op::CZ:state.controlled_z(a,b);break;
    }
  }

  void apply_clifford(Op op,std::size_t a,std::size_t b){
    check(a);if(op==Op::CX||op==Op::CZ)check_pair(a,b);
    CompactCore next_base=base_;
    update_base(next_base,op,a,b);
    Basis next_basis=make_basis(next_base);
    ComponentMap next;next.reserve(components_.size());
    for(const auto& entry:components_){
      Pauli p{entry.first.x,entry.first.z,0};
      accumulate(next,conjugate(std::move(p),op,a,b),entry.second,next_basis);
    }
    base_=std::move(next_base);basis_=std::move(next_basis);components_=std::move(next);
  }

  void apply_phase(std::size_t q,double theta){
    check(q);finite(theta);
    const std::complex<double> e{std::cos(theta),std::sin(theta)};
    const auto alpha=(std::complex<double>{1.,0.}+e)*.5;
    const auto beta=(std::complex<double>{1.,0.}-e)*.5;
    const Pauli zq=pauli_z_op(q);
    ComponentMap next;
    const std::size_t wanted=components_.size()>max_components_/2?
        max_components_:std::min(max_components_,components_.size()*2+1);
    next.reserve(wanted);
    for(const auto& entry:components_){
      Pauli p{entry.first.x,entry.first.z,0};
      accumulate(next,p,entry.second*alpha,basis_);
      accumulate(next,multiply(zq,p),entry.second*beta,basis_);
    }
    components_=std::move(next);
  }

  std::size_t n_,words_,max_components_,t_count_=0;
  CompactCore base_;
  Basis basis_;
  ComponentMap components_;
};

// User-facing 1000-qubit core. It stays in CompactCore for Clifford work and
// promotes lazily to the multiword reduced representation on the first
// non-Clifford phase.
class MilUniversalCore {
 public:
  explicit MilUniversalCore(std::size_t qubits=1000,
                            std::size_t max_components=65536)
      : n_(qubits),max_components_(max_components),compact_(qubits) {
    if(qubits==0||qubits>1000)throw std::invalid_argument("MIL Core supports 1..1000 qubits");
  }
  std::size_t qubits() const{return n_;}
  bool promoted() const{return static_cast<bool>(wide_);}
  std::size_t component_count() const{return wide_?wide_->component_count():1;}
  std::size_t logical_payload_bytes() const{
    return wide_?wide_->logical_payload_bytes():compact_.state_bytes();
  }

  void hadamard(std::size_t q){if(wide_)wide_->hadamard(q);else compact_.hadamard(q);}
  void phase_s(std::size_t q){if(wide_)wide_->phase_s(q);else compact_.phase_s(q);}
  void pauli_x(std::size_t q){if(wide_)wide_->pauli_x(q);else compact_.pauli_x(q);}
  void pauli_z(std::size_t q){if(wide_)wide_->pauli_z(q);else compact_.pauli_z(q);}
  void cnot(std::size_t a,std::size_t b){if(wide_)wide_->cnot(a,b);else compact_.cnot(a,b);}
  void controlled_z(std::size_t a,std::size_t b){if(wide_)wide_->controlled_z(a,b);else compact_.controlled_z(a,b);}

  void phase_t(std::size_t q){promote();wide_->phase_t(q);}
  void phase_t_dagger(std::size_t q){promote();wide_->phase_t_dagger(q);}
  void phase(std::size_t q,double theta){promote();wide_->phase(q,theta);}
  void controlled_phase(std::size_t a,std::size_t b,double theta){
    promote();wide_->controlled_phase(a,b,theta);
  }

  double expectation(const std::string& pauli) const {
    if(wide_)return wide_->expectation(pauli);
    auto parsed=mundo_x_bridge::Pauli::parse(pauli,n_);
    return mundo_x_bridge::PauliObserver(compact_).expectation(std::move(parsed));
  }

 private:
  void promote(){
    if(!wide_)wide_=std::make_unique<WideReducedExactTRepresentation>(compact_,max_components_);
  }
  std::size_t n_,max_components_;
  CompactCore compact_;
  std::unique_ptr<WideReducedExactTRepresentation> wide_;
};

} // namespace mundo_x
