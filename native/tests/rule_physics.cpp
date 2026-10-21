#include "mundo_x/rule_physics.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <type_traits>

using Physics=mundo_x::RulePhysics;
using Body=Physics::Body;
using Vec=Physics::Vec3;
static std::size_t checks=0;
static void require(bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(message);}
static void near(double a,double b,double e,const char* message){require(std::isfinite(a)&&std::abs(a-b)<=e,message);}
template<class F>static void rejects(F action){bool failed=false;try{action();}catch(const std::exception&){failed=true;}require(failed,"expected failure");}
static Vec sub(Vec a,const Vec& b){return a-=b;}
static Vec mul(Vec a,double s){return a*=s;}
static double dot(const Vec& a,const Vec& b){return a.x*b.x+a.y*b.y+a.z*b.z;}
static void equal(const Body& a,const Body& b) {
  const double aa[]={a.position.x,a.position.y,a.position.z,a.velocity.x,a.velocity.y,a.velocity.z,a.inverse_mass,a.radius,a.restitution};
  const double bb[]={b.position.x,b.position.y,b.position.z,b.velocity.x,b.velocity.y,b.velocity.z,b.inverse_mass,b.radius,b.restitution};
  for(unsigned k=0;k<9;++k)require(aa[k]==bb[k],"body result differs from ordered reference");
}

