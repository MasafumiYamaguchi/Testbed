# Third-party dependencies

The existing MIT LICENSE remains unchanged. Dependency source is not modified.
Retain upstream license files with redistributed libraries. Versions below are
intentional reproducibility pins, not claims of being the latest release.

| Dependency | Revision | Source | License | Use |
|---|---|---|---|---|
| SDL3 3.2.28 | `7f3ae3d57459e59943a4ecfefc8f6277ec6bf540` | https://github.com/libsdl-org/SDL | zlib (`LICENSE.txt`) | Window, future GPU backend |
| Dear ImGui 1.92.1 | `5d4126876bc10396d4c6511853ff10964414c776` | https://github.com/ocornut/imgui | MIT (`LICENSE.txt`) | Candidate for Issue #4; not linked yet |
| SDL_shadercross | `1ff05bec573988a98ef9e0260b4da44f512b8367` | https://github.com/libsdl-org/SDL_shadercross | zlib (`LICENSE.txt`) | Candidate for Issue #4; not linked yet |

SDL_shadercross's DXC/SPIRV-Cross toolchain and OpenVDB's transitive dependencies
are not distributed or downloaded in Issue #3. Their resolved versions and notices
must be recorded when the respective spike introduces them. HLSL is the shader
source language, not an additional runtime dependency.

Technical source: SDL public API headers/documentation (https://wiki.libsdl.org/SDL3),
CMake/CTest documentation (https://cmake.org/documentation/). No proprietary
product code, internal formats, UI layout or algorithms were inspected.
