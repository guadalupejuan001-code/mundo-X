// Streaming circuit worker. No saved gates or O(N) reference states.
#include "mundo_x/factorized_core.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/resource.h>
#include <vector>
#include <sched.h>
namespace {
using Clock=std::chrono::steady_clock;using Complex=std::complex<double>;using Wide=std::complex<long double>;
struct Options {
 std::string method="factorized",family="product_ht";std::size_t n=8,max_block=28,block_size=4;
 std::uint64_t seed=20261018,memory=8ULL*1024*1024*1024,payload=4ULL*1024*1024*1024;unsigned cpu=45;
};
struct Fnv {std::uint64_t value=14695981039346656037ULL;void byte(unsigned char b){value^=b;value*=1099511628211ULL;}void word(std::uint64_t x){for(unsigned k=0;k<8;++k)byte((x>>(8*k))&255);}std::string str()const{std::ostringstream s;s<<std::hex<<std::setw(16)<<std::setfill('0')<<value;return s.str();}};
std::string quote(const std::string&s){std::ostringstream out;out<<'"';for(unsigned char c:s){if(c=='"'||c=='\\')out<<'\\'<<c;else if(c=='\n')out<<"\\n";else if(c=='\r')out<<"\\r";else if(c=='\t')out<<"\\t";else if(c<32)out<<"?";else out<<c;}out<<'"';return out.str();}
double ms(Clock::time_point a,Clock::time_point b){return std::chrono::duration<double,std::milli>(b-a).count();}
std::uint64_t rss(){std::ifstream f("/proc/self/status");std::string k;while(f>>k){if(k=="VmHWM:"){std::uint64_t v=0;f>>v;return v*1024;}f.ignore(std::numeric_limits<std::streamsize>::max(),'\n');}return 0;}
void progress(const char*p,double t){std::cerr<<std::setprecision(17)<<"{\"phase\":"<<quote(p)<<",\"milliseconds\":"<<t<<"}"<<std::endl;}
Options parse(int argc,char**argv){Options o;for(int k=1;k<argc;++k){const std::string key=argv[k];if(++k==argc)throw std::invalid_argument("missing value");const std::string v=argv[k];
 if(key=="--method")o.method=v;else if(key=="--family")o.family=v;else if(key=="--qubits")o.n=std::stoull(v);else if(key=="--max-block-qubits")o.max_block=std::stoull(v);else if(key=="--block-size")o.block_size=std::stoull(v);else if(key=="--seed")o.seed=std::stoull(v);else if(key=="--memory-bytes")o.memory=std::stoull(v);else if(key=="--logical-amplitude-bytes")o.payload=std::stoull(v);else if(key=="--cpu-seconds")o.cpu=std::stoul(v);else throw std::invalid_argument("unknown option "+key);}
 if(!o.n||o.n>200000000||!o.max_block||o.max_block>28||!o.block_size||o.block_size>20||!o.memory||o.memory>12ULL*1024*1024*1024||!o.payload||o.payload>8ULL*1024*1024*1024||!o.cpu||o.cpu>60)throw std::invalid_argument("declared ceiling exceeded");
 if(o.family=="bridge_recover"&&o.n!=4)throw std::invalid_argument("bridge needs4 qubits");
 if(o.family=="ghz_blocks"&&o.n%o.block_size)throw std::invalid_argument("whole equal GHZ blocks required");
 if(o.family=="bell_pairs"&&o.n%2)throw std::invalid_argument("Bell pair exams require an even qubit count");
 return o;}
int limits(const Options&o){const rlimit as{static_cast<rlim_t>(o.memory),static_cast<rlim_t>(o.memory)},cpu{static_cast<rlim_t>(o.cpu),static_cast<rlim_t>(o.cpu+1)},core{0,0};if(setrlimit(RLIMIT_AS,&as)||setrlimit(RLIMIT_CPU,&cpu)||setrlimit(RLIMIT_CORE,&core))throw std::runtime_error("limits failed");cpu_set_t allowed,one;CPU_ZERO(&allowed);if(sched_getaffinity(0,sizeof(allowed),&allowed))throw std::runtime_error("affinity read failed");for(int c=0;c<CPU_SETSIZE;++c)if(CPU_ISSET(c,&allowed)){CPU_ZERO(&one);CPU_SET(c,&one);if(sched_setaffinity(0,sizeof(one),&one))throw std::runtime_error("affinity write failed");return c;}throw std::runtime_error("no allowed CPU");}
constexpr const char*PATTERNS[8]={"H","XHT","HS","HTS","X","HTT","XHSS","HXTS"};
std::size_t pattern(const Options&o,std::size_t q){return(q+(o.seed&7U))&7U;}
template<class F>void gates(const Options&o,F emit){
 if(o.family=="product_ht")for(std::size_t q=0;q<o.n;++q){emit('H',q,0);emit('T',q,0);}
 else if(o.family=="heterogeneous_product")for(std::size_t q=0;q<o.n;++q)for(const char*p=PATTERNS[pattern(o,q)];*p;++p)emit(*p,q,0);
 else if(o.family=="bell_pairs")for(std::size_t q=0;q+1<o.n;q+=2){emit('H',q,0);emit('C',q,q+1);}
 else if(o.family=="ghz"||o.family=="ghz_blocks"){const auto width=o.family=="ghz"?o.n:o.block_size;for(std::size_t base=0;base<o.n;base+=width){emit('H',base,0);for(std::size_t j=1;j<width;++j)emit('C',base+j-1,base+j);}}
 else if(o.family=="bridge_recover"){emit('H',0,0);emit('C',0,1);emit('H',2,0);emit('C',2,3);emit('C',1,2);emit('C',1,2);}
 else throw std::invalid_argument("unknown family");}
template<class Q>void apply(Q&q,char op,std::size_t a,std::size_t b){switch(op){case'H':q.hadamard(a);break;case'X':q.pauli_x(a);break;case'T':q.phase_t(a);break;case'S':q.phase_s(a);break;case'C':q.cnot(a,b);break;default:throw std::invalid_argument("unknown gate");}}
const mundo_x::FactorizedCore&core(const mundo_x::FactorizedCore&q){return q;}
std::unique_ptr<mundo_x::FactorizedCore>make_factor(const Options&o){return std::make_unique<mundo_x::FactorizedCore>(o.n,o.max_block,o.payload);}
struct Result{
 std::string status="ok",reason,circuit_hash,hash_a,hash_b,storage="null";
 double hash_ms=0,init_ms=0,evo_ms=0,refactor_ms=0,basis_ms=0,query_hash_ms=0,query_ms=0,validation_ms=0,negative_ms=0,oracle_ms=0;
 std::size_t gate_count=0,executed=0,factors=0,largest=0,amplitudes=0,payload=0;std::array<double,3>p{},z{};
 mundo_x::FactorizedCore::LogAmplitude log_a{true,0,0},log_b{true,0,0};
 double local_error=0,log_error=0,phase_error=0,oracle_error=0,tolerance=0;
 bool legacy=false,scaled=false,negative_ran=false,negative_pass=false,oracle_ran=false,oracle_pass=false,manual_split=false;
};
std::array<Wide,2>reference_pattern(std::size_t k){std::array<Wide,2>s{Wide{1,0},Wide{0,0}};const long double h=1/std::sqrt(2.L);for(const char*p=PATTERNS[k];*p;++p)switch(*p){case'H':{const auto a=s[0],b=s[1];s[0]=(a+b)*h;s[1]=(a-b)*h;break;}case'X':std::swap(s[0],s[1]);break;case'T':s[1]*=Wide{h,h};break;case'S':s[1]*=Wide{0,1};break;}return s;}
bool support_bit(std::size_t k,bool second){return k==4?true:bool((k&1U)^unsigned(second));}
std::string bit_hash(const std::vector<bool>&bits){Fnv h;h.word(bits.size());for(std::size_t base=0;base<bits.size();base+=8){unsigned char b=0;for(std::size_t j=0;j<8&&base+j<bits.size();++j)if(bits[base+j])b|=1U<<j;h.byte(b);}return h.str();}
struct Expected{bool zero=false;long double magnitude=0,phase=0;};
Expected expected(const Options&o,bool second){const long double pi=std::acos(-1.L),ln2=std::log(2.L);Expected e;
 if(o.family=="product_ht"){e.magnitude=-static_cast<long double>(o.n)*ln2/2;e.phase=second?static_cast<long double>(o.n%8)*pi/4:0;}
 else if(o.family=="heterogeneous_product")for(std::size_t k=0;k<8;++k){const auto rel=(k+8-(o.seed&7U))&7U;const auto count=o.n/8+(rel<o.n%8?1:0);const auto a=reference_pattern(k)[support_bit(k,second)];if(a==Wide{}&&count){e.zero=true;return e;}if(count){e.magnitude+=static_cast<long double>(count)*std::log(std::abs(a));e.phase+=static_cast<long double>(count)*std::arg(a);}}
 else if(o.family=="bell_pairs"){e.magnitude=-static_cast<long double>(o.n/2)*ln2/2;e.zero=second&&o.n%2;}
 else if(o.family=="ghz")e.magnitude=-ln2/2;else if(o.family=="ghz_blocks")e.magnitude=-static_cast<long double>(o.n/o.block_size)*ln2/2;else if(o.family=="bridge_recover")e.magnitude=-ln2;
 e.phase=std::remainder(e.phase,2*pi);return e;}
double expected_p(const Options&o,std::size_t q){if(o.family=="heterogeneous_product"){const auto s=reference_pattern(pattern(o,q));return static_cast<double>(std::norm(s[1])/(std::norm(s[0])+std::norm(s[1])));}if(o.family=="bell_pairs"&&o.n%2&&q==o.n-1)return 0;return .5;}
// Independent double statevector for small cases; no DenseCore or stored gates.
class Oracle{public:explicit Oracle(std::size_t n):state(std::size_t{1}<<n,Complex{}){state[0]=1;}void apply(char op,std::size_t a,std::size_t b){const auto bit=std::size_t{1}<<a;
 if(op=='H'||op=='X'){for(std::size_t base=0;base<state.size();base+=bit*2)for(std::size_t j=0;j<bit;++j){auto&x=state[base+j];auto&y=state[base+j+bit];if(op=='X')std::swap(x,y);else{const auto xx=x,yy=y;x=(xx+yy)/std::sqrt(2.);y=(xx-yy)/std::sqrt(2.);}}}
 else if(op=='T'||op=='S'){const auto phase=op=='S'?Complex{0,1}:std::polar(1.,std::acos(-1.)/4);for(std::size_t i=0;i<state.size();++i)if(i&bit)state[i]*=phase;}
 else if(op=='C'){const auto target=std::size_t{1}<<b;for(std::size_t i=0;i<state.size();++i)if((i&bit)&&!(i&target))std::swap(state[i],state[i|target]);}}
 std::vector<Complex>state;};
void queries(Result&r,const mundo_x::FactorizedCore&q,const Options&o){auto start=Clock::now();std::vector<bool>a(o.n,false),b(o.n,true);std::size_t deterministic=o.n;
 if(o.family=="heterogeneous_product")for(std::size_t i=0;i<o.n;++i){const auto k=pattern(o,i);a[i]=support_bit(k,false);b[i]=support_bit(k,true);if(k==4&&deterministic==o.n)deterministic=i;}
 r.basis_ms=ms(start,Clock::now());start=Clock::now();r.hash_a=bit_hash(a);r.hash_b=bit_hash(b);r.query_hash_ms=ms(start,Clock::now());const std::array<std::size_t,3>sites{0,o.n/2,o.n-1};start=Clock::now();
 for(std::size_t j=0;j<3;++j)r.p[j]=q.probability_one(sites[j]);
 for(std::size_t j=0;j<3;++j)r.z[j]=q.expectation_z(sites[j]);
 r.log_a=q.log_amplitude(a);r.log_b=q.log_amplitude(b);r.query_ms=ms(start,Clock::now());
 start=Clock::now();bool valid=true;for(std::size_t j=0;j<3;++j){const auto p=expected_p(o,sites[j]);valid=valid&&std::isfinite(r.p[j])&&std::isfinite(r.z[j]);r.local_error=std::max({r.local_error,std::abs(r.p[j]-p),std::abs(r.z[j]-(1-2*p))});}
 auto compare=[&](const mundo_x::FactorizedCore::LogAmplitude&actual,const Expected&e){if(actual.zero!=e.zero){valid=false;return;}if(e.zero)return;valid=valid&&std::isfinite(actual.log_magnitude)&&std::isfinite(actual.phase);r.log_error=std::max(r.log_error,static_cast<double>(std::abs(static_cast<long double>(actual.log_magnitude)-e.magnitude)));r.phase_error=std::max(r.phase_error,static_cast<double>(std::abs(std::remainder(static_cast<long double>(actual.phase)-e.phase,2*std::acos(-1.L)))));};
 compare(r.log_a,expected(o,false));compare(r.log_b,expected(o,true));
 // Preregistered heuristic criterion, not a proved floating-point error bound.
 r.tolerance=std::min(1e-7,std::max(3e-11,4.*static_cast<double>(o.n)*std::numeric_limits<double>::epsilon()));r.legacy=valid&&r.local_error<=3e-11&&r.log_error<=1e-8&&r.phase_error<=1e-8;r.scaled=valid&&r.local_error<=3e-11&&r.log_error<=r.tolerance&&r.phase_error<=r.tolerance;r.validation_ms=ms(start,Clock::now());
 if(deterministic<o.n){a[deterministic]=false;start=Clock::now();const auto negative=q.log_amplitude(a);r.negative_ms=ms(start,Clock::now());r.negative_ran=true;r.negative_pass=negative.zero;if(!negative.zero)r.scaled=false;}
 if(o.n<=12){
  start=Clock::now();Oracle oracle(o.n);
  gates(o,[&](char op,std::size_t x,std::size_t y){oracle.apply(op,x,y);});
  const auto actual=q.dense_state(12);
  bool finite=actual.size()==oracle.state.size();
  for(std::size_t i=0;i<actual.size()&&i<oracle.state.size();++i){
   const bool entries_finite=std::isfinite(actual[i].real())&&std::isfinite(actual[i].imag())&&
                             std::isfinite(oracle.state[i].real())&&std::isfinite(oracle.state[i].imag());
   finite=finite&&entries_finite;
   if(!entries_finite)continue; // std::max alone would silently ignore NaN.
   const double error=std::abs(actual[i]-oracle.state[i]);
   finite=finite&&std::isfinite(error);
   if(std::isfinite(error))r.oracle_error=std::max(r.oracle_error,error);
  }
  r.oracle_ran=true;r.oracle_pass=finite&&r.oracle_error<=3e-11;
  r.oracle_ms=ms(start,Clock::now());if(!r.oracle_pass)r.scaled=false;
 }
 if(!r.scaled)r.status="query_validation_failed";}
void refactor(Result&r,mundo_x::FactorizedCore&q,const Options&o){if(o.family=="bridge_recover"){auto s=Clock::now();r.manual_split=q.try_split({0,1});r.refactor_ms=ms(s,Clock::now());r.evo_ms+=r.refactor_ms;}}
template<class Q>auto storage_json(const Q&q,int)->decltype(q.storage_estimate(),std::string{}){const auto s=q.storage_estimate();std::ostringstream out;out<<"{\"singleton_capacity_bytes\":"<<s.singleton_capacity_bytes<<",\"active_singleton_amplitude_bytes\":"<<s.active_singleton_amplitude_bytes<<",\"retained_singleton_capacity_bytes\":"<<s.retained_singleton_capacity_bytes<<",\"multiqubit_amplitude_capacity_bytes\":"<<s.multiqubit_amplitude_capacity_bytes<<",\"estimated_metadata_bytes\":"<<s.estimated_metadata_bytes<<",\"estimated_reserved_bytes\":"<<s.estimated_reserved_bytes<<",\"excludes_allocator_and_temporary_memory\":true}";return out.str();}
template<class Q>std::string storage_json(const Q&,long){return "null";}
template<class Factory>Result run(const Options&o,Factory factory){Result r;auto start=Clock::now();Fnv hash;hash.word(o.n);gates(o,[&](char op,std::size_t a,std::size_t b){hash.word(static_cast<unsigned char>(op));hash.word(a);hash.word(b);++r.gate_count;});r.circuit_hash=hash.str();r.hash_ms=ms(start,Clock::now());progress("circuit_hash_prepared",r.hash_ms);
 start=Clock::now();auto owner=factory(o);r.init_ms=ms(start,Clock::now());progress("initialization_complete",r.init_ms);auto&q=*owner;start=Clock::now();gates(o,[&](char op,std::size_t a,std::size_t b){apply(q,op,a,b);++r.executed;});r.evo_ms=ms(start,Clock::now());refactor(r,q,o);progress("evolution_complete",r.evo_ms);
 r.factors=core(q).factor_count();r.largest=core(q).largest_factor_qubits();r.amplitudes=core(q).amplitude_count();r.payload=core(q).logical_amplitude_bytes();r.storage=storage_json(core(q),0);queries(r,core(q),o);progress("queries_complete",r.query_ms);if(o.family=="bridge_recover"&&r.factors!=2){r.status="query_validation_failed";r.reason="bridge did not recover two Bell factors";}return r;}
void log_json(const mundo_x::FactorizedCore::LogAmplitude&a){std::cout<<"{\"zero\":"<<(a.zero?"true":"false")<<",\"log_magnitude\":";if(a.zero)std::cout<<"null";else std::cout<<a.log_magnitude;std::cout<<",\"phase\":";if(a.zero)std::cout<<"null";else std::cout<<a.phase;std::cout<<'}';}
void output(const Options&o,const Result&r,int cpu,double wall){std::cout<<std::setprecision(17)<<"{\"status\":"<<quote(r.status)<<",\"reason\":"<<quote(r.reason)<<",\"method\":"<<quote(o.method)<<",\"family\":"<<quote(o.family)<<",\"qubits\":"<<o.n<<",\"seed\":"<<o.seed
 <<",\"circuit_fnv1a64\":"<<quote(r.circuit_hash)<<",\"query_a_fnv1a64\":"<<quote(r.hash_a)<<",\"query_b_fnv1a64\":"<<quote(r.hash_b)<<",\"gate_count\":"<<r.gate_count<<",\"executed_gate_count\":"<<r.executed
 <<",\"circuit_hash_preparation_ms\":"<<r.hash_ms<<",\"initialization_ms\":"<<r.init_ms<<",\"evolution_ms\":"<<r.evo_ms<<",\"refactorization_ms\":"<<r.refactor_ms<<",\"basis_preparation_ms\":"<<r.basis_ms<<",\"query_hash_preparation_ms\":"<<r.query_hash_ms<<",\"query_ms\":"<<r.query_ms<<",\"analytic_validation_ms\":"<<r.validation_ms<<",\"negative_query_ms\":"<<r.negative_ms<<",\"small_oracle_ms\":"<<r.oracle_ms<<",\"worker_total_ms\":"<<wall
 <<",\"factor_count\":"<<r.factors<<",\"largest_factor_qubits\":"<<r.largest<<",\"amplitude_count\":"<<r.amplitudes<<",\"logical_amplitude_bytes\":"<<r.payload<<",\"storage_estimate\":"<<r.storage<<",\"payload_kind\":\"logical_complex_double_amplitudes_only_not_RAM\",\"peak_rss_bytes\":"<<rss()<<",\"peak_rss_source\":\"linux_proc_self_VmHWM\""
 <<",\"max_block_qubits\":"<<o.max_block<<",\"group_block_qubits\":"<<o.block_size<<",\"logical_amplitude_budget_bytes\":"<<o.payload<<",\"worker_address_space_limit_bytes\":"<<o.memory<<",\"allowed_cpu_count\":1,\"selected_cpu\":"<<cpu
 <<",\"legacy_tolerance\":1e-8,\"legacy_tolerance_pass\":"<<(r.legacy?"true":"false")<<",\"scaled_log_tolerance\":"<<r.tolerance<<",\"scaled_tolerance_pass\":"<<(r.scaled?"true":"false")<<",\"tolerance_model\":\"preregistered_heuristic_min_1e-7_max_3e-11_4N_double_epsilon_not_a_proven_bound\",\"local_tolerance\":3e-11,\"small_oracle_tolerance\":3e-11"
 <<",\"max_local_query_error\":"<<r.local_error<<",\"max_log_magnitude_error\":"<<r.log_error<<",\"max_log_phase_error\":"<<r.phase_error<<",\"small_oracle_ran\":"<<(r.oracle_ran?"true":"false")<<",\"small_oracle_pass\":";if(r.oracle_ran)std::cout<<(r.oracle_pass?"true":"false");else std::cout<<"null";
 std::cout<<",\"max_small_oracle_amplitude_error\":";if(r.oracle_ran)std::cout<<r.oracle_error;else std::cout<<"null";std::cout<<",\"negative_zero_probe_ran\":"<<(r.negative_ran?"true":"false")<<",\"negative_zero_probe_pass\":";if(r.negative_ran)std::cout<<(r.negative_pass?"true":"false");else std::cout<<"null";
 std::cout<<",\"query_sites\":[0,"<<o.n/2<<','<<o.n-1<<"],\"probabilities\":["<<r.p[0]<<','<<r.p[1]<<','<<r.p[2]<<"],\"expectations_z\":["<<r.z[0]<<','<<r.z[1]<<','<<r.z[2]<<"],\"log_basis_amplitudes\":[";log_json(r.log_a);std::cout<<',';log_json(r.log_b);std::cout<<"],\"raw_global_amplitudes_queried\":false,\"query_scope\":\"three_local_probabilities_three_Z_expectations_two_supported_log_basis_amplitudes\",\"circuit_saved\":false,\"evolution_includes_stream_generation\":true,\"manual_split_success\":"<<(r.manual_split?"true":"false")<<"}\n";}
}
int main(int argc,char**argv){const auto begin=Clock::now();try{const auto o=parse(argc,argv);const auto cpu=limits(o);Result r;if(o.method=="factorized")r=run(o,make_factor);else throw std::invalid_argument("unknown method");output(o,r,cpu,ms(begin,Clock::now()));return r.status=="ok"?0:2;}
 catch(const std::bad_alloc&e){std::cout<<"{\"status\":\"allocation_failure\",\"reason\":"<<quote(e.what())<<",\"peak_rss_bytes\":"<<rss()<<"}\n";return 3;}
 catch(const std::length_error&e){std::cout<<"{\"status\":\"logical_budget_rejection\",\"reason\":"<<quote(e.what())<<",\"peak_rss_bytes\":"<<rss()<<"}\n";return 3;}
 catch(const std::invalid_argument&e){std::cout<<"{\"status\":\"invalid_argument\",\"reason\":"<<quote(e.what())<<",\"peak_rss_bytes\":"<<rss()<<"}\n";return 4;}
 catch(const std::exception&e){std::cout<<"{\"status\":\"runtime_failure\",\"reason\":"<<quote(e.what())<<",\"peak_rss_bytes\":"<<rss()<<"}\n";return 4;}}
