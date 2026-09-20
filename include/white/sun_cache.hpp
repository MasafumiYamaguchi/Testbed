#pragma once
#include "white/dense_cache.hpp"
namespace white {
std::uint64_t sun_cache_key(const Scene&,std::array<std::uint32_t,3> density_extent,unsigned resolution,unsigned steps);
double sun_optical_depth(const Scene&,const GridLayout&,std::span<const float> density,Vec3 origin,unsigned steps);
}
