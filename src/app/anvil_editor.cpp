#include "white/editor_ui.hpp"
#include "white/editor_geometry.hpp"
#include <imgui.h>
#include <ImGuizmo.h>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <stdexcept>
namespace white {
void EditorUi::anvil_item(bool changed,const AnvilSettings& settings){
    auto scene=session.document().scene();
    if(changed)try{scene=scene_with_anvil_settings(scene,settings);}catch(const std::exception& e){status_=e.what();changed=false;}
    inspector_item(changed,std::move(scene));
}
void EditorUi::draw_anvil_ui(){
    const auto& initial=session.document().scene();
    if(!initial.anvil){
        if(editable_developed_source(initial)){
            ImGui::BeginDisabled(gizmo_drag_||inspector_drag_||orbit_drag_);
            if(ImGui::Button("Add anvil controls"))try{apply(new_anvil_scene(initial));}catch(const std::exception& e){status_=e.what();}
            ImGui::EndDisabled();
        }return;
    }
    if(!ImGui::CollapsingHeader("Anvil",ImGuiTreeNodeFlags_DefaultOpen))return;
    auto p=session.document().scene().anvil->settings;
    bool changed=ImGui::Checkbox("Anvil enabled",&p.enabled);anvil_item(changed,p);
    p=session.document().scene().anvil->settings;changed=ImGui::Checkbox("Follow generation wind",&p.follow_wind);anvil_item(changed,p);
    auto number=[&](const char* name,double AnvilSettings::*member){auto s=session.document().scene().anvil->settings;ImGui::SetNextItemWidth(100);const bool edit=ImGui::InputDouble(name,&(s.*member),0,0,"%.3f");anvil_item(edit,s);};
    number("Start altitude m",&AnvilSettings::start_height);number("Thickness m",&AnvilSettings::thickness);
    number("Horizontal width m",&AnvilSettings::width);number("Forward extension m",&AnvilSettings::extension);
    number("Shear",&AnvilSettings::shear);number("Edge fade m",&AnvilSettings::edge_fade);number("Anvil density",&AnvilSettings::density_scale);
    p=session.document().scene().anvil->settings;double angle=std::atan2(p.direction.z,p.direction.x)*180/3.141592653589793;
    ImGui::SetNextItemWidth(100);changed=ImGui::InputDouble("Direction degrees",&angle,0,0,"%.2f");
    if(changed){angle*=3.141592653589793/180;p.direction={std::cos(angle),0,std::sin(angle)};p.follow_wind=false;}anvil_item(changed,p);
    ImGui::BeginDisabled(gizmo_drag_||inspector_drag_||orbit_drag_);
    ImGui::RadioButton("Cell handles",&anvil_handle_,0);ImGui::SameLine();ImGui::RadioButton("Width handle",&anvil_handle_,1);
    ImGui::RadioButton("Direction handle",&anvil_handle_,2);ImGui::EndDisabled();
    ImGui::TextWrapped("Width and direction handles use these same parameters. Direction edits select manual override. Growth uses the bent column as the attachment; wind is applied once.");
}
void EditorUi::draw_anvil_gizmo(float x,float y,float width,float height){
    auto scene=session.document().scene();const auto handle=anvil_handle_==1?AnvilHandle::width:AnvilHandle::direction;
    auto position=anvil_handle_position(*scene.anvil,handle);Vec3 radii{1,1,1};
    auto view=camera_view(scene.camera),projection=camera_projection(scene.camera,width/height),model=primitive_matrix(scene.cloud.transform,position,radii);
    ImGuizmo::SetDrawlist(ImGui::GetForegroundDrawList());ImGuizmo::SetRect(x,y,width,height);ImGuizmo::SetOrthographic(false);
    const bool changed=ImGuizmo::Manipulate(view.data(),projection.data(),ImGuizmo::OPERATION(ImGuizmo::TRANSLATE_X|ImGuizmo::TRANSLATE_Z),ImGuizmo::LOCAL,model.data());
    const bool using_now=ImGuizmo::IsUsing();if(using_now&&!gizmo_drag_){session.begin_drag();gizmo_drag_=true;}
    if(changed)try{primitive_from_matrix(scene.cloud.transform,model,position,radii);apply(scene_with_anvil_handle(scene,handle,position));}catch(const std::exception& e){status_=e.what();}
    if(!using_now&&gizmo_drag_){session.end_drag();gizmo_drag_=false;}
}
void EditorUi::start_anvil_test(){
    auto scene=new_anvil_scene(new_developed_scene(new_centerline_scene(new_cumulonimbus_scene())));
    auto settings=scene.anvil->settings;settings.enabled=true;settings.direction={0,0,-1};scene=scene_with_anvil_settings(scene,settings);
    scene.camera.position={0,75,390};scene.camera.target={0,75,0};session.apply(scene);
    development_=scene.anvil->cloud.target_cell;prefab_group_=true;anvil_handle_=1;
}
void EditorUi::anvil_test_input(int frame){
    if(frame==90||frame==130){
        anvil_handle_=frame==90?1:2;smoke_before_=session.document().scene();
        auto& io=ImGui::GetIO();const float vx=290,vy=60,vw=io.DisplaySize.x-310,vh=io.DisplaySize.y-90;
        const auto view=camera_view(smoke_before_.camera),projection=camera_projection(smoke_before_.camera,vw/vh);
        const auto p=local_to_world(smoke_before_.cloud.transform,anvil_handle_position(*smoke_before_.anvil,anvil_handle_==1?AnvilHandle::width:AnvilHandle::direction));
        const double x=view[0]*p.x+view[4]*p.y+view[8]*p.z+view[12],y=view[1]*p.x+view[5]*p.y+view[9]*p.z+view[13],z=view[2]*p.x+view[6]*p.y+view[10]*p.z+view[14];
        smoke_x_=vx+float(.5+.5*projection[0]*x/-z)*vw+35;smoke_y_=vy+float(.5-.5*projection[5]*y/-z)*vh;
    }
    if((frame>=95&&frame<=116)||(frame>=135&&frame<=156)){
        const int start=frame<130?100:140;auto& io=ImGui::GetIO();io.AddFocusEvent(true);
        io.AddMousePosEvent(smoke_x_+float(std::clamp(frame-start,0,14))*2,smoke_y_);
        if(frame==start)io.AddMouseButtonEvent(0,true);if(frame==start+15)io.AddMouseButtonEvent(0,false);
    }
}
void EditorUi::verify_anvil_test(int frame){
    if(frame==120||frame==160){
        const auto scene=session.document().scene();const auto& before=smoke_before_.anvil->settings;const auto& after=scene.anvil->settings;
        if(gizmo_drag_||scene.camera!=smoke_before_.camera||scene.anvil->cloud!=smoke_before_.anvil->cloud)throw std::runtime_error("Anvil handle changed camera/trunk or failed release");
        if(frame==120&&after.width<=before.width+1)throw std::runtime_error("Anvil width gizmo did not widen the sheet");
        if(frame==160&&(after.direction==before.direction||after.follow_wind))throw std::runtime_error("Anvil direction gizmo did not set manual override");
        if(scene_with_anvil_settings(smoke_before_,after)!=scene||!session.undo()||session.document().scene()!=smoke_before_||!session.redo()||session.document().scene()!=scene)throw std::runtime_error("Anvil gizmo/inspector/Undo mismatch");
        std::cout<<"anvil_"<<(frame==120?"width":"direction")<<"_gizmo_inspector_single_undo=true PASS\n";
    }
    if(frame==180){
        const auto scene=session.document().scene();session.save("anvil-edited.white.json");session.load("anvil-edited.white.json",true);
        if(session.document().scene()!=scene)throw std::runtime_error("Anvil editor Save/Open mismatch");
        std::cout<<"anvil_source_save_reload=true PASS\n";
    }
}
}
