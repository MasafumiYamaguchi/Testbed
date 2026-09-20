#include "white/modifiers.hpp"
#include "white/frozen_cloud.hpp"
#include "white/generation.hpp"
#include "white/developed_scene.hpp"
#include "white/anvil_scene.hpp"
#include "white/persistence.hpp"
#include "white/revision_queue.hpp"
#include <nlohmann/json.hpp>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace white;
namespace {
void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
template<class F>void reject(F action,const char* message){bool rejected=false;try{action();}catch(const std::exception&){rejected=true;}check(rejected,message);}
FinishModifier layer(Id id,FinishModifierKind kind){FinishModifier value;value.id=id;value.kind=kind;value.target_id=71;value.mask.center={0,35,0};return value;}
void masks_and_order(){
    const std::array<Id,2> ids{101,205};FinishStack stack;
    auto cut=layer(1,FinishModifierKind::cut);cut.strength=.5;auto multiply=layer(2,FinishModifierKind::density);multiply.density_multiplier=2;
    stack.layers={cut,multiply};check(validate_finish_stack(stack,71,ids).empty(),"Valid stack rejected");
    auto result=evaluate_finish_stack(stack,{0,35,0},71,ids);check(result.density[0]==1&&result.density[1]==1,"Cut then multiplication order incorrect");
    stack=finish_stack_command(stack,{FinishCommandKind::move_up,2,{}});result=evaluate_finish_stack(stack,{0,35,0},71,ids);check(result.density[0]==1.5,"Ordered cut was incorrectly commuted with multiplication");
    stack.layers[1].strength=1;result=evaluate_finish_stack(stack,{0,35,0},71,ids);check(result.density[0]==0,"Later density operation revived complete cut");
    const auto boundary=evaluate_finish_stack(stack,{15,35,0},71,ids),near_outside=evaluate_finish_stack(stack,{15.001,35,0},71,ids);
    check(boundary.density[0]==0&&near_outside.density[0]<2e-6,"Amplification before hard cut introduced a discontinuous edge");
    stack.layers[1].hard_cut=false;result=evaluate_finish_stack(stack,{0,35,0},71,ids);check(result.density[0]==1,"Soft cut unexpectedly became a hard constraint");
    stack.layers[1].target_kind=FinishTargetKind::field;stack.layers[1].target_id=101;stack.layers[1].hard_cut=true;
    result=evaluate_finish_stack(stack,{0,35,0},71,ids);check(result.density[0]==0&&result.density[1]==2,"Field cut erased or retargeted neighboring field");
    stack.layers[1].enabled=false;check(evaluate_finish_stack(stack,{0,35,0},71,ids).density[0]==2,"Disabled layer affected density");
    stack.layers.push_back(layer(3,FinishModifierKind::protect_detail));result=evaluate_finish_stack(stack,{0,35,0},71,ids);check(result.detail[0]==0&&result.detail[1]==0&&result.density[0]==2,"Protection changed density coefficient rather than noise amplitudes");
    const auto outside=evaluate_finish_stack(stack,{100,35,0},71,ids);check(outside.density==std::array{1.,1.}&&outside.detail==std::array{1.,1.},"Local masks affected distant point");
    check(std::abs(ellipsoid_mask_weight(stack.layers[0].mask,{16,35,0})-.5)<1e-14,"Mask feather is not bounded smooth outward falloff");
    const auto packed=gpu_finish_stack(stack,71,ids);check(packed.layers[0].target.x==3&&packed.layers[1].target.x==1&&packed.settings.y==2,"GPU target mask or density bound differs");
    auto partial=stack;partial.layers[1].enabled=true;partial.layers[1].strength=1-1e-9;const auto partial_gpu=gpu_finish_stack(partial,71,ids);check(partial_gpu.layers[1].operation.z==1&&partial_gpu.layers[1].target.y==0,"Float rounding promoted a partial ordered cut into a final hard constraint");
}
void graph_and_errors(){
    const std::array<Id,1> ids{101};FinishStack stack;stack=finish_stack_command(stack,{FinishCommandKind::add,0,layer(1,FinishModifierKind::cut)});
    const auto original=stack;stack=finish_stack_command(stack,{FinishCommandKind::duplicate,1,{}});check(stack.layers.size()==2&&stack.layers[1].id==2&&stack.layers[1].mask==stack.layers[0].mask,"Duplicate lost source values or stable identity");
    stack=finish_stack_command(stack,{FinishCommandKind::erase,2,{}});check(stack==original,"Delete did not restore original stack");
    reject([&]{finish_stack_command(stack,{FinishCommandKind::erase,99,{}});},"Missing layer silently ignored");
    auto bad=stack;bad.layers.front().target_kind=FinishTargetKind::field;bad.layers.front().target_id=999;check(!validate_finish_stack(bad,71,ids).empty(),"Missing field reference accepted");reject([&]{gpu_finish_stack(bad,71,ids);},"Missing field reference packed as another field");
    bad=stack;bad.layers.front().mask.radii.x=0;check(!validate_finish_stack(bad,71,ids).empty(),"Zero mask radius accepted");
    bad=stack;bad.layers.front().mask.center.x=100000;bad.layers.front().mask.falloff=.01;check(!validate_finish_stack(bad,71,ids).empty(),"Unrepresentable GPU mask precision accepted");
    bad=stack;bad.layers.front().strength=2;check(!validate_finish_stack(bad,71,ids).empty(),"Out-of-range strength accepted");
    bad=stack;bad.layers.push_back(bad.layers.front());check(!validate_finish_stack(bad,71,ids).empty(),"Repeated layer ID accepted");
    check(finish_stack_hash(stack)!=finish_stack_hash(bad),"Graph hash omitted layer order/identity");
}
void protected_field(){
    auto r=density_fixture(0);r.noise.medium_strength=.8;r.noise.micro_erosion=2;r.noise.warp_amplitude=2;const DensityField detailed(r);
    auto clean=r;clean.noise.medium_strength=clean.noise.micro_erosion=clean.noise.warp_amplitude=0;const DensityField bare(clean);
    for(int x=-20;x<20;++x)for(int y=0;y<80;y+=4){const Vec3 p{double(x),double(y),0};check(detailed.at(p,0)==bare.at(p),"Protection did not suppress each noise amplitude before one field evaluation");check(detailed.at(p,1,2)==2*detailed.at(p),"Density multiplier violated common field evaluator");}
}
float float_mask(const EllipsoidMask& mask,Vec3 p){
    const float x=(float(p.x)-float(mask.center.x))/float(mask.radii.x),y=(float(p.y)-float(mask.center.y))/float(mask.radii.y),z=(float(p.z)-float(mask.center.z))/float(mask.radii.z);
    const float distance=(std::sqrt(x*x+y*y+z*z)-1)*std::min({float(mask.radii.x),float(mask.radii.y),float(mask.radii.z)});
    const float t=std::clamp(distance/float(mask.falloff),0.f,1.f);return 1-t*t*(3-2*t);
}
void precision(){
    double worst=0;
    for(double origin:{0.,100.,1000.}){EllipsoidMask mask{{origin+.123,origin-.347,-origin+.879},{3.1,19.7,8.3},4.2};const auto allowance=ellipsoid_mask_error_bound(mask);
        for(auto direction:std::array{Vec3{1,0,0},Vec3{0,1,0},Vec3{0,0,1},Vec3{.6,.48,.64}})for(int i=0;i<=600;++i){const double distance=-.02+(mask.falloff+.04)*i/600.;const double ratio=1+distance/3.1;
            const Vec3 p=mask.center+Vec3{direction.x*mask.radii.x,direction.y*mask.radii.y,direction.z*mask.radii.z}*ratio;
            const auto error=std::abs(double(float_mask(mask,p))-ellipsoid_mask_weight(mask,p));worst=std::max(worst,error);check(error<=allowance,"Float mask exceeded derived coordinate/feather error bound");
        }
    }
    std::cout<<"modifier_mask_precision float_max_abs="<<worst<<" boundary_feather_and_anisotropy=true PASS\n";
}
Scene freeze_static(Scene source){GenerationSettings settings;settings.enabled=false;const auto outcome=generate_cloud_state(source,settings);check(outcome.status==GenerationStatus::completed,outcome.message.c_str());return freeze_candidate(outcome);}
Scene fixed_fixture(){auto source=new_anvil_scene(new_cumulonimbus_scene());source.anvil->cloud.settings.mode=TopLobeMode::children;source.anvil->settings.enabled=true;refresh_anvil_scene(source);auto settings=default_generation_settings(source);settings.stage=.8;settings.wind.back().displacement={40,0,12};const auto outcome=generate_cloud_state(source,settings);check(outcome.status==GenerationStatus::completed,outcome.message.c_str());auto fixed=freeze_candidate(outcome);fixed.camera.target={35,65,0};fixed.camera.position={35,80,350};return fixed;}
FinishModifier scene_layer(const Scene& scene,Id id,FinishModifierKind kind){auto value=layer(id,kind);value.target_id=scene.frozen->id;return value;}
void precision_rejection_keeps_history(){
    // Match the native lifecycle's detailed two-field/anvil state. Each
    // individual layer is supported, but a second protection layer is not.
    auto fixed=fixed_fixture();auto cut=scene_layer(fixed,1,FinishModifierKind::cut);cut.mask.center={20,55,0};cut.mask.radii={17,25,24};fixed=scene_with_finish_command(fixed,{FinishCommandKind::add,0,cut});
    auto multiply=cut;multiply.id=2;multiply.kind=FinishModifierKind::density;multiply.density_multiplier=3;fixed=scene_with_finish_command(fixed,{FinishCommandKind::add,0,multiply});
    auto protection=cut;protection.id=3;protection.kind=FinishModifierKind::protect_detail;protection.mask.center={15,70,0};protection.mask.radii={25,40,30};fixed=scene_with_finish_command(fixed,{FinishCommandKind::add,0,protection});
    const auto field=fixed.frozen->fields.front();auto noise=field.recipe.noise;noise.medium_strength=.8;noise.micro_erosion=2;fixed=scene_with_frozen_detail(fixed,field.development_id,noise,field.recipe.detail_seed+123,field.layers);
    const auto jobs=generation_job_count();const auto original_frozen=fixed.frozen;EditorSession session(fixed);
    const auto reordered=scene_with_finish_command(fixed,{FinishCommandKind::move_up,2,{}});session.apply(reordered);check(session.undo(),"Precision fixture reorder was not undoable");
    const auto revision=session.document().revision();bool rejected=false;
    try{session.apply(scene_with_finish_command(fixed,{FinishCommandKind::duplicate,3,{}}));}catch(const std::invalid_argument& error){rejected=std::string(error.what()).find("0.5% GPU density precision budget")!=std::string::npos;}
    check(rejected&&session.document().scene()==fixed&&session.document().revision()==revision&&!session.can_undo()&&session.can_redo(),"Over-budget protection duplicate mutated the document or history");
    check(session.redo()&&session.document().scene()==reordered&&session.undo()&&session.document().scene()==fixed,"Precision rejection lost the original Undo/Redo transaction");
    const auto duplicated=scene_with_finish_command(fixed,{FinishCommandKind::duplicate,1,{}});check(duplicated.finish_stack.layers.size()==4&&duplicated.finish_stack.layers[1].id==4,"Legal cut duplicate lost stable identity");
    session.apply(duplicated);check(session.undo()&&session.document().scene()==fixed&&session.redo()&&session.document().scene()==duplicated,"Legal duplicate did not form one Undo command");
    session.apply(scene_with_finish_command(session.document().scene(),{FinishCommandKind::erase,4,{}}));check(session.document().scene()==fixed&&session.document().scene().frozen==original_frozen&&generation_job_count()==jobs,"Duplicate/delete changed frozen content or reran generation");
    std::cout<<"modifier_precision_rejection current_revision_history_retained=true legal_duplicate_delete=true generation_jobs=0 PASS\n";
}
void scene_lifecycle(){
    auto plain=fixture_scene(0);plain.cloud.noise.medium_strength=.7;plain.cloud.noise.micro_erosion=2;plain.cloud.noise.warp_amplitude=2;const auto original=freeze_static(plain);
    auto cut=scene_layer(original,1,FinishModifierKind::cut);cut.mask.center={0,35,0};auto fixed=scene_with_finish_command(original,{FinishCommandKind::add,0,cut});
    const auto jobs=generation_job_count();check(fixed.frozen==original.frozen,"Finishing mutated immutable frozen payload");check(scene_density_requires_direct(fixed)&&has(classify_change(original,fixed),Dirty::density)&&density_input_hash(original)!=density_input_hash(fixed),"Stack edit omitted common density invalidation");
    auto multiply=scene_layer(fixed,2,FinishModifierKind::density);multiply.density_multiplier=4;multiply.mask=cut.mask;fixed=scene_with_finish_command(fixed,{FinishCommandKind::add,0,multiply});
    const auto& field=fixed.frozen->fields.front();auto noise=field.recipe.noise;noise.micro_erosion=3;fixed=scene_with_frozen_detail(fixed,field.development_id,noise,927,field.layers);
    check(SceneDensityEvaluator(fixed).at(cut.mask.center)==0,"Hard cut revived after density amplification or new detail");
    check(fixed.frozen->content_hash==original.frozen->content_hash&&fixed.finish_stack.layers.front()==cut,"Detail changed shape identity or destroyed modifier");
    const auto before=fixed;fixed=scene_with_finish_command(fixed,{FinishCommandKind::move_up,2,{}});EditorSession session(before);session.apply(fixed);check(session.undo()&&session.document().scene()==before&&session.redo()&&session.document().scene()==fixed,"Reorder Undo did not restore exact graph");
    auto moved=fixed;moved.frozen->transform.translation={35,-7,15};moved.frozen->transform.scale={1.5,.7,2};refresh_frozen_scene(moved);const auto local=Vec3{12,45,3};const auto world=local_to_world(moved.cloud.transform,local);check(std::abs(SceneDensityEvaluator(fixed).at(local)-SceneDensityEvaluator(moved).at(world_to_local(moved.cloud.transform,world)))<1e-10&&moved.finish_stack==fixed.finish_stack,"Object transform detached local mask");
    auto bad=fixed;bad.finish_stack.layers[0].target_kind=FinishTargetKind::field;bad.finish_stack.layers[0].target_id=123456;reject([&]{session.apply(bad);},"Missing target replaced successful document");check(session.document().scene()==fixed,"Missing target changed document/history");
    check(generation_job_count()==jobs,"Finishing or transform edits restarted growth");
    const auto encoded=scene_json(fixed);check(parse_scene_json(encoded)==fixed,"Finishing stack roundtrip changed IDs/order/masks");
    auto json=nlohmann::json::parse(encoded);json["cloud"]["finishing"]["space"]="world";reject([&]{parse_scene_json(json.dump());},"Unsupported world-fixed masks accepted");
    json=nlohmann::json::parse(encoded);json["cloud"]["finishing"]["layers"][0]["target"]["id"]="123456";reject([&]{parse_scene_json(json.dump());},"Broken saved target accepted");
    json=nlohmann::json::parse(scene_json(original));json["schema_version"]=10;json["cloud"].erase("finishing");check(parse_scene_json(json.dump())==original,"Schema10 frozen migration changed field or fabricated modifiers");
    auto identity=fixed;identity.finish_stack.layers[0].id=std::numeric_limits<Id>::max();check(parse_scene_json(scene_json(identity))==identity,"Modifier uint64 identity lost precision");
}
void protection_and_targets(){
    auto source=new_developed_scene(new_cumulonimbus_scene());auto second=make_developed_cell(*source.developed);second.translation={40,0,0};source=scene_with_developed_command(source,DevelopedAdd{second});const auto fixed=freeze_static(source);
    const auto jobs=generation_job_count();auto protection=scene_layer(fixed,1,FinishModifierKind::protect_detail);protection.mask.center={0,60,0};protection.mask.radii={18,45,18};
    auto protected_scene=scene_with_finish_command(fixed,{FinishCommandKind::add,0,protection});auto reseeded=protected_scene;
    for(const auto& f:protected_scene.frozen->fields)reseeded=scene_with_frozen_detail(reseeded,f.development_id,f.recipe.noise,f.recipe.detail_seed+554,f.layers);
    const SceneDensityEvaluator a(protected_scene),b(reseeded);std::size_t outside_changed=0;
    for(int y=10;y<=105;y+=5)for(int x=-40;x<=85;x+=5){const Vec3 p{double(x),double(y),0};const auto weight=ellipsoid_mask_weight(protection.mask,p);if(weight==1)check(a.at(p)==b.at(p),"Fully protected mask changed after seed edit");if(weight==0&&std::abs(a.at(p)-b.at(p))>1e-8)++outside_changed;}
    check(outside_changed>10,"Protection accidentally snapshotted or froze the whole density field");
    auto cut=scene_layer(fixed,2,FinishModifierKind::cut);cut.mask.center={20,55,0};cut.mask.radii={60,60,60};cut.target_kind=FinishTargetKind::field;cut.target_id=fixed.frozen->fields.front().development_id;
    const auto isolated=scene_with_finish_command(fixed,{FinishCommandKind::add,0,cut});const Vec3 p{20,55,0};const auto& neighbor=fixed.frozen->fields[1];const auto expected=DensityField(frozen_effective_recipe(neighbor)).at(p-neighbor.translation);check(std::abs(SceneDensityEvaluator(isolated).at(p)-expected)<1e-14&&expected>0,"Field-specific cut erased or reassigned neighboring field");
    check(generation_job_count()==jobs,"Protected-detail or target edits reran generation");
    auto adopted=adopt_frozen_with_finish(protected_scene,fixed);check(adopted.finish_stack==protected_scene.finish_stack,"Candidate adoption silently dropped valid layers");
    auto targeted=protected_scene;targeted.finish_stack.layers[0].target_kind=FinishTargetKind::field;targeted.finish_stack.layers[0].target_id=fixed.frozen->fields[1].development_id;require_valid(targeted);
    auto one=fixed;one.frozen->fields.pop_back();one.frozen->curves.pop_back();refresh_frozen_scene(one);reject([&]{adopt_frozen_with_finish(targeted,one);},"Candidate silently rebound missing field target");
    auto hierarchy=fixed_fixture();hierarchy.frozen->fields[1].development_id=targeted.finish_stack.layers[0].target_id;refresh_frozen_scene(hierarchy);reject([&]{adopt_frozen_with_finish(targeted,hierarchy);},"Numeric field-ID collision silently changed a development target into a top field");
    const auto maximum=SceneDensityEvaluator(protected_scene).maximum();for(int y=-1;y<150;y+=7)for(int x=-60;x<110;x+=5){const double density=a.at({double(x),double(y),0});check(density>=0&&density<=maximum,"Finishing violated conservative maximum");}
    auto enormous=fixture_scene(0);enormous.cloud.envelope={{-100000,-100000,-100000},{100000,100000,100000}};const auto large_frame=freeze_static(enormous);auto precise_cut=scene_layer(large_frame,1,FinishModifierKind::cut);
    reject([&]{scene_with_finish_command(large_frame,{FinishCommandKind::add,0,precise_cut});},"Small mask ignored GPU sample-position cancellation in an oversized envelope");
}
void fixtures(const std::filesystem::path& directory){
    std::filesystem::create_directories(directory);auto base=fixed_fixture();
    auto cut=scene_layer(base,1,FinishModifierKind::cut);cut.mask.center={20,55,0};cut.mask.radii={17,25,24};auto carved=scene_with_finish_command(base,{FinishCommandKind::add,0,cut});
    auto amplify=scene_layer(base,2,FinishModifierKind::density);amplify.mask.center={50,80,0};amplify.mask.radii={35,30,35};amplify.density_multiplier=2.5;auto dense=scene_with_finish_command(carved,{FinishCommandKind::add,0,amplify});
    auto protection=scene_layer(base,3,FinishModifierKind::protect_detail);protection.mask.center={20,70,0};protection.mask.radii={25,35,30};auto protected_scene=scene_with_finish_command(dense,{FinishCommandKind::add,0,protection});auto reseeded=protected_scene;
    for(const auto& f:protected_scene.frozen->fields)reseeded=scene_with_frozen_detail(reseeded,f.development_id,f.recipe.noise,f.recipe.detail_seed+1234,f.layers);
    auto solo=protected_scene;for(auto& layer:solo.finish_stack.layers)layer.enabled=layer.kind==FinishModifierKind::protect_detail;require_valid(solo);
    nlohmann::json manifest=nlohmann::json::array();for(const auto& [name,scene]:std::array<std::pair<const char*,Scene>,6>{{{"base",base},{"cut",carved},{"density",dense},{"protected",protected_scene},{"protected-seed",reseeded},{"solo",solo}}}){const auto file=std::string(name)+".white.json";save_scene_atomic(scene,directory/file);manifest.push_back({{"name",name},{"recipe",file},{"content_hash",std::to_string(scene.frozen->content_hash)},{"density_hash",std::to_string(density_input_hash(scene))}});}
    std::ofstream(directory/"manifest.json")<<manifest.dump(2)<<'\n';
}
}
int main(int argc,char** argv){try{masks_and_order();graph_and_errors();protected_field();precision();precision_rejection_keeps_history();scene_lifecycle();protection_and_targets();if(argc==2)fixtures(argv[1]);std::cout<<"Modifier masks, ordered commands, immutable frozen identity, zero generation jobs, target diagnostics, schema11, hard cuts and protected detail PASS\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
