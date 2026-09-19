#include "white/density_profile.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace white {
namespace {
// Largest adjacent float spacing at this magnitude. Using both neighbors also
// covers a binade boundary, where the spacing changes by a factor of two.
double float_ulp(double magnitude) {
    const float value=float(std::abs(magnitude));
    if(!std::isfinite(value))return std::numeric_limits<double>::infinity();
    return std::max(double(std::nextafter(value,std::numeric_limits<float>::infinity()))-value,
                    double(value)-std::nextafter(value,-std::numeric_limits<float>::infinity()));
}
}
double altitude_density_error_bound(const AltitudeDensityProfile& profile) {
    if(!profile.enabled)return 0;
    if(profile.knots.size()<2||!std::isfinite(profile.base)||!std::isfinite(profile.height)||profile.height<=0)
        return std::numeric_limits<double>::infinity();
    double slope=0,knot_error=0,scale_error=0;
    for(std::size_t i=0;i<profile.knots.size();++i){const auto& knot=profile.knots[i];
        if(!std::isfinite(knot.t)||!std::isfinite(knot.scale)||knot.t<0||knot.t>1||knot.scale<0||knot.scale>1)
            return std::numeric_limits<double>::infinity();
        knot_error=std::max(knot_error,std::abs(double(float(knot.t))-knot.t));
        scale_error=std::max(scale_error,std::abs(double(float(knot.scale))-knot.scale));
        if(i){const auto& previous=profile.knots[i-1];const double span=knot.t-previous.t;
            if(span<=0)return std::numeric_limits<double>::infinity();
            slope=std::max(slope,std::abs(knot.scale-previous.scale)/span);
        }
    }
    // Constant factors retain only ordinary scalar rounding, already covered
    // by the existing density-kernel tolerance; no position error is amplified.
    if(slope==0)return 0;
    const double packed_height=float(profile.height);
    if(!std::isfinite(packed_height)||packed_height<=0)return std::numeric_limits<double>::infinity();
    const double magnitude=std::max(std::abs(profile.base),std::abs(profile.base+profile.height));
    const double position_error=4*float_ulp(magnitude);
    const double base_error=std::abs(double(float(profile.base))-profile.base);
    const double height_error=std::abs(packed_height-profile.height);
    const double subtract_error=.5*float_ulp(profile.height+position_error+base_error);
    const double quotient_error=(position_error+base_error+subtract_error+height_error)/packed_height;
    const double normalize_error=quotient_error+.5*float_ulp(1+quotient_error);
    // Perturbing ordered knot abscissae shifts the continuous linear profile
    // by at most the largest packing error. Its maximum slope bounds both
    // this shift and normalization error, including clamped endpoints.
    // Eight float epsilons cover subtract/divide/lerp arithmetic on [0,1].
    return std::min(1.,slope*(normalize_error+knot_error)+scale_error+
                        8*std::numeric_limits<float>::epsilon());
}
std::vector<std::string> validate_altitude_density(const AltitudeDensityProfile& profile){
    std::vector<std::string> errors;auto check=[&](bool ok,const char* text){if(!ok)errors.emplace_back(text);};
    check(std::isfinite(profile.base)&&std::abs(profile.base)<=1e6,"Altitude density base must be finite within 1000000 local metres");
    check(std::isfinite(profile.height)&&profile.height>=1e-4&&profile.height<=1e6,"Altitude density height must lie in [0.0001,1000000] local metres");
    check(profile.knots.size()>=2&&profile.knots.size()<=max_altitude_density_knots,"Altitude density requires 2..8 knots");
    if(profile.knots.size()<2||profile.knots.size()>max_altitude_density_knots)return errors;
    check(profile.knots.front().t==0&&profile.knots.back().t==1,"Altitude density endpoints must be t=0 and t=1");
    for(std::size_t i=0;i<profile.knots.size();++i){const auto& k=profile.knots[i];
        check(std::isfinite(k.t)&&k.t>=0&&k.t<=1,"Altitude density knot t must be finite in [0,1]");
        check(std::isfinite(k.scale)&&k.scale>=0&&k.scale<=1,"Altitude density scale must be finite in [0,1]");
        if(i)check(k.t-profile.knots[i-1].t>=0.001,"Altitude density knots need ordered separation >=0.001");
    }
    if(errors.empty()&&profile.enabled){
        for(std::size_t i=1;i<profile.knots.size();++i){const auto& a=profile.knots[i-1];const auto& b=profile.knots[i];
            if(a.scale!=b.scale)check(float(profile.base+profile.height*a.t)<float(profile.base+profile.height*b.t),
                "Altitude density band has no distinct float heights; widen the band, increase height or move the local base toward zero");
        }
        check(altitude_density_error_bound(profile)<=max_altitude_density_error,
            "Altitude density precision exceeds the 0.0001 float modulation error budget; widen density bands, reduce contrast, increase height or move the local base toward zero");
    }
    return errors;
}
AltitudeDensityEvaluator::AltitudeDensityEvaluator(AltitudeDensityProfile profile):profile_(std::move(profile)){
    const auto errors=validate_altitude_density(profile_);
    if(!errors.empty()){std::string text="Invalid altitude density profile:";for(const auto& error:errors)text+="\n- "+error;throw std::invalid_argument(text);}
    if(profile_.enabled)maximum_=std::max_element(profile_.knots.begin(),profile_.knots.end(),[](const auto& a,const auto& b){return a.scale<b.scale;})->scale;
}
double AltitudeDensityEvaluator::at(double local_y)const{
    if(!std::isfinite(local_y))throw std::invalid_argument("Nonfinite altitude density sample");
    if(!profile_.enabled)return 1;
    const double t=std::clamp((local_y-profile_.base)/profile_.height,0.,1.);
    const auto next=std::upper_bound(profile_.knots.begin(),profile_.knots.end(),t,[](double value,const auto& knot){return value<knot.t;});
    if(next==profile_.knots.end())return profile_.knots.back().scale;
    const auto& a=*(next-1);const auto& b=*next;const double u=(t-a.t)/(b.t-a.t);
    return a.scale+(b.scale-a.scale)*u;
}
}
