#include "white/generation.hpp"
#include "white/frozen_cloud.hpp"
#include "white/anvil_scene.hpp"
#include "white/modifiers.hpp"
#include <iostream>
using namespace white;
int main(){
 auto scene=new_anvil_scene(new_cumulonimbus_scene({}));scene.anvil->cloud.settings.mode=TopLobeMode::children;scene.anvil->settings.enabled=true;refresh_anvil_scene(scene);
 auto settings=default_generation_settings(scene);settings.stage=.8;settings.wind.back().displacement={40,0,12};auto fixed=freeze_candidate(generate_cloud_state(scene,settings));
 FinishModifier cut;cut.id=1;cut.target_id=fixed.frozen->id;cut.kind=FinishModifierKind::cut;cut.mask.center={20,55,0};cut.mask.radii={17,25,24};fixed=scene_with_finish_command(fixed,{FinishCommandKind::add,0,cut});
 auto multiply=cut;multiply.id=2;multiply.kind=FinishModifierKind::density;multiply.density_multiplier=3;fixed=scene_with_finish_command(fixed,{FinishCommandKind::add,0,multiply});
 auto protection=cut;protection.id=3;protection.kind=FinishModifierKind::protect_detail;protection.mask.center={15,70,0};protection.mask.radii={25,40,30};fixed=scene_with_finish_command(fixed,{FinishCommandKind::add,0,protection});
 const auto field=fixed.frozen->fields.front();auto noise=field.recipe.noise;noise.medium_strength=.8;noise.micro_erosion=2;fixed=scene_with_frozen_detail(fixed,field.development_id,noise,field.recipe.detail_seed+123,field.layers);
 auto report=[](const char* name,const Scene& value){std::cout<<name<<" bound="<<finish_density_error_bound(*value.frozen,value.finish_stack)<<" limit="<<.005*std::max(1.,value.frozen->rho_max*finish_density_bound(value.finish_stack))<<" errors="<<validate(value).size()<<'\n';};
 report("base",fixed);
 auto reordered=scene_with_finish_command(fixed,{FinishCommandKind::move_up,2,{}});report("reordered",reordered);
 for(Id id:{1,2,3}){auto duplicate=fixed;duplicate.finish_stack=finish_stack_command(duplicate.finish_stack,{FinishCommandKind::duplicate,id,{}});std::cout<<"duplicate "<<id<<" ";report("",duplicate);}
}
