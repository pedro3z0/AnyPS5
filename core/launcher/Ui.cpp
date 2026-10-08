#include "Ui.hpp"

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

#include "stb_image.h"

#include <SDL.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>

namespace Launcher {

namespace {

constexpr int kCellWidth = 168;
constexpr int kIconSize = 128;

}

std::vector<std::string> EnvFields() {
    return {"ANYPS5_GPU", "ANYPS5_SYSTEM_FONTS", "ANYPS5_SHADER_CACHE_DIR", "ANYPS5_NO_SHADER_CACHE", "ANYPS5_NGS2_TRACE"};
}

std::string EnvHint(const std::string& key) {
    if (key == "ANYPS5_GPU") return "Vulkan device name filter (case-insensitive)";
    if (key == "ANYPS5_SYSTEM_FONTS") return "directory with SST-*.otf or Noto substitutes";
    if (key == "ANYPS5_SHADER_CACHE_DIR") return "shader cache directory (default: shader_cache beside the game)";
    if (key == "ANYPS5_NO_SHADER_CACHE") return "disable the disk shader cache when set";
    if (key == "ANYPS5_NGS2_TRACE") return "dump NGS2 voice params to stderr";
    return "";
}

bool TextInput(const char* label, std::string& value) {
    static std::map<std::uintptr_t, std::array<char, 512>> buffers;
    auto& buffer = buffers[reinterpret_cast<std::uintptr_t>(&value)];
    if (std::strncmp(buffer.data(), value.c_str(), buffer.size() - 1) != 0) {
        std::snprintf(buffer.data(), buffer.size(), "%s", value.c_str());
    }
    const bool changed = ImGui::InputText(label, buffer.data(), buffer.size());
    if (changed) value = buffer.data();
    return changed;
}

namespace {

void LoadIcon(App& app, const Game& game, void* renderer) {
    if (game.icon.empty() || app.icons.count(game.icon) != 0) return;
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* pixels = stbi_load(game.icon.c_str(), &width, &height, &channels, 4);
    if (pixels == nullptr) return;
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormatFrom(pixels, width, height, 32, width * 4, SDL_PIXELFORMAT_RGBA32);
    SDL_Texture* texture = nullptr;
    if (surface != nullptr) {
        if (width > kIconSize || height > kIconSize) {
            const double scale = static_cast<double>(kIconSize) / static_cast<double>(std::max(width, height));
            SDL_Surface* scaled = SDL_CreateRGBSurfaceWithFormat(0, static_cast<int>(width * scale), static_cast<int>(height * scale), 32, SDL_PIXELFORMAT_RGBA32);
            if (scaled != nullptr) {
                SDL_UpperBlitScaled(surface, nullptr, scaled, nullptr);
                texture = SDL_CreateTextureFromSurface(static_cast<SDL_Renderer*>(renderer), scaled);
                SDL_FreeSurface(scaled);
            }
        } else {
            texture = SDL_CreateTextureFromSurface(static_cast<SDL_Renderer*>(renderer), surface);
        }
        SDL_FreeSurface(surface);
    }
    stbi_image_free(pixels);
    if (texture != nullptr) app.icons[game.icon] = reinterpret_cast<std::uintptr_t>(texture);
}

void DrawMenuBar(App& app) {
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Add game")) {
                app.dialog = {};
                app.dialog.showAdd = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Quit")) app.exitRequested = true;
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Log", nullptr, &app.dialog.logOpen);
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }
}

void DrawToolbar(App& app) {
    const Game* selected = app.selected >= 0 && app.selected < static_cast<int>(app.games.size()) ? &app.games[app.selected] : nullptr;
    ImGui::BeginDisabled(app.busy || selected == nullptr);
    if (ImGui::Button("Convert") && selected != nullptr) {
        app.Convert(*selected);
        app.Audit(*selected);
    }
    ImGui::SameLine();
    if (ImGui::Button("Play") && selected != nullptr) app.Launch(*selected);
    ImGui::SameLine();
    if (ImGui::Button("Audit") && selected != nullptr) app.Audit(*selected);
    ImGui::SameLine();
    if (ImGui::Button("Settings")) app.dialog.showSettings = true;
    ImGui::SameLine();
    if (ImGui::Button("Input")) app.dialog.showInput = true;
    ImGui::SameLine();
    if (ImGui::Button("Open output") && selected != nullptr) {
        const std::string command = "xdg-open '" + selected->out + "' || open '" + selected->out + "' || explorer '" + selected->out + "'";
        std::system(command.c_str());
    }
    ImGui::SameLine();
    if (ImGui::Button("Remove") && selected != nullptr) {
        app.games.erase(app.games.begin() + app.selected);
        app.selected = -1;
        app.Save();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(app.busy);
    if (ImGui::Button("Add game")) {
        app.dialog = {};
        app.dialog.showAdd = true;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Logs")) app.dialog.logOpen = !app.dialog.logOpen;
    if (app.busy) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.95f, 0.6f, 0.2f, 1.0f), "%s running...", app.lastCommand.c_str());
    }
}

