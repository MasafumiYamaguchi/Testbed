#include "white/editor_geometry.hpp"
#include <cmath>
#include <iostream>
using namespace white;
int main(){int failures=0;auto check=[&](bool ok,const char* label){std::cout<<(ok?"PASS ":"FAIL ")<<label<<'\n';failures+=!ok;};
    Camera c;c.position={0,20,100};c.target={0,20,0};auto ray=camera_ray(c,0.5,0.5,1.5);
    check(ray.direction==Vec3{0,0,-1},"screen center follows camera forward");
    CloudRecipe recipe;check(pick_primitive(recipe,ray,false)==2,"primitive picking hits cell");
    check(!pick_primitive(recipe,{{100,20,100},{0,0,-1}},false),"primitive miss");
    recipe.cuts.push_back({3,{0,20,0},{5,5,5},0});check(pick_primitive(recipe,ray,true)==3,"cut mode picks cut primitive");
    Transform t{{10,20,30},{0,std::sqrt(0.5),0,std::sqrt(0.5)},{2,3,4}};
    const Vec3 center{4,5,6},radii{7,8,9};auto matrix=primitive_matrix(t,center,radii);Vec3 a,b;primitive_from_matrix(t,matrix,a,b);
    check(dot(a-center,a-center)<1e-12&&dot(b-radii,b-radii)<1e-12,"gizmo matrix round-trip under cloud rotation/scale");
    matrix[12]+=4;primitive_from_matrix(t,matrix,a,b);check(std::abs(a.z-center.z-1)<1e-6,"world gizmo translation converts to cloud-local coordinate");
    recipe.transform=t;auto world=local_to_world(t,recipe.cells[0].center);auto origin=world+Vec3{0,0,100};
    check(pick_primitive(recipe,{origin,{0,0,-1}},false)==2,"transformed cell picking");
    return failures?1:0;
}
