#include "white/noise.hpp"
#include <cmath>
#include <algorithm>
namespace white {
namespace {
std::uint32_t hash(std::uint32_t x){x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;return x^(x>>16);}
double smooth(double t){return t*t*t*(t*(t*6-15)+10);}
double mix(double a,double b,double t){return a+(b-a)*t;}
}
std::uint32_t noise_seed(std::uint64_t seed){return hash(std::uint32_t(seed)^hash(std::uint32_t(seed>>32)));}
double value_noise(Vec3 p,std::uint32_t seed){
    const auto ix=std::int32_t(std::floor(p.x)),iy=std::int32_t(std::floor(p.y)),iz=std::int32_t(std::floor(p.z));
    const double x=smooth(p.x-ix),y=smooth(p.y-iy),z=smooth(p.z-iz);
    auto sample=[&](int dx,int dy,int dz){auto h=hash(std::uint32_t(ix+dx)*0x8da6b343u^std::uint32_t(iy+dy)*0xd8163841u^std::uint32_t(iz+dz)*0xcb1ab31fu^seed);return double(h&0xffffffu)/16777215.0;};
    return std::clamp(mix(mix(mix(sample(0,0,0),sample(1,0,0),x),mix(sample(0,1,0),sample(1,1,0),x),y),
        mix(mix(sample(0,0,1),sample(1,0,1),x),mix(sample(0,1,1),sample(1,1,1),x),y),z),0.0,1.0);
}
double detail_noise(Vec3 p,std::uint32_t seed){return (2*value_noise(p,seed)+value_noise(p*2,seed^0x9e3779b9u))/3;}
Vec3 domain_displacement(Vec3 p,std::uint32_t seed,double maximum_metres){
    const double scale=maximum_metres/std::sqrt(3.0);
    return Vec3{2*value_noise(p,seed)-1,2*value_noise(p,seed^0xa511e9b3u)-1,2*value_noise(p,seed^0x63d83595u)-1}*scale;
}
}
