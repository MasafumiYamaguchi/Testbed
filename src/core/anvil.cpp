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
double float_ulp(double magnitude){
    const float value=float(std::abs(magnitude));
    if(!std::isfinite(value))return INFINITY;
    return std::max(double(std::nextafter(value,INFINITY))-value,double(value)-std::nextafter(value,-INFINITY));
}
double packed_error(double value){return std::abs(double(float(value))-value);}
double product_error(double a,double b,double ea,double eb){
    return a*eb+b*ea+ea*eb+.5*float_ulp((a+ea)*(b+eb));
}
double edge_error(const AnvilSettings& s,Vec3 center,const Bounds& support,const NoiseSettings& noise,Vec3 translation){
    if(!s.enabled||s.density_scale==0)return 0;
    const double along=(s.width+s.extension)*.5,cross=s.width*.5,half=s.thickness*.5;
    const double dx=std::abs(s.direction.x),dz=std::abs(s.direction.z),shear=std::abs(s.shear);
    const double edx=packed_error(s.direction.x),edz=packed_error(s.direction.z),eshear=packed_error(s.shear);
    const double qx=dx*along+dz*cross,qz=dz*along+dx*cross;
    const auto position_error=[&](double lo,double hi){return 4*float_ulp(std::max(std::abs(lo),std::abs(hi)));};
    const double px=position_error(support.min.x,support.max.x),py=position_error(support.min.y,support.max.y),pz=position_error(support.min.z,support.max.z);
    const auto subtract_error=[](double position,double origin,double extent){const double error=position+packed_error(origin);return error+.5*float_ulp(extent+error);};
    double ex=subtract_error(px,center.x,qx+dx*shear*half),ey=subtract_error(py,center.y,half),ez=subtract_error(pz,center.z,qz+dz*shear*half);
    const double shear_error=product_error(shear,half,eshear,ey);
    const double sx=product_error(dx,shear*half,edx,shear_error),sz=product_error(dz,shear*half,edz,shear_error);
    ex+=sx+.5*float_ulp(qx+ex+sx);ez+=sz+.5*float_ulp(qz+ez+sz);
    const auto dot_error=[&](double ax,double az,double eax,double eaz,double radius){
        const double error=product_error(qx,ax,ex,eax)+product_error(qz,az,ez,eaz);
        return error+.5*float_ulp(radius+error);
    };
    const auto division_error=[](double error,double radius){
        const double quotient=(error+packed_error(radius))/double(float(radius));
        return quotient+.5*float_ulp(1+quotient);
    };
    const double eu=division_error(dot_error(dx,dz,edx,edz,along),along),ev=division_error(dot_error(dz,dx,edz,edx,cross),cross),ew=division_error(ey,half);
    // Euclidean norm is 1-Lipschitz. Four ULPs allow its multiply/add/sqrt
    // arithmetic independently of the coordinate/normalization perturbation.
    const double norm_error=std::hypot(eu,ev,ew)+4*float_ulp(1+std::hypot(eu,ev,ew));
    const double difference_error=norm_error+.5*float_ulp(1+norm_error);
    double distance_error=product_error(1.,half,difference_error,packed_error(half));
    if(noise.micro_erosion>0){
        const auto coordinate_error=[&](double lo,double hi,double position,double translation,double origin){
            double extent=std::max(std::abs(lo-translation),std::abs(hi-translation));
            double error=position+packed_error(translation);error+=.5*float_ulp(extent+error);
            extent=std::max(std::abs(lo-translation-origin),std::abs(hi-translation-origin));
            error+=packed_error(origin);error+=.5*float_ulp(extent+error);
            return product_error(extent,noise.micro_frequency,error,packed_error(noise.micro_frequency));
        };
        const double coordinate=coordinate_error(support.min.x,support.max.x,px,translation.x,noise.origin.x)+coordinate_error(support.min.y,support.max.y,py,translation.y,noise.origin.y)+coordinate_error(support.min.z,support.max.z,pz,translation.z,noise.origin.z);
        // Quintic value noise has per-axis derivative <= 1.875. The fixed
        // 2:1 octaves raise this to 2.5; 128 epsilons cover interpolation and
        // polynomial arithmetic on [0,1], including lattice-value packing.
        const double noise_error=std::min(1.,2.5*coordinate+128*std::numeric_limits<float>::epsilon());
        distance_error+=product_error(noise.micro_erosion,1.,packed_error(noise.micro_erosion),noise_error);
        distance_error+=.5*float_ulp(half+noise.micro_erosion+distance_error);
    }
    const double fade_error=packed_error(s.edge_fade);
    const double normalize_error=(distance_error+fade_error+.5*float_ulp(s.edge_fade+distance_error+fade_error))/double(float(s.edge_fade));
    // Smoothstep's maximum derivative is 1.5. Extra scalar arithmetic is
    // bounded separately; this allowance is never applied to legacy fields.
    return std::min(1.,1.5*(normalize_error+.5*float_ulp(1+normalize_error))+16*std::numeric_limits<float>::epsilon());
}
}
double anvil_edge_error_bound(const AnvilSettings& s,Vec3 center,Bounds support,const NoiseSettings& noise,Vec3 translation){
    try{
        if(!s.enabled||s.density_scale==0)return 0;
        if(!range(s.thickness,1,2000)||!range(s.width,8,10000)||s.thickness>s.width*.5||
           !range(s.extension,0,40000)||s.extension>s.width*4||!range(s.edge_fade,.01,250)||s.edge_fade>s.thickness*.125||
           !finite(s.direction)||s.direction.y!=0||std::abs(dot(s.direction,s.direction)-1)>=1e-9||!range(s.shear,-4,4)||
           !finite(center)||!finite(support.min)||!finite(support.max)||support.min.x>=support.max.x||support.min.y>=support.max.y||support.min.z>=support.max.z||
           !finite(translation)||!finite(noise.origin)||!range(noise.micro_frequency,.0001,2)||!range(noise.micro_erosion,0,20))return INFINITY;
        return edge_error(s,center,support,noise,translation);
    }catch(const std::exception&){return INFINITY;}
}
double anvil_edge_error_bound(const AnvilSource& source){
    try{
        const auto& s=source.settings;if(!s.enabled||s.density_scale==0)return 0;
        const auto& cell=target(source);const auto center=anchor(source)+s.direction*(s.extension*.5);
        auto support=TopLobeEvaluationPlan(source.cloud).local_support();const auto sheet=anvil_bounds(source);grow(support,sheet.min,sheet.max);
        return anvil_edge_error_bound(s,center,support,cell.shape.source.modifiers.noise,cell.translation);
    }catch(const std::exception&){return INFINITY;}
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
            check(anvil_edge_error_bound(source)<=max_anvil_edge_error,"Anvil edge arithmetic exceeds the 0.002 coverage error budget; widen fade or reduce coordinate/erosion");
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
