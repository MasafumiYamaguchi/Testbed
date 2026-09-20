#include "white/benchmark.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <vector>
namespace white {
EditTrack edit_track(std::string_view name){
    if(name=="density")return EditTrack::density;
    if(name=="camera")return EditTrack::camera;
    if(name=="exposure")return EditTrack::exposure;
    throw std::invalid_argument("Benchmark track must be density, camera or exposure");
}
Scene benchmark_scene(const Scene& baseline,EditTrack track,unsigned step){
    Scene scene=baseline;const double phase=(double(step)+1)*0.17;
    if(track==EditTrack::density){if(scene.cloud.cells.empty())throw std::invalid_argument("Density benchmark requires a cell");scene.cloud.cells[0].center.x+=2*std::sin(phase);}
    if(track==EditTrack::camera)scene.camera.position.x+=5*std::sin(phase);
    if(track==EditTrack::exposure)scene.exposure_ev+=(step%2==0?0.5:0.0);
    require_valid(scene);return scene;
}
double percentile(std::span<const double> samples,double q){
    if(samples.empty()||!std::isfinite(q)||q<=0||q>1)throw std::invalid_argument("Invalid percentile input");
    std::vector<double> sorted(samples.begin(),samples.end());
    for(double n:sorted)if(!std::isfinite(n)||n<0)throw std::invalid_argument("Invalid timing sample");
    std::sort(sorted.begin(),sorted.end());
    return sorted[std::min(sorted.size()-1,std::size_t(std::ceil(q*sorted.size()))-1)];
}
std::string benchmark_csv(std::span<const FrameMeasurement> rows){
    std::ostringstream out;out.imbue(std::locale::classic());out<<std::setprecision(12);
    out<<"sample,revision,record_cpu_elapsed_ms,submit_cpu_elapsed_ms,fence_wait_cpu_elapsed_ms,edit_to_submission_ms,edit_to_gpu_complete_ms,width,height,cached_density,hdr_updated\n";
    for(std::size_t i=0;i<rows.size();++i){const auto& r=rows[i];out<<i<<','<<r.revision<<','<<r.record_ms<<','<<r.submit_ms<<','<<r.fence_wait_ms<<','<<r.edit_to_submit_ms<<','<<r.edit_to_complete_ms<<','<<r.width<<','<<r.height<<','<<r.cached<<','<<r.hdr_updated<<'\n';}
    return out.str();
}
std::string benchmark_summary(std::span<const FrameMeasurement> rows){
    std::vector<double> submit,complete;unsigned cached=0,hdr=0;double work_ms=0;
    for(const auto& r:rows){submit.push_back(r.edit_to_submit_ms);complete.push_back(r.edit_to_complete_ms);cached+=r.cached;hdr+=r.hdr_updated;work_ms+=r.edit_to_complete_ms;}
    std::ostringstream out;out.imbue(std::locale::classic());out<<std::setprecision(9);
    out<<"benchmark_samples="<<rows.size()<<" edit_to_submission_p50_ms="<<percentile(submit,0.5)<<" edit_to_submission_p95_ms="<<percentile(submit,0.95)<<" edit_to_gpu_complete_p50_ms="<<percentile(complete,0.5)<<" edit_to_gpu_complete_p95_ms="<<percentile(complete,0.95)<<" serial_work_updates_per_second="<<(work_ms>0?1000*rows.size()/work_ms:0)<<" cached_samples="<<cached<<" direct_samples="<<rows.size()-cached<<" hdr_updates="<<hdr<<" gpu_timestamp_ms=unavailable scanout_latency_ms=unavailable\n";
    return out.str();
}
}
