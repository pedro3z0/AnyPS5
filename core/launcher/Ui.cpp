#include "Ui.hpp"

#include "anyps5/Version.hpp"

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

#include "stb_image.h"

#include <SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <map>
#include <vector>

namespace Launcher {

namespace {

constexpr int kCellWidth = 168;
constexpr int kIconSize = 128;

void OpenAddDialog(App& app) {
    app.dialog = {};
    app.dialog.dump = app.Config("lastDump", "");
    app.dialog.out = app.Config("lastOut", "");
    app.dialog.windows = HostPrefersWindows();
    app.dialog.intel = HostIsIntel();
    app.dialog.showAdd = true;
}

std::string ShortPath(const std::string& path) {
    const char* homeValue = std::getenv("HOME");
    if (homeValue == nullptr || homeValue[0] == '\0') homeValue = std::getenv("USERPROFILE");
    if (homeValue == nullptr || homeValue[0] == '\0') return path;
    const std::string prefix = std::string(homeValue) + "/";
    if (path.rfind(prefix, 0) == 0) return "~/" + path.substr(prefix.size());
    return path;
}

}

void DrawGlyph(Glyph glyph, ImDrawList* draw, const ImVec2& origin, float size, ImU32 color) {
    const float x = origin.x;
    const float y = origin.y;
    const float c = size * 0.5f;
    const float stroke = size * 0.10f > 1.5f ? size * 0.10f : 1.5f;
    switch (glyph) {
    case Glyph::Play: {
        const ImVec2 triangle[3] = {
            ImVec2(x + size * 0.24f, y + size * 0.16f),
            ImVec2(x + size * 0.24f, y + size * 0.84f),
            ImVec2(x + size * 0.80f, y + c),
        };
        draw->AddConvexPolyFilled(triangle, 3, color);
        break;
    }
    case Glyph::Convert: {
        const ImVec2 center(x + c, y + c);
        const float radius = size * 0.30f;
        draw->PathArcTo(center, radius, -0.6f, 3.9f, 20);
        draw->PathStroke(color, false, stroke);
        const float angle = 3.9f;
        const ImVec2 end(center.x + radius * std::cos(angle), center.y + radius * std::sin(angle));
        const ImVec2 direction(-std::sin(angle), std::cos(angle));
        const ImVec2 outward(std::cos(angle), std::sin(angle));
        const float head = size * 0.17f;
        const ImVec2 arrow[3] = {
            ImVec2(end.x + direction.x * head, end.y + direction.y * head),
            ImVec2(end.x + outward.x * head * 0.9f, end.y + outward.y * head * 0.9f),
            ImVec2(end.x - outward.x * head * 0.9f, end.y - outward.y * head * 0.9f),
        };
        draw->AddConvexPolyFilled(arrow, 3, color);
        break;
    }
    case Glyph::Audit: {
        draw->AddRect(ImVec2(x + size * 0.14f, y + size * 0.14f), ImVec2(x + size * 0.86f, y + size * 0.86f), color, size * 0.14f, 0, stroke);
        draw->AddLine(ImVec2(x + size * 0.30f, y + size * 0.52f), ImVec2(x + size * 0.44f, y + size * 0.68f), color, stroke);
        draw->AddLine(ImVec2(x + size * 0.44f, y + size * 0.68f), ImVec2(x + size * 0.72f, y + size * 0.32f), color, stroke);
        break;
    }
    case Glyph::Sliders: {
        for (int row = 0; row < 3; ++row) {
            const float lineY = y + size * (0.28f + 0.22f * static_cast<float>(row));
            draw->AddLine(ImVec2(x + size * 0.14f, lineY), ImVec2(x + size * 0.86f, lineY), color, stroke);
            const float knobX = x + size * (0.36f + 0.20f * static_cast<float>((row + 1) % 3));
            draw->AddCircleFilled(ImVec2(knobX, lineY), size * 0.12f, color, 12);
        }
        break;
    }
    case Glyph::Keyboard: {
        draw->AddRect(ImVec2(x + size * 0.10f, y + size * 0.26f), ImVec2(x + size * 0.90f, y + size * 0.74f), color, size * 0.10f, 0, stroke);
        for (int row = 0; row < 2; ++row) {
            for (int column = 0; column < 4; ++column) {
                draw->AddCircleFilled(ImVec2(x + size * (0.26f + 0.16f * static_cast<float>(column)), y + size * (0.38f + 0.16f * static_cast<float>(row))), size * 0.045f, color, 6);
            }
        }
        break;
    }
    case Glyph::Folder: {
        draw->AddRectFilled(ImVec2(x + size * 0.12f, y + size * 0.18f), ImVec2(x + size * 0.44f, y + size * 0.36f), color, size * 0.06f);
        draw->AddRectFilled(ImVec2(x + size * 0.12f, y + size * 0.30f), ImVec2(x + size * 0.88f, y + size * 0.84f), color, size * 0.10f);
        break;
    }
    case Glyph::Trash: {
        draw->AddRectFilled(ImVec2(x + size * 0.42f, y + size * 0.10f), ImVec2(x + size * 0.58f, y + size * 0.22f), color, 1.0f);
        draw->AddRectFilled(ImVec2(x + size * 0.18f, y + size * 0.24f), ImVec2(x + size * 0.82f, y + size * 0.34f), color, size * 0.06f);
        draw->AddRectFilled(ImVec2(x + size * 0.26f, y + size * 0.36f), ImVec2(x + size * 0.74f, y + size * 0.86f), color, size * 0.10f);
        break;
    }
    case Glyph::Plus: {
        draw->AddLine(ImVec2(x + c, y + size * 0.16f), ImVec2(x + c, y + size * 0.84f), color, stroke * 1.2f);
        draw->AddLine(ImVec2(x + size * 0.16f, y + c), ImVec2(x + size * 0.84f, y + c), color, stroke * 1.2f);
        break;
    }
    case Glyph::Page: {
        draw->AddRect(ImVec2(x + size * 0.22f, y + size * 0.12f), ImVec2(x + size * 0.78f, y + size * 0.88f), color, size * 0.08f, 0, stroke);
        for (int row = 0; row < 3; ++row) {
            const float lineY = y + size * (0.36f + 0.16f * static_cast<float>(row));
            draw->AddLine(ImVec2(x + size * 0.34f, lineY), ImVec2(x + size * 0.66f, lineY), color, stroke * 0.8f);
        }
        break;
    }
    case Glyph::Close: {
        draw->AddLine(ImVec2(x + size * 0.24f, y + size * 0.24f), ImVec2(x + size * 0.76f, y + size * 0.76f), color, stroke * 1.2f);
        draw->AddLine(ImVec2(x + size * 0.76f, y + size * 0.24f), ImVec2(x + size * 0.24f, y + size * 0.76f), color, stroke * 1.2f);
        break;
    }
    case Glyph::Refresh: {
        draw->AddLine(ImVec2(x + c, y + size * 0.14f), ImVec2(x + c, y + size * 0.68f), color, stroke);
        draw->AddLine(ImVec2(x + size * 0.30f, y + size * 0.52f), ImVec2(x + c, y + size * 0.70f), color, stroke);
        draw->AddLine(ImVec2(x + size * 0.70f, y + size * 0.52f), ImVec2(x + c, y + size * 0.70f), color, stroke);
        draw->AddLine(ImVec2(x + size * 0.22f, y + size * 0.86f), ImVec2(x + size * 0.78f, y + size * 0.86f), color, stroke);
        break;
    }
    }
}

bool IconButton(const char* id, const char* label, Glyph glyph) {
    const ImGuiStyle& style = ImGui::GetStyle();
    const float glyphSize = 15.0f;
    const bool hasLabel = label[0] != '\0';
    const ImVec2 textSize = hasLabel ? ImGui::CalcTextSize(label) : ImVec2(0.0f, 0.0f);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float width = (hasLabel ? textSize.x + style.ItemInnerSpacing.x : 0.0f) + glyphSize + style.FramePadding.x * 2.0f;
    const ImVec2 size(width, ImGui::GetFrameHeight());
    ImGui::InvisibleButton(id, size);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImU32 background = ImGui::GetColorU32(active ? ImGuiCol_ButtonActive : hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button);
    draw->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), background, style.FrameRounding);
    const ImU32 foreground = ImGui::GetColorU32(ImGuiCol_Text);
    DrawGlyph(glyph, draw, ImVec2(origin.x + style.FramePadding.x, origin.y + (size.y - glyphSize) * 0.5f), glyphSize, foreground);
    if (hasLabel) {
        draw->AddText(ImVec2(origin.x + style.FramePadding.x + glyphSize + style.ItemInnerSpacing.x, origin.y + (size.y - textSize.y) * 0.5f), foreground, label);
    }
    return ImGui::IsItemClicked();
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

