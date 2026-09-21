#include "white/reference.hpp"
#include "white/editor_geometry.hpp"
#include "white/persistence.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace white;
using Json=nlohmann::json;
constexpr std::array<std::uint64_t,4> seeds{17,42,99991,4294967313ULL};
constexpr unsigned coarse_view=512,coarse_shadow=64,fine_view=1024,fine_shadow=128;
// The fifth component keeps covariance between RGB channels: its variance is
// measured directly instead of incorrectly treating color channels as independent.
using Values=std::array<double,5>;
Values values(const ReferenceSample& r){return {r.radiance.x,r.radiance.y,r.radiance.z,r.transmittance,(r.radiance.x+r.radiance.y+r.radiance.z)/3};}
struct Moments {
    unsigned n=0;Values mean{},m2{};
    void add(const Values& v){++n;for(std::size_t k=0;k<v.size();++k){const double delta=v[k]-mean[k];mean[k]+=delta/n;m2[k]+=delta*(v[k]-mean[k]);}}
    Values estimator_variance()const{Values result{};if(n>1)for(std::size_t k=0;k<result.size();++k)result[k]=std::max(0.,m2[k]/(n-1)/n);return result;}
};
struct Estimate {Values mean{},variance{};};
struct Metrics {double bias=0,rmse=0,expected_mc_rmse=0,image_mean_standard_error=0,quadrature_rmse=0,quadrature_mean_absolute=0,maximum_absolute=0;};
unsigned integer(const char* text){unsigned result=0;const std::string s=text;const auto [end,error]=std::from_chars(s.data(),s.data()+s.size(),result);if(error!=std::errc{}||end!=s.data()+s.size())throw std::invalid_argument("Invalid unsigned argument");return result;}
void check(bool ok,const std::string& reason){if(!ok)throw std::runtime_error(reason);}
void write(const std::filesystem::path& path,const std::string& data){std::ofstream out(path,std::ios::binary);out<<data;out.close();check(bool(out),"Cannot write "+path.string());}
HdrImage image(const std::vector<Estimate>& pixels,unsigned width,unsigned height){HdrImage result{width,height,{}};result.rgba.reserve(pixels.size()*4);for(const auto& p:pixels)for(unsigned k=0;k<4;++k)result.rgba.push_back(float(p.mean[k]));return result;}
std::vector<Estimate> estimates(const std::vector<Moments>& pixels){std::vector<Estimate> result;result.reserve(pixels.size());for(const auto& p:pixels)result.push_back({p.mean,p.estimator_variance()});return result;}
Metrics compare(const std::vector<Estimate>& estimate,const std::vector<Estimate>& fine,const std::vector<Estimate>& coarse,unsigned channel){
    Metrics m;double square=0,variance=0,quad_square=0;
    for(std::size_t p=0;p<estimate.size();++p){const double delta=estimate[p].mean[channel]-fine[p].mean[channel],q=std::abs(fine[p].mean[channel]-coarse[p].mean[channel]);
        m.bias+=delta;square+=delta*delta;variance+=estimate[p].variance[channel];quad_square+=q*q;m.quadrature_mean_absolute+=q;m.maximum_absolute=std::max(m.maximum_absolute,std::abs(delta));}
    const double n=double(estimate.size());m.bias/=n;m.rmse=std::sqrt(square/n);m.expected_mc_rmse=std::sqrt(variance/n);m.image_mean_standard_error=std::sqrt(variance)/n;m.quadrature_rmse=std::sqrt(quad_square/n);m.quadrature_mean_absolute/=n;return m;
}
void gate(const Metrics& m,const std::string& name){
    // Image-level gates avoid treating thousands of correlated diagnostic
    // pixel checks as independent hypothesis tests. These are engineering
    // uncertainty envelopes, not simultaneous confidence guarantees.
    check(std::abs(m.bias)<=6*m.image_mean_standard_error+m.quadrature_mean_absolute+2e-5,name+" image mean differs beyond aggregate uncertainty and quadrature discrepancy");
    check(m.rmse<=3*m.expected_mc_rmse+m.quadrature_rmse+2e-4,name+" image RMSE exceeds the MC uncertainty/quadrature envelope");
}
void row(std::ostream& out,const std::string& fixture,const std::string& seed,unsigned spp,const char* channel,const Metrics& m){
    out<<fixture<<','<<seed<<','<<spp<<','<<channel<<','<<m.bias<<','<<m.rmse<<','<<m.expected_mc_rmse<<','<<m.image_mean_standard_error<<','<<m.quadrature_rmse<<','<<m.quadrature_mean_absolute<<','<<m.maximum_absolute<<'\n';
}
void pixel_csv(const std::filesystem::path& path,const std::vector<Estimate>& estimate,const std::vector<Estimate>& fine,const std::vector<Estimate>& coarse,unsigned width){
    std::ofstream out(path);out<<std::setprecision(17)<<"x,y,mean_R,mean_G,mean_B,mean_T,variance_mean_R,variance_mean_G,variance_mean_B,variance_mean_T,mean_RGB_average,variance_mean_RGB_average,fine_RGB_average,coarse_RGB_average,fine_T,coarse_T\n";
    for(std::size_t i=0;i<estimate.size();++i){out<<i%width<<','<<i/width;for(unsigned k=0;k<4;++k)out<<','<<estimate[i].mean[k];for(unsigned k=0;k<4;++k)out<<','<<estimate[i].variance[k];out<<','<<estimate[i].mean[4]<<','<<estimate[i].variance[4]<<','<<fine[i].mean[4]<<','<<coarse[i].mean[4]<<','<<fine[i].mean[3]<<','<<coarse[i].mean[3]<<'\n';}
    out.close();check(bool(out),"Pixel CSV write failed");
}
void export_image(const std::filesystem::path& directory,const TrackingSnapshot& snapshot,const std::vector<Estimate>& pixels,unsigned width,unsigned height,unsigned spp,std::uint64_t seed,const std::string& integrator,unsigned view=0,unsigned shadow=0){
    HdrMetadata meta;meta.scene=snapshot.scene();meta.samples_per_pixel=spp;meta.cached=true;meta.density_extent=snapshot.grid().extent;meta.view_steps=view;meta.shadow_steps=shadow;
    meta.provenance_json=Json{{"integrator",integrator},{"seed",std::to_string(seed)},{"pixel_jitter",false},{"complete",true},{"mode","single scattering"},{"comparison_scope","same saved R32F grid and pixel-center camera rays; no sun cache or preview multiple-scattering approximation"}}.dump();
    export_hdr(image(pixels,width,height),meta,directory);
}
}

