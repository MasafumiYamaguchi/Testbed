#pragma once
#include "white/document.hpp"
#include <array>

namespace white {
// Validated, ID-sorted CPU reference. This is an implicit field, not an exact
// signed-distance function and must not be used for sphere tracing.
class DensityField {
public:
    explicit DensityField(CloudRecipe);
    double at(Vec3 local,double detail_scale=1,double density_scale=1) const;
    double maximum() const;
    Bounds local_support() const;
    Bounds world_support() const;
    const CloudRecipe& recipe() const {return recipe_;}
private:
    CloudRecipe recipe_;
    AltitudeDensityEvaluator altitude_density_;
};
CloudRecipe density_fixture(int preset); // 0 tall, 1 wide, 2 fusion, 3 flat, 4 cut
Scene fixture_scene(int preset);
struct alignas(16) Float4 {float x=0,y=0,z=0,w=0;};
struct alignas(16) UInt4 {std::uint32_t x=0,y=0,z=0,w=0;};
struct alignas(16) GpuDensityParams {
    std::array<Float4,8> centers{},radii{},cut_centers{},cut_radii{};
    Float4 envelope_min{},envelope_max{},settings{},config{};
    std::array<UInt4,8> cell_keys{};
    Float4 noise_origin{},noise_bands{},noise_warp{};
    UInt4 noise_seeds{};
    Float4 altitude_density_params{};
    std::array<Float4,8> altitude_density_knots{};
};
static_assert(sizeof(GpuDensityParams)==912);
GpuDensityParams gpu_density_params(const DensityField&);
}
