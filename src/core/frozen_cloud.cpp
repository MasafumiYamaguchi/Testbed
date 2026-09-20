#include "white/frozen_cloud.hpp"
#include "white/generation.hpp"
#include "white/anvil_scene.hpp"
#include "white/noise.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <type_traits>

namespace white {
namespace {
bool finite(Vec3 p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
bool range(double v,double lo,double hi){return std::isfinite(v)&&v>=lo&&v<=hi;}
void require(const std::vector<std::string>& errors){if(errors.empty())return;std::string out="Invalid frozen cloud:";for(const auto& e:errors)out+="\n- "+e;throw std::invalid_argument(out);}
double smooth(double x){x=std::clamp(x,0.,1.);return x*x*(3-2*x);}
double coverage(double d){return 1-smooth((d+2)/2);}
double implicit(Vec3 p,Vec3 center,Vec3 r){p=p-center;p={p.x/r.x,p.y/r.y,p.z/r.z};return (std::sqrt(dot(p,p))-1)*std::min({r.x,r.y,r.z});}
double merge(double a,double b,double k){if(k==0)return std::min(a,b);const double h=std::max(k-std::abs(a-b),0.)/k;return std::min(a,b)-h*h*k*.25;}
void grow(Bounds& b,Vec3 lo,Vec3 hi){b.min={std::min(b.min.x,lo.x),std::min(b.min.y,lo.y),std::min(b.min.z,lo.z)};b.max={std::max(b.max.x,hi.x),std::max(b.max.y,hi.y),std::max(b.max.z,hi.z)};}
Bounds primitive_support(const CloudRecipe& r,double extra){
    if(r.cells.empty())return r.envelope;
    Bounds b{{INFINITY,INFINITY,INFINITY},{-INFINITY,-INFINITY,-INFINITY}};
    const double margin=(r.cells.size()-1)*r.blend_width/4+r.noise.warp_amplitude+2+extra;
    for(const auto& c:r.cells){const auto radius=c.radii*(1+margin/std::min({c.radii.x,c.radii.y,c.radii.z}));grow(b,c.center-radius,c.center+radius);}return b;
}
Bounds anvil_bounds(const FrozenAnvil& a){
    const Vec3 side{-a.direction.z,0,a.direction.x};const double along=a.along_radius+std::abs(a.shear)*a.half_thickness;
    const Vec3 extent{std::abs(a.direction.x)*along+std::abs(side.x)*a.cross_radius,a.half_thickness,std::abs(a.direction.z)*along+std::abs(side.z)*a.cross_radius};
    return {a.center-extent,a.center+extent};
}
struct Metrics {Bounds support{{-1,-1,-1},{1,1,1}};double maximum=0;};
Metrics metrics(const FrozenCloudState& state){
    Metrics out;if(state.fields.empty())return out;
    out.support={{INFINITY,INFINITY,INFINITY},{-INFINITY,-INFINITY,-INFINITY}};
    for(std::size_t i=0;i<state.fields.size();++i){const auto& group=state.fields[i];const DensityField field(frozen_effective_recipe(group));
        const auto b=state.fields.size()==1?field.local_support():primitive_support(field.recipe(),state.fusion_width/4);
        grow(out.support,b.min+group.translation,b.max+group.translation);
        const double maximum=field.maximum();out.maximum=i?std::max(out.maximum,maximum)+state.overlap*std::min(out.maximum,maximum):maximum;
    }
    if(state.anvil){const auto& a=*state.anvil;const auto b=anvil_bounds(a);grow(out.support,b.min,b.max);
        const auto r=frozen_effective_recipe(state.fields[0]);const AltitudeDensityEvaluator profile(r.altitude_density);
        out.maximum=std::max(out.maximum,r.density*a.density_scale*profile.maximum());
    }
    return out;
}
struct Shape {double distance=0,coefficient=0;};
Shape shape_sample(const DensityField& field,const AltitudeDensityEvaluator& profile,Vec3 p){
    const auto& r=field.recipe();if(r.cells.empty()||r.density==0||(r.base.enabled&&p.y<=r.base.height))return {};
    for(const auto& cut:r.cuts)if(implicit(p,cut.center,cut.radii)<=0)return {};
    double distance=0,sum=0;
    for(std::size_t i=0;i<r.cells.size();++i){const auto& c=r.cells[i];auto q=p;
        if(r.noise.warp_amplitude>0)q=q+domain_displacement((p-r.noise.origin)*r.noise.warp_frequency,noise_seed(cell_random_key(r,c)),r.noise.warp_amplitude);
        const double d=implicit(q,c.center,c.radii);distance=i?merge(distance,d,r.blend_width):d;sum+=coverage(d);
    }
    const auto n=p-r.noise.origin;const auto seed=noise_seed(r.detail_seed);
    if(r.noise.micro_erosion>0)distance+=r.noise.micro_erosion*detail_noise(n*r.noise.micro_frequency,seed^0x6c8e9cf5u);
    double factor=r.density*(1+r.overlap*std::max(0.,sum-1));
    if(r.noise.medium_strength>0)factor*=1-r.noise.medium_strength*detail_noise(n*r.noise.medium_frequency,seed);
    if(r.base.enabled&&r.base.transition>0)factor*=smooth((p.y-r.base.height)/r.base.transition);
    for(const auto& cut:r.cuts)if(cut.transition>0)factor*=smooth(implicit(p,cut.center,cut.radii)/cut.transition);
    factor*=profile.at(p.y);return {distance,factor};
}
GenerationRecipe source_recipe(const Scene& s){
    if(s.frozen)throw std::invalid_argument("A frozen result cannot become an implicit generation recipe");
    if(s.anvil)return *s.anvil;
    if(s.top_lobes)return *s.top_lobes;
    if(s.developed)return *s.developed;
    if(s.centerline)return *s.centerline;
    if(s.cumulonimbus)return *s.cumulonimbus;
    return s.cloud;
}
}
CloudRecipe frozen_effective_recipe(const FrozenField& field){
    auto r=field.recipe;
    if(!field.layers.base)r.density=0;
    if(!field.layers.macro)r.noise.warp_amplitude=0;
    if(!field.layers.medium)r.noise.medium_strength=0;
    if(!field.layers.micro)r.noise.micro_erosion=0;
    return r;
}
double frozen_anvil_edge_error_bound(const FrozenCloudState& state){
    if(!state.anvil)return 0;
    if(state.fields.empty())return INFINITY;
    const auto& a=*state.anvil;const auto& field=state.fields.front();const auto recipe=frozen_effective_recipe(field);
    if(recipe.density==0)return 0;
    AnvilSettings settings;settings.enabled=true;settings.thickness=2*a.half_thickness;
    settings.width=2*a.cross_radius;settings.extension=2*(a.along_radius-a.cross_radius);
    settings.direction=a.direction;settings.shear=a.shear;settings.edge_fade=a.edge_fade;settings.density_scale=a.density_scale;
    return anvil_edge_error_bound(settings,a.center,state.support,recipe.noise,field.translation);
}
std::vector<std::string> validate_frozen_cloud(const FrozenCloudState& s){
    std::vector<std::string> errors;auto check=[&](bool okay,const char* text){if(!okay)errors.emplace_back(text);};
    check(s.contract_version==frozen_cloud_contract_version,"Unsupported frozen data contract version");
    check(s.id!=0,"Frozen cloud stable ID must be nonzero");
    check(s.selection_kind=="development_stage"&&s.selection_unit=="dimensionless"&&range(s.selection_value,0,1),"Unsupported selected-state kind, unit or value");
    check(s.generation_version!=0,"Generation version must be nonzero");
    check(s.fields.size()<=2&&s.curves.size()<=2&&s.hierarchy.size()<=3,"Frozen field/curve/hierarchy budget exceeded");
    check(range(s.fusion_width,0,10000)&&range(s.overlap,0,1),"Frozen fusion/overlap outside supported range");
    std::set<Id> fields,primitives;std::size_t count=0;
    Scene metadata;metadata.cloud.id=s.id;metadata.cloud.cells.clear();metadata.cloud.transform=s.transform;metadata.cloud.optics=s.optics;metadata.cloud.envelope=s.support;
    for(const auto& e:validate(metadata))errors.push_back(e);
    for(std::size_t i=0;i<std::min<std::size_t>(s.fields.size(),2);++i){const auto& f=s.fields[i];
        check(f.development_id!=0&&fields.insert(f.development_id).second,"Frozen fields require unique stable IDs");
        check(finite(f.translation)&&std::max({std::abs(f.translation.x),std::abs(f.translation.y),std::abs(f.translation.z)})<=100000,"Frozen field translation outside supported range");
        check(f.development_id==f.recipe.id,"Frozen field ID differs from evaluated recipe ID");
        check(f.recipe.transform==Transform{}&&f.recipe.optics==s.optics,"Frozen field transform/optics must share object authority");
        Scene r;r.cloud=f.recipe;for(const auto& e:validate(r))errors.push_back("Frozen field: "+e);
        if(f.translation.y!=0&&f.recipe.altitude_density.enabled){
            auto shifted=f.recipe.altitude_density;shifted.base+=f.translation.y;
            const double error=2*(altitude_density_error_bound(f.recipe.altitude_density)+altitude_density_error_bound(shifted));
            check(error<=max_altitude_density_error,"Translated frozen field exceeds the altitude modulation error budget");
        }
        count+=f.recipe.cells.size();for(const auto& cell:f.recipe.cells)check(primitives.insert(cell.id).second,"Frozen primitive IDs must be globally unique");
    }
    check(count<=8,"Frozen cloud exceeds eight structural primitives");
    check(!primitives.contains(s.id),"Frozen cloud ID collides with a primitive");
    for(auto id:fields)check(!primitives.contains(id),"Frozen field ID collides with a primitive");
    std::map<Id,std::pair<std::size_t,Cut>> cuts;
    for(std::size_t i=0;i<std::min<std::size_t>(s.fields.size(),2);++i)for(auto cut:s.fields[i].recipe.cuts){
        check(cut.id!=s.id&&!fields.contains(cut.id)&&!primitives.contains(cut.id),"Frozen cut ID collides with structural identity");
        cut.center=cut.center+s.fields[i].translation;
        const auto [found,inserted]=cuts.emplace(cut.id,std::pair{i,cut});
        // A generated top inherits the trunk's same logical cuts. Retain that
        // alias only when both records describe the same object-local cut.
        check(inserted||(s.top_enabled&&i==1&&found->second.first==0&&found->second.second==cut),"Frozen cut ID aliases a different cut");
    }
    std::set<Id> curves;
    for(const auto& curve:s.curves){
        check(fields.contains(curve.development_id)&&curves.insert(curve.development_id).second,"Frozen curve requires one matching field ID");
        check(range(curve.base,-100000,100000)&&range(curve.height,10,4000),"Frozen curve frame outside supported range");
        check(finite(curve.growth_direction)&&std::abs(dot(curve.growth_direction,curve.growth_direction)-1)<1e-9&&curve.growth_direction.y>=.2,"Frozen curve direction must be upward and normalized");
        check(curve.points.size()>=2&&curve.points.size()<=6&&curve.profile.size()>=2&&curve.profile.size()<=8,"Frozen curve point/profile budget exceeded");
        if(!curve.points.empty())check(curve.points.front().t==0&&curve.points.back().t==1,"Frozen curve controls require endpoints at zero and one");
        if(!curve.profile.empty())check(curve.profile.front().t==0&&curve.profile.back().t==1,"Frozen curve profile requires endpoints at zero and one");
        std::set<Id> point_ids;double previous=-1;
        for(const auto& p:curve.points){check(p.id!=0&&point_ids.insert(p.id).second&&range(p.t,0,1)&&p.t>previous&&finite(p.offset)&&p.offset.y==0,"Invalid frozen control point");previous=p.t;}
        point_ids.clear();previous=-1;
        for(const auto& p:curve.profile){check(p.id!=0&&point_ids.insert(p.id).second&&range(p.t,0,1)&&p.t>previous&&range(p.radius_scale,.125,4)&&range(p.density_scale,0,1),"Invalid frozen profile point");previous=p.t;}
    }
    check(std::isfinite(s.top_boundary)&&s.top_mode<=2,"Invalid frozen top mask");
    check(!s.top_enabled||(s.fields.size()==2&&s.fields[1].translation==Vec3{}&&s.overlap==0&&!s.hierarchy.empty()&&s.top_mode!=0),"Frozen top mask requires an explicit second field and hierarchy");
    check(s.top_enabled||s.hierarchy.empty(),"Inactive frozen top cannot retain an unreferenced hierarchy");
    std::map<Id,unsigned> nodes;std::set<Id> top_primitives;unsigned roots=0;
    if(s.top_enabled&&s.fields.size()==2){
        const auto& r=s.fields[1].recipe;for(const auto& c:r.cells)top_primitives.insert(c.id);
        check(r.base.enabled&&r.base.height==s.top_boundary,"Frozen top mask differs from evaluated field boundary");
        check(r.cells.size()==s.hierarchy.size(),"Frozen hierarchy does not cover the top field primitives");
        const AltitudeDensityProfile mask{true,r.base.height,r.base.transition,{{0,0},{1,1}}};
        check(1.5*altitude_density_error_bound(mask)<=max_altitude_density_error,"Frozen top mask exceeds the float modulation error budget");
    }
    for(const auto& node:s.hierarchy){
        check(node.id!=0&&!nodes.contains(node.id)&&top_primitives.contains(node.id),"Frozen hierarchy must refer to unique top-field primitives");
        const auto parent=nodes.find(node.parent_id);check((node.depth==1&&node.parent_id==0)||(node.depth==2&&parent!=nodes.end()&&parent->second==1),"Frozen hierarchy parent/depth is invalid");
        roots+=node.depth==1;nodes.emplace(node.id,node.depth);
    }
    check(!s.top_enabled||(roots==1&&(s.top_mode!=1||s.hierarchy.size()==1)),"Frozen top hierarchy requires exactly one root and a matching comparison mode");
    if(s.anvil){const auto& a=*s.anvil;
        check(!s.fields.empty()&&(s.fields.size()==1||s.top_enabled),"Frozen anvil requires one trunk field");
        check(finite(a.center)&&finite(a.direction)&&a.direction.y==0&&std::abs(dot(a.direction,a.direction)-1)<1e-9,"Frozen anvil frame invalid");
        check(range(a.along_radius,4,25000)&&range(a.cross_radius,4,5000)&&range(a.half_thickness,.5,1000)&&a.cross_radius>=2*a.half_thickness&&a.along_radius>=a.cross_radius&&a.along_radius<=5*a.cross_radius,"Frozen anvil dimensions invalid");
        check(range(a.shear,-4,4)&&range(a.edge_fade,.01,250)&&a.edge_fade<=a.half_thickness*.25&&range(a.density_scale,0,1)&&std::isfinite(a.start_height),"Frozen anvil density/fade invalid");
        double scale=1;for(auto p:{s.support.min,s.support.max,a.center})scale=std::max({scale,std::abs(p.x),std::abs(p.y),std::abs(p.z)});
        check(scale*std::numeric_limits<float>::epsilon()*8/a.edge_fade<=.002,"Frozen anvil exceeds GPU coordinate precision budget");
        check(frozen_anvil_edge_error_bound(s)<=max_anvil_edge_error,"Frozen anvil edge arithmetic exceeds the coverage error budget");
    }
    if(s.provenance){check(s.provenance->settings.algorithm_version==s.generation_version,"Provenance generation version differs from frozen metadata");check(s.provenance->settings.stage==s.selection_value,"Provenance selected stage differs from frozen metadata");}
    if(errors.empty())try{const auto expected=metrics(s);check(s.support==expected.support&&s.rho_max==expected.maximum,"Frozen support/rho_max differs from evaluated structure");check(s.content_hash==frozen_content_hash(s),"Frozen structural identity mismatch");check(s.payload_hash==frozen_payload_hash(s),"Frozen payload checksum mismatch");}catch(const std::exception& e){errors.emplace_back(e.what());}
    return errors;
}
void refresh_frozen_cloud(FrozenCloudState& state){
    for(auto& field:state.fields){field.recipe.transform={};field.recipe.optics=state.optics;}
    const auto result=metrics(state);state.support=result.support;state.rho_max=result.maximum;state.content_hash=frozen_content_hash(state);state.payload_hash=frozen_payload_hash(state);require(validate_frozen_cloud(state));
}
CloudRecipe frozen_proxy_recipe(const FrozenCloudState& state){
    CloudRecipe out;out.cells.clear();if(!state.fields.empty()){
        out=frozen_effective_recipe(state.fields.front());const auto shift=state.fields.front().translation;
        for(auto& cell:out.cells)cell.center=cell.center+shift;
        for(auto& cut:out.cuts)cut.center=cut.center+shift;
        out.base.height+=shift.y;out.noise.origin=out.noise.origin+shift;
        if(out.altitude_density.enabled)out.altitude_density.base+=shift.y;
    }
    out.id=state.id;out.transform=state.transform;out.optics=state.optics;out.envelope=state.support;return out;
}
void refresh_frozen_scene(Scene& scene){if(!scene.frozen)throw std::invalid_argument("Scene has no frozen authority");auto next=scene;refresh_frozen_cloud(*next.frozen);next.cloud=frozen_proxy_recipe(*next.frozen);require_valid(next);scene=std::move(next);}
Scene freeze_candidate(const GenerationOutcome& outcome){if(outcome.status!=GenerationStatus::completed||!outcome.candidate)throw std::invalid_argument("Only a successfully completed generation candidate can be frozen");return freeze_candidate(*outcome.candidate);}
Scene freeze_candidate(const GenerationCandidate& candidate){
    require_valid(candidate.initial);require_valid(candidate.evaluated);
    if(candidate.input_hash!=generation_input_hash(candidate.initial,candidate.settings))throw std::invalid_argument("Candidate input hash differs from its provenance");
    const auto& selected=candidate.evaluated;FrozenCloudState state;state.id=selected.cloud.id;state.transform=selected.cloud.transform;state.optics=selected.cloud.optics;
    state.selection_value=candidate.settings.stage;state.generation_version=candidate.settings.algorithm_version;state.generation_input_hash=candidate.input_hash;
    state.provenance=GenerationProvenance{source_recipe(candidate.initial),candidate.settings};
    if(const auto* source=editable_developed_source(selected)){
        const DevelopedEvaluationPlan plan(*source);state.fusion_width=source->fusion_width;state.overlap=source->overlap;
        for(std::size_t i=0;i<plan.fields().size();++i){const auto& cell=plan.cloud().cells[i];state.fields.push_back({cell.id,plan.fields()[i].recipe(),cell.translation,{}});
            const auto& p=cell.shape.source.parameters;state.curves.push_back({cell.id,p.cloud_base,p.height,p.growth_direction,cell.shape.points,cell.shape.profile});}
        if(scene_has_active_top_lobes(selected)){
            const auto& top=*editable_top_lobe_source(selected);const TopLobeEvaluationPlan hierarchy(top);
            state.fields.push_back({top.field_id,hierarchy.top_field()->recipe(),{}, {}});state.top_enabled=true;state.top_boundary=top_lobe_mask_height(top);state.top_mode=unsigned(top.settings.mode);state.fusion_width=top.settings.fusion_width;state.overlap=0;
            for(const auto& node:hierarchy.hierarchy())state.hierarchy.push_back({node.id,node.parent_id,node.depth});
        }
        if(scene_has_active_anvil(selected)){
            const auto& source=*selected.anvil;const auto& a=source.settings;const auto& cell=source.cloud.trunk.cells.front();
            state.anvil=FrozenAnvil{anvil_connection_point(source)+a.direction*(a.extension*.5),a.direction,(a.width+a.extension)*.5,a.width*.5,a.thickness*.5,a.shear,a.edge_fade,a.density_scale,a.start_height+cell.translation.y};
        }
    }else {auto r=selected.cloud;r.transform={};state.fields.push_back({r.id,std::move(r),{}, {}});}
    refresh_frozen_cloud(state);auto result=selected;result.anvil.reset();result.top_lobes.reset();result.developed.reset();result.centerline.reset();result.cumulonimbus.reset();result.frozen=std::move(state);result.cloud=frozen_proxy_recipe(*result.frozen);require_valid(result);return result;
}
bool frozen_can_regenerate(const FrozenCloudState& state){return state.provenance&&state.generation_version==generation_algorithm_version&&state.provenance->settings.algorithm_version==generation_algorithm_version;}
Scene generation_initial_scene(const FrozenCloudState& state){
    require(validate_frozen_cloud(state));if(!frozen_can_regenerate(state))throw std::invalid_argument("Saved generation version is unavailable; the frozen result remains usable");
    Scene out;std::visit([&](const auto& source){using T=std::decay_t<decltype(source)>;
        if constexpr(std::is_same_v<T,CloudRecipe>)out.cloud=source;
        else if constexpr(std::is_same_v<T,CumulonimbusGroup>){out.cumulonimbus=source;out.cloud=derive_cumulonimbus_recipe(source);}
        else if constexpr(std::is_same_v<T,CenterlineShape>){out.centerline=source;out.cloud=lower_centerline_to_recipe(source);}
        else if constexpr(std::is_same_v<T,DevelopedCloud>){out.developed=source;out.cloud=developed_proxy_recipe(source);}
        else if constexpr(std::is_same_v<T,TopLobeSource>){out.top_lobes=source;out.cloud=top_lobe_proxy_recipe(source);}
        else {out.anvil=source;out.cloud=anvil_proxy_recipe(source);}
    },state.provenance->initial);require_valid(out);return out;
}
Scene scene_with_frozen_detail(Scene scene,Id id,NoiseSettings noise,std::uint64_t seed,FrozenDetailLayers layers){
    require_valid(scene);if(!scene.frozen)throw std::invalid_argument("Freeze a completed state before detail editing");
    auto& fields=scene.frozen->fields;const auto found=std::find_if(fields.begin(),fields.end(),[&](const auto& f){return f.development_id==id;});
    if(found==fields.end())throw std::invalid_argument("Unknown frozen field ID");
    if(noise.origin!=found->recipe.noise.origin)throw std::invalid_argument("Frozen detail keeps the saved reference origin");
    if(noise.warp_amplitude>found->recipe.noise.warp_amplitude){const auto support=primitive_support(found->recipe,noise.warp_amplitude-found->recipe.noise.warp_amplitude);grow(found->recipe.envelope,support.min,support.max);}
    const auto content=scene.frozen->content_hash;
    found->recipe.noise=noise;found->recipe.detail_seed=seed;found->layers=layers;refresh_frozen_scene(scene);
    if(scene.frozen->content_hash!=content)throw std::logic_error("Detail changed immutable frozen geometry");
    return scene;
}
FrozenEvaluationPlan::FrozenEvaluationPlan(FrozenCloudState state):state_(std::move(state)){
    require(validate_frozen_cloud(state_));for(const auto& field:state_.fields){fields_.emplace_back(frozen_effective_recipe(field));profiles_.emplace_back(fields_.back().recipe().altitude_density);}
}
double FrozenEvaluationPlan::at(Vec3 p)const{
    if(!finite(p))throw std::invalid_argument("Nonfinite frozen density sample");
    if(fields_.empty())return 0;
    const auto& support=state_.support;if(p.x<=support.min.x||p.y<=support.min.y||p.z<=support.min.z||p.x>=support.max.x||p.y>=support.max.y||p.z>=support.max.z)return 0;
    double original=0;
    if(fields_.size()==1||(state_.top_enabled&&(p.y<=state_.top_boundary||fields_[1].maximum()==0)))original=fields_[0].at(p-state_.fields[0].translation);
    else {const auto a=shape_sample(fields_[0],profiles_[0],p-state_.fields[0].translation),b=shape_sample(fields_[1],profiles_[1],p-state_.fields[1].translation);original=std::clamp(developed_density_union(a.distance,a.coefficient,b.distance,b.coefficient,state_.fusion_width,state_.overlap),0.,state_.rho_max);}
    if(!state_.anvil||p.y<=state_.anvil->start_height)return original;
    const auto& a=*state_.anvil;const Vec3 side{-a.direction.z,0,a.direction.x};auto q=p-a.center;q=q-a.direction*(a.shear*q.y);
    const double u=dot(q,a.direction)/a.along_radius,v=dot(q,side)/a.cross_radius,w=q.y/a.half_thickness;
    double distance=(std::sqrt(u*u+v*v+w*w)-1)*a.half_thickness;
    const auto& r=fields_[0].recipe();const auto local=p-state_.fields[0].translation,n=local-r.noise.origin;const auto seed=noise_seed(r.detail_seed);
    if(r.noise.micro_erosion>0)distance+=r.noise.micro_erosion*detail_noise(n*r.noise.micro_frequency,seed^0x6c8e9cf5u);
    double addition=r.density*a.density_scale*(1-smooth((distance+a.edge_fade)/a.edge_fade));if(addition==0)return original;
    if(r.noise.medium_strength>0)addition*=1-r.noise.medium_strength*detail_noise(n*r.noise.medium_frequency,seed);
    if(r.base.enabled){if(local.y<=r.base.height)return original;if(r.base.transition>0)addition*=smooth((local.y-r.base.height)/r.base.transition);}
    for(const auto& cut:r.cuts){const double d=implicit(local,cut.center,cut.radii);if(d<=0)return original;if(cut.transition>0)addition*=smooth(d/cut.transition);}
    addition*=profiles_[0].at(local.y);return std::clamp(std::max(original,addition),0.,state_.rho_max);
}
Bounds FrozenEvaluationPlan::world_support()const{Bounds out{{INFINITY,INFINITY,INFINITY},{-INFINITY,-INFINITY,-INFINITY}};const auto b=state_.support;for(unsigned i=0;i<8;++i){const auto p=local_to_world(state_.transform,{i&1?b.max.x:b.min.x,i&2?b.max.y:b.min.y,i&4?b.max.z:b.min.z});grow(out,p,p);}return out;}
GpuAnvilParams FrozenEvaluationPlan::gpu_params()const{
    GpuAnvilParams out;auto& group=out.cloud.fields;for(std::size_t i=0;i<fields_.size();++i){group.groups[i]=gpu_density_params(fields_[i]);const auto t=state_.fields[i].translation;group.translations[i]={float(t.x),float(t.y),float(t.z),0};}
    group.envelope_min={float(state_.support.min.x),float(state_.support.min.y),float(state_.support.min.z),0};group.envelope_max={float(state_.support.max.x),float(state_.support.max.y),float(state_.support.max.z),0};
    const bool top=state_.top_enabled&&fields_.size()==2;
    group.settings={float(fields_.size()),float(state_.fusion_width),float(state_.overlap),float(state_.rho_max)};
    out.cloud.mask={top?1.f:0.f,float(state_.top_boundary),float(state_.top_mode),float(state_.hierarchy.size())};
    if(state_.anvil){const auto& a=*state_.anvil;out.center={float(a.center.x),float(a.center.y),float(a.center.z),0};out.direction={float(a.direction.x),0,float(a.direction.z),float(a.shear)};out.dimensions={float(a.along_radius),float(a.cross_radius),float(a.half_thickness),float(a.edge_fade)};out.settings={1,float(a.density_scale),float(a.start_height),float(state_.rho_max)};}
    out.envelope_min=group.envelope_min;out.envelope_max=group.envelope_max;return out;
}
}
