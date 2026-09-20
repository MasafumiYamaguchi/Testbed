#pragma once
#include "white/top_lobes.hpp"

namespace white {
// Exact zero/one-group Recipe, or first group's metadata carrier with the full
// union envelope. For multiple groups density must use SceneDensityEvaluator.
CloudRecipe developed_proxy_recipe(const DevelopedCloud&);
const DevelopedCloud* editable_developed_source(const Scene&);
bool scene_has_multiple_developments(const Scene&);
bool scene_has_active_top_lobes(const Scene&);
bool scene_density_requires_direct(const Scene&);
void refresh_developed_scene(Scene&);
Scene new_developed_scene(Scene scene={});
Scene scene_with_developed_command(Scene,const DevelopedCommand&);
class SceneDensityEvaluator {
public:
    explicit SceneDensityEvaluator(const Scene&);
    double at(Vec3 local)const;
    double maximum()const;
    Bounds local_support()const;
    Bounds world_support()const;
    const CloudRecipe& recipe()const{return recipe_;}
    GpuTopLobeParams gpu_params()const;
private:
    CloudRecipe recipe_;
    std::optional<DensityField> single_;
    std::optional<DevelopedEvaluationPlan> developed_;
    std::optional<TopLobeEvaluationPlan> top_;
};
GpuTopLobeParams gpu_scene_density_params(const Scene&);
}
