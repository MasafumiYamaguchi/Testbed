#include "white/optics.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace white {
namespace {
void nonnegative(double x,const char* name){if(!std::isfinite(x)||x<0)throw std::invalid_argument(name);}
void check_result(OpticalResult r){nonnegative(r.transmittance,"Invalid transmittance");nonnegative(r.radiance,"Invalid radiance");if(r.transmittance>1)throw std::invalid_argument("Transmittance exceeds one");}
}
MediumCoefficients optical_coefficients(double rho,double scale,double albedo){
    nonnegative(rho,"Invalid density");nonnegative(scale,"Invalid extinction scale");nonnegative(albedo,"Invalid albedo");if(albedo>1)throw std::invalid_argument("Albedo exceeds one");
    const double t=rho*scale;nonnegative(t,"Extinction product overflow");const double s=t*albedo;return {t,s,t-s};
}
OpticalResult integrate_constant_source(double sigma,double length,double j){
    nonnegative(sigma,"Invalid extinction coefficient");nonnegative(length,"Invalid ray length");nonnegative(j,"Invalid source per metre");
    const double tau=sigma*length;nonnegative(tau,"Optical depth overflow");
    const double integral=tau==0?length:-std::expm1(-tau)/sigma;
    OpticalResult result{std::exp(-tau),j*integral};check_result(result);return result;
}
OpticalResult compose_front_to_back(OpticalResult front,OpticalResult back){
    check_result(front);check_result(back);OpticalResult result{front.transmittance*back.transmittance,front.radiance+front.transmittance*back.radiance};check_result(result);return result;
}
OpticalResult integrate_piecewise(std::span<const OpticalSegment> segments){OpticalResult result;for(auto s:segments)result=compose_front_to_back(result,integrate_constant_source(s.sigma,s.length,s.j));return result;}
OpticalResult integrate_homogeneous(double sigma,double length,unsigned steps,double source) {
    nonnegative(sigma,"Invalid extinction coefficient");nonnegative(length,"Invalid ray length");nonnegative(source,"Invalid source function");
    if(steps==0||steps>1000000)throw std::invalid_argument("Invalid step count");
    const double tau=sigma*(length/steps);nonnegative(tau,"Optical depth overflow");
    const OpticalResult segment{std::exp(-tau),source*-std::expm1(-tau)};check_result(segment);
    OpticalResult result;for(unsigned i=0;i<steps;++i)result=compose_front_to_back(result,segment);return result;
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
