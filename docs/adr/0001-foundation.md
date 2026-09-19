# 0001: Minimal native foundation

Status: accepted for Issue #3; GPU backend decision remains pending Issue #4.

C++20 and CMake separate the desktop process from a CPU-only `white_core` library.
CTest runs without SDL, a window system, shader compilers or GPU drivers when
`WHITE_BUILD_APP=OFF`. The desktop target currently opens an empty SDL window.

Dependency direction: Editor -> Command/Document -> Field Evaluation.
Renderer and Exporter consume immutable Document snapshots and the same Field
Evaluation contract. GPU Platform owns device/resource lifetimes below Renderer
and GPU Field Evaluation. CPU Document never includes SDL or a renderer header.
These are responsibilities, not placeholder engine classes. Add modules only
when their owning issue introduces real behavior.

First candidate: SDL3 GPU with the Windows D3D12 backend, HLSL, SDL_shadercross,
Dear ImGui. Versions are locked in `cmake/Dependencies.cmake`. ImGui and
shadercross are declared but not downloaded/linked by the empty app. Issue #4
must prove compute, 3D texture sampling, readback and presentation before accepting
the backend. A failed spike should record a single replacement in a new ADR.

The foundation uses the SDL window surface only to make startup/capture observable;
this is not a GPU renderer and does not validate the graphics backend. It is
replaced by the GPU swapchain in Issue #4. No alternative renderer is being built.

OpenVDB is an independent optional spike in Issue #13, never a requirement for
opening the app. Its option currently fails explicitly rather than pretending to
build a target that does not exist. No product export is implemented here.

CI proves configure/build/CPU tests and window startup on its runner. It cannot
prove RTX 5070 Ti functionality, performance, D3D12 correctness or memory bounds.
Record those separately with GPU, driver, backend, commit and measurement method.
