#include "white/editor_ui.hpp"
#include <imgui.h>
#include <iostream>
#include <stdexcept>

namespace white {
void EditorUi::draw_frozen_ui(){
    const auto& current=session.document().scene();if(!current.frozen)return;
    if(!ImGui::CollapsingHeader("Frozen field details",ImGuiTreeNodeFlags_DefaultOpen))return;
    const auto state=*current.frozen;
    ImGui::Text("Frozen stage %.3f",state.selection_value);
    ImGui::Text("Structure %016llx",static_cast<unsigned long long>(state.content_hash));
    ImGui::Text("Generation jobs: %llu",static_cast<unsigned long long>(generation_runs_));
    ImGui::TextWrapped("The selected structure and reference coordinates are fixed. Each field has independent detail controls.");
    ImGui::BeginDisabled(generation_job_.valid()||gizmo_drag_||inspector_drag_||orbit_drag_);
    for(std::size_t index=0;index<state.fields.size();++index){const auto& field=state.fields[index];
        ImGui::PushID(std::to_string(field.development_id).c_str());
        ImGui::Text("%s %llu",state.top_enabled&&index==1?"Top field":"Column",static_cast<unsigned long long>(field.development_id));
        auto layers=field.layers;bool changed=ImGui::Checkbox("Base",&layers.base);
        ImGui::SameLine();changed=ImGui::Checkbox("Macro",&layers.macro)||changed;
        changed=ImGui::Checkbox("Medium",&layers.medium)||changed;ImGui::SameLine();changed=ImGui::Checkbox("Micro",&layers.micro)||changed;
        auto noise=field.recipe.noise;auto seed=field.recipe.detail_seed;
        ImGui::SetNextItemWidth(100);changed=ImGui::InputScalar("Detail seed",ImGuiDataType_U64,&seed)||changed;
        ImGui::SetNextItemWidth(100);changed=ImGui::InputDouble("Medium strength",&noise.medium_strength,0,0,"%.3f")||changed;
        ImGui::SetNextItemWidth(100);changed=ImGui::InputDouble("Erosion m",&noise.micro_erosion,0,0,"%.3f")||changed;
        ImGui::SetNextItemWidth(100);changed=ImGui::InputDouble("Macro warp m",&noise.warp_amplitude,0,0,"%.3f")||changed;
        if(changed)try{apply(scene_with_frozen_detail(session.document().scene(),field.development_id,noise,seed,layers));}catch(const std::exception& e){status_=e.what();}
        ImGui::PopID();
    }
    ImGui::EndDisabled();
    ImGui::TextWrapped("Top detail preserves the lower mask. Flat bases and complete cuts remain final constraints. Anvil detail currently shares its column; separate side/interior masks follow in later work.");
}
void EditorUi::start_freeze_test(){
    auto scene=new_anvil_scene(new_cumulonimbus_scene({}));scene.anvil->cloud.settings.mode=TopLobeMode::children;scene.anvil->settings.enabled=true;refresh_anvil_scene(scene);
    scene.camera.position={45,80,380};scene.camera.target={45,65,0};session.apply(scene);
    auto settings=default_generation_settings(scene);settings.stage=.8;settings.wind.back().displacement={55,0,18};prepare_generation_draft({scene,std::move(settings),{},0},false);
    freeze_test_jobs_=generation_job_count();launch_generation();
}
void EditorUi::freeze_test_step(int frame){
    poll_generation();
    auto check=[](bool okay,const char* message){if(!okay)throw std::runtime_error(message);};
    if(frame==90){
        check(generation_candidate_.has_value()&&!generation_job_.valid(),"Native generation did not finish before Freeze");const auto before=session.document().scene();adopt_generation_candidate();
        const auto fixed=session.document().scene();check(fixed.frozen.has_value(),"Native Freeze failed");
        check(session.undo()&&session.document().scene()==before&&session.redo()&&session.document().scene()==fixed,"Freeze must undo/redo in one command");
        smoke_before_=fixed;save_scene_atomic(fixed,"freeze-first.white.json");
        std::cout<<"freeze_native=adopt completed_candidate=true single_undo=true redo=true generation_jobs="<<generation_runs_<<" PASS\n";
    }
    if(frame==120){
        const auto before=session.document().scene();const auto& top=before.frozen->fields.back();auto noise=top.recipe.noise;noise.medium_strength=.75;
        apply(scene_with_frozen_detail(before,top.development_id,noise,top.recipe.detail_seed+17,top.layers));
        const auto detailed=session.document().scene();check(detailed.frozen->content_hash==before.frozen->content_hash,"Detail regenerated the fixed structure");
        check(session.undo()&&session.document().scene()==before&&session.redo()&&session.document().scene()==detailed,"Detail edit lost atomic Undo/Redo");
        // Change light colour as part of the zero-growth edit check while
        // keeping the finishing captures bright enough for visual review.
        auto view=detailed;view.exposure_ev=.4;view.cloud.optics.albedo=.85;view.camera.position.x+=8;view.sun.irradiance={13.5,15,16.5};apply(view);
        check(generation_job_count()==freeze_test_jobs_+1&&generation_runs_==1,"Detail/camera/light started a growth job");
        smoke_before_=session.document().scene();save_scene_atomic(smoke_before_,"freeze-detail.white.json");
        std::cout<<"freeze_native=finish content_hash_stable=true detail_camera_light_generation_jobs=0 single_undo=true PASS\n";
    }
    if(frame==145)launch_generation(true);
    if(frame==160){
        check(!generation_job_.valid()&&session.document().scene()==smoke_before_,"Cancelled generation replaced the fixed cloud");
        check(generation_job_count()==freeze_test_jobs_+2,"Cancelled native job was not recorded");
        generation_settings_.stage=.45;launch_generation();
        std::cout<<"freeze_native=cancel fixed_cloud_preserved=true retry_started=true PASS\n";
    }
    if(frame==185){
        check(generation_candidate_.has_value()&&!generation_job_.valid(),"Retry did not produce a completed candidate");adopt_generation_candidate();
        const auto changed=session.document().scene();check(changed.frozen->selection_value==.45&&changed.frozen->content_hash!=smoke_before_.frozen->content_hash,"Explicit new candidate did not replace the selected stage");
        check(session.undo()&&session.document().scene()==smoke_before_&&session.redo()&&session.document().scene()==changed,"New candidate adoption lost previous successful state");
        session.save("freeze-smoke.white.json");session.load("freeze-smoke.white.json");
        check(session.document().scene()==changed&&generation_job_count()==freeze_test_jobs_+3,"Frozen reload regenerated or changed the cloud");
        std::cout<<"freeze_native=replace single_undo=true previous_success_retained=true save_reload=true reload_generation_jobs=0 PASS\n";
    }
}
}
