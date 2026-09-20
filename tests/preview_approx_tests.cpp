#include "white/preview_approx.hpp"
#include "white/phase.hpp"
#include "white/optics.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace white;
int main() {
    int failures=0;
    auto check=[&](bool ok,const char* message){if(!ok){++failures;std::cerr<<"FAIL "<<message<<'\n';}};
    auto rejected=[&](auto operation,const char* message){try{operation();check(false,message);}catch(const std::invalid_argument&){};};
    const PreviewApproxSettings on{true,.5};
    check(!PreviewApproxSettings{}.enabled,"approximation must default off");
    // A broad domain, including strongly backward/forward phase and thick shadow.
    for(double tau:{0.,1e-12,1e-6,.01,.2,1.,5.,20.,100.,1000.})
    for(double albedo:{0.,.2,.8,.98,1.})
    for(double g:{-.95,-.7,0.,.7,.95})
    for(double cosine:{-1.,-.3,0.,.3,1.}) {
        const double direct=2*albedo*hg_phase(cosine,g)*std::exp(-tau);
        const auto off=preview_approx_source(tau,albedo,g,cosine,2);
        const auto enabled=preview_approx_source(tau,albedo,g,cosine,2,on);
        check(off.total()==direct,"OFF changed the single-scattering formula");
        check(std::isfinite(enabled.total())&&enabled.total()>=off.total(),"invalid added source");
        check(preview_approx_source(tau,albedo,g,cosine,0,on).total()==0,"zero sun left emission");
        check(preview_approx_source(tau,0,g,cosine,2,on).total()==0,"zero albedo left emission");
        check(preview_approx_source(tau,albedo,g,cosine,2,{true,0}).total()==direct,"zero strength differs from OFF");
        check(preview_approx_source(tau,albedo,g,cosine,4,on).total()==2*enabled.total(),"sun scaling is not linear");
        check(integrate_homogeneous(0,10,1,enabled.total()).radiance==0,"vacuum left emission");
        if(tau==0)check(enabled.multiple_scattering_approx==0,"lit boundary added unbounded fill");
    }
    // Optically thin limit: extra source is O(tau), integrated extra light O(tau^2).
    const auto thin1=preview_approx_source(1e-6,.8,.4,.2,1,on);
    const auto thin2=preview_approx_source(5e-7,.8,.4,.2,1,on);
    const double l1=integrate_homogeneous(1e-6,1,1,thin1.multiple_scattering_approx).radiance;
    const double l2=integrate_homogeneous(5e-7,1,1,thin2.multiple_scattering_approx).radiance;
    check(std::abs(l1/l2-4)<1e-5,"thin-medium extra is not second order");
    const auto thick1=preview_approx_source(30,.8,0,0,1,on);
    const auto thick2=preview_approx_source(60,.8,0,0,1,on);
    check(thick2.total()<thick1.total(),"fully shadowed asymptote does not decay");
    check(preview_approx_source(std::numeric_limits<double>::infinity(),1,.95,1,1,on).total()==0,"infinite shadow left emission");
    for(double strength:{-.01,1.01,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()})
        rejected([&]{require_valid_preview_approx({true,strength});},"invalid strength accepted");
    rejected([&]{preview_approx_source(-1,.8,0,0,1,on);},"negative tau accepted");
    rejected([&]{preview_approx_source(1,1.1,0,0,1,on);},"invalid albedo accepted");
    rejected([&]{preview_approx_source(1,.8,.951,0,1,on);},"invalid phase accepted");
    rejected([&]{preview_approx_source(1,.8,0,1.1,1,on);},"invalid cosine accepted");
    rejected([&]{preview_approx_source(1,.8,0,0,-1,on);},"negative sun accepted");
    rejected([&]{preview_approx_source(std::numeric_limits<double>::quiet_NaN(),.8,0,0,1,on);},"NaN tau accepted");
    // Log this predetermined parameter sweep. It is NOT a Monte Carlo comparison.
    std::cout<<"tau,albedo,cosine,single_source,approx_source\n";
    for(double tau:{.2,5.})for(double albedo:{.2,.8,.98})for(double mu:{-1.,1.}) {
        const auto source=preview_approx_source(tau,albedo,.7,mu,1,on);
        std::cout<<tau<<','<<albedo<<','<<mu<<','<<source.single_scattering<<','<<source.total()<<'\n';
    }
    return failures?1:0;
}
