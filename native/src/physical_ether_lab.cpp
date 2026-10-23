// Ether Physical Address: ingest actual field measurements (or a clearly
// labeled synthetic test CSV), reserve holdouts, infer an operational field
// and evaluate predictions without materializing a spatial lattice.
#include "mundo_x/physical_ether_address.hpp"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Decoder=mundo_x::PhysicalEtherDecoder;
using Observation=mundo_x::PhysicalObservation;
using Address=mundo_x::PhysicalAddress;

struct CSV {
  std::string frame;
  double resolution=0.;
  std::vector<Observation> train,holdout;
};

std::string trim(std::string s) {
  const auto start=s.find_first_not_of(" \t\r\n");
  if(start==std::string::npos)return "";
  const auto end=s.find_last_not_of(" \t\r\n");
  return s.substr(start,end-start+1);
}
std::vector<std::string> columns(const std::string& line) {
  std::vector<std::string> result;
  std::istringstream input(line);
  std::string field;
  while(std::getline(input,field,','))result.push_back(trim(field));
  return result;
}
double number(const std::string& field) {
  std::size_t end=0;
  const double value=std::stod(field,&end);
  if(end!=field.size() || !std::isfinite(value))
    throw std::invalid_argument("invalid finite number: "+field);
  return value;
}
CSV load_csv(const std::string& path) {
  std::ifstream input(path);
  if(!input)throw std::runtime_error("could not read measurement CSV");
  CSV csv;
  std::string line;
  std::size_t line_number=0;
  bool header=false;
  while(std::getline(input,line)) {
    ++line_number;
    const auto stripped=trim(line);
    if(stripped.empty()||stripped[0]=='#')continue;
    const auto fields=columns(stripped);
    if(!header) {
      const std::vector<std::string> expected{
        "role","frame","x_m","y_m","z_m","time_s","resolution_m",
        "bx_uT","by_uT","bz_uT"};
      if(fields!=expected)
        throw std::invalid_argument("invalid CSV heading; expected role,frame,x_m,y_m,z_m,time_s,resolution_m,bx_uT,by_uT,bz_uT");
      header=true;
      continue;
    }
    if(fields.size()!=10)
      throw std::invalid_argument("measurement has wrong number of columns at line "+std::to_string(line_number));
    const std::string role=fields[0],frame=fields[1];
    if(csv.frame.empty()) {
      csv.frame=frame;
      csv.resolution=number(fields[6]);
    } else if(frame!=csv.frame || number(fields[6])!=csv.resolution) {
      throw std::invalid_argument("mixed reference frames or resolution in measurement CSV");
    }
    Observation record{
      {frame,number(fields[2]),number(fields[3]),number(fields[4]),
       number(fields[5]),number(fields[6]),Address::Observable::MagneticVector},
      {number(fields[7]),number(fields[8]),number(fields[9])}
    };
    if(role=="TRAIN")csv.train.push_back(record);
    else if(role=="HOLDOUT")csv.holdout.push_back(record);
    else throw std::invalid_argument("role must be TRAIN or HOLDOUT");
    if(csv.train.size()>Decoder::kMaxSamples || csv.holdout.size()>Decoder::kMaxSamples)
      throw std::length_error("CSV exceeds physical Ether measurement budget");
  }
  if(!header)throw std::invalid_argument("missing CSV heading");
  if(csv.train.size()<8 || csv.holdout.size()<4)
    throw std::invalid_argument("need at least 8 TRAIN and 4 HOLDOUT rows");
  return csv;
}
std::string quote(const std::string& s) {
  std::string out="\"";
  for(const auto c:s) {
    if(c=='"'||c=='\\')out+='\\';
    if(c=='\n'||c=='\r')out+=' ';
    else out+=c;
  }
  return out+"\"";
}
}

int main(int argc,char** argv) {
  if(argc!=2 && argc!=6) {
    std::cerr<<"Usage: physical_ether_lab measures.csv [query_x query_y query_z query_time]\n"
             <<"Do not include HOLDOUT measurements in TRAIN.\n";
    return 2;
  }
  try {
    const auto csv=load_csv(argv[1]);
    Decoder ether(csv.frame,csv.resolution);
    for(const auto& sample:csv.train)ether.observe(sample);
    const auto model=ether.infer();
    const auto score=ether.evaluate(csv.holdout);
    std::cout<<std::fixed<<std::setprecision(8);
    std::cout<<"{\"model\":\"uniform-plus-single-magnetic-dipole\""
             <<",\"claimed_universal_lattice\":false"
             <<",\"dataset\":"
             <<quote(argv[1])
             <<",\"frame\":"<<quote(csv.frame)
             <<",\"train\":"<<csv.train.size()
             <<",\"holdout\":"<<csv.holdout.size()
             <<",\"fit_rmse_uT\":"<<model.fit_rmse_uT
             <<",\"holdout_rmse_uT\":"<<score.model_rmse_uT
             <<",\"uniform_null_rmse_uT\":"<<score.uniform_rmse_uT
             <<",\"passes_predeclared_20_percent_gain\":"
             <<(score.improves_over_uniform?"true":"false")
             <<",\"background_uT\":["
             <<model.background_uT.x<<","<<model.background_uT.y<<","
             <<model.background_uT.z<<"]"
             <<",\"effective_dipole_uT_m3\":["
             <<model.effective_moment_uT_m3.x<<","
             <<model.effective_moment_uT_m3.y<<","
             <<model.effective_moment_uT_m3.z<<"]"
             <<",\"ether_memory_overrides\":"<<ether.ether_overrides()
             <<",\"physical_sensor_validation_required\":true";
    if(argc==6) {
      const Address query{csv.frame,number(argv[2]),number(argv[3]),number(argv[4]),
                          number(argv[5]),csv.resolution,Address::Observable::MagneticVector};
      const auto p=ether.predict(query);
      std::cout<<",\"query_prediction_uT\":["<<p.magnetic_uT.x<<","
               <<p.magnetic_uT.y<<","<<p.magnetic_uT.z<<"]"
               <<",\"query_magnitude_uT\":"<<p.magnitude_uT;
    }
    std::cout<<"}\n";
    return 0;
  } catch(const std::exception& error) {
    std::cerr<<"Ether Physical Address error: "<<error.what()<<'\n';
    return 1;
  }
}
