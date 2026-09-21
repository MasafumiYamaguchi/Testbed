# ProjectWhite

An original art-directable cloud creation tool. Implementation follows [the roadmap](https://github.com/MasafumiYamaguchi/Testbed/issues/2).
Current milestone: Phase 0 cloud editing, noise, dense cache and gate evaluation (#3–#14). The Windows application previews
ellipsoid density with single scattering and provides primitive picking, Move/
Scale gizmos, numeric inspection, a flat base, cuts, camera orbit, Undo/Redo and
JSON Save/Open. Medium density modulation, micro edge erosion and bounded structural warp are
available in Shape details. Optional 128³/256³ density caches rebuild asynchronously
and preserve the latest direct preview while editing. [Phase-0 physical acceptance remains pending](docs/phase-0-gate.md):
physical RTX validation is outstanding. The user has authorized subsequent phases
to proceed while that validation is deferred. Production export remains later work.

## Windows build

Install Visual Studio 2022 with **Desktop development with C++**, MSVC v143,
a Windows 10/11 SDK, CMake 3.25+ and Git. Use an x64 Developer PowerShell.
The first configure fetches pinned SDL, ImGui, ImGuizmo and JSON source revisions.
For offline builds pre-populate sources and use their `FETCHCONTENT_SOURCE_DIR_*`
CMake overrides; see `cmake/Dependencies.cmake`.

```powershell
./scripts/fetch-dxc.ps1
cmake --preset windows
cmake --build --preset windows-debug --parallel
ctest --preset windows-debug
.\build\windows\Debug\white_app.exe
cmake --build --preset windows-release --parallel
ctest --preset windows-release
.\build\windows\Release\white_app.exe --self-test --lifecycle-test --frames 840 --capture evidence.bmp
```

`SDL3.dll` is copied beside the executable. Keep it there. A clean checkout is
sufficient; no personal absolute paths, credentials or proprietary SDKs are used.
Delete `build/windows` to verify a clean rebuild. Failed dependency downloads
include Git's error; verify network access or use the source override above.
Old compilers fail at configure time. The fetch script verifies the DXC archive
SHA256; CMake diagnoses missing DXC. Shaders are compiled during the build and
copied to `shaders/` beside the executable. Preserve this directory when copying
the app. `WHITE_BUILD_GPU_SPIKE=OFF` explicitly selects the older bootstrap window;
the GPU app never silently falls back. Windows D3D12 support is required.

## CPU-only build (no GPU)

GCC 11+ or a C++20-capable Clang, CMake 3.25+, Git and Ninja. Since Issue #6,
the first configure fetches pinned nlohmann/json. Offline users may supply its
source with `FETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON`:

```sh
cmake --preset cpu-debug
cmake --build --preset cpu-debug
ctest --preset cpu-debug
cmake --preset cpu-release
cmake --build --preset cpu-release
ctest --preset cpu-release
```

Tests use explicit failures, so Release builds do not disable assertions. Resource
size arithmetic is checked in 64 bits before allocating. The app's `--help` and
invalid argument tests run without initializing a window system.

## Evidence and scope

GitHub Actions builds Debug/Release on Windows and Linux CPU-only, runs CTest,
starts the Windows app and captures its real desktop window and GPU framebuffer. Download the
`windows-Release-evidence` artifact for PNG, logs and the executable. The original
unconverted client bitmap is also retained. Fixtures include density agreement, ray/box intersections, analytic optical
integration and per-pixel CPU/GPU transmittance comparison. The UI smoke injects
ImGui mouse events for gizmo translation/scale and checks one-step Undo/Redo; it
also captures four shape operations, three camera views and an inside view.
These checks do not establish physical RTX performance. [GPU contracts and acceptance](docs/adr/0002-gpu-spike.md)
separate numerical tests, lifecycle smoke checks and remaining physical GPU tests.

See [architecture decision](docs/adr/0001-foundation.md) and
[dependency notices](THIRD_PARTY.md). The [optional OpenVDB spike](spikes/vdb/README.md) has its own build and
write/reopen test and is not on the preview build path. Real GPU validation and
performance measurements are separate from hosted CI success.

The [single-state density export contract](docs/adr/0034-single-state-density-export.md)
defines an immutable Frozen + supported finishing snapshot, world-metre voxel
coordinates and provenance/optical metadata. Its CPU API and contract tests do
not yet provide a product VDB writer or paint support.
The CPU-only `white_export_plan` command reports separate dense-grid, tile,
aligned transfer, readback-queue and caller-retained-snapshot reservations before
execution-buffer allocation. See [resource preflight](docs/adr/0035-export-resource-preflight.md)
for explicit byte limits and the remaining unmeasured memory costs.

## Editing

Click a blue cell outline to select it. Choose Move or Scale and drag an axis;
numeric Center/Radii fields use the same editing session. Add cut creates an
orange ellipsoid; enable Select cuts to pick cuts. Flat base and Base height
control the underside. Right-drag in the viewport orbits; the wheel zooms.
Front/Side/Top are fixed viewing directions. Esc cancels the current drag.

Save/Open use the UTF-8 path shown at the top of the inspector. Open asks before
discarding unsaved edits. Invalid values keep the last valid scene and show the
validation error. The inspector scrolls on small windows. See the
[reproduction steps and UI contract](docs/adr/0007-editor.md).

## Fixed comparison captures

The Release workflow also captures 17 saved Recipe/view fixtures, including
direct versus 128³/256³ comparisons for seven shapes/detail variants. Download
`windows-Release-evidence` and inspect `evidence/gate/` for the actual PNGs,
recipes, hashes, adapter and numeric logs. The `--recipe` option accepts a saved
Scene; render width, view/shadow steps and cache resolution can be fixed on the
command line. See the [gate protocol and remaining measurements](docs/phase-0-gate.md).
