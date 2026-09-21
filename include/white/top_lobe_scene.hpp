#pragma once
#include "white/developed_scene.hpp"
namespace white {
CloudRecipe top_lobe_proxy_recipe(const TopLobeSource&);
void refresh_top_lobe_scene(Scene&);
Scene new_top_lobe_scene(Scene,Id target=0);
Scene scene_with_top_lobe_command(Scene,const TopLobeCommand&);
Scene scene_with_top_lobe_settings(Scene,const TopLobeSettings&);
}
