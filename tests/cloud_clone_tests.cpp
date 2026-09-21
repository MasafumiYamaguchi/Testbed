#include "white/cloud_clone.hpp"
#include "white/cloud_presets.hpp"
#include "white/anvil_scene.hpp"
#include "white/centerline_scene.hpp"
#include "white/persistence.hpp"
#include "white/revision_queue.hpp"
#include <cstring>
#include <iostream>
#include <set>
#include <stdexcept>
#include <type_traits>

using namespace white;
static_assert(requires(Scene scene){scene.finish_stack.layers;},"Clone finishing regression requires the schema11 Scene contract");
namespace {
void check(bool good,const char* text){if(!good)throw std::runtime_error(text);}
template<class F>void reject(F action,const char* message){try{action();}catch(const std::exception&){return;}throw std::runtime_error(message);}
Scene fixed(CloudPresetKind kind,std::uint64_t seed){CloudPresetRequest request;request.kind=kind;request.structure_seed=seed;request.detail_seed=seed;request.stage=.8;request.wind=PresetWind::upper_shear;
    auto draft=make_cloud_preset(request);const auto outcome=generate_cloud_state(draft.initial,draft.settings);check(outcome.status==GenerationStatus::completed,outcome.message.c_str());return freeze_candidate(outcome);
}
template<class S>bool has_finishing(const S& scene){if constexpr(requires {scene.finish_stack.layers;})return !scene.finish_stack.layers.empty();else return false;}
void identical_density(const Scene& original,const Scene& clone){
    const SceneDensityEvaluator a(original),b(clone);check(a.local_support()==b.local_support()&&a.world_support()==b.world_support()&&a.maximum()==b.maximum(),"Clone changed density support or bound");
    const auto ga=a.gpu_params(),gb=b.gpu_params();check(std::memcmp(&ga,&gb,sizeof(ga))==0,"Clone changed the complete GPU density packet");
    const auto bounds=a.local_support();std::size_t positive=0;
    for(int z=0;z<13;++z)for(int y=0;y<19;++y)for(int x=0;x<17;++x){const Vec3 p{bounds.min.x+(bounds.max.x-bounds.min.x)*x/16,bounds.min.y+(bounds.max.y-bounds.min.y)*y/18,bounds.min.z+(bounds.max.z-bounds.min.z)*z/12};
        const auto first=a.at(p),second=b.at(p);check(first==second,"Clone changed evaluated density");positive+=first>0;}
    check(positive>0||a.maximum()==0,"Clone comparison never sampled positive density");
    if(!has_finishing(original))check(density_input_hash(original)==density_input_hash(clone),"Equivalent clone changed effective density identity");
}
template<class S>void finish_copy(S source){
    if constexpr(requires {source.finish_stack.layers;}){
        using Layer=typename std::decay_t<decltype(source.finish_stack.layers)>::value_type;
        Layer whole;whole.id=source.frozen->id;whole.kind=decltype(whole.kind)::density;whole.target_kind=decltype(whole.target_kind)::object;
        whole.target_id=source.frozen->id;whole.density_multiplier=.8;whole.mask.center={0,45,0};whole.mask.radii={30,30,30};whole.mask.falloff=8;
        auto local=whole;local.id=source.frozen->fields[0].development_id;local.kind=decltype(local.kind)::protect_detail;local.target_kind=decltype(local.target_kind)::field;
        local.target_id=source.frozen->fields[0].development_id;local.strength=.5;
        auto disabled=local;disabled.id=9000;disabled.enabled=false;disabled.kind=decltype(disabled.kind)::cut;disabled.target_id=source.frozen->fields.back().development_id;
        source.finish_stack.layers={whole,local,disabled};require_valid(source);const auto before=source;const auto jobs=generation_job_count();
        const auto clone=clone_frozen_cloud(source);check(clone.finish_ids.size()==source.finish_stack.layers.size()&&clone.scene.finish_stack.layers.size()==source.finish_stack.layers.size(),"Clone omitted finishing layers or their identity namespace");
        std::set<Id> old_ids;for(const auto& pair:clone.ids)old_ids.insert(pair.from);for(const auto& layer:source.finish_stack.layers)old_ids.insert(layer.id);
        for(std::size_t i=0;i<source.finish_stack.layers.size();++i){auto expected=source.finish_stack.layers[i];expected.id=remap_cloned_id(clone.finish_ids,expected.id);expected.target_id=remap_cloned_id(clone.ids,expected.target_id);
            check(expected==clone.scene.finish_stack.layers[i],"Clone changed finish mask/order/value or conflated layer and target ID namespaces");
            check(!old_ids.contains(expected.id)&&expected.id>9000,"Clone reused a finishing identity or ignored a higher layer-ID namespace");}
        auto missing_remap=clone.scene;missing_remap.finish_stack.layers.front().target_id=source.frozen->id;
        reject([&]{require_valid(missing_remap);},"Clone retained the old object target without a valid new reference");
        identical_density(source,clone.scene);check(parse_scene_json(scene_json(clone.scene))==clone.scene,"Finish clone did not survive persistence");
        auto changed=clone.scene;changed.finish_stack.layers[0].mask.center.x+=1;require_valid(changed);
        check(source==before&&generation_job_count()==jobs,"Finish clone edit affected original or started growth");
        std::cout<<"clone_finish_namespace exact_density=true target_remap=true layer_remap=true masks_preserved=true PASS\n";
    }
}
void identities(const Scene& original,const FrozenCloneResult& cloned){
    const auto& a=*original.frozen;const auto& b=*cloned.scene.frozen;const auto mapped=[&](Id id){return remap_cloned_id(cloned.ids,id);};
    std::set<Id> old,new_ids;Id previous_old=0,previous_new=0;
    for(const auto pair:cloned.ids){check(pair.from>previous_old&&pair.to>previous_new,"Clone ID allocation is not strictly monotone");old.insert(pair.from);new_ids.insert(pair.to);previous_old=pair.from;previous_new=pair.to;}
    for(Id id:new_ids)check(!old.contains(id),"Clone shares a source identity");
    check(b.id==mapped(a.id)&&b.fields.size()==a.fields.size()&&b.curves.size()==a.curves.size(),"Clone lost object/field identities");
    for(std::size_t i=0;i<a.fields.size();++i){const auto& before=a.fields[i];const auto& after=b.fields[i];
        check(after.development_id==mapped(before.development_id)&&after.recipe.id==mapped(before.recipe.id),"Field/recipe identity not remapped");
        check(after.translation==before.translation&&after.layers==before.layers&&after.recipe.detail_seed==before.recipe.detail_seed&&after.recipe.noise==before.recipe.noise,"Clone changed finishing values or reference origin");
        for(std::size_t c=0;c<before.recipe.cells.size();++c){const auto& x=before.recipe.cells[c];const auto& y=after.recipe.cells[c];
            check(y.id==mapped(x.id)&&x.center==y.center&&x.radii==y.radii&&cell_random_key(before.recipe,x)==cell_random_key(after.recipe,y),"Primitive identity/geometry/warp key mismatch");}
        for(std::size_t c=0;c<before.recipe.cuts.size();++c){auto expected=before.recipe.cuts[c];expected.id=mapped(expected.id);check(expected==after.recipe.cuts[c],"Cut identity or parameters changed");}
    }
    for(std::size_t i=0;i<a.curves.size();++i){auto expected=a.curves[i];expected.development_id=mapped(expected.development_id);for(auto& p:expected.points)p.id=mapped(p.id);for(auto& p:expected.profile)p.id=mapped(p.id);check(expected==b.curves[i],"Curve/control/profile reference not remapped");}
    for(std::size_t i=0;i<a.hierarchy.size();++i){auto expected=a.hierarchy[i];expected.id=mapped(expected.id);if(expected.parent_id)expected.parent_id=mapped(expected.parent_id);check(expected==b.hierarchy[i],"Hierarchy parent/primitive reference not remapped");}
    check(a.anvil==b.anvil&&a.transform==b.transform&&a.optics==b.optics&&a.selection_value==b.selection_value,"Clone changed selected shape, anvil, transform or optics");
    check(a.content_hash!=b.content_hash&&a.payload_hash!=b.payload_hash,"Fresh cloud identity omitted from content/payload hashes");
}
void normal_clones(){
    for(const auto& preset:cloud_presets())for(auto seed:{UINT64_C(0),UINT64_MAX,UINT64_MAX-1}){
        const auto original=fixed(preset.kind,seed);const auto before=original;const auto jobs=generation_job_count();const auto cloned=clone_frozen_cloud(original);
        check(original==before&&generation_job_count()==jobs,"Cloning mutated source or ran a growth job");identities(original,cloned);identical_density(original,cloned.scene);
        check(cloned.scene.frozen->provenance.has_value()&&frozen_can_regenerate(*cloned.scene.frozen),"Clone silently discarded supported provenance");
        const auto initial=generation_initial_scene(*cloned.scene.frozen);check(generation_input_hash(initial,cloned.scene.frozen->provenance->settings)==cloned.scene.frozen->generation_input_hash,"Remapped provenance has an invalid input fingerprint");
        const auto restored=parse_scene_json(scene_json(cloned.scene));check(restored==cloned.scene,"Clone persistence lost remapping or local seed compensation");identical_density(original,restored);
        auto edit=regenerate_fixed_detail(cloned.scene,seed+1);check(original==before&&edit.frozen->content_hash==cloned.scene.frozen->content_hash&&generation_job_count()==jobs,"Clone detail edit affected source or regenerated structure");
        const auto& ids=cloned.ids;check(remap_cloned_id(ids,original.frozen->id)==cloned.scene.frozen->id,"Public clone map differs from result");
    }
}
void typed_history_and_limits(){
    auto custom=Scene{};custom.cloud.cells.front().structure_seed=UINT64_MAX;custom.cloud.noise.warp_amplitude=2;
    custom.cloud.cuts.push_back({50,{9,30,0},{2,3,2},1});
    for(auto source:std::array{custom,new_cumulonimbus_scene(),new_centerline_scene()}){
        GenerationSettings settings;settings.enabled=false;auto frozen=freeze_candidate(generate_cloud_state(source,settings));
        const auto clone=clone_frozen_cloud(frozen);identities(frozen,clone);identical_density(frozen,clone.scene);
        check(clone.scene.frozen->provenance->initial.index()==frozen.frozen->provenance->initial.index(),"Clone changed typed provenance kind");
    }
    auto original=fixed(CloudPresetKind::anvil,42);auto unknown=original;unknown.frozen->generation_version=999;unknown.frozen->provenance->settings.algorithm_version=999;refresh_frozen_scene(unknown);
    auto jobs=generation_job_count();const auto unknown_clone=clone_frozen_cloud(unknown);identical_density(unknown,unknown_clone.scene);
    check(unknown_clone.scene.frozen->provenance&&unknown_clone.scene.frozen->generation_version==999&&!frozen_can_regenerate(*unknown_clone.scene.frozen)&&generation_job_count()==jobs,"Unavailable generation was invoked, stripped, or enabled by clone");
    check(parse_scene_json(scene_json(unknown_clone.scene))==unknown_clone.scene,"Unavailable provenance clone did not survive persistence");
    auto independent=original;independent.frozen->provenance.reset();refresh_frozen_scene(independent);
    auto no_history=clone_frozen_cloud(independent);identical_density(independent,no_history.scene);check(!no_history.scene.frozen->provenance&&generation_job_count()==jobs,"History-free clone inferred a source or grew again");
    const auto count=no_history.ids.size();FrozenCloneOptions edge;edge.first_id=UINT64_MAX-count+1;
    const auto high=clone_frozen_cloud(independent,edge);check(high.ids.back().to==UINT64_MAX,"Clone refused valid upper uint64 boundary");identical_density(independent,high.scene);
    reject([&]{clone_frozen_cloud(high.scene);},"Exhausted clone namespace wrapped");
    edge.first_id=UINT64_MAX;reject([&]{clone_frozen_cloud(independent,edge);},"Multi-ID allocation overflow accepted");
    edge.first_id=0;reject([&]{clone_frozen_cloud(independent,edge);},"Zero ID accepted");
    edge.first_id=independent.frozen->id;reject([&]{clone_frozen_cloud(independent,edge);},"Source ID collision accepted");
    edge.first_id=1000;edge.reserved_ids={1001};reject([&]{clone_frozen_cloud(independent,edge);},"Reserved ID collision accepted");
    edge.first_id.reset();const auto reserved=clone_frozen_cloud(independent,edge);check(reserved.ids.front().to>1001,"Default allocator ignored reservation");
    edge.reserved_ids.assign(4097,1000);reject([&]{clone_frozen_cloud(independent,edge);},"Unbounded clone reservation accepted");
    reject([&]{clone_frozen_cloud(Scene{});},"Unfrozen scene cloned through fixed-state operation");
    reject([&]{remap_cloned_id(no_history.ids,UINT64_MAX);},"Unknown clone reference accepted");
    auto malformed=independent;malformed.frozen->fields[0].recipe.cells[0].id=0;reject([&]{clone_frozen_cloud(malformed);},"Malformed zero-ID source accepted");
    check(original.frozen&&generation_job_count()==jobs,"Rejected clone started generation");
}
}
int main(){try{normal_clones();typed_history_and_limits();finish_copy(fixed(CloudPresetKind::multiple,42));std::cout<<"Whole-cloud clone: exact density/GPU packet, monotone fresh identities, remapped typed history, independent edits, uint64 corners/overflow, unknown-version copies and zero generation jobs PASS\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
