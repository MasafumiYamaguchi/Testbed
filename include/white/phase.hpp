#pragma once
#include "white/document.hpp"
namespace white {
inline constexpr double max_phase_g=0.95;
double hg_phase(double cosine,double g);
struct PhaseSample {Vec3 direction;double pdf;};
// Both vectors describe photon propagation, not two outward-pointing directions.
PhaseSample sample_hg(Vec3 incoming,double g,double u,double v);
double phase_random(std::uint32_t pixel,std::uint32_t sample,std::uint32_t dimension,std::uint64_t seed);
}
