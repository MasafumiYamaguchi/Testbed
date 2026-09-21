#include "white/editor_ui.hpp"
#include <imgui.h>
#include <algorithm>
#include <limits>
namespace white {
void EditorUi::poll_generation(){
    if(!generation_job_.valid()||generation_job_.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;
    auto outcome=generation_job_.get();generation_message_=outcome.message;
    if(outcome.status==GenerationStatus::completed)try{
        auto fixed=freeze_candidate(*outcome.candidate);
        if(!generation_job_resets_edits_&&generation_job_guard_)fixed=preserve_candidate_finish(*generation_job_guard_,std::move(fixed));
        generation_discarded_.reset();generation_discarded_guard_.reset();generation_discarded_fixed_.reset();
        generation_candidate_fixed_=std::move(fixed);generation_candidate_resets_edits_=generation_job_resets_edits_;generation_candidate_is_clone_=false;
        generation_candidate_draft_token_=generation_job_draft_token_;
        generation_candidate_=std::move(outcome.candidate);generation_guard_=std::move(generation_job_guard_);
    }catch(const std::exception& e){generation_message_=std::string("Candidate rejected; current cloud retained: ")+e.what();}
}
void EditorUi::launch_generation(bool cancel_before_start){
    if(generation_job_.valid()||!generation_initial_)throw std::logic_error("Generation already running or no initial recipe");
    if(!generation_draft_resets_edits_&&(!generation_draft_current_||!generation_draft_matches_current(*generation_draft_current_,session.document().scene())))
        throw std::logic_error("The current cloud structure or saved source changed after draft preparation. Prepare a new draft before generating.");
    generation_preview_candidate_=false;generation_job_resets_edits_=generation_draft_resets_edits_;
    generation_job_draft_token_=generation_draft_token_;
    generation_job_guard_=session.document().scene();generation_stop_=std::stop_source{};generation_progress_=std::make_shared<std::atomic<double>>(0);
    if(cancel_before_start)generation_stop_.request_stop();
    auto source=*generation_initial_;source.camera=generation_job_guard_->camera;source.sun=generation_job_guard_->sun;source.exposure_ev=generation_job_guard_->exposure_ev;source.preview_approx=generation_job_guard_->preview_approx;
    const auto settings=generation_settings_;const auto stop=generation_stop_.get_token();const auto progress=generation_progress_;
    generation_job_=std::async(std::launch::async,[source,settings,stop,progress]{return generate_cloud_state(source,settings,stop,[progress](double value){progress->store(value);});});
    ++generation_runs_;generation_message_="Generating selected state...";
}
void EditorUi::adopt_generation_candidate(bool reset_confirmed){
    if(generation_job_.valid()||!generation_candidate_fixed_||!generation_guard_||!candidate_scene_unchanged(session.document().scene(),*generation_guard_))throw std::logic_error("Prepare a completed candidate from the current cloud before adoption");
    if(generation_candidate_resets_edits_&&!reset_confirmed)throw std::logic_error("Preset adoption requires confirmation before replacing manual edits");
    if(generation_candidate_is_clone_&&generation_draft_token_==std::numeric_limits<std::uint64_t>::max())throw std::overflow_error("Candidate draft identity namespace exhausted");
    const auto fixed=generation_candidate_view();apply(fixed);
    if(session.document().scene()!=fixed)throw std::runtime_error("Freeze adoption failed");
    if(generation_candidate_is_clone_){
        ++generation_draft_token_;
        generation_draft_current_=session.document().scene();
        generation_initial_.reset();generation_draft_resets_edits_=false;
        if(frozen_can_regenerate(*fixed.frozen)){generation_initial_=generation_initial_scene(*fixed.frozen);generation_settings_=fixed.frozen->provenance->settings;}
    }
    // Even identical prepared inputs express a distinct pending reset intent.
    if(generation_candidate_draft_token_==generation_draft_token_){generation_draft_resets_edits_=false;generation_draft_current_=session.document().scene();}
    generation_preview_candidate_=false;generation_candidate_resets_edits_=false;generation_guard_=session.document().scene();generation_message_="Selected state frozen. Detail edits keep the evaluated structure; Undo restores the previous cloud.";
}
void EditorUi::discard_generation_candidate(){
    if(generation_job_.valid()||!generation_candidate_fixed_)throw std::logic_error("Wait for generation before discarding a completed candidate");
    generation_discarded_is_clone_=generation_candidate_is_clone_;generation_discarded_resets_edits_=generation_candidate_resets_edits_;
    generation_discarded_draft_token_=generation_candidate_draft_token_;
    generation_preview_candidate_=false;generation_discarded_=std::move(generation_candidate_);generation_discarded_fixed_=std::move(generation_candidate_fixed_);generation_candidate_fixed_.reset();generation_discarded_guard_=generation_guard_;generation_candidate_.reset();
    generation_message_="Candidate discarded; current cloud retained. This discard can be undone.";
}
void EditorUi::undo_generation_candidate_discard(){
    if(generation_job_.valid()||!generation_discarded_fixed_||generation_candidate_fixed_)throw std::logic_error("No discarded candidate is available to restore");
    generation_candidate_is_clone_=generation_discarded_is_clone_;generation_candidate_resets_edits_=generation_discarded_resets_edits_;
    generation_candidate_draft_token_=generation_discarded_draft_token_;
    generation_candidate_=std::move(generation_discarded_);generation_candidate_fixed_=std::move(generation_discarded_fixed_);generation_discarded_fixed_.reset();generation_guard_=std::move(generation_discarded_guard_);generation_discarded_.reset();
    generation_message_="Candidate restored. The current cloud is unchanged.";
}
void EditorUi::draw_generation_ui(){
    if(!prefab_source(session.document().scene())&&!editable_developed_source(session.document().scene())&&!generation_initial_&&!session.document().scene().frozen)return;
    if(!ImGui::CollapsingHeader("Growth and wind"))return;
    const bool running=generation_job_.valid();
    ImGui::BeginDisabled(running||gizmo_drag_||inspector_drag_||orbit_drag_);
    const auto& current=session.document().scene();
    if(!current.frozen&&ImGui::Button("Use current shape as start"))try{
        auto initial=session.document().scene();auto settings=default_generation_settings(initial);
        prepare_generation_draft({std::move(initial),std::move(settings),{},0},false);generation_message_="Initial shape captured. Set a stage, then generate a candidate.";
    }catch(const std::exception& e){generation_message_=e.what();}
    if(current.frozen){
        ImGui::BeginDisabled(!frozen_can_regenerate(*current.frozen));
        if(ImGui::Button("Use saved generation recipe"))try{prepare_generation_draft({generation_initial_scene(*current.frozen),current.frozen->provenance->settings,{},0},false);generation_message_="Saved inputs loaded; generate a separate candidate to change stage or wind.";}catch(const std::exception& e){generation_message_=e.what();}
        ImGui::EndDisabled();
        if(!frozen_can_regenerate(*current.frozen))ImGui::TextWrapped("The fixed result is usable; this saved generation version is unavailable.");
    }
    ImGui::EndDisabled();
    if(!generation_initial_)ImGui::TextWrapped("Capture a starting shape to compare growth and wind without changing the current cloud.");
    else {
    ImGui::BeginDisabled(running);
    ImGui::SetNextItemWidth(100);ImGui::InputDouble("Stage 0..1",&generation_settings_.stage,0,0,"%.3f");
    ImGui::SetNextItemWidth(100);ImGui::InputDouble("Initial height fraction",&generation_settings_.initial_height_fraction,0,0,"%.3f");
    ImGui::TextWrapped("Stage is dimensionless. Wind values are horizontal displacement in local metres at stage 1, not real-world wind speed.");
    if(ImGui::TreeNode("Altitude wind profile")){
        ImGui::SetNextItemWidth(100);ImGui::InputDouble("Reference base m",&generation_settings_.wind_base,0,0,"%.1f");
        ImGui::SetNextItemWidth(100);ImGui::InputDouble("Reference height m",&generation_settings_.wind_height,0,0,"%.1f");
        for(std::size_t i=0;i<generation_settings_.wind.size();++i){
            ImGui::PushID(int(i));auto& k=generation_settings_.wind[i];
            ImGui::BeginDisabled(i==0||i+1==generation_settings_.wind.size());ImGui::SetNextItemWidth(85);ImGui::InputDouble("Altitude 0..1",&k.altitude,0,0,"%.3f");ImGui::EndDisabled();
            ImGui::SetNextItemWidth(85);ImGui::InputDouble("Drift X m",&k.displacement.x,0,0,"%.1f");
            ImGui::SetNextItemWidth(85);ImGui::InputDouble("Drift Z m",&k.displacement.z,0,0,"%.1f");
            if(i>0&&i+1<generation_settings_.wind.size()&&ImGui::SmallButton("Remove knot")){generation_settings_.wind.erase(generation_settings_.wind.begin()+std::ptrdiff_t(i));ImGui::PopID();break;}
            ImGui::PopID();
        }
        ImGui::BeginDisabled(generation_settings_.wind.size()>=max_wind_knots);
        if(ImGui::SmallButton("Insert altitude knot")){
            auto& knots=generation_settings_.wind;std::size_t widest=0;for(std::size_t i=1;i+1<knots.size();++i)if(knots[i+1].altitude-knots[i].altitude>knots[widest+1].altitude-knots[widest].altitude)widest=i;
            const auto a=knots[widest],b=knots[widest+1];knots.insert(knots.begin()+std::ptrdiff_t(widest+1),{(a.altitude+b.altitude)*.5,(a.displacement+b.displacement)*.5});
        }
        ImGui::EndDisabled();
        if(ImGui::SmallButton("Calm"))for(auto& knot:generation_settings_.wind)knot.displacement={};
        ImGui::SameLine();if(ImGui::SmallButton("Upper shear"))generation_settings_.wind={{0,{}},{.5,{20,0,5}},{1,{70,0,25}}};
        ImGui::TextWrapped("Wind uses this fixed altitude frame. The cloud base stays anchored; increasing the cloud bounds does not move the wind profile.");ImGui::TreePop();
    }
    if(generation_settings_.enabled&&ImGui::TreeNode("Individual growth and guides")){
        const auto initial=editable_developed_source(*generation_initial_)?*generation_initial_:new_developed_scene(*generation_initial_);
        const auto* source=editable_developed_source(initial);
        for(auto& cell:generation_settings_.cells){ImGui::PushID(std::to_string(cell.cell_id).c_str());
            ImGui::Text("Development %llu",static_cast<unsigned long long>(cell.cell_id));
            ImGui::SetNextItemWidth(85);ImGui::InputDouble("Onset 0..0.8",&cell.start_stage,0,0,"%.2f");
            ImGui::SetNextItemWidth(85);ImGui::InputDouble("Amount 0..1",&cell.amount,0,0,"%.2f");
            if(source)for(const auto& c:source->cells)if(c.id==cell.cell_id)for(const auto& p:c.shape.points){
                bool pinned=std::find(cell.pinned_controls.begin(),cell.pinned_controls.end(),p.id)!=cell.pinned_controls.end();
                const auto label="Pin guide XZ "+std::to_string(p.id);
                if(ImGui::Checkbox(label.c_str(),&pinned)){if(pinned)cell.pinned_controls.push_back(p.id);else std::erase(cell.pinned_controls,p.id);}
            }
            ImGui::PopID();
        }
        ImGui::TextWrapped("Pinned guides preserve initial horizontal positions. Manual primitive offsets and cuts remain in their saved local coordinates.");ImGui::TreePop();
    }
    if(ImGui::TreeNode("Whole-cloud motion")){
        ImGui::SetNextItemWidth(100);ImGui::InputDouble("Translation X m",&generation_settings_.reference_translation.x,0,0,"%.1f");
        ImGui::SetNextItemWidth(100);ImGui::InputDouble("Translation Z m",&generation_settings_.reference_translation.z,0,0,"%.1f");
        ImGui::TextWrapped("Optional bulk motion is separate from the relative wind deformation.");ImGui::TreePop();
    }
    if(ImGui::Button("Generate candidate"))try{launch_generation();}catch(const std::exception& e){generation_message_=e.what();}
    ImGui::EndDisabled();
    }
    if(generation_job_.valid()){
        ImGui::ProgressBar(float(generation_progress_->load()),{-1,0});if(ImGui::Button("Cancel generation")){generation_stop_.request_stop();generation_preview_candidate_=false;}
    }
    if(generation_candidate_fixed_){
        if(generation_candidate_is_clone_)ImGui::TextWrapped("Cloned current state / fresh IDs / no growth");
        else if(generation_candidate_)ImGui::Text("Candidate stage %.3f / %.2f ms",generation_candidate_->settings.stage,generation_candidate_->elapsed_ms);
        ImGui::BeginDisabled(generation_job_.valid()||gizmo_drag_||inspector_drag_||orbit_drag_);
        if(ImGui::RadioButton("Current",!generation_preview_candidate_))generation_preview_candidate_=false;
        ImGui::SameLine();if(ImGui::RadioButton("Candidate",generation_preview_candidate_))generation_preview_candidate_=true;
        ImGui::EndDisabled();
        ImGui::TextWrapped("Current and one candidate share camera and light. Candidate preview does not edit the saved cloud.");
        const bool scene_unchanged=generation_guard_&&candidate_scene_unchanged(session.document().scene(),*generation_guard_);
        ImGui::BeginDisabled(generation_job_.valid()||!scene_unchanged||gizmo_drag_||inspector_drag_||orbit_drag_);
        if(ImGui::Button(generation_candidate_is_clone_?"Adopt cloned candidate":"Freeze and adopt candidate")){
            if(generation_candidate_resets_edits_)ImGui::OpenPopup("Adopt preset candidate?");
            else try{adopt_generation_candidate();}catch(const std::exception& e){generation_message_=e.what();}
        }
        ImGui::EndDisabled();
        if(!scene_unchanged)ImGui::TextWrapped("The scene changed after generation. Generate again before adopting so newer edits cannot be overwritten.");
        ImGui::BeginDisabled(generation_job_.valid());if(ImGui::SmallButton("Discard candidate"))discard_generation_candidate();ImGui::EndDisabled();
    }
    if(ImGui::BeginPopupModal("Adopt preset candidate?",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
        ImGui::TextWrapped("This preset replaces the current generated structure and its source cuts/manual adjustments. Undo restores the current cloud. Clear any finishing layers explicitly before replacing the preset.");
        if(ImGui::Button("Adopt and replace")){try{adopt_generation_candidate(true);ImGui::CloseCurrentPopup();}catch(const std::exception& e){generation_message_=e.what();}}
        ImGui::SameLine();if(ImGui::Button("Keep current")){generation_preview_candidate_=false;ImGui::CloseCurrentPopup();}
        ImGui::EndPopup();
    }
    if(generation_discarded_fixed_&&!generation_job_.valid()&&ImGui::SmallButton("Undo candidate discard"))undo_generation_candidate_discard();
    if(!generation_message_.empty())ImGui::TextWrapped("%s",generation_message_.c_str());
}
}
