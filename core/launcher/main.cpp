#define SDL_MAIN_HANDLED

#include "App.hpp"
#include "Ui.hpp"

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

#include <SDL.h>

#include <algorithm>
#include <cstdio>
#include <iostream>
#include <string>
#include <thread>

namespace Launcher {

namespace {

std::filesystem::path FindRoot(const std::filesystem::path& executable) {
    std::filesystem::path path = executable.parent_path();
    for (int i = 0; i < 6; i++) {
        if (std::filesystem::exists(path / "tools" / "convert.sh")) return path;
        if (std::filesystem::exists(path / ".." / "lib" / "anyps5")) return path;
        path = path.parent_path();
    }
    return executable.parent_path();
}

int Check(const std::filesystem::path& root) {
    const auto games = LoadLibrary();
    std::cout << "library: " << games.size() << " games\n";
    for (const auto& game : games) {
        const auto exe = ExecutablePath(game);
        std::cout << game.name << ": " << (std::filesystem::is_regular_file(exe) ? "ready" : "not converted") << "\n";
    }
    const auto script = root / "tools" / "convert.sh";
    if (std::filesystem::is_regular_file(script)) {
        std::cout << "scripts: ok\n";
        return 0;
    }
    std::cout << "scripts: convert.sh not found under " << root.string() << "\n";
    return 1;
}

int Launch(int argc, char** argv) {
    App app;
    app.executableDir = std::filesystem::absolute(argc > 0 ? argv[0] : "launcher").parent_path();
    app.root = FindRoot(app.executableDir / "launcher");
    app.Load();
    if (argc > 1 && std::string(argv[1]) == "--check") return Check(app.root);
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL: %s\n", SDL_GetError());
        return 1;
    }
    int width = 1280;
    int height = 800;
    try {
        width = std::max(960, std::stoi(app.Config("windowW", "1280")));
        height = std::max(640, std::stoi(app.Config("windowH", "800")));
    } catch (const std::exception&) {
    }
    SDL_Renderer* renderer = nullptr;
    SDL_Window* window = SDL_CreateWindow("AnyPS5", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height,
                                          SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (window != nullptr) {
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
        if (renderer == nullptr) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (window == nullptr || renderer == nullptr) {
        std::fprintf(stderr, "SDL: %s\n", SDL_GetError());
        if (window != nullptr) SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    Launcher::ApplyStyle();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);
    app.renderer = renderer;
    bool running = true;
    while (running && !app.exitRequested) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) running = false;
        }
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        DrawUi(app, renderer, window);
        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 20, 20, 24, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
    }
    for (const auto& [name, id] : app.icons) {
        (void)name;
        SDL_DestroyTexture(reinterpret_cast<SDL_Texture*>(id));
    }
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    int savedWidth = 0;
    int savedHeight = 0;
    SDL_GetWindowSize(window, &savedWidth, &savedHeight);
    app.SetConfig("windowW", std::to_string(savedWidth));
    app.SetConfig("windowH", std::to_string(savedHeight));
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

}

}

int main(int argc, char** argv) {
    SDL_SetMainReady();
    return Launcher::Launch(argc, argv);
}
