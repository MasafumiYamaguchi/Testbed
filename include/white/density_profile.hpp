#pragma once
#include <cstddef>
#include <string>
#include <vector>

namespace white {
inline constexpr std::size_t max_altitude_density_knots=8;
inline constexpr double max_altitude_density_error=1e-4;
struct AltitudeDensityKnot {
    double t=0,scale=1;
    bool operator==(const AltitudeDensityKnot&)const=default;
};
// Independent local-height modulation, after geometric/noise/base/cut masks.
// Defaults preserve the previous density kernel exactly. Knots are normalized
// height, not world Y or current AABB coordinates; height must be positive.
struct AltitudeDensityProfile {
    bool enabled=false;
    double base=0,height=1;
    std::vector<AltitudeDensityKnot> knots{{0,1},{1,1}};
    bool operator==(const AltitudeDensityProfile&)const=default;
};
std::vector<std::string> validate_altitude_density(const AltitudeDensityProfile&);
// Additional modulation error budget for the packed float kernel, assuming
// local Y is within four float ULPs at the profile's largest coordinate.
// This does not bound independent ray/grid coordinate construction errors.
// Disabled/constant profiles have no altitude-dependent amplification (zero).
double altitude_density_error_bound(const AltitudeDensityProfile&);
class AltitudeDensityEvaluator {
public:
    explicit AltitudeDensityEvaluator(AltitudeDensityProfile={});
    double at(double local_y)const;
    double maximum()const{return maximum_;}
private:
    AltitudeDensityProfile profile_;
    double maximum_=1;
};
}
