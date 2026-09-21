#include "white/developed_cells.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace white;
namespace {
void check(bool okay,const char* text){if(!okay)throw std::runtime_error(text);}
void near(double a,double b,double tolerance,const char* text){check(std::abs(a-b)<=tolerance,text);}
const DevelopedCell& cell(const DevelopedCloud& cloud,Id id){const auto found=std::find_if(cloud.cells.begin(),cloud.cells.end(),[&](const auto& c){return c.id==id;});if(found==cloud.cells.end())throw std::runtime_error("Missing developed cell");return *found;}
void reject(const DevelopedCloud& before,const DevelopedCommand& command,const char* diagnostic){
    const auto original=before;bool rejected=false;try{(void)command_developed_cloud(before,command);}catch(const std::exception& e){rejected=std::string(e.what()).find(diagnostic)!=std::string::npos;}check(rejected,"Missing command rejection");check(before==original,"Rejected command mutated source");
}
CenterlineShape curved(bool manual=false){
    CenterlineShape shape;shape.points[1].offset={10,0,-8};shape.profile={{1,0,1,.7},{2,.5,1.25,1},{3,1,.8,.4}};
    auto& m=shape.source.modifiers;m.transform.translation={15,8,-3};m.transform.scale={1.2,.8,1.1};m.noise.origin={9,2,-4};m.optics.g=.4;
    m.cuts={{80,{8,45,3},{5,8,6},2}};if(manual)m.manual_cells={{81,{80,35,15},{9,14,8},991}};
    return shape;
}
void exact_migration(){
    for(bool manual:{false,true}){
        const auto source=curved(manual);const DensityField original(lower_centerline_to_recipe(source));
        const auto cloud=develop_centerline(source);const DevelopedEvaluationPlan migrated(cloud);
        check(cloud.cells[0].roles==std::vector<unsigned>({0,1,2,3,4}),"Migration resampled five-role topology");
        check(lower_single_developed_recipe(cloud)==original.recipe(),"Migration lost exact Recipe state");
        check(migrated.local_support()==original.local_support()&&migrated.maximum()==original.maximum(),"Migration changed support or maximum");
        const auto bound=original.local_support();const GridLayout grid{bound,{17,13,11}};
        for(unsigned z=0;z<11;++z)for(unsigned y=0;y<13;++y)for(unsigned x=0;x<17;++x){const auto p=index_to_local(grid,{double(x),double(y),double(z)});check(migrated.at(p)==original.at(p),"Single-group migration changed density arithmetic");}
        const auto packet=migrated.gpu_params();const auto expected=gpu_density_params(original);
        check(packet.settings.x==1&&std::memcmp(&packet.groups[0],&expected,sizeof(expected))==0,"Migration changed density GPU uniforms");
    }
    const auto plain=develop_cumulonimbus(CumulonimbusGroup{});check(developed_primitive_count(plain)==5,"Plain prefab migration failed");
}
void budget_and_independence(){
    auto source=develop_centerline(curved());const Id first=source.cells[0].id;
    auto second=make_developed_cell(source);second.translation={170,0,0};second.shape.source.parameters.height=180;
    second.shape.points[1].offset={-14,0,9};second.shape.profile={{1,0,1,.2},{2,.5,1.4,.9},{3,1,1,.6}};
    second.shape.source.cell_adjustments={{second.shape.source.cell_ids[2],{1,2,3},{1.1,1.2,.9},991}};
    second.shape.source.modifiers.cuts={{second.shape.source.cell_ids.back()+1,{0,35,0},{3,5,4},1}};
    auto pair=command_developed_cloud(source,DevelopedAdd{second});check(developed_primitive_count(pair)==8&&pair.cells.size()==2,"Five-plus-three budget failed");
    check(cell(pair,first)==cell(source,first),"Addition changed unrelated developed curve");
    const DevelopedEvaluationPlan initial(pair);const auto old_first=initial.fields()[0].recipe();
    auto moved=command_developed_cloud(pair,DevelopedMove{second.id,{220,10,-5}});
    auto stretched=command_developed_cloud(moved,DevelopedEdit{second.id,CumulonimbusCommand{CumulonimbusParameter::height,240.0}});
    auto reseeded=command_developed_cloud(stretched,DevelopedEdit{second.id,CumulonimbusCommand{CumulonimbusParameter::structure_seed,UINT64_C(4294967313)}});
    check(cell(reseeded,first)==cell(pair,first),"Selected move/stretch/reseed changed another development");
    const DevelopedEvaluationPlan after(reseeded);check(after.fields()[0].recipe()==old_first,"Unrelated local generation changed after another development edit");
    for(const auto& primitive:old_first.cells)check(cell_random_key(old_first,primitive)==cell_random_key(after.fields()[0].recipe(),primitive),"Unrelated local random key changed");
    auto removed=command_developed_cloud(reseeded,DevelopedRemove{first});check(removed.cells.size()==1&&cell(removed,second.id)==cell(reseeded,second.id),"Delete required conversion or changed survivor");
    check(developed_primitive_count(removed)==3,"Deleting original group did not free its five primitives");
    auto duplicate=command_developed_cloud(removed,DevelopedDuplicate{second.id,DevelopedDuplicateSeed::retain});
    check(duplicate.cells.size()==2&&developed_primitive_count(duplicate)==6,"Three-role development duplicate failed");
    const auto& copied=duplicate.cells.back();check(copied.id!=second.id&&copied.shape.points==cell(removed,second.id).shape.points&&copied.shape.profile==cell(removed,second.id).shape.profile,"Duplicate lost independent curve/profile");
    check(copied.shape.source.parameters.structure_seed==cell(removed,second.id).shape.source.parameters.structure_seed,"Retain policy changed raw seed");
    for(unsigned i=0;i<5;++i)check(copied.shape.source.cell_ids[i]!=cell(removed,second.id).shape.source.cell_ids[i],"Duplicate reused reserved role ID");
    check(copied.shape.source.cell_adjustments[0].cell_id==copied.shape.source.cell_ids[2]&&copied.shape.source.cell_adjustments[0].center_offset==Vec3{1,2,3},"Duplicate broke local adjustment reference");
    check(copied.shape.source.modifiers.cuts[0].id!=cell(removed,second.id).shape.source.modifiers.cuts[0].id&&copied.shape.source.modifiers.cuts[0].center==Vec3{0,35,0},"Duplicate lost local cut or reused its ID");
    auto regenerated=command_developed_cloud(removed,DevelopedDuplicate{second.id,DevelopedDuplicateSeed::regenerate,123});check(regenerated.cells.back().shape.source.parameters.structure_seed==123,"Explicit regeneration seed lost");
    reject(source,DevelopedDuplicate{first,DevelopedDuplicateSeed::retain},"Eight-primitive");
    reject(pair,DevelopedAdd{make_developed_cell(pair)},"two independently");
    reject(pair,DevelopedSetRoles{second.id,{0,1,2,3}},"Eight-primitive");
    reject(removed,DevelopedSetRoles{second.id,{0,2,4}},"middle");
    auto expanded=command_developed_cloud(removed,DevelopedSetRoles{second.id,{4,0,2,1}});check(developed_primitive_count(expanded)==4&&cell(expanded,second.id).shape==cell(removed,second.id).shape,"Role-budget edit changed source generation");
    auto empty=command_developed_cloud(removed,DevelopedRemove{second.id});const DevelopedEvaluationPlan vacuum(empty);check(vacuum.maximum()==0&&vacuum.at({0,50,0})==0&&lower_single_developed_recipe(empty).cells.empty(),"Empty developed cloud did not become vacuum");
}
void fusion_and_support(){
    // Regression for a masked dominant shape: density must approach the other
    // shape continuously as the first coefficient vanishes, without occlusion.
    const double baseline=developed_density_union(-10,0,-1,1,4,.25);
    near(baseline,.5,1e-15,"Zero coefficient occluded overlapping shape");
    for(double c:{1e-3,1e-6,1e-9,0.})near(developed_density_union(-10,c,-1,1,4,.25),baseline+.25*c,1e-14,"Density-mask limit is discontinuous");
    for(double k:{0.,.1,4.,100.}){
        check(developed_density_union(1000,1,1000,1,k,1)==0,"Smooth fusion invented distant ghost density");
        check(developed_density_union(.1,0,.1,0,k,1)==0,"Zero-density shapes invented a bridge");
        for(double a:{0.,.1,1.,5.})for(double b:{0.,.2,2.,7.})for(double overlap:{0.,.25,1.})for(double da:{-10.,-1.,.1,10.})for(double db:{-10.,-1.,.1,10.}){
            const double value=developed_density_union(da,a,db,b,k,overlap);check(value>=0&&value<=std::max(a,b)+overlap*std::min(a,b)+1e-13,"Fusion exceeded density bound");
            check(value==developed_density_union(db,b,da,a,k,overlap),"Two-group fusion changed under exchange");
        }
    }
    check(developed_density_union(.1,1,.1,1,4,0)>0,"Geometric fusion did not create a bounded bridge");
    auto cloud=develop_centerline(curved());auto second=make_developed_cell(cloud);second.translation={55,0,0};second.shape.source.parameters.height=180;cloud=command_developed_cloud(cloud,DevelopedAdd{second});
    const DevelopedEvaluationPlan field(cloud);auto reversed=cloud;std::reverse(reversed.cells.begin(),reversed.cells.end());const DevelopedEvaluationPlan same(reversed);
    const auto bounds=field.local_support();const GridLayout grid{bounds,{19,17,13}};
    for(unsigned z=0;z<13;++z)for(unsigned y=0;y<17;++y)for(unsigned x=0;x<19;++x){const auto p=index_to_local(grid,{double(x),double(y),double(z)});const auto value=field.at(p);check(value==same.at(p)&&std::isfinite(value)&&value>=0&&value<=field.maximum(),"Grouped field changed with order or exceeded bounds");}
    for(unsigned axis=0;axis<3;++axis){Vec3 p=(bounds.min+bounds.max)*.5;double* coord=axis==0?&p.x:axis==1?&p.y:&p.z;*coord=axis==0?bounds.max.x+100:axis==1?bounds.max.y+100:bounds.max.z+100;check(field.at(p)==0,"Support exterior is not exactly zero");}
    bool rejected=false;try{(void)lower_single_developed_recipe(cloud);}catch(const std::invalid_argument&){rejected=true;}check(rejected,"Multi-profile source silently flattened to legacy Recipe");
    const auto packet=field.gpu_params();check(packet.settings.x==2&&packet.groups[0].config.z+packet.groups[1].config.z==8,"GPU packet exceeded or lost primitive budget");
    const GridLayout small{bounds,{7,5,3}};const auto baked=bake_developed_cloud(field,small);
    for(unsigned z=0;z<3;++z)for(unsigned y=0;y<5;++y)for(unsigned x=0;x<7;++x)check(baked[(z*5+y)*7+x]==float(field.at(index_to_local(small,{double(x),double(y),double(z)}))),"Baked evaluator differs from procedural group");
    auto narrow=develop_cumulonimbus(CumulonimbusGroup{});const Id id=narrow.cells[0].id;
    narrow.cells[0].shape.profile={{1,0,1,0},{2,.49,1,0},{3,.5,1,1},{4,1,1,1}};
    check(validate_developed_cloud(narrow).empty(),"Local steep-profile fixture should be valid before translation");
    reject(narrow,DevelopedMove{id,{0,100000,0}},"altitude modulation error budget");
    // Pure X/Z movement does not add Y cancellation to the altitude profile.
    check(validate_developed_cloud(command_developed_cloud(narrow,DevelopedMove{id,{100000,0,0}})).empty(),"Horizontal translation incorrectly invalidated altitude precision");
}
void local_mask_isolation(){
    auto cloud=develop_cumulonimbus(CumulonimbusGroup{});const auto second=make_developed_cell(cloud);
    cloud=command_developed_cloud(cloud,DevelopedAdd{second});
    cloud.cells[0].shape.source.modifiers.cuts.push_back({next_developed_id(cloud),{0,40,0},{20,20,20},3});
    const DevelopedEvaluationPlan cut(cloud);const Vec3 p{0,40,0};
    const double surviving=cut.fields()[1].at(p);
    check(surviving>0,"Mask-isolation fixture has no surviving second development");
    near(cut.at(p),surviving,1e-12,"First development cut globally erased the second");
    auto reversed=cloud;std::reverse(reversed.cells.begin(),reversed.cells.end());
    near(DevelopedEvaluationPlan(reversed).at(p),surviving,1e-12,"Mask isolation changed with source order");
    cloud.cells[0].shape.source.modifiers.cuts.clear();cloud.cells[0].shape.source.parameters.cloud_base=100;
    const DevelopedEvaluationPlan base(cloud);
    near(base.at(p),base.fields()[1].at(p),1e-12,"First development base globally erased the second");
}
void fusion_commands(){
    auto original=develop_centerline(curved());auto second=make_developed_cell(original);
    second.translation={55,0,0};original=command_developed_cloud(original,DevelopedAdd{second});
    const DevelopedEvaluationPlan before(original);
    const auto wider=command_developed_cloud(original,DevelopedSetFusion{80,original.overlap});
    auto expected=original;expected.fusion_width=80;
    check(wider==expected,"Fusion-width edit changed overlap, stable IDs, or source modifiers");
    const DevelopedEvaluationPlan width_plan(wider);
    check(width_plan.maximum()==before.maximum(),"Shape fusion changed the density-overlap bound");
    check(width_plan.local_support().min.x<before.local_support().min.x&&width_plan.local_support().max.x>before.local_support().max.x,"Fusion-width edit failed to expand conservative support");
    const auto denser=command_developed_cloud(wider,DevelopedSetFusion{wider.fusion_width,.6});
    expected.overlap=.6;check(denser==expected,"Overlap edit changed fusion width, stable IDs, or source modifiers");
    const DevelopedEvaluationPlan overlap_plan(denser);
    check(overlap_plan.local_support()==width_plan.local_support(),"Density overlap changed geometric support");
    const double a=overlap_plan.fields()[0].maximum(),b=overlap_plan.fields()[1].maximum();
    near(overlap_plan.maximum(),std::max(a,b)+.6*std::min(a,b),1e-14,"Overlap edit failed to update conservative density bound");
    const auto packet=overlap_plan.gpu_params();
    check(packet.settings.y==80&&packet.settings.z==float(.6),"Independent fusion/overlap edits did not reach GPU packet");
    // Reapplying the prior shared settings restores the complete source; the
    // Scene command adapter stores this same atomic value as one undo entry.
    check(command_developed_cloud(denser,DevelopedSetFusion{original.fusion_width,original.overlap})==original,"Restoring fusion settings did not restore exact source");
    for(double invalid:{-1.,10001.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()})
        reject(original,DevelopedSetFusion{invalid,original.overlap},"Fusion width");
    for(double invalid:{-.01,1.01,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()})
        reject(original,DevelopedSetFusion{original.fusion_width,invalid},"Density overlap");
}
}
int main(){try{exact_migration();budget_and_independence();fusion_and_support();local_mask_isolation();fusion_commands();std::cout<<"Developed cells: exact legacy migration, independent curves, role budgets, deletion/duplicate/reseed, continuous bounded fusion, typed fusion/overlap controls, support and grouped packet passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
