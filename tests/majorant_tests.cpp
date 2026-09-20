#include "white/majorant.hpp"
#include "white/dense_cache.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#define check(v) do{if(!(v))throw std::runtime_error("Majorant contract line "+std::to_string(__LINE__));}while(false)
int main(){try{
    white::GridLayout grid{{{-13,-7,-3},{21,15,23}},{17,19,23}};std::vector<float> values(17*19*23);
    for(int fixture=0;fixture<3;++fixture){
        for(size_t i=0;i<values.size();++i)values[i]=fixture==0?0:fixture==1?(i==8+8*17+8*17*19?9.f:0.f):float((i*48271+17)%1024)/1024;
        const auto m=white::build_majorant(grid,values);check(m.levels.back().maxima[0]==*std::max_element(values.begin(),values.end()));
        // Exhaust every interpolation cell and all eight contributing values.
        for(unsigned z=0;z<22;++z)for(unsigned y=0;y<18;++y)for(unsigned x=0;x<16;++x)for(int corner=0;corner<8;++corner){
            const auto q=white::index_to_local(grid,{x+.5,y+.5,z+.5});const auto b=white::majorant_at(m,q);const unsigned ix=x+(corner&1),iy=y+((corner>>1)&1),iz=z+((corner>>2)&1);check(b>=values[(iz*19+iy)*17+ix]);
        }
        for(auto direction:{white::Vec3{1,0,0},white::Vec3{-1,0,0},white::Vec3{0,1,0},white::Vec3{0,0,-1},white::Vec3{.3,.7,-.2}}){
            const auto segments=white::traverse_bricks(grid,{0,0,0},direction,0,1000);check(!segments.empty());
            for(size_t i=0;i<segments.size();++i){check(segments[i].exit>segments[i].entry);if(i)check(segments[i].entry==segments[i-1].exit);const auto q=white::Vec3{0,0,0}+direction*((segments[i].entry+segments[i].exit)*.5);const auto index=white::local_to_index(grid,q);const double v[]{index.x+.5,index.y+.5,index.z+.5};for(int a=0;a<3;++a)check(unsigned(std::floor(v[a]/8))==segments[i].brick[a]);}
            double direct=0,skipped=0;for(unsigned i=0;i<512;++i){const auto q=direction*(i*.1);const auto d=white::sample_dense(grid,values,q);direct+=d;if(white::majorant_at(m,q)>0)skipped+=d;}check(direct==skipped);
        }
    }
    // Exact brick boundaries, both directions, zero direction axes, end on face.
    const auto boundary=white::index_to_local(grid,{7.5,7.5,7.5});for(double sign:{-1.,1.}){auto intervals=white::traverse_bricks(grid,boundary,{sign,0,0},0,100);check(!intervals.empty());check(intervals.front().brick[0]==(sign<0?0u:1u));}
    std::cout<<"Majorant exhaustive support, impulse halo, random grid, hierarchy, exact fixed-sample skipping and DDA boundary checks passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
