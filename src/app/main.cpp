#include "white/build_info.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <charconv>
#include <iostream>
#include <string>
#include <string_view>

int main(int argc, char** argv) {
    int frame_limit = 0;
    std::string capture;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == "--help") {
            std::cout << "ProjectWhite --frames N --capture output.bmp\n"
                         "Phase 0 foundation. Empty SDL window; GPU validation is Issue #4.\n";
            return 0;
        }
        if (arg == "--frames" && i + 1 < argc) {
            const std::string_view value(argv[++i]);
            const auto result = std::from_chars(value.data(), value.data() + value.size(), frame_limit);
            if (result.ec != std::errc{} || result.ptr != value.data() + value.size() || frame_limit <= 0) {
                std::cerr << "--frames requires a positive integer\n"; return 2;
            }
        } else if (arg == "--capture" && i + 1 < argc) {
            capture = argv[++i];
        } else {
            std::cerr << "Unknown or incomplete argument: " << arg << '\n'; return 2;
        }
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) { std::cerr << SDL_GetError() << '\n'; return 1; }
    SDL_Window* window = SDL_CreateWindow("ProjectWhite | Phase 0 foundation", 1100, 700, SDL_WINDOW_RESIZABLE);
    if (!window) { std::cerr << SDL_GetError() << '\n'; SDL_Quit(); return 1; }
    std::cout << white::app_name << ' ' << white::version << " SDL=" << SDL_GetVersion()
              << " video=" << SDL_GetCurrentVideoDriver() << " GPU=not-initialized\n";
    bool running = true;
    int result = 0;
    for (int frame = 0; running && (frame_limit == 0 || frame < frame_limit); ++frame) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) running = false;
        }
        if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) { SDL_Delay(16); continue; }
        SDL_Surface* surface = SDL_GetWindowSurface(window);
        if (!surface || !SDL_FillSurfaceRect(surface, nullptr, SDL_MapSurfaceRGB(surface, 24, 30, 40))
            || !SDL_UpdateWindowSurface(window)) {
            std::cerr << SDL_GetError() << '\n'; result = 1; break;
        }
        if (!capture.empty()) {
            if (!SDL_SaveBMP(surface, capture.c_str())) { std::cerr << SDL_GetError() << '\n'; result = 1; break; }
            capture.clear();
        }
        SDL_Delay(16);
    }
    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}
