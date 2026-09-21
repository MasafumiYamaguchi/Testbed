#include "white/anvil_scene.hpp"
#include "white/top_lobe_scene.hpp"
#include <stdexcept>
#include <cmath>
namespace white {
CloudRecipe anvil_proxy_recipe(const AnvilSource& source){const AnvilEvaluationPlan plan(source);auto proxy=top_lobe_proxy_recipe(source.cloud);proxy.envelope=plan.local_support();return proxy;}
void refresh_anvil_scene(Scene& scene){
    if(!scene.anvil)throw std::invalid_argument("Scene has no anvil source");
    auto next=scene;for(auto& cell:next.anvil->cloud.trunk.cells)cell.shape.source.modifiers.optics=next.anvil->cloud.trunk.optics;
    next.cloud=anvil_proxy_recipe(*next.anvil);require_valid(next);scene=std::move(next);
}
Scene new_anvil_scene(Scene scene){
    if(scene.anvil){require_valid(scene);return scene;}
    scene=new_top_lobe_scene(std::move(scene));scene.anvil=make_anvil_source(*scene.top_lobes);scene.top_lobes.reset();refresh_anvil_scene(scene);return scene;
}
Scene scene_with_anvil_settings(Scene scene,const AnvilSettings& settings){
    if(!scene.anvil)throw std::invalid_argument("Scene has no anvil source");
    scene.anvil->settings=settings;refresh_anvil_scene(scene);return scene;
}
Vec3 anvil_handle_position(const AnvilSource& source,AnvilHandle handle){
    const auto& s=source.settings;const auto a=anvil_connection_point(source);
    return a+(handle==AnvilHandle::width?Vec3{-s.direction.z,0,s.direction.x}*(s.width*.5):s.direction*(s.width*.5+s.extension));
}
Scene scene_with_anvil_handle(Scene scene,AnvilHandle handle,Vec3 position){
    if(!scene.anvil)throw std::invalid_argument("Scene has no anvil source");
    auto s=scene.anvil->settings;auto delta=position-anvil_connection_point(*scene.anvil);delta.y=0;
    if(handle==AnvilHandle::width)s.width=2*std::abs(dot(delta,Vec3{-s.direction.z,0,s.direction.x}));
    else{const double length=std::hypot(delta.x,delta.z);if(length<1e-6)throw std::invalid_argument("Direction handle is too close to its anchor");s.direction=delta*(1/length);s.follow_wind=false;}
    return scene_with_anvil_settings(std::move(scene),s);
}
}
