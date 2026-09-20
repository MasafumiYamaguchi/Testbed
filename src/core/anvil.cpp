#include "white/anvil.hpp"
#include "white/noise.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace white {
namespace {
bool finite(Vec3 p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
bool range(double v,double lo,double hi){return std::isfinite(v)&&v>=lo&&v<=hi;}
void require(const std::vector<std::string>& errors){if(errors.empty())return;std::string message="Invalid anvil source:";for(const auto& e:errors)message+="\n- "+e;throw std::invalid_argument(message);}
double smooth(double x){x=std::clamp(x,0.,1.);return x*x*(3-2*x);}
double implicit(Vec3 p,Vec3 c,Vec3 r){p=p-c;p={p.x/r.x,p.y/r.y,p.z/r.z};return (std::sqrt(dot(p,p))-1)*std::min({r.x,r.y,r.z});}
void grow(Bounds& b,Vec3 lo,Vec3 hi){b.min={std::min(b.min.x,lo.x),std::min(b.min.y,lo.y),std::min(b.min.z,lo.z)};b.max={std::max(b.max.x,hi.x),std::max(b.max.y,hi.y),std::max(b.max.z,hi.z)};}
const DevelopedCell& target(const AnvilSource& source){const auto& cells=source.cloud.trunk.cells;const auto found=std::find_if(cells.begin(),cells.end(),[&](const auto& c){return c.id==source.cloud.target_cell;});if(found==cells.end())throw std::invalid_argument("Unknown anvil target");return *found;}
Vec3 anchor(const AnvilSource& source){const auto& c=target(source);const auto& p=c.shape.source.parameters;const double t=(source.settings.start_height+source.settings.thickness*.5-p.cloud_base)/p.height;return sample_centerline(c.shape,t).position+c.translation;}
Bounds anvil_bounds(const AnvilSource& source){
    const auto& p=source.settings;const auto a=anchor(source);const Vec3 side{-p.direction.z,0,p.direction.x};
    const auto center=a+p.direction*(p.extension*.5);
    const double along=(p.width+p.extension)*.5+std::abs(p.shear)*p.thickness*.5,cross=p.width*.5;
    const Vec3 extent{std::abs(p.direction.x)*along+std::abs(side.x)*cross,p.thickness*.5,std::abs(p.direction.z)*along+std::abs(side.z)*cross};
    return {center-extent,center+extent};
}
}
Vec3 anvil_connection_point(const AnvilSource& source){return anchor(source);}
AnvilSource make_anvil_source(const TopLobeSource& cloud){
    AnvilSource out;out.cloud=cloud;const auto& p=target(out).shape.source.parameters;
    out.settings.start_height=p.cloud_base+p.height*.7;out.settings.thickness=std::max(1.,std::min(24.,p.height*.2));
    out.settings.width=std::max(2*out.settings.thickness,p.width*1.25);out.settings.extension=out.settings.width*.8;
    out.settings.edge_fade=std::min(1.,out.settings.thickness*.125);require(validate_anvil(out));return out;
}
std::vector<std::string> validate_anvil(const AnvilSource& source){
    auto errors=validate_top_lobes(source.cloud);auto check=[&](bool okay,const char* text){if(!okay)errors.emplace_back(text);};const auto& s=source.settings;
    check(source.contract_version==anvil_contract_version,"Unsupported anvil source contract");
    check(range(s.thickness,1,2000)&&range(s.width,8,10000)&&s.thickness<=s.width*.5,"Anvil needs thickness >=1 m and width >= twice thickness");
    check(range(s.extension,0,40000)&&s.extension<=s.width*4,"Forward extension must be 0..4 times width to retain a substantial neck");
    check(finite(s.direction)&&s.direction.y==0&&std::abs(dot(s.direction,s.direction)-1)<1e-9,"Anvil direction must be a unit horizontal vector");
    check(range(s.shear,-4,4)&&range(s.density_scale,0,1),"Shear must be -4..4 and density scale 0..1");
    check(range(s.edge_fade,.01,250)&&s.edge_fade<=s.thickness*.125,"Edge fade must be .01 m through one eighth of thickness");
    const auto& cells=source.cloud.trunk.cells;const auto found=std::find_if(cells.begin(),cells.end(),[&](const auto& c){return c.id==source.cloud.target_cell;});
    if(found!=cells.end()){const auto& p=found->shape.source.parameters;
        check(range(s.start_height,p.cloud_base+p.height*.5,p.cloud_base+p.height*.9),"Start altitude must be in the upper half through 90% of development height");
        check(s.start_height+s.thickness<=p.cloud_base+p.height+1e-10,"Insufficient remaining height for requested anvil thickness");
        if(errors.empty()&&s.enabled){
            // The shader packs/subtracts object-local positions on every axis.
            // A distant X/Z attachment can lose the narrow edge just as a high
            // altitude can. Include both coordinate operands, even when local
            // offsets and the development translation nearly cancel out.
            // World transform translation is not part of this local field.
            const auto bounds=anvil_bounds(source);const auto local_anchor=anchor(source)-found->translation;
            const auto magnitude=[](Vec3 v){return std::max({std::abs(v.x),std::abs(v.y),std::abs(v.z)});};
            const double scale=std::max({1.,magnitude(bounds.min),magnitude(bounds.max),magnitude(local_anchor),magnitude(found->translation),s.width,s.extension});
            check(scale*std::numeric_limits<float>::epsilon()*8/s.edge_fade<=.002,"Anvil edge exceeds GPU precision budget; widen fade or reduce coordinate/extent");
        }
    }
    if(s.enabled)check(cells.size()==1,"Enabled anvil currently requires exactly one developed trunk");
    if(errors.empty()&&s.enabled&&s.density_scale>0){
        const TopLobeEvaluationPlan cloud(source.cloud);auto bounds=cloud.local_support();const auto top=anvil_bounds(source);grow(bounds,top.min,top.max);
        Scene checked;checked.cloud.cells.clear();checked.cloud.envelope=bounds;for(const auto& error:validate(checked))errors.push_back(error);
        if(cloud.maximum()>0)check(cloud.at(anchor(source))>0,"Anvil connection point is empty or cut away; change altitude or that cut");
    }
    return errors;
}
AnvilEvaluationPlan::AnvilEvaluationPlan(AnvilSource source):source_(std::move(source)),cloud_(source_.cloud){
    require(validate_anvil(source_));support_=cloud_.local_support();maximum_=cloud_.maximum();anchor_=anchor(source_);
    if(!source_.settings.enabled||source_.settings.density_scale==0||maximum_==0)return;
    const auto& c=target(source_);field_.emplace(lower_centerline_to_recipe(c.shape));profile_.emplace(field_->recipe().altitude_density);translation_=c.translation;
    boundary_=source_.settings.start_height+c.translation.y;anchor_=anchor(source_);
    center_=anchor_+source_.settings.direction*(source_.settings.extension*.5);side_={-source_.settings.direction.z,0,source_.settings.direction.x};
    const auto bounds=anvil_bounds(source_);grow(support_,bounds.min,bounds.max);
    maximum_=std::max(maximum_,field_->recipe().density*source_.settings.density_scale*profile_->maximum());
}
double AnvilEvaluationPlan::at(Vec3 p)const{
    if(!finite(p))throw std::invalid_argument("Nonfinite anvil field position");
    const double original=cloud_.at(p);
    if(!field_||p.y<=boundary_)return original;
    const auto& s=source_.settings;auto q=p-center_;q=q-s.direction*(s.shear*q.y);
    const double u=dot(q,s.direction)/((s.width+s.extension)*.5),v=dot(q,side_)/(s.width*.5),w=q.y/(s.thickness*.5);
    double distance=(std::sqrt(u*u+v*v+w*w)-1)*s.thickness*.5;
    const auto& r=field_->recipe();const auto local=p-translation_,n=local-r.noise.origin;const auto seed=noise_seed(r.detail_seed);
    if(r.noise.micro_erosion>0)distance+=r.noise.micro_erosion*detail_noise(n*r.noise.micro_frequency,seed^0x6c8e9cf5u);
    double addition=r.density*s.density_scale*(1-smooth((distance+s.edge_fade)/s.edge_fade));
    if(addition==0)return original;
    if(r.noise.medium_strength>0)addition*=1-r.noise.medium_strength*detail_noise(n*r.noise.medium_frequency,seed);
    if(r.base.enabled){if(local.y<=r.base.height)return original;if(r.base.transition>0)addition*=smooth((local.y-r.base.height)/r.base.transition);}
    for(const auto& cut:r.cuts){const double d=implicit(local,cut.center,cut.radii);if(d<=0)return original;if(cut.transition>0)addition*=smooth(d/cut.transition);}
    addition*=profile_->at(local.y);
    return std::clamp(std::max(original,addition),0.,maximum_);
}
Bounds AnvilEvaluationPlan::world_support()const{Bounds out{{INFINITY,INFINITY,INFINITY},{-INFINITY,-INFINITY,-INFINITY}};for(unsigned i=0;i<8;++i){const auto p=local_to_world(source_.cloud.trunk.transform,{i&1?support_.max.x:support_.min.x,i&2?support_.max.y:support_.min.y,i&4?support_.max.z:support_.min.z});grow(out,p,p);}return out;}
GpuAnvilParams AnvilEvaluationPlan::gpu_params()const{
    GpuAnvilParams out;out.cloud=cloud_.gpu_params();const auto& s=source_.settings;
    out.center={float(center_.x),float(center_.y),float(center_.z),0};out.direction={float(s.direction.x),0,float(s.direction.z),float(s.shear)};
    out.dimensions={float((s.width+s.extension)*.5),float(s.width*.5),float(s.thickness*.5),float(s.edge_fade)};
    out.settings={field_?1.f:0.f,float(s.density_scale),float(boundary_),float(maximum_)};
    out.envelope_min={float(support_.min.x),float(support_.min.y),float(support_.min.z),0};out.envelope_max={float(support_.max.x),float(support_.max.y),float(support_.max.z),0};return out;
}
std::vector<float> bake_anvil(const AnvilEvaluationPlan& plan,const GridLayout& grid){
    const std::uint64_t count=std::uint64_t(grid.extent[0])*grid.extent[1]*grid.extent[2];
    if(std::any_of(grid.extent.begin(),grid.extent.end(),[](auto n){return n<2||n>256;})||count>16777216)throw std::invalid_argument("Anvil bake requires dimensions 2..256 and <=16777216 samples");
    (void)index_to_local(grid,{});std::vector<float> out(std::size_t(count),0);
    for(unsigned z=0;z<grid.extent[2];++z)for(unsigned y=0;y<grid.extent[1];++y)for(unsigned x=0;x<grid.extent[0];++x)out[(std::size_t(z)*grid.extent[1]+y)*grid.extent[0]+x]=float(plan.at(index_to_local(grid,{double(x),double(y),double(z)})));
    return out;
}
}
