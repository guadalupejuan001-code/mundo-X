#include "mundo_x/physical_ether_address.hpp"

#include <cmath>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Decoder=mundo_x::PhysicalEtherDecoder;
using Address=mundo_x::PhysicalAddress;
using Observation=mundo_x::PhysicalObservation;
using Vec3=mundo_x::PhysicalVector;

unsigned checks=0;
void check(bool ok,const std::string& message) {
  ++checks;
  if(!ok)throw std::runtime_error(message);
}
void near(double value,double expected,double tolerance,
          const std::string& message) {
  ++checks;
  if(!std::isfinite(value)||std::abs(value-expected)>tolerance)
    throw std::runtime_error(message+": found "+std::to_string(value)+
                             ", expected "+std::to_string(expected));
}
template<class Fn>
void rejected(Fn&& fn,const std::string& reason) {
  bool threw=false;
  try {fn();} catch(const std::exception&) {threw=true;}
  check(threw,reason);
}

// This is an INDEPENDENT forward equation, NOT a call to the decoder's
// private prediction routine. A 3D ideal point dipole + constant Earth field
// is a hypothesis, not an unknown experimental ground truth.
Vec3 ground_truth(Vec3 p,Vec3 background,Vec3 moment) {
  const double d=p.x*p.x+p.y*p.y+p.z*p.z;
  const double distance=std::sqrt(d);
  const double r3=d*distance,r5=r3*d;
  const double scalar=p.x*moment.x+p.y*moment.y+p.z*moment.z;
  return {
    background.x+3*p.x*scalar/r5-moment.x/r3,
    background.y+3*p.y*scalar/r5-moment.y/r3,
    background.z+3*p.z*scalar/r5-moment.z/r3
  };
}
Observation sample(std::string frame,Vec3 pos,double time,
                   Vec3 background,Vec3 moment,unsigned seed,bool noisy) {
  auto b=ground_truth(pos,background,moment);
  if(noisy) {
    b.x+=.027*std::sin(seed*1.2+.5);
    b.y+=.032*std::cos(seed*.8+1.);
    b.z+=.025*std::sin(seed*1.9+.25);
  }
  return {{std::move(frame),pos.x,pos.y,pos.z,time,.01,
           Address::Observable::MagneticVector},b};
}

