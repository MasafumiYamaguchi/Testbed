#include "white/ratio_tracking.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>

namespace {
using namespace white;
void check(bool ok,const std::string& what) {
    if(!ok)throw std::runtime_error("Ratio contract: "+what);
}
template<class F> void rejects(F function,const char* what) {
    bool failed=false;
    try {function();}catch(const std::exception&) {failed=true;}
    check(failed,what);
}
Scene scene_for(GridLayout grid,double extinction=1) {
    Scene scene;
    scene.cloud.envelope=grid.local_bounds;
    scene.cloud.base.enabled=false;
    scene.cloud.optics.extinction_scale=extinction;
    return scene;
}
std::vector<float> rows(GridLayout grid,const std::vector<float>& row) {
    std::vector<float> values(std::size_t(grid.extent[0])*grid.extent[1]*grid.extent[2]);
    for(std::size_t i=0;i<values.size();++i)values[i]=row[i%row.size()];
    return values;
}
struct Statistics {
    std::uint64_t samples=0,events=0,draws=0;
    double mean=0,m2=0;
    void add(const RatioTrackingResult& value) {
        check(std::isfinite(value.transmittance)&&value.transmittance>=0&&value.transmittance<=1,
            "finite nonnegative bounded T");
        ++samples;
        const double difference=value.transmittance-mean;
        mean+=difference/double(samples);
        m2+=difference*(value.transmittance-mean);
        events+=value.events;draws+=value.random_draws;
    }
    double variance()const {return samples>1?m2/double(samples-1):0;}
    double error()const {return std::sqrt(variance()/double(samples));}
};
Statistics measure(const char* fixture,const TrackingSnapshot& snapshot,const TrackingRay& ray,
    TrackingMode mode,std::uint64_t seed,std::uint32_t count,double reference,
    double quadrature_error=0) {
    Statistics statistics;
    for(std::uint32_t sample=0;sample<count;++sample)
        statistics.add(ratio_track(snapshot,ray,{seed,37,sample,2,91},{mode,1000000}));
    std::cout<<fixture<<','<<(mode==TrackingMode::global?"global":"local")<<','<<seed
        <<','<<count<<','<<reference<<','<<statistics.mean<<','<<statistics.variance()
        <<','<<statistics.error()<<','<<statistics.events<<','<<statistics.draws
        <<','<<quadrature_error<<'\n';
    // Statistical tolerance and deterministic quadrature discrepancy are
    // separate columns; neither is disguised as floating-point roundoff.
    check(std::abs(statistics.mean-reference)<=6*statistics.error()+quadrature_error+2e-5,
        std::string(fixture)+" mean outside six standard errors; seed="+std::to_string(seed)+
        " samples="+std::to_string(count));
    return statistics;
}
double march(const TrackingSnapshot& snapshot,const TrackingRay& ray,unsigned count) {
    const double ds=(ray.end-ray.begin)/count;
    double tau=0;
    for(unsigned i=0;i<count;++i)
        tau+=tracking_extinction(snapshot,ray,ray.begin+(i+.5)*ds)*ds;
    return std::exp(-tau);
}
}