bool MultilineText(const char* label, std::string& value, float width, float height) {
    static std::array<char, 8192> buffer{};
    if (std::strncmp(buffer.data(), value.c_str(), buffer.size() - 1) != 0) {
        std::snprintf(buffer.data(), buffer.size(), "%s", value.c_str());
    }
    const bool changed = ImGui::InputTextMultiline(label, buffer.data(), buffer.size(), ImVec2(width, height));
    if (changed) value = buffer.data();
    return changed;
}

void ApplyStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(16, 14);
    style.FramePadding = ImVec2(12, 7);
    style.ItemSpacing = ImVec2(9, 8);
    style.ItemInnerSpacing = ImVec2(7, 5);
    style.IndentSpacing = 22;
    style.ScrollbarSize = 12;
    style.GrabMinSize = 11;
    style.WindowBorderSize = 1;
    style.ChildBorderSize = 1;
    style.PopupBorderSize = 1;
    style.FrameBorderSize = 0;
    style.WindowRounding = 10;
    style.ChildRounding = 14;
    style.FrameRounding = 8;
    style.PopupRounding = 12;
    style.ScrollbarRounding = 7;
    style.GrabRounding = 5;
    style.TabRounding = 8;
    const ImVec4 background = ImVec4(0.043f, 0.055f, 0.078f, 1.0f);
    const ImVec4 surface = ImVec4(0.078f, 0.098f, 0.133f, 1.0f);
    const ImVec4 raised = ImVec4(0.114f, 0.141f, 0.188f, 1.0f);
    const ImVec4 border = ImVec4(0.176f, 0.216f, 0.286f, 1.0f);
    const ImVec4 accent = ImVec4(0.039f, 0.518f, 1.0f, 1.0f);
    const ImVec4 accentDim = ImVec4(0.024f, 0.384f, 0.776f, 1.0f);
    const ImVec4 text = ImVec4(0.945f, 0.957f, 0.976f, 1.0f);
    const ImVec4 textDim = ImVec4(0.545f, 0.588f, 0.659f, 1.0f);
    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text] = text;
    colors[ImGuiCol_TextDisabled] = textDim;
    colors[ImGuiCol_WindowBg] = background;
    colors[ImGuiCol_ChildBg] = ImVec4(0.059f, 0.075f, 0.106f, 1.0f);
    colors[ImGuiCol_PopupBg] = surface;
    colors[ImGuiCol_Border] = border;
    colors[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_FrameBg] = raised;
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.153f, 0.184f, 0.243f, 1.0f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.176f, 0.216f, 0.286f, 1.0f);
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
    colors[ImGuiCol_Header] = ImVec4(0.114f, 0.141f, 0.208f, 1.0f);
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
    config.SizePixels = 17.0f;
    const char* homeValue = std::getenv("HOME");
    const std::string prefix = homeValue != nullptr && homeValue[0] != '\0' ? std::string(homeValue) : std::string();
    std::vector<std::string> candidates;
    if (!prefix.empty()) {
        candidates.push_back(prefix + "/.local/share/fonts/Inter/extras/ttf/Inter-Regular.ttf");
        candidates.push_back(prefix + "/.local/share/fonts/Inter/ttf/Inter-Regular.ttf");
    }
    candidates.push_back("/usr/share/fonts/TTF/OpenSans-Regular.ttf");
    candidates.push_back("/usr/share/fonts/truetype/open-sans/OpenSans-Regular.ttf");
    candidates.push_back("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
    candidates.push_back("/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf");
    candidates.push_back("/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf");
    candidates.push_back("C:/Windows/Fonts/segoeui.ttf");
    candidates.push_back("/System/Library/Fonts/Helvetica.ttc");
    for (const std::string& candidate : candidates) {
        if (std::filesystem::is_regular_file(candidate)) {
            if (io.Fonts->AddFontFromFileTTF(candidate.c_str(), config.SizePixels, &config) != nullptr) return;
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
        texture = SDL_CreateTextureFromSurface(static_cast<SDL_Renderer*>(renderer), surface);
        SDL_FreeSurface(surface);
    }
    stbi_image_free(pixels);
    if (texture != nullptr) app.icons[game.icon] = reinterpret_cast<std::uintptr_t>(texture);
}

void DrawToolbar(App& app) {
    const Game* selected = app.selected >= 0 && app.selected < static_cast<int>(app.games.size()) ? &app.games[app.selected] : nullptr;
    ImGui::TextColored(ImVec4(0.039f, 0.518f, 1.0f, 1.0f), "AnyPS5");
    ImGui::SameLine();
    ImGui::TextDisabled("%s", ANYPS5_VERSION);
    ImGui::SameLine();
    ImGui::BeginDisabled(app.busy || selected == nullptr);
    if (IconButton("##convert", "Convert", Glyph::Convert) && selected != nullptr) {
        app.Convert(*selected);
        app.Audit(*selected);
    }
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.039f, 0.518f, 1.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.60f, 1.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.024f, 0.384f, 0.776f, 1.0f));
    const bool play = IconButton("##play", "Play", Glyph::Play);
    ImGui::PopStyleColor(3);
    if (play && selected != nullptr) app.Launch(*selected);
    ImGui::SameLine();
    if (IconButton("##audit", "Audit", Glyph::Audit) && selected != nullptr) app.Audit(*selected);
    ImGui::SameLine();
    if (IconButton("##settings", "Settings", Glyph::Sliders)) app.dialog.showSettings = true;
    ImGui::SameLine();
    if (IconButton("##input", "Input", Glyph::Keyboard)) {
        app.dialog.showSettings = true;
        app.dialog.focusInput = true;
    }
    ImGui::SameLine();
    if (IconButton("##openoutput", "Output", Glyph::Folder) && selected != nullptr) {
        const std::string command = "xdg-open '" + selected->out + "' || open '" + selected->out + "' || explorer '" + selected->out + "'";
        std::system(command.c_str());
    }
    ImGui::SameLine();
    if (IconButton("##remove", "Remove", Glyph::Trash) && selected != nullptr) {
        app.games.erase(app.games.begin() + app.selected);
        app.selected = -1;
        app.Save();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    {
        const ImGuiStyle& style = ImGui::GetStyle();
        const ImVec2 barOrigin = ImGui::GetCursorScreenPos();
        const float barHeight = ImGui::GetFrameHeight();
        ImGui::Dummy(ImVec2(style.ItemSpacing.x * 2.0f, barHeight));
        ImGui::GetWindowDrawList()->AddLine(ImVec2(barOrigin.x + style.ItemSpacing.x, barOrigin.y + 3.0f), ImVec2(barOrigin.x + style.ItemSpacing.x, barOrigin.y + barHeight - 3.0f), ImGui::GetColorU32(ImGuiCol_Separator));
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(app.busy);
    if (IconButton("##addgame", "Add game", Glyph::Plus)) OpenAddDialog(app);
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (IconButton("##logs", "Logs", Glyph::Page)) app.dialog.logOpen = !app.dialog.logOpen;
    ImGui::SameLine();
    if (IconButton("##updates", "Updates", Glyph::Refresh)) app.CheckForUpdates();
    ImGui::SameLine();
    if (IconButton("##quit", "", Glyph::Close)) app.exitRequested = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Quit");
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
    const ImVec2 cardStart = ImGui::GetCursorScreenPos();
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
    if (!app.busy && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        app.selected = index;
        app.Launch(app.games[index]);
    }
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kIconSize + 12.0f);
    if (selected) {
        ImGui::TextColored(ImVec4(0.039f, 0.518f, 1.0f, 1.0f), "%s", game.title.c_str());
    } else {
        ImGui::Text("%s", game.title.c_str());
    }
    ImGui::PopTextWrapPos();
    if (!game.titleId.empty()) {
        ImGui::TextDisabled("%s", game.titleId.c_str());
    }
    ImGui::TextDisabled("%s", game.status.empty() ? "not converted" : game.status.c_str());
    ImGui::EndGroup();
    const ImVec2 cardEnd(cardStart.x + 148.0f, ImGui::GetCursorScreenPos().y);
    ImDrawList* cardDraw = ImGui::GetWindowDrawList();
    if (selected) {
        cardDraw->AddRect(ImVec2(cardStart.x - 4.0f, cardStart.y - 4.0f), ImVec2(cardEnd.x + 4.0f, cardEnd.y + 4.0f), IM_COL32(10, 132, 255, 60), 16.0f, 0, 5.0f);
        cardDraw->AddRect(cardStart, cardEnd, IM_COL32(10, 132, 255, 255), 13.0f, 0, 2.0f);
    } else if (ImGui::IsMouseHoveringRect(cardStart, cardEnd)) {
        cardDraw->AddRect(cardStart, cardEnd, IM_COL32(255, 255, 255, 36), 13.0f, 0, 1.0f);
    }
    ImGui::PopID();
}

