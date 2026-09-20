#include "white/top_lobes.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace white;
namespace {
void check(bool okay,const char* text){if(!okay)throw std::runtime_error(text);}
template<class F> void rejects(F operation,const char* text){bool rejected=false;try{operation();}catch(const std::invalid_argument&){rejected=true;}check(rejected,text);}
TopLobeSource fixture(bool detailed=true){
    CenterlineShape curve;
    if(detailed){
        curve.points[1].offset={8,0,-5};
        curve.profile={{1,0,1,.65},{2,.5,1.1,1},{3,1,.9,.55}};
        curve.source.modifiers.cuts={{80,{12,45,3},{5,9,7},2}};
        curve.source.modifiers.noise.origin={5,7,-3};
        curve.source.modifiers.transform={{15,8,-3},{0,0,0},{1.2,.8,1.1}};
        curve.source.cell_adjustments={{curve.source.cell_ids[2],{2,0,-1},{1.05,1,.95},UINT64_C(912)}};
    }else curve.source.modifiers.noise={};
    auto trunk=develop_centerline(curve);
    return make_top_lobe_source(trunk,trunk.cells.front().id);
}
void hierarchy_and_seeds(){
    auto source=fixture();const auto original=source.trunk;
    check(generate_top_lobes(source).empty(),"OFF generated active lobes");
    source.settings.mode=TopLobeMode::parent;const auto parent=generate_top_lobes(source);
    check(parent.size()==1&&parent[0].depth==1&&parent[0].parent_id==0,"Parent-only is not one root primitive");
    source.settings.mode=TopLobeMode::children;const auto children=generate_top_lobes(source);
    check(children.size()==3&&children[0]==parent[0],"Children mode moved or replaced parent");
    for(std::size_t i=1;i<children.size();++i){
        check(children[i].depth==2&&children[i].parent_id==parent[0].id,"Child is not attached to explicit parent");
        check(children[i].primitive.id==source.lobe_ids[i],"Child lost stable path ID");
        const auto delta=children[i].primitive.center-parent[0].primitive.center;
        const auto r=children[i].primitive.radii,pr=parent[0].primitive.radii;
        check(std::sqrt(dot(delta,delta))<std::min({pr.x,pr.y,pr.z})+std::min({r.x,r.y,r.z}),"Child detached from parent support");
    }
    auto one=source;one.settings.child_limit=1;const auto first_child=generate_top_lobes(one);
    check(first_child.size()==2&&first_child[0]==children[0]&&first_child[1]==children[1],"Changing child count changed retained topology");
    auto sparse=source;sparse.settings.hierarchy_density=0;
    check(generate_top_lobes(sparse)==parent,"Zero hierarchy density changed parent or retained children");
    auto detail=source;detail.trunk.cells[0].shape.source.parameters.detail_seed+=UINT64_C(4294967311);
    check(generate_top_lobes(detail)==children,"Detail seed changed lobe geometry or hierarchy");
    auto structure=source;structure.trunk.cells[0].shape.source.parameters.structure_seed+=1;
    const auto regenerated=generate_top_lobes(structure);
    check(regenerated!=children,"Structure seed did not regenerate hierarchy");
    for(std::size_t i=0;i<children.size();++i)check(regenerated[i].id==children[i].id&&regenerated[i].parent_id==children[i].parent_id,"Reseeding invalidated path IDs");
    check(source.trunk==original,"Lobe generation modified authoritative trunk");
    for(std::uint64_t seed=0;seed<32;++seed){
        auto dense=source;dense.trunk.cells[0].shape.source.parameters.structure_seed=seed;
        const auto all=generate_top_lobes(dense);dense.settings.hierarchy_density=.5;
        for(const auto& node:generate_top_lobes(dense)){
            const auto found=std::find_if(all.begin(),all.end(),[&](const auto& n){return n.id==node.id;});
            check(found!=all.end()&&*found==node,"Density thinning moved retained hierarchy nodes");
        }
    }
}
void exact_lower_region_and_packet(){
    std::size_t exact_samples=0;
    for(bool detailed:{false,true})for(double translation:{0.,7.25}){
        auto source=fixture(detailed);source.trunk.cells[0].translation={11,translation,-9};
        const DevelopedEvaluationPlan trunk(source.trunk);const TopLobeEvaluationPlan off(source);
        const auto old=trunk.gpu_params();const auto off_packet=off.gpu_params();
        check(std::memcmp(&old,&off_packet.fields,sizeof(old))==0&&off_packet.mask.x==0,"OFF changed complete GPU packet");
        check(off.local_support()==trunk.local_support()&&off.maximum()==trunk.maximum(),"OFF changed support or maximum");
        source.settings.mode=TopLobeMode::children;const TopLobeEvaluationPlan active(source);
        const auto packet=active.gpu_params();
        check(packet.mask.x==1&&packet.mask.w==3&&packet.fields.groups[1].config.z==3,"Children packet does not contain three actual lobes");
        check(std::memcmp(&old.groups[0],&packet.fields.groups[0],sizeof(GpuDensityParams))==0,"Top lobes changed trunk GPU parameters");
        const auto boundary=top_lobe_mask_height(source);const auto bounds=trunk.local_support();
        for(unsigned z=0;z<13;++z)for(unsigned y=0;y<17;++y)for(unsigned x=0;x<17;++x){
            const Vec3 p{bounds.min.x+(bounds.max.x-bounds.min.x)*x/16.,bounds.min.y+(boundary-bounds.min.y)*y/16.,bounds.min.z+(bounds.max.z-bounds.min.z)*z/12.};
            check(active.at(p)==trunk.at(p)&&off.at(p)==trunk.at(p),"Top lobes changed fixed lower region");++exact_samples;
        }
        source.settings.mode=TopLobeMode::parent;const TopLobeEvaluationPlan parent(source);
        check(parent.hierarchy().size()==1&&parent.gpu_params().fields.groups[1].config.z==1,"Parent-only packed hidden extra primitives");
        source.settings.density_scale=0;const TopLobeEvaluationPlan zero(source);
        const auto zero_packet=zero.gpu_params();
        check(zero.local_support()==trunk.local_support()&&zero.maximum()==trunk.maximum()&&std::memcmp(&old,&zero_packet.fields,sizeof(old))==0,"Zero lobe density changed support, maximum or GPU fields");
        const GridLayout grid{active.local_support(),{13,17,11}};
        for(unsigned z=0;z<11;++z)for(unsigned y=0;y<17;++y)for(unsigned x=0;x<13;++x){const auto p=index_to_local(grid,{double(x),double(y),double(z)});check(zero.at(p)==trunk.at(p),"Zero lobe density changed original field");}
        check(zero_packet.mask.x==0,"Zero lobe density missed exact OFF GPU path");
    }
    auto pair=fixture(false);auto second=make_developed_cell(pair.trunk);second.translation={90,0,0};
    pair.trunk=command_developed_cloud(pair.trunk,DevelopedAdd{second});
    pair=make_top_lobe_source(pair.trunk,pair.target_cell);
    const TopLobeEvaluationPlan off_pair(pair);const DevelopedEvaluationPlan plain_pair(pair.trunk);
    check(off_pair.gpu_params().fields.settings.x==2&&off_pair.at({70,50,0})==plain_pair.at({70,50,0}),"OFF did not preserve two independent developments");
    pair.settings.mode=TopLobeMode::parent;
    rejects([&]{TopLobeEvaluationPlan invalid(pair);},"Enabled lobes silently flattened two developments");
    std::cout<<"top_lobes exact_lower_samples="<<exact_samples<<'\n';
}
void support_continuity_and_bake(){
    auto source=fixture(false);source.settings.mode=TopLobeMode::children;
    source.settings.growth_direction={.6,.8,0};const TopLobeEvaluationPlan field(source);const DevelopedEvaluationPlan trunk(source.trunk);
    const auto bounds=field.local_support();const GridLayout grid{bounds,{31,29,23}};
    double increase=0;
    for(unsigned z=0;z<23;++z)for(unsigned y=0;y<29;++y)for(unsigned x=0;x<31;++x){
        const auto p=index_to_local(grid,{double(x),double(y),double(z)});const auto value=field.at(p);
        check(std::isfinite(value)&&value>=0&&value<=field.maximum(),"Lobes exceeded finite density bound");
        check(value+1e-13>=trunk.at(p),"Adding top lobes removed underlying density");
        increase=std::max(increase,value-trunk.at(p));
    }
    check(increase>1e-4,"Default lobes produced no measurable shape addition");
    const auto mask=top_lobe_mask_height(source);
    for(int z=-4;z<=4;++z)for(int x=-4;x<=4;++x){
        const Vec3 at{x*10.,mask,z*10.};const double exact=field.at(at);
        check(exact==trunk.at(at),"Mask boundary is not exactly fixed");
        check(std::abs(field.at(at+Vec3{0,1e-7,0})-exact)<1e-5,"Top mask has a density discontinuity");
    }
    for(unsigned axis=0;axis<3;++axis)for(bool high:{false,true}){
        auto p=(bounds.min+bounds.max)*.5;double& v=axis==0?p.x:axis==1?p.y:p.z;
        v=high?(axis==0?bounds.max.x:axis==1?bounds.max.y:bounds.max.z)+1000:(axis==0?bounds.min.x:axis==1?bounds.min.y:bounds.min.z)-1000;
        check(field.at(p)==0,"Lobe support leaked outside conservative bounds");
    }
    const GridLayout small{bounds,{9,7,5}};const auto baked=bake_top_lobes(field,small);
    for(unsigned z=0;z<5;++z)for(unsigned y=0;y<7;++y)for(unsigned x=0;x<9;++x)check(baked[(z*7+y)*9+x]==float(field.at(index_to_local(small,{double(x),double(y),double(z)}))),"Lobe bake differs from procedural evaluator");
    const auto world=field.world_support();
    for(unsigned i=0;i<8;++i){const auto p=local_to_world(source.trunk.transform,{i&1?bounds.max.x:bounds.min.x,i&2?bounds.max.y:bounds.min.y,i&4?bounds.max.z:bounds.min.z});check(p.x>=world.min.x&&p.y>=world.min.y&&p.z>=world.min.z&&p.x<=world.max.x&&p.y<=world.max.y&&p.z<=world.max.z,"World support lost transformed corner");}
    std::cout<<"top_lobes bounded_samples="<<31*29*23<<" max_shape_addition="<<increase<<'\n';
}
void invalid_sources(){
    const auto source=fixture(false);
    auto invalid=[&](auto edit){auto changed=source;changed.settings.mode=TopLobeMode::children;edit(changed);check(!validate_top_lobes(changed).empty(),"Invalid source produced no validation diagnostic");rejects([&]{(void)generate_top_lobes(changed);},"Invalid hierarchy was generated");};
    invalid([](auto& s){s.settings.depth_limit=3;});invalid([](auto& s){s.settings.depth_limit=1;});
    invalid([](auto& s){s.settings.child_limit=3;});invalid([](auto& s){s.settings.parent_radius=0;});
    invalid([](auto& s){s.settings.child_radius_ratio=0;});invalid([](auto& s){s.settings.parent_radius=std::numeric_limits<double>::quiet_NaN();});
    invalid([](auto& s){s.settings.mode=static_cast<TopLobeMode>(9);});invalid([](auto& s){s.settings.growth_direction={1,0,0};});
    invalid([](auto& s){s.settings.mask_transition=0;});invalid([](auto& s){s.settings.top_start=.2;});
    invalid([](auto& s){s.lobe_ids[2]=s.lobe_ids[0];});invalid([](auto& s){s.target_cell=0;});
    invalid([](auto& s){s.trunk.cells[0].shape.source.modifiers.manual_cells={{900,{70,50,0},{5,5,5},1}};});
    auto capacity=source;capacity.settings.mode=TopLobeMode::parent;capacity.trunk.cells[0].shape.source.modifiers.manual_cells={{900,{70,50,0},{5,5,5},1},{901,{80,50,0},{5,5,5},2}};
    check(validate_top_lobes(capacity).empty()&&TopLobeEvaluationPlan(capacity).hierarchy().size()==1,"Exact seven-plus-one parent budget rejected");
    capacity.settings.mode=TopLobeMode::children;capacity.settings.hierarchy_density=0;
    check(!validate_top_lobes(capacity).empty(),"Random thinning bypassed worst-case primitive budget");
    invalid([](auto& s){s.trunk.cells[0].translation.y=100000;s.settings.mask_transition=.1;});
    auto expanded=source;expanded.settings.mode=TopLobeMode::parent;expanded.settings.fusion_width=1000;
    expanded.trunk.cells[0].shape.source.modifiers.manual_cells={{900,{999900,50,0},{1,1,1},1}};
    check(validate_developed_cloud(expanded.trunk).empty(),"Expanded-support regression has invalid baseline trunk");
    check(!validate_top_lobes(expanded).empty(),"Fusion-expanded support exceeded Scene coordinate cap without rejection");
}
}
int main(){try{hierarchy_and_seeds();exact_lower_region_and_packet();support_continuity_and_bake();invalid_sources();std::cout<<"Top lobes: stable finite hierarchy, exact lower region, real parent-only packet, seed separation, support and CPU bake passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
