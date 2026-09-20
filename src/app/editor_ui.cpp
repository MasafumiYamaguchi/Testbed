#include "white/editor_ui.hpp"
#include "white/editor_geometry.hpp"
#include <imgui.h>
#include <ImGuizmo.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <stdexcept>

namespace white {
namespace {
void draw_primitive(const Scene& scene,Vec3 center,Vec3 radii,bool selected,bool cut,float x,float y,float width,float height) {
    const auto view=camera_view(scene.camera),projection=camera_projection(scene.camera,width/height);
    auto project=[&](Vec3 local,ImVec2& out) {
        const auto p=local_to_world(scene.cloud.transform,local);
        const double vx=view[0]*p.x+view[4]*p.y+view[8]*p.z+view[12];
        const double vy=view[1]*p.x+view[5]*p.y+view[9]*p.z+view[13];
        const double vz=view[2]*p.x+view[6]*p.y+view[10]*p.z+view[14];
        if(-vz<scene.camera.near_plane)return false;
        out={x+float(0.5+0.5*projection[0]*vx/-vz)*width,y+float(0.5-0.5*projection[5]*vy/-vz)*height};return true;
    };
    auto* draw=ImGui::GetForegroundDrawList();draw->PushClipRect({x,y},{x+width,y+height},true);
    const auto color=cut?IM_COL32(255,160,90,selected?230:95):IM_COL32(115,195,255,selected?210:65);
    for(int axis=0;axis<3;++axis)for(int segment=0;segment<64;++segment) {
        if(!selected&&segment%2)continue;
        auto point=[&](int i){double a=i*6.283185307179586/64;Vec3 v=center;
            if(axis==0){v.y+=radii.y*std::cos(a);v.z+=radii.z*std::sin(a);}
            if(axis==1){v.x+=radii.x*std::cos(a);v.z+=radii.z*std::sin(a);}
            if(axis==2){v.x+=radii.x*std::cos(a);v.y+=radii.y*std::sin(a);}return v;};
        ImVec2 a,b;if(project(point(segment),a)&&project(point(segment+1),b))draw->AddLine(a,b,color,selected?1.5f:1.0f);
    }
    if(selected){ImVec2 p;if(project(center,p)){
        const auto delta=local_to_world(scene.cloud.transform,center)-scene.camera.position;
        char label[64];std::snprintf(label,sizeof(label),"%s / %.1f m",cut?"Cut":"Cell",std::sqrt(dot(delta,delta)));
        draw->AddText({p.x+12,p.y+12},color,label);
    }}
    draw->PopClipRect();
}
}
void EditorUi::apply(Scene scene) {
    try{session.apply(std::move(scene));status_="Edited";}catch(const std::exception& e){status_=e.what();}
}
void EditorUi::inspector_item(bool changed,Scene scene) {
    if(ImGui::IsItemActivated()&&!inspector_drag_){session.begin_drag();inspector_drag_=true;}
    if(changed)apply(std::move(scene));
    if(ImGui::IsItemDeactivated()&&inspector_drag_){session.end_drag();inspector_drag_=false;}
}
void EditorUi::camera_preset(int direction) {
    auto s=session.document().scene();const auto target=s.camera.target;
    if(direction==0){s.camera.position=target+Vec3{0,0,180};s.camera.up={0,1,0};}
    if(direction==1){s.camera.position=target+Vec3{180,0,0};s.camera.up={0,1,0};}
    if(direction==2){s.camera.position=target+Vec3{0,180,0};s.camera.up={0,0,-1};}
    apply(std::move(s));
}
void EditorUi::draw(GpuSpike& gpu) {
    ImGuizmo::BeginFrame();ImGuizmo::Enable(true);
    auto& io=ImGui::GetIO();
    if(ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        session.cancel_drag();gizmo_drag_=inspector_drag_=orbit_drag_=false;ImGuizmo::Enable(false);status_="Edit cancelled";
    }
    if(!io.WantTextInput&&io.KeyCtrl&&ImGui::IsKeyPressed(ImGuiKey_Z))session.undo();
    if(!io.WantTextInput&&io.KeyCtrl&&ImGui::IsKeyPressed(ImGuiKey_Y))session.redo();
    ImGui::SetNextWindowPos({15,15},ImGuiCond_Always);ImGui::SetNextWindowSize({260,io.DisplaySize.y-30},ImGuiCond_Always);
    ImGui::Begin("Cloud editor",nullptr,ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoCollapse);
    ImGui::Text("PROJECT WHITE / PHASE 0");ImGui::TextUnformatted(session.modified()?"Unsaved changes":"Saved");
    const bool dragging=gizmo_drag_||inspector_drag_||orbit_drag_;
    ImGui::BeginDisabled(dragging);
    if(ImGui::Button("Undo"))session.undo();
    ImGui::SameLine();
    if(ImGui::Button("Redo"))session.redo();
    ImGui::SetNextItemWidth(-1);ImGui::InputText("##File",filename_,sizeof(filename_));
    if(ImGui::Button("Save"))try{session.save(std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(filename_))));status_="Saved";}catch(const std::exception& e){status_=e.what();}
    ImGui::SameLine();
    if(ImGui::Button("Open")) {
        if(session.modified())ImGui::OpenPopup("Unsaved changes");
        else try{session.load(std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(filename_))));status_="Opened";}catch(const std::exception& e){status_=e.what();}
    }
    if(ImGui::BeginPopupModal("Unsaved changes",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Opening replaces the current unsaved scene.");
        if(ImGui::Button("Discard and open")) {
            try{session.load(std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(filename_))),true);status_="Opened";}catch(const std::exception& e){status_=e.what();}
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();if(ImGui::Button("Cancel"))ImGui::CloseCurrentPopup();ImGui::EndPopup();
    }
    ImGui::Separator();ImGui::Checkbox("Select cuts",&select_cuts_);
    if(ImGui::Button("Add cell"))try{selected_=session.add_cell();select_cuts_=false;}catch(const std::exception& e){status_=e.what();}
    ImGui::SameLine();if(ImGui::Button("Add cut")) try {
        auto s=session.document().scene();selected_=next_id(s);s.cloud.cuts.push_back({selected_,{12,40,0},{17,20,26},3});apply(std::move(s));select_cuts_=true;
    }catch(const std::exception& e){status_=e.what();}
    if(ImGui::Button("Delete selected")) {
        auto s=session.document().scene();
        if(select_cuts_)std::erase_if(s.cloud.cuts,[&](auto& c){return c.id==selected_;});
        else std::erase_if(s.cloud.cells,[&](auto& c){return c.id==selected_;});
        apply(std::move(s));
    }
    ImGui::EndDisabled();
    const auto& scene=session.document().scene();
    if(select_cuts_){for(const auto& cut:scene.cloud.cuts){auto label="Cut "+std::to_string(cut.id);if(ImGui::Selectable(label.c_str(),selected_==cut.id))selected_=cut.id;}}
    else {for(const auto& cell:scene.cloud.cells){auto label="Cell "+std::to_string(cell.id);if(ImGui::Selectable(label.c_str(),selected_==cell.id))selected_=cell.id;}}
    if(ImGui::RadioButton("Move",!scale_))scale_=false;
    ImGui::SameLine();if(ImGui::RadioButton("Scale",scale_))scale_=true;
    ImGui::Separator();
    auto s=session.document().scene();Vec3* center=nullptr;Vec3* radii=nullptr;
    if(select_cuts_){for(auto& cut:s.cloud.cuts)if(cut.id==selected_){center=&cut.center;radii=&cut.radii;}}
    else {for(auto& cell:s.cloud.cells)if(cell.id==selected_){center=&cell.center;radii=&cell.radii;}}
    if(center) {
        ImGui::TextUnformatted("Center (local metres)");ImGui::SetNextItemWidth(-1);
        bool changed=ImGui::InputScalarN("##Center",ImGuiDataType_Double,&center->x,3,nullptr,nullptr,"%.2f");inspector_item(changed,s);
        ImGui::TextUnformatted("Radii (0.0001 .. 10000)");ImGui::SetNextItemWidth(-1);
        changed=ImGui::InputScalarN("##Radii",ImGuiDataType_Double,&radii->x,3,nullptr,nullptr,"%.2f");inspector_item(changed,s);
    }
    s=session.document().scene();if(ImGui::Checkbox("Flat base",&s.cloud.base.enabled))apply(s);
    s=session.document().scene();ImGui::SetNextItemWidth(130);
    const bool base_changed=ImGui::InputDouble("Base height",&s.cloud.base.height,0,0,"%.2f");inspector_item(base_changed,s);
    ImGui::Separator();
    ImGui::TextUnformatted("Camera");if(ImGui::Button("Front"))camera_preset(0);ImGui::SameLine();if(ImGui::Button("Side"))camera_preset(1);ImGui::SameLine();if(ImGui::Button("Top"))camera_preset(2);
    ImGui::Checkbox("Lit volume",&gpu.show_volume);
    if(focus_noise_)ImGui::SetNextItemOpen(true);
    if(ImGui::CollapsingHeader("Shape details")) {
        ImGui::PushItemWidth(110);
        s=session.document().scene();float medium=float(s.cloud.noise.medium_strength);
        bool changed=ImGui::SliderFloat("Medium",&medium,0,1);s.cloud.noise.medium_strength=medium;inspector_item(changed,s);
        s=session.document().scene();float erosion=float(s.cloud.noise.micro_erosion);
        changed=ImGui::SliderFloat("Edge erosion m",&erosion,0,20);s.cloud.noise.micro_erosion=erosion;inspector_item(changed,s);
        s=session.document().scene();float warp=float(s.cloud.noise.warp_amplitude);
        changed=ImGui::SliderFloat("Warp bound m",&warp,0,20);s.cloud.noise.warp_amplitude=warp;inspector_item(changed,s);
        s=session.document().scene();float medium_freq=float(s.cloud.noise.medium_frequency);
        changed=ImGui::SliderFloat("Medium freq",&medium_freq,0.0001f,2,"%.4f",ImGuiSliderFlags_Logarithmic);s.cloud.noise.medium_frequency=medium_freq;inspector_item(changed,s);
        s=session.document().scene();float micro_freq=float(s.cloud.noise.micro_frequency);
        changed=ImGui::SliderFloat("Edge freq",&micro_freq,0.0001f,2,"%.4f",ImGuiSliderFlags_Logarithmic);s.cloud.noise.micro_frequency=micro_freq;inspector_item(changed,s);
        s=session.document().scene();float warp_freq=float(s.cloud.noise.warp_frequency);
        changed=ImGui::SliderFloat("Warp freq",&warp_freq,0.0001f,2,"%.4f",ImGuiSliderFlags_Logarithmic);s.cloud.noise.warp_frequency=warp_freq;inspector_item(changed,s);
        ImGui::BeginDisabled(gizmo_drag_||inspector_drag_||orbit_drag_);
        if(ImGui::Button("Regenerate detail only")){s=session.document().scene();++s.cloud.detail_seed;apply(std::move(s));}
        if(ImGui::Button("Noise off")){s=session.document().scene();s.cloud.noise.medium_strength=s.cloud.noise.micro_erosion=s.cloud.noise.warp_amplitude=0;apply(std::move(s));}
        ImGui::EndDisabled();
        ImGui::Text("Detail seed: %llu",static_cast<unsigned long long>(session.document().scene().cloud.detail_seed));
        ImGui::TextWrapped("Frequencies: cycles/local metre. Base, envelope and full cuts stay protected.");
        ImGui::PopItemWidth();
        if(focus_noise_){ImGui::SetScrollHereY(1);focus_noise_=false;}
    }
    if(ImGui::CollapsingHeader("Dense cache")) {
        bool cached=gpu.use_cache;
        if(ImGui::Checkbox("Use baked density",&cached))try{
            if(cached)gpu.set_cache_resolution(gpu.cache_resolution);
            gpu.use_cache=cached;gpu.volume_dirty=true;status_=cached?"Dense cache":"Direct density";
        }catch(const std::exception& e){status_=e.what();}
        int resolution=gpu.cache_resolution==256?1:0;
        if(ImGui::Combo("Grid",&resolution,"128 cubed\0 256 cubed\0"))try{gpu.set_cache_resolution(resolution?256:128);}catch(const std::exception& e){status_=e.what();}
        ImGui::Text("Bakes: %llu",static_cast<unsigned long long>(gpu.bake_count));
        ImGui::Text("Resource budget: %.1f MiB",gpu.estimated_gpu_bytes/1048576.0);
        ImGui::TextWrapped("Filtering changes fine edges. Base and full cuts remain clipped.");
    }
    if(ImGui::CollapsingHeader("View settings")) {
        ImGui::PushItemWidth(120);
        if(ImGui::SliderInt("View steps",&gpu.view_steps,8,256))gpu.volume_dirty=true;
        if(ImGui::SliderInt("Shadow steps",&gpu.shadow_steps,1,32))gpu.volume_dirty=true;
        if(ImGui::SliderInt("Internal width",&gpu.internal_width,64,320))gpu.volume_dirty=true;
        s=session.document().scene();float exposure=float(s.exposure_ev);
        const bool change=ImGui::SliderFloat("Exposure EV",&exposure,-4,4);s.exposure_ev=exposure;inspector_item(change,s);
        s=session.document().scene();float angle=float(std::atan2(s.sun.direction_to_light.x,s.sun.direction_to_light.z)*180/3.141592653589793);
        const bool sun_changed=ImGui::SliderFloat("Sun angle",&angle,-180,180);
        if(sun_changed){double a=angle*3.141592653589793/180;Vec3 d{std::sin(a),0.8,std::cos(a)};s.sun.direction_to_light=d*(1/std::sqrt(dot(d,d)));}
        inspector_item(sun_changed,s);
        ImGui::Combo("Slice axis",&axis,"X (YZ)\0Y (XZ)\0Z (XY)\0");ImGui::SliderFloat("Slice",&slice,0,1);
        ImGui::PopItemWidth();
    }
    if(ImGui::CollapsingHeader("Validation")) {
        if(ImGui::Button("Retry preview"))last_scene_attempt_=0;
        if(ImGui::Button("Check density"))try{gpu.validate();status_=gpu.report;}catch(const std::exception& e){status_=e.what();}
        ImGui::Text("Revision: %llu",static_cast<unsigned long long>(session.document().revision()));
        ImGui::Text("Error: %.8f",gpu.max_error);
    }
    ImGui::Separator();ImGui::TextWrapped("%s",status_.c_str());ImGui::End();

    const float vx=290,vy=60,vw=std::max(1.0f,io.DisplaySize.x-310),vh=std::max(1.0f,io.DisplaySize.y-90);
    auto current=session.document().scene();Vec3 selected_center{},selected_radii{};bool selected=false;
    if(select_cuts_){for(auto& c:current.cloud.cuts)if(c.id==selected_){selected_center=c.center;selected_radii=c.radii;selected=true;}}
    else {for(auto& c:current.cloud.cells)if(c.id==selected_){selected_center=c.center;selected_radii=c.radii;selected=true;}}
    if(gpu.show_volume){
        for(const auto& c:current.cloud.cells)draw_primitive(current,c.center,c.radii,!select_cuts_&&selected_==c.id,false,vx,vy,vw,vh);
        for(const auto& c:current.cloud.cuts)draw_primitive(current,c.center,c.radii,select_cuts_&&selected_==c.id,true,vx,vy,vw,vh);
    }
    const bool modal=ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId);
    const bool gizmo_active=selected&&gpu.show_volume&&!inspector_drag_&&!orbit_drag_&&!modal;
    if(gizmo_active) {
        auto view=camera_view(current.camera),projection=camera_projection(current.camera,vw/vh),model=primitive_matrix(current.cloud.transform,selected_center,selected_radii);
        ImGuizmo::SetDrawlist(ImGui::GetForegroundDrawList());ImGuizmo::SetRect(vx,vy,vw,vh);ImGuizmo::SetOrthographic(false);
        const bool manipulated=ImGuizmo::Manipulate(view.data(),projection.data(),scale_?ImGuizmo::SCALE:ImGuizmo::TRANSLATE,ImGuizmo::LOCAL,model.data());
        const bool using_now=ImGuizmo::IsUsing();
        if(using_now&&!gizmo_drag_){session.begin_drag();gizmo_drag_=true;}
        if(manipulated) {
            primitive_from_matrix(current.cloud.transform,model,selected_center,selected_radii);
            if(select_cuts_){for(auto& c:current.cloud.cuts)if(c.id==selected_){c.center=selected_center;c.radii=selected_radii;}}
            else {for(auto& c:current.cloud.cells)if(c.id==selected_){c.center=selected_center;c.radii=selected_radii;}}
            apply(std::move(current));
        }
        if(!using_now&&gizmo_drag_){session.end_drag();gizmo_drag_=false;}
    }
    const bool over=io.MousePos.x>=vx&&io.MousePos.x<vx+vw&&io.MousePos.y>=vy&&io.MousePos.y<vy+vh;
    if(over&&!modal&&!(gizmo_active&&ImGuizmo::IsOver())&&!gizmo_drag_&&!inspector_drag_) {
        if(ImGui::IsMouseClicked(ImGuiMouseButton_Left)&&gpu.show_volume) {
            const auto& sc=session.document().scene();auto ray=camera_ray(sc.camera,(io.MousePos.x-vx)/vw,(io.MousePos.y-vy)/vh,vw/vh);
            if(auto pick=pick_primitive(sc.cloud,ray,select_cuts_))selected_=*pick;
        }
        if(ImGui::IsMouseClicked(ImGuiMouseButton_Right)){session.begin_drag();orbit_drag_=true;}
        if(io.MouseWheel!=0&&!orbit_drag_) {
            auto sc=session.document().scene();auto delta=sc.camera.position-sc.camera.target;double length=std::sqrt(dot(delta,delta));
            sc.camera.position=sc.camera.target+delta*(std::clamp(length*std::exp(-io.MouseWheel*0.15),1.0,10000.0)/length);apply(std::move(sc));
        }
    }
    if(orbit_drag_) {
        if(ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
            auto sc=session.document().scene();auto delta=sc.camera.position-sc.camera.target;double length=std::sqrt(dot(delta,delta));
            double yaw=std::atan2(delta.x,delta.z)-io.MouseDelta.x*0.008,pitch=std::clamp(std::asin(delta.y/length)+io.MouseDelta.y*0.008,-1.45,1.45);
            sc.camera.position=sc.camera.target+Vec3{length*std::cos(pitch)*std::sin(yaw),length*std::sin(pitch),length*std::cos(pitch)*std::cos(yaw)};sc.camera.up={0,1,0};apply(std::move(sc));
        }else {session.end_drag();orbit_drag_=false;}
    }
    ImGui::GetForegroundDrawList()->AddText({vx,20},IM_COL32(220,225,235,255),"Click: select | Right-drag: orbit | Wheel: zoom | Esc: cancel");
    if(session.document().revision()!=gpu.scene_revision&&session.document().revision()!=last_scene_attempt_) {
        last_scene_attempt_=session.document().revision();
        try{gpu.set_scene(session.document().scene(),session.document().revision());}
        catch(const std::exception& e){status_=std::string("Preview stale; document kept: ")+e.what();}
    }
}
void EditorUi::scripted_input(int frame) {
    auto& io=ImGui::GetIO();
    if(frame==90) {
        smoke_original_=session.document().scene();camera_preset(0);selected_=smoke_original_.cloud.cells[0].id;select_cuts_=scale_=false;
        smoke_before_=session.document().scene();
        const auto p=local_to_world(smoke_before_.cloud.transform,smoke_before_.cloud.cells[0].center);
        const auto c=smoke_before_.camera;const double distance=c.position.z-p.z;
        const double pixels=(io.DisplaySize.y-90)/(2*distance*std::tan(c.vertical_fov_degrees*3.141592653589793/360));
        smoke_x_=290+(io.DisplaySize.x-310)*0.5f+float((p.x-c.target.x)*pixels);
        smoke_y_=60+(io.DisplaySize.y-90)*0.5f-float((p.y-c.target.y)*pixels)-18;
    }
    if(frame==135){scale_=true;smoke_before_=session.document().scene();}
    if((frame>=95&&frame<=116)||(frame>=136&&frame<=156)) {
        const bool scaling=frame>=136;const int start=scaling?140:100,finish=scaling?150:114;
        float offset=float(std::clamp(frame-start,0,finish-start))*2;
        io.AddFocusEvent(true);io.AddMousePosEvent(smoke_x_,smoke_y_-offset);
        if(frame==start)io.AddMouseButtonEvent(0,true);
        if(frame==finish+1)io.AddMouseButtonEvent(0,false);
    }
}
void EditorUi::verify_scripted_input(int frame) {
    if(frame!=120&&frame!=160)return;
    const auto edited=session.document().scene();const auto& a=smoke_before_.cloud.cells[0];const auto& b=edited.cloud.cells[0];
    const bool change=frame==120?b.center.y>a.center.y+0.1:b.radii.y>a.radii.y+0.1;
    if(!change||edited.camera!=smoke_before_.camera||gizmo_drag_)throw std::runtime_error("ImGuizmo input smoke did not edit the selected primitive exclusively");
    if(!session.undo()||session.document().scene()!=smoke_before_)throw std::runtime_error("One gizmo drag must undo in one command");
    if(!session.redo()||session.document().scene()!=edited)throw std::runtime_error("Gizmo redo mismatch");
    if(!session.undo())throw std::runtime_error("Gizmo smoke reset failed");
    std::cout<<"gizmo_input="<<(frame==120?"translate":"scale")<<" camera_unchanged=true single_undo=true redo=true PASS\n";
    if(frame==160){session.apply(smoke_original_);scale_=false;}
}
void EditorUi::scripted_edit(int step) {
    auto s=session.document().scene();
    if(step==0)s.cloud.cells[0].radii.y*=1.3;
    if(step==1)s.cloud.cells[0].radii.x*=1.3;
    if(step==2){s.cloud.base.enabled=true;s.cloud.base.height=12;}
    if(step==3){selected_=next_id(s);s.cloud.cuts.push_back({selected_,{12,40,0},{17,20,26},3});select_cuts_=true;}
    if(step<4){session.begin_drag();session.apply(s);session.end_drag();}
    if(step==4) {
        const auto expected=session.document().scene();const auto revision=session.document().revision();
        if(!session.undo()||!session.redo()||session.document().scene()!=expected||session.document().revision()<=revision)throw std::runtime_error("Editor Undo/Redo smoke failed");
        session.save("editor-smoke.white.json");session.load("editor-smoke.white.json");
        if(session.document().scene()!=expected)throw std::runtime_error("Editor save/reload smoke failed");
    }
    if(step>=5&&step<=7)camera_preset(step-5);
    if(step==8) {
        auto inside=session.document().scene();inside.camera.position=local_to_world(inside.cloud.transform,inside.cloud.cells[0].center);
        inside.camera.target=inside.camera.position+Vec3{0,0,-1};inside.camera.up={0,1,0};apply(std::move(inside));
    }
    if(step==9){auto sun=session.document().scene();sun.camera=smoke_original_.camera;sun.sun.direction_to_light.x*=-1;sun.sun.direction_to_light.z*=-1;apply(std::move(sun));}
    if(step==10){auto empty=session.document().scene();empty.cloud.cells.clear();apply(std::move(empty));}
    if(step==11){session.apply(fixture_scene(4));select_cuts_=true;selected_=3;focus_noise_=true;}
    if(step>=12&&step<=16){
        auto noise=session.document().scene();const auto cells=noise.cloud.cells;const auto structure=noise.cloud.structure_seed;
        if(step==12)noise.cloud.noise.medium_strength=0.8;
        if(step==13)noise.cloud.noise.micro_erosion=8;
        if(step==14)noise.cloud.noise.warp_amplitude=8;
        if(step==15)++noise.cloud.detail_seed;
        if(step==16){noise.cloud.noise.medium_strength=1;noise.cloud.noise.micro_erosion=20;noise.cloud.noise.warp_amplitude=20;}
        session.apply(noise);
        if(session.document().scene().cloud.cells!=cells||session.document().scene().cloud.structure_seed!=structure)throw std::runtime_error("Noise edit changed macro parameters");
    }
    if(step==17){auto compare=fixture_scene(4);compare.cloud.noise.medium_strength=0.8;compare.cloud.noise.micro_erosion=8;compare.cloud.noise.warp_amplitude=8;compare.cloud.noise.micro_frequency=0.15;session.apply(compare);}
    std::cout<<"editor_smoke_step="<<step<<" revision="<<session.document().revision()<<" PASS\n";
}
}
