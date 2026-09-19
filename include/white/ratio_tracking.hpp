#pragma once
#include "white/delta_tracking.hpp"

namespace white {
struct RatioTrackingResult {
    double transmittance=1;
    // Retained even when converting the final product to double underflows.
    // Negative infinity denotes an exactly zero factor, not a cutoff.
    double log_transmittance=0;
    std::uint64_t events=0,intervals=0,random_draws=0;
    bool underflow=false;
};

// Estimates T for exactly the immutable density/interpolation contract used by
// delta_track. RNG dimension 2 is reserved for ratio flights; delta uses 0/1.
// Event-budget, majorant and numerical failures throw and publish no estimate.
// There is no early termination threshold or uncorrected roulette.
RatioTrackingResult ratio_track(const TrackingSnapshot&,const TrackingRay&,
    TrackingRngKey,TrackingOptions={});
}