int main(int argc,char** argv){try{
    if(argc<3||argc>7){std::cerr<<"white_reference_image_tests FIXTURE_ROOT NEW_OUTPUT [spp=8192] [width=16] [height=9] [grid=32]\n";return 2;}
    const std::filesystem::path fixture_root=argv[1],output=argv[2];
    const unsigned samples=argc>3?integer(argv[3]):8192,width=argc>4?integer(argv[4]):16,height=argc>5?integer(argv[5]):9,resolution=argc>6?integer(argv[6]):32;
    check(samples>=1024&&samples<=8192&&width>=4&&width<=64&&height>=4&&height<=36&&resolution>=8&&resolution<=128,"Comparison dimensions/samples outside bounded contract");
    check(std::uint64_t(width)*height*samples*seeds.size()*4<=67108864,"Comparison exceeds 67 million total path budget");
    check(!std::filesystem::exists(output),"Comparison output must be a new directory");std::filesystem::create_directories(output);
    const std::array<std::pair<const char*,const char*>,4> cases{{{"fusion","gate/fusion.white.json"},{"cut","gate/cut.white.json"},{"detail-17","gate/detail-17.white.json"},{"low-sun","sun/low-sun.white.json"}}};
    std::ofstream summary(output/"comparison.csv");summary<<std::setprecision(17)<<"fixture,seed,samples_per_seed,channel,image_bias,image_rmse,expected_mc_rmse,image_mean_standard_error,quadrature_rmse,quadrature_mean_absolute,maximum_absolute\n";
    Json protocol={{"format_version",1},{"fixtures",Json::array()},{"width",width},{"height",height},{"density_extent",{resolution,resolution,resolution}},{"seeds",Json::array()},{"samples_per_seed",samples},{"checkpoint_samples_per_seed",1024},{"pixel_footprint","exact pixel center; normalized ((x+.5)/width,(y+.5)/height), aspect width/height; no jitter"},{"mode","single scattering"},{"coarse_quadrature",{{"view_steps",coarse_view},{"shadow_steps",coarse_shadow}}},{"fine_quadrature",{{"view_steps",fine_view},{"shadow_steps",fine_shadow}}},{"ray_extent","primary near/far camera clipping; sun visibility to frozen medium exit"},{"majorant","local conservative trilinear-halo bricks"},{"event_limit_per_call",100000},{"gates","Image mean within 6 aggregate SE + mean absolute quadrature discrepancy + 2e-5. Image RMSE within 3 expected MC RMSE + quadrature RMSE + 2e-4. Engineering envelopes, not simultaneous confidence guarantees."},{"pixel_statistics","Per-pixel CSV reports unbiased sample variance divided by sample count, including directly measured RGB-average covariance. No per-pixel significance gate."},{"density_model","One immutable CPU-baked R32F voxel-center array shared by raymarch and MC; trilinear clamp, hard base/cut/envelope mask after interpolation."},{"bake_error","Excluded from the MC-versus-raymarch comparison because both consume the identical frozen array. Separate procedural density error is not inferred from this agreement."},{"quadrature_error","The difference between 512x64 and 1024x128 midpoint integrations is measured separately; it is a refinement diagnostic, not a rigorous integration-error bound."},{"native_comparison","No GPU equality claimed here. A later native comparison must match camera pixels and quality, and account separately for CPU/GPU density-bake roundoff or load this exact saved R32F array."}};
    for(auto seed:seeds)protocol["seeds"].push_back(std::to_string(seed));
    const auto started=std::chrono::steady_clock::now();std::uint64_t total_paths=0,total_events=0;
    for(const auto& [name,path]:cases){
        const auto scene=read_scene(fixture_root/path);const auto frozen=bake_reference_snapshot(scene,resolution);const auto destination=output/name;std::filesystem::create_directory(destination);
        write(destination/"scene.white.json",scene_json(scene));
        // Explicit little-endian raw floats keep the exact comparison medium
        // reusable without relying on procedural re-evaluation or text rounding.
        std::ofstream raw(destination/"density-r32f-le.bin",std::ios::binary);std::uint64_t hash=14695981039346656037ULL;
        for(float value:frozen.density()){const auto bits=std::bit_cast<std::uint32_t>(value);for(unsigned b=0;b<4;++b){const auto byte=static_cast<unsigned char>(bits>>(8*b));raw.put(char(byte));hash^=byte;hash*=1099511628211ULL;}}raw.close();check(bool(raw),"Density snapshot write failed");
        std::vector<TrackingRay> rays;std::vector<Estimate> coarse,fine;const std::size_t count=std::size_t(width)*height;rays.reserve(count);coarse.reserve(count);fine.reserve(count);
        for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){const auto ray=camera_ray(scene.camera,(x+.5)/width,(y+.5)/height,double(width)/height);rays.push_back({ray.origin,ray.direction,scene.camera.near_plane,scene.camera.far_plane});
            coarse.push_back({values(reference_raymarch(frozen,rays.back(),coarse_view,coarse_shadow)),{}});fine.push_back({values(reference_raymarch(frozen,rays.back(),fine_view,fine_shadow)),{}});}
        export_image(destination/"midpoint-512x64",frozen,coarse,width,height,1,0,"deterministic midpoint quadrature",coarse_view,coarse_shadow);
        export_image(destination/"midpoint-1024x128",frozen,fine,width,height,1,0,"deterministic midpoint quadrature",fine_view,fine_shadow);
        std::vector<Estimate> combined(count),combined_low(count);Json fixture={{"name",name},{"source_recipe",path},{"density_values_fnv1a64",std::to_string(hash)},{"density_values_file","density-r32f-le.bin"},{"pixel_count",count},{"grid_local_bounds",{{scene.cloud.envelope.min.x,scene.cloud.envelope.min.y,scene.cloud.envelope.min.z},{scene.cloud.envelope.max.x,scene.cloud.envelope.max.y,scene.cloud.envelope.max.z}}},{"attempted_paths",0},{"tracking_events",0}};
        for(const auto seed:seeds){
            std::vector<Moments> moments(count);ReferenceSettings settings;settings.width=width;settings.height=height;settings.samples=samples;settings.mode=ReferenceMode::single_scattering;settings.pixel_jitter=false;settings.seed=seed;
            for(unsigned sample=0;sample<samples;++sample){
                for(std::size_t p=0;p<count;++p){const auto result=reference_sample(frozen,rays[p],settings,{seed,unsigned(p),sample,0,0});
                    check(result.status==ReferenceStatus::complete,"Incomplete MC path: "+std::string(name)+" seed="+std::to_string(seed)+" pixel="+std::to_string(p)+" sample="+std::to_string(sample));moments[p].add(values(result));++total_paths;total_events+=result.tracking_events;fixture["attempted_paths"]=fixture["attempted_paths"].get<std::uint64_t>()+1;fixture["tracking_events"]=fixture["tracking_events"].get<std::uint64_t>()+result.tracking_events;}
                if(sample+1==1024||sample+1==samples){const auto estimate=estimates(moments);for(auto channel:{4u,3u})row(summary,name,std::to_string(seed),sample+1,channel==4?"RGB_average":"T",compare(estimate,fine,coarse,channel));
                    auto& combined_at=sample+1==samples?combined:combined_low;for(std::size_t p=0;p<count;++p)for(unsigned k=0;k<5;++k){combined_at[p].mean[k]+=estimate[p].mean[k]/seeds.size();combined_at[p].variance[k]+=estimate[p].variance[k]/(seeds.size()*seeds.size());}}
            }
            const auto estimate=estimates(moments);const auto stem="seed-"+std::to_string(seed);pixel_csv(destination/(stem+"-pixels.csv"),estimate,fine,coarse,width);export_image(destination/stem,frozen,estimate,width,height,samples,seed,"independent-seed delta + ratio single-scattering MC");
        }
        for(auto channel:{4u,3u}){const auto metrics=compare(combined,fine,coarse,channel);row(summary,name,"combined-four-seeds",samples,channel==4?"RGB_average":"T",metrics);gate(metrics,std::string(name)+(channel==4?" RGB":" T"));if(samples>1024)row(summary,name,"combined-four-seeds",1024,channel==4?"RGB_average":"T",compare(combined_low,fine,coarse,channel));
            std::cout<<std::setprecision(10)<<name<<' '<<(channel==4?"RGB":"T")<<" rmse="<<metrics.rmse<<" mc_expected="<<metrics.expected_mc_rmse<<" quadrature="<<metrics.quadrature_rmse<<" bias="<<metrics.bias<<" aggregate_SE="<<metrics.image_mean_standard_error<<'\n';}
        pixel_csv(destination/"combined-pixels.csv",combined,fine,coarse,width);protocol["fixtures"].push_back(std::move(fixture));
    }
    summary.close();check(bool(summary),"Summary CSV write failed");protocol["all_paths_complete"]=true;protocol["attempted_paths"]=total_paths;protocol["tracking_events"]=total_events;protocol["elapsed_seconds"]=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
    write(output/"protocol.json",protocol.dump(2)+"\n");std::cout<<"same_grid_image_cases=4 seeds=4 paths="<<total_paths<<" incomplete=0 elapsed_seconds="<<protocol["elapsed_seconds"]<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<"Reference image comparison failed: "<<e.what()<<'\n';return 1;}}
