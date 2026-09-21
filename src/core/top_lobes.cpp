#include "white/top_lobes.hpp"
#include "white/noise.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>

namespace white {
namespace {
void require(const std::vector<std::string>& errors){if(errors.empty())return;std::string text="Invalid top-lobe source:";for(const auto& e:errors)text+="\n- "+e;throw std::invalid_argument(text);}
bool finite(Vec3 p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
bool range(double v,double lo,double hi){return std::isfinite(v)&&v>=lo&&v<=hi;}
const DevelopedCell& target(const TopLobeSource& s){const auto found=std::find_if(s.trunk.cells.begin(),s.trunk.cells.end(),[&](const auto& c){return c.id==s.target_cell;});if(found==s.trunk.cells.end())throw std::invalid_argument("Unknown top-lobe target developed Cell ID");return *found;}
std::uint64_t mix(std::uint64_t x){x+=UINT64_C(0x9e3779b97f4a7c15);x=(x^(x>>30))*UINT64_C(0xbf58476d1ce4e5b9);x=(x^(x>>27))*UINT64_C(0x94d049bb133111eb);return x^(x>>31);}
double random(std::uint64_t key,unsigned path,unsigned dimension){return (double(mix(key^mix(path)^mix(UINT64_C(0x10000)+dimension))>>12)+.5)*0x1p-52;}
Vec3 normalized(Vec3 p){return p*(1/std::sqrt(dot(p,p)));}
double smooth(double x){x=std::clamp(x,0.,1.);return x*x*(3-2*x);}
double coverage(double d){return 1-smooth((d+2)/2);}
double implicit(Vec3 p,Vec3 center,Vec3 r){p=p-center;p={p.x/r.x,p.y/r.y,p.z/r.z};return (std::sqrt(dot(p,p))-1)*std::min({r.x,r.y,r.z});}
double merge(double a,double b,double k){if(k==0)return std::min(a,b);const double h=std::max(k-std::abs(a-b),0.)/k;return std::min(a,b)-h*h*k*.25;}
void grow(Bounds& b,Vec3 lo,Vec3 hi){b.min={std::min(b.min.x,lo.x),std::min(b.min.y,lo.y),std::min(b.min.z,lo.z)};b.max={std::max(b.max.x,hi.x),std::max(b.max.y,hi.y),std::max(b.max.z,hi.z)};}
Bounds support(const CloudRecipe& recipe,double extra=0){Bounds b{{INFINITY,INFINITY,INFINITY},{-INFINITY,-INFINITY,-INFINITY}};const double margin=(recipe.cells.size()-1)*recipe.blend_width/4+recipe.noise.warp_amplitude+2+extra;
    for(const auto& c:recipe.cells){const auto r=c.radii*(1+margin/std::min({c.radii.x,c.radii.y,c.radii.z}));grow(b,c.center-r,c.center+r);}return b;}
std::vector<TopLobeNode> generate(const TopLobeSource& s){
    if(s.settings.mode==TopLobeMode::off)return {};
    const auto& development=target(s);const auto& p=development.shape.source.parameters;const auto& settings=s.settings;
    const auto key=mix(s.target_cell)^mix(p.structure_seed);
    const double anchor_t=std::min(.98,settings.top_start+.22);
    const auto frame=sample_centerline(development.shape,anchor_t);const double radius=settings.parent_radius*(.9+.2*random(key,0,0));
    const auto center=frame.position+development.translation+settings.growth_direction*(radius*.15)+frame.normal*(radius*.12*(2*random(key,0,1)-1));
    Cell parent{s.lobe_ids[0],center,{radius*.95,radius*1.05,radius},mix(key^UINT64_C(0xa11ce))};
    std::vector<TopLobeNode> result{{parent.id,0,1,parent}};
    if(settings.mode==TopLobeMode::children)for(unsigned child=0;child<settings.child_limit;++child){
        const unsigned path=child+1;if(random(key,path,0)>=settings.hierarchy_density)continue;
        const double sign=child==0?-1.:1.;
        const auto direction=normalized(settings.growth_direction*.75+frame.normal*(sign*.8)+frame.binormal*(.3*(2*random(key,path,1)-1)));
        const double child_radius=radius*settings.child_radius_ratio*(.9+.2*random(key,path,2));
        // Children attach to the parent, not to independent random world points.
        // This separation guarantees substantial overlap at every radius ratio.
        const auto child_center=parent.center+direction*(radius*.72);
        Cell primitive{s.lobe_ids[path],child_center,{child_radius,child_radius*(1+.12*random(key,path,3)),child_radius*.95},mix(key^mix(path))};
        result.push_back({primitive.id,parent.id,2,primitive});
    }
    return result;
}
CloudRecipe top_recipe(const TopLobeSource& s,const std::vector<TopLobeNode>& nodes){
    const auto& development=target(s);auto recipe=lower_centerline_to_recipe(development.shape);const auto offset=development.translation;
    recipe.id=s.field_id;recipe.transform={};recipe.cells.clear();for(const auto& node:nodes)recipe.cells.push_back(node.primitive);
    recipe.density*=s.settings.density_scale;recipe.blend_width=s.settings.fusion_width;recipe.overlap=0;
    recipe.base={true,development.shape.source.parameters.cloud_base+development.shape.source.parameters.height*s.settings.top_start+offset.y,s.settings.mask_transition};
    for(auto& cut:recipe.cuts)cut.center=cut.center+offset;
    recipe.noise.origin=recipe.noise.origin+offset;
    if(recipe.altitude_density.enabled)recipe.altitude_density.base+=offset.y;
    recipe.envelope=support(recipe);return recipe;
}
struct Shape {double distance=0,coefficient=0;};
Shape sample_shape(const DensityField& field,const AltitudeDensityEvaluator& profile,Vec3 p){
    const auto& r=field.recipe();Shape result;if(r.cells.empty()||r.density==0||(r.base.enabled&&p.y<=r.base.height))return result;
    for(const auto& cut:r.cuts)if(implicit(p,cut.center,cut.radii)<=0)return result;
    double distance=0,sum=0;for(std::size_t i=0;i<r.cells.size();++i){const auto& c=r.cells[i];auto q=p;
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
}
TopLobeSource make_top_lobe_source(const DevelopedCloud& cloud,Id id){
    TopLobeSource out;out.trunk=cloud;out.target_cell=id;(void)target(out);Id next=next_developed_id(cloud);
    if(next>std::numeric_limits<Id>::max()-4)throw std::overflow_error("Top-lobe source needs four unused stable IDs");
    out.field_id=next++;for(auto& lobe:out.lobe_ids)lobe=next++;require(validate_top_lobes(out));return out;
}
TopLobeSource command_top_lobes(TopLobeSource source,const TopLobeCommand& command){
    require(validate_top_lobes(source));
    if(const auto* settings=std::get_if<TopLobeSettingsCommand>(&command))source.settings=settings->settings;
    else source.trunk=command_developed_cloud(source.trunk,std::get<TopLobeTrunkCommand>(command).command);
    require(validate_top_lobes(source));return source;
}
std::vector<std::string> validate_top_lobes(const TopLobeSource& s){
    auto errors=validate_developed_cloud(s.trunk);auto check=[&](bool okay,const char* text){if(!okay)errors.emplace_back(text);};const auto& p=s.settings;
    check(s.contract_version==top_lobe_contract_version,"Unsupported top-lobe source contract");
    check(p.mode==TopLobeMode::off||p.mode==TopLobeMode::parent||p.mode==TopLobeMode::children,"Unknown top-lobe mode");
    check(p.depth_limit>=1&&p.depth_limit<=max_top_lobe_depth,"Top-lobe depth limit must be 1..2; deeper recursion is unsupported");
    check(p.mode!=TopLobeMode::children||p.depth_limit==2,"Children mode needs depth limit two");
    check(p.child_limit>=1&&p.child_limit<=2,"Top-lobe child limit must be one or two");
    check(range(p.top_start,.5,.9),"Top mask must start within normalized height 0.5..0.9");
    check(range(p.parent_radius,.5,500)&&range(p.child_radius_ratio,.2,.8),"Lobe radii must be nondegenerate: parent 0.5..500 m, child ratio 0.2..0.8");
    check(range(p.hierarchy_density,0,1)&&range(p.density_scale,0,1),"Hierarchy density and material density scale must be 0..1");
    check(range(p.mask_transition,.1,1000)&&range(p.fusion_width,0,1000),"Mask transition must be .1..1000 m and fusion 0..1000 m");
    check(finite(p.growth_direction)&&std::abs(dot(p.growth_direction,p.growth_direction)-1)<1e-9&&p.growth_direction.y>=.2,"Top growth direction must be a unit upward vector with y >= .2");
    const auto found=std::find_if(s.trunk.cells.begin(),s.trunk.cells.end(),[&](const auto& c){return c.id==s.target_cell;});check(found!=s.trunk.cells.end(),"Unknown top-lobe target developed Cell ID");
    std::set<Id> ids{s.trunk.id};for(const auto& c:s.trunk.cells){ids.insert(c.id);for(auto id:c.shape.source.cell_ids)ids.insert(id);for(const auto& cut:c.shape.source.modifiers.cuts)ids.insert(cut.id);for(const auto& manual:c.shape.source.modifiers.manual_cells)ids.insert(manual.id);}
    check(s.field_id!=0&&ids.insert(s.field_id).second,"Top field ID must be globally unique");for(auto id:s.lobe_ids)check(id!=0&&ids.insert(id).second,"Reserved lobe IDs must be nonzero and globally unique");
    if(p.mode!=TopLobeMode::off){check(s.trunk.cells.size()==1,"Enabled top lobes require one trunk development in the current two-group evaluator");const std::size_t budget=p.mode==TopLobeMode::parent?1:1+p.child_limit;check(developed_primitive_count(s.trunk)+budget<=8,"Top-lobe worst-case generation exceeds the eight-primitive budget");}
    if(errors.empty()&&p.mode!=TopLobeMode::off){Scene scene;scene.cloud=top_recipe(s,generate(s));for(const auto& error:validate(scene))errors.push_back(error);
        // Smoothstep's maximum derivative is 1.5. The existing profile bound
        // accounts for object-coordinate packing/subtraction/normalization.
        const AltitudeDensityProfile mask{true,scene.cloud.base.height,p.mask_transition,{{0,0},{1,1}}};
        for(const auto& error:validate_altitude_density(mask))errors.push_back("Top mask: "+error);
        check(1.5*altitude_density_error_bound(mask)<=max_altitude_density_error,"Top mask exceeds the 0.0001 float modulation budget; widen its transition or reduce local altitude");
        if(errors.empty()&&DensityField(scene.cloud).maximum()>0){
            const DevelopedEvaluationPlan trunk(s.trunk);const auto offset=trunk.cloud().cells[0].translation;
            const auto a=support(trunk.fields()[0].recipe(),p.fusion_width/4),b=support(scene.cloud,p.fusion_width/4);
            scene.cloud.envelope={a.min+offset,a.max+offset};grow(scene.cloud.envelope,b.min,b.max);
            for(const auto& error:validate(scene))errors.push_back("Combined top-lobe support: "+error);
        }
    }
    return errors;
}
std::vector<TopLobeNode> generate_top_lobes(const TopLobeSource& s){require(validate_top_lobes(s));return generate(s);}
double top_lobe_mask_height(const TopLobeSource& s){require(validate_top_lobes(s));const auto& c=target(s);return c.shape.source.parameters.cloud_base+c.shape.source.parameters.height*s.settings.top_start+c.translation.y;}
TopLobeEvaluationPlan::TopLobeEvaluationPlan(TopLobeSource source):source_(std::move(source)),trunk_(source_.trunk){
    require(validate_top_lobes(source_));hierarchy_=generate(source_);support_=trunk_.local_support();maximum_=trunk_.maximum();mask_height_=top_lobe_mask_height(source_);
    if(source_.settings.mode==TopLobeMode::off)return;
    top_.emplace(top_recipe(source_,hierarchy_));
    if(top_->maximum()==0)return;
    profiles_.emplace_back(trunk_.fields()[0].recipe().altitude_density);profiles_.emplace_back(top_->recipe().altitude_density);
    const auto& translation=trunk_.cloud().cells[0].translation;const auto a=support(trunk_.fields()[0].recipe(),source_.settings.fusion_width/4),b=support(top_->recipe(),source_.settings.fusion_width/4);
    support_={a.min+translation,a.max+translation};grow(support_,b.min,b.max);maximum_=std::max(maximum_,top_->maximum());
}
double TopLobeEvaluationPlan::at(Vec3 p)const{
    if(!finite(p))throw std::invalid_argument("Nonfinite top-lobe field position");
    // Preserve the fixed lower region bit-for-bit, including multiplication
    // order. Do not merely rely on a zero coefficient in the grouped formula.
    if(!top_||top_->maximum()==0||p.y<=mask_height_)return trunk_.at(p);
    if(p.x<=support_.min.x||p.y<=support_.min.y||p.z<=support_.min.z||p.x>=support_.max.x||p.y>=support_.max.y||p.z>=support_.max.z)return 0;
    const auto a=sample_shape(trunk_.fields()[0],profiles_[0],p-trunk_.cloud().cells[0].translation),b=sample_shape(*top_,profiles_[1],p);
    return std::clamp(developed_density_union(a.distance,a.coefficient,b.distance,b.coefficient,source_.settings.fusion_width,0),0.,maximum_);
}
Bounds TopLobeEvaluationPlan::world_support()const{Bounds b{{INFINITY,INFINITY,INFINITY},{-INFINITY,-INFINITY,-INFINITY}};for(unsigned i=0;i<8;++i){const auto q=local_to_world(source_.trunk.transform,{i&1?support_.max.x:support_.min.x,i&2?support_.max.y:support_.min.y,i&4?support_.max.z:support_.min.z});grow(b,q,q);}return b;}
GpuTopLobeParams TopLobeEvaluationPlan::gpu_params()const{
    const bool active=top_&&top_->maximum()>0;
    GpuTopLobeParams out;out.fields=trunk_.gpu_params();out.mask={active?1.f:0.f,float(mask_height_),float(source_.settings.mode),float(hierarchy_.size())};
    if(active){out.fields.groups[1]=gpu_density_params(*top_);out.fields.translations[1]={};out.fields.envelope_min={float(support_.min.x),float(support_.min.y),float(support_.min.z),0};out.fields.envelope_max={float(support_.max.x),float(support_.max.y),float(support_.max.z),0};out.fields.settings={2,float(source_.settings.fusion_width),0,float(maximum_)};}return out;
}
std::vector<float> bake_top_lobes(const TopLobeEvaluationPlan& plan,const GridLayout& grid){const std::uint64_t count=std::uint64_t(grid.extent[0])*grid.extent[1]*grid.extent[2];if(std::any_of(grid.extent.begin(),grid.extent.end(),[](auto n){return n<2||n>256;})||count>16777216)throw std::invalid_argument("Top-lobe bake requires dimensions 2..256 and <=16777216 samples");(void)index_to_local(grid,{});std::vector<float> out(std::size_t(count),0);for(unsigned z=0;z<grid.extent[2];++z)for(unsigned y=0;y<grid.extent[1];++y)for(unsigned x=0;x<grid.extent[0];++x)out[(std::size_t(z)*grid.extent[1]+y)*grid.extent[0]+x]=float(plan.at(index_to_local(grid,{double(x),double(y),double(z)})));return out;}
}
