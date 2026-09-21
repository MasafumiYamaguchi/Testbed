#include "white/density_profile.hpp"
#include "white/density.hpp"
#include "white/dense_cache.hpp"
#include "white/field_graph.hpp"
#include "white/revision_queue.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace white;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void near(double a,double b,const char* message,double tolerance=1e-10){check(std::abs(a-b)<=tolerance,message);}
void rejected(AltitudeDensityProfile profile){check(!validate_altitude_density(profile).empty(),"Invalid altitude profile accepted by validation");bool rejected=false;try{(void)AltitudeDensityEvaluator(std::move(profile));}catch(const std::invalid_argument&){rejected=true;}check(rejected,"Invalid altitude profile accepted by evaluator");}
float shader_scalar(float y,const GpuDensityParams& params){const auto p=params.altitude_density_params;const auto n=unsigned(p.z);if(n==0)return 1;const float t=std::clamp((y-p.x)/p.y,0.f,1.f);for(unsigned i=1;i<8;++i)if(i<n&&t<params.altitude_density_knots[i].x){const auto a=params.altitude_density_knots[i-1],b=params.altitude_density_knots[i];const float u=(t-a.x)/(b.x-a.x);return a.y+(b.y-a.y)*u;}return params.altitude_density_knots[n-1].y;}
}
int main(){try{
    const AltitudeDensityEvaluator off;for(double y:{-1e100,-1.,0.,.5,1.,1e100})check(off.at(y)==1,"Default altitude profile changed legacy density");check(off.maximum()==1,"Disabled profile bound differs");
    AltitudeDensityProfile profile{true,10,20,{{0,.2},{.25,.8},{.75,.4},{1,.6}}};
    const AltitudeDensityEvaluator evaluator(profile);near(evaluator.at(0),.2,"Below-profile endpoint differs");near(evaluator.at(10),.2,"First knot differs");near(evaluator.at(15),.8,"Interior knot differs");near(evaluator.at(20),.6,"Linear interpolation differs");near(evaluator.at(25),.4,"Second interior knot differs");near(evaluator.at(40),.6,"Above-profile endpoint differs");near(evaluator.maximum(),.8,"Profile maximum is not knot maximum");
    for(double base:{-100.,0.,100.})for(double height:{10.,4000.}){auto p=profile;p.base=base;p.height=height;const AltitudeDensityEvaluator eval(p);near(eval.at(base+.5*height),.6,"Profile physical-range interpolation differs");}
    auto high_broad=profile;high_broad.base=100000;high_broad.height=4000;
    near(AltitudeDensityEvaluator(high_broad).at(102000),.6,"Resolved high-coordinate broad profile was rejected");
    auto disabled=profile;disabled.enabled=false;check(AltitudeDensityEvaluator(disabled).at(15)==1&&AltitudeDensityEvaluator(disabled).maximum()==1,"Disabled nonidentity knots changed density");
    auto invalid=profile;invalid.height=0;rejected(invalid);invalid=profile;invalid.height=std::numeric_limits<double>::infinity();rejected(invalid);invalid=profile;invalid.base=std::numeric_limits<double>::quiet_NaN();rejected(invalid);
    invalid=profile;invalid.knots[1].t=0;rejected(invalid);invalid=profile;invalid.knots[1].scale=-.01;rejected(invalid);invalid=profile;invalid.knots[1].scale=1.01;rejected(invalid);invalid=profile;invalid.knots.back().t=.9;rejected(invalid);invalid=profile;invalid.knots.resize(9);rejected(invalid);
    // Accepted inputs must not lose a sharp band to float-coordinate precision.
    rejected({true,1e6,1e-4,{{0,0},{1,1}}});
    rejected({true,100000,10,{{0,1},{.5,1},{.501,0},{1,0}}});
    const AltitudeDensityEvaluator resolved_band({true,0,120,{{0,1},{.4,1},{.45,0},{1,0}}});
    near(resolved_band.at(51),.5,"Reasonably resolved local profile band changed");
    const AltitudeDensityEvaluator high_constant({true,1e6,1e-4,{{0,.5},{1,.5}}});
    check(high_constant.at(1000000.00005)==.5,"Constant profile was needlessly precision-rejected");
    bool nonfinite=false;try{(void)evaluator.at(std::numeric_limits<double>::quiet_NaN());}catch(const std::invalid_argument&){nonfinite=true;}check(nonfinite,"Nonfinite altitude sample accepted");
    auto recipe=density_fixture(2);recipe.noise.medium_strength=.2;recipe.noise.warp_amplitude=2;recipe.noise.origin={1,2,3};recipe.base={true,0,2};
    const DensityField legacy(recipe);recipe.altitude_density.enabled=true;const DensityField identity(recipe);
    for(unsigned i=0;i<201;++i){const Vec3 p{-30+double(i%61),double(i%80),double(i%9)-4};check(legacy.at(p)==identity.at(p),"Unity profile changed legacy density arithmetic");}
    recipe.altitude_density={true,0,120,{{0,.2},{.1,.4},{.25,.7},{.4,.8},{.55,.3},{.7,.1},{.85,.5},{1,.6}}};
    const DensityField field(recipe);const auto params=gpu_density_params(field);const auto graph=field_graph_from_recipe(recipe);const FieldEvaluationPlan plan(graph);
    check(lower_to_recipe(graph)==recipe,"Field graph lost altitude profile");const auto graph_params=plan.gpu_params();check(std::memcmp(&params,&graph_params,sizeof(params))==0,"Field graph changed altitude GPU uniforms");
    check(sizeof(GpuDensityParams)==912&&offsetof(GpuDensityParams,altitude_density_params)==768&&offsetof(GpuDensityParams,altitude_density_knots)==784,"Altitude GPU layout mismatch");
    double max_float_error=0;const AltitudeDensityEvaluator reference(recipe.altitude_density);
    for(unsigned i=0;i<20001;++i){const double y=-10+140.*i/20000;max_float_error=std::max(max_float_error,std::abs(double(shader_scalar(float(y),params))-reference.at(y)));}
    check(max_float_error<5e-7,"Float packed profile and CPU interpolation disagree");
    // Constant geometry isolates the profile. Cache values already include its
    // modulation; sampling/clipping must retain that value, not multiply twice.
    CloudRecipe slab;slab.cells={{2,{0,0,0},{1000,1000,1000},0}};slab.envelope={{-4,-4,-4},{4,4,4}};slab.base.enabled=false;slab.density=2;slab.altitude_density={true,-4,8,{{0,.2},{.4,.7},{.6,.1},{1,.8}}};
    const DensityField slab_field(slab);const GridLayout grid{slab.envelope,{17,19,23}};std::vector<float> values(17*19*23);std::size_t samples=0;
    for(unsigned z=0;z<23;++z)for(unsigned y=0;y<19;++y)for(unsigned x=0;x<17;++x){const auto p=index_to_local(grid,{double(x),double(y),double(z)});const auto value=slab_field.at(p);check(value>=0&&value<=slab_field.maximum(),"Profile violated density bound");values[(std::size_t(z)*19+y)*17+x]=float(value);++samples;}
    near(slab_field.maximum(),1.6,"Density bound omitted profile maximum");
    for(unsigned y=0;y<19;++y){const auto p=index_to_local(grid,{8,double(y),11});const double cached=sample_dense(grid,values,p);check(hard_density_region(slab,p),"Profile unexpectedly changed hard density region");near(cached,slab_field.at(p),"Baked profile was lost or applied twice",1e-6);}
    auto cut=slab;cut.base={true,0,1};cut.cuts={{3,{0,1,0},{1,1,1},.5}};const DensityField cut_field(cut);check(cut_field.at({0,-1,0})==0&&cut_field.at({0,1,0})==0,"Altitude profile regrew flat base or cut");
    for(auto& knot:slab.altitude_density.knots)knot.scale=0;
    const DensityField zero(slab);check(zero.maximum()==0&&zero.at({0,0,0})==0,"Zero profile left density");
    Scene original;original.cloud=recipe;const auto original_hash=density_input_hash(original);auto changed=original;changed.cloud.altitude_density.knots[3].scale=.6;
    check(has(classify_change(original,changed),Dirty::density)&&density_input_hash(changed)!=original_hash,"Altitude knot edit did not invalidate density cache");
    changed=original;changed.exposure_ev=2;check(density_input_hash(changed)==original_hash,"Exposure invalidated altitude density cache");changed=original;changed.cloud.optics.g=.7;check(density_input_hash(changed)==original_hash,"Phase edit invalidated altitude density cache");
    std::cout<<"Altitude density: "<<samples<<" finite bounded bake samples; 8-knot CPU/packed-float max_error="<<max_float_error<<"; unity compatibility, mask preservation, cache no-double-application and invalidation passed\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
