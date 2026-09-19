#include "white/preview_approx.hpp"
#include "white/reference.hpp"
#include "white/optics.hpp"
#include "white/phase.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <charconv>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
using namespace white;
namespace {
struct Moments {
    unsigned n=0;double mean=0,m2=0;
    void add(double value){++n;double d=value-mean;mean+=d/n;m2+=d*(value-mean);}
    double stderr()const{return n>1?std::sqrt(m2/(n-1)/n):0;}
};
// A deterministic central ray in a homogeneous box. The sun and view are
// parallel or antiparallel, so shadow optical depth is analytic and does not
// introduce shadow-cache/quadrature errors into the approximation comparison.
double approximate_ray(double tau,double albedo,double g,double cosine,unsigned steps) {
    double L=0,T=1;const double dt=2./steps,sigma=tau/2;
    for(unsigned i=0;i<steps;++i) {
        const double z=-1+(i+.5)*dt;
        const double tau_sun=sigma*(cosine>0?1-z:z+1);
        const auto source=preview_approx_source(tau_sun,albedo,g,cosine,1,{true,.5});
        const auto step=integrate_homogeneous(sigma,dt,1,source.total());
        L+=T*step.radiance;T*=step.transmittance;
    }
    return L+std::exp(-tau)*reference_background.x;
}
}
int main(int argc,char** argv) {
    // Fixed before any MC evaluation. Optional count changes uncertainty only.
    unsigned samples=4096;
    if(argc>2)return 2;
    if(argc==2) {
        const std::string input=argv[1];
        const auto parsed=std::from_chars(input.data(),input.data()+input.size(),samples);
        if(parsed.ec!=std::errc{}||parsed.ptr!=input.data()+input.size())return 2;
    }
    if(samples<256||samples>1000000)return 2;
    constexpr std::array<std::uint64_t,4> seeds{17ull,42ull,0x100000011ull,99991ull};
    unsigned failures=0;double off_squared=0,on_squared=0;unsigned rows=0,improved=0;
    unsigned resolved_improved=0,resolved_worse=0;
    std::cout<<std::setprecision(12);
    std::cout<<"set,tau,albedo,g,cosine,samples,single_hdr,approx_hdr,mc_hdr,mc_standard_error,single_abs_error,approx_abs_error,approx_error_over_mc_se,approx_quadrature_delta,seed_spread,elapsed_ms,mc_minus_model_midpoint_over_se,comparison_at_6se,seed17_mean,seed17_se,seed42_mean,seed42_se,seed4294967313_mean,seed4294967313_se,seed99991_mean,seed99991_se\n";
    for(unsigned set=0;set<2;++set)
    for(unsigned thickness=0;thickness<2;++thickness)
    for(unsigned a=0;a<3;++a)
    for(double mu:{-1.,1.}) {
        const double tau=set?(thickness?7:.35):(thickness?5:.2);
        const double albedo=(set?std::array<double,3>{.35,.9,.995}:std::array<double,3>{.2,.8,.98})[a];
        const double g=set?.3:.7;
        Scene scene;scene.cloud.base.enabled=false;scene.cloud.cuts.clear();
        scene.cloud.envelope=set?Bounds{{-.75,-.5,-1},{.75,.5,1}}:Bounds{{-1,-1,-1},{1,1,1}};
        scene.cloud.optics={tau/2,albedo,g};scene.sun.direction_to_light={0,0,mu};scene.sun.irradiance={1,1,1};
        const GridLayout grid{scene.cloud.envelope,{8,8,8}};
        TrackingSnapshot snapshot(scene,grid,std::vector<float>(512,1));
        const TrackingRay ray{{0,0,-3},{0,0,1},0,10};
        ReferenceSettings settings;settings.mode=ReferenceMode::multiple_scattering;
        settings.bounce_limit=256;settings.event_limit=1000000;
        const double direct_factor=mu>0?tau*std::exp(-tau):-.5*std::expm1(-2*tau);
        const double single=albedo*hg_phase(mu,g)*direct_factor+std::exp(-tau)*reference_background.x;
        const double approx=approximate_ray(tau,albedo,g,mu,2048);
        const double quadrature=std::abs(approx-approximate_ray(tau,albedo,g,mu,1024));
        Moments pooled;std::array<Moments,4> seed_moments;
        double seed_min=std::numeric_limits<double>::infinity(),seed_max=0;
        const auto start=std::chrono::steady_clock::now();
        for(unsigned seed_index=0;seed_index<seeds.size();++seed_index) {
            auto& per_seed=seed_moments[seed_index];
            for(unsigned s=0;s<samples;++s) {
                auto result=reference_sample(snapshot,ray,settings,{seeds[seed_index],0,s,0,0});
                if(result.status!=ReferenceStatus::complete||!std::isfinite(result.radiance.x)) {
                    ++failures;std::cerr<<"reference incomplete set="<<set<<" tau="<<tau<<" albedo="<<albedo<<" status="<<unsigned(result.status)<<'\n';
                    continue;
                }
                pooled.add(result.radiance.x);per_seed.add(result.radiance.x);
            }
            seed_min=std::min(seed_min,per_seed.mean);seed_max=std::max(seed_max,per_seed.mean);
        }
        const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        const double off_error=std::abs(single-pooled.mean),on_error=std::abs(approx-pooled.mean),se=pooled.stderr();
        off_squared+=off_error*off_error;on_squared+=on_error*on_error;++rows;improved+=on_error<off_error;
        // Since ON >= OFF, its absolute error is lower precisely when the true
        // mean exceeds their midpoint. Use a deliberately broad +/-6 estimated
        // SE band to avoid calling a noise-level difference an improvement.
        // This is a descriptive sampling diagnostic, not a confidence guarantee
        // for all scenes or a family-wise hypothesis test.
        const double midpoint_z=se>0?(pooled.mean-(single+approx)*.5)/se:0;
        const char* verdict="unresolved";
        if(midpoint_z>6){verdict="improved";++resolved_improved;}
        else if(midpoint_z< -6){verdict="worse";++resolved_worse;}
        std::cout<<(set?"held_out":"predeclared")<<','<<tau<<','<<albedo<<','<<g<<','<<mu<<','<<pooled.n<<','<<single<<','<<approx<<','<<pooled.mean<<','<<se<<','<<off_error<<','<<on_error<<','<<(se>0?on_error/se:0)<<','<<quadrature<<','<<seed_max-seed_min<<','<<elapsed<<','<<midpoint_z<<','<<verdict;
        for(const auto& seed:seed_moments)std::cout<<','<<seed.mean<<','<<seed.stderr();
        std::cout<<'\n';
        if(pooled.n!=samples*4||quadrature>1e-5)++failures;
    }
    std::cerr<<std::setprecision(12)<<"comparison_rows="<<rows<<" point_estimate_improved="<<improved<<" resolved_improved_6se="<<resolved_improved<<" resolved_worse_6se="<<resolved_worse<<" unresolved_6se="<<rows-resolved_improved-resolved_worse<<" single_rmse="<<std::sqrt(off_squared/rows)<<" approx_rmse="<<std::sqrt(on_squared/rows)<<" incomplete_or_invalid="<<failures<<'\n';
    // Improvement is deliberately not a passing criterion. A worse candidate
    // must remain measurable so the adoption decision can reject it honestly.
    return failures?1:0;
}
