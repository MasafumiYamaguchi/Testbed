#include "white/phase.hpp"
#include "white/persistence.hpp"
#include "white/revision_queue.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
using namespace white;
int main(){int failures=0;auto check=[&](bool ok,const char* msg){if(!ok){std::cerr<<"FAIL "<<msg<<'\n';++failures;}};constexpr double pi=3.14159265358979323846;
 for(double g:{-.95,-.5,-1e-8,0.,1e-8,.5,.95}){
  constexpr int count=65536;double integral=0,moment=0;
  for(int i=0;i<=count;++i){double mu=-1+2.*i/count,w=(i==0||i==count)?1:(i%2?4:2);const double p=hg_phase(mu,g);integral+=w*p;moment+=w*p*mu;check(std::isfinite(p)&&p>0,"invalid pdf");}
  integral*=2*pi*(2./count)/3;moment*=2*pi*(2./count)/3;check(std::abs(integral-1)<2e-6&&std::abs(moment-g)<2e-6,"phase normalization/mean cosine");
  std::cout<<"HG g="<<g<<" sphere_integral="<<integral<<" cosine_integral="<<moment<<'\n';
  for(std::uint64_t seed:{17ull,42ull,0x100000011ull,99991ull}){
   std::array<unsigned,8> bins{};double mean=0;constexpr unsigned n=32768;
   for(unsigned i=0;i<n;++i){auto sample=sample_hg({0,0,1},g,phase_random(i,3,0,seed),phase_random(i,3,1,seed));const double mu=sample.direction.z;mean+=mu;++bins[std::min(7,int((mu+1)*4))];check(std::abs(dot(sample.direction,sample.direction)-1)<1e-12,"sample direction not unit");check(std::abs(sample.pdf-hg_phase(mu,g))<1e-10,"sample/evaluate pdf disagreement");}
   mean/=n;const double bound=6*std::sqrt((1-g*g)/(3*n))+.0002;check(std::abs(mean-g)<bound,"sample mean outside six-sigma bound");
   auto cdf=[&](double mu){return std::abs(g)<1e-5?(mu+1)*.5:((1-g*g)/std::sqrt(1+g*g-2*g*mu)-(1-g))/(2*g);};
   for(int b=0;b<8;++b){double probability=cdf(-1+(b+1)*.25)-cdf(-1+b*.25),expected=n*probability;check(std::abs(double(bins[b])-expected)<=6*std::sqrt(n*probability*(1-probability))+3,"histogram disagrees with pdf CDF");}
   std::cout<<"HG sample g="<<g<<" seed="<<seed<<" count="<<n<<" mean_cos="<<mean<<" mean_bound="<<bound<<'\n';
  }
 }
 check(hg_phase(.3,0)==1/(4*pi),"g=0 not isotropic");
 for(auto wi:{Vec3{1,0,0},Vec3{0,0,-1},Vec3{0,1,0}}){auto s=sample_hg(wi,.7,.3,.6);check(std::abs(dot(s.direction,s.direction)-1)<1e-12,"basis unstable on axis");}
 Scene a;auto b=a;b.cloud.optics.g=.7;check(classify_change(a,b)==Dirty::optics&&density_input_hash(a)==density_input_hash(b),"g dirties density");check(parse_scene_json(scene_json(b))==b,"g serialization");b.cloud.optics.g=.951;check(!validate(b).empty(),"g beyond bound accepted");
 if(failures==0)std::cout<<"HG normalization, sampling, RNG, persistence and invalidation PASS\n";return failures?1:0;
}
