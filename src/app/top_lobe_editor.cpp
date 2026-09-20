#include "white/editor_ui.hpp"
#include <imgui.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
namespace white {
void EditorUi::top_lobe_item(bool changed,const TopLobeSettings& settings){
    auto scene=session.document().scene();
    if(changed)try{scene=scene_with_top_lobe_settings(scene,settings);}catch(const std::exception& e){status_=e.what();changed=false;}
    inspector_item(changed,std::move(scene));
}
void EditorUi::draw_top_lobes_ui(){
    const auto& initial=session.document().scene();
    if(!initial.top_lobes){
        const auto* source=editable_developed_source(initial);
        if(source&&!source->cells.empty()){
            ImGui::BeginDisabled(gizmo_drag_||inspector_drag_||orbit_drag_);
            if(ImGui::Button("Enable top hierarchy controls"))try{apply(new_top_lobe_scene(initial,development_));}catch(const std::exception& e){status_=e.what();}
            ImGui::EndDisabled();
        }
        return;
    }
    if(!ImGui::CollapsingHeader("Top lobe hierarchy",ImGuiTreeNodeFlags_DefaultOpen))return;
    auto settings=session.document().scene().top_lobes->settings;
    int mode=int(settings.mode);bool changed=ImGui::Combo("Lobes",&mode,"OFF\0Large parent\0Parent and children\0");settings.mode=TopLobeMode(mode);top_lobe_item(changed,settings);
    auto number=[&](const char* label,double TopLobeSettings::*member){auto p=session.document().scene().top_lobes->settings;ImGui::SetNextItemWidth(105);bool edit=ImGui::InputDouble(label,&(p.*member),0,0,"%.3f");top_lobe_item(edit,p);};
    number("Top start 0.5..0.9",&TopLobeSettings::top_start);
    number("Parent radius m",&TopLobeSettings::parent_radius);
    number("Child radius ratio",&TopLobeSettings::child_radius_ratio);
    number("Child occupancy 0..1",&TopLobeSettings::hierarchy_density);
    number("Fusion m",&TopLobeSettings::fusion_width);
    number("Mask transition m",&TopLobeSettings::mask_transition);
    number("Top density 0..1",&TopLobeSettings::density_scale);
    settings=session.document().scene().top_lobes->settings;int depth=int(settings.depth_limit);changed=ImGui::InputInt("Depth limit 1..2",&depth);settings.depth_limit=unsigned(depth);top_lobe_item(changed,settings);
    settings=session.document().scene().top_lobes->settings;int children=int(settings.child_limit);changed=ImGui::InputInt("Children limit 1..2",&children);settings.child_limit=unsigned(children);top_lobe_item(changed,settings);
    settings=session.document().scene().top_lobes->settings;ImGui::SetNextItemWidth(-1);changed=ImGui::InputScalarN("##TopGrowth",ImGuiDataType_Double,&settings.growth_direction.x,3,nullptr,nullptr,"%.3f");const double length=std::sqrt(dot(settings.growth_direction,settings.growth_direction));if(changed&&length>0)settings.growth_direction=settings.growth_direction*(1/length);top_lobe_item(changed,settings);
    ImGui::TextUnformatted("Top growth direction (y >= 0.2)");
    const TopLobeEvaluationPlan plan(*session.document().scene().top_lobes);
    ImGui::Text("Generated: %zu / 3; depth <= 2",plan.hierarchy().size());
    ImGui::Text("Density bound: %.3f",plan.maximum());
    ImGui::TextWrapped("Active lobes support one developed cell and render Direct. Below the top mask, the original density stays exact. OFF preserves multiple cells. Structure seed changes hierarchy; detail seed changes noise only.");
    ImGui::BeginDisabled(gizmo_drag_||inspector_drag_||orbit_drag_||settings.mode!=TopLobeMode::off);
    if(ImGui::Button("Return to independent cells"))try{apply(new_developed_scene(session.document().scene()));}catch(const std::exception& e){status_=e.what();}
    ImGui::EndDisabled();
}
void EditorUi::start_top_lobes_test(){
    auto scene=new_developed_scene(new_centerline_scene(new_cumulonimbus_scene(session.document().scene())));
    scene.camera.position={0,60,300};scene.camera.target={0,60,0};scene=new_top_lobe_scene(scene,scene.developed->cells.front().id);
    session.apply(scene);development_=scene.top_lobes->target_cell;prefab_group_=true;curve_point_=0;smoke_original_=scene;session.save("top-lobes-off.white.json");
    auto side=scene;side.camera.position={300,60,0};save_scene_atomic(side,"top-lobes-off-side.white.json");
}
void EditorUi::top_lobes_test_step(int frame){
    if(frame==85||frame==125){
        auto scene=session.document().scene();auto settings=scene.top_lobes->settings;settings.mode=frame==85?TopLobeMode::parent:TopLobeMode::children;
        const auto begin=std::chrono::steady_clock::now();scene=scene_with_top_lobe_settings(scene,settings);const TopLobeEvaluationPlan plan(*scene.top_lobes);const auto end=std::chrono::steady_clock::now();
        const TopLobeEvaluationPlan baseline(*smoke_original_.top_lobes);const double boundary=top_lobe_mask_height(*scene.top_lobes);
        for(int x=-30;x<=30;x+=3)for(int y=0;y<=70;y+=2){Vec3 p{double(x),std::min(double(y),boundary),0};if(plan.at(p)!=baseline.at(p))throw std::runtime_error("Top hierarchy changed protected lower density");}
        apply(scene);const std::string name=frame==85?"parent":"children";session.save("top-lobes-"+name+".white.json");auto side=scene;side.camera.position={300,60,0};save_scene_atomic(side,"top-lobes-"+name+"-side.white.json");
        std::cout<<"top_lobes_mode="<<name<<" generated="<<plan.hierarchy().size()<<" maximum="<<plan.maximum()<<" source_build_cpu_ms="<<std::chrono::duration<double,std::milli>(end-begin).count()<<" lower_density_exact=true PASS\n";
    }
    if(frame==165){
        auto before=session.document().scene();const auto nodes=generate_top_lobes(*before.top_lobes);const auto id=before.top_lobes->target_cell;const auto target=std::find_if(before.top_lobes->trunk.cells.begin(),before.top_lobes->trunk.cells.end(),[&](const auto& c){return c.id==id;});
        auto scene=scene_with_developed_command(before,DevelopedEdit{id,CumulonimbusCommand{CumulonimbusParameter::detail_seed,target->shape.source.parameters.detail_seed+1}});
        if(generate_top_lobes(*scene.top_lobes)!=nodes)throw std::runtime_error("Detail seed changed top hierarchy");
        apply(scene);if(!session.undo()||session.document().scene()!=before||!session.redo()||session.document().scene()!=scene)throw std::runtime_error("Top source one-step Undo failed");
        session.save("top-lobes-detail.white.json");session.load("top-lobes-detail.white.json",true);if(session.document().scene()!=scene)throw std::runtime_error("Top source Save/Open mismatch");
        std::cout<<"top_lobes_detail_seed_topology_preserved=true single_undo=true save_reload=true PASS\n";
    }
}
}
