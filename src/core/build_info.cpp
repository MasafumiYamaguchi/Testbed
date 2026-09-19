#include "white/build_info.hpp"
#include <limits>
#include <stdexcept>

namespace white {
std::uint64_t checked_volume_bytes(std::uint32_t x, std::uint32_t y,
                                   std::uint32_t z, std::uint32_t element_bytes) {
    std::uint64_t result = 1;
    for (const auto factor : {x, y, z, element_bytes}) {
        if (factor == 0) throw std::invalid_argument("Volume extents and element size must be positive");
        if (result > std::numeric_limits<std::uint64_t>::max() / factor)
            throw std::overflow_error("Volume byte size exceeds uint64_t");
        result *= factor;
    }
    return result;
}
}
