#pragma once
#include "white/document.hpp"
#include <span>
namespace white {
inline constexpr unsigned majorant_brick=8;
struct MajorantLevel {std::array<unsigned,3> extent;std::vector<float> maxima;};
struct Majorant {GridLayout density_grid;std::vector<MajorantLevel> levels;};
Majorant build_majorant(const GridLayout&,std::span<const float>);
float majorant_at(const Majorant&,Vec3);
struct BrickInterval {double entry,exit;std::array<unsigned,3> brick;};
std::vector<BrickInterval> traverse_bricks(const GridLayout&,Vec3 origin,Vec3 direction,double begin,double end);
}
