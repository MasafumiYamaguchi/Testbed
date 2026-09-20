#include "white/density.hpp"
#include "white/noise.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace white {
namespace {
double smooth(double x) {x=std::clamp(x,0.0,1.0);return x*x*(3-2*x);}
double implicit(Vec3 p,Vec3 center,Vec3 radii) {
    p=p-center;p={p.x/radii.x,p.y/radii.y,p.z/radii.z};
    return (std::sqrt(dot(p,p))-1)*std::min({radii.x,radii.y,radii.z});
}
double smooth_min(double a,double b,double k) {
    if(k==0)return std::min(a,b);
    const double h=std::max(k-std::abs(a-b),0.0)/k;
    return std::min(a,b)-h*h*k*0.25;
}
double coverage(double d) {return 1-smooth((d+2)/2);}
Float4 pack(Vec3 p,float w=0) {return {float(p.x),float(p.y),float(p.z),w};}
}
DensityField::DensityField(CloudRecipe recipe):recipe_(std::move(recipe)) {
    Scene s;s.cloud=recipe_;require_valid(s);
    std::sort(recipe_.cells.begin(),recipe_.cells.end(),[](auto& a,auto& b){return a.id<b.id;});
    std::sort(recipe_.cuts.begin(),recipe_.cuts.end(),[](auto& a,auto& b){return a.id<b.id;});
}
double DensityField::maximum() const {
    return recipe_.cells.empty()?0:recipe_.density*(1+recipe_.overlap*double(recipe_.cells.size()-1));
}
double DensityField::at(Vec3 p) const {
    if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z))throw std::invalid_argument("Non-finite field sample");
    const auto& r=recipe_;const auto& e=r.envelope;
    if(r.cells.empty()||p.x<=e.min.x||p.y<=e.min.y||p.z<=e.min.z||p.x>=e.max.x||p.y>=e.max.y||p.z>=e.max.z)return 0;
    if(r.base.enabled&&p.y<=r.base.height)return 0;
    double merged=0,sum=0;
    for(size_t i=0;i<r.cells.size();++i) {
        const auto& c=r.cells[i];auto q=p;
        if(r.noise.warp_amplitude>0)q=q+domain_displacement((p-r.noise.origin)*r.noise.warp_frequency,noise_seed(cell_random_key(r,c)),r.noise.warp_amplitude);
        const double d=implicit(q,c.center,c.radii);
        merged=i==0?d:smooth_min(merged,d,r.blend_width);sum+=coverage(d);
    }
    const auto n=p-r.noise.origin;const auto seed=noise_seed(r.detail_seed);
    if(r.noise.micro_erosion>0)merged+=r.noise.micro_erosion*detail_noise(n*r.noise.micro_frequency,seed^0x6c8e9cf5u);
    double value=r.density*coverage(merged)*(1+r.overlap*std::max(0.0,sum-1));
    if(r.noise.medium_strength>0)value*=1-r.noise.medium_strength*detail_noise(n*r.noise.medium_frequency,seed);
    if(r.base.enabled&&r.base.transition>0)value*=smooth((p.y-r.base.height)/r.base.transition);
    for(const auto& cut:r.cuts) {
        const double d=implicit(p,cut.center,cut.radii);
        if(d<=0)return 0;
        if(cut.transition>0)value*=smooth(d/cut.transition);
    }
    return std::clamp(value,0.0,maximum());
}
Bounds DensityField::local_support() const {
    // The explicit envelope is always conservative, including empty fields.
    // A tighter shape bound can be introduced later with a separate proof.
    return recipe_.envelope;
}
Bounds DensityField::world_support() const {
    Bounds out{{1e300,1e300,1e300},{-1e300,-1e300,-1e300}};
    const auto b=local_support();
    for(int i=0;i<8;++i) {
        const auto p=local_to_world(recipe_.transform,{i&1?b.max.x:b.min.x,i&2?b.max.y:b.min.y,i&4?b.max.z:b.min.z});
        out.min={std::min(out.min.x,p.x),std::min(out.min.y,p.y),std::min(out.min.z,p.z)};
        out.max={std::max(out.max.x,p.x),std::max(out.max.y,p.y),std::max(out.max.z,p.z)};
    }
    return out;
}
CloudRecipe density_fixture(int preset) {
    CloudRecipe r;r.envelope={{-50,-20,-50},{50,100,50}};
    r.cells[0].center={0,35,0};r.cells[0].radii={18,48,18};r.base.enabled=false;
    switch(preset) {
    case 0:break;
    case 1:r.cells[0].center={0,25,0};r.cells[0].radii={42,20,25};break;
    case 2:r.cells[0].center.x=-12;r.cells.push_back({3,{15,30,0},{25,36,22},7});r.blend_width=12;break;
    case 3:r.base.enabled=true;r.base.height=10;r.base.transition=3;break;
    case 4:r.base.enabled=true;r.base.height=0;r.cuts.push_back({3,{12,40,0},{17,20,26},3});break;
    default:throw std::invalid_argument("Unknown density fixture");
    }
    return r;
}
GpuDensityParams gpu_density_params(const DensityField& field) {
    GpuDensityParams out;const auto& r=field.recipe();
    for(size_t i=0;i<r.cells.size();++i){out.centers[i]=pack(r.cells[i].center);out.radii[i]=pack(r.cells[i].radii);}
    for(size_t i=0;i<r.cuts.size();++i){out.cut_centers[i]=pack(r.cuts[i].center);out.cut_radii[i]=pack(r.cuts[i].radii,float(r.cuts[i].transition));}
    out.envelope_min=pack(r.envelope.min);out.envelope_max=pack(r.envelope.max);
    out.settings={float(r.base.height),float(r.base.transition),float(r.density),float(r.blend_width)};
    out.config={float(r.overlap),r.base.enabled?1.0f:0.0f,float(r.cells.size()),float(r.cuts.size())};
    for(size_t i=0;i<r.cells.size();++i)out.cell_keys[i].x=noise_seed(cell_random_key(r,r.cells[i]));
    out.noise_origin=pack(r.noise.origin);out.noise_bands={float(r.noise.medium_frequency),float(r.noise.medium_strength),float(r.noise.micro_frequency),float(r.noise.micro_erosion)};
    out.noise_warp={float(r.noise.warp_frequency),float(r.noise.warp_amplitude),0,0};out.noise_seeds.x=noise_seed(r.detail_seed);
    return out;
}
Scene fixture_scene(int preset) {
    Scene scene;scene.cloud=density_fixture(preset);scene.camera.position={120,70,120};scene.camera.target={0,35,0};
    const double angle=-40*3.141592653589793/180;
    Vec3 sun{std::sin(angle),0.8,std::cos(angle)};scene.sun.direction_to_light=sun*(1/std::sqrt(dot(sun,sun)));
    scene.sun.irradiance={15,15,15};scene.exposure_ev=1;return scene;
}
}