// The original PR21 equations, with every pair visited. This independent
// traversal has no bounding boxes, index, candidate list or incremental refit.
static void reference_step(std::vector<Body>& bodies,const Physics::Rules& rules,double dt) {
  for(auto& b:bodies)if(b.inverse_mass>0){b.velocity+=mul(rules.uniform_acceleration,dt);b.position+=mul(b.velocity,dt);}
  for(std::size_t i=0;i<bodies.size();++i)for(std::size_t j=i+1;j<bodies.size();++j){
    auto& a=bodies[i];auto& b=bodies[j];
    const Vec delta=sub(b.position,a.position);
    const double minimum=a.radius+b.radius,d2=dot(delta,delta);
    if(d2>=minimum*minimum)continue;
    const double distance=std::sqrt(std::max(d2,1e-24));
    const Vec normal=distance>1e-12?mul(delta,1./distance):Vec{1,0,0};
    const double inv_sum=a.inverse_mass+b.inverse_mass;if(inv_sum==0)continue;
    const Vec correction=mul(normal,(minimum-distance)/inv_sum);
    if(a.inverse_mass>0)a.position-=mul(correction,a.inverse_mass);
    if(b.inverse_mass>0)b.position+=mul(correction,b.inverse_mass);
    const double closing=dot(sub(b.velocity,a.velocity),normal);
    if(closing<0){
      const double impulse=-(1.+std::min(a.restitution,b.restitution))*closing/inv_sum;
      if(a.inverse_mass>0)a.velocity-=mul(normal,impulse*a.inverse_mass);
      if(b.inverse_mass>0)b.velocity+=mul(normal,impulse*b.inverse_mass);
    }
  }
}
int main() {
  static_assert(std::is_const<typename std::remove_reference<decltype(std::declval<Physics&>().bodies())>::type>::value,
                "unvalidated mutable body exposure");
  try {
    Physics::Rules rules;rules.max_substep=.01;
    Physics falling(rules);Body falling_body;falling_body.radius=.1;falling.add(falling_body);
    falling.step(1.);
    near(falling.bodies()[0].velocity.y,-9.80665,1e-10,"uniform acceleration velocity");
    near(falling.bodies()[0].position.y,-9.80665*.01*.01*5050.,1e-9,"uniform acceleration position");
    rules.uniform_acceleration={0,0,0};rules.max_substep=1.;
    Physics elastic(rules);Body a,b;a.position={-.49,0,0};b.position={.49,0,0};
    a.velocity={1,0,0};b.velocity={-1,0,0};a.restitution=b.restitution=1;
    elastic.add(a);elastic.add(b);elastic.step(.01);
    near(elastic.bodies()[0].velocity.x,-1,1e-12,"elastic momentum a");
    near(elastic.bodies()[1].velocity.x,1,1e-12,"elastic momentum b");
    require(elastic.step_stats().contacts==1,"elastic contact count");

    rules.pair_gravity=1;rules.max_substep=.001;
    Physics gravity(rules);a={};b={};a.position={-1,0,0};b.position={1,0,0};a.radius=b.radius=.01;
    gravity.add(a);gravity.add(b);gravity.step(.01);
    near(gravity.bodies()[0].velocity.x,-gravity.bodies()[1].velocity.x,1e-12,"pair gravity momentum");
    require(gravity.bodies()[0].velocity.x>0,"pair gravity direction");
    require(gravity.step_stats().gravity_pairs==10,"pair gravity work accounted");

    rules.pair_gravity=0;rules.max_substep=.01;rules.uniform_acceleration={0,-10,0};
    Physics fixed(rules);a={};a.inverse_mass=0;a.position={1,2,3};fixed.add(a);fixed.step(1.);
    equal(fixed.bodies()[0],a);
    const auto before=fixed.bodies();const auto revision=fixed.revision();
    rejects([&]{fixed.step(std::numeric_limits<double>::max());});
    require(fixed.revision()==revision,"invalid duration changed revision");equal(fixed.bodies()[0],before[0]);
    a.position.x=std::numeric_limits<double>::quiet_NaN();
    rejects([&]{fixed.set_body(0,a);});equal(fixed.bodies()[0],before[0]);
    auto invalid=rules;invalid.softening=std::numeric_limits<double>::denorm_min();rejects([&]{fixed.set_rules(invalid);});
    invalid=rules;invalid.max_substeps=0;rejects([&]{Physics bad(invalid);});

    rules.uniform_acceleration={0,0,0};rules.max_pair_tests=1;
    Physics bounded(rules);a={};a.velocity={1,0,0};
    for(int k=0;k<3;++k){a.position={double(k)*3,0,0};bounded.add(a);}
    const auto saved=bounded.bodies();const auto epoch=bounded.revision();
    rejects([&]{bounded.step(.01);});require(bounded.revision()==epoch,"pair-budget failure changed revision");
    for(std::size_t i=0;i<saved.size();++i)equal(bounded.bodies()[i],saved[i]);
    rules.max_pair_tests=8000000;bounded.set_rules(rules);bounded.step(.01);
    near(bounded.bodies()[0].position.x,.01,0,"recovery after pair budget failure");

    Physics overflow(rules);a={};a.velocity={1,0,0};overflow.add(a);a.velocity={1e308,0,0};overflow.add(a);
    const auto overflow_before=overflow.bodies();rejects([&]{overflow.step(.01);});
    for(std::size_t i=0;i<overflow_before.size();++i)equal(overflow.bodies()[i],overflow_before[i]);

    std::mt19937_64 random(42);
    unsigned sparse_scenes=0,dense_scenes=0;
    for(unsigned trial=0;trial<64;++trial){
      rules.uniform_acceleration={.12,-.2,.07};Physics actual(rules);
      std::vector<Body> reference;
      for(unsigned i=0;i<192;++i){
        Body body;
        const double spread=trial%4==0?.02:trial%4==1?.6:3.;
        body.position={spread*double(i%8),spread*double(i/8%8),spread*double(i/64)};
        body.velocity={double(int(random()%21)-10)*.1,double(int(random()%21)-10)*.1,double(int(random()%21)-10)*.1};
        body.inverse_mass=random()%7==0?0.:1./double(1+random()%5);
        body.radius=.15+.05*double(random()%5);body.restitution=double(random()%11)/10.;
        actual.add(body);reference.push_back(body);
      }
      for(unsigned step=0;step<4;++step){
        reference_step(reference,rules,.005);actual.step(.005);
        for(std::size_t i=0;i<reference.size();++i)equal(actual.bodies()[i],reference[i]);
        if(actual.step_stats().dense_fallback)++dense_scenes;else ++sparse_scenes;
      }
    }
    require(sparse_scenes>0&&dense_scenes>0,"reference campaign missed a contact path");

    rules.uniform_acceleration={0,0,0};Physics sparse(rules);
    for(unsigned i=0;i<4096;++i){a={};a.position={double(i)*3,0,0};sparse.add(a);}
    sparse.step(.01);
    require(sparse.step_stats().collision_pairs==0,"sparse broadphase enumerated separated pairs");
    require(sparse.step_stats().scratch_bytes<4*1024*1024,"unexpected scratch growth");
    rules.max_tree_visits=1;sparse.set_rules(rules);const auto sparse_revision=sparse.revision();
    rejects([&]{sparse.step(.01);});require(sparse.revision()==sparse_revision,"tree-budget failure published a step");

    std::cout<<"{\"suite\":\"rule_physics\",\"ok\":true,\"checks\":"<<checks
             <<",\"reference_scenes\":64,\"reference_steps\":256,\"sparse_pairs_4096\":0}\n";
    return 0;
  }catch(const std::exception& e){std::cerr<<"rule_physics: "<<e.what()<<'\n';return 1;}
}