void DrawGrid(App& app, void* renderer, float height) {
    const float width = ImGui::GetContentRegionAvail().x * 0.72f;
    const int columns = std::max(1, static_cast<int>(width / (kCellWidth + 14)));
    ImGui::BeginChild("games", ImVec2(width, height), ImGuiChildFlags_Borders);
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
            if (IconButton("##firstgame", "Add your first game", Glyph::Plus)) OpenAddDialog(app);
        }

        ImGui::EndTable();
    }
    ImGui::EndChild();
}

void DrawDetails(App& app, float height) {
    ImGui::SameLine();
    ImGui::BeginChild("details", ImVec2(0, height), ImGuiChildFlags_Borders);
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
        ImGui::TextColored(ImVec4(0.039f, 0.518f, 1.0f, 1.0f), "Status");
        ImGui::TextWrapped("%s", game.status.empty() ? "not converted" : game.status.c_str());
        ImGui::TextDisabled("Last run: %s", game.lastRun.empty() ? "never" : game.lastRun.c_str());
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.039f, 0.518f, 1.0f, 1.0f), "Paths");
        ImGui::TextDisabled("Dump: %s", game.dump.c_str());
        ImGui::TextDisabled("Out: %s", game.out.c_str());
        if (!game.input.empty()) {
            const std::string inputInfo = std::filesystem::path(game.input).filename().string() + " (" + ClassifyKind(game.input) + ")";
            ImGui::TextDisabled("Input: %s", inputInfo.c_str());
        } else {
            ImGui::TextDisabled("Input: not chosen");
        }
        if (!game.created.empty()) ImGui::TextDisabled("Added: %s", game.created.c_str());
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.039f, 0.518f, 1.0f, 1.0f), "Environment");
        std::map<std::string, std::string> effective = game.env;
        const auto defaultCache = (std::filesystem::path(game.out) / "shader_cache").string();
        const auto cached = effective.find("ANYPS5_SHADER_CACHE_DIR");
        const bool cacheDefault = cached == effective.end() || cached->second.empty();
        if (cacheDefault) effective["ANYPS5_SHADER_CACHE_DIR"] = defaultCache;
        for (const auto& [key, value] : effective) {
            if (value.empty()) continue;
            if (key == "ANYPS5_SHADER_CACHE_DIR" && cacheDefault) {
                ImGui::TextDisabled("%s = %s (default)", key.c_str(), value.c_str());
            } else {
                ImGui::TextDisabled("%s = %s", key.c_str(), value.c_str());
            }
        }
    }
    ImGui::EndChild();
}

