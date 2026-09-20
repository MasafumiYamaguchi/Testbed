#pragma once
#include "white/top_lobes.hpp"
namespace white {
AnvilSource make_anvil_source(const TopLobeSource&);
Vec3 anvil_connection_point(const AnvilSource&);
std::vector<std::string> validate_anvil(const AnvilSource&);
struct alignas(16) GpuAnvilParams {
    GpuTopLobeParams cloud{};
    Float4 center{}; // Object-local center.
    Float4 direction{}; // Unit XZ direction, shear in w.
    Float4 dimensions{}; // Along radius, cross radius, half-thickness, fade.
    Float4 settings{}; // Enabled, density scale, start altitude, maximum.
    Float4 envelope_min{},envelope_max{};
};
static_assert(sizeof(GpuAnvilParams)==2016);
class AnvilEvaluationPlan {
public:
    explicit AnvilEvaluationPlan(AnvilSource);
    const AnvilSource& source()const{return source_;}
    double at(Vec3 object_local)const;
    double maximum()const{return maximum_;}
    Bounds local_support()const{return support_;}
    Bounds world_support()const;
    Vec3 connection_point()const{return anchor_;}
    GpuAnvilParams gpu_params()const;
private:
    AnvilSource source_;
    TopLobeEvaluationPlan cloud_;
    std::optional<DensityField> field_;
    std::optional<AltitudeDensityEvaluator> profile_;
    Bounds support_{};
    Vec3 anchor_{},center_{},side_{},translation_{};
    double boundary_=0,maximum_=0;
};
std::vector<float> bake_anvil(const AnvilEvaluationPlan&,const GridLayout&);
}
