# ProjectWhite

An original art-directable cloud creation tool. Implementation follows [the roadmap](https://github.com/MasafumiYamaguchi/Testbed/issues/2).
Current milestone: Issue #4 GPU field validation. The Windows application displays
a GPU-generated 3D ramp/impulse slice with ImGui controls and checked readback.
Cloud shapes and editing are not implemented yet.

## Windows build

Install Visual Studio 2022 with **Desktop development with C++**, MSVC v143,
a Windows 10/11 SDK, CMake 3.25+ and Git. Use an x64 Developer PowerShell.
The first app configure downloads the exact SDL revision over HTTPS. For an
offline build pre-populate SDL and set `FETCHCONTENT_SOURCE_DIR_SDL3` to that path.

```powershell
./scripts/fetch-dxc.ps1
cmake --preset windows
cmake --build --preset windows-debug --parallel
ctest --preset windows-debug
.\build\windows\Debug\white_app.exe
cmake --build --preset windows-release --parallel
ctest --preset windows-release
.\build\windows\Release\white_app.exe --self-test --lifecycle-test --frames 180 --capture evidence.bmp
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

## CPU-only build (no downloads or GPU)

GCC 11+ or a C++20-capable Clang, CMake 3.25+ and Ninja:

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
unconverted client bitmap is also retained. Ramp/impulse fixtures prove neither
cloud rendering nor RTX performance. [GPU contracts and acceptance](docs/adr/0002-gpu-spike.md)
separate numerical tests, lifecycle smoke checks and remaining physical GPU tests.

See [architecture decision](docs/adr/0001-foundation.md) and
[dependency notices](THIRD_PARTY.md). OpenVDB is reserved for the independent
Issue #13 spike and is not on the preview build path. Real GPU validation and
performance measurements are separate from hosted CI success.
