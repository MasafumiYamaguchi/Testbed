#include "white/developed_scene.hpp"
#include "white/top_lobe_scene.hpp"
#include "white/anvil_scene.hpp"
#include <stdexcept>

namespace white {
CloudRecipe developed_proxy_recipe(const DevelopedCloud& source) {
    if(source.cells.size()<=1)return lower_single_developed_recipe(source);
    const DevelopedEvaluationPlan full(source);auto first=full.cloud();first.cells.resize(1);
    auto recipe=lower_single_developed_recipe(first);recipe.envelope=full.local_support();return recipe;
}
const TopLobeSource* editable_top_lobe_source(const Scene& scene){if(scene.anvil)return &scene.anvil->cloud;return scene.top_lobes?&*scene.top_lobes:nullptr;}
const DevelopedCloud* editable_developed_source(const Scene& scene){if(const auto* top=editable_top_lobe_source(scene))return &top->trunk;return scene.developed?&*scene.developed:nullptr;}
bool scene_has_multiple_developments(const Scene& scene){const auto source=editable_developed_source(scene);return source&&source->cells.size()>1;}
bool scene_has_active_top_lobes(const Scene& scene){const auto* top=editable_top_lobe_source(scene);return top&&TopLobeEvaluationPlan(*top).gpu_params().mask.x!=0;}
bool scene_has_active_anvil(const Scene& scene){return scene.anvil&&AnvilEvaluationPlan(*scene.anvil).gpu_params().settings.x!=0;}
bool scene_density_requires_direct(const Scene& scene){
    if(scene.frozen)return scene.frozen->fields.size()>1||scene.frozen->anvil.has_value();
    return scene_has_multiple_developments(scene)||scene_has_active_top_lobes(scene)||scene_has_active_anvil(scene);
}
void refresh_developed_scene(Scene& scene) {
    if(scene.anvil){refresh_anvil_scene(scene);return;}
    if(scene.top_lobes){refresh_top_lobe_scene(scene);return;}
    if(!scene.developed)throw std::invalid_argument("Scene has no developed source");
    auto next=scene;
    for(auto& cell:next.developed->cells)cell.shape.source.modifiers.optics=next.developed->optics;
    next.cloud=developed_proxy_recipe(*next.developed);require_valid(next);scene=std::move(next);
}
Scene new_developed_scene(Scene scene) {
    if(scene.anvil){require_valid(scene);if(scene.anvil->settings.enabled)throw std::invalid_argument("Disable anvil before converting to independent cells");scene.top_lobes=scene.anvil->cloud;scene.anvil.reset();}
    if(scene.top_lobes){
        require_valid(scene);if(scene.top_lobes->settings.mode!=TopLobeMode::off)throw std::invalid_argument("Disable top lobes explicitly before converting to developed cells");
        scene.developed=scene.top_lobes->trunk;scene.top_lobes.reset();refresh_developed_scene(scene);return scene;
    }
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
    if(editable_top_lobe_source(scene))return scene_with_top_lobe_command(std::move(scene),TopLobeTrunkCommand{command});
    if(!scene.developed)throw std::invalid_argument("Scene has no developed source");
    scene.developed=command_developed_cloud(*scene.developed,command);refresh_developed_scene(scene);return scene;
}
SceneDensityEvaluator::SceneDensityEvaluator(const Scene& scene):recipe_(scene.cloud) {
    require_valid(scene);
    if(scene.frozen)frozen_.emplace(*scene.frozen);
    else if(scene_has_active_anvil(scene))anvil_.emplace(*scene.anvil);
    else if(scene_has_active_top_lobes(scene))top_.emplace(*editable_top_lobe_source(scene));
    else if(scene_has_multiple_developments(scene))developed_.emplace(*editable_developed_source(scene));else single_.emplace(recipe_);
}
double SceneDensityEvaluator::at(Vec3 local)const{return frozen_?frozen_->at(local):anvil_?anvil_->at(local):top_?top_->at(local):developed_?developed_->at(local):single_->at(local);}
double SceneDensityEvaluator::maximum()const{return frozen_?frozen_->maximum():anvil_?anvil_->maximum():top_?top_->maximum():developed_?developed_->maximum():single_->maximum();}
Bounds SceneDensityEvaluator::local_support()const{return frozen_?frozen_->local_support():anvil_?anvil_->local_support():top_?top_->local_support():developed_?developed_->local_support():single_->local_support();}
Bounds SceneDensityEvaluator::world_support()const{return frozen_?frozen_->world_support():anvil_?anvil_->world_support():top_?top_->world_support():developed_?developed_->world_support():single_->world_support();}
GpuAnvilParams SceneDensityEvaluator::gpu_params()const {
    if(frozen_)return frozen_->gpu_params();
    if(anvil_)return anvil_->gpu_params();
    GpuAnvilParams result;
    if(top_)result.cloud=top_->gpu_params();
    else if(developed_)result.cloud={developed_->gpu_params(),{}};
    else{
        auto& out=result.cloud.fields;out.groups[0]=gpu_density_params(*single_);const auto bounds=single_->local_support();
        out.envelope_min={float(bounds.min.x),float(bounds.min.y),float(bounds.min.z),0};out.envelope_max={float(bounds.max.x),float(bounds.max.y),float(bounds.max.z),0};
        out.settings={1,0,0,float(single_->maximum())};
    }
    result.envelope_min=result.cloud.fields.envelope_min;result.envelope_max=result.cloud.fields.envelope_max;return result;
}
GpuAnvilParams gpu_scene_density_params(const Scene& scene){return SceneDensityEvaluator(scene).gpu_params();}
}
