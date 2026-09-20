#include "white/cloud_presets.hpp"
#include "white/anvil_scene.hpp"
#include <algorithm>
#include <array>
#include <stdexcept>

namespace white {
namespace {
constexpr std::array definitions{
    CloudPresetDefinition{CloudPresetKind::cumulonimbus,"cumulonimbus","Cumulonimbus",1,1,80,120,0,1,true,false},
    CloudPresetDefinition{CloudPresetKind::wide,"wide","Wide cloud",1,1,150,75,0,1,false,false},
    CloudPresetDefinition{CloudPresetKind::multiple,"multiple","Two developments",1,1,80,120,70,2,false,false},
    CloudPresetDefinition{CloudPresetKind::anvil,"anvil","Anvil cloud",1,1,90,150,0,1,true,true}
};
std::uint64_t mix(std::uint64_t x){x+=UINT64_C(0x9e3779b97f4a7c15);x=(x^(x>>30))*UINT64_C(0xbf58476d1ce4e5b9);x=(x^(x>>27))*UINT64_C(0x94d049bb133111eb);return x^(x>>31);}
DevelopedCloud& editable(Scene& s){return s.anvil?s.anvil->cloud.trunk:s.top_lobes?s.top_lobes->trunk:*s.developed;}
void require_errors(const std::vector<std::string>& errors){if(!errors.empty())throw std::invalid_argument(errors.front());}
}
std::span<const CloudPresetDefinition> cloud_presets(){return definitions;}
GenerationDraft make_cloud_preset(const CloudPresetRequest& request){
    const auto found=std::find_if(definitions.begin(),definitions.end(),[&](const auto& p){return p.kind==request.kind;});
    if(found==definitions.end()||request.preset_version!=found->version||found->generation_version!=generation_algorithm_version)throw std::invalid_argument("Unsupported cloud preset/version");
    if(request.wind!=PresetWind::calm&&request.wind!=PresetWind::upper_shear)throw std::invalid_argument("Unknown preset wind example");
    const auto& preset=*found;auto initial=new_cumulonimbus_scene();auto& p=initial.cumulonimbus->parameters;
    p.width=preset.width;p.height=preset.height;p.structure_seed=request.structure_seed;p.detail_seed=request.detail_seed;
    initial.cloud=derive_cumulonimbus_recipe(*initial.cumulonimbus);initial=new_developed_scene(initial);
    if(preset.developments==2){
        initial=scene_with_developed_command(initial,DevelopedSetRoles{initial.developed->cells[0].id,{0,1,2}});
        auto second=make_developed_cell(*initial.developed);second.translation={preset.separation,0,8};
        second.shape.source.parameters.width=preset.width*.85;second.shape.source.parameters.height=preset.height*.85;
        second.shape.source.parameters.structure_seed=mix(request.structure_seed);second.shape.source.parameters.detail_seed=request.detail_seed;
        initial=scene_with_developed_command(initial,DevelopedAdd{second});
    }
    if(preset.top_lobes){initial=new_top_lobe_scene(initial);auto top=initial.top_lobes->settings;top.mode=TopLobeMode::children;initial=scene_with_top_lobe_settings(initial,top);}
    if(preset.anvil){initial=new_anvil_scene(initial);auto settings=initial.anvil->settings;settings.enabled=true;initial=scene_with_anvil_settings(initial,settings);}
    auto settings=default_generation_settings(initial);settings.stage=request.stage;
    if(request.wind==PresetWind::upper_shear)settings.wind={{0,{}},{.5,{12,0,4}},{1,{55,0,18}}};
    require_errors(validate_generation(initial,settings));
    return {std::move(initial),std::move(settings),std::string(preset.key),preset.version};
}
GenerationDraft make_structure_variation(const Scene& current,StructureVariationScope scope,std::uint64_t seed,Id selected){
    require_valid(current);if(scope!=StructureVariationScope::whole_cloud&&scope!=StructureVariationScope::selected_development)throw std::invalid_argument("Unknown structure regeneration scope");
    GenerationDraft draft;
    if(current.frozen){draft.initial=generation_initial_scene(*current.frozen);draft.settings=current.frozen->provenance->settings;}
    else {draft.initial=current;draft.settings=default_generation_settings(current);}
    if(!editable_developed_source(draft.initial))draft.initial=new_developed_scene(draft.initial);
    auto& cloud=editable(draft.initial);bool changed=false;
    for(auto& cell:cloud.cells){if(scope==StructureVariationScope::selected_development&&cell.id!=selected)continue;
        cell.shape.source.parameters.structure_seed=scope==StructureVariationScope::selected_development?seed:mix(seed^cell.id);changed=true;
    }
    if(!changed)throw std::invalid_argument("Select a valid development ID before regenerating that scope");
    refresh_developed_scene(draft.initial);require_errors(validate_generation(draft.initial,draft.settings));return draft;
}
Scene regenerate_fixed_detail(Scene scene,std::uint64_t seed,Id target){
    require_valid(scene);if(!scene.frozen)throw std::invalid_argument("Freeze a selected state before detail-only regeneration");
    const auto before=*scene.frozen;bool changed=false;
    for(const auto& field:before.fields){if(target&&field.development_id!=target)continue;
        scene=scene_with_frozen_detail(std::move(scene),field.development_id,field.recipe.noise,target?seed:mix(seed^field.development_id),field.layers);changed=true;
    }
    if(!changed)throw std::invalid_argument("Unknown fixed-field detail target");
    if(scene.frozen->content_hash!=before.content_hash)throw std::logic_error("Detail-only regeneration changed frozen structure");
    return scene;
}
Scene candidate_comparison_scene(const Scene& current,const Scene& candidate){
    require_valid(current);require_valid(candidate);auto result=candidate;
    result.camera=current.camera;result.sun=current.sun;result.exposure_ev=current.exposure_ev;result.preview_approx=current.preview_approx;
    return result;
}
bool candidate_scene_unchanged(const Scene& current,const Scene& guard){
    auto expected=guard;expected.camera=current.camera;expected.sun=current.sun;expected.exposure_ev=current.exposure_ev;expected.preview_approx=current.preview_approx;return current==expected;
}
}
