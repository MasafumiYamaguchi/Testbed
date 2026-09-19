#include "white/delta_tracking.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
using namespace white;
void check(bool ok,const std::string& what) {if(!ok)throw std::runtime_error(what);}
Scene scene_for(GridLayout grid,double scale=0.8) {
    Scene scene;scene.cloud.envelope=grid.local_bounds;scene.cloud.base.enabled=false;
    scene.cloud.cuts.clear();scene.cloud.optics.extinction_scale=scale;return scene;
}
template<class F> void fails(F&& run,TrackingFailureReason reason) {
    try {run();}catch(const TrackingFailure& e){check(e.reason()==reason,"Wrong failure reason");return;}
    throw std::runtime_error("Expected explicit tracking failure");
}
struct Statistics {
    std::uint64_t count=0,escaped=0,candidates=0,nulls=0;
    double sum=0,sum2=0;
    std::array<std::uint64_t,8> bins{};
    void add(const DeltaTrackResult& result,double entry,double length) {
        ++count;escaped+=result.event==TrackingEvent::escape;
        candidates+=result.candidate_events;nulls+=result.null_events;
        const double d=result.distance-entry;sum+=d;sum2+=d*d;
        if(result.event==TrackingEvent::collision)++bins[std::min(std::size_t(d/length*8),std::size_t(7))];
    }
};
void report(const char* fixture,TrackingMode mode,std::uint64_t seed,const Statistics& s,double truth,double ms) {
    const double p=double(s.escaped)/s.count,ci=1.96*std::sqrt(p*(1-p)/s.count);
    std::cout<<fixture<<','<<(mode==TrackingMode::global?"global":"local")<<','<<seed<<','<<s.count<<','
        <<truth<<','<<p<<','<<std::max(0.,p-ci)<<','<<std::min(1.,p+ci)<<','
        <<s.sum/s.count<<','<<s.candidates<<','<<s.nulls<<','<<ms<<'\n';
}
void survival_check(const Statistics& s,double truth) {
    check(std::abs(double(s.escaped)/s.count-truth)<=6*std::sqrt(truth*(1-truth)/s.count)+2./s.count,"Survival outside six-sigma statistical gate");
}
void homogeneous() {
    const GridLayout grid{{{0,-1,-1},{4,1,1}},{32,5,3}};
    const TrackingSnapshot snapshot(scene_for(grid),grid,std::vector<float>(32*5*3,0.75f));
    const TrackingRay ray{{-1,0,0},{1,0,0},0,10};
    constexpr double length=4,sigma=0.6;
    const double survival=std::exp(-sigma*length),mean=(1-survival)/sigma;
    const double variance=2*(1-survival*(1+sigma*length))/(sigma*sigma)-mean*mean;
    for(auto mode:{TrackingMode::global,TrackingMode::local})for(std::uint64_t seed:{17ULL,42ULL,0x100000011ULL,99991ULL}) {
        Statistics s;const auto start=std::chrono::steady_clock::now();
        for(std::uint32_t sample=0;sample<32768;++sample) {
            const auto r=delta_track(snapshot,ray,{seed,23,sample,2,7},{mode,1000});
            check(r.distance>=1&&r.distance<=5,"Free flight outside clipped ray");
            check(r.null_events==0,"Homogeneous tight majorant unexpectedly produced nulls");
            s.add(r,1,length);
            if(s.count==2048||s.count==8192||s.count==32768) {
                survival_check(s,survival);
                check(std::abs(s.sum/s.count-mean)<=6*std::sqrt(variance/s.count)+2./s.count,"Censored free-flight mean outside uncertainty");
                for(std::size_t b=0;b<8;++b) {
                    const double p=std::exp(-sigma*length*double(b)/8)-std::exp(-sigma*length*double(b+1)/8);
                    check(std::abs(double(s.bins[b])/s.count-p)<=6*std::sqrt(p*(1-p)/s.count)+2./s.count,"Free-flight distribution bin outside uncertainty");
                }
                report("homogeneous",mode,seed,s,survival,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
            }
        }
    }
}
void heterogeneous() {
    const GridLayout grid{{{0,-1,-1},{8,1,1}},{64,5,3}};
    auto scene=scene_for(grid,0.75);std::vector<float> density(64*5*3);
    double optical_depth=0;
    for(unsigned z=0;z<3;++z)for(unsigned y=0;y<5;++y)for(unsigned x=0;x<64;++x)
        density[(z*5+y)*64+x]=(x>=24&&x<32)?2.f:0.02f;
    for(unsigned x=0;x<64;++x)optical_depth+=density[x]*0.125*scene.cloud.optics.extinction_scale;
    // For this full axis segment the clamped half-voxel end intervals and
    // piecewise-linear interior integrate to sum(voxel values)*voxel size.
    const double survival=std::exp(-optical_depth);
    const TrackingSnapshot snapshot(scene,grid,density);const TrackingRay ray{{-1,0,0},{1,0,0},0,20};
    std::uint64_t global_events=0,local_events=0;
    for(std::uint64_t seed:{17ULL,42ULL,0x100000011ULL,99991ULL}) {
        std::array<Statistics,2> statistics;
        for(auto mode:{TrackingMode::global,TrackingMode::local}) {
            auto& s=statistics[mode==TrackingMode::global?0:1];const auto start=std::chrono::steady_clock::now();
            for(std::uint32_t sample=0;sample<32768;++sample) {
                const auto mode_seed=seed^(mode==TrackingMode::global?0ULL:0xda3e39cb94b95bdbULL);
                s.add(delta_track(snapshot,ray,{mode_seed,11,sample,0,0},{mode,1000}),1,8);
                if(s.count==2048||s.count==8192||s.count==32768) {
                    survival_check(s,survival);
                    report("sparse_slab",mode,seed,s,survival,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
                }
            }
        }
        const auto& g=statistics[0];const auto& l=statistics[1];
        check(std::abs(double(g.escaped)/g.count-double(l.escaped)/l.count)<=6*std::sqrt(2*survival*(1-survival)/g.count)+4./g.count,"Global/local estimates disagree beyond uncertainty");
        global_events+=g.candidates;local_events+=l.candidates;
    }
    check(local_events*2<global_events,"Local majorant did not reduce sparse-slab candidates");
}
void boundaries_and_failures() {
    const GridLayout grid{{{-2,-3,-4},{2,3,4}},{17,19,23}};auto scene=scene_for(grid,1);
    std::vector<float> density(17*19*23,1);const TrackingSnapshot snapshot(scene,grid,density);
    // Published snapshot does not alias caller-owned density or Scene.
    density[0]=500;scene.cloud.optics.extinction_scale=100;
    check(snapshot.density()[0]==1&&snapshot.scene().cloud.optics.extinction_scale==1,"Snapshot aliases mutable caller data");
    for(auto mode:{TrackingMode::global,TrackingMode::local}) {
        const auto p=index_to_local(grid,{7.5,7.5,7.5});
        for(Vec3 d:{Vec3{1,0,0},Vec3{-1,0,0},Vec3{0,1,0},Vec3{0,0,-1}}) {
            const auto intervals=tracking_intervals(snapshot,{p,d,0,100},mode);
            check(!intervals.empty(),"Inside/boundary ray lost its interval");
            for(std::size_t i=0;i<intervals.size();++i) {
                check(intervals[i].exit>intervals[i].entry,"Boundary interval stalled");
                if(i)check(intervals[i].entry==intervals[i-1].exit,"Gap at brick boundary");
            }
            for(unsigned sample=0;sample<64;++sample)(void)delta_track(snapshot,{p,d,0,100},{17,0,sample,0,0},{mode,1000});
        }
        const TrackingSnapshot empty(scene_for(grid),grid,std::vector<float>(17*19*23));
        const auto escaped=delta_track(empty,{{0,0,0},{1,0,0},0,100},{},{mode,0});
        check(escaped.event==TrackingEvent::escape&&escaped.distance==2&&escaped.random_draws==0,"Vacuum should exit without proposals");
        const auto missed=delta_track(snapshot,{{20,0,0},{1,0,0},0,10},{},{mode,1});
        check(missed.event==TrackingEvent::escape&&missed.distance==10&&missed.intervals==0,"Missed grid should escape");
        auto bound=empty.majorant();for(auto& level:bound.levels)for(auto& value:level.maxima)value=1000;
        const TrackingSnapshot loose(scene_for(grid,1),grid,std::vector<float>(17*19*23),bound);
        fails([&]{(void)delta_track(loose,{{0,0,0},{1,0,0},0,100},{},{mode,1});},TrackingFailureReason::event_budget);
        fails([&]{(void)delta_track(loose,{{0,0,0},{1,0,0},0,100},{},{mode,0});},TrackingFailureReason::event_budget);
    }
    for(std::size_t level=0;level<snapshot.majorant().levels.size();++level) {
        auto bad=snapshot.majorant();bad.levels[level].maxima[0]=0.99f;
        fails([&]{TrackingSnapshot invalid(snapshot.scene(),grid,std::vector<float>(17*19*23,1),bad);},TrackingFailureReason::invalid_majorant);
    }
    auto bad=snapshot.majorant();bad.levels[0].maxima[0]=std::numeric_limits<float>::quiet_NaN();
    fails([&]{TrackingSnapshot invalid(snapshot.scene(),grid,std::vector<float>(17*19*23,1),bad);},TrackingFailureReason::invalid_majorant);
    auto mask_scene=scene_for(grid,1);mask_scene.cloud.base.enabled=true;mask_scene.cloud.base.height=0;
    mask_scene.cloud.cuts={{3,{0,1,0},{.5,.5,.5},1}};
    const TrackingSnapshot masked(mask_scene,grid,std::vector<float>(17*19*23,1));
    check(masked.density_at_world({0,-1,0})==0&&masked.density_at_world({0,1,0})==0&&masked.density_at_world({1,1,0})==1,"Snapshot must match preview hard masks");
    // Translation + rotation + nonuniform scale: a local x segment of length
    // four becomes eight world metres, and extinction remains per world metre.
    auto transformed_scene=scene_for(grid,1);transformed_scene.cloud.transform={{7,11,-13},{0,0,std::sqrt(.5),std::sqrt(.5)},{2,3,4}};
    const TrackingSnapshot transformed(transformed_scene,grid,std::vector<float>(17*19*23,1));
    const auto origin=local_to_world(transformed_scene.cloud.transform,{-3,0,0});
    const auto segments=tracking_intervals(transformed,{origin,{0,1,0},0,20},TrackingMode::local);
    check(std::abs(segments.front().entry-2)<1e-12&&std::abs(segments.back().exit-10)<1e-12,"TRS changed world-distance parameterization");
    check(std::abs(tracking_extinction(transformed,{origin,{0,1,0},0,20},5)-1)<1e-12,"TRS extinction changed units");
}
void rng_contract() {
    const TrackingRngKey base{0x100000011ULL,13,29,7,11};
    const double reference=tracking_uniform(base,31,0);
    check(reference>0&&reference<1&&reference==tracking_uniform(base,31,0),"RNG open/reproducible contract");
    auto changed=base;changed.seed=17;check(reference!=tracking_uniform(changed,31,0),"Seed high bits ignored");
    changed=base;++changed.pixel;check(reference!=tracking_uniform(changed,31,0),"Pixel ignored");
    changed=base;++changed.sample;check(reference!=tracking_uniform(changed,31,0),"Sample ignored");
    changed=base;++changed.bounce;check(reference!=tracking_uniform(changed,31,0),"Bounce ignored");
    changed=base;++changed.stream;check(reference!=tracking_uniform(changed,31,0),"Stream ignored");
    check(reference!=tracking_uniform(base,32,0)&&reference!=tracking_uniform(base,31,1),"Event/dimension ignored");
    check(reference!=tracking_uniform(base,0x10000001fULL,0),"Event high bits ignored");
    std::array<unsigned,16> bins{};double sum=0;
    for(std::uint64_t event=0;event<65536;++event) {
        const double u=tracking_uniform(base,event,0);check(u>0&&u<1,"RNG emitted closed endpoint");
        ++bins[std::size_t(u*16)];sum+=u;
    }
    check(std::abs(sum/65536-.5)<6/std::sqrt(12.*65536),"RNG mean outside uncertainty");
    for(auto n:bins)check(std::abs(double(n)-4096)<6*std::sqrt(65536.*(1./16)*(15./16)),"RNG bin outside uncertainty");
}
}
int main() {try {
    std::cout<<std::setprecision(10)<<"fixture,mode,seed,samples,expected_survival,survival,ci95_low,ci95_high,censored_mean,candidates,nulls,elapsed_ms\n";
    rng_contract();boundaries_and_failures();homogeneous();heterogeneous();
    std::cout<<"Delta tracking: immutable grid, hard masks, conservative bounds, global/local free flights, independent seeds, failures and world units passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
