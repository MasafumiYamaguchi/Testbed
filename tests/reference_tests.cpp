#include "white/reference.hpp"
#include "white/developed_scene.hpp"
#include "white/phase.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#define require(ok) do{if(!(ok))throw std::runtime_error("Reference contract failed at line "+std::to_string(__LINE__));}while(false)
namespace {
white::TrackingSnapshot slab(double sigma,double albedo,double g=0,white::Vec3 sun={0,0,1}){
    white::Scene scene;scene.cloud.base.enabled=false;scene.cloud.cuts.clear();scene.cloud.envelope={{-1,-1,-1},{1,1,1}};scene.cloud.optics={sigma,albedo,g};scene.sun.direction_to_light=sun;scene.camera.position={0,0,-3};scene.camera.target={0,0,0};scene.camera.near_plane=.01;
    white::GridLayout grid{scene.cloud.envelope,{8,8,8}};return {scene,grid,std::vector<float>(512,1)};
}
struct Moments {double sum=0,square=0;unsigned n=0;void add(double x){sum+=x;square+=x*x;++n;}double mean()const{return sum/n;}double variance()const{return std::max(0.,(square-sum*sum/n)/(n-1));}double standard_error()const{return std::sqrt(variance()/n);}};
void analytic_single(){
    white::ReferenceSettings settings;settings.mode=white::ReferenceMode::single_scattering;const white::TrackingRay ray{{0,0,-3},{0,0,1},0,100};
    for(double g:{-.6,0.,.6})for(double sign:{-1.,1.}){
        const auto medium=slab(.4,.8,g,{0,0,sign});const double tau=.8,phase=white::hg_phase(sign,g);
        const double analytic=.8*phase*(sign>0?tau*std::exp(-tau):.5*(-std::expm1(-2*tau)))+white::reference_background.x*std::exp(-tau);
        const auto marched=white::reference_raymarch(medium,ray,512,64);require(std::abs(marched.radiance.x-analytic)<2e-7);
        for(std::uint64_t seed:std::array<std::uint64_t,4>{17,42,99991,4294967313ull}){
            Moments stats;for(unsigned i=0;i<8192;++i){const auto r=white::reference_sample(medium,ray,settings,{seed,0,i,0,0});require(r.status==white::ReferenceStatus::complete);stats.add(r.radiance.x);}
            require(std::abs(stats.mean()-analytic)<6*stats.standard_error()+2e-5);
            std::cout<<"single_slab,"<<g<<','<<sign<<','<<seed<<','<<stats.n<<','<<analytic<<','<<stats.mean()<<','<<stats.standard_error()<<'\n';
        }
    }
}
void boundaries(){
    white::ReferenceSettings settings;const white::TrackingRay ray{{0,0,-3},{0,0,1},0,100};
    auto empty=slab(0,1);const auto vacuum=white::reference_sample(empty,ray,settings,{1,0,0,0,0});require(vacuum.radiance==white::reference_background&&vacuum.transmittance==1&&vacuum.collision_events==0);
    const auto absorbing=slab(.7,0);Moments t;
    for(unsigned i=0;i<8192;++i){const auto r=white::reference_sample(absorbing,ray,settings,{17,0,i,0,0});require(r.status==white::ReferenceStatus::complete&&r.collision_events==0);require(r.radiance.x==white::reference_background.x*r.transmittance);t.add(r.transmittance);}
    require(std::abs(t.mean()-std::exp(-1.4))<6*t.standard_error());
    const auto internal=white::reference_raymarch(absorbing,{{0,0,0},{0,0,1},0,100},512,16);require(std::abs(internal.transmittance-std::exp(-.7))<1e-12);
    const auto conservative=slab(1,1,.6);for(unsigned i=0;i<256;++i){const auto r=white::reference_sample(conservative,ray,settings,{99991,0,i,0,0});require(r.status==white::ReferenceStatus::complete&&std::isfinite(r.radiance.x));}
    // A safety ceiling is distinguishable from a legitimate escaping path.
    settings.event_limit=1;const auto thick=slab(100,.98);unsigned event_failures=0;
    for(unsigned i=0;i<32;++i)event_failures+=white::reference_sample(thick,ray,settings,{7,0,i,0,0}).status==white::ReferenceStatus::event_limit;
    require(event_failures>0);
    settings.event_limit=100000;settings.bounce_limit=1;settings.roulette_start=1;unsigned bounce_failures=0;
    for(unsigned i=0;i<128;++i)bounce_failures+=white::reference_sample(slab(3,1),ray,settings,{42,0,i,0,0}).status==white::ReferenceStatus::bounce_limit;
    require(bounce_failures>0);
    const auto ordinary=slab(1,.9);
    const white::TrackingSnapshot unresolvable(ordinary.scene(),ordinary.grid(),std::vector<float>(512,std::numeric_limits<float>::max()));
    require(white::reference_sample(unresolvable,ray,settings,{17,0,0,0,0}).status==white::ReferenceStatus::numerical_failure);
}
void grouped_snapshot_rejection(){
    white::Scene scene;scene.developed=white::develop_cumulonimbus(white::CumulonimbusGroup{});
    white::refresh_developed_scene(scene);
    // A one-group source retains the supported exact legacy frozen-grid path.
    const auto single=white::bake_reference_snapshot(scene,4);require(single.density().size()==64);
    const auto next=white::make_developed_cell(*scene.developed);
    scene=white::scene_with_developed_command(scene,white::DevelopedAdd{next});
    const white::GridLayout grid{scene.cloud.envelope,{4,4,4}};
    auto rejects=[](auto operation){bool rejected=false;try{operation();}catch(const std::invalid_argument& e){rejected=std::string(e.what()).find("independent hard-mask frozen-grid contract")!=std::string::npos;}require(rejected);};
    rejects([&]{(void)white::bake_reference_snapshot(scene,4);});
    rejects([&]{(void)white::TrackingSnapshot(scene,grid,std::vector<float>(64,1));});
    const auto density=std::vector<float>(64,1);const auto majorant=white::build_majorant(grid,density);
    rejects([&]{(void)white::TrackingSnapshot(scene,grid,density,majorant);});
}
void progressive(){
    white::ReferenceSettings settings;settings.width=4;settings.height=3;settings.samples=64;settings.seed=17;
    const auto snapshot=slab(.8,.9,.3);white::ReferenceRenderer one(snapshot,settings),chunks(snapshot,settings);
    require(white::reference_report_json(chunks).find("\"complete\": false")!=std::string::npos);
    one.advance(64);chunks.advance(16);
    require(chunks.statistics().complete()&&white::reference_report_json(chunks).find("in progress")!=std::string::npos);
    chunks.advance(48);
    require(one.image().rgba==chunks.image().rgba);require(one.statistics().complete()&&chunks.statistics().complete());require(one.mean_estimator_variance()>0);
    const auto report=white::reference_report_json(one);require(report.find("primary camera")!=std::string::npos&&report.find("\"complete\": true")!=std::string::npos);
    bool rejected=false;try{one.advance(1);}catch(const std::exception&){rejected=true;}require(rejected);
    settings.event_limit=1;white::ReferenceRenderer partial(slab(100,.98),settings);partial.advance(1);require(!partial.statistics().complete());require(white::reference_report_json(partial).find("PARTIAL")!=std::string::npos);
}
void secondary_extent(){
    const auto full=slab(.8,.9,.3);auto scene=full.scene();scene.camera.far_plane=.5;
    const white::TrackingSnapshot short_far(scene,full.grid(),std::vector<float>(full.density().begin(),full.density().end()));
    const white::TrackingRay ray{{0,0,-3},{0,0,1},0,100};white::ReferenceSettings settings;
    // The caller supplies the primary interval. Changing the camera's default
    // far plane must not change light visibility or later scattering orders.
    const auto full_march=white::reference_raymarch(full,ray,512,64),short_march=white::reference_raymarch(short_far,ray,512,64);
    require(full_march.radiance==short_march.radiance&&full_march.transmittance==short_march.transmittance);
    for(auto mode:{white::ReferenceMode::single_scattering,white::ReferenceMode::multiple_scattering}){
        settings.mode=mode;
        for(unsigned i=0;i<512;++i){
            const auto a=white::reference_sample(full,ray,settings,{42,0,i,0,0});
            const auto b=white::reference_sample(short_far,ray,settings,{42,0,i,0,0});
            require(a.radiance==b.radiance&&a.transmittance==b.transmittance&&a.status==b.status&&a.collision_events==b.collision_events);
        }
    }
    // Primary clipping remains intentional, including a camera interval ending
    // halfway through the homogeneous box.
    const white::TrackingRay clipped{{0,0,-3},{0,0,1},0,3};
    const auto primary=white::reference_raymarch(full,clipped,512,64);
    require(std::abs(primary.transmittance-std::exp(-.8))<1e-12);
    auto bad=ray;bad.direction_world={0,0,.5};bool rejected=false;
    try{(void)white::reference_raymarch(full,bad,16,16);}catch(const std::invalid_argument&){rejected=true;}require(rejected);
    // Tracking permits tiny length roundoff; the phase must not reject a
    // corresponding cosine slightly above one at the forward direction.
    auto rounded=ray;rounded.direction_world.z+=1e-10;
    for(unsigned i=0;i<64;++i)require(white::reference_sample(full,rounded,settings,{42,0,i,0,0}).status==white::ReferenceStatus::complete);
    settings.mode=static_cast<white::ReferenceMode>(99);rejected=false;
    try{(void)white::reference_sample(full,ray,settings,{});}catch(const std::invalid_argument&){rejected=true;}require(rejected);
}
void multiple_seeds(){
    const auto medium=slab(.8,.9,.3);white::ReferenceSettings settings;const white::TrackingRay ray{{0,0,-3},{0,0,1},0,100};
    const auto single=white::reference_raymarch(medium,ray,512,64);Moments seed_means;double average_variance=0;double low_variance=0;
    for(std::uint64_t seed:std::array<std::uint64_t,4>{17,42,99991,4294967313ull}){
        Moments stats;
        for(unsigned i=0;i<4096;++i){const auto r=white::reference_sample(medium,ray,settings,{seed,0,i,0,0});require(r.status==white::ReferenceStatus::complete);stats.add(r.radiance.x);
            if(i==511||i==4095){std::cout<<"multi_slab,"<<seed<<','<<i+1<<','<<stats.mean()<<','<<stats.variance()<<','<<stats.standard_error()<<'\n';if(i==511)low_variance+=stats.variance()/stats.n;}
        }
        seed_means.add(stats.mean());average_variance+=stats.variance()/stats.n;
    }
    // Confidence-aware consistency; no requirement of monotonically improving
    // individual random estimates. Additional orders have nonnegative energy.
    require(seed_means.mean()>single.radiance.x);require(average_variance<low_variance*.3);
    require(seed_means.variance()<average_variance*4);
}
void roulette_weighting(){
    const auto medium=slab(.8,.7,.3);const white::TrackingRay ray{{0,0,-3},{0,0,1},0,100};
    white::ReferenceSettings early,late;early.roulette_start=1;late.roulette_start=64;
    for(std::uint64_t seed:std::array<std::uint64_t,4>{17,42,99991,4294967313ull}){
        Moments roulette,unthinned;std::uint64_t terminations=0;
        for(unsigned i=0;i<16384;++i){
            const auto a=white::reference_sample(medium,ray,early,{seed,0,i,0,0});
            // Independent keys make the combined standard-error estimate valid.
            const auto b=white::reference_sample(medium,ray,late,{seed^0x95c706f3ULL,0,i,0,0});
            require(a.status==white::ReferenceStatus::complete&&b.status==white::ReferenceStatus::complete);
            roulette.add(a.radiance.x);unthinned.add(b.radiance.x);terminations+=a.roulette_terminations;
        }
        const double error=std::hypot(roulette.standard_error(),unthinned.standard_error());
        require(terminations>0&&std::abs(roulette.mean()-unthinned.mean())<6*error+2e-5);
        std::cout<<"roulette_weighting,"<<seed<<','<<roulette.n<<','<<roulette.mean()<<','<<unthinned.mean()<<','<<error<<','<<terminations<<'\n';
    }
}
}
int main(){try{std::cout<<std::setprecision(17);analytic_single();boundaries();grouped_snapshot_rejection();secondary_extent();progressive();multiple_seeds();roulette_weighting();std::cout<<"Reference contracts passed: multi-seed analytic single scatter, empty/absorption/unit albedo/internal camera, unclipped secondary transport, progressive reproducibility, typed safety/numerical limits, multiple-seed variance and roulette weighting\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
