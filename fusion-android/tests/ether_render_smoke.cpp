#include "mundo_x/ether_renderer.hpp"
#include <cstdint>
#include <iostream>
#include <stdexcept>

void require(bool good,const char* msg){
  if(!good) throw std::runtime_error(msg);
}
int main(){
  mundo_x::EtherRenderer renderer(256,192,32);
  renderer.set_camera({2.3,1.6,7.2},{0,0,0},47.);
  for(std::uint32_t n=0;n<5;n++){
    mundo_x::EtherRenderer::Body body{};
    body.id=n;
    body.center={(n<2 ? -0.6:0.6)+(n%3)*0.12, (n%2)*0.25, n*0.11};
    body.radius=.24;
    body.color= n%2 ? std::array<double,3>{{.88,.31,.21}}:
                        std::array<double,3>{{.23,.72,.97}};
    renderer.add_body(body);
  }
  const auto direct=renderer.render(true);
  require(direct.exact_mismatches==0,"Ether renderer culling not exact");
  require(direct.bodies==5,"Ether geometry body count");
  require(direct.width==256&&direct.height==192,"frame dimensions");
  bool hasSphere=false;
  for(auto const& p:renderer.frame()){
    if(p.body>=0){hasSphere=true;break;}
  }
  require(hasSphere,"Empty 3D sphere scene");
  const auto hash=direct.frame_hash;
  renderer.set_camera({-3.,1.5,6.0},{0,0,0},47.);
  const auto rotated=renderer.render(true);
  require(rotated.exact_mismatches==0,"Ether rotated view differs from brute oracle");
  require(rotated.frame_hash!=hash,"Camera orbit does not change actual 3D rays");
  std::cout<<"ETHER_3D_FUSION_PASS bodies="<<rotated.bodies
    <<" exact_mismatches="<<rotated.exact_mismatches
    <<" native_pixels="<<renderer.frame().size()<<"\n";
  return 0;
}
