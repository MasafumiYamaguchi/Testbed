#include "white/hdr_export.hpp"
#include "white/persistence.hpp"
#include "white/diagnostics.hpp"
#include <nlohmann/json.hpp>
#include <atomic>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif
namespace white {
void export_hdr(const HdrImage& image,const HdrMetadata& meta,const std::filesystem::path& destination,ExportFault fault) {
    if(!image.width||!image.height||image.width>8192||image.height>8192||image.rgba.size()!=size_t(image.width)*image.height*4)throw std::invalid_argument("Invalid HDR dimensions");
    for(size_t i=0;i<image.rgba.size();++i)if(!std::isfinite(image.rgba[i])||image.rgba[i]<0||(i%4==3&&image.rgba[i]>1))throw std::invalid_argument("Invalid HDR pixel");
    auto scene=nlohmann::json::parse(scene_json(meta.scene));
    if(destination.filename().empty()||std::filesystem::exists(destination))throw std::runtime_error("Export destination must be new");
    static std::atomic<unsigned> sequence{0};auto parent=destination.parent_path();if(parent.empty())parent=".";
    auto temporary=parent/(".white-hdr-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(sequence++));
    if(!std::filesystem::create_directory(temporary))throw std::runtime_error("Cannot reserve export directory");
    try {
        auto bytes=encode_exr(image);std::ofstream exr(temporary/"linear.exr",std::ios::binary);exr.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));exr.close();if(!exr)throw std::runtime_error("EXR write failed");
        // PPM deliberately uses the same exposure -> Reinhard -> sRGB sequence
        // as the shader. Top-to-bottom rows, RGB channels, no second gamma.
        std::ofstream display(temporary/"display.ppm",std::ios::binary);display<<"P6\n"<<image.width<<' '<<image.height<<"\n255\n";
        for(size_t i=0;i<image.rgba.size();++i)if(i%4!=3){double x=double(image.rgba[i]);if(meta.diagnostic_mode&&meta.diagnostic_mode!=4)x=std::min(1.,x/diagnostic_scale(meta.diagnostic_mode));else{x*=std::exp2(meta.scene.exposure_ev);x=x/(1+x);}x=x<=.0031308?12.92*x:1.055*std::pow(x,1/2.4)-.055;display.put(char(std::lround(x*255)));}
        display.close();if(!display)throw std::runtime_error("Display image write failed");
        nlohmann::json metadata={{"format_version",1},{"diagnostic_mode",meta.diagnostic_mode},{"diagnostic_name",diagnostic_name(meta.diagnostic_mode)},{"diagnostic_display_scale",diagnostic_scale(meta.diagnostic_mode)},{"commit",WHITE_COMMIT},{"frame",std::to_string(meta.frame)},{"revision",std::to_string(meta.revision)},{"width",image.width},{"height",image.height},{"view_steps",meta.view_steps},{"shadow_steps",meta.shadow_steps},{"samples_per_pixel",meta.samples_per_pixel},{"jitter_seed",42},{"jitter",meta.samples_per_pixel>1?"pixel footprint; no ray-step jitter":"fixed or single jittered sample"},{"cached_density",meta.cached},{"actual_density_extent",meta.density_extent},{"sun_tau_cache_resolution",meta.sun_cache_resolution},{"empty_space_skipping",meta.empty_skip},{"working_space","linear Rec.709 / D65"},{"row_order","top to bottom"},{"RGB","L_scatter + T * background; already composited"},{"A","transmittance T; NOT opacity; do not alpha composite RGB"},{"background_linear",{.015,.022,.035}},{"display_transform","exposure 2^EV -> Reinhard x/(1+x) -> sRGB"},{"scene",scene}};
        if(!meta.provenance_json.empty()){auto provenance=nlohmann::json::parse(meta.provenance_json);if(!provenance.is_object())throw std::invalid_argument("Reference provenance must be an object");metadata["jitter_seed"]=provenance.at("seed");metadata["jitter"]=provenance.at("pixel_jitter").get<bool>()?"pixel footprint; no ray-step jitter":"fixed pixel centers";metadata["reference"]=std::move(provenance);}
        std::ofstream sidecar(temporary/"metadata.json",std::ios::binary);if(meta.diagnostic_mode){metadata["RGB"]="Raw diagnostic values; see diagnostic_name. Not composited radiance.";metadata["display_transform"]="diagnostic raw / scale clamped then sRGB; scattering uses exposure/Reinhard";}sidecar<<metadata.dump(2)<<'\n';sidecar.close();if(!sidecar)throw std::runtime_error("Metadata write failed");
        if(fault==ExportFault::before_publish)throw std::runtime_error("Injected export failure");
#ifdef _WIN32
        if(!MoveFileExW(temporary.c_str(),destination.c_str(),MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Export publication failed");
#else
        if(syscall(SYS_renameat2,AT_FDCWD,temporary.c_str(),AT_FDCWD,destination.c_str(),RENAME_NOREPLACE)!=0)throw std::runtime_error("Export publication failed");
#endif
    }catch(...){std::error_code ignored;std::filesystem::remove_all(temporary,ignored);throw;}
}
}
