#include "white/cloud_presets.hpp"
#include "white/anvil_scene.hpp"
#include <iostream>
#include <stdexcept>
using namespace white;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>void reject(F action){try{action();}catch(const std::exception&){return;}throw std::runtime_error("Invalid preset input accepted");}
}
int main(){try{
    check(cloud_presets().size()==4,"Missing minimum preset");
    for(const auto& preset:cloud_presets()){
        CloudPresetRequest request;request.kind=preset.kind;request.stage=.8;
        const auto calm=make_cloud_preset(request);request.wind=PresetWind::upper_shear;const auto windy=make_cloud_preset(request);
        check(calm.initial==windy.initial&&calm.settings.wind!=windy.settings.wind,"Wind example changed the initial source");
        check(make_cloud_preset(request)==windy,"Preset data is not deterministic");
        const auto generated=generate_cloud_state(windy.initial,windy.settings);
        check(generated.status==GenerationStatus::completed,generated.message.c_str());
        const auto repeated=generate_cloud_state(windy.initial,windy.settings);
        check(repeated.candidate->evaluated==generated.candidate->evaluated,"Preset/version/seed did not reproduce the selected state");
        const auto frozen=freeze_candidate(generated);const auto count=generation_job_count();
        const auto detailed=regenerate_fixed_detail(frozen,1234);
        check(generation_job_count()==count&&detailed.frozen->content_hash==frozen.frozen->content_hash,"Detail-only regeneration reran growth or changed fixed geometry");
        auto view=frozen;view.camera.position.x+=1;view.exposure_ev+=1;
        const auto comparison=candidate_comparison_scene(view,detailed);
        check(comparison.camera==view.camera&&comparison.frozen==detailed.frozen&&candidate_scene_unchanged(view,frozen),"Comparison changed candidate geometry or ignored shared view");
        auto copy=windy;copy.settings.wind.back().displacement.x+=1;
        check(copy.settings!=windy.settings&&make_cloud_preset(request)==windy,"Draft copies share mutable settings");
    }
    CloudPresetRequest request;request.kind=CloudPresetKind::multiple;auto draft=make_cloud_preset(request);
    auto& first=draft.initial.developed->cells.front();first.shape.source.modifiers.cuts.push_back({100,{0,20,0},{3,3,3},1});refresh_developed_scene(draft.initial);
    const auto before=*draft.initial.developed;const auto selected=before.cells.back().id;
    const auto variation=make_structure_variation(draft.initial,StructureVariationScope::selected_development,123,selected);
    check(variation.initial.developed->cells.front()==before.cells.front(),"Selected-cell regeneration changed another cell or its cuts");
    check(variation.initial.developed->cells.back().shape.source.parameters.structure_seed==123&&variation.initial.developed->cells.back().id==selected,"Selected regeneration failed to preserve stable identity");
    request.preset_version=2;reject([&]{make_cloud_preset(request);});
    reject([&]{make_structure_variation(draft.initial,StructureVariationScope::selected_development,123,9999);});
    std::cout<<"Versioned presets, calm/wind inputs, selected scope, detail-only no-growth, draft value copies and comparison view PASS\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
