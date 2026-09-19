#include "white/optics.hpp"
#include <array>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <limits>
using namespace white;
int main(){int failed=0;auto check=[&](bool ok,const char* name){if(!ok){std::cerr<<"FAIL "<<name<<'\n';++failed;}};
 auto close=[](double a,double b,double abs_tol=1e-13,double rel_tol=2e-12){return std::abs(a-b)<=abs_tol+rel_tol*std::abs(b);};
 std::cout<<std::setprecision(16)<<"case,steps,transmittance,radiance,T_abs_error,L_abs_error\n";
 for(double sigma:{0.,1e-12,.01,.5,10.})for(unsigned steps:{1u,8u,64u,128u}){
  auto actual=integrate_homogeneous(sigma,10,steps,.6);const double t=std::exp(-10*sigma),l=.6*-std::expm1(-10*sigma);
  std::cout<<"slab-"<<sigma<<','<<steps<<','<<actual.transmittance<<','<<actual.radiance<<','<<std::abs(actual.transmittance-t)<<','<<std::abs(actual.radiance-l)<<'\n';
  check(close(actual.transmittance,t),"slab T");check(close(actual.radiance,l,l<1e-8?1e-24:1e-13,2e-11),"slab L including near vacuum");
 }
 auto vacuum=integrate_constant_source(0,3,2);check(vacuum.transmittance==1&&vacuum.radiance==6,"fixed source vacuum limit");
 auto tiny=integrate_constant_source(1e-12,5,1e-15);check(close(tiny.radiance,5e-15,1e-27,1e-11),"tiny source does not vanish");
 auto a=integrate_constant_source(.1,2,.3),b=integrate_constant_source(.5,1,.2);
 auto ab=compose_front_to_back(a,b),ba=compose_front_to_back(b,a);
 check(close(ab.transmittance,std::exp(-.7))&&close(ab.transmittance,ba.transmittance),"transmittance independent of order");
 check(!close(ab.radiance,ba.radiance),"front/back radiance must not commute");
 const std::array<OpticalSegment,2> pieces{{{.1,2,.3},{.5,1,.2}}};auto piecewise=integrate_piecewise(pieces);check(close(piecewise.radiance,ab.radiance),"piecewise composition");
 auto base=integrate_constant_source(.1,10,.2),scaled=integrate_constant_source(.025,40,.05);
 check(close(base.transmittance,scaled.transmittance)&&close(base.radiance,scaled.radiance),"world scale and inverse coefficients preserve optical thickness/source");
 auto coeff=optical_coefficients(2,.1,.75);check(close(coeff.extinction,.2)&&close(coeff.scattering,.15)&&close(coeff.absorption,.05),"sigma_t sigma_s sigma_a contract");
 double previous=1;
 for(unsigned n:{16u,32u,64u,128u}){OpticalResult result;double ds=10./n;for(unsigned i=0;i<n;++i){double x=(i+.5)*ds;result=compose_front_to_back(result,integrate_homogeneous(.02+.003*x*x,ds,1,.6));}double error=std::abs(result.transmittance-std::exp(-1.2));std::cout<<"quadratic,"<<n<<','<<result.transmittance<<','<<result.radiance<<','<<error<<','<<std::abs(result.radiance-.6*-std::expm1(-1.2))<<'\n';check(error<previous*.26,"midpoint quadratic convergence should be second order");previous=error;}
 auto rejects=[&](auto fn){bool caught=false;try{fn();}catch(const std::invalid_argument&){caught=true;}check(caught,"invalid optical input diagnosed");};
 rejects([]{optical_coefficients(-1,1,.5);});rejects([]{optical_coefficients(1,1,1.1);});rejects([]{integrate_constant_source(1,-1,1);});rejects([]{integrate_constant_source(1,1,-1);});rejects([]{integrate_constant_source(std::numeric_limits<double>::quiet_NaN(),1,1);});rejects([]{integrate_constant_source(1,std::numeric_limits<double>::infinity(),1);});rejects([]{integrate_constant_source(1e308,1e308,1);});
 return failed?1:0;
}
