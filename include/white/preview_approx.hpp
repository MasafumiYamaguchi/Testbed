#pragma once

namespace white {
// Preview-only appearance settings. These are not physical medium coefficients.
struct PreviewApproxSettings {
    bool enabled=false;
    double strength=0.5;
    bool operator==(const PreviewApproxSettings&) const = default;
};
inline constexpr unsigned preview_approx_octaves=2;
inline constexpr double preview_approx_extinction_scale=0.5;
inline constexpr double preview_approx_anisotropy_scale=0.5;
void require_valid_preview_approx(PreviewApproxSettings);
struct PreviewSource {
    double single_scattering=0;
    double multiple_scattering_approx=0;
    double total() const {return single_scattering+multiple_scattering_approx;}
};
// Returns source function j / sigma_t, before integration along the view ray.
// tau_sun is dimensionless optical depth toward the sun (not transmittance).
// cosine follows the photon propagation convention used by hg_phase().
// sun_irradiance is one linear RGB component; evaluate independently per channel.
// tau_sun may be +infinity (fully blocked); all other inputs must be finite.
// Default settings retain single scattering exactly. This is a preview
// approximation, not a reference estimator or an energy-conservation guarantee.
PreviewSource preview_approx_source(double tau_sun,double albedo,double g,
    double cosine,double sun_irradiance,PreviewApproxSettings={});
}