void run() {
  const Vec3 background{38.0,-11.0,22.0};
  const Vec3 moment{.125,-.064,.088};
  const Vec3 changed_magnet{-.04,.12,-.07};
  Decoder ether("bench_magnet",.01);
  check(ether.observations()==0,"new Ether physical session should be empty");
  check(ether.quantum_active_sites()==0,
        "physical address must not allocate simulated qubits");
  rejected([&]{ether.infer();},"no model without physical observations");

  unsigned seed=0;
  // 16 calibration sites: heterogeneous 3D angles/radii.
  // The held-out sites are strictly INSIDE their spatial envelope.
  for(double z:{.17,.32}) {
    for(double x:{-.26,-.12,.12,.26}) {
      for(double y:{-.23,.23}) {
        const unsigned id=seed++;
        ether.observe(sample("bench_magnet",{x,y,z},1.+id*.16,
                             background,moment,id,true));
      }
    }
  }
  check(ether.observations()==16,"observations were not stored in Ether");
  check(ether.quantum_active_sites()==0,
        "classical magnetometer records accidentally materialized qubits");
  const auto exact_data=ether.observed_samples();
  check(exact_data.size()==16,"Ether exact byte retrieval failed");
  near(exact_data.front().address.x_m,-.26,0.,
       "Ether IEEE754 stored position must be bit-identical");
  near(exact_data.front().magnetic_uT.x,
       sample("bench_magnet", {-.26,-.23,.17},1.,
              background,moment,0,true).magnetic_uT.x,0.,
       "Ether IEEE754 stored field must be bit-identical");

  const auto model=ether.infer();
  check(model.ready && model.calibration_samples==16,
        "rule-first magnetic reconstruction did not complete");
  near(model.background_uT.x,background.x,.08,"background Bx");
  near(model.background_uT.y,background.y,.08,"background By");
  near(model.background_uT.z,background.z,.08,"background Bz");
  near(model.effective_moment_uT_m3.x,moment.x,.001,"dipole mx");
  near(model.effective_moment_uT_m3.y,moment.y,.001,"dipole my");
  near(model.effective_moment_uT_m3.z,moment.z,.001,"dipole mz");
  check(model.fit_rmse_uT<.05,"training magnetic reconstruction should be precise");
  check(model.ether_overrides==ether.ether_overrides(),
        "model must reference genuine Ether memory");

  std::vector<Observation> holdouts;
  std::vector<Observation> different_source;
  for(double z:{.20,.29})
    for(double x:{-.18,.18})
      for(double y:{-.11,.11}) {
        holdouts.push_back(sample("bench_magnet",{x,y,z},2.0,
                        background,moment,seed++,true));
        different_source.push_back(sample("bench_magnet",{x,y,z},2.0,
                        background,changed_magnet,seed++,true));
      }

  auto score=ether.evaluate(holdouts);
  check(score.count==8 && score.model_rmse_uT<.1,
        "held-out dipole predictions are not accurate");
  check(score.uniform_rmse_uT>3.0 && score.improves_over_uniform,
        "dipole inference must outperform a uniform-field null model");
  const auto mismatch=ether.evaluate(different_source);
  check(mismatch.model_rmse_uT>score.model_rmse_uT*5,
        "a changed physical source must invalidate the previous prediction");

  const auto core_before=ether.core_snapshot();
  const auto memory_before=ether.memory_snapshot();
  const auto override_before=ether.ether_overrides();
  // Copying training sites, even with new timestamps or sub-resolution
  // position jitter, must never masquerade as independent spatial evidence.
  auto leaked=holdouts;
  leaked.front()=exact_data.front();
  leaked.front().address.time_s+=1.;
  rejected([&]{ether.evaluate(leaked);},"TRAIN site accepted as a holdout");
  leaked.front().address.x_m+=.002;
  rejected([&]{ether.evaluate(leaked);},"sub-resolution TRAIN overlap accepted");
  auto repeated=holdouts;
  repeated.back()=repeated.front();
  rejected([&]{ether.evaluate(repeated);},"duplicate holdout site accepted");
  repeated.back().address.y_m+=.002;
  rejected([&]{ether.evaluate(repeated);},"sub-resolution holdout repeat accepted");
  rejected([&]{ether.evaluate({holdouts.front()});},"one holdout treated as validation");
  rejected([&]{ether.evaluate(std::vector<Observation>(129,holdouts.front()));},
           "unbounded holdout set accepted");
  auto separated=holdouts;
  separated.front()=exact_data.front();
  separated.front().address.x_m+=.01;
  check(ether.evaluate(separated).count==8,
        "exactly one declared resolution was rejected");
  check(ether.core_snapshot()==core_before && ether.memory_snapshot()==memory_before,
        "holdout validation mutated authoritative samples");
  // One thousand distinct requests, without allocating a 3D lattice.
  for(unsigned i=0;i<1000;++i) {
    const double x=-.20+.40*(i%100)/99.;
    const double y=-.19+.38*(i/100)/9.;
    const auto prediction=ether.predict(
       {"bench_magnet",x,y,.245,2.,.01,
        Address::Observable::MagneticVector});
    check(prediction.calibration_samples==16,
          "read-only physical site query changed the calibration source");
    check(std::isfinite(prediction.magnitude_uT),"invalid predicted field");
  }
  check(ether.ether_overrides()==override_before,
        "physical queries materialized grid state in Ether memory");
  check(ether.core_snapshot()==core_before &&
        ether.memory_snapshot()==memory_before,
        "physical read-only predictions mutated Ether state");
  check(ether.quantum_active_sites()==0,
        "physical field queries created unnecessary quantum components");
  rejected([&]{
    (void)ether.predict({"bench_magnet",4.,0.,0.,2.,.01,
                        Address::Observable::MagneticVector});
  },"prediction must reject uncalibrated spatial extrapolation");
  rejected([&]{
    (void)ether.predict({"another_frame",.1,.0,.2,2.,.01,
                        Address::Observable::MagneticVector});
  },"frame mismatch must be rejected");
  rejected([&]{
    (void)ether.predict({"bench_magnet",.1,.0,.2,100.,.01,
                        Address::Observable::MagneticVector});
  },"static temporal model must reject uncalibrated future");

  // A rejected reading NEVER partially changes any Ether memory page.
  auto wrong=holdouts.front();
  wrong.magnetic_uT.x=std::numeric_limits<double>::quiet_NaN();
  rejected([&]{ether.observe(wrong);},"NaN magnetic reading accepted");
  check(ether.core_snapshot()==core_before &&
        ether.memory_snapshot()==memory_before,
        "invalid reading modified the authoritative physical observations");
  wrong=holdouts.front();
  wrong.address.frame="other_origin";
  rejected([&]{ether.observe(wrong);},"mismatched reference frame accepted");
  check(ether.observations()==16,"invalid observation changed Ether count");

  // Snapshots reconstruct the model only from canonical Ether COW records.
  Decoder restored("bench_magnet",.01);
  restored.restore(core_before,memory_before);
  check(restored.observations()==16,"restore lost observations");
  check(restored.memory_snapshot()==memory_before,"restore lost exact IEEE754 bytes");
  const auto replay_model=restored.infer();
  near(replay_model.effective_moment_uT_m3.x,model.effective_moment_uT_m3.x,
       1e-12,"model must reconstruct from Ether memory only");
  const auto replay=restored.evaluate(holdouts);
  near(replay.model_rmse_uT,score.model_rmse_uT,1e-12,
       "independently restored predictions differ");

  Decoder wrong_frame("other_origin",.01);
  const auto wrong_before=wrong_frame.memory_snapshot();
  rejected([&]{wrong_frame.restore(core_before,memory_before);},
           "cross-frame snapshot restore was accepted");
  check(wrong_frame.memory_snapshot()==wrong_before,
        "failed restore corrupted receiving Ether runtime");

  // Two radii provide six independent equations, so the numeric rank guard
  // alone used to accept eight rows copied from just two physical positions.
  Decoder two_sites("two_sites");
  for(unsigned i=0;i<8;++i)
    two_sites.observe(sample("two_sites",{i%2?.4:.2,0.,0.},1.+.1*i,
                             background,moment,i,false));
  rejected([&]{two_sites.infer();},"two sites passed the eight-position protocol");
  check(!two_sites.model().ready,"rejected fit published a usable model");

  Decoder jittered("jittered");
  for(unsigned i=0;i<8;++i)
    jittered.observe(sample("jittered",{i%2?.4:.2,.0001*i,0.},1.,
                            background,moment,i,false));
  rejected([&]{jittered.infer();},"position jitter bypassed the distinct-site guard");

  // Distinct positions can still be rank deficient: a fixed-radius circle
  // in z=0 cannot separate the background Bz from the dipole's mz.
  Decoder degenerate("degenerate");
  for(Vec3 p:std::vector<Vec3>{{.25,0.,0.},{.15,.2,0.},{0.,.25,0.},
      {-.15,.2,0.},{-.25,0.,0.},{-.15,-.2,0.},{0.,-.25,0.},{.15,-.2,0.}})
    degenerate.observe(sample("degenerate",p,1.,background,moment,0,false));
  rejected([&]{degenerate.infer();},
           "rank-deficient geometry was treated as a discovered field");

  // Null experiment: a uniform field must not be called dipole evidence.
  Decoder control("null_trial");
  const Vec3 no_moment{};
  for(double z:{.17,.32})
    for(double x:{-.26,-.12,.12,.26})
      for(double y:{-.23,.23})
        control.observe(sample("null_trial",{x,y,z},1.,
                               background,no_moment,0,false));
  control.infer();
  std::vector<Observation> null_holdouts;
  for(double x:{-.1,.1})
    for(double y:{-.1,.1})
      null_holdouts.push_back(sample("null_trial",{x,y,.22},1.,
                                     background,no_moment,0,false));
  const auto null_score=control.evaluate(null_holdouts);
  check(!null_score.improves_over_uniform,
        "false positive: uniform-field control claimed an anomalous dipole");

  std::cout<<std::fixed<<std::setprecision(6)
    << "{\"suite\":\"ether_physical_address\",\"checks\":"<<checks
    <<",\"training_sites\":"<<model.calibration_samples
    <<",\"holdout_sites\":"<<score.count
    <<",\"holdout_rmse_uT\":"<<score.model_rmse_uT
    <<",\"uniform_holdout_rmse_uT\":"<<score.uniform_rmse_uT
    <<",\"changed_source_rmse_uT\":"<<mismatch.model_rmse_uT
    <<",\"training_rmse_uT\":"<<model.fit_rmse_uT
    <<",\"spatial_queries_without_allocation\":1000"
    <<",\"ether_overrides\":"<<ether.ether_overrides()
    <<",\"quantum_active_sites\":"<<ether.quantum_active_sites()
    <<",\"synthetic_validation\":true"
    <<",\"lattice_physical_evidence\":false"
    <<",\"ok\":true}\n";
}
}  // namespace

int main() {
  try {run();return 0;}
  catch(const std::exception& e) {
    std::cerr<<"FAIL Physical Ether decoder: "<<e.what()<<'\n';
    return 1;
  }
}
