#include "white/developed_cells.hpp"
#include "white/noise.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <type_traits>

namespace white {
namespace {
bool finite(Vec3 p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
double smooth(double x){x=std::clamp(x,0.,1.);return x*x*(3-2*x);}
double coverage(double d){return 1-smooth((d+2)/2);}
double implicit(Vec3 p,Vec3 center,Vec3 radii){p=p-center;p={p.x/radii.x,p.y/radii.y,p.z/radii.z};return (std::sqrt(dot(p,p))-1)*std::min({radii.x,radii.y,radii.z});}
double merge(double a,double b,double k){if(k==0)return std::min(a,b);const double h=std::max(k-std::abs(a-b),0.)/k;return std::min(a,b)-h*h*k*.25;}
void require(const std::vector<std::string>& errors){if(errors.empty())return;std::string text="Invalid developed cloud:";for(const auto& error:errors)text+="\n- "+error;throw std::invalid_argument(text);}
void canonical(DevelopedCloud& cloud){std::sort(cloud.cells.begin(),cloud.cells.end(),[](const auto& a,const auto& b){return a.id<b.id;});for(auto& cell:cloud.cells)std::sort(cell.roles.begin(),cell.roles.end());}
void grow(Bounds& b,Vec3 lo,Vec3 hi){b.min={std::min(b.min.x,lo.x),std::min(b.min.y,lo.y),std::min(b.min.z,lo.z)};b.max={std::max(b.max.x,hi.x),std::max(b.max.y,hi.y),std::max(b.max.z,hi.z)};}
Bounds primitive_support(const CloudRecipe& recipe,double extra=0){
    Bounds b{{INFINITY,INFINITY,INFINITY},{-INFINITY,-INFINITY,-INFINITY}};
    const double margin=(recipe.cells.size()-1)*recipe.blend_width/4+recipe.noise.warp_amplitude+2+extra;
    for(const auto& cell:recipe.cells){const auto r=cell.radii*(1+margin/std::min({cell.radii.x,cell.radii.y,cell.radii.z}));grow(b,cell.center-r,cell.center+r);}return b;
}
CloudRecipe field_recipe(const DevelopedCloud& cloud,const DevelopedCell& cell){
    auto recipe=lower_centerline_to_recipe(cell.shape);recipe.transform={};recipe.optics=cloud.optics;
    std::set<Id> active;for(auto role:cell.roles)active.insert(cell.shape.source.cell_ids.at(role));
    std::erase_if(recipe.cells,[&](const Cell& c){return std::find(cell.shape.source.cell_ids.begin(),cell.shape.source.cell_ids.end(),c.id)!=cell.shape.source.cell_ids.end()&&!active.contains(c.id);});
    // Keeping every role must preserve the original envelope and arithmetic.
    if(cell.roles.size()!=cumulonimbus_generated_cells)recipe.envelope=primitive_support(recipe);
    return recipe;
}
std::set<Id> all_ids(const DevelopedCloud& cloud){
    std::set<Id> ids{cloud.id};for(const auto& cell:cloud.cells){ids.insert(cell.id);for(auto id:cell.shape.source.cell_ids)ids.insert(id);for(const auto& c:cell.shape.source.modifiers.manual_cells)ids.insert(c.id);for(const auto& c:cell.shape.source.modifiers.cuts)ids.insert(c.id);}return ids;
}
Id take_id(Id& next){if(next==std::numeric_limits<Id>::max())throw std::overflow_error("Developed stable ID namespace exhausted");return next++;}
DevelopedCell& selected(DevelopedCloud& cloud,Id id){const auto found=std::find_if(cloud.cells.begin(),cloud.cells.end(),[&](const auto& c){return c.id==id;});if(found==cloud.cells.end())throw std::invalid_argument("Unknown developed Cell ID");return *found;}
struct ShapeSample {double distance=0,coefficient=0,density=0;};
ShapeSample shape_sample(const DensityField& field,const AltitudeDensityEvaluator& profile,Vec3 p){
    const auto& r=field.recipe();ShapeSample out;
    if(r.cells.empty()||r.density==0||(r.base.enabled&&p.y<=r.base.height))return out;
    for(const auto& cut:r.cuts)if(implicit(p,cut.center,cut.radii)<=0)return out;
    double merged=0,sum=0;
    for(std::size_t i=0;i<r.cells.size();++i){const auto& c=r.cells[i];auto q=p;
        if(r.noise.warp_amplitude>0)q=q+domain_displacement((p-r.noise.origin)*r.noise.warp_frequency,noise_seed(cell_random_key(r,c)),r.noise.warp_amplitude);
        const double d=implicit(q,c.center,c.radii);merged=i==0?d:merge(merged,d,r.blend_width);sum+=coverage(d);
    }
    const auto n=p-r.noise.origin;const auto seed=noise_seed(r.detail_seed);
    if(r.noise.micro_erosion>0)merged+=r.noise.micro_erosion*detail_noise(n*r.noise.micro_frequency,seed^0x6c8e9cf5u);
    double factor=r.density*(1+r.overlap*std::max(0.,sum-1));
    if(r.noise.medium_strength>0)factor*=1-r.noise.medium_strength*detail_noise(n*r.noise.medium_frequency,seed);
    if(r.base.enabled&&r.base.transition>0)factor*=smooth((p.y-r.base.height)/r.base.transition);
    for(const auto& cut:r.cuts)if(cut.transition>0)factor*=smooth(implicit(p,cut.center,cut.radii)/cut.transition);
    factor*=profile.at(p.y);
    out.distance=merged;out.coefficient=factor;out.density=coverage(merged)*factor;return out;
}
}
std::size_t developed_primitive_count(const DevelopedCloud& cloud){std::size_t n=0;for(const auto& cell:cloud.cells)n+=cell.roles.size()+cell.shape.source.modifiers.manual_cells.size();return n;}
std::vector<std::string> validate_developed_cloud(const DevelopedCloud& cloud){
    std::vector<std::string> errors;auto check=[&](bool okay,const char* text){if(!okay)errors.emplace_back(text);};
    check(cloud.contract_version==developed_cloud_contract_version,"Unsupported developed source version");
    check(cloud.cells.size()<=max_developed_cells,"At most two independently developed cells are supported");
    check(developed_primitive_count(cloud)<=max_developed_primitives,"Eight-primitive budget exceeded; choose role budgets explicitly");
    check(std::isfinite(cloud.fusion_width)&&cloud.fusion_width>=0&&cloud.fusion_width<=10000,"Fusion width must be in 0..10000 local metres");
    check(std::isfinite(cloud.overlap)&&cloud.overlap>=0&&cloud.overlap<=1,"Density overlap must be in 0..1");
    Scene base;base.cloud.id=cloud.id;base.cloud.transform=cloud.transform;base.cloud.optics=cloud.optics;
    // Validation's default Cell ID need not be part of this source namespace.
    base.cloud.cells.clear();for(const auto& error:validate(base))errors.push_back(error);
    std::set<Id> ids{cloud.id};
    for(std::size_t index=0;index<std::min(cloud.cells.size(),max_developed_cells);++index){const auto& cell=cloud.cells[index];
        check(cell.id!=0&&ids.insert(cell.id).second,"Developed Cell IDs must be nonzero and globally unique");
        check(cell.shape.source.cloud_id==cell.id,"Development source cloud ID must equal its stable cell ID");
        check(cell.shape.source.modifiers.transform==Transform{},"Development-local source transform must be identity; use its translation and object transform");
        check(cell.shape.source.modifiers.optics==cloud.optics,"All developed cells share the object's optical coefficients");
        check(finite(cell.translation)&&std::max({std::abs(cell.translation.x),std::abs(cell.translation.y),std::abs(cell.translation.z)})<=100000,"Development translation must be finite within 100000 local metres");
        check(cell.roles.size()>=3&&cell.roles.size()<=5,"Each development needs three to five explicit generated roles");
        std::set<unsigned> roles;
        for(auto role:cell.roles)check(role<5&&roles.insert(role).second,"Role indices must be unique in 0..4");
        check(roles.contains(0)&&roles.contains(1)&&roles.contains(2),"Base, middle and upper roles 0/1/2 are required to represent a developed curve");
        for(auto id:cell.shape.source.cell_ids)check(id!=0&&ids.insert(id).second,"Reserved primitive IDs must be globally unique, including inactive roles");
        for(const auto& c:cell.shape.source.modifiers.manual_cells)check(c.id!=0&&ids.insert(c.id).second,"Manual primitive IDs must be globally unique");
        for(const auto& c:cell.shape.source.modifiers.cuts)check(c.id!=0&&ids.insert(c.id).second,"Cut IDs must be globally unique");
        for(const auto& error:validate_centerline(cell.shape))errors.push_back(error);
    }
    if(errors.empty())for(const auto& cell:cloud.cells){auto recipe=field_recipe(cloud,cell);const auto support=primitive_support(recipe,cloud.cells.size()>1?cloud.fusion_width/4:0);
        if(cell.translation.y!=0&&recipe.altitude_density.enabled){
            auto shifted=recipe.altitude_density;shifted.base+=cell.translation.y;
            // The GPU rounds object Y, subtracts packed translation, and then
            // normalizes relative to the local profile. Both coordinate scales
            // matter: translated-only validation misses cancellation when a
            // large local base is translated back near zero. Twice the sum of
            // the existing four-ULP bounds conservatively allows the additional
            // subtraction/translation packing. Zero translation keeps the exact
            // previously accepted single-development migration domain.
            const double error=2*(altitude_density_error_bound(recipe.altitude_density)+altitude_density_error_bound(shifted));
            check(error<=max_altitude_density_error,"Translated development exceeds the 0.0001 altitude modulation error budget; widen the density band or reduce local translation");
        }
        recipe.envelope={support.min+cell.translation,support.max+cell.translation};Scene checked;checked.cloud=recipe;for(const auto& error:validate(checked))errors.push_back(error);}
    return errors;
}
Id next_developed_id(const DevelopedCloud& cloud){require(validate_developed_cloud(cloud));const auto ids=all_ids(cloud);if(*ids.rbegin()==std::numeric_limits<Id>::max())throw std::overflow_error("Developed stable ID namespace exhausted");return *ids.rbegin()+1;}
DevelopedCloud develop_centerline(const CenterlineShape& shape){
    const auto recipe=lower_centerline_to_recipe(shape);DevelopedCloud cloud;cloud.id=shape.source.cloud_id;cloud.transform=recipe.transform;cloud.optics=recipe.optics;cloud.fusion_width=recipe.blend_width;cloud.overlap=recipe.overlap;
    Id maximum=cloud.id;for(const auto& cell:recipe.cells)maximum=std::max(maximum,cell.id);for(const auto& cut:recipe.cuts)maximum=std::max(maximum,cut.id);
    if(maximum==std::numeric_limits<Id>::max())throw std::overflow_error("Developed migration needs one unused ID");
    DevelopedCell cell;cell.id=maximum+1;cell.shape=shape;cell.shape.source.cloud_id=cell.id;cell.shape.source.modifiers.transform={};cell.roles={0,1,2,3,4};cloud.cells.push_back(cell);
    require(validate_developed_cloud(cloud));return cloud;
}
DevelopedCloud develop_cumulonimbus(const CumulonimbusGroup& group){CenterlineShape shape;shape.source=group;return develop_centerline(shape);}
DevelopedCell make_developed_cell(const DevelopedCloud& cloud){
    Id next=next_developed_id(cloud);DevelopedCell cell;cell.id=take_id(next);cell.shape.source.cloud_id=cell.id;cell.shape.source.modifiers.optics=cloud.optics;
    for(auto& id:cell.shape.source.cell_ids)id=take_id(next);
    return cell;
}
DevelopedCloud command_developed_cloud(const DevelopedCloud& source,const DevelopedCommand& command){
    require(validate_developed_cloud(source));auto next=source;
    std::visit([&](const auto& edit){using T=std::decay_t<decltype(edit)>;
        if constexpr(std::is_same_v<T,DevelopedAdd>)next.cells.push_back(edit.cell);
        else if constexpr(std::is_same_v<T,DevelopedRemove>){(void)selected(next,edit.id);std::erase_if(next.cells,[&](const auto& c){return c.id==edit.id;});}
        else if constexpr(std::is_same_v<T,DevelopedMove>)selected(next,edit.id).translation=edit.translation;
        else if constexpr(std::is_same_v<T,DevelopedEdit>)selected(next,edit.id).shape=command_centerline(selected(next,edit.id).shape,edit.command);
        else if constexpr(std::is_same_v<T,DevelopedSetRoles>)selected(next,edit.id).roles=edit.roles;
        else {
            auto copy=selected(next,edit.id);Id id=next_developed_id(source);copy.id=take_id(id);copy.shape.source.cloud_id=copy.id;
            for(auto& primitive:copy.shape.source.cell_ids){const Id old=primitive;primitive=take_id(id);for(auto& a:copy.shape.source.cell_adjustments)if(a.cell_id==old)a.cell_id=primitive;}
            for(auto& c:copy.shape.source.modifiers.manual_cells)c.id=take_id(id);
            for(auto& c:copy.shape.source.modifiers.cuts)c.id=take_id(id);
            switch(edit.policy){case DevelopedDuplicateSeed::retain:break;case DevelopedDuplicateSeed::regenerate:
                if(edit.structure_seed==copy.shape.source.parameters.structure_seed)throw std::invalid_argument("Duplicate regeneration requires a different explicit structure seed");
                copy.shape.source.parameters.structure_seed=edit.structure_seed;break;default:throw std::invalid_argument("Unknown developed duplicate seed policy");}
            next.cells.push_back(copy);
        }
    },command);canonical(next);require(validate_developed_cloud(next));return next;
}
double developed_density_union(double da,double ca,double db,double cb,double fusion,double overlap){
    if(!std::isfinite(da)||!std::isfinite(db)||!std::isfinite(ca)||!std::isfinite(cb)||ca<0||cb<0||
       !std::isfinite(fusion)||fusion<0||!std::isfinite(overlap)||overlap<0||overlap>1)
        throw std::invalid_argument("Invalid developed density union inputs");
    const double qa=coverage(da),qb=coverage(db),a=ca*qa,b=cb*qb;
    const double fused=coverage(merge(da,db,fusion));
    const double bridge=std::max(0.,fused-std::max(qa,qb))*std::min(ca,cb);
    return std::max(a,b)+bridge+overlap*std::min(a,b);
}
DevelopedEvaluationPlan::DevelopedEvaluationPlan(DevelopedCloud cloud):cloud_(std::move(cloud)){
    canonical(cloud_);require(validate_developed_cloud(cloud_));
    if(cloud_.cells.empty())return;
    support_={{INFINITY,INFINITY,INFINITY},{-INFINITY,-INFINITY,-INFINITY}};
    for(const auto& cell:cloud_.cells){fields_.emplace_back(field_recipe(cloud_,cell));profiles_.emplace_back(fields_.back().recipe().altitude_density);const auto b=cloud_.cells.size()==1?fields_.back().local_support():primitive_support(fields_.back().recipe(),cloud_.fusion_width/4);grow(support_,b.min+cell.translation,b.max+cell.translation);}
    maximum_=fields_[0].maximum();if(fields_.size()==2)maximum_=std::max(maximum_,fields_[1].maximum())+cloud_.overlap*std::min(maximum_,fields_[1].maximum());
}
double DevelopedEvaluationPlan::at(Vec3 p)const{
    if(!finite(p))throw std::invalid_argument("Nonfinite developed density position");
    if(fields_.empty()||p.x<=support_.min.x||p.y<=support_.min.y||p.z<=support_.min.z||p.x>=support_.max.x||p.y>=support_.max.y||p.z>=support_.max.z)return 0;
    if(fields_.size()==1)return fields_[0].at(p-cloud_.cells[0].translation);
    const auto a=shape_sample(fields_[0],profiles_[0],p-cloud_.cells[0].translation),b=shape_sample(fields_[1],profiles_[1],p-cloud_.cells[1].translation);
    return std::clamp(developed_density_union(a.distance,a.coefficient,b.distance,b.coefficient,cloud_.fusion_width,cloud_.overlap),0.,maximum_);
}
Bounds DevelopedEvaluationPlan::world_support()const{
    Bounds b{{INFINITY,INFINITY,INFINITY},{-INFINITY,-INFINITY,-INFINITY}};
    for(unsigned i=0;i<8;++i){const Vec3 p{i&1?support_.max.x:support_.min.x,i&2?support_.max.y:support_.min.y,i&4?support_.max.z:support_.min.z};const auto q=local_to_world(cloud_.transform,p);grow(b,q,q);}return b;
}
GpuDevelopedParams DevelopedEvaluationPlan::gpu_params()const{
    GpuDevelopedParams out;for(std::size_t i=0;i<fields_.size();++i){out.groups[i]=gpu_density_params(fields_[i]);const auto p=cloud_.cells[i].translation;out.translations[i]={float(p.x),float(p.y),float(p.z),0};}
    out.envelope_min={float(support_.min.x),float(support_.min.y),float(support_.min.z),0};out.envelope_max={float(support_.max.x),float(support_.max.y),float(support_.max.z),0};
    out.settings={float(fields_.size()),float(cloud_.fusion_width),float(cloud_.overlap),float(maximum_)};return out;
}
CloudRecipe lower_single_developed_recipe(const DevelopedCloud& cloud){
    const DevelopedEvaluationPlan plan(cloud);if(cloud.cells.size()>1)throw std::invalid_argument("Independent developed profiles require grouped CPU/HLSL evaluation; legacy Recipe lowering cannot flatten them");
    CloudRecipe recipe;if(cloud.cells.empty()){recipe.cells.clear();recipe.envelope=plan.local_support();}else {
        recipe=plan.fields()[0].recipe();const auto p=plan.cloud().cells[0].translation;
        recipe.envelope={recipe.envelope.min+p,recipe.envelope.max+p};for(auto& c:recipe.cells)c.center=c.center+p;for(auto& c:recipe.cuts)c.center=c.center+p;
        recipe.base.height+=p.y;recipe.noise.origin=recipe.noise.origin+p;if(recipe.altitude_density.enabled)recipe.altitude_density.base+=p.y;
    }
    recipe.id=cloud.id;recipe.transform=cloud.transform;recipe.optics=cloud.optics;Scene scene;scene.cloud=recipe;require_valid(scene);return recipe;
}
std::vector<float> bake_developed_cloud(const DevelopedEvaluationPlan& plan,const GridLayout& grid){
    const std::uint64_t count=std::uint64_t(grid.extent[0])*grid.extent[1]*grid.extent[2];
    if(std::any_of(grid.extent.begin(),grid.extent.end(),[](auto n){return n<2||n>256;})||count>16777216)throw std::invalid_argument("Developed bake requires dimensions 2..256 and <=16777216 samples");
    (void)index_to_local(grid,{});std::vector<float> output(std::size_t(count),0);
    for(unsigned z=0;z<grid.extent[2];++z)for(unsigned y=0;y<grid.extent[1];++y)for(unsigned x=0;x<grid.extent[0];++x)output[(std::size_t(z)*grid.extent[1]+y)*grid.extent[0]+x]=float(plan.at(index_to_local(grid,{double(x),double(y),double(z)})));
    return output;
}
}
