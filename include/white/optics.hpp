#pragma once
#include "white/document.hpp"
#include <optional>
namespace white {
struct OpticalResult {double transmittance=1,radiance=0;};
OpticalResult integrate_homogeneous(double sigma,double length,unsigned steps,double source);
struct RayInterval {double entry,exit;};
std::optional<RayInterval> intersect_bounds(Vec3 origin,Vec3 direction,Bounds,double near,double far);
}
