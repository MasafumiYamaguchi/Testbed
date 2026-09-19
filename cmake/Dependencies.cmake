include(FetchContent)
find_package(Git REQUIRED)
# Immutable revisions, rather than moving tags. Optional candidates stay out of
# the runtime dependency graph until their owning issue implements a consumer.
set(WHITE_SDL_REVISION 7f3ae3d57459e59943a4ecfefc8f6277ec6bf540) # release-3.2.28
set(WHITE_IMGUI_REVISION 5d4126876bc10396d4c6511853ff10964414c776) # v1.92.1
set(WHITE_SHADERCROSS_REVISION 1ff05bec573988a98ef9e0260b4da44f512b8367)
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3
  GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
  GIT_TAG ${WHITE_SDL_REVISION}
  GIT_PROGRESS TRUE)
FetchContent_MakeAvailable(SDL3)
FetchContent_Declare(imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG ${WHITE_IMGUI_REVISION})
FetchContent_Declare(shadercross
  GIT_REPOSITORY https://github.com/libsdl-org/SDL_shadercross.git
  GIT_TAG ${WHITE_SHADERCROSS_REVISION})
FetchContent_Declare(imguizmo
  GIT_REPOSITORY https://github.com/CedricGuillemet/ImGuizmo.git
  GIT_TAG 18cef5e031d8c6973d80284c67f60549fafd78c1
  SOURCE_SUBDIR white-no-upstream-targets)
