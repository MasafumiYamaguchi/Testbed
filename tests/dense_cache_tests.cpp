#include "white/dense_cache.hpp"
#include "white/density.hpp"
#include <cmath>
#include <iostream>
using namespace white;
int main(){int failures=0;auto check=[&](bool ok,const char* label){std::cout<<(ok?"PASS ":"FAIL ")<<label<<'\n';failures+=!ok;};
    GridLayout grid{{{-20,3,-5},{30,83,10}},{17,19,23}};std::vector<float> values(17*19*23);
    for(int z=0;z<23;++z)for(int y=0;y<19;++y)for(int x=0;x<17;++x)values[(z*19+y)*17+x]=float(x+2*y+4*z)/256;
    auto p=index_to_local(grid,{4.27,8.43,12.61});check(std::abs(sample_dense(grid,values,p)-(4.27+2*8.43+4*12.61)/256)<1e-12,"noncubic transformed ramp interpolation has no half-voxel shift");
    check(std::abs(sample_dense(grid,values,index_to_local(grid,{0,0,0})))<1e-12,"first voxel center");
    check(sample_dense(grid,values,grid.local_bounds.max)==0,"envelope boundary is zero");
    std::fill(values.begin(),values.end(),0);values[(11*19+9)*17+8]=1;
    check(std::abs(sample_dense(grid,values,index_to_local(grid,{8.5,9,11}))-0.5)<1e-12,"impulse trilinear half texel");
    auto budget=cache_budget({256,256,256},64ull*1024*1024,0);check(budget.texture_bytes==64ull*1024*1024&&budget.peak_gpu_buffer_bytes==192ull*1024*1024,"budget includes replacement and padded readback");
    try{cache_budget({512,512,512},0,0);check(false,"overbudget rejected");}catch(const std::invalid_argument&){check(true,"overbudget rejected before allocation");}
    auto r=density_fixture(4);check(!hard_density_region(r,{12,40,0})&&!hard_density_region(r,{0,-1,0}),"hard cut and base protected after filtering");
    return failures?1:0;
}
