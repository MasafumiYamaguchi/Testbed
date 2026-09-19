#include "white/density.hpp"
#include "white/noise.hpp"
#include "white/persistence.hpp"
#include <cmath>
#include <iostream>
using namespace white;
int main(int argc,char** argv){
    int failures=0;auto check=[&](bool ok,const char* label){std::cout<<(ok?"PASS ":"FAIL ")<<label<<'\n';failures+=!ok;};
    auto r=density_fixture(4);r.noise.medium_strength=0.8;r.noise.micro_erosion=7;r.noise.warp_amplitude=5;
    auto other=r;++other.detail_seed;
    check(other.cells==r.cells&&other.structure_seed==r.structure_seed,"detail reseed preserves macro parameters and structure seed");
    DensityField a(r),b(other);double difference=0;
    for(int i=0;i<50;++i){Vec3 p{-10+i*0.3,30+i*0.15,2};difference+=std::abs(a.at(p)-b.at(p));}
    check(difference>0.01,"detail seed changes density");
    auto remote=r;remote.cells.push_back({99,{400,300,200},{10,10,10},123});DensityField far(remote);bool same=true;
    for(int i=0;i<40;++i){Vec3 p{-10+i*0.4,30,0};same=same&&a.at(p)==far.at(p);}
    check(same&&cell_random_key(r,r.cells[0])==cell_random_key(remote,remote.cells[0]),"remote cell preserves local random series and nonoverlapping density");
    auto bounds=r;bounds.envelope={{-200,-200,-200},{200,200,200}};DensityField wider(bounds);same=true;
    for(int i=0;i<40;++i){Vec3 p{-10+i*0.4,30,0};same=same&&a.at(p)==wider.at(p);}
    check(same,"changing bounds does not renormalize noise coordinates");
    bool constraints=true,bounded=true,warp_bounded=true;
    for(std::uint64_t seed:{0ull,1ull,0xffffffffffffffffull,982345ull})for(bool maximum:{false,true}){
        auto test=r;test.detail_seed=seed;test.noise.medium_strength=maximum?1:0;test.noise.micro_erosion=maximum?20:0;test.noise.warp_amplitude=maximum?20:0;
        test.noise.medium_frequency=test.noise.micro_frequency=test.noise.warp_frequency=maximum?2:0.0001;
        DensityField field(test);
        for(int x=-5;x<=5;++x)for(int y=-5;y<=5;++y)for(int z=-5;z<=5;++z){
            Vec3 p{x*9.1,y*12.2+35,z*9.3};double d=field.at(p);bounded=bounded&&std::isfinite(d)&&d>=0&&d<=field.maximum();
            Vec3 cut=p-test.cuts[0].center;auto cr=test.cuts[0].radii;bool in_cut=cut.x*cut.x/(cr.x*cr.x)+cut.y*cut.y/(cr.y*cr.y)+cut.z*cut.z/(cr.z*cr.z)<=1;
            if(p.y<=test.base.height||in_cut||p.x<=test.envelope.min.x||p.x>=test.envelope.max.x||p.y<=test.envelope.min.y||p.y>=test.envelope.max.y||p.z<=test.envelope.min.z||p.z>=test.envelope.max.z)constraints=constraints&&d==0;
            auto warp=domain_displacement(p*test.noise.warp_frequency,noise_seed(seed),test.noise.warp_amplitude);warp_bounded=warp_bounded&&dot(warp,warp)<=test.noise.warp_amplitude*test.noise.warp_amplitude+1e-9;
        }
    }
    check(constraints,"bottom, envelope and full cuts remain empty after all detail stages");
    check(bounded,"multiple seeds and parameter extrema preserve finite density upper bound");check(warp_bounded,"domain displacement obeys documented magnitude bound");
    Scene scene;scene.cloud=r;check(parse_scene_json(scene_json(scene))==scene,"noise settings and seeds round-trip");
    if(argc==2){auto legacy=read_scene(argv[1]);check(legacy.schema_version==7&&legacy.algorithm_version==3&&legacy.cloud.noise==NoiseSettings{}&&legacy.cloud.cuts.size()==1,"v1 scene migrates with all noise disabled");}
    else check(false,"legacy fixture path required");
    scene.cloud.noise.warp_amplitude=20.1;check(!validate(scene).empty(),"invalid displacement rejected");
    return failures?1:0;
}
