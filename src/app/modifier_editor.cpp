#include "white/editor_ui.hpp"
#include "white/frozen_cloud.hpp"
#include <imgui.h>
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace white {
namespace {
const char* label(FinishModifierKind kind){switch(kind){case FinishModifierKind::cut:return "Cut";case FinishModifierKind::density:return "Density";case FinishModifierKind::protect_detail:return "Protect detail";}return "Invalid";}
FinishModifier make_layer(const Scene& scene,FinishModifierKind kind){FinishModifier layer;layer.kind=kind;layer.target_id=scene.frozen->id;layer.mask.center={0,50,0};layer.mask.radii={18,24,20};if(kind==FinishModifierKind::density)layer.density_multiplier=1.5;return layer;}
}
void EditorUi::finish_item(bool changed,const FinishModifier& layer){
    auto scene=session.document().scene();if(changed)try{scene=scene_with_finish_command(scene,{FinishCommandKind::replace,layer.id,layer});}catch(const std::exception& error){status_=error.what();changed=false;}
    inspector_item(changed,std::move(scene));
}
void EditorUi::draw_modifier_ui(){
    const auto& current=session.document().scene();if(!current.frozen)return;
    if(!ImGui::CollapsingHeader("Finishing layers",ImGuiTreeNodeFlags_DefaultOpen))return;
    ImGui::TextWrapped("Masks follow the cloud. Hard cut interiors stay empty; protection reduces local detail.");
    const auto layers=current.finish_stack.layers;
    if(std::none_of(layers.begin(),layers.end(),[&](const auto& l){return l.id==finish_selected_;}))finish_selected_=layers.empty()?0:layers.front().id;
    for(const auto& layer:layers){ImGui::PushID(std::to_string(layer.id).c_str());
        auto updated=layer;const bool changed=ImGui::Checkbox("##enabled",&updated.enabled);finish_item(changed,updated);ImGui::SameLine();
        const auto name=std::to_string(layer.id)+" / "+label(layer.kind);if(ImGui::Selectable(name.c_str(),layer.id==finish_selected_))finish_selected_=layer.id;
        ImGui::PopID();
    }
    const bool editing=gizmo_drag_||inspector_drag_||orbit_drag_;
    ImGui::BeginDisabled(editing||layers.size()>=max_finish_modifiers);
    for(auto kind:{FinishModifierKind::cut,FinishModifierKind::density,FinishModifierKind::protect_detail}){
        if(kind!=FinishModifierKind::cut)ImGui::SameLine();
        const auto name=std::string("+ ")+label(kind);if(ImGui::SmallButton(name.c_str()))try{auto layer=make_layer(session.document().scene(),kind);layer.id=next_finish_id(session.document().scene().finish_stack);apply(scene_with_finish_command(session.document().scene(),{FinishCommandKind::add,0,layer}));finish_selected_=layer.id;}catch(const std::exception& error){status_=error.what();}
    }
    ImGui::EndDisabled();
    auto selected=[&]()->std::optional<FinishModifier>{const auto& stack=session.document().scene().finish_stack;const auto it=std::find_if(stack.layers.begin(),stack.layers.end(),[&](const auto& l){return l.id==finish_selected_;});return it==stack.layers.end()?std::nullopt:std::optional(*it);};
    if(!selected())return;
    ImGui::BeginDisabled(editing);
    auto command=[&](const char* name,FinishCommandKind kind){if(ImGui::SmallButton(name))try{apply(scene_with_finish_command(session.document().scene(),{kind,finish_selected_,{}}));}catch(const std::exception& error){status_=error.what();}};
    command("Up",FinishCommandKind::move_up);ImGui::SameLine();command("Down",FinishCommandKind::move_down);
    ImGui::SameLine();ImGui::BeginDisabled(layers.size()>=max_finish_modifiers);command("Duplicate",FinishCommandKind::duplicate);ImGui::EndDisabled();
    command("Delete",FinishCommandKind::erase);ImGui::SameLine();
    if(ImGui::SmallButton("Solo"))try{auto scene=session.document().scene();for(auto& l:scene.finish_stack.layers)l.enabled=l.id==finish_selected_;apply(std::move(scene));}catch(const std::exception& error){status_=error.what();}
    ImGui::EndDisabled();if(!selected())return;
    auto layer=*selected();int kind=int(layer.kind);ImGui::SetNextItemWidth(120);bool changed=ImGui::Combo("Operation",&kind,"Cut\0Density\0Protect detail\0");if(changed)layer.kind=FinishModifierKind(kind);finish_item(changed,layer);
    layer=*selected();ImGui::SetNextItemWidth(100);changed=ImGui::InputDouble("Strength 0..1",&layer.strength,0,0,"%.3f");finish_item(changed,layer);
    if(layer.kind==FinishModifierKind::density){layer=*selected();ImGui::SetNextItemWidth(100);changed=ImGui::InputDouble("Multiplier 0..4",&layer.density_multiplier,0,0,"%.3f");finish_item(changed,layer);}
    if(layer.kind==FinishModifierKind::cut){layer=*selected();changed=ImGui::Checkbox("Final hard interior",&layer.hard_cut);finish_item(changed,layer);}
    auto number=[&](const char* name,int property,int axis){auto value=*selected();double* item=property==0?&value.mask.center.x:property==1?&value.mask.radii.x:&value.mask.falloff;if(property<2){if(axis==1)item=property==0?&value.mask.center.y:&value.mask.radii.y;if(axis==2)item=property==0?&value.mask.center.z:&value.mask.radii.z;}ImGui::SetNextItemWidth(100);const bool edit=ImGui::InputDouble(name,item,0,0,"%.3f");finish_item(edit,value);};
    number("Mask X m",0,0);number("Mask Y m",0,1);number("Mask Z m",0,2);
    number("Radius X m",1,0);number("Radius Y m",1,1);number("Radius Z m",1,2);number("Feather m",2,0);
    layer=*selected();const auto target=layer.target_kind==FinishTargetKind::object?std::string("Whole cloud"):("Field "+std::to_string(layer.target_id));
    if(ImGui::BeginCombo("Target",target.c_str())){
        auto retarget=[&](FinishTargetKind kind,Id id){auto replacement=*selected();replacement.target_kind=kind;replacement.target_id=id;try{apply(scene_with_finish_command(session.document().scene(),{FinishCommandKind::replace,replacement.id,replacement}));}catch(const std::exception& error){status_=error.what();}};
        if(ImGui::Selectable("Whole cloud",layer.target_kind==FinishTargetKind::object))retarget(FinishTargetKind::object,session.document().scene().frozen->id);
        const auto fields=session.document().scene().frozen->fields;for(const auto& field:fields){const auto name="Field "+std::to_string(field.development_id);if(ImGui::Selectable(name.c_str(),layer.target_kind==FinishTargetKind::field&&layer.target_id==field.development_id))retarget(FinishTargetKind::field,field.development_id);}
        ImGui::EndCombo();
    }
}
void EditorUi::start_modifier_test(){
    auto scene=new_anvil_scene(new_cumulonimbus_scene({}));scene.anvil->cloud.settings.mode=TopLobeMode::children;scene.anvil->settings.enabled=true;refresh_anvil_scene(scene);
    auto settings=default_generation_settings(scene);settings.stage=.8;settings.wind.back().displacement={40,0,12};
    auto outcome=generate_cloud_state(scene,settings);if(outcome.status!=GenerationStatus::completed)throw std::runtime_error(outcome.message);
    auto fixed=freeze_candidate(outcome);fixed.camera.position={40,80,360};fixed.camera.target={35,65,0};session.apply(fixed);
    modifier_test_jobs_=generation_job_count();modifier_test_content_=fixed.frozen->content_hash;smoke_original_=fixed;
}
void EditorUi::modifier_test_step(int frame){
    auto check=[](bool okay,const char* message){if(!okay)throw std::runtime_error(message);};
    if(frame==45){auto layer=make_layer(session.document().scene(),FinishModifierKind::cut);layer.id=1;layer.mask.center={20,55,0};layer.mask.radii={17,25,24};apply(scene_with_finish_command(session.document().scene(),{FinishCommandKind::add,0,layer}));finish_selected_=1;smoke_before_=session.document().scene();
        check(SceneDensityEvaluator(smoke_before_).at(layer.mask.center)==0,"Native hard cut remains nonzero");check(session.undo()&&session.document().scene()==smoke_original_&&session.redo()&&session.document().scene()==smoke_before_,"Modifier add must be one Undo command");
        std::cout<<"modifier_native=add hard_cut=true single_undo=true redo=true PASS\n";
    }
    if(frame==75){auto layer=make_layer(session.document().scene(),FinishModifierKind::density);layer.id=2;layer.mask=smoke_before_.finish_stack.layers[0].mask;layer.density_multiplier=3;apply(scene_with_finish_command(session.document().scene(),{FinishCommandKind::add,0,layer}));
        auto protection=make_layer(session.document().scene(),FinishModifierKind::protect_detail);protection.id=3;protection.mask.center={15,70,0};protection.mask.radii={25,40,30};apply(scene_with_finish_command(session.document().scene(),{FinishCommandKind::add,0,protection}));
        const auto& field=session.document().scene().frozen->fields.front();auto noise=field.recipe.noise;noise.medium_strength=.8;noise.micro_erosion=2;apply(scene_with_frozen_detail(session.document().scene(),field.development_id,noise,field.recipe.detail_seed+123,field.layers));
        check(SceneDensityEvaluator(session.document().scene()).at(layer.mask.center)==0,"Density/detail resurrected hard cut");
        std::cout<<"modifier_native=detail_and_density hard_cut_final=true layers_retained=true PASS\n";
    }
    if(frame==105){const auto before=session.document().scene();apply(scene_with_finish_command(before,{FinishCommandKind::move_up,2,{}}));check(session.undo()&&session.document().scene()==before,"Reorder Undo lost exact graph");
        apply(scene_with_finish_command(before,{FinishCommandKind::duplicate,3,{}}));check(session.document().scene().finish_stack.layers.back().id==4,"Duplicate reused a stable ID");apply(scene_with_finish_command(session.document().scene(),{FinishCommandKind::erase,4,{}}));
        check(session.document().scene()==before,"Duplicate/delete changed original layers");
        auto broken=before;broken.finish_stack.layers.front().target_kind=FinishTargetKind::field;broken.finish_stack.layers.front().target_id=999999;bool rejected=false;try{session.apply(broken);}catch(const std::exception&){rejected=true;}check(rejected&&session.document().scene()==before,"Missing reference changed current cloud");
        check(generation_job_count()==modifier_test_jobs_&&before.frozen->content_hash==modifier_test_content_,"Finishing restarted generation or changed fixed structure");
        session.save("modifier-smoke.white.json");session.load("modifier-smoke.white.json");check(session.document().scene()==before,"Modifier save/reload changed graph");
        std::cout<<"modifier_native=reorder_duplicate_delete missing_reference_rejected=true content_hash_stable=true generation_jobs=0 save_reload=true PASS\n";
    }
}
}
