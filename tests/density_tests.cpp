#include "white/density.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
using namespace white;
int main() {
    int failures=0;auto check=[&](bool ok,const char* label){std::cout<<(ok?"PASS ":"FAIL ")<<label<<'\n';failures+=!ok;};
    for(int fixture=0;fixture<5;++fixture) {
        DensityField field(density_fixture(fixture));bool valid=true;
        GridLayout grid{field.local_support(),{31,33,35}};
        for(int z=0;z<35;++z)for(int y=0;y<33;++y)for(int x=0;x<31;++x) {
            auto p=index_to_local(grid,{double(x),double(y),double(z)});auto value=field.at(p);
            valid=valid&&std::isfinite(value)&&value>=0&&value<=field.maximum();
            if(field.recipe().base.enabled&&p.y<=field.recipe().base.height)valid=valid&&value==0;
        }
        check(valid,"fixture samples finite/nonnegative/bounded with base constraint");
        check(field.at({1000,1000,1000})==0,"outside conservative support is zero");
        check(field.at(field.local_support().min)==0,"envelope boundary is zero");
    }
    DensityField tall(density_fixture(0)),wide(density_fixture(1)),flat(density_fixture(3)),cut(density_fixture(4));
    check(tall.at({0,35,0})==1&&tall.at({0,70,0})>0,"tall ellipsoid has vertical extent");
    check(wide.at({30,25,0})>0&&wide.at({0,70,0})==0,"wide ellipsoid has horizontal extent");
    check(flat.at({0,10,0})==0&&flat.at({0,9,0})==0&&flat.at({0,14,0})>0,"flat base never regrows below plane");
    check(cut.at({12,40,0})==0&&cut.at({0,60,0})>0,"complete cut and retained body");
    auto r=density_fixture(2);DensityField a(r);std::reverse(r.cells.begin(),r.cells.end());DensityField b(r);
    bool order=true;for(int i=-30;i<=30;++i)order=order&&a.at({double(i),35,0})==b.at({double(i),35,0});
    check(order,"ID-sorted smooth union independent of vector order");
    r=density_fixture(0);r.cells.push_back(r.cells[0]);r.cells[1].id=3;r.blend_width=0;r.overlap=0;
    check(DensityField(r).at({0,35,0})==1,"coincident shape union does not add density by default");
    r.overlap=1;check(DensityField(r).at({0,35,0})==2&&DensityField(r).maximum()==2,"overlap addition independently controlled and bounded");
    r.cells.clear();check(DensityField(r).at({0,35,0})==0&&DensityField(r).maximum()==0,"empty field");
    r=density_fixture(0);r.cells[0].radii={1e-4,1e-4,1e-4};r.density=1000;
    check(std::isfinite(DensityField(r).at(r.cells[0].center)),"minimum radius and maximum density remain finite");
    r.transform.translation={10,20,30};r.transform.scale={2,3,4};auto bounds=DensityField(r).world_support();
    check(bounds.min==Vec3{-90,-40,-170}&&bounds.max==Vec3{110,320,230},"world support contains transformed envelope");
    try {tall.at({std::numeric_limits<double>::quiet_NaN(),0,0});check(false,"non-finite sample rejected");}
    catch(const std::invalid_argument&){check(true,"non-finite sample rejected");}
    check(sizeof(GpuDensityParams)==576,"uniform layout exact size");
    return failures?1:0;
}
