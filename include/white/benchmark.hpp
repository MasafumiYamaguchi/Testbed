#pragma once
#include "white/document.hpp"
#include <span>
#include <string>
#include <string_view>
namespace white {
enum class EditTrack {density,camera,exposure};
EditTrack edit_track(std::string_view);
Scene benchmark_scene(const Scene& baseline,EditTrack,unsigned step);
// Nearest-rank percentile; finite nonnegative samples and 0 < q <= 1.
double percentile(std::span<const double> samples,double q);
struct FrameMeasurement {
    std::uint64_t revision=0;
    double record_ms=0,submit_ms=0,fence_wait_ms=0,edit_to_submit_ms=0,edit_to_complete_ms=0;
    unsigned width=0,height=0;
    bool cached=false,hdr_updated=false;
};
std::string benchmark_csv(std::span<const FrameMeasurement>);
std::string benchmark_summary(std::span<const FrameMeasurement>);
}
