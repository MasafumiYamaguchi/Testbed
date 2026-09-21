#include "white/cumulonimbus_cells.hpp"
#include "white/centerline.hpp"
#include "white/persistence.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace white;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
bool near(Vec3 a,Vec3 b){return std::abs(a.x-b.x)<1e-10&&std::abs(a.y-b.y)<1e-10&&std::abs(a.z-b.z)<1e-10;}
const Cell& cell(const CloudRecipe& recipe,Id id) {
    const auto found=std::find_if(recipe.cells.begin(),recipe.cells.end(),[&](const Cell& c){return c.id==id;});
    if(found==recipe.cells.end())throw std::runtime_error("Test cell missing");
    return *found;
}
void unchanged_others(const CloudRecipe& before,const CloudRecipe& after,Id changed) {
    for(const auto& c:before.cells)if(c.id!=changed) {
        check(c==cell(after,c.id),"Unrelated cell changed");
        check(cell_random_key(before,c)==cell_random_key(after,cell(after,c.id)),"Unrelated cell random sequence changed");
    }
}
void reject(const CumulonimbusGroup& source,const CumulonimbusCellCommand& command,const char* text) {
    const auto original=source;bool rejected=false;
    try{(void)command_cumulonimbus_cell(source,command);}
    catch(const std::exception& error){rejected=std::string(error.what()).find(text)!=std::string::npos;}
    check(rejected,"Missing expected command rejection");check(source==original,"Rejected command mutated source");
}
CumulonimbusGroup fixture() {
    CumulonimbusGroup group;group.cloud_id=40;group.cell_ids={45,44,43,42,41};
    group.cell_adjustments={{42,{2,3,4},{1.1,.9,1.2},UINT64_C(4294967313)}};
    auto& m=group.modifiers;m.manual_cells={{70,{160,50,12},{10,15,9},UINT64_C(991)}};
    m.cuts={{90,{2,35,5},{4,7,8},1}};m.noise.origin={9,-3,8};
    m.transform.translation={3,6,9};m.transform.scale={1.1,1.2,.9};m.optics.g=.5;
    check(validate_cumulonimbus(group).empty(),"Invalid fixture");return group;
}
void local_edits() {
    const auto source=fixture();const auto initial=derive_cumulonimbus_recipe(source);const auto original=cell(initial,42);
    auto moved=command_cumulonimbus_cell(source,CumulonimbusMoveCell{42,original.center+Vec3{8,9,-7}});
    auto recipe=derive_cumulonimbus_recipe(moved);
    check(near(cell(recipe,42).center,original.center+Vec3{8,9,-7}),"Selected generated cell failed to move");
    check(moved.modifiers==source.modifiers&&moved.parameters==source.parameters&&moved.cell_ids==source.cell_ids,"Move changed source modifiers or role order");
    unchanged_others(initial,recipe,42);
    auto scaled=command_cumulonimbus_cell(moved,CumulonimbusScaleCell{42,{original.radii.x,original.radii.y*1.8,original.radii.z}});
    auto scaled_recipe=derive_cumulonimbus_recipe(scaled);
    check(near(cell(scaled_recipe,42).radii,{original.radii.x,original.radii.y*1.8,original.radii.z}),"Selected generated cell failed to stretch");
    check(cell(scaled_recipe,42).center==cell(recipe,42).center&&cell(scaled_recipe,42).structure_seed==original.structure_seed,"Scale lost center or seed override");
    unchanged_others(initial,scaled_recipe,42);
    auto seeded=command_cumulonimbus_cell(scaled,CumulonimbusReseedCell{42,UINT64_MAX});
    auto seeded_recipe=derive_cumulonimbus_recipe(seeded);unchanged_others(initial,seeded_recipe,42);
    check(cell(seeded_recipe,42).structure_seed==UINT64_MAX,"Reseed narrowed uint64 seed");
    check(cell_random_key(scaled_recipe,cell(scaled_recipe,42))!=cell_random_key(seeded_recipe,cell(seeded_recipe,42)),"Reseed left selected key unchanged");
    check(seeded.parameters==source.parameters&&seeded.modifiers==source.modifiers,"Reseed changed global seeds or modifiers");
    check(command_cumulonimbus_cell(seeded,CumulonimbusMoveCell{42,cell(seeded_recipe,42).center})==seeded,"No-op move added an adjustment");
    auto manual_move=command_cumulonimbus_cell(source,CumulonimbusMoveCell{70,{190,65,18}});
    auto manual_scale=command_cumulonimbus_cell(manual_move,CumulonimbusScaleCell{70,{6,12,8}});
    auto manual_seed=command_cumulonimbus_cell(manual_scale,CumulonimbusReseedCell{70,65537});
    unchanged_others(initial,derive_cumulonimbus_recipe(manual_seed),70);
    check(cell(derive_cumulonimbus_recipe(manual_seed),70)==Cell{70,{190,65,18},{6,12,8},65537},"Manual edit failed");
    check(manual_seed.cell_adjustments==source.cell_adjustments,"Manual edit changed generated adjustments");
}
void duplication_limits_selection() {
    const auto source=fixture();const auto initial=derive_cumulonimbus_recipe(source);
    check(next_cumulonimbus_cell_id(source)==91,"ID allocator ignored a cut");
    const auto generated=cumulonimbus_cell_capabilities(source,42),manual=cumulonimbus_cell_capabilities(source,70);
    check(generated.kind==CumulonimbusCellKind::generated&&!generated.can_remove&&generated.remaining_manual_slots==2,"Generated capabilities incorrect");
    check(manual.kind==CumulonimbusCellKind::manual&&manual.can_remove,"Manual capabilities incorrect");
    auto retained=command_cumulonimbus_cell(source,CumulonimbusDuplicateCell{42,91,CumulonimbusDuplicateSeed::retain});
    const auto retained_recipe=derive_cumulonimbus_recipe(retained);auto expected=cell(initial,42);expected.id=91;
    check(cell(retained_recipe,91)==expected,"Retain duplicate lost geometry or local seed");
    check(cell_random_key(initial,cell(initial,42))!=cell_random_key(retained_recipe,cell(retained_recipe,91)),"New stable ID failed to separate duplicate stream");
    unchanged_others(initial,retained_recipe,0);
    auto regenerated=command_cumulonimbus_cell(retained,CumulonimbusDuplicateCell{70,92,CumulonimbusDuplicateSeed::regenerate,UINT64_MAX});
    check(cell(derive_cumulonimbus_recipe(regenerated),92).structure_seed==UINT64_MAX,"Explicit duplicate seed lost");
    check(cumulonimbus_cell_capabilities(regenerated,92).remaining_manual_slots==0,"Eight-cell limit count incorrect");
    reject(regenerated,CumulonimbusAddCell{{93,{0,40,0},{3,3,3},9}},"at most eight");
    reject(source,CumulonimbusRemoveCell{42},"Custom Cloud");
    auto removed=command_cumulonimbus_cell(regenerated,CumulonimbusRemoveCell{91});
    check(!cumulonimbus_cell_selection(removed,91)&&cumulonimbus_cell_selection(removed,42)==42,"Selection became dangling or changed target");
    check(!cumulonimbus_cell_selection(removed,{})&&!cumulonimbus_cell_selection(removed,999),"Missing selection did not clear");
    unchanged_others(derive_cumulonimbus_recipe(regenerated),derive_cumulonimbus_recipe(removed),91);
    auto added=command_cumulonimbus_cell(removed,CumulonimbusAddCell{{93,{250,45,20},{8,12,9},19}});
    const auto added_recipe=derive_cumulonimbus_recipe(added);
    check(added_recipe.envelope.max.x>258&&added_recipe.cells.size()==8,"Add did not update conservative support/count");
    unchanged_others(derive_cumulonimbus_recipe(removed),added_recipe,0);
    for(Id id:{Id(0),Id(40),Id(42),Id(90)})reject(source,CumulonimbusDuplicateCell{42,id,CumulonimbusDuplicateSeed::retain},"unused");
    reject(source,CumulonimbusDuplicateCell{42,91,CumulonimbusDuplicateSeed::regenerate,cell(initial,42).structure_seed},"different explicit");
    reject(source,CumulonimbusDuplicateCell{42,91,static_cast<CumulonimbusDuplicateSeed>(99)},"Unknown duplicate");
    reject(source,CumulonimbusReseedCell{999,1},"Unknown");
    reject(source,CumulonimbusMoveCell{42,{std::numeric_limits<double>::infinity(),0,0}},"finite");
    reject(source,CumulonimbusScaleCell{42,{-1,1,1}},"radius adjustment");
    reject(source,CumulonimbusScaleCell{42,{1e6,1e6,1e6}},"radius adjustment");
    auto exhausted=source;exhausted.modifiers.manual_cells[0].id=UINT64_MAX;
    bool threw=false;try{(void)next_cumulonimbus_cell_id(exhausted);}catch(const std::overflow_error&){threw=true;}check(threw,"Exhausted IDs wrapped");
}
void ordering_history_density() {
    auto a=fixture();a.cell_adjustments.push_back({45,{1,2,3},{1,1,1},4});
    a.modifiers.manual_cells.push_back({69,{-100,50,0},{12,15,10},14});
    auto b=a;std::reverse(b.cell_adjustments.begin(),b.cell_adjustments.end());std::reverse(b.modifiers.manual_cells.begin(),b.modifiers.manual_cells.end());
    const auto edited_a=command_cumulonimbus_cell(a,CumulonimbusReseedCell{70,99});
    const auto edited_b=command_cumulonimbus_cell(b,CumulonimbusReseedCell{70,99});
    check(edited_a==edited_b,"Equivalent source ordering changed command result");
    check(edited_a.cell_ids==a.cell_ids,"Canonical ordering changed generated role identity");
    const DensityField field_a(derive_cumulonimbus_recipe(a)),field_b(derive_cumulonimbus_recipe(b));
    for(Vec3 p:std::vector<Vec3>{{0,30,0},{20,65,8},{-100,50,0},{160,50,12}})check(field_a.at(p)==field_b.at(p),"Cell order changed density evaluation");
    CumulonimbusDocument history(fixture());const auto original=history.group();
    history.begin_edit();
    for(double x:{25.,35.,45.})history.replace(command_cumulonimbus_cell(history.group(),CumulonimbusMoveCell{42,{x,80,10}}));
    history.end_edit();const auto moved=history.group();
    check(history.undo()&&history.group()==original&&!history.can_undo(),"Move drag did not undo atomically");
    check(history.redo()&&history.group()==moved,"Move redo lost source references");
    history.replace(command_cumulonimbus_cell(history.group(),CumulonimbusDuplicateCell{42,91,CumulonimbusDuplicateSeed::retain}));
    const auto duplicated=history.group();history.replace(command_cumulonimbus_cell(history.group(),CumulonimbusRemoveCell{91}));
    check(history.undo()&&history.group()==duplicated&&cumulonimbus_cell_selection(history.group(),91)==91,"Delete Undo did not restore ID/selection");
    CumulonimbusGroup overlap;overlap.modifiers.noise.medium_strength=0;overlap.modifiers.noise.micro_erosion=0;overlap.modifiers.noise.warp_amplitude=0;
    for(Id id:overlap.cell_ids){overlap=command_cumulonimbus_cell(overlap,CumulonimbusMoveCell{id,{0,50,0}});overlap=command_cumulonimbus_cell(overlap,CumulonimbusScaleCell{id,{20,20,20}});}
    for(Id id:{Id(7),Id(8),Id(9)})overlap=command_cumulonimbus_cell(overlap,CumulonimbusDuplicateCell{2,id,CumulonimbusDuplicateSeed::retain});
    for(double blend:{0.,2.,40.})for(double density_overlap:{0.,.25,1.}) {
        overlap.modifiers.blend_width=blend;overlap.modifiers.overlap=density_overlap;
        const DensityField field(derive_cumulonimbus_recipe(overlap));const double expected=overlap.parameters.density*(1+7*density_overlap);
        check(std::abs(field.at({0,50,0})-expected)<1e-12,"Fusion width changed overlapping density independently of overlap");
        check(field.maximum()==expected,"Maximum density did not bound eight overlapping cells");
        for(double x:{0.,15.,19.,21.,35.})check(field.at({x,50,0})<=expected,"Fusion exceeded explicit density bound");
    }
}
void scene_actions() {
    Scene scene;CenterlineShape shape;shape.source=fixture();
    shape.points[1].offset={12,0,-8};shape.profile={{1,0,1,.4},{2,.5,1.3,1},{3,1,.8,.7}};
    scene.centerline=shape;scene.cloud=lower_centerline_to_recipe(shape);require_valid(scene);
    EditorSession session(scene);const auto initial=scene.cloud;
    const auto old=cell(initial,42);const Vec3 requested_center=old.center+Vec3{9,7,2};
    auto moved=scene_with_cumulonimbus_cell_command(scene,CumulonimbusMoveCell{42,requested_center});
    check(near(cell(moved.cloud,42).center,requested_center),"Scene move targeted undeformed cell geometry");
    unchanged_others(initial,moved.cloud,42);
    const Vec3 requested_radii{old.radii.x*1.2,old.radii.y*.9,old.radii.z*1.1};
    auto scaled=scene_with_cumulonimbus_cell_command(moved,CumulonimbusScaleCell{42,requested_radii});
    check(near(cell(scaled.cloud,42).radii,requested_radii),"Scene scale targeted undeformed cell geometry");
    check(scaled.centerline->points==shape.points&&scaled.centerline->profile==shape.profile,"Local geometry lost saved centerline/profile");
    const auto duplicate=scene_with_cumulonimbus_cell_command(scene,CumulonimbusDuplicateCell{42,91,CumulonimbusDuplicateSeed::retain});
    auto expected=old;expected.id=91;check(cell(duplicate.cloud,91)==expected,"Scene duplicate did not capture displayed centerline geometry");
    check(duplicate.centerline->points==shape.points&&duplicate.centerline->profile==shape.profile&&duplicate.cloud.altitude_density==scene.cloud.altitude_density,"Duplication changed the global curve/density profile");
    session.apply(duplicate);check(session.undo()&&session.document().scene()==scene&&!session.can_undo(),"Duplicate was not one session Undo");
    check(session.redo()&&session.document().scene()==duplicate,"Duplicate session Redo lost source");
    const auto seeded=scene_with_cumulonimbus_cell_command(duplicate,CumulonimbusReseedCell{91,UINT64_MAX});
    unchanged_others(duplicate.cloud,seeded.cloud,91);session.apply(seeded);
    check(session.undo()&&session.document().scene()==duplicate,"Reseed was not one session Undo");
    check(parse_scene_json(scene_json(seeded))==seeded,"Cell action failed source-only scene persistence");
    reject(shape.source,CumulonimbusRemoveCell{42},"Custom Cloud");
}
}
int main(){try{local_edits();duplication_limits_selection();ordering_history_density();scene_actions();
    std::cout<<"Cumulonimbus v1 cell adapter: independent edits, explicit duplicate seeds, ordering, limits, support and Undo passed; independent developed curves remain unsupported\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
