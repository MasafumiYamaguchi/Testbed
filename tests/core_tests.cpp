#include "white/build_info.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>

int main() {
    int failures = 0;
    auto check = [&](bool ok, const char* name) {
        std::cout << (ok ? "PASS " : "FAIL ") << name << '\n';
        if (!ok) ++failures;
    };
    check(white::checked_volume_bytes(128, 128, 128, 4) == 8388608, "128 cubed float grid");
    check(white::checked_volume_bytes(17, 19, 23, 4) == 29716, "non-aligned extents");
    check(white::checked_volume_bytes(1024, 1024, 1024, 8) == 8589934592ULL, "size exceeds 32-bit");
    try { (void)white::checked_volume_bytes(1, 0, 1, 4); check(false, "reject zero"); }
    catch (const std::invalid_argument&) { check(true, "reject zero"); }
    try {
        constexpr auto m = std::numeric_limits<std::uint32_t>::max();
        (void)white::checked_volume_bytes(m, m, m, 4);
        check(false, "reject overflow");
    } catch (const std::overflow_error&) { check(true, "reject overflow"); }
    return failures == 0 ? 0 : 1;
}
