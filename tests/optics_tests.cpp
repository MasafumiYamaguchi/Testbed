#include "white/optics.hpp"
#include <cmath>
#include <iostream>
using namespace white;
int main() {
    int failures=0;auto check=[&](bool ok,const char* name){std::cout<<(ok?"PASS ":"FAIL ")<<name<<'\n';failures+=!ok;};
    for(double sigma:{0.0,1e-9,0.01,0.1,1.0,10.0})for(unsigned steps:{1u,7u,64u,256u}) {
        auto r=integrate_homogeneous(sigma,20,steps,0.5);auto exact=std::exp(-sigma*20);
        check(std::abs(r.transmittance-exact)<1e-12&&std::abs(r.radiance-0.5*(1-exact))<1e-12,"homogeneous transmittance/source analytic agreement");
    }
    auto zero=integrate_homogeneous(1,0,64,1);check(zero.transmittance==1&&zero.radiance==0,"zero distance");
    auto absorbed=integrate_homogeneous(1,3,64,0);check(absorbed.radiance==0,"absorption-only no light");
    Bounds b{{-1,-1,-1},{1,1,1}};
    auto inside=intersect_bounds({0,0,0},{1,0,0},b,0,100);check(inside&&inside->entry==0&&inside->exit==1,"inside camera");
    auto axis=intersect_bounds({-3,0,0},{1,0,0},b,0,100);check(axis&&axis->entry==2&&axis->exit==4,"axis parallel ray");
    check(!intersect_bounds({-3,2,0},{1,0,0},b,0,100),"parallel outside ray");
    auto edge=intersect_bounds({-3,1,0},{1,0,0},b,0,100);check(edge&&edge->entry==2,"boundary ray finite");
    try{integrate_homogeneous(-1,1,1,1);check(false,"invalid coefficients rejected");}catch(const std::invalid_argument&){check(true,"invalid coefficients rejected");}
    return failures?1:0;
}