int main() {try {
    using namespace white;
    std::cout<<std::setprecision(17)
        <<"fixture,mode,seed,samples,reference_T,mean_T,sample_variance,standard_error,events,random_draws,quadrature_delta\n";
    const GridLayout grid{{{0,-2,-3},{32,2,3}},{32,3,3}};
    const TrackingRay ray{{-1,0,0},{1,0,0},0,34};
    const auto scene=scene_for(grid,.125);
    const TrackingSnapshot vacuum(scene,grid,std::vector<float>(32*3*3));
    for(auto mode:{TrackingMode::global,TrackingMode::local}) {
        auto result=ratio_track(vacuum,ray,{}, {mode,0});
        check(result.transmittance==1&&result.log_transmittance==0&&result.events==0&&result.random_draws==0,
            "vacuum T=1 with no events or draws");
    }

    // The main ray sees density 1. An off-ray voxel raises global M, creating
    // nontrivial ratio weights while leaving the analytical line integral fixed.
    std::vector<float> homogeneous(32*3*3,1);
    homogeneous[0]=4;
    const TrackingSnapshot slab(scene,grid,homogeneous);
    double slab_global_events=0,slab_local_events=0;
    for(auto mode:{TrackingMode::global,TrackingMode::local})
        for(std::uint64_t seed:{17ull,42ull,0x100000011ull})
            for(std::uint32_t count:{2048u,16384u}) {
                auto statistics=measure("homogeneous",slab,ray,mode,seed,count,std::exp(-4.));
                if(count==16384) {
                    if(mode==TrackingMode::global)slab_global_events+=statistics.events;
                    else slab_local_events+=statistics.events;
                }
            }
    check(slab_local_events<slab_global_events*.7,"local majorants reduce events in the sparse off-ray fixture");

    // Piecewise constant VOXELS become piecewise linear density at transitions.
    // Over the complete x extent, the exact clamped-trilinear integral is the
    // sum of the voxel values times the voxel width, including both half cells.
    std::vector<float> pieces(32);
    for(unsigned x=0;x<32;++x)pieces[x]=x<8?0.f:x<16?.25f:x<24?1.f:0.f;
    const TrackingSnapshot piecewise(scene,grid,rows(grid,pieces));
    const double piecewise_t=std::exp(-std::accumulate(pieces.begin(),pieces.end(),0.)*.125);
    for(auto mode:{TrackingMode::global,TrackingMode::local})
        for(std::uint64_t seed:{17ull,42ull,0x100000011ull})
            for(std::uint32_t count:{2048u,16384u})
                measure("piecewise-voxel-with-exact-linear-transition",piecewise,ray,mode,seed,count,piecewise_t);

    // Noncubic, heterogeneous, oblique ray: an independently refined midpoint
    // integral provides a reference and an explicit discretization discrepancy.
    const GridLayout varied_grid{{{-3,-2,-1},{5,4,3}},{17,11,9}};
    std::vector<float> varied(17*11*9);
    for(unsigned z=0;z<9;++z)for(unsigned y=0;y<11;++y)for(unsigned x=0;x<17;++x)
        varied[(z*11+y)*17+x]=float(.05+.4*std::pow(std::sin(x*.27+y*.19-z*.13),2));
    const TrackingSnapshot varied_medium(scene_for(varied_grid,.6),varied_grid,varied);
    Vec3 direction{1,.23,.11};direction=direction*(1/std::sqrt(dot(direction,direction)));
    const TrackingRay oblique{{-4,-.3,.1},direction,0,12};
    const double coarse=march(varied_medium,oblique,32768),fine=march(varied_medium,oblique,65536);
    check(std::abs(coarse-fine)<1e-5,"refined independent midpoint reference converges");
    for(auto mode:{TrackingMode::global,TrackingMode::local})
        for(std::uint64_t seed:{17ull,42ull,0x100000011ull})
            for(std::uint32_t count:{2048u,16384u})
                measure("oblique-trilinear",varied_medium,oblique,mode,seed,count,fine,std::abs(fine-coarse));

    for(auto mode:{TrackingMode::global,TrackingMode::local}) {
        auto zero_ray=ray;zero_ray.begin=7;zero_ray.end=7;
        const auto zero=ratio_track(slab,zero_ray,{},{mode,0});
        check(zero.transmittance==1&&zero.events==0&&zero.random_draws==0,"zero length returns exactly one");
        const TrackingRay miss{{-1,20,20},{1,0,0},0,34};
        check(ratio_track(slab,miss,{},{mode,0}).transmittance==1,"miss returns one");
        const TrackingRay reverse{{33,0,0},{-1,0,0},0,34};
        measure("reverse-boundaries",piecewise,reverse,mode,42,16384,piecewise_t);
        const TrackingRay boundary{{16,0,0},{1,0,0},0,16};
        // Integral over x=[16,32]: seven full unit cells, two halves, and
        // quarter-transition contributions from the discontinuity at x=16.
        const double boundary_t=march(piecewise,boundary,32768);
        measure("exact-brick-start",piecewise,boundary,mode,42,16384,boundary_t);
    }

    // Loose M=2, actual sigma=1 over 1000 metres. Finite log T remains available
    // after the double-valued product underflows. Continuing all events is
    // observable: thousands of candidates, not a threshold-dependent cutoff.
    const GridLayout thick_grid{{{0,-1,-1},{1000,1,1}},{8,3,3}};
    auto thick_values=std::vector<float>(8*3*3,1);thick_values[0]=2;
    const TrackingSnapshot thick(scene_for(thick_grid),thick_grid,thick_values);
    const TrackingRay thick_ray{{0,0,0},{1,0,0},0,1000};
    for(auto mode:{TrackingMode::global,TrackingMode::local})for(std::uint64_t seed:{17ull,42ull,0x100000011ull}) {
        const auto value=ratio_track(thick,thick_ray,{seed},{mode,100000});
        check(value.transmittance==0&&value.underflow&&std::isfinite(value.log_transmittance)&&
            value.log_transmittance<-1000&&value.events>1500,"thick-medium underflow is reported without NaN or early cutoff");
        std::cout<<"thick-log,"<<(mode==TrackingMode::global?"global":"local")<<','<<seed
            <<",1,0,"<<value.transmittance<<",0,0,"<<value.events<<','<<value.random_draws<<",0 log_T="<<value.log_transmittance<<'\n';
        bool budget_failed=false;
        try {ratio_track(thick,thick_ray,{seed},{mode,4});}
        catch(const TrackingFailure& failure) {
            budget_failed=failure.reason()==TrackingFailureReason::event_budget&&
                std::string(failure.what()).find("sample invalid")!=std::string::npos&&
                std::string(failure.what()).find("seed="+std::to_string(seed))!=std::string::npos;
        }
        check(budget_failed,"event cap invalidates the whole estimate with typed, reproducible diagnosis");
    }

    // Exact reproducibility and distinct collision/light dimensions. All key
    // components must participate, including the high 32 seed bits.
    TrackingRngKey key{17,3,5,7,11};
    const auto first=ratio_track(slab,ray,key),second=ratio_track(slab,ray,key);
    check(first.transmittance==second.transmittance&&first.events==second.events&&first.random_draws==second.random_draws,
        "same immutable input/key reproduces ratio estimate exactly");
    for(std::uint64_t event=0;event<128;++event) {
        const double ratio=tracking_uniform(key,event,2);
        check(ratio>0&&ratio<1&&ratio!=tracking_uniform(key,event,0)&&ratio!=tracking_uniform(key,event,1),
            "ratio/collision random dimensions are distinct and open interval");
    }

    auto invalid=build_majorant(grid,homogeneous);
    invalid.levels.front().maxima[0]=0;
    rejects([&]{TrackingSnapshot bad(scene,grid,homogeneous,invalid);},"underestimated local majorant fails before sampling");
    invalid=build_majorant(grid,homogeneous);invalid.levels.back().maxima[0]=0;
    rejects([&]{TrackingSnapshot bad(scene,grid,homogeneous,invalid);},"underestimated global majorant fails before sampling");
    auto invalid_ray=ray;invalid_ray.direction_world={0,0,0};
    rejects([&]{ratio_track(vacuum,invalid_ray,{});},"invalid vacuum ray cannot bypass validation");
    invalid_ray=ray;invalid_ray.begin=std::numeric_limits<double>::quiet_NaN();
    rejects([&]{ratio_track(slab,invalid_ray,{});},"nonfinite ray rejected");
    std::cout<<"Ratio tracking analytic means, variance, events, independent quadrature, boundaries, log underflow and failure checks passed\n";
    return 0;
}catch(const std::exception& error) {
    std::cerr<<error.what()<<'\n';
    return 1;
}}
