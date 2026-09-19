#include "white/document.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace white;
int main() {
    int failed=0;
    auto check=[&](bool ok,const char* name){std::cout<<(ok?"PASS ":"FAIL ")<<name<<'\n';failed+=!ok;};
    auto near=[](Vec3 a,Vec3 b){auto d=a-b;return dot(d,d)<1e-16;};
    Scene s;check(validate(s).empty(),"single cell recipe");
    auto empty=s;empty.cloud.cells.clear();check(validate(empty).empty(),"empty recipe");
    auto multiple=s;multiple.cloud.cells.push_back({next_id(multiple),{5,20,0},{10,10,10},7});
    check(validate(multiple).empty(),"multiple cells");
    const auto key=cell_random_key(multiple.cloud,multiple.cloud.cells[0]);
    std::reverse(multiple.cloud.cells.begin(),multiple.cloud.cells.end());
    check(cell_random_key(multiple.cloud,multiple.cloud.cells[1])==key,"reordering keeps local random stream");
    multiple.cloud.detail_seed++;check(cell_random_key(multiple.cloud,multiple.cloud.cells[1])==key,"detail seed independent of structure");
    multiple.cloud.cells.erase(multiple.cloud.cells.begin());check(multiple.cloud.cells[0].id==2,"deletion keeps surviving ID");
    Transform t{{10,-7,40},{0,std::sqrt(0.5),0,std::sqrt(0.5)},{2,3,4}};
    check(near(local_to_world(t,{1,0,0}),{10,-7,38}),"known rotated and scaled point");
    check(near(world_to_local(t,local_to_world(t,{1.25,-9,7})),{1.25,-9,7}),"world/local round trip");
    GridLayout grid{{{-2,10,-9},{8,50,21}},{5,4,3}};
    check(near(index_to_local(grid,{0,0,0}),{-1,15,-4}),"known voxel center");
    check(near(local_to_index(grid,index_to_local(grid,{4,3,2})),{4,3,2}),"index/local round trip");
    check(near(local_to_index(grid,grid.local_bounds.min),{-0.5,-0.5,-0.5}),"lower boundary convention");
    auto bad=s;bad.cloud.cells[0].radii.x=-1;check(!validate(bad).empty(),"negative radius rejected");
    bad=s;bad.cloud.transform.scale.y=0;check(!validate(bad).empty(),"singular transform rejected");
    bad=s;bad.cloud.transform.rotation.w=2;check(!validate(bad).empty(),"non-unit rotation rejected");
    bad=s;bad.cloud.cells[0].center.x=std::numeric_limits<double>::quiet_NaN();check(!validate(bad).empty(),"NaN rejected");
    bad=s;bad.sun.irradiance.z=std::numeric_limits<double>::infinity();check(!validate(bad).empty(),"Inf rejected");
    bad=s;bad.cloud.optics.albedo=1.01;check(!validate(bad).empty(),"invalid albedo rejected");
    bad=s;bad.cloud.cells.resize(9);check(!validate(bad).empty(),"cell limit rejected");
    bad=s;bad.cloud.cuts.push_back({2});check(!validate(bad).empty(),"cross-type duplicate ID rejected");
    bad=s;bad.camera.target=bad.camera.position;check(!validate(bad).empty(),"degenerate camera rejected");
    bad=s;bad.schema_version=999;check(!validate(bad).empty(),"future schema rejected");
    auto c=s;c.camera.position.x++;check(classify_change(s,c)==Dirty::camera,"camera change only");
    c=s;c.exposure_ev++;check(classify_change(s,c)==Dirty::display,"exposure change only");
    c=s;c.sun.irradiance.x++;check(classify_change(s,c)==Dirty::sun,"sun change only");
    c=s;c.cloud.optics.extinction_scale*=2;check(classify_change(s,c)==Dirty::optics,"optics do not dirty density");
    c=s;c.cloud.cells[0].radii.x++;check(classify_change(s,c)==Dirty::density,"shape change dirties density");
    Document d;check(!d.replace(s)&&d.revision()==1,"no-op preserves revision");
    check(d.replace(c)&&d.revision()==2,"change increments revision");
    try {d.replace(bad);check(false,"invalid replacement rejected");} catch(const std::invalid_argument&) {check(d.scene()==c&&d.revision()==2,"invalid replacement preserves document");}
    s.cloud.id=std::numeric_limits<Id>::max();
    try {(void)next_id(s);check(false,"ID overflow rejected");}catch(const std::overflow_error&){check(true,"ID overflow rejected");}
    return failed?1:0;
}
