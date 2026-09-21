#include "white/cumulonimbus.hpp"
#include "white/revision_queue.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>

using namespace white;
namespace {
void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
void near(double a,double b,const char* message){check(std::abs(a-b)<1e-10,message);}
const Cell& cell(const CloudRecipe& recipe,Id id) {
    const auto it=std::find_if(recipe.cells.begin(),recipe.cells.end(),[&](const auto& c){return c.id==id;});
    if(it==recipe.cells.end())throw std::runtime_error("Cell ID lost");
    return *it;
}
void rejected(const CumulonimbusGroup& group,const char* diagnostic) {
    const auto errors=validate_cumulonimbus(group);
    check(std::any_of(errors.begin(),errors.end(),[&](const auto& s){return s.find(diagnostic)!=std::string::npos;}),"Expected specific validation error");
    bool failed=false;try{(void)derive_cumulonimbus_graph(group);}catch(const std::invalid_argument&){failed=true;}
    check(failed,"Invalid source reached graph construction");
}
void evaluate(const CumulonimbusGroup& group,std::size_t& samples) {
    const auto recipe=derive_cumulonimbus_recipe(group);const auto graph=derive_cumulonimbus_graph(group);
    check(validate_field_graph(graph).empty(),"Prefab graph is invalid");
    check(lower_to_recipe(graph)==recipe,"Prefab graph changed source-derived recipe");
    const DensityField direct(recipe);const FieldEvaluationPlan plan(graph);
    const auto a=gpu_density_params(direct),b=plan.gpu_params();
    check(std::memcmp(&a,&b,sizeof(a))==0,"Prefab GPU uniforms differ from CPU recipe");
    const GridLayout grid{recipe.envelope,{19,23,17}};double peak=0;
    for(unsigned z=0;z<17;++z)for(unsigned y=0;y<23;++y)for(unsigned x=0;x<19;++x) {
        const auto p=index_to_local(grid,{double(x),double(y),double(z)});
        const auto value=plan.at(p);++samples;
        check(std::isfinite(value)&&value>=0&&value<=plan.maximum(),"Prefab sample invalid");
        check(value==direct.at(p),"Prefab graph and recipe CPU density differ");
        if(group.modifiers.base_enabled&&p.y<=group.parameters.cloud_base)check(value==0,"Cloud base did not clip density");
        peak=std::max(peak,value);
    }
    if(group.parameters.density>0)check(peak>0,"New prefab has no positive density");
}
}
int main(){try {
    std::size_t samples=0;CumulonimbusGroup original;
    check(validate_cumulonimbus(original).empty(),"Default Cumulonimbus is invalid");
    const auto recipe=derive_cumulonimbus_recipe(original);
    check(recipe.cells.size()==5&&recipe.base.enabled&&recipe.detail_seed==17,"Default prefab contract differs");
    check(DensityField(recipe).at({0,50,0})>0,"Default prefab center is empty");
    evaluate(original,samples);
    std::set<CumulonimbusParameter> described;
    for(const auto& info:cumulonimbus_parameter_info()) {
        check(!info.name.empty()&&!info.unit.empty()&&!info.range.empty(),"Parameter metadata lacks units or range");
        check(described.insert(info.parameter).second,"Repeated public parameter descriptor");
        (void)cumulonimbus_value(original.parameters,info.parameter);
    }
    check(described.size()==7,"Missing public parameter descriptor");
    // Published extrema are valid in combination, not merely in isolation.
    for(int mask=0;mask<8;++mask) {
        auto group=original;group.parameters.width=mask&1?2000:10;group.parameters.height=mask&2?4000:10;
        group.parameters.cloud_base=mask&4?100000:-100000;group.parameters.density=mask&1?1000:0;
        group.parameters.growth_direction={std::sqrt(0.96),0.2,0};
        check(validate_cumulonimbus(group).empty(),"Combined public parameter extrema invalid");
        const auto extreme=derive_cumulonimbus_recipe(group);
        check(extreme.base.height==group.parameters.cloud_base,"Cloud base extrema were clamped");
    }
    auto tilted=original;tilted.parameters.growth_direction={0.6,0.8,0};evaluate(tilted,samples);
    const auto tilted_recipe=derive_cumulonimbus_recipe(tilted);
    for(std::size_t i=0;i<5;++i) {
        const auto& before=recipe.cells[i];const auto& after=tilted_recipe.cells[i];
        near(after.center.x-before.center.x,before.center.y*0.75,"Growth direction did not tilt cell center");
        check(before.radii==after.radii&&before.id==after.id,"Growth direction altered dimensions or stable ID");
    }
    auto edited=original;
    edited.cell_adjustments.push_back({edited.cell_ids[2],{9,-2,5},{1.1,0.9,1.2},UINT64_C(991)});
    edited.modifiers.manual_cells.push_back({100,{55,60,8},{6,8,10},UINT64_C(77)});
    edited.modifiers.cuts.push_back({101,{2,40,8},{6,8,9},2});
    edited.modifiers.noise.origin={12,-5,4};edited.modifiers.noise.micro_erosion=1;
    edited.modifiers.optics.g=0.7;edited.modifiers.transform.translation={10,20,30};
    evaluate(edited,samples);
    CumulonimbusDocument command(edited),inspector(edited),gizmo(edited);
    command.apply({CumulonimbusParameter::height,240.0});
    inspector.apply({CumulonimbusParameter::height,240.0});
    gizmo.begin_edit();for(double height:{130.0,160.0,210.0,240.0})gizmo.apply({CumulonimbusParameter::height,height});gizmo.end_edit();
    check(command.group()==inspector.group()&&command.group()==gizmo.group(),"Command/Inspector/gizmo value paths disagree");
    const auto taller=derive_cumulonimbus_recipe(command.group());
    check(command.group().cell_ids==edited.cell_ids&&command.group().cell_adjustments==edited.cell_adjustments&&command.group().modifiers==edited.modifiers,
        "Height edit replaced unrelated local source edits");
    check(command.group().parameters.detail_seed==edited.parameters.detail_seed,"Height edit changed detail seed");
    check(cell(taller,100)==edited.modifiers.manual_cells[0]&&taller.cuts==edited.modifiers.cuts,"Height edit changed manual cells/cuts");
    const auto& adjusted=cell(taller,edited.cell_ids[2]);
    near(adjusted.center.x,9,"Height edit discarded center offset");near(adjusted.center.y,240*0.7-2,"Height edit scaled local offset");
    near(adjusted.radii.y,240*0.3*0.9,"Height edit discarded local radius scale");
    check(adjusted.structure_seed==991,"Height edit discarded per-cell seed");
    check(gizmo.undo()&&gizmo.group()==edited&&!gizmo.can_undo(),"Gizmo drag was not a single undo step");
    check(gizmo.redo()&&gizmo.group()==command.group(),"Gizmo redo lost source model");
    const auto before_reset=command.group();command.reset(CumulonimbusParameter::height);
    auto expected=before_reset;expected.parameters.height=CumulonimbusParameters{}.height;
    check(command.group()==expected,"Reset changed more than its target parameter");
    check(command.undo()&&command.group()==before_reset,"Reset could not be undone");
    check(command.redo()&&command.group()==expected,"Reset could not be redone");
    const auto before_cancel=command.group();const auto revision=command.revision();
    command.begin_edit();command.reset(CumulonimbusParameter::detail_seed);command.apply({CumulonimbusParameter::width,100.0});
    check(!command.can_undo()&&!command.undo(),"Undo allowed during an active edit");
    command.cancel_edit();check(command.group()==before_cancel&&command.revision()>revision,"Cancel lost source or rewound revision");
    // A canceled/no-op gesture must preserve redo history.
    command.undo();const auto undo_group=command.group();check(command.can_redo(),"Missing redo history");
    command.begin_edit();command.apply({CumulonimbusParameter::density,2.0});command.cancel_edit();
    check(command.group()==undo_group&&command.can_redo(),"Canceled gesture destroyed redo");
    command.begin_edit();command.end_edit();check(command.can_redo(),"Empty gesture destroyed redo");
    command.begin_edit();command.apply({CumulonimbusParameter::density,2.0});command.apply({CumulonimbusParameter::density,undo_group.parameters.density});command.end_edit();
    check(command.can_redo(),"Net-zero gesture destroyed redo");
    const auto unchanged=command.group();const auto unchanged_revision=command.revision();
    check(!command.replace(unchanged)&&command.revision()==unchanged_revision,"No-op replacement changed revision");
    for(const auto& bad_command:std::array{
        CumulonimbusCommand{CumulonimbusParameter::height,0.0},
        CumulonimbusCommand{CumulonimbusParameter::width,std::numeric_limits<double>::quiet_NaN()},
        CumulonimbusCommand{CumulonimbusParameter::growth_direction,Vec3{0,0,0}},
        CumulonimbusCommand{CumulonimbusParameter::height,UINT64_C(80)},
        CumulonimbusCommand{CumulonimbusParameter(999),2.0}}) {
        bool failed=false;try{command.apply(bad_command);}catch(const std::invalid_argument&){failed=true;}
        check(failed&&command.group()==unchanged&&command.revision()==unchanged_revision,"Invalid command changed document");
    }
    auto bad=original;bad.contract_version=2;rejected(bad,"contract version");
    bad=original;bad.parameters.width=2001;rejected(bad,"Width");
    bad=original;bad.parameters.height=9;rejected(bad,"Height");
    bad=original;bad.parameters.cloud_base=100001;rejected(bad,"Cloud base");
    bad=original;bad.parameters.density=-1;rejected(bad,"Density");
    bad=original;bad.parameters.growth_direction={0,-1,0};rejected(bad,"Growth direction");
    bad=original;bad.cell_ids[0]=bad.cell_ids[1];rejected(bad,"globally unique");
    bad=original;bad.cloud_id=bad.cell_ids[0];rejected(bad,"globally unique");
    bad=original;bad.cell_adjustments.push_back({99});rejected(bad,"unique generated Cell ID");
    bad=original;bad.cell_adjustments={{2},{2}};rejected(bad,"unique generated Cell ID");
    bad=original;bad.cell_adjustments.push_back({2,{}, {0,1,1},{}});rejected(bad,"radius adjustment");
    bad=original;bad.modifiers.manual_cells.resize(4);rejected(bad,"three additional");
    bad=original;bad.modifiers.manual_cells.push_back({2});rejected(bad,"globally unique");
    bad=original;bad.modifiers.cuts.resize(9);rejected(bad,"eight cuts");
    bad=original;bad.modifiers.noise.warp_amplitude=21;rejected(bad,"Noise strength");
    bad=original;bad.modifiers.manual_cells.push_back({99,{0,0,0},{0,1,1},0});rejected(bad,"center/radii");
    auto seeded=original;seeded.parameters.structure_seed=std::numeric_limits<std::uint64_t>::max();seeded.parameters.detail_seed=0;
    const auto seeded_recipe=derive_cumulonimbus_recipe(seeded);
    check(seeded_recipe.structure_seed==std::numeric_limits<std::uint64_t>::max()&&seeded_recipe.detail_seed==0&&seeded.cell_ids==original.cell_ids,"64-bit seeds lost precision or changed IDs");
    const auto custom=cumulonimbus_to_custom_cloud(edited);
    check(custom==derive_cumulonimbus_graph(edited)&&lower_to_recipe(custom)==derive_cumulonimbus_recipe(edited),"Explicit Custom Cloud conversion changed appearance");
    check(edited.cell_adjustments.size()==1&&edited.modifiers.cuts.size()==1,"Custom conversion mutated source");
    const auto initial_scene=new_cumulonimbus_scene();
    auto local_recipe=initial_scene.cloud;local_recipe.cells[2].center.x+=4;local_recipe.cells[2].radii.y*=0.8;
    local_recipe.cuts.push_back({99,{10,20,0},{4,5,6},1});local_recipe.detail_seed=UINT64_C(4294967313);
    const auto adapted=edit_cumulonimbus_recipe(*initial_scene.cumulonimbus,local_recipe);
    const auto adapted_recipe=derive_cumulonimbus_recipe(adapted);
    check(adapted_recipe.cells==local_recipe.cells&&adapted_recipe.cuts==local_recipe.cuts&&adapted_recipe.detail_seed==local_recipe.detail_seed,
        "Existing local recipe edits were not represented exactly in the source");
    const auto raised=command_cumulonimbus(adapted,{CumulonimbusParameter::height,200.0});
    check(raised.cell_adjustments==adapted.cell_adjustments&&raised.modifiers==adapted.modifiers&&raised.cell_ids==adapted.cell_ids,
        "Public height edit replaced preserved local source edits");
    auto optical_scene=initial_scene;auto optical_recipe=optical_scene.cloud;optical_recipe.optics.g=0.6;
    optical_scene.cumulonimbus=edit_cumulonimbus_recipe(*optical_scene.cumulonimbus,optical_recipe);
    optical_scene.cloud=derive_cumulonimbus_recipe(*optical_scene.cumulonimbus);
    check(density_input_hash(optical_scene)==density_input_hash(initial_scene)&&!has(classify_change(initial_scene,optical_scene),Dirty::density),
        "Source-backed optical edit invalidated the density snapshot");
    check(optical_scene.cumulonimbus->cell_adjustments.empty(),"Unrelated optical edit invented local cell adjustments");
    bool removed=false;local_recipe=initial_scene.cloud;local_recipe.cells.erase(local_recipe.cells.begin());
    try{(void)edit_cumulonimbus_recipe(*initial_scene.cumulonimbus,local_recipe);}catch(const std::invalid_argument&){removed=true;}
    check(removed,"Local edit path silently deleted a generated stable cell");
    const auto converted_scene=custom_cloud_scene(initial_scene);
    check(!converted_scene.cumulonimbus&&converted_scene.cloud==initial_scene.cloud,"Scene Custom conversion changed density or optics");
    // Bounded history retains the latest 128 edits.
    CumulonimbusDocument history;for(int i=0;i<140;++i)history.apply({CumulonimbusParameter::height,121.0+i});
    int undo_count=0;while(history.undo())++undo_count;check(undo_count==128,"History bound differs from editor contract");
    std::cout<<"Cumulonimbus v1: "<<samples<<" exact graph/Recipe samples; five stable cells, seven command parameters, local edits, extrema, grouped Undo/Redo/reset/cancel passed\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
