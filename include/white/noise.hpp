#pragma once
#include "white/document.hpp"
namespace white {
std::uint32_t noise_seed(std::uint64_t);
double value_noise(Vec3 p,std::uint32_t seed); // continuous [0,1]
double detail_noise(Vec3 p,std::uint32_t seed); // two fixed octaves, [0,1]
Vec3 domain_displacement(Vec3 p,std::uint32_t seed,double maximum_metres);
}