void DrawStatusBar(App& app) {
    ImGui::Separator();
    ImGui::Text("%d games", static_cast<int>(app.games.size()));
    ImGui::SameLine();
    ImGui::TextDisabled("dump: %s", ShortPath(app.Config("lastDump", "")).c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("out: %s", ShortPath(app.Config("lastOut", "")).c_str());
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char clockText[8];
    std::strftime(clockText, sizeof(clockText), "%H:%M", &local);
    const float clockWidth = ImGui::CalcTextSize(clockText).x;
    const float clockX = ImGui::GetWindowWidth() - clockWidth - ImGui::GetStyle().WindowPadding.x;
    if (ImGui::GetCursorPosX() + 8.0f < clockX) {
        ImGui::SameLine(clockX);
        ImGui::TextDisabled("%s", clockText);
    }
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
    const auto lines = app.SnapshotLog();
    if (lines.empty()) {
        ImGui::TextDisabled("No output yet; convert, audit, or run a game to see it here");
    }
    for (const auto& line : lines) ImGui::TextWrapped("%s", line.c_str());
    ImGui::EndChild();
    ImGui::End();
}

}

namespace {

void CenterModal() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
}

}

void DrawModals(App& app) {
    if (app.busy) {
        if (!app.dialog.workingOpen) {
            app.dialog.workingOpen = true;
            CenterModal();
            ImGui::OpenPopup("Working");
        }
        if (ImGui::BeginPopupModal("Working", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
            ImGui::Text("%s", app.lastCommand.c_str());
            ImGui::SameLine();
            const int step = static_cast<int>(ImGui::GetFrameCount() / 20) % 4;
            ImGui::TextDisabled("%s", step == 0 ? "   " : step == 1 ? ".  " : step == 2 ? ".. " : "...");
            ImGui::TextDisabled("The log window shows live output; this closes when the task finishes.");
            ImGui::EndPopup();
        }
    } else {
        app.dialog.workingOpen = false;
    }
    if (app.lastFailed) {
        if (!app.dialog.failureOpen) {
            app.dialog.failureOpen = true;
            CenterModal();
            ImGui::OpenPopup("Action failed");
        }
        if (ImGui::BeginPopupModal("Action failed", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            std::string message;
            std::string logPath;
            {
                std::lock_guard<std::mutex> lock(app.logMutex);
                message = app.lastFailure;
                logPath = app.lastLog;
            }
            ImGui::TextWrapped("%s", message.c_str());
            if (!logPath.empty()) ImGui::TextDisabled("log: %s", logPath.c_str());
            if (ImGui::Button("Copy")) {
                ImGui::SetClipboardText((message + "\nlog: " + logPath).c_str());
            }
            ImGui::SameLine();
            if (!logPath.empty()) {
                if (ImGui::Button("Open log")) {
                    const std::string command = "xdg-open '" + logPath + "' || open '" + logPath + "' || explorer '" + logPath + "'";
                    std::system(command.c_str());
                }
                ImGui::SameLine();
            }
            if (ImGui::Button("Close")) {
                app.lastFailed = false;
                app.dialog.failureOpen = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    } else {
        app.dialog.failureOpen = false;
    }
}

void DrawUi(App& app, void* renderer, void* window) {
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowSize(viewport->WorkSize);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                                    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus;
    if (ImGui::Begin("AnyPS5", nullptr, flags)) {
        DrawToolbar(app);
        ImGui::Separator();
        const float contentHeight = ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeight() - 6.0f;
        DrawGrid(app, renderer, contentHeight);
        DrawDetails(app, contentHeight);
        DrawStatusBar(app);
    }
    ImGui::End();
    DrawLog(app);
    DrawDialogs(app);
    DrawModals(app);
}

}
