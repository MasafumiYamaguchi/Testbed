#include "white/optics.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace white {
OpticalResult integrate_homogeneous(double sigma,double length,unsigned steps,double source) {
    if(!std::isfinite(sigma)||!std::isfinite(length)||!std::isfinite(source)||sigma<0||length<0||source<0||steps==0||steps>1000000)
        throw std::invalid_argument("Invalid homogeneous integration parameters");
    OpticalResult result;const double opacity=-std::expm1(-sigma*(length/steps));
    for(unsigned i=0;i<steps;++i){result.radiance+=result.transmittance*source*opacity;result.transmittance*=1-opacity;}
    return result;
}
std::optional<RayInterval> intersect_bounds(Vec3 o,Vec3 d,Bounds bounds,double near,double far) {
    auto finite=[](Vec3 p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);};
    if(!finite(o)||!finite(d)||!finite(bounds.min)||!finite(bounds.max)||dot(d,d)==0||!std::isfinite(near)||!std::isfinite(far)||near<0||far<near||bounds.max.x<bounds.min.x||bounds.max.y<bounds.min.y||bounds.max.z<bounds.min.z)
        throw std::invalid_argument("Invalid ray bounds");
    const double origins[]={o.x,o.y,o.z},directions[]={d.x,d.y,d.z},lo[]={bounds.min.x,bounds.min.y,bounds.min.z},hi[]={bounds.max.x,bounds.max.y,bounds.max.z};
    for(int i=0;i<3;++i) {
        if(directions[i]==0){if(origins[i]<lo[i]||origins[i]>hi[i])return {};}
        else {double a=(lo[i]-origins[i])/directions[i],b=(hi[i]-origins[i])/directions[i];near=std::max(near,std::min(a,b));far=std::min(far,std::max(a,b));}
        if(far<=near)return {};
    }
    return RayInterval{near,far};
}
}
