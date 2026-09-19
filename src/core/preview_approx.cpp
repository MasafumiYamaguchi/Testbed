#include "white/preview_approx.hpp"
#include "white/phase.hpp"
#include <cmath>
#include <stdexcept>

namespace white {
void require_valid_preview_approx(PreviewApproxSettings settings) {
    if(!std::isfinite(settings.strength)||settings.strength<0||settings.strength>1)
        throw std::invalid_argument("preview multiple-scattering strength must be in [0,1]");
}
PreviewSource preview_approx_source(double tau,double albedo,double g,double cosine,
    double sun,PreviewApproxSettings settings) {
    require_valid_preview_approx(settings);
    if(std::isnan(tau)||tau<0||!std::isfinite(albedo)||albedo<0||albedo>1||
       !std::isfinite(sun)||sun<0)
        throw std::invalid_argument("invalid preview lighting input");
    const double phase=hg_phase(cosine,g); // Validate even for an unlit sample.
    PreviewSource result;
    if(sun==0||albedo==0||std::isinf(tau))return result;
    result.single_scattering=sun*albedo*phase*std::exp(-tau);
    if(settings.enabled&&settings.strength>0&&tau>0) {
        const double collision_weight=-std::expm1(-tau);
        double octave_tau=tau,octave_g=g,weight=albedo;
        for(unsigned octave=0;octave<preview_approx_octaves;++octave) {
            octave_tau*=preview_approx_extinction_scale;
            octave_g*=preview_approx_anisotropy_scale;
            weight*=albedo*settings.strength;
            result.multiple_scattering_approx+=sun*collision_weight*weight*
                hg_phase(cosine,octave_g)*std::exp(-octave_tau);
        }
    }
    if(!std::isfinite(result.total()))throw std::overflow_error("preview source overflow");
    return result;
}
}
