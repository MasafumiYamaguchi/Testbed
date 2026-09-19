#include "white/sun_cache.hpp"
#include "white/optics.hpp"
#include "white/persistence.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
void check(bool v){if(!v)throw std::runtime_error("Sun cache contract");}
int main(int argc,char** argv){try{
    if(argc>1)for(const auto& file:std::filesystem::directory_iterator(argv[1]))if(file.path().extension()==".json")(void)white::read_scene(file.path());
    white::Scene s;s.cloud.envelope={{-10,-20,-30},{10,20,30}};s.cloud.base.enabled=false;s.cloud.cuts.clear();s.cloud.optics.extinction_scale=.02;
    white::GridLayout grid{s.cloud.envelope,{9,11,13}};std::vector<float> values(9*11*13,1);
    for(auto sun:{white::Vec3{1,0,0},white::Vec3{0,1,0},white::Vec3{0,0,-1},white::Vec3{.9999500037496876,.009999500037496877,0}}){
        s.sun.direction_to_light=sun;
        for(unsigned steps:{1,8,32}){const auto interval=white::intersect_bounds({0,0,0},sun,s.cloud.envelope,0,s.camera.far_plane);const double expected=interval->exit*.02;
            check(std::abs(white::sun_optical_depth(s,grid,values,{0,0,0},steps)-expected)<1e-12);}
    }
    std::fill(values.begin(),values.end(),0);check(white::sun_optical_depth(s,grid,values,{0,0,0},8)==0);
    const auto key=white::sun_cache_key(s,grid.extent,32,8);auto v=s;v.exposure_ev=2;v.camera.position.x+=1;v.cloud.optics.g=.5;v.cloud.optics.albedo=.5;v.sun.irradiance.x+=2;
    check(white::sun_cache_key(v,grid.extent,32,8)==key);
    v=s;v.sun.direction_to_light={0,1,0};check(white::sun_cache_key(v,grid.extent,32,8)!=key);
    v=s;v.cloud.optics.extinction_scale*=2;check(white::sun_cache_key(v,grid.extent,32,8)!=key);
    v=s;v.cloud.density*=2;check(white::sun_cache_key(v,grid.extent,32,8)!=key);
    v=s;v.cloud.transform.scale.x*=2;check(white::sun_cache_key(v,grid.extent,32,8)!=key);
    check(white::sun_cache_key(s,grid.extent,64,8)!=key&&white::sun_cache_key(s,grid.extent,32,16)!=key);
    // Interpolating tau then exponentiating is deliberately not interpolating T.
    check(std::abs(std::exp(-1.)-(1+std::exp(-2.))*.5)>.1);
    std::cout<<"Sun cache: empty/homogeneous, noncubic axes/low sun, optical/density/transform invalidation, view-only reuse passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
