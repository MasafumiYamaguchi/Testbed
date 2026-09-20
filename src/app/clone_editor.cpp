#include "white/editor_ui.hpp"
#include "white/cloud_clone.hpp"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace white {
void EditorUi::clone_current_candidate(){
    if(generation_job_.valid()||gizmo_drag_||inspector_drag_||orbit_drag_)throw std::logic_error("Finish the current edit or generation before cloning");
    const auto current=session.document().scene();auto cloned=clone_frozen_cloud(current);
    if(scene_json(cloned.scene).size()>max_scene_bytes)throw std::invalid_argument("Cloned candidate exceeds the scene resource budget");
    // Publish only after the complete clone/remap passes validation. A failed
    // allocation leaves Current and the last completed candidate untouched.
    generation_candidate_fixed_=std::move(cloned.scene);generation_candidate_.reset();generation_guard_=current;
    generation_candidate_is_clone_=true;generation_candidate_resets_edits_=false;
    generation_candidate_draft_token_=0; // Adoption explicitly replaces the draft.
    generation_discarded_.reset();generation_discarded_guard_.reset();generation_discarded_fixed_.reset();
    generation_preview_candidate_=true;
    generation_message_="Current appearance copied with fresh IDs and remapped targets. Compare or explicitly adopt; Current is unchanged. Future growth follows the clone's new identity.";
}
void EditorUi::clone_test_step(int frame,GpuSpike& gpu){
    auto check=[](bool good,const char* text){if(!good)throw std::runtime_error(text);};
    if(frame==240){
        smoke_before_=session.document().scene();const auto jobs=generation_job_count();clone_current_candidate();
        check(generation_candidate_is_clone_&&!generation_candidate_&&generation_candidate_fixed_->frozen->id!=smoke_before_.frozen->id&&session.document().scene()==smoke_before_&&generation_job_count()==jobs,"Clone did not create an independent zero-growth candidate");
        const auto preview=generation_candidate_view();const auto& original=*smoke_before_.frozen;const auto& copied=*preview.frozen;
        check(smoke_before_.finish_stack.layers.size()==3&&preview.finish_stack.layers.size()==3,"Native clone lost finishing layers");
        for(std::size_t i=0;i<smoke_before_.finish_stack.layers.size();++i){
            auto expected=smoke_before_.finish_stack.layers[i];const auto& actual=preview.finish_stack.layers[i];
            check(std::none_of(smoke_before_.finish_stack.layers.begin(),smoke_before_.finish_stack.layers.end(),[&](const auto& layer){return actual.id==layer.id;}),"Native clone reused an original finishing identity");
            expected.id=actual.id;
            if(expected.target_kind==FinishTargetKind::object)expected.target_id=copied.id;
            else {const auto field=std::find_if(original.fields.begin(),original.fields.end(),[&](const auto& field){return field.development_id==expected.target_id;});
                check(field!=original.fields.end(),"Native clone source has a missing finishing target");expected.target_id=copied.fields.at(std::size_t(field-original.fields.begin())).development_id;}
            check(expected==actual,"Native clone lost layer order/settings or remapped a target through the wrong identity namespace");
        }
        const auto before_packet=gpu_scene_density_params(smoke_before_),after_packet=gpu_scene_density_params(preview);
        check(std::memcmp(&before_packet,&after_packet,sizeof(before_packet))==0,"Native clone changed the finished GPU density packet");
        save_scene_atomic(preview,"preset-clone.white.json");
        std::cout<<"preset_native=clone_candidate current_preserved=true fresh_ids=true generation_jobs=0 PASS\n";
        std::cout<<"preset_native=clone_finishing_remapped layers=3 fresh_layer_ids=true object_field_targets=true exact_gpu_packet=true PASS\n";
    }
    if(frame==245){
        const auto candidate=*generation_candidate_fixed_;discard_generation_candidate();undo_generation_candidate_discard();
        check(generation_candidate_is_clone_&&generation_candidate_fixed_&&*generation_candidate_fixed_==candidate&&session.document().scene()==smoke_before_,"Clone discard/Undo lost its candidate kind or remapped scene");generation_preview_candidate_=true;
    }
    if(frame==255){
        check(gpu.scene_revision>=(UINT64_C(1)<<63)&&gpu.rendered_revision>=(UINT64_C(1)<<63)&&session.document().scene()==smoke_before_,"Clone was not independently previewed");
        std::cout<<"preset_native=clone_preview render_revision_distinct=true current_preserved=true PASS\n";
    }
    if(frame==270){
        const auto expected=generation_candidate_view();adopt_generation_candidate();
        check(expected.finish_stack.layers.size()==3&&session.document().scene().finish_stack==expected.finish_stack,"Clone adoption dropped its remapped finishing stack");
        check(session.document().scene()==expected&&session.undo()&&session.document().scene()==smoke_before_&&session.redo()&&session.document().scene()==expected,"Clone adoption lost exact Undo/Redo");
        check(generation_job_count()==preset_test_jobs_+5,"Clone or its preview ran growth");
        save_scene_atomic(expected,"preset-clone-adopted.white.json");
        std::cout<<"preset_native=clone_adopt fresh_ids=true generation_jobs=0 single_undo=true redo=true PASS\n";
        std::cout<<"preset_native=clone_finishing_adopt_retained exact_stack=true undo_redo=true PASS\n";
    }
}
}
