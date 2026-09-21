#include "white/cloud_clone.hpp"
#include "white/generation.hpp"
#include "white/anvil_scene.hpp"
#include "white/persistence.hpp"
#include <algorithm>
#include <bit>
#include <limits>
#include <set>
#include <stdexcept>
#include <type_traits>

namespace white {
namespace {
constexpr std::size_t max_clone_ids=256,max_reserved_ids=4096;
constexpr std::uint64_t mix(std::uint64_t x){x+=UINT64_C(0x9e3779b97f4a7c15);x=(x^(x>>30))*UINT64_C(0xbf58476d1ce4e5b9);x=(x^(x>>27))*UINT64_C(0x94d049bb133111eb);return x^(x>>31);}
constexpr std::uint64_t inverse_odd(std::uint64_t value){
    // Newton iteration doubles the correct low bits each time in Z/(2^64).
    std::uint64_t inverse=1;for(unsigned i=0;i<6;++i)inverse*=2-value*inverse;return inverse;
}
constexpr std::uint64_t undo_shift(std::uint64_t value,unsigned shift){
    std::uint64_t out=value;for(unsigned bit=shift;bit<64;bit+=shift)out^=value>>bit;return out;
}
constexpr std::uint64_t unmix(std::uint64_t x){
    x=undo_shift(x,31);x*=inverse_odd(UINT64_C(0x94d049bb133111eb));
    x=undo_shift(x,27);x*=inverse_odd(UINT64_C(0xbf58476d1ce4e5b9));
    x=undo_shift(x,30);return x-UINT64_C(0x9e3779b97f4a7c15);
}
static_assert(unmix(mix(0))==0&&unmix(mix(UINT64_MAX))==UINT64_MAX);
static_assert(unmix(mix(UINT64_MAX-1))==UINT64_MAX-1&&unmix(mix(1))==1);
template<class T>void bounded(const T& values,std::size_t maximum){if(values.size()>maximum)throw std::invalid_argument("Cloud clone source exceeds its bounded collection contract");}
template<class F>void recipe_ids(CloudRecipe& r,F& visit){
    bounded(r.cells,8);bounded(r.cuts,8);visit(r.id);for(auto& c:r.cells)visit(c.id);for(auto& c:r.cuts)visit(c.id);
}
template<class F>void group_ids(CumulonimbusGroup& g,F& visit){
    bounded(g.cell_adjustments,5);bounded(g.modifiers.manual_cells,3);bounded(g.modifiers.cuts,8);
    visit(g.cloud_id);for(auto& id:g.cell_ids)visit(id);for(auto& a:g.cell_adjustments)visit(a.cell_id);
    for(auto& c:g.modifiers.manual_cells)visit(c.id);
    for(auto& c:g.modifiers.cuts)visit(c.id);
}
template<class F>void curve_ids(CenterlineShape& c,F& visit){
    group_ids(c.source,visit);bounded(c.points,6);bounded(c.profile,8);
    for(auto& p:c.points)visit(p.id);
    for(auto& p:c.profile)visit(p.id);
}
template<class F>void developed_ids(DevelopedCloud& d,F& visit){
    bounded(d.cells,2);visit(d.id);for(auto& c:d.cells){visit(c.id);curve_ids(c.shape,visit);}
}
template<class F>void top_ids(TopLobeSource& t,F& visit){developed_ids(t.trunk,visit);visit(t.target_cell);visit(t.field_id);for(auto& id:t.lobe_ids)visit(id);}
template<class F>void provenance_ids(GenerationProvenance& p,F& visit){
    std::visit([&](auto& source){using T=std::decay_t<decltype(source)>;
        if constexpr(std::is_same_v<T,CloudRecipe>)recipe_ids(source,visit);
        else if constexpr(std::is_same_v<T,CumulonimbusGroup>)group_ids(source,visit);
        else if constexpr(std::is_same_v<T,CenterlineShape>)curve_ids(source,visit);
        else if constexpr(std::is_same_v<T,DevelopedCloud>)developed_ids(source,visit);
        else if constexpr(std::is_same_v<T,TopLobeSource>)top_ids(source,visit);
        else top_ids(source.cloud,visit);
    },p.initial);
    bounded(p.settings.cells,2);bounded(p.settings.wind,6);
    for(auto& c:p.settings.cells){visit(c.cell_id);bounded(c.pinned_controls,6);for(auto& id:c.pinned_controls)visit(id);}
}
template<class F>void frozen_ids(FrozenCloudState& state,F& visit){
    bounded(state.fields,2);bounded(state.curves,2);bounded(state.hierarchy,3);visit(state.id);
    for(auto& field:state.fields){visit(field.development_id);recipe_ids(field.recipe,visit);}
    for(auto& c:state.curves){visit(c.development_id);bounded(c.points,6);bounded(c.profile,8);for(auto& p:c.points)visit(p.id);for(auto& p:c.profile)visit(p.id);}
    for(auto& n:state.hierarchy){visit(n.id);if(n.parent_id)visit(n.parent_id);}
    if(state.provenance)provenance_ids(*state.provenance,visit);
}
Scene initial_scene(const GenerationRecipe& recipe){
    Scene out;std::visit([&](const auto& source){using T=std::decay_t<decltype(source)>;
        if constexpr(std::is_same_v<T,CloudRecipe>)out.cloud=source;
        else if constexpr(std::is_same_v<T,CumulonimbusGroup>){out.cumulonimbus=source;out.cloud=derive_cumulonimbus_recipe(source);}
        else if constexpr(std::is_same_v<T,CenterlineShape>){out.centerline=source;out.cloud=lower_centerline_to_recipe(source);}
        else if constexpr(std::is_same_v<T,DevelopedCloud>){out.developed=source;out.cloud=developed_proxy_recipe(source);}
        else if constexpr(std::is_same_v<T,TopLobeSource>){out.top_lobes=source;out.cloud=top_lobe_proxy_recipe(source);}
        else {out.anvil=source;out.cloud=anvil_proxy_recipe(source);}
    },recipe);require_valid(out);return out;
}
std::uint64_t provenance_hash(const GenerationProvenance& provenance){
    // This is the generation-input fingerprint encoding, not the generation
    // algorithm. It remains defined for a saved unavailable algorithm version.
    // Known-version tests compare it against generation_input_hash directly.
    const auto initial=initial_scene(provenance.initial);const auto& s=provenance.settings;
    auto validation=s;validation.algorithm_version=generation_algorithm_version;validation.enabled=false;
    const auto errors=validate_generation(initial,validation);if(!errors.empty())throw std::invalid_argument("Cannot clone invalid generation provenance: "+errors.front());
    std::uint64_t hash=UINT64_C(14695981039346656037);
    for(unsigned char c:scene_json(initial)){hash^=c;hash*=UINT64_C(1099511628211);}
    auto add=[&](std::uint64_t value){for(unsigned i=0;i<8;++i){hash^=(value>>(8*i))&255;hash*=UINT64_C(1099511628211);}};
    auto real=[&](double value){add(std::bit_cast<std::uint64_t>(value==0?0.:value));};
    add(s.algorithm_version);add(s.enabled);real(s.stage);real(s.initial_height_fraction);real(s.wind_base);real(s.wind_height);
    add(s.wind.size());for(const auto& k:s.wind){real(k.altitude);real(k.displacement.x);real(k.displacement.z);}
    real(s.reference_translation.x);real(s.reference_translation.z);
    auto cells=s.cells;std::sort(cells.begin(),cells.end(),[](const auto& a,const auto& b){return a.cell_id<b.cell_id;});add(cells.size());
    for(auto& c:cells){add(c.cell_id);real(c.start_stage);real(c.amount);std::sort(c.pinned_controls.begin(),c.pinned_controls.end());add(c.pinned_controls.size());for(auto id:c.pinned_controls)add(id);}
    return hash;
}
template<class S>std::set<Id> finish_ids(const S& scene){
    std::set<Id> ids;if constexpr(requires {scene.finish_stack.layers;}){
        bounded(scene.finish_stack.layers,4);for(const auto& layer:scene.finish_stack.layers){if(layer.id==0||!ids.insert(layer.id).second)throw std::invalid_argument("Invalid finish layer clone identity");}
    }return ids;
}
template<class S>void finish_targets(S& scene,std::span<const IdRemap> ids,std::span<const IdRemap> layers){
    // Finishing IDs have their own namespace. This also supports the upcoming
    // Scene.finish_stack contract without conflating layer IDs with object IDs.
    if constexpr(requires {scene.finish_stack.layers;})for(auto& layer:scene.finish_stack.layers){layer.target_id=remap_cloned_id(ids,layer.target_id);layer.id=remap_cloned_id(layers,layer.id);}
}
}
Id remap_cloned_id(std::span<const IdRemap> ids,Id old){
    const auto found=std::lower_bound(ids.begin(),ids.end(),old,[](const auto& pair,Id value){return pair.from<value;});
    if(found==ids.end()||found->from!=old)throw std::invalid_argument("Cloud clone reference has no ID mapping");
    return found->to;
}
FrozenCloneResult clone_frozen_cloud(const Scene& source,const FrozenCloneOptions& options){
    require_valid(source);if(!source.frozen)throw std::invalid_argument("Freeze a selected cloud before cloning it");
    if(options.reserved_ids.size()>max_reserved_ids)throw std::invalid_argument("Cloud clone reservation limit exceeded");
    FrozenCloneResult result{source,{}, {}};std::set<Id> originals;
    auto collect=[&](Id& id){if(id==0)throw std::invalid_argument("Cloud clone contains a zero stable identity");originals.insert(id);if(originals.size()>max_clone_ids)throw std::invalid_argument("Cloud clone identity budget exceeded");};
    frozen_ids(*result.scene.frozen,collect);
    const auto layer_ids=finish_ids(source);auto occupied=originals;occupied.insert(layer_ids.begin(),layer_ids.end());
    for(Id id:options.reserved_ids){if(id==0)throw std::invalid_argument("Reserved clone IDs must be nonzero");occupied.insert(id);}
    Id first=0;if(options.first_id)first=*options.first_id;
    else {const auto maximum=*occupied.rbegin();if(maximum==UINT64_MAX)throw std::overflow_error("Cloud clone stable ID namespace exhausted");first=maximum+1;}
    if(first==0)throw std::invalid_argument("Cloud clone allocation cannot start at zero");
    const auto count=originals.size()+layer_ids.size();
    if(count-1>UINT64_MAX-first)throw std::overflow_error("Cloud clone stable ID range would overflow");
    const auto last=first+count-1;const auto collision=occupied.lower_bound(first);
    if(collision!=occupied.end()&&*collision<=last)throw std::invalid_argument("Cloud clone stable ID range collides with an existing identity");
    Id next=first;for(Id old:originals){result.ids.push_back({old,next});if(next!=last)++next;}
    for(Id old:layer_ids){result.finish_ids.push_back({old,next});if(next!=last)++next;}
    auto remap=[&](Id& id){id=remap_cloned_id(result.ids,id);};frozen_ids(*result.scene.frozen,remap);
    // Keep G ^ mix(id) ^ mix(localSeed) unchanged. Since mix is a bijection,
    // localSeed' = unmix(mix(localSeed) ^ mix(oldId) ^ mix(newId)). All unsigned
    // arithmetic intentionally wraps mod 2^64; no stochastic search is needed.
    for(std::size_t i=0;i<source.frozen->fields.size();++i){const auto& before=source.frozen->fields[i].recipe;auto& after=result.scene.frozen->fields[i].recipe;
        for(std::size_t c=0;c<before.cells.size();++c){const auto& old=before.cells[c];auto& copy=after.cells[c];copy.structure_seed=unmix(mix(old.structure_seed)^mix(old.id)^mix(copy.id));
            if(cell_random_key(before,old)!=cell_random_key(after,copy))throw std::logic_error("Cloud clone failed to preserve a stochastic key");}
    }
    if(result.scene.frozen->provenance)result.scene.frozen->generation_input_hash=provenance_hash(*result.scene.frozen->provenance);
    finish_targets(result.scene,result.ids,result.finish_ids);refresh_frozen_scene(result.scene);require_valid(result.scene);return result;
}
}
