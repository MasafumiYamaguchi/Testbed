#pragma once
#include "white/anvil.hpp"
namespace white {
CloudRecipe anvil_proxy_recipe(const AnvilSource&);
void refresh_anvil_scene(Scene&);
Scene new_anvil_scene(Scene);
Scene scene_with_anvil_settings(Scene,const AnvilSettings&);
enum class AnvilHandle {width,direction};
Vec3 anvil_handle_position(const AnvilSource&,AnvilHandle);
Scene scene_with_anvil_handle(Scene,AnvilHandle,Vec3 object_local);
}
