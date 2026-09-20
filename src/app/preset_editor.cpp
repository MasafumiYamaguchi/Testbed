#include "white/editor_ui.hpp"
#include <imgui.h>
#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace white {
namespace {constexpr std::uint64_t candidate_revision_bit=UINT64_C(1)<<63;}
void EditorUi::prepare_generation_draft(GenerationDraft draft,bool resets_manual_edits){
    if(generation_job_.valid())throw std::logic_error("Wait for or cancel the running candidate before changing its draft");
    require_valid(draft.initial);const auto errors=validate_generation(draft.initial,draft.settings);if(!errors.empty())throw std::invalid_argument(errors.front());
    if(scene_json(draft.initial).size()>max_scene_bytes)throw std::invalid_argument("Candidate draft exceeds the scene resource budget");
    if(generation_draft_token_==std::numeric_limits<std::uint64_t>::max())throw std::overflow_error("Candidate draft identity namespace exhausted");
    ++generation_draft_token_;
    generation_initial_=std::move(draft.initial);generation_settings_=std::move(draft.settings);generation_draft_resets_edits_=resets_manual_edits;
    generation_preset_label_=draft.preset_key.empty()?"Seed variation":draft.preset_key+" / v"+std::to_string(draft.preset_version);
    generation_preview_candidate_=false;generation_message_="Draft prepared. Generate a candidate to compare; the current cloud is unchanged.";
}
Scene EditorUi::generation_candidate_view()const{
    if(!generation_candidate_fixed_)throw std::logic_error("No completed frozen candidate is available");
    // A clone already owns a copied/remapped finish stack. Reapplying the old
    // stack here would replace its new target IDs with the original IDs.
    if(generation_candidate_is_clone_)return candidate_comparison_scene(session.document().scene(),*generation_candidate_fixed_);
    const auto& current=session.document().scene();
    // A fresh preset may reuse numeric IDs for different source fields. Only
    // identity-preserving variations can inherit the current finishing stack.
    if(generation_candidate_resets_edits_&&!current.finish_stack.layers.empty())
        throw std::logic_error("Clear the current finishing layers before replacing the cloud with a preset. The current cloud is retained.");
    return candidate_comparison_scene(current,adopt_frozen_with_finish(current,*generation_candidate_fixed_));
}
void EditorUi::publish_editor_preview(GpuSpike& gpu){
    if(candidate_preview_active())try{
        auto preview=generation_candidate_view();
        if(!generation_preview_scene_||*generation_preview_scene_!=preview||last_scene_attempt_==0){
            if(session.document().revision()>=candidate_revision_bit||generation_preview_epoch_>=candidate_revision_bit-1)throw std::overflow_error("Candidate render revision namespace exhausted");
            const auto revision=candidate_revision_bit+(++generation_preview_epoch_);
            gpu.set_scene(preview,revision,std::chrono::steady_clock::now());generation_preview_scene_=std::move(preview);last_scene_attempt_=session.document().revision();
        }
        return;
    }catch(const std::exception& e){generation_preview_candidate_=false;status_=std::string("Candidate preview rejected; returning to current: ")+e.what();}
    if(generation_preview_scene_){generation_preview_scene_.reset();last_scene_attempt_=0;}
    if(session.document().revision()!=gpu.scene_revision&&session.document().revision()!=last_scene_attempt_){
        last_scene_attempt_=session.document().revision();
        try{gpu.set_scene(session.document().scene(),session.document().revision(),session.document().changed_at());}
        catch(const std::exception& e){status_=std::string("Preview stale; document kept: ")+e.what();}
    }
}
void EditorUi::draw_preset_ui(){
    if(!ImGui::CollapsingHeader("Presets and seed scope"))return;
    ImGui::BeginDisabled(generation_job_.valid()||gizmo_drag_||inspector_drag_||orbit_drag_);
    ImGui::SetNextItemWidth(-1);ImGui::Combo("Preset",&preset_kind_,"Cumulonimbus\0Wide cloud\0Two developments\0Anvil cloud\0");
    ImGui::SetNextItemWidth(-1);ImGui::Combo("Wind example",&preset_wind_,"Calm\0Upper shear\0");
    ImGui::SetNextItemWidth(100);ImGui::InputScalar("Structure seed",ImGuiDataType_U64,&preset_structure_seed_);
    ImGui::SetNextItemWidth(100);ImGui::InputScalar("Detail seed",ImGuiDataType_U64,&preset_detail_seed_);
    ImGui::SetNextItemWidth(100);ImGui::InputDouble("Preset stage 0..1",&preset_stage_,0,0,"%.3f");
    if(ImGui::Button("Prepare preset draft"))try{
        prepare_generation_draft(make_cloud_preset({CloudPresetKind(preset_kind_),cloud_preset_version,preset_structure_seed_,preset_detail_seed_,preset_stage_,PresetWind(preset_wind_)}),true);
    }catch(const std::exception& e){generation_message_=e.what();}
    ImGui::TextWrapped("Preset v1 uses the same generation algorithm as manual editing. Calm and upper shear start from the same source and seed.");
    ImGui::Separator();ImGui::SetNextItemWidth(-1);
    ImGui::Combo("Regeneration scope",&variation_scope_,"Whole structure\0Selected development\0Fixed detail only\0");
    ImGui::SetNextItemWidth(100);ImGui::InputScalar("Variation seed",ImGuiDataType_U64,&variation_seed_);
    const auto& current=session.document().scene();
    std::vector<Id> choices;
    if(variation_scope_==2&&current.frozen)for(const auto& field:current.frozen->fields)choices.push_back(field.development_id);
    else if(current.frozen)for(const auto& curve:current.frozen->curves)choices.push_back(curve.development_id);
    else if(const auto* source=editable_developed_source(current))for(const auto& cell:source->cells)choices.push_back(cell.id);
    else if(prefab_source(current))try{const auto prepared=new_developed_scene(current);for(const auto& cell:prepared.developed->cells)choices.push_back(cell.id);}catch(const std::exception&){}
    if(variation_scope_==1&&!choices.empty()&&std::find(choices.begin(),choices.end(),variation_target_)==choices.end())variation_target_=choices.front();
    if(variation_scope_!=0){const auto label=variation_target_?std::to_string(variation_target_):"All fields";
        if(ImGui::BeginCombo("Target ID",label.c_str())){
            if(variation_scope_==2&&ImGui::Selectable("All fixed fields",variation_target_==0))variation_target_=0;
            for(auto id:choices)if(ImGui::Selectable(std::to_string(id).c_str(),variation_target_==id))variation_target_=id;
            ImGui::EndCombo();
        }
    }
    ImGui::BeginDisabled(candidate_preview_active());
    ImGui::BeginDisabled(!current.frozen);
    if(ImGui::Button("Clone current as candidate"))try{clone_current_candidate();}catch(const std::exception& e){generation_message_=e.what();}
    ImGui::EndDisabled();
    ImGui::TextWrapped("Clone copies the current fixed appearance with new IDs and remapped targets. Future growth uses its new identity and may differ from the original; no growth runs when copying.");
    if(variation_scope_==2){
        ImGui::BeginDisabled(!current.frozen);
        if(ImGui::Button("Regenerate fixed detail"))try{apply(regenerate_fixed_detail(current,variation_seed_,variation_target_));generation_message_="Fixed detail seed changed. Structure, selected stage and growth job count are unchanged.";}catch(const std::exception& e){generation_message_=e.what();}
        ImGui::EndDisabled();
    }else if(ImGui::Button("Prepare structural variation"))try{
        prepare_generation_draft(make_structure_variation(current,variation_scope_==0?StructureVariationScope::whole_cloud:StructureVariationScope::selected_development,variation_seed_,variation_target_),false);
    }catch(const std::exception& e){generation_message_=e.what();}
    ImGui::EndDisabled();
    ImGui::TextWrapped("Structure variation retains saved cuts and IDs. It prepares a separate candidate; detail-only never runs growth.");
    if(!generation_preset_label_.empty())ImGui::TextWrapped("Draft: %s",generation_preset_label_.c_str());
    ImGui::TextWrapped("Budget: current cloud plus one candidate. Successful generation replaces that candidate; failed or cancelled work retains it. One render target is shared for comparison.");
    ImGui::EndDisabled();
}
void EditorUi::start_preset_test(){
    CloudPresetRequest request;request.stage=.8;auto calm=make_cloud_preset(request);preset_test_jobs_=generation_job_count();
    auto original=freeze_candidate(generate_cloud_state(calm.initial,calm.settings));original.camera.position={30,80,350};original.camera.target={30,65,0};session.apply(original);smoke_original_=original;
    request.wind=PresetWind::upper_shear;prepare_generation_draft(make_cloud_preset(request),true);launch_generation();
    save_scene_atomic(original,"preset-current.white.json");
}
void EditorUi::preset_test_step(int frame,GpuSpike& gpu){
    poll_generation();auto check=[](bool good,const char* text){if(!good)throw std::runtime_error(text);};
    if(frame==90){check(generation_candidate_&&!generation_job_.valid(),"Preset candidate did not complete");generation_preview_candidate_=true;}
    if(frame==95){
        auto shared=session.document().scene();shared.camera.position.x+=7;shared.camera.target.x+=7;shared.sun.direction_to_light={0,.8,.6};shared.exposure_ev=.25;apply(shared);smoke_original_=shared;
        save_scene_atomic(shared,"preset-current.white.json");
    }
    if(frame==100){
        check(session.document().scene()==smoke_original_&&gpu.scene_revision>=candidate_revision_bit&&gpu.rendered_revision>=candidate_revision_bit,"Candidate preview overwrote current or reused its render revision");
        bool protected_reset=false;try{adopt_generation_candidate();}catch(const std::logic_error&){protected_reset=true;}
        check(protected_reset&&session.document().scene()==smoke_original_,"Preset reset skipped explicit adoption confirmation");
        const auto preview=generation_candidate_view();check(preview.camera==smoke_original_.camera&&preview.sun==smoke_original_.sun,"Candidate comparison did not share current camera and light");
        save_scene_atomic(preview,"preset-candidate.white.json");
        std::cout<<"preset_native=candidate_preview current_document_preserved=true render_revision_distinct=true shared_view=true reset_confirmation=true PASS\n";
        const auto jobs=generation_job_count();FinishModifier temporary;temporary.kind=FinishModifierKind::density;temporary.target_id=smoke_original_.frozen->id;temporary.mask={{20,55,0},{30,30,30},8};temporary.density_multiplier=1.2;
        session.apply(scene_with_finish_command(smoke_original_,{FinishCommandKind::add,0,temporary}));const auto finished=session.document().scene();
        bool finishing_reset_rejected=false;try{(void)generation_candidate_view();}catch(const std::logic_error&){finishing_reset_rejected=true;}
        check(finishing_reset_rejected&&session.document().scene()==finished&&session.undo()&&session.document().scene()==smoke_original_&&generation_job_count()==jobs,"Fresh preset preview silently discarded existing finishing or changed Current");
        std::cout<<"preset_native=finishing_reset_guard nonempty_stack_rejected=true current_retained=true undo=true generation_jobs=0 PASS\n";
    }
    if(frame==105){
        const auto candidate=*generation_candidate_fixed_;const auto token=generation_candidate_draft_token_;discard_generation_candidate();check(!generation_candidate_&&session.document().scene()==smoke_original_,"Discard changed current cloud");
        undo_generation_candidate_discard();check(generation_candidate_fixed_&&*generation_candidate_fixed_==candidate&&session.document().scene()==smoke_original_,"Undo discard did not restore the sole candidate");
        check(generation_candidate_draft_token_==token,"Undo discard did not restore candidate draft identity");
        generation_preview_candidate_=true;std::cout<<"preset_native=discard_undo current_preserved=true candidate_restored=true PASS\n";
    }
    if(frame==110){
        const auto candidate=generation_candidate_view();adopt_generation_candidate(true);
        check(!generation_draft_resets_edits_,"Successful adoption failed to consume its own prepared draft's reset intent");
        check(session.document().scene()==candidate&&session.undo()&&session.document().scene()==smoke_original_,"Confirmed preset replacement was not one undoable command");
        std::cout<<"preset_native=confirmed_reset single_undo=true current_restored=true PASS\n";
    }
    if(frame==115)generation_preview_candidate_=false;
    if(frame==125){check(gpu.scene_revision==session.document().revision()&&session.document().scene()==smoke_original_,"Current return did not restore current render");std::cout<<"preset_native=current_return document_unchanged=true revision_restored=true PASS\n";}
    if(frame==130){
        check(session.redo(),"Confirmed preset adoption could not be restored for the stage regression");
        const auto before=session.document().scene();const auto jobs=generation_job_count();auto edited=before;
        FinishModifier cut;cut.id=101;cut.kind=FinishModifierKind::cut;cut.target_id=edited.frozen->id;cut.mask={{20,55,0},{17,25,24},4};
        edited=scene_with_finish_command(std::move(edited),{FinishCommandKind::add,0,cut});
        auto density=cut;density.id=102;density.kind=FinishModifierKind::density;density.strength=.5;density.density_multiplier=1.5;density.mask={{40,60,0},{30,35,30},8};
        edited=scene_with_finish_command(std::move(edited),{FinishCommandKind::add,0,density});
        auto protection=density;protection.id=103;protection.kind=FinishModifierKind::protect_detail;protection.target_kind=FinishTargetKind::field;protection.target_id=edited.frozen->fields.front().development_id;protection.strength=.8;protection.mask.center={20,70,0};
        edited=scene_with_finish_command(std::move(edited),{FinishCommandKind::add,0,protection});apply(edited);
        check(session.document().scene()==edited&&edited.frozen==before.frozen&&generation_job_count()==jobs,"Native finishing preparation changed the fixed source or ran growth");
        save_scene_atomic(edited,"preset-finishing-base.white.json");
        std::cout<<"preset_native=finishing_added layers=3 content_hash_stable=true generation_jobs=0 PASS\n";
    }
    if(frame==135){
        const auto count=generation_job_count();const auto content=session.document().scene().frozen->content_hash;const auto finishing=session.document().scene().finish_stack;
        apply(regenerate_fixed_detail(session.document().scene(),991));
        auto edited=session.document().scene();const auto field=edited.frozen->fields.front();auto noise=field.recipe.noise;noise.medium_strength=.35;
        auto layers=field.layers;layers.micro=false;
        edited=scene_with_frozen_detail(std::move(edited),field.development_id,noise,field.recipe.detail_seed,layers);
        edited.frozen->optics.albedo=.6;refresh_frozen_scene(edited);apply(edited);
        check(generation_job_count()==count&&session.document().scene().frozen->content_hash==content,"Preset detail-only command regenerated structure");
        check(finishing.layers.size()==3&&session.document().scene().finish_stack==finishing,"Native detail changes lost finishing masks, settings, order or targets");
        smoke_before_=session.document().scene();save_scene_atomic(smoke_before_,"preset-detail.white.json");
        std::cout<<"preset_native=detail_only generation_jobs=0 content_hash_stable=true PASS\n";
        std::cout<<"preset_native=finishing_detail_retained exact_stack=true generation_jobs=0 PASS\n";
    }
    if(frame==140){
        check(!generation_draft_resets_edits_,"Confirmed preset adoption leaked reset intent into a later stage edit");
        generation_settings_.stage=.6;launch_generation();
    }
    if(frame==150){
        check(!generation_job_.valid()&&generation_candidate_fixed_&&!generation_candidate_resets_edits_,"Stage-only candidate retained preset reset intent or did not finish");
        const auto& current=*smoke_before_.frozen;const auto& candidate=*generation_candidate_fixed_->frozen;
        check(candidate.selection_value==.6&&candidate.optics==current.optics&&candidate.fields.size()==current.fields.size(),"Stage-only candidate lost its selection or edited optics");
        for(std::size_t i=0;i<current.fields.size();++i){const auto& a=current.fields[i];const auto& b=candidate.fields[i];
            check(a.development_id==b.development_id&&a.recipe.noise==b.recipe.noise&&a.recipe.detail_seed==b.recipe.detail_seed&&a.layers==b.layers,"Stage-only candidate discarded fixed-field detail or layer edits");}
        const auto expected=generation_candidate_view();check(expected.finish_stack==smoke_before_.finish_stack&&expected.finish_stack.layers.size()==3,"Stage candidate preview dropped the finishing graph");adopt_generation_candidate();
        check(session.document().scene()==expected,"Stage-only candidate required another reset confirmation");smoke_before_=session.document().scene();
        save_scene_atomic(smoke_before_,"preset-stage-preserved.white.json");
        std::cout<<"preset_native=stage_after_reset new_reset_intent=false detail_preserved=true layers_preserved=true optics_preserved=true unconfirmed_adoption=true PASS\n";
        std::cout<<"preset_native=finishing_stage_retained exact_stack=true PASS\n";
    }
    if(frame==151){
        const auto prior_token=generation_candidate_draft_token_;
        prepare_generation_draft({*generation_initial_,generation_settings_,{},0},true);
        check(generation_draft_token_>prior_token,"Preparing identical preset inputs did not create a new reset intent");
        const auto before=session.document().scene();adopt_generation_candidate();
        check(session.document().scene()==before&&generation_draft_resets_edits_&&!generation_candidate_resets_edits_,"Adopting an older completed candidate consumed a newer identical preset's reset intent");
        const auto token=generation_draft_token_;generation_draft_token_=std::numeric_limits<std::uint64_t>::max();bool exhausted=false;
        try{prepare_generation_draft({*generation_initial_,generation_settings_,{},0},true);}catch(const std::overflow_error&){exhausted=true;}
        const bool unchanged=generation_draft_token_==std::numeric_limits<std::uint64_t>::max()&&generation_draft_resets_edits_&&session.document().scene()==before;
        generation_draft_token_=token;check(exhausted&&unchanged,"Exhausted draft token wrapped or changed pending intent/current state");
        std::cout<<"preset_native=pending_reset identical_inputs=true older_candidate_adopted=true newer_reset_intent_preserved=true PASS\n";
    }
    if(frame==155){const auto id=session.document().scene().frozen->curves.front().development_id;prepare_generation_draft(make_structure_variation(session.document().scene(),StructureVariationScope::selected_development,777,id),false);launch_generation();}
    if(frame==175){check(generation_candidate_&&!generation_job_.valid(),"Selected-development candidate did not complete");generation_preview_candidate_=true;}
    if(frame==180){
        check(session.document().scene()==smoke_before_&&gpu.scene_revision>=candidate_revision_bit,"Scoped candidate overwrote current");
        const auto preview=generation_candidate_view();check(preview.finish_stack==smoke_before_.finish_stack&&preview.finish_stack.layers.size()==3,"Scoped candidate preview dropped finishing or rebound stable targets");
        save_scene_atomic(preview,"preset-scoped.white.json");std::cout<<"preset_native=finishing_scoped_retained exact_stack=true current_preserved=true PASS\n";
    }
    if(frame==185){auto shared=session.document().scene();shared.camera.position.z+=10;shared.camera.target.z+=10;shared.exposure_ev+=.25;apply(shared);smoke_before_=shared;}
    if(frame==195)launch_generation(true);
    if(frame==210){check(!generation_job_.valid()&&session.document().scene()==smoke_before_&&generation_candidate_.has_value(),"Cancelled retry lost current/candidate");std::cout<<"preset_native=cancel previous_current_and_candidate_retained=true PASS\n";}
    if(frame==220){
        adopt_generation_candidate();const auto adopted=session.document().scene();
        check(adopted.camera==smoke_before_.camera&&adopted.sun==smoke_before_.sun&&adopted.exposure_ev==smoke_before_.exposure_ev,"Adoption lost view edits made after generation");
        check(adopted.finish_stack==smoke_before_.finish_stack&&adopted.finish_stack.layers.size()==3,"Scoped adoption silently dropped or changed finishing layers");
        check(session.undo()&&session.document().scene()==smoke_before_&&session.redo()&&session.document().scene()==adopted,"Scoped candidate adoption lost Undo/Redo");
        check(generation_job_count()==preset_test_jobs_+5,"Unexpected growth work during candidate comparison");
        save_scene_atomic(adopted,"preset-adopted.white.json");std::cout<<"preset_native=adopt single_undo=true redo=true total_generation_jobs=5 PASS\n";
        std::cout<<"preset_native=finishing_adopt_retained exact_stack=true undo_redo=true PASS\n";
    }
    clone_test_step(frame,gpu);
}
}
