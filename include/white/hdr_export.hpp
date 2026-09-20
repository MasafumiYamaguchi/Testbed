#pragma once
#include "white/document.hpp"
#include <filesystem>
#include <span>
#include <vector>
namespace white {
struct HdrImage {unsigned width=0,height=0;std::vector<float> rgba;};
std::vector<unsigned char> encode_exr(const HdrImage&);
HdrImage decode_exr(std::span<const unsigned char>);
struct HdrMetadata {Scene scene;std::uint64_t frame=0,revision=0;unsigned view_steps=0,shadow_steps=0;bool cached=false;unsigned sun_cache_resolution=0;bool empty_skip=false;unsigned samples_per_pixel=1;unsigned diagnostic_mode=0;};
enum class ExportFault {none,before_publish};
// New directory only. RGB is linear Rec.709 with fixed background already
// composited; A is transmittance, not opacity. Never alpha-composite again.
void export_hdr(const HdrImage&,const HdrMetadata&,const std::filesystem::path&,ExportFault=ExportFault::none);
}
