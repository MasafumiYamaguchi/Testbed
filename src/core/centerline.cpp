#include "white/centerline.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <type_traits>

namespace white {
namespace {
bool finite(Vec3 p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
Vec3 cross(Vec3 a,Vec3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
double norm(Vec3 p){return std::sqrt(dot(p,p));}
void require(const std::vector<std::string>& errors){if(errors.empty())return;std::string message="Invalid centerline:";for(const auto& e:errors)message+="\n- "+e;throw std::invalid_argument(message);}
Vec3 slope(const std::vector<CenterlinePoint>& points,std::size_t i){
    const auto left=i?i-1:i,right=std::min(i+1,points.size()-1);
    return (points[right].offset-points[left].offset)*(1/(points[right].t-points[left].t));
}
std::size_t segment(const auto& points,double t){
    auto after=std::upper_bound(points.begin(),points.end(),t,[](double x,const auto& p){return x<p.t;});
    return after==points.end()?points.size()-2:std::size_t(after-points.begin()-1);
}
struct Hermite {Vec3 a,b,c,d;double start,span;};
Hermite coefficients(const std::vector<CenterlinePoint>& points,std::size_t i){
    const auto& p=points[i];const auto& q=points[i+1];const double h=q.t-p.t;
    const auto u=slope(points,i)*h,v=slope(points,i+1)*h;
    return {p.offset*2-q.offset*2+u+v,q.offset*3-p.offset*3-u*2-v,u,p.offset,p.t,h};
}
std::pair<Vec3,Vec3> offset_and_derivative(const CenterlineShape& shape,double t){
    const auto h=coefficients(shape.points,segment(shape.points,t));const double u=(t-h.start)/h.span;
    return {((h.a*u+h.b)*u+h.c)*u+h.d,(h.a*(3*u*u)+h.b*(2*u)+h.c)*(1/h.span)};
}
std::pair<double,double> profile_at(const CenterlineShape& shape,double t){
    const auto i=segment(shape.profile,t);const auto& a=shape.profile[i];const auto& b=shape.profile[i+1];
    const double u=(t-a.t)/(b.t-a.t);
    return {a.radius_scale+(b.radius_scale-a.radius_scale)*u,a.density_scale+(b.density_scale-a.density_scale)*u};
}
Vec3 axis_position(const CumulonimbusParameters& p,double t){
    const double y=p.height*t;return {y*p.growth_direction.x/p.growth_direction.y,p.cloud_base+y,y*p.growth_direction.z/p.growth_direction.y};
}
double curvature_unchecked(const CenterlineShape& shape){
    double bound=0;for(std::size_t i=0;i+1<shape.points.size();++i){
        const auto h=coefficients(shape.points,i);const double scale=1/(h.span*h.span*shape.source.parameters.height*shape.source.parameters.height);
        bound=std::max({bound,norm(h.b*2)*scale,norm(h.a*6+h.b*2)*scale});
    }return bound;
}
std::vector<std::string> source_errors(const CenterlineShape& shape){
    auto errors=validate_cumulonimbus(shape.source);
    auto check=[&](bool ok,const char* text){if(!ok)errors.emplace_back(text);};
    check(shape.contract_version==centerline_contract_version,"Unsupported centerline contract version");
    check(shape.points.size()>=2&&shape.points.size()<=max_centerline_points,"Centerline requires 2..6 control points");
    check(shape.profile.size()>=2&&shape.profile.size()<=max_centerline_profile_points,"Centerline requires 2..8 profile points");
    auto check_knots=[&](const auto& points,std::size_t limit){
        if(points.size()<2||points.size()>limit)return;
        check(points.front().t==0&&points.back().t==1,"Curve/profile endpoints must remain t=0 and t=1");
        std::set<Id> ids;
        for(std::size_t i=0;i<points.size();++i){
            check(points[i].id!=0&&ids.insert(points[i].id).second,"Curve/profile IDs must be nonzero and unique in their namespace");
            check(std::isfinite(points[i].t)&&points[i].t>=0&&points[i].t<=1,"Curve/profile t must be finite in [0,1]");
            if(i)check(points[i].t-points[i-1].t>=0.001,"Curve/profile points require ordered height separation >=0.001");
        }
    };
    check_knots(shape.points,max_centerline_points);check_knots(shape.profile,max_centerline_profile_points);
    for(std::size_t i=0;i<std::min(shape.points.size(),max_centerline_points);++i){const auto p=shape.points[i].offset;
        check(finite(p)&&p.y==0&&std::max(std::abs(p.x),std::abs(p.z))<=10000,"Control offsets must be finite local XZ metres within 10000; Y must be zero");}
    for(std::size_t i=0;i<std::min(shape.profile.size(),max_centerline_profile_points);++i){const auto& p=shape.profile[i];
        check(std::isfinite(p.radius_scale)&&p.radius_scale>=0.125&&p.radius_scale<=4,"Radius profile must lie in [0.125,4]");
        check(std::isfinite(p.density_scale)&&p.density_scale>=0&&p.density_scale<=1,"Density profile must lie in [0,1]");}
    if(errors.empty())check(curvature_unchecked(shape)<=max_centerline_curvature,"Curve exceeds conservative curvature bound 0.1 per local metre");
    return errors;
}
CloudRecipe geometry_unchecked(const CenterlineShape& shape){
    auto unadjusted=shape.source;unadjusted.cell_adjustments.clear();
    auto recipe=derive_cumulonimbus_recipe(unadjusted);
    constexpr std::array<double,5> roles{.18,.42,.70,.72,.76};
    for(std::size_t i=0;i<roles.size();++i){
        auto& cell=recipe.cells[i];cell.center=cell.center+offset_and_derivative(shape,roles[i]).first;
        const double radius=profile_at(shape,roles[i]).first;cell.radii=cell.radii*radius;
        const auto edit=std::find_if(shape.source.cell_adjustments.begin(),shape.source.cell_adjustments.end(),[&](const auto& a){return a.cell_id==cell.id;});
        if(edit!=shape.source.cell_adjustments.end()){
            cell.center=cell.center+edit->center_offset;
            cell.radii={cell.radii.x*edit->radius_scale.x,cell.radii.y*edit->radius_scale.y,cell.radii.z*edit->radius_scale.z};
            if(edit->structure_seed)cell.structure_seed=*edit->structure_seed;
        }
    }
    // Same support argument as the prefab: implicit smooth union expands each
    // ellipsoid by at most (N-1)k/4, plus bounded warp and the kernel edge band.
    // Per-axis expansion preserves anisotropic ellipsoids conservatively.
    const double margin=(recipe.cells.size()-1)*recipe.blend_width/4+recipe.noise.warp_amplitude+2;
    Bounds bounds{{INFINITY,INFINITY,INFINITY},{-INFINITY,-INFINITY,-INFINITY}};
    for(const auto& c:recipe.cells){const auto radius=c.radii*(1+margin/std::min({c.radii.x,c.radii.y,c.radii.z}));const auto lo=c.center-radius,hi=c.center+radius;
        bounds.min={std::min(bounds.min.x,lo.x),std::min(bounds.min.y,lo.y),std::min(bounds.min.z,lo.z)};
        bounds.max={std::max(bounds.max.x,hi.x),std::max(bounds.max.y,hi.y),std::max(bounds.max.z,hi.z)};}
    recipe.envelope=bounds;return recipe;
}
AltitudeDensityProfile density_profile_unchecked(const CenterlineShape& shape){
    AltitudeDensityProfile profile;
    if(std::any_of(shape.profile.begin(),shape.profile.end(),[](const auto& p){return p.density_scale!=1;})){
        profile.enabled=true;profile.base=shape.source.parameters.cloud_base;profile.height=shape.source.parameters.height;profile.knots.clear();
        for(const auto& p:shape.profile)profile.knots.push_back({p.t,p.density_scale});
    }
    return profile;
}
void history_push(std::vector<CenterlineShape>& stack,const CenterlineShape& value){if(stack.size()==128)stack.erase(stack.begin());stack.push_back(value);}
}
std::vector<std::string> validate_centerline(const CenterlineShape& shape){
    auto errors=source_errors(shape);if(!errors.empty())return errors;
    Scene scene;scene.cloud=geometry_unchecked(shape);scene.cloud.altitude_density=density_profile_unchecked(shape);
    for(const auto& e:validate(scene))errors.push_back("Derived centerline Recipe: "+e);
    return errors;
}
double centerline_curvature_bound(const CenterlineShape& shape){require(validate_centerline(shape));return curvature_unchecked(shape);}
CenterlineSample sample_centerline(const CenterlineShape& shape,double t){
    require(validate_centerline(shape));if(!std::isfinite(t)||t<0||t>1)throw std::invalid_argument("Centerline sample t must lie in [0,1]");
    const auto [offset,derivative]=offset_and_derivative(shape,t);const auto& p=shape.source.parameters;
    const Vec3 axis{p.height*p.growth_direction.x/p.growth_direction.y,p.height,p.height*p.growth_direction.z/p.growth_direction.y};
    const auto tangent=(axis+derivative)*(1/norm(axis+derivative));
    const auto raw_normal=cross(tangent,{0,0,1}),normal=raw_normal*(1/norm(raw_normal));
    const auto [radius,density]=profile_at(shape,t);
    return {axis_position(p,t)+offset,tangent,normal,cross(normal,tangent),radius,density};
}
CloudRecipe centerline_geometry_recipe(const CenterlineShape& shape){require(validate_centerline(shape));return geometry_unchecked(shape);}
bool centerline_recipe_compatible(const CenterlineShape& shape){require(validate_centerline(shape));return true;}
CloudRecipe lower_centerline_to_recipe(const CenterlineShape& shape){
    require(validate_centerline(shape));auto recipe=geometry_unchecked(shape);
    // All-unity profiles canonicalize to the legacy disabled/default value.
    // Adding extra radius-only knots therefore does not invalidate density by
    // inventing a second equivalent profile representation in the Recipe.
    recipe.altitude_density=density_profile_unchecked(shape);
    Scene scene;scene.cloud=recipe;require_valid(scene);return recipe;
}
FieldGraph lower_centerline_to_graph(const CenterlineShape& shape){return field_graph_from_recipe(lower_centerline_to_recipe(shape));}
CenterlineEvaluationPlan::CenterlineEvaluationPlan(CenterlineShape shape):shape_(std::move(shape)),geometry_(centerline_geometry_recipe(shape_)),density_(lower_centerline_to_recipe(shape_)){}
double CenterlineEvaluationPlan::at(Vec3 local)const{return density_.at(local);}
double CenterlineEvaluationPlan::maximum()const{return density_.maximum();}
GpuDensityParams CenterlineEvaluationPlan::gpu_params()const{return gpu_density_params(density_);}
std::vector<float> bake_centerline(const CenterlineEvaluationPlan& plan,const GridLayout& grid){
    const std::uint64_t count=std::uint64_t(grid.extent[0])*grid.extent[1]*grid.extent[2];
    if(std::any_of(grid.extent.begin(),grid.extent.end(),[](auto n){return n<2||n>256;})||count>16777216)throw std::invalid_argument("Centerline bake requires dimensions 2..256 and <=16777216 samples");
    (void)index_to_local(grid,{});std::vector<float> output(std::size_t(count),0);
    for(unsigned z=0;z<grid.extent[2];++z)for(unsigned y=0;y<grid.extent[1];++y)for(unsigned x=0;x<grid.extent[0];++x)
        output[(std::size_t(z)*grid.extent[1]+y)*grid.extent[0]+x]=float(plan.at(index_to_local(grid,{double(x),double(y),double(z)})));
    return output;
}
CenterlineDocument::CenterlineDocument(CenterlineShape shape):shape_(std::move(shape)){require(validate_centerline(shape_));}
CenterlineShape command_centerline(CenterlineShape shape,const CenterlineCommand& command){CenterlineDocument document(std::move(shape));document.apply(command);return document.shape();}
void CenterlineDocument::advance(){if(revision_==std::numeric_limits<std::uint64_t>::max())throw std::overflow_error("Centerline revision exhausted");++revision_;}
bool CenterlineDocument::replace(CenterlineShape shape){
    require(validate_centerline(shape));if(shape==shape_)return false;
    if(revision_==std::numeric_limits<std::uint64_t>::max())throw std::overflow_error("Centerline revision exhausted");
    if(!editing()){history_push(undo_,shape_);redo_.clear();}shape_=std::move(shape);advance();return true;
}
bool CenterlineDocument::apply(const CenterlineCommand& command){
    auto next=shape_;
    std::visit([&](const auto& value){using T=std::decay_t<decltype(value)>;
        if constexpr(std::is_same_v<T,CumulonimbusCommand>){CumulonimbusDocument source(next.source);source.apply(value);next.source=source.group();}
        else if constexpr(std::is_same_v<T,CenterlineSetProfile>){
            const auto p=std::find_if(next.profile.begin(),next.profile.end(),[&](const auto& p){return p.id==value.id;});
            if(p==next.profile.end())throw std::invalid_argument("Unknown centerline profile ID");
            p->radius_scale=value.radius_scale;p->density_scale=value.density_scale;
        }else if constexpr(std::is_same_v<T,CenterlineMovePoint>){
            const auto p=std::find_if(next.points.begin(),next.points.end(),[&](const auto& p){return p.id==value.id;});
            if(p==next.points.end())throw std::invalid_argument("Unknown centerline control ID");
            if(!finite(value.local_position))throw std::invalid_argument("Nonfinite centerline point position");
            const auto& parameters=next.source.parameters;
            if(p==next.points.begin()||p==next.points.end()-1){
                // The endpoint height is a constraint, not an inferred ratio.
                // Recomputing (base+height-base)/height can round away from 1
                // even for an unchanged requested plane at fractional bases.
                const double plane=parameters.cloud_base+parameters.height*p->t;
                if(value.local_position.y!=plane)throw std::invalid_argument("Centerline endpoint height is fixed; edit the height handle instead");
            }else p->t=(value.local_position.y-parameters.cloud_base)/parameters.height;
            const auto offset=value.local_position-axis_position(parameters,p->t);p->offset={offset.x,0,offset.z};
        }else if constexpr(std::is_same_v<T,CenterlineInsertPoint>){
            if(!std::isfinite(value.t)||value.t<=0||value.t>=1)throw std::invalid_argument("Inserted centerline point must be inside (0,1)");
            if(next.points.size()>=max_centerline_points)throw std::invalid_argument("Centerline control point limit exceeded");
            const auto offset=offset_and_derivative(next,value.t).first;
            const auto at=std::upper_bound(next.points.begin(),next.points.end(),value.t,[](double t,const auto& p){return t<p.t;});
            next.points.insert(at,{value.id,value.t,offset});
        }else{
            const auto p=std::find_if(next.points.begin(),next.points.end(),[&](const auto& p){return p.id==value.id;});
            if(p==next.points.end())throw std::invalid_argument("Unknown centerline control ID");
            if(p==next.points.begin()||p==next.points.end()-1)throw std::invalid_argument("Centerline endpoints cannot be removed");
            next.points.erase(p);
        }
    },command);return replace(std::move(next));
}
void CenterlineDocument::begin_edit(){if(editing())throw std::logic_error("Centerline edit already active");start_=shape_;}
void CenterlineDocument::end_edit(){if(!editing())throw std::logic_error("No centerline edit active");if(shape_!=*start_){history_push(undo_,*start_);redo_.clear();}start_.reset();}
void CenterlineDocument::cancel_edit(){if(!editing())throw std::logic_error("No centerline edit active");if(shape_!=*start_){advance();shape_=std::move(*start_);}start_.reset();}
bool CenterlineDocument::undo(){if(!can_undo())return false;if(revision_==std::numeric_limits<std::uint64_t>::max())throw std::overflow_error("Centerline revision exhausted");history_push(redo_,shape_);shape_=std::move(undo_.back());undo_.pop_back();advance();return true;}
bool CenterlineDocument::redo(){if(!can_redo())return false;if(revision_==std::numeric_limits<std::uint64_t>::max())throw std::overflow_error("Centerline revision exhausted");history_push(undo_,shape_);shape_=std::move(redo_.back());redo_.pop_back();advance();return true;}
}
