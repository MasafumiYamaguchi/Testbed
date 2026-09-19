#include "white/editor_ui.hpp"
#include "white/editor_geometry.hpp"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
namespace white {
void EditorUi::developed_item(bool changed,const DevelopedCommand& command) {
    auto scene=session.document().scene();
    if(changed)try{scene=scene_with_developed_command(scene,command);}catch(const std::exception& e){status_=e.what();changed=false;}
    inspector_item(changed,std::move(scene));
}
void EditorUi::draw_developed_ui() {
    if(!session.document().scene().developed)return;
    auto source=*session.document().scene().developed;
    if(!source.cells.empty()&&std::none_of(source.cells.begin(),source.cells.end(),[&](const auto& c){return c.id==development_;}))development_=source.cells.front().id;
    if(!ImGui::CollapsingHeader("Independent developed cells",ImGuiTreeNodeFlags_DefaultOpen))return;
    ImGui::Text("%zu / 2 cells; %zu / 8 lobes",source.cells.size(),developed_primitive_count(source));
    if(source.cells.size()>1)ImGui::TextWrapped("Direct density and shadows: each cell keeps its own profile and protected masks.");
    const bool dragging=gizmo_drag_||inspector_drag_||orbit_drag_;
    ImGui::BeginDisabled(dragging);
    for(const auto& c:source.cells){const auto label="Development "+std::to_string(c.id);if(ImGui::Selectable(label.c_str(),development_==c.id)){development_=c.id;prefab_group_=true;curve_point_=0;}}
    if(ImGui::Button("Add development"))try{auto cell=make_developed_cell(source);cell.translation.x=source.cells.empty()?0:90;development_=cell.id;apply(scene_with_developed_command(session.document().scene(),DevelopedAdd{cell}));prefab_group_=true;curve_point_=0;}catch(const std::exception& e){status_=e.what();}
    ImGui::EndDisabled();
    source=*session.document().scene().developed;
    const auto found=std::find_if(source.cells.begin(),source.cells.end(),[&](const auto& c){return c.id==development_;});
    if(found==source.cells.end())return;
    auto cell=*found;
    ImGui::BeginDisabled(dragging);
    ImGui::Checkbox("New seeds on duplicate",&duplicate_regenerate_);
    ImGui::TextWrapped("Keep seeds retains numeric values. New stable IDs have independent local noise streams.");
    if(ImGui::Button("Duplicate development"))try{apply(scene_with_developed_command(session.document().scene(),DevelopedDuplicate{development_,duplicate_regenerate_?DevelopedDuplicateSeed::regenerate:DevelopedDuplicateSeed::retain,cell.shape.source.parameters.structure_seed+1}));}catch(const std::exception& e){status_=e.what();}
    ImGui::SameLine();if(ImGui::Button("Delete"))try{apply(scene_with_developed_command(session.document().scene(),DevelopedRemove{development_}));curve_point_=0;}catch(const std::exception& e){status_=e.what();}
    int roles=int(cell.roles.size())-3;
    if(ImGui::Combo("Lobe budget",&roles,"3 lobes\0 4 lobes\0 5 lobes\0"))try{std::vector<unsigned> active;for(int i=0;i<roles+3;++i)active.push_back(unsigned(i));apply(scene_with_developed_command(session.document().scene(),DevelopedSetRoles{development_,active}));}catch(const std::exception& e){status_=e.what();}
    ImGui::EndDisabled();
    if(std::none_of(session.document().scene().developed->cells.begin(),session.document().scene().developed->cells.end(),[&](const auto& c){return c.id==development_;}))return;
    auto fresh=[&](){const auto& cells=session.document().scene().developed->cells;return *std::find_if(cells.begin(),cells.end(),[&](const auto& c){return c.id==development_;});};
    if(ImGui::RadioButton("Move cell",!scale_&&prefab_group_)){scale_=false;prefab_group_=true;curve_point_=0;}ImGui::SameLine();
    if(ImGui::RadioButton("Size cell",scale_&&prefab_group_)){scale_=true;prefab_group_=true;curve_point_=0;}
    auto position=fresh().translation;ImGui::SetNextItemWidth(-1);
    bool changed=ImGui::InputScalarN("##DevelopmentPosition",ImGuiDataType_Double,&position.x,3,nullptr,nullptr,"%.2f");developed_item(changed,DevelopedMove{development_,position});
    ImGui::TextUnformatted("Position in object-local metres");
    ImGui::PushItemWidth(100);
    for(const auto parameter:{CumulonimbusParameter::width,CumulonimbusParameter::height,CumulonimbusParameter::cloud_base,CumulonimbusParameter::density}){
        auto value=std::get<double>(cumulonimbus_value(fresh().shape.source.parameters,parameter));
        const char* label=parameter==CumulonimbusParameter::width?"Cell width m":parameter==CumulonimbusParameter::height?"Cell height m":parameter==CumulonimbusParameter::cloud_base?"Cell base m":"Cell density";
        changed=ImGui::InputDouble(label,&value,0,0,"%.2f");developed_item(changed,DevelopedEdit{development_,CumulonimbusCommand{parameter,value}});
    }
    auto direction=fresh().shape.source.parameters.growth_direction;ImGui::SetNextItemWidth(-1);
    changed=ImGui::InputScalarN("##CellGrowth",ImGuiDataType_Double,&direction.x,3,nullptr,nullptr,"%.3f");const double length=std::sqrt(dot(direction,direction));if(changed&&length>0)direction=direction*(1/length);
    developed_item(changed,DevelopedEdit{development_,CumulonimbusCommand{CumulonimbusParameter::growth_direction,direction}});ImGui::TextUnformatted("Growth direction (y >= 0.2)");
    for(const auto parameter:{CumulonimbusParameter::structure_seed,CumulonimbusParameter::detail_seed}){auto value=std::get<std::uint64_t>(cumulonimbus_value(fresh().shape.source.parameters,parameter));changed=ImGui::InputScalar(parameter==CumulonimbusParameter::structure_seed?"Cell structure seed":"Cell detail seed",ImGuiDataType_U64,&value);developed_item(changed,DevelopedEdit{development_,CumulonimbusCommand{parameter,value}});}
    ImGui::PopItemWidth();
    if(ImGui::TreeNode("Cell centerline and profiles")){
        const auto shape=fresh().shape;
        for(const auto& p:shape.points){auto label="Control "+std::to_string(p.id);if(ImGui::Selectable(label.c_str(),curve_point_==p.id&&!prefab_group_)){curve_point_=p.id;prefab_group_=false;scale_=false;}}
        auto point=std::find_if(shape.points.begin(),shape.points.end(),[&](const auto& p){return p.id==curve_point_;});
        if(point!=shape.points.end()){
            auto p=sample_centerline(shape,point->t).position;ImGui::SetNextItemWidth(-1);changed=ImGui::InputScalarN("##DevelopmentControl",ImGuiDataType_Double,&p.x,3,nullptr,nullptr,"%.2f");developed_item(changed,DevelopedEdit{development_,CenterlineMovePoint{point->id,p}});
            ImGui::BeginDisabled(dragging);
            if(ImGui::SmallButton("Insert control"))try{if(point+1==shape.points.end())throw std::invalid_argument("Select a control below the top");Id id=0;for(const auto& p:shape.points)id=std::max(id,p.id);apply(scene_with_developed_command(session.document().scene(),DevelopedEdit{development_,CenterlineInsertPoint{id+1,(point->t+(point+1)->t)*.5}}));curve_point_=id+1;}catch(const std::exception& e){status_=e.what();}
            ImGui::SameLine();if(ImGui::SmallButton("Delete control"))try{apply(scene_with_developed_command(session.document().scene(),DevelopedEdit{development_,CenterlineRemovePoint{curve_point_}}));}catch(const std::exception& e){status_=e.what();}ImGui::EndDisabled();
        }
        for(const auto& knot:shape.profile){ImGui::PushID(std::to_string(knot.id).c_str());auto entries=fresh().shape.profile;auto k=*std::find_if(entries.begin(),entries.end(),[&](const auto& p){return p.id==knot.id;});
            ImGui::Text("Height %.0f%%",k.t*100);changed=ImGui::InputDouble("Radius",&k.radius_scale,0,0,"%.3f");developed_item(changed,DevelopedEdit{development_,CenterlineSetProfile{k.id,k.radius_scale,k.density_scale}});
            auto latest=fresh().shape.profile; k=*std::find_if(latest.begin(),latest.end(),[&](const auto& p){return p.id==knot.id;});changed=ImGui::InputDouble("Density profile",&k.density_scale,0,0,"%.3f");developed_item(changed,DevelopedEdit{development_,CenterlineSetProfile{k.id,k.radius_scale,k.density_scale}});ImGui::PopID();}
        ImGui::TextWrapped("Radius scales selected lobes at center heights; density varies continuously by local height.");ImGui::TreePop();
    }
}
void EditorUi::start_developed_test(){
    auto scene=new_developed_scene(new_centerline_scene(new_cumulonimbus_scene(session.document().scene())));
    scene.camera.position={45,60,360};scene.camera.target={45,60,0};session.apply(scene);development_=scene.developed->cells.front().id;prefab_group_=true;curve_point_=0;scale_=false;smoke_original_=scene;session.save("developed-single.white.json");
}
void EditorUi::developed_test_input(int frame){
    if(frame==80){auto scene=session.document().scene();auto cell=make_developed_cell(*scene.developed);cell.translation={90,0,0};cell.shape.source.parameters.structure_seed=987654321;cell.shape.profile={{1,0,1,1},{2,.5,1.25,.35},{3,1,.8,1}};development_=cell.id;apply(scene_with_developed_command(scene,DevelopedAdd{cell}));}
    if(frame==90){smoke_before_=session.document().scene();const auto& c=smoke_before_.developed->cells.back();auto& io=ImGui::GetIO();const float vx=290,vy=60,vw=io.DisplaySize.x-310,vh=io.DisplaySize.y-90;const auto view=camera_view(smoke_before_.camera),projection=camera_projection(smoke_before_.camera,vw/vh);const auto p=local_to_world(smoke_before_.cloud.transform,c.translation+Vec3{0,c.shape.source.parameters.cloud_base,0});const double x=view[0]*p.x+view[4]*p.y+view[8]*p.z+view[12],y=view[1]*p.x+view[5]*p.y+view[9]*p.z+view[13],z=view[2]*p.x+view[6]*p.y+view[10]*p.z+view[14];smoke_x_=vx+float(.5+.5*projection[0]*x/-z)*vw+35;smoke_y_=vy+float(.5-.5*projection[5]*y/-z)*vh;}
    if(frame>=100&&frame<=114){auto& io=ImGui::GetIO();io.AddMousePosEvent(smoke_x_+float(frame-100)*2,smoke_y_);if(frame==100)io.AddMouseButtonEvent(0,true);}
    if(frame==115)ImGui::GetIO().AddMouseButtonEvent(0,false);
}
void EditorUi::verify_developed_test(int frame){
    if(frame==120){const auto scene=session.document().scene();const auto& c=scene.developed->cells.back();if(c.translation.x<=90.1||gizmo_drag_)throw std::runtime_error("Development move gizmo did not move selected cell");const auto command=scene_with_developed_command(smoke_before_,DevelopedMove{development_,c.translation});if(scene!=command||!session.undo()||session.document().scene()!=smoke_before_||!session.redo()||session.document().scene()!=scene)throw std::runtime_error("Development gizmo/Command/Undo mismatch");std::cout<<"developed_move_gizmo_command_undo=true PASS\n";}
    if(frame==140){auto scene=session.document().scene();auto id=scene.developed->cells.back().id;auto next=scene_with_developed_command(scene,DevelopedEdit{id,CumulonimbusCommand{CumulonimbusParameter::height,155.0}});if(next.developed->cells.front()!=scene.developed->cells.front())throw std::runtime_error("Stretch changed neighboring development");apply(next);session.save("developed-smoke.white.json");session.load("developed-smoke.white.json",true);if(session.document().scene()!=next)throw std::runtime_error("Developed persistence mismatch");std::cout<<"developed_independent_stretch_source_save_reload=true PASS\n";}
    if(frame==170){auto before=session.document().scene();auto empty=before;while(!empty.developed->cells.empty())empty=scene_with_developed_command(empty,DevelopedRemove{empty.developed->cells.back().id});apply(empty);session.save("developed-empty.white.json");if(!session.undo()||session.document().scene()!=before)throw std::runtime_error("Empty development restore failed");std::cout<<"developed_empty_source_undo=true PASS\n";}
    if(frame==155){auto before=session.document().scene();auto id=before.developed->cells.back().id;apply(scene_with_developed_command(before,DevelopedRemove{id}));if(!session.undo()||session.document().scene()!=before)throw std::runtime_error("Development delete undo failed");std::cout<<"developed_delete_undo=true PASS\n";}
}
}
