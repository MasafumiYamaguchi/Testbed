#pragma once
#include <cstdint>
#include <string_view>

namespace white {
inline constexpr std::string_view app_name = "ProjectWhite";
inline constexpr std::string_view version = "0.1.0";
// Checked multiplication shared by resource allocation callers. No allocation
// occurs here. Zero extents and zero element sizes are invalid resources.
std::uint64_t checked_volume_bytes(std::uint32_t x, std::uint32_t y,
                                   std::uint32_t z, std::uint32_t element_bytes);
}
