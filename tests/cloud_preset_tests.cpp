#include "white/cloud_presets.hpp"
#include "white/cloud_clone.hpp"
#include "white/anvil_scene.hpp"
#include "white/modifiers.hpp"
#include "white/persistence.hpp"
#include <nlohmann/json.hpp>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace white;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
double length(Vec3 value){return std::sqrt(dot(value,value));}
template<class F>void reject(F action){try{action();}catch(const std::exception&){return;}throw std::runtime_error("Invalid preset input accepted");}
void draft_lineage_and_motion(){
    CloudPresetRequest request;request.kind=CloudPresetKind::wide;request.stage=.8;
    auto draft=make_cloud_preset(request);draft.settings.reference_translation={30,0,-12};
    draft.initial.developed->transform={{3,7,-2},{0,std::sin(.15),0,std::cos(.15)},{1.1,.9,1.2}};refresh_developed_scene(draft.initial);
    auto current=freeze_candidate(generate_cloud_state(draft.initial,draft.settings));const auto prepared=current;
    const auto field=current.frozen->fields.front();auto noise=field.recipe.noise;noise.medium_strength=.35;noise.warp_amplitude+=2;auto layers=field.layers;layers.micro=false;
    current=scene_with_frozen_detail(std::move(current),field.development_id,noise,991,layers);
    current.frozen->optics.albedo=.6;current.frozen->transform.translation={37,11,-9};current.frozen->transform.scale={1.2,.8,1.1};
    current.frozen->transform.rotation={0,std::sin(.3),0,std::cos(.3)};refresh_frozen_scene(current);
    current.camera.position.x+=7;current.exposure_ev=.5;
    FinishModifier finish;finish.kind=FinishModifierKind::density;finish.target_id=current.frozen->id;finish.mask={{0,40,0},{20,20,20},3};finish.density_multiplier=.7;
    current=scene_with_finish_command(std::move(current),{FinishCommandKind::add,0,finish});
    const auto jobs=generation_job_count();
    check(generation_draft_matches_current(prepared,current),"Prepared draft rejected detail, sampling bounds, layers, optics, view, finishing or instance edits");
    auto changed=current;changed.frozen->fields.front().recipe.cuts.push_back({700,{0,40,0},{8,8,8},1});refresh_frozen_scene(changed);
    check(!generation_draft_matches_current(prepared,changed),"Prepared draft accepted a new structural cut");
    changed=current;changed.frozen->fields.front().recipe.cells.front().radii.x*=.9;refresh_frozen_scene(changed);
    check(!generation_draft_matches_current(prepared,changed),"Prepared draft accepted different evaluated geometry");
    check(!generation_draft_matches_current(prepared,clone_frozen_cloud(current).scene),"Prepared draft accepted replacement identities with the same appearance");
    changed=current;changed.frozen->provenance->settings.reference_translation.x+=1;refresh_frozen_scene(changed);
    check(!generation_draft_matches_current(prepared,changed),"Prepared draft accepted different source provenance");
    changed=current;changed.frozen->provenance.reset();refresh_frozen_scene(changed);
    check(!generation_draft_matches_current(prepared,changed)&&generation_job_count()==jobs,"Prepared draft accepted missing provenance or ran generation");
    auto next=draft.settings;next.stage=.35;next.reference_translation={-14,0,25};
    auto candidate=freeze_candidate(generate_cloud_state(draft.initial,next));
    auto initial_instance=draft.initial;initial_instance.developed->transform=current.cloud.transform;initial_instance.developed->optics=current.cloud.optics;refresh_developed_scene(initial_instance);
    check(generation_draft_matches_current(draft.initial,initial_instance),"Unfrozen initial instance/optics edits invalidated the source draft");
    const auto initial_result=preserve_candidate_finish(initial_instance,candidate);
    check(length(initial_result.cloud.transform.translation-local_to_world(initial_instance.cloud.transform,next.reference_translation*next.stage))<1e-10,"Initial source instance lost the new generation displacement");
    const auto result=preserve_candidate_finish(current,candidate);
    const auto expected=local_to_world(current.cloud.transform,next.reference_translation*next.stage-draft.settings.reference_translation*draft.settings.stage);
    check(length(result.cloud.transform.translation-expected)<1e-10&&result.cloud.transform.rotation==current.cloud.transform.rotation&&result.cloud.transform.scale==current.cloud.transform.scale,"Candidate lost manual TRS or applied the wrong local bulk-motion delta");
    check(result.frozen->fields.front().recipe.noise==noise&&result.frozen->fields.front().layers==layers,"Transform preservation lost field finishing");
    const auto plain=preserve_candidate_finish(prepared,candidate);
    check(length(plain.cloud.transform.translation-candidate.cloud.transform.translation)<1e-10,"Ordinary adoption accumulated or removed legitimate nonzero generation motion");
    auto off=next;off.enabled=false;const auto disabled=freeze_candidate(generate_cloud_state(draft.initial,off));
    const auto disabled_result=preserve_candidate_finish(current,disabled);
    check(length(disabled_result.cloud.transform.translation-local_to_world(current.cloud.transform,draft.settings.reference_translation*(-draft.settings.stage)))<1e-10,"Disabled candidate retained a nonzero generation displacement");
    const auto enabled_result=preserve_candidate_finish(disabled,candidate);
    check(length(enabled_result.cloud.transform.translation-candidate.cloud.transform.translation)<1e-10,"Disabled current state contributed an old generation displacement");
    const auto reject_jobs=generation_job_count();reject([&]{preserve_candidate_finish(changed,candidate);});
    changed=prepared;++changed.frozen->generation_version;++changed.frozen->provenance->settings.algorithm_version;refresh_frozen_scene(changed);
    reject([&]{preserve_candidate_finish(changed,candidate);});
    check(generation_job_count()==reject_jobs,"Unknown/history-free motion rejection invoked generation");
    std::cout<<"preset_draft_lineage structure_and_provenance_guard=true mutable_finish_allowed=true manual_trs=true bulk_motion_delta=true disabled_motion_zero=true PASS\n";
}
void finishing_integration(){
    CloudPresetRequest request;request.kind=CloudPresetKind::multiple;request.stage=.8;request.wind=PresetWind::upper_shear;
    auto draft=make_cloud_preset(request);draft.initial.developed->cells.front().shape.source.modifiers.cuts.push_back({100,{0,20,0},{3,3,3},1});refresh_developed_scene(draft.initial);
    auto current=freeze_candidate(generate_cloud_state(draft.initial,draft.settings));
    FinishModifier density;density.id=40;density.kind=FinishModifierKind::density;density.target_id=current.frozen->id;density.strength=.7;density.density_multiplier=1.4;density.mask={{20,55,0},{35,40,30},8};
    auto cut=density;cut.id=20;cut.kind=FinishModifierKind::cut;cut.target_kind=FinishTargetKind::field;cut.target_id=current.frozen->fields.back().development_id;cut.strength=.35;cut.hard_cut=false;cut.enabled=false;
    auto protect=density;protect.id=30;protect.kind=FinishModifierKind::protect_detail;protect.target_kind=FinishTargetKind::field;protect.target_id=current.frozen->fields.front().development_id;protect.strength=.6;
    current.finish_stack.layers={cut,protect,density};require_valid(current);
    auto field=current.frozen->fields.front();field.layers.micro=false;field.recipe.noise.medium_strength=.6;
    current=scene_with_frozen_detail(current,field.development_id,field.recipe.noise,field.recipe.detail_seed,field.layers);
    current.frozen->optics.albedo=.6;refresh_frozen_scene(current);
    const auto original=current;const auto stack=current.finish_stack;const auto jobs=generation_job_count();
    const auto selected=current.frozen->fields.back().development_id;
    const auto detail=regenerate_fixed_detail(current,9876,selected);
    check(detail.finish_stack==stack&&detail.frozen->content_hash==current.frozen->content_hash&&detail.frozen->fields.front()==current.frozen->fields.front(),"Scoped detail changed finishing graph or the unselected fixed field");
    auto expected=current.frozen->fields.back();expected.recipe.detail_seed=9876;
    check(detail.frozen->fields.back()==expected&&generation_job_count()==jobs&&current==original,"Detail-only scope changed mask/field settings or ran generation");
    const auto all_detail=regenerate_fixed_detail(current,54321);
    check(all_detail.finish_stack==stack&&all_detail.frozen->content_hash==current.frozen->content_hash&&generation_job_count()==jobs,"Whole detail regeneration dropped finishing or ran growth");
    auto view=current;view.camera.position.x+=7;view.sun.direction_to_light={0,.8,.6};view.exposure_ev+=.5;
    check(candidate_scene_unchanged(view,current),"Shared-view change unexpectedly invalidated finished candidate guard");
    auto edited=current;edited.finish_stack.layers.front().enabled=true;require_valid(edited);
    check(!candidate_scene_unchanged(edited,current),"Finishing edits after generation escaped candidate adoption guard");
    for(auto scope:{StructureVariationScope::selected_development,StructureVariationScope::whole_cloud}){
        const auto before_jobs=generation_job_count();const auto variation=make_structure_variation(current,scope,12345,selected);
        check(generation_job_count()==before_jobs&&current==original,"Preparing a finished variation mutated Current or ran generation");
        if(scope==StructureVariationScope::selected_development)check(variation.initial.developed->cells.front()==draft.initial.developed->cells.front(),"Selected variation changed an unselected development or saved cuts");
        const auto candidate=freeze_candidate(generate_cloud_state(variation.initial,variation.settings));const auto candidate_hash=candidate.frozen->content_hash;
        const auto preserved=preserve_candidate_finish(current,candidate);
        const auto adopted=adopt_frozen_with_finish(current,preserved);
        const auto comparison=candidate_comparison_scene(view,adopted);
        check(comparison.finish_stack==stack&&comparison.frozen->content_hash==candidate_hash&&current==original,"Variation preview/adoption lost exact finishing order, settings or target identities");
        check(comparison.camera==view.camera&&comparison.sun==view.sun&&comparison.exposure_ev==view.exposure_ev&&comparison.frozen->optics==current.frozen->optics,"Finished variation lost shared view or current optics");
        for(std::size_t i=0;i<current.frozen->fields.size();++i){const auto& a=current.frozen->fields[i];const auto& b=comparison.frozen->fields[i];check(a.development_id==b.development_id&&a.recipe.noise==b.recipe.noise&&a.recipe.detail_seed==b.recipe.detail_seed&&a.layers==b.layers&&a.recipe.cuts==b.recipe.cuts,"Variation adoption lost field detail, layer toggles, cuts or local references");}
        check(generation_job_count()==before_jobs+1,"Finishing preservation or comparison ran an additional generation job");
        EditorSession session(current);session.apply(comparison);check(session.undo()&&session.document().scene()==original&&session.redo()&&session.document().scene()==comparison,"Finished candidate adoption lost single-command Undo/Redo");
        check(parse_scene_json(scene_json(comparison))==comparison,"Adopted finishing graph did not roundtrip exactly");
    }
    std::cout<<"preset_finish_integration scoped_detail=true whole_detail=true scoped_structure=true whole_structure=true exact_stack=true single_undo=true no_implicit_growth=true PASS\n";
}
void fixtures(const std::filesystem::path& directory){
    std::filesystem::create_directories(directory);
    nlohmann::json manifest={{"preset_version",cloud_preset_version},{"algorithm_version",generation_algorithm_version},{"structure_seed","42"},{"detail_seed","17"},{"stage",.8},{"stage_unit","dimensionless"},{"wind_unit","object-local metres at stage 1; not metres/second"},{"initial_hash_format","FNV-1a-64 of canonical scene_json(initial) bytes"},{"fixtures",nlohmann::json::array()}};
    for(const auto& preset:cloud_presets())for(const auto wind:{PresetWind::calm,PresetWind::upper_shear}){
        CloudPresetRequest request;request.kind=preset.kind;request.stage=.8;request.wind=wind;
        const auto draft=make_cloud_preset(request);auto fixed=freeze_candidate(generate_cloud_state(draft.initial,draft.settings));
        std::uint64_t initial_hash=UINT64_C(14695981039346656037);for(const unsigned char c:scene_json(draft.initial)){initial_hash^=c;initial_hash*=UINT64_C(1099511628211);}
        fixed.camera.position={40,90,390};fixed.camera.target={40,65,0};
        const auto name=std::string(preset.key)+(wind==PresetWind::calm?"-calm":"-wind");const auto filename=name+".white.json";
        save_scene_atomic(fixed,directory/filename);check(read_scene(directory/filename)==fixed,"Preset fixture changed during persistence roundtrip");
        manifest["fixtures"].push_back({{"name",name},{"preset",preset.key},{"wind",wind==PresetWind::calm?"calm":"upper_shear"},{"recipe",filename},{"initial_hash",std::to_string(initial_hash)},{"content_hash",std::to_string(fixed.frozen->content_hash)},{"payload_hash",std::to_string(fixed.frozen->payload_hash)}});
    }
    std::ofstream output(directory/"preset-manifest.json");output<<manifest.dump(2)<<'\n';if(!output)throw std::runtime_error("Cannot write preset fixture manifest");
}
}
int main(int argc,char** argv){try{
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
        auto view=frozen;view.camera.position.x+=1;view.exposure_ev+=1;view.sun.direction_to_light={0,.8,.6};view.preview_approx.enabled=!view.preview_approx.enabled;
        const auto comparison=candidate_comparison_scene(view,detailed);
        check(comparison.camera==view.camera&&comparison.sun==view.sun&&comparison.exposure_ev==view.exposure_ev&&comparison.preview_approx==view.preview_approx&&comparison.frozen==detailed.frozen&&candidate_scene_unchanged(view,frozen),"Comparison changed candidate geometry or ignored shared view");
        check(!candidate_scene_unchanged(detailed,frozen),"Detail edits after generation escaped the adoption guard");
        auto changed=frozen;changed.frozen->transform.translation.x+=1;refresh_frozen_scene(changed);
        check(!candidate_scene_unchanged(changed,frozen),"Structural edits after generation escaped the adoption guard");
        auto copy=windy;copy.settings.wind.back().displacement.x+=1;
        check(copy.settings!=windy.settings&&make_cloud_preset(request)==windy,"Draft copies share mutable settings");
        auto finished=detailed;finished.frozen->fields.front().layers.micro=false;finished.frozen->optics.albedo=.6;refresh_frozen_scene(finished);
        const auto source_geometry=frozen.frozen->content_hash;const auto preserve_jobs=generation_job_count();
        const auto preserved=preserve_candidate_finish(finished,frozen);
        check(preserved.frozen->content_hash==source_geometry&&generation_job_count()==preserve_jobs,"Finish preservation regenerated or changed candidate geometry");
        check(preserved.frozen->fields.front().recipe.detail_seed==finished.frozen->fields.front().recipe.detail_seed&&preserved.frozen->fields.front().layers==finished.frozen->fields.front().layers&&preserved.frozen->optics==finished.frozen->optics,"Normal candidate adoption lost detail/layer/optics finishing");
    }
    CloudPresetRequest request;request.kind=CloudPresetKind::multiple;auto draft=make_cloud_preset(request);
    auto& first=draft.initial.developed->cells.front();first.shape.source.modifiers.cuts.push_back({100,{0,20,0},{3,3,3},1});refresh_developed_scene(draft.initial);
    const auto before=*draft.initial.developed;const auto selected=before.cells.back().id;
    const auto variation=make_structure_variation(draft.initial,StructureVariationScope::selected_development,123,selected);
    check(variation.initial.developed->cells.front()==before.cells.front(),"Selected-cell regeneration changed another cell or its cuts");
    check(variation.initial.developed->cells.back().shape.source.parameters.structure_seed==123&&variation.initial.developed->cells.back().id==selected,"Selected regeneration failed to preserve stable identity");
    request.preset_version=2;reject([&]{make_cloud_preset(request);});
    reject([&]{make_structure_variation(draft.initial,StructureVariationScope::selected_development,123,9999);});
    finishing_integration();draft_lineage_and_motion();
    if(argc>1)fixtures(argv[1]);
    std::cout<<"Versioned presets, calm/wind inputs, selected scope, detail-only no-growth, draft value copies and comparison view PASS\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
