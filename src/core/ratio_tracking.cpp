#include "white/ratio_tracking.hpp"
#include <cmath>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace white {
namespace {
[[noreturn]] void fail(const char* reason,const TrackingRngKey& key,
    std::uint64_t events,double t,double sigma,double bound,
    TrackingFailureReason category=TrackingFailureReason::numerical) {
    std::ostringstream message;
    message.precision(17);
    message<<"Ratio tracking "<<reason<<"; seed="<<key.seed
        <<" pixel="<<key.pixel<<" sample="<<key.sample
        <<" bounce="<<key.bounce<<" stream="<<key.stream
        <<" event="<<events<<" t="<<t<<" sigma="<<sigma
        <<" majorant="<<bound;
    throw TrackingFailure(category,message.str());
}
}

RatioTrackingResult ratio_track(const TrackingSnapshot& snapshot,
    const TrackingRay& ray,TrackingRngKey key,TrackingOptions options) {
    // Interval construction also validates rays that miss the volume or have
    // zero length; invalid inputs must not masquerade as transparent media.
    const auto intervals=tracking_intervals(snapshot,ray,options.mode);
    RatioTrackingResult result;
    double correction=0;
    for(const auto& interval:intervals) {
        ++result.intervals;
        const double bound=interval.majorant_extinction;
        if(!std::isfinite(bound)||bound<0||!std::isfinite(interval.entry)||
           !std::isfinite(interval.exit)||interval.exit<interval.entry)
            fail("invalid interval",key,result.events,interval.entry,0,bound);
        if(bound==0||interval.exit==interval.entry)continue;
        double t=interval.entry;
        while(t<interval.exit) {
            if(result.random_draws==std::numeric_limits<std::uint64_t>::max())
                fail("random counter exhausted",key,result.events,t,0,bound);
            const double uniform=tracking_uniform(key,result.random_draws++,2);
            if(!(uniform>0&&uniform<1))
                fail("invalid random variate",key,result.events,t,0,bound);
            const double flight=-std::log(uniform)/bound;
            // Compare lengths before addition. A very large finite flight (or
            // positive overflow for tiny extinction) simply leaves the interval.
            if(flight>=interval.exit-t)break;
            const double next=t+flight;
            if(!std::isfinite(flight)||!(next>t)||next>=interval.exit)
                fail("distance cannot progress at double precision",key,result.events,t,0,bound);
            t=next;
            if(result.events>=options.event_budget)
                fail("event budget exhausted; sample invalid",key,result.events,t,0,bound,TrackingFailureReason::event_budget);
            ++result.events;
            const double sigma=tracking_extinction(snapshot,ray,t);
            if(!std::isfinite(sigma)||sigma<0||sigma>bound)
                fail("majorant violation; sample invalid",key,result.events,t,sigma,bound,TrackingFailureReason::invalid_majorant);
            if(sigma==bound) {
                result.log_transmittance=-std::numeric_limits<double>::infinity();
                correction=0;
            } else if(sigma>0&&std::isfinite(result.log_transmittance)) {
                // log1p preserves very small extinction ratios. Compensated
                // accumulation reduces rounding across many small factors.
                const double term=std::log1p(-sigma/bound)-correction;
                const double next_log=result.log_transmittance+term;
                correction=(next_log-result.log_transmittance)-term;
                result.log_transmittance=next_log;
            }
            // Continue even after an exact zero or floating-point underflow:
            // later event-limit or majorant failures still invalidate the sample.
        }
    }
    result.transmittance=std::exp(result.log_transmittance);
    result.underflow=result.transmittance==0&&std::isfinite(result.log_transmittance);
    return result;
}
}
