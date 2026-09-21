#include "white/developed_scene.hpp"
#include <stdexcept>

namespace white {
CloudRecipe developed_proxy_recipe(const DevelopedCloud& source) {
    if(source.cells.size()<=1)return lower_single_developed_recipe(source);
    const DevelopedEvaluationPlan full(source);auto first=full.cloud();first.cells.resize(1);
    auto recipe=lower_single_developed_recipe(first);recipe.envelope=full.local_support();return recipe;
}
bool scene_has_multiple_developments(const Scene& scene){return scene.developed&&scene.developed->cells.size()>1;}
bool scene_density_requires_direct(const Scene& scene){return scene_has_multiple_developments(scene);}
void refresh_developed_scene(Scene& scene) {
    if(!scene.developed)throw std::invalid_argument("Scene has no developed source");
    auto next=scene;
    for(auto& cell:next.developed->cells)cell.shape.source.modifiers.optics=next.developed->optics;
    next.cloud=developed_proxy_recipe(*next.developed);require_valid(next);scene=std::move(next);
}
Scene new_developed_scene(Scene scene) {
    if(scene.developed){require_valid(scene);return scene;}
    if(scene.centerline)scene.developed=develop_centerline(*scene.centerline);
    else if(scene.cumulonimbus)scene.developed=develop_cumulonimbus(*scene.cumulonimbus);
    else {
        // A Custom Recipe has no unambiguous inverse source representation.
        throw std::invalid_argument("Create a Cumulonimbus or centerline before enabling independent developments");
    }
    scene.centerline.reset();scene.cumulonimbus.reset();refresh_developed_scene(scene);return scene;
}
Scene scene_with_developed_command(Scene scene,const DevelopedCommand& command) {
    if(!scene.developed)throw std::invalid_argument("Scene has no developed source");
    scene.developed=command_developed_cloud(*scene.developed,command);refresh_developed_scene(scene);return scene;
}
SceneDensityEvaluator::SceneDensityEvaluator(const Scene& scene):recipe_(scene.cloud) {
    require_valid(scene);
    if(scene_has_multiple_developments(scene))developed_.emplace(*scene.developed);else single_.emplace(recipe_);
}
double SceneDensityEvaluator::at(Vec3 local)const{return developed_?developed_->at(local):single_->at(local);}
double SceneDensityEvaluator::maximum()const{return developed_?developed_->maximum():single_->maximum();}
Bounds SceneDensityEvaluator::local_support()const{return developed_?developed_->local_support():single_->local_support();}
Bounds SceneDensityEvaluator::world_support()const{return developed_?developed_->world_support():single_->world_support();}
GpuDevelopedParams SceneDensityEvaluator::gpu_params()const {
    if(developed_)return developed_->gpu_params();
    GpuDevelopedParams out;out.groups[0]=gpu_density_params(*single_);const auto bounds=single_->local_support();
    out.envelope_min={float(bounds.min.x),float(bounds.min.y),float(bounds.min.z),0};out.envelope_max={float(bounds.max.x),float(bounds.max.y),float(bounds.max.z),0};
    out.settings={1,0,0,float(single_->maximum())};return out;
}
GpuDevelopedParams gpu_scene_density_params(const Scene& scene){return SceneDensityEvaluator(scene).gpu_params();}
}
