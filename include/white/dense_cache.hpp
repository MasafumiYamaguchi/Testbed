#pragma once
#include "white/document.hpp"
#include <span>
namespace white {
struct CacheBudget {std::uint64_t texture_bytes,transfer_bytes,peak_gpu_buffer_bytes,cpu_reference_bytes;};
CacheBudget cache_budget(std::array<std::uint32_t,3> extent,std::uint64_t previous_bytes,std::uint64_t other_gpu_bytes,std::uint64_t limit=256ull*1024*1024);
bool hard_density_region(const CloudRecipe&,Vec3);
// R32F, x fastest, voxel centers; normalized linear clamp inside the envelope.
double sample_dense(const GridLayout&,std::span<const float>,Vec3 local);
}
