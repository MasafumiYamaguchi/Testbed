#include "white/centerline_scene.hpp"
#include <algorithm>
#include <stdexcept>

namespace white {
const CumulonimbusGroup* prefab_source(const Scene& scene) {
    if(scene.centerline)return &scene.centerline->source;
    return scene.cumulonimbus?&*scene.cumulonimbus:nullptr;
}
Scene new_centerline_scene(Scene scene) {
    if(scene.developed||scene.top_lobes)throw std::invalid_argument("Edit the selected development centerline in its source controls");
    if(scene.centerline)return scene;
    if(!scene.cumulonimbus)scene=new_cumulonimbus_scene(std::move(scene));
    CenterlineShape shape;shape.source=*scene.cumulonimbus;
    shape.profile={{1,0,1,1},{2,.35,1,1},{3,.65,1,1},{4,1,1,1}};
    scene.cumulonimbus.reset();scene.centerline=std::move(shape);
    scene.cloud=lower_centerline_to_recipe(*scene.centerline);require_valid(scene);return scene;
}
Scene scene_with_centerline_command(Scene scene,const CenterlineCommand& command) {
    if(!scene.centerline)throw std::invalid_argument("Scene has no editable centerline");
    scene.centerline=command_centerline(*scene.centerline,command);
    scene.cloud=lower_centerline_to_recipe(*scene.centerline);require_valid(scene);return scene;
}
CenterlineShape edit_centerline_recipe(const CenterlineShape& source,const CloudRecipe& requested) {
    const auto before=lower_centerline_to_recipe(source);
    if(requested.altitude_density!=before.altitude_density)throw std::invalid_argument("Edit the centerline density profile through its source controls");
    if(requested.envelope!=before.envelope)throw std::invalid_argument("Centerline support is derived; convert to Custom Cloud to edit it");
    auto uncurved=derive_cumulonimbus_recipe(source.source);
    auto adjusted=requested;adjusted.envelope=uncurved.envelope;adjusted.altitude_density=uncurved.altitude_density;
    for(auto id:source.source.cell_ids) {
        const auto old=std::find_if(before.cells.begin(),before.cells.end(),[&](const auto& c){return c.id==id;});
        const auto base=std::find_if(uncurved.cells.begin(),uncurved.cells.end(),[&](const auto& c){return c.id==id;});
        const auto wanted=std::find_if(adjusted.cells.begin(),adjusted.cells.end(),[&](const auto& c){return c.id==id;});
        if(wanted==adjusted.cells.end())throw std::invalid_argument("Convert to Custom Cloud before deleting a generated centerline cell");
        wanted->center=base->center+(wanted->center-old->center);
        wanted->radii={base->radii.x*(wanted->radii.x/old->radii.x),base->radii.y*(wanted->radii.y/old->radii.y),base->radii.z*(wanted->radii.z/old->radii.z)};
    }
    auto result=source;result.source=edit_cumulonimbus_recipe(source.source,adjusted);
    (void)lower_centerline_to_recipe(result);return result;
}
}
