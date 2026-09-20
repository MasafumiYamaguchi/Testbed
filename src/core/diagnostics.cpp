#include "white/diagnostics.hpp"
#include <array>
#include <stdexcept>
namespace white {
std::string_view diagnostic_name(unsigned mode){static constexpr std::array<std::string_view,12> names{"radiance","density slice Z=0.5","view optical depth","density-weighted sun T","single scattering","AABB support","brick maximum slice","brick occupancy slice","view evaluations","total density evaluations","skipped intervals","invalid or majorant violation"};if(mode>=names.size())throw std::invalid_argument("Unknown diagnostic mode");return names[mode];}
float diagnostic_scale(unsigned mode){(void)diagnostic_name(mode);switch(mode){case 1:case 6:return 5;case 2:return 8;case 8:case 10:return 64;case 9:return 576;default:return 1;}}
}
