#include "white/phase.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace white {
namespace {
constexpr double pi=3.14159265358979323846;
void check_g(double g){if(!std::isfinite(g)||std::abs(g)>max_phase_g)throw std::invalid_argument("HG g must be within [-0.95,0.95]");}
std::uint32_t mix(std::uint32_t x){x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;return x^(x>>16);}
Vec3 cross(Vec3 a,Vec3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
}
double hg_phase(double cosine,double g){
    check_g(g);if(!std::isfinite(cosine)||std::abs(cosine)>1+1e-12)throw std::invalid_argument("Invalid phase cosine");cosine=std::clamp(cosine,-1.,1.);
    const double d=g>=0?(1-g)*(1-g)+2*g*(1-cosine):(1+g)*(1+g)-2*g*(1+cosine);
    return (1-g*g)/(4*pi*d*std::sqrt(d));
}
PhaseSample sample_hg(Vec3 incoming,double g,double u,double v){
    check_g(g);const double norm=dot(incoming,incoming);
    if(!std::isfinite(norm)||std::abs(norm-1)>1e-6||!std::isfinite(u)||!std::isfinite(v)||u<0||u>=1||v<0||v>=1)throw std::invalid_argument("Invalid phase sample inputs");
    incoming=incoming*(1/std::sqrt(norm));const double a=2*u-1,den=1+g*a;
    // Expanded inverse CDF cancels the factor g algebraically, so g=0 and tiny
    // nonzero g share one stable expression rather than a mismatched branch.
    const double cosine=std::clamp((a*(1+g*g)+.5*g*(a*a+3)+.5*g*g*g*(a*a-1))/(den*den),-1.,1.);
    const double sine=std::sqrt(std::max(0.,1-cosine*cosine)),phi=2*pi*v;
    const Vec3 helper=std::abs(incoming.z)<.999?Vec3{0,0,1}:Vec3{1,0,0};
    auto tangent=cross(helper,incoming);tangent=tangent*(1/std::sqrt(dot(tangent,tangent)));const auto bitangent=cross(incoming,tangent);
    return {incoming*cosine+tangent*(sine*std::cos(phi))+bitangent*(sine*std::sin(phi)),hg_phase(cosine,g)};
}
double phase_random(std::uint32_t pixel,std::uint32_t sample,std::uint32_t dimension,std::uint64_t seed){
    const auto key=mix(std::uint32_t(seed))^mix(std::uint32_t(seed>>32)^0x9e3779b9u);
    const auto bits=mix(key^mix(pixel+0x68bc21ebu)^mix(sample+0x02e5be93u)^mix(dimension+0x967a889bu));
    return double(bits>>8)*(1.0/16777216.0);
}
}
