#include "white/top_lobe_scene.hpp"
#include "white/anvil_scene.hpp"
#include <stdexcept>
namespace white {
CloudRecipe top_lobe_proxy_recipe(const TopLobeSource& source) {
    const TopLobeEvaluationPlan plan(source);auto proxy=developed_proxy_recipe(source.trunk);
    if(plan.gpu_params().mask.x!=0)proxy.envelope=plan.local_support();
    return proxy;
}
void refresh_top_lobe_scene(Scene& scene) {
    if(scene.anvil){refresh_anvil_scene(scene);return;}
    if(!scene.top_lobes)throw std::invalid_argument("Scene has no top-lobe source");
    auto next=scene;for(auto& cell:next.top_lobes->trunk.cells)cell.shape.source.modifiers.optics=next.top_lobes->trunk.optics;
    next.cloud=top_lobe_proxy_recipe(*next.top_lobes);require_valid(next);scene=std::move(next);
}
Scene new_top_lobe_scene(Scene scene,Id target) {
    if(scene.anvil){require_valid(scene);return scene;}
    if(scene.top_lobes){require_valid(scene);return scene;}
    scene=new_developed_scene(std::move(scene));
    if(!target){if(scene.developed->cells.empty())throw std::invalid_argument("Select a development before adding top lobes");target=scene.developed->cells.front().id;}
    scene.top_lobes=make_top_lobe_source(*scene.developed,target);scene.developed.reset();refresh_top_lobe_scene(scene);return scene;
}
Scene scene_with_top_lobe_command(Scene scene,const TopLobeCommand& command) {
    if(scene.anvil){scene.anvil->cloud=command_top_lobes(scene.anvil->cloud,command);refresh_anvil_scene(scene);return scene;}
    if(!scene.top_lobes)throw std::invalid_argument("Scene has no top-lobe source");
    scene.top_lobes=command_top_lobes(*scene.top_lobes,command);refresh_top_lobe_scene(scene);return scene;
}
Scene scene_with_top_lobe_settings(Scene scene,const TopLobeSettings& settings){return scene_with_top_lobe_command(std::move(scene),TopLobeSettingsCommand{settings});}
}
