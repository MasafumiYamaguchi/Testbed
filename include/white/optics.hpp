#pragma once
#include "white/document.hpp"
#include <optional>
#include <span>
namespace white {
struct OpticalResult {double transmittance=1,radiance=0;};
struct MediumCoefficients {double extinction,scattering,absorption;};
MediumCoefficients optical_coefficients(double density,double extinction_scale,double albedo);
// j is radiance added per world metre; source in integrate_homogeneous is j/sigma_t.
OpticalResult integrate_constant_source(double sigma,double length,double j);
OpticalResult compose_front_to_back(OpticalResult front,OpticalResult back);
struct OpticalSegment {double sigma,length,j;};
OpticalResult integrate_piecewise(std::span<const OpticalSegment>);
OpticalResult integrate_homogeneous(double sigma,double length,unsigned steps,double source);
struct RayInterval {double entry,exit;};
std::optional<RayInterval> intersect_bounds(Vec3 origin,Vec3 direction,Bounds,double near,double far);
}
