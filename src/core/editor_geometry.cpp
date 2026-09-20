#include "white/editor_geometry.hpp"
#include <cmath>
#include <limits>
namespace white {
namespace {
Vec3 cross(Vec3 a,Vec3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
Vec3 normalize(Vec3 p){return p*(1/std::sqrt(dot(p,p)));}
}
Ray camera_ray(const Camera& c,double x,double y,double aspect) {
    const auto forward=normalize(c.target-c.position),right=normalize(cross(forward,c.up)),up=cross(right,forward);
    const double t=std::tan(c.vertical_fov_degrees*3.141592653589793/360);
    return {c.position,normalize(forward+right*((2*x-1)*aspect*t)+up*((1-2*y)*t))};
}
Matrix4 camera_view(const Camera& c) {
    const auto f=normalize(c.target-c.position),r=normalize(cross(f,c.up)),u=cross(r,f);
    return {float(r.x),float(u.x),float(-f.x),0,float(r.y),float(u.y),float(-f.y),0,float(r.z),float(u.z),float(-f.z),0,
        float(-dot(r,c.position)),float(-dot(u,c.position)),float(dot(f,c.position)),1};
}
Matrix4 camera_projection(const Camera& c,double aspect) {
    const double f=1/std::tan(c.vertical_fov_degrees*3.141592653589793/360),n=c.near_plane,z=c.far_plane;
    return {float(f/aspect),0,0,0,0,float(f),0,0,0,0,float((z+n)/(n-z)),-1,0,0,float(2*z*n/(n-z)),0};
}
Matrix4 primitive_matrix(const Transform& t,Vec3 center,Vec3 radii) {
    const auto o=local_to_world(t,center),x=local_to_world(t,center+Vec3{radii.x,0,0})-o,y=local_to_world(t,center+Vec3{0,radii.y,0})-o,z=local_to_world(t,center+Vec3{0,0,radii.z})-o;
    return {float(x.x),float(x.y),float(x.z),0,float(y.x),float(y.y),float(y.z),0,float(z.x),float(z.y),float(z.z),0,float(o.x),float(o.y),float(o.z),1};
}
void primitive_from_matrix(const Transform& t,const Matrix4& m,Vec3& center,Vec3& radii) {
    center=world_to_local(t,{m[12],m[13],m[14]});
    const auto o=world_to_local(t,{0,0,0});
    const auto x=world_to_local(t,{m[0],m[1],m[2]})-o,y=world_to_local(t,{m[4],m[5],m[6]})-o,z=world_to_local(t,{m[8],m[9],m[10]})-o;
    radii={std::sqrt(dot(x,x)),std::sqrt(dot(y,y)),std::sqrt(dot(z,z))};
}
std::optional<Id> pick_primitive(const CloudRecipe& r,Ray ray,bool cuts) {
    const auto o=world_to_local(r.transform,ray.origin),d=world_to_local(r.transform,ray.origin+ray.direction)-o;
    std::optional<Id> selected;double closest=std::numeric_limits<double>::infinity();
    auto test=[&](Id id,Vec3 center,Vec3 radii) {
        Vec3 p=o-center;p={p.x/radii.x,p.y/radii.y,p.z/radii.z};Vec3 v{d.x/radii.x,d.y/radii.y,d.z/radii.z};
        const double a=dot(v,v),b=dot(p,v),c=dot(p,p)-1,discriminant=b*b-a*c;
        if(a<=0||discriminant<0)return;
        double t=(-b-std::sqrt(discriminant))/a;if(t<0)t=(-b+std::sqrt(discriminant))/a;
        if(t>=0&&t<closest){closest=t;selected=id;}
    };
    if(cuts)for(const auto& p:r.cuts)test(p.id,p.center,p.radii);
    else for(const auto& p:r.cells)test(p.id,p.center,p.radii);
    return selected;
}
}
