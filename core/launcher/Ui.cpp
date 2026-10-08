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

void OpenAddDialog(App& app) {
    app.dialog = {};
    app.dialog.dump = app.Config("lastDump", "");
    app.dialog.out = app.Config("lastOut", "");
    app.dialog.showAdd = true;
}

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

bool TextInput(const char* label, std::string& value, const char* hint) {
    static std::map<ImGuiID, std::array<char, 512>> buffers;
    auto& buffer = buffers[ImGui::GetID(label)];
    if (std::strncmp(buffer.data(), value.c_str(), buffer.size() - 1) != 0) {
        std::snprintf(buffer.data(), buffer.size(), "%s", value.c_str());
    }
    const bool changed = hint != nullptr && hint[0] != '\0'
        ? ImGui::InputTextWithHint(label, hint, buffer.data(), buffer.size())
        : ImGui::InputText(label, buffer.data(), buffer.size());
    if (changed) value = buffer.data();
    return changed;
}

void ApplyStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(14, 12);
    style.FramePadding = ImVec2(10, 6);
    style.ItemSpacing = ImVec2(8, 7);
    style.ItemInnerSpacing = ImVec2(6, 5);
    style.IndentSpacing = 20;
    style.ScrollbarSize = 13;
    style.GrabMinSize = 12;
    style.WindowBorderSize = 1;
    style.ChildBorderSize = 1;
    style.PopupBorderSize = 1;
    style.FrameBorderSize = 0;
    style.WindowRounding = 8;
    style.ChildRounding = 6;
    style.FrameRounding = 5;
    style.PopupRounding = 6;
    style.ScrollbarRounding = 5;
    style.GrabRounding = 4;
    style.TabRounding = 5;
    const ImVec4 background = ImVec4(0.086f, 0.094f, 0.114f, 1.0f);
    const ImVec4 surface = ImVec4(0.122f, 0.133f, 0.157f, 1.0f);
    const ImVec4 raised = ImVec4(0.157f, 0.169f, 0.196f, 1.0f);
    const ImVec4 border = ImVec4(0.227f, 0.243f, 0.278f, 1.0f);
    const ImVec4 accent = ImVec4(0.176f, 0.549f, 0.937f, 1.0f);
    const ImVec4 accentDim = ImVec4(0.141f, 0.435f, 0.745f, 1.0f);
    const ImVec4 text = ImVec4(0.902f, 0.925f, 0.953f, 1.0f);
    const ImVec4 textDim = ImVec4(0.549f, 0.573f, 0.616f, 1.0f);
    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text] = text;
    colors[ImGuiCol_TextDisabled] = textDim;
    colors[ImGuiCol_WindowBg] = background;
    colors[ImGuiCol_ChildBg] = ImVec4(0.106f, 0.114f, 0.137f, 1.0f);
    colors[ImGuiCol_PopupBg] = surface;
    colors[ImGuiCol_Border] = border;
    colors[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_FrameBg] = raised;
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.196f, 0.212f, 0.243f, 1.0f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.227f, 0.247f, 0.286f, 1.0f);
    colors[ImGuiCol_TitleBg] = surface;
    colors[ImGuiCol_TitleBgActive] = surface;
    colors[ImGuiCol_TitleBgCollapsed] = surface;
    colors[ImGuiCol_MenuBarBg] = surface;
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_ScrollbarGrab] = border;
    colors[ImGuiCol_ScrollbarGrabHovered] = raised;
    colors[ImGuiCol_ScrollbarGrabActive] = accent;
    colors[ImGuiCol_CheckMark] = accent;
    colors[ImGuiCol_SliderGrab] = accent;
    colors[ImGuiCol_SliderGrabActive] = accent;
    colors[ImGuiCol_Button] = raised;
    colors[ImGuiCol_ButtonHovered] = accent;
    colors[ImGuiCol_ButtonActive] = accentDim;
    colors[ImGuiCol_Header] = ImVec4(0.157f, 0.176f, 0.208f, 1.0f);
    colors[ImGuiCol_HeaderHovered] = accentDim;
    colors[ImGuiCol_HeaderActive] = accent;
    colors[ImGuiCol_Separator] = border;
    colors[ImGuiCol_SeparatorHovered] = accent;
    colors[ImGuiCol_SeparatorActive] = accent;
    colors[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_ResizeGripHovered] = accent;
    colors[ImGuiCol_ResizeGripActive] = accent;
    colors[ImGuiCol_Tab] = surface;
    colors[ImGuiCol_TabHovered] = accent;
    colors[ImGuiCol_TabActive] = accentDim;
    colors[ImGuiCol_TabUnfocused] = surface;
    colors[ImGuiCol_TabUnfocusedActive] = accentDim;
    colors[ImGuiCol_TableHeaderBg] = surface;
    colors[ImGuiCol_TableBorderStrong] = border;
    colors[ImGuiCol_TableBorderLight] = border;
    colors[ImGuiCol_TextSelectedBg] = accentDim;
    colors[ImGuiCol_NavHighlight] = accent;
    ImGuiIO& io = ImGui::GetIO();
    ImFontConfig config;
    config.SizePixels = 16.0f;
    const char* candidates[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "C:/Windows/Fonts/segoeui.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
    };
    for (const char* candidate : candidates) {
        if (std::filesystem::is_regular_file(candidate)) {
            if (io.Fonts->AddFontFromFileTTF(candidate, config.SizePixels, &config) != nullptr) return;
        }
    }
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
            if (ImGui::MenuItem("Add game")) OpenAddDialog(app);

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
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.176f, 0.549f, 0.937f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.227f, 0.627f, 0.980f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.141f, 0.435f, 0.745f, 1.0f));
    if (ImGui::Button("Play") && selected != nullptr) app.Launch(*selected);
    ImGui::PopStyleColor(3);
    ImGui::SameLine();
    if (ImGui::Button("Audit") && selected != nullptr) app.Audit(*selected);

    ImGui::SameLine();
    if (ImGui::Button("Settings")) app.dialog.showSettings = true;
    ImGui::SameLine();
    if (ImGui::Button("Input")) {
        app.dialog.showSettings = true;
        app.dialog.focusInput = true;
    }
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
    if (ImGui::Button("Add game")) OpenAddDialog(app);

    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Logs")) app.dialog.logOpen = !app.dialog.logOpen;
    if (app.busy) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.95f, 0.6f, 0.2f, 1.0f), "%s running...", app.lastCommand.c_str());
    }
}