void DrawGrid(App& app, void* renderer) {
    const float width = ImGui::GetContentRegionAvail().x * 0.68f;
    const int columns = std::max(1, static_cast<int>(width / kCellWidth));
    ImGui::BeginChild("games", ImVec2(width, 0), ImGuiChildFlags_Borders);
    if (ImGui::BeginTable("grid", columns)) {
        int index = 0;
        while (index < static_cast<int>(app.games.size())) {
            ImGui::TableNextRow();
            for (int column = 0; column < columns && index < static_cast<int>(app.games.size()); column++, index++) {
                ImGui::TableSetColumnIndex(column);
                const auto& game = app.games[index];
                ImGui::PushID(index);
                LoadIcon(app, game, renderer);
                const auto it = app.icons.find(game.icon);
                ImGui::BeginGroup();
                if (it != app.icons.end()) {
                    ImGui::Image(static_cast<ImTextureID>(it->second), ImVec2(kIconSize, kIconSize));
                } else {
                    ImGui::Button("?", ImVec2(kIconSize, kIconSize));
                }
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kCellWidth - 12.0f);
                ImGui::Text("%s", game.title.c_str());
                ImGui::PopTextWrapPos();
                ImGui::EndGroup();
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    app.selected = index;
                    app.Launch(app.games[index]);
                }
                if (ImGui::IsItemClicked()) app.selected = index;
                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::Text("%s (%s)", game.title.c_str(), game.titleId.c_str());
                    ImGui::Text("%s", game.status.c_str());
                    ImGui::EndTooltip();
                }
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
}

void DrawDetails(App& app) {
    ImGui::SameLine();
    ImGui::BeginChild("details", ImVec2(0, 0), ImGuiChildFlags_Borders);
    if (app.selected < 0 || app.selected >= static_cast<int>(app.games.size())) {
        ImGui::TextDisabled("Select a game");
    } else {
        const auto& game = app.games[app.selected];
        ImGui::TextWrapped("%s", game.title.c_str());
        if (!game.titleId.empty()) ImGui::TextDisabled("ID: %s", game.titleId.c_str());
        if (!game.version.empty()) ImGui::TextDisabled("Version: %s", game.version.c_str());
        ImGui::Separator();
        ImGui::TextWrapped("Status: %s", game.status.empty() ? "not converted" : game.status.c_str());
        ImGui::TextDisabled("Last run: %s", game.lastRun.empty() ? "never" : game.lastRun.c_str());
        ImGui::Separator();
        ImGui::TextWrapped("Dump: %s", game.dump.c_str());
        ImGui::TextWrapped("Out: %s", game.out.c_str());
        ImGui::TextWrapped("Input: %s", game.input.c_str());
    }
    ImGui::EndChild();
}

void DrawStatusBar(App& app) {
    ImGui::Separator();
    ImGui::Text("%d games | %s", static_cast<int>(app.games.size()), app.root.c_str());
}

void DrawLog(App& app) {
    if (!app.dialog.logOpen) return;
    ImGui::Begin("Log", &app.dialog.logOpen);
    if (ImGui::Button("Clear")) {
        std::lock_guard<std::mutex> lock(app.logMutex);
        app.logLines.clear();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("last file: %s", app.lastLog.c_str());
    ImGui::Separator();
    ImGui::BeginChild("lines");
    for (const auto& line : app.SnapshotLog()) ImGui::TextWrapped("%s", line.c_str());
    ImGui::EndChild();
    ImGui::End();
}

}

void DrawUi(App& app, void* renderer, void* window) {
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowSize(viewport->WorkSize);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                                    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoBringToFrontOnFocus;
    if (ImGui::Begin("AnyPS5", nullptr, flags)) {
        DrawMenuBar(app);
        DrawToolbar(app);
        ImGui::Separator();
        DrawGrid(app, renderer);
        DrawDetails(app);
        DrawStatusBar(app);
    }
    ImGui::End();
    DrawLog(app);
    DrawDialogs(app);
}

}
