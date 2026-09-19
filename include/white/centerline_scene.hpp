#pragma once
#include "white/centerline.hpp"
namespace white {
const CumulonimbusGroup* prefab_source(const Scene&);
Scene new_centerline_scene(Scene scene={});
Scene scene_with_centerline_command(Scene,const CenterlineCommand&);
CenterlineShape edit_centerline_recipe(const CenterlineShape&,const CloudRecipe&);
}