void DrawCard(App& app, int index, void* renderer) {
    const auto& game = app.games[index];
    ImGui::PushID(index);
    LoadIcon(app, game, renderer);
    const auto it = app.icons.find(game.icon);
    const bool selected = app.selected == index;
    ImGui::BeginGroup();
    ImGui::PushStyleColor(ImGuiCol_Button, selected ? ImVec4(0.176f, 0.549f, 0.937f, 0.35f) : ImVec4(0.157f, 0.169f, 0.196f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.176f, 0.549f, 0.937f, 0.22f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.176f, 0.549f, 0.937f, 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 6));
    if (it != app.icons.end()) {
        if (ImGui::ImageButton("cover", static_cast<ImTextureID>(it->second), ImVec2(kIconSize, kIconSize))) app.selected = index;
    } else {
        if (ImGui::Button("?", ImVec2(kIconSize, kIconSize))) app.selected = index;
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        app.selected = index;
        app.Launch(app.games[index]);
    }
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kIconSize + 12.0f);
    if (selected) {
        ImGui::TextColored(ImVec4(0.4f, 0.67f, 0.97f, 1.0f), "%s", game.title.c_str());
    } else {
        ImGui::Text("%s", game.title.c_str());
    }
    ImGui::PopTextWrapPos();
    if (!game.titleId.empty()) {
        ImGui::TextDisabled("%s", game.titleId.c_str());
    }
    ImGui::TextDisabled("%s", game.status.empty() ? "not converted" : game.status.c_str());
    ImGui::EndGroup();
    ImGui::PopID();
}

void DrawGrid(App& app, void* renderer) {
    const float width = ImGui::GetContentRegionAvail().x * 0.72f;
    const int columns = std::max(1, static_cast<int>(width / (kCellWidth + 14)));
    ImGui::BeginChild("games", ImVec2(width, 0), ImGuiChildFlags_Borders);
    if (ImGui::BeginTable("grid", columns)) {
        int index = 0;
        while (index < static_cast<int>(app.games.size())) {
            ImGui::TableNextRow();
            for (int column = 0; column < columns && index < static_cast<int>(app.games.size()); column++, index++) {
                ImGui::TableSetColumnIndex(column);
                DrawCard(app, index, renderer);
            }
        }
        if (app.games.empty()) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.549f, 0.573f, 0.616f, 1.0f), "No games yet");
            ImGui::TextDisabled("Add a dumped game to convert, audit and play it here");
            ImGui::Spacing();
            if (ImGui::Button("Add your first game", ImVec2(220, 0))) OpenAddDialog(app);
        }

        ImGui::EndTable();
    }
    ImGui::EndChild();
}

void DrawDetails(App& app) {
    ImGui::SameLine();
    ImGui::BeginChild("details", ImVec2(0, 0), ImGuiChildFlags_Borders);
    if (app.selected < 0 || app.selected >= static_cast<int>(app.games.size())) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.549f, 0.573f, 0.616f, 1.0f), "Select a game");
        ImGui::TextDisabled("Double-click a game to play");
    } else {
        const auto& game = app.games[app.selected];
        ImGui::TextColored(ImVec4(0.902f, 0.925f, 0.953f, 1.0f), "%s", game.title.c_str());
        if (!game.titleId.empty()) ImGui::TextDisabled("ID: %s", game.titleId.c_str());
        if (!game.version.empty()) ImGui::TextDisabled("Version: %s", game.version.c_str());
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.4f, 0.67f, 0.97f, 1.0f), "Status");
        ImGui::TextWrapped("%s", game.status.empty() ? "not converted" : game.status.c_str());
        ImGui::TextDisabled("Last run: %s", game.lastRun.empty() ? "never" : game.lastRun.c_str());
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.4f, 0.67f, 0.97f, 1.0f), "Paths");
        ImGui::TextDisabled("Dump: %s", game.dump.c_str());
        ImGui::TextDisabled("Out: %s", game.out.c_str());
        ImGui::TextDisabled("Input: %s", game.input.c_str());
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.4f, 0.67f, 0.97f, 1.0f), "Environment");
        for (const auto& [key, value] : game.env) {
            if (!value.empty()) ImGui::TextDisabled("%s = %s", key.c_str(), value.c_str());
        }
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
        if (app.IsConfigured()) {
            DrawMenuBar(app);
            DrawToolbar(app);
            ImGui::Separator();
            DrawGrid(app, renderer);
            DrawDetails(app);
        } else {
            DrawSetup(app);
        }
        DrawStatusBar(app);
    }
    ImGui::End();
    DrawLog(app);
    DrawDialogs(app);
}

}
