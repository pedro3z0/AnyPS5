#include "Ui.hpp"

#include "imgui.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace Launcher {

namespace {

void DrawFileBrowser(App& app) {
    if (!app.dialog.showBrowser) return;
    const bool pickDump = app.dialog.browserTarget == "dump";
    if (app.dialog.browserPath.empty()) {
        app.dialog.browserPath = app.Config(pickDump ? "lastDump" : "lastOut", "");
        if (app.dialog.browserPath.empty()) {
            const char* home = std::getenv("HOME");
            const std::filesystem::path downloads = (home != nullptr && home[0] != '\0' ? std::filesystem::path(home) / "Downloads" : std::filesystem::current_path());
            app.dialog.browserPath = std::filesystem::is_directory(downloads) ? downloads.string() : app.root.string();
        }
    }
    if (!ImGui::Begin(pickDump ? "Choose the dump folder" : "Choose the output folder", &app.dialog.showBrowser)) {
        ImGui::End();
        return;
    }
    std::error_code error;
    if (ImGui::Button("up")) {
        const std::filesystem::path here(app.dialog.browserPath);
        const std::filesystem::path parent = here.parent_path();
        if (!parent.empty()) app.dialog.browserPath = parent.string();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    TextInput("path", app.dialog.browserPath, "type a path and press enter");
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        const std::filesystem::path typed = std::filesystem::path(app.dialog.browserPath).lexically_normal();
        if (std::filesystem::is_directory(typed, error)) app.dialog.browserPath = typed.string();
    }
    if (!ImGui::IsItemActive()) {
        const std::filesystem::path current(app.dialog.browserPath);
        if (current.is_absolute() && !std::filesystem::is_directory(current, error)) {
            app.dialog.browserPath = current.parent_path().string();
        }
    }
    const std::filesystem::path full(app.dialog.browserPath);
    if (full.is_absolute()) {
        std::filesystem::path acc;
        int crumb = 0;
        for (const auto& part : full) {
            const std::string label = part.string();
            if (label.empty()) continue;
            acc /= part;
            if (crumb > 0) {
                ImGui::SameLine(0, 3);
                ImGui::TextDisabled(">");
                ImGui::SameLine(0, 3);
            }
            ImGui::PushID(crumb);
            crumb++;
            if (ImGui::SmallButton(label.c_str())) app.dialog.browserPath = acc.string();
            ImGui::PopID();
        }
    }
    ImGui::Separator();
    const std::filesystem::path path(app.dialog.browserPath);
    if (ImGui::BeginChild("entries")) {
        std::vector<std::filesystem::path> directories;
        for (const auto& item : std::filesystem::directory_iterator(path, error)) {
            if (item.is_directory()) directories.push_back(item.path());
        }
        std::sort(directories.begin(), directories.end());
        for (const auto& directory : directories) {
            const std::string name = directory.filename().string();
            if (name.empty()) continue;
            if (ImGui::Selectable((name + "/").c_str())) app.dialog.browserPath = directory.string();
        }
    }
    ImGui::EndChild();
    const bool isDirectory = std::filesystem::is_directory(path, error);
    ImGui::BeginDisabled(!isDirectory);
    if (ImGui::Button("use this folder", ImVec2(200, 0))) {
        if (pickDump) app.dialog.dump = path.string();
        else app.dialog.out = path.string();
        app.dialog.browserPath.clear();
        app.dialog.browserTarget.clear();
        app.dialog.showBrowser = false;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("cancel")) {
        app.dialog.browserPath.clear();
        app.dialog.browserTarget.clear();
        app.dialog.showBrowser = false;
    }
    ImGui::End();
}


void DrawAddDialog(App& app) {
    if (!app.dialog.showAdd) return;
    if (ImGui::Begin("Add game", &app.dialog.showAdd, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::SetNextItemWidth(420.0f);
        TextInput("name", app.dialog.name, "display name (defaults to the dump title)");
        ImGui::SetNextItemWidth(420.0f);
        TextInput("dump", app.dialog.dump, "folder containing the dumped game");
        ImGui::SameLine();
        if (ImGui::Button("browse dump")) {
            app.dialog.browserPath = app.dialog.dump.empty() ? app.Config("lastDump", "") : app.dialog.dump;
            if (app.dialog.browserPath.empty()) app.dialog.browserPath = app.root.string();
            app.dialog.browserTarget = "dump";
            app.dialog.showBrowser = true;
        }
        if (app.dialog.scanKey != app.dialog.dump) {
            app.dialog.scanKey = app.dialog.dump;
            app.dialog.candidates = InputCandidates(app.dialog.dump);
            app.dialog.chosen = 0;
            const std::filesystem::path dumpDir(app.dialog.dump);
            if (app.dialog.name.empty() && std::filesystem::is_directory(dumpDir)) {
                app.dialog.name = ReadTitleMeta(dumpDir, Game{}).title;
            }
        }
        if (ImGui::Button("scan")) {
            app.dialog.scanKey = app.dialog.dump;
            app.dialog.candidates = InputCandidates(app.dialog.dump);
            const auto game = ReadTitleMeta(std::filesystem::path(app.dialog.dump), Game{});
            if (app.dialog.name.empty()) app.dialog.name = game.title;
            app.dialog.chosen = 0;
        }
        ImGui::SameLine();

        if (app.dialog.candidates.empty()) {
            ImGui::TextDisabled("no files scanned");
        } else {
            const auto& chosen = app.dialog.candidates[app.dialog.chosen];
            ImGui::SetNextItemWidth(280.0f);
            if (ImGui::BeginCombo("input", chosen.path.filename().string().c_str())) {
                for (int i = 0; i < static_cast<int>(app.dialog.candidates.size()); i++) {
                    const auto& candidate = app.dialog.candidates[i];
                    const std::string label = candidate.path.filename().string() + " (" + candidate.kind + ")";
                    const bool selected = app.dialog.chosen == i;
                    if (ImGui::Selectable(label.c_str(), selected)) app.dialog.chosen = i;
                }
                ImGui::EndCombo();
            }
        }
        ImGui::SetNextItemWidth(420.0f);
        TextInput("out", app.dialog.out, "output directory for the converted game");
        ImGui::SameLine();
        if (ImGui::Button("browse out")) {
            app.dialog.browserPath = app.dialog.out.empty() ? app.Config("lastOut", "") : app.dialog.out;
            if (app.dialog.browserPath.empty()) app.dialog.browserPath = app.root.string();
            app.dialog.browserTarget = "out";
            app.dialog.showBrowser = true;
        }

        ImGui::Checkbox("windows", &app.dialog.windows);
        ImGui::SameLine();
        ImGui::Checkbox("intel", &app.dialog.intel);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80.0f);
        const char* filters[] = {"0", "1", "2"};
        ImGui::Combo("unused-filter", &app.dialog.filter, filters, 3);
        if (!app.dialog.error.empty()) {
            ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1.0f), "%s", app.dialog.error.c_str());
        }
        if (ImGui::Button("add")) {
            const auto dump = std::filesystem::path(app.dialog.dump);
            if (app.dialog.name.empty()) {
                app.dialog.error = "a name is required";
            } else if (!std::filesystem::is_directory(dump)) {
                app.dialog.error = "dump dir is missing";
            } else if (app.dialog.candidates.empty()) {
                app.dialog.error = "scan the dump first";
            } else if (app.dialog.candidates[app.dialog.chosen].kind != "elf") {
                app.dialog.error = "input is a SELF or PKG container; pick the decrypted ELF";
            } else {
                Game game = ReadTitleMeta(dump, Game{});
                game.name = app.dialog.name;
                game.input = app.dialog.candidates[app.dialog.chosen].path.string();
                game.out = app.dialog.out;
                game.windows = app.dialog.windows;
                game.intel = app.dialog.intel;
                game.filter = filters[app.dialog.filter];
                game.created = Timestamp();
                app.games.push_back(game);
                app.Save();
                app.SetConfig("lastDump", game.dump);
                app.SetConfig("lastOut", game.out);
                app.selected = static_cast<int>(app.games.size()) - 1;
                app.dialog = {};
                app.dialog.showAdd = false;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("cancel")) {
            app.dialog = {};
            app.dialog.showAdd = false;
        }
    }
    ImGui::End();
}


bool Toggle(Game& game, const char* key, bool active) {
    const auto it = game.env.find(key);
    const bool current = it != game.env.end() && !it->second.empty() && it->second != "0";
    if (current == active) return false;
    if (active) game.env[key] = "1";
    else if (std::string(key) == "ANYPS5_VSYNC") game.env[key] = "0";
    else game.env.erase(key);
    return true;
}

void DrawGraphicsTab(Game& game) {
    ImGui::TextColored(ImVec4(0.4f, 0.67f, 0.97f, 1.0f), "Display");
    bool vsync = game.env.find("ANYPS5_VSYNC") == game.env.end() || game.env.at("ANYPS5_VSYNC") != "0";
    if (ImGui::Checkbox("Vsync", &vsync)) Toggle(game, "ANYPS5_VSYNC", vsync);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Off switches the swapchain to mailbox or immediate present modes");
    }
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.4f, 0.67f, 0.97f, 1.0f), "Shaders");
    ImGui::SetNextItemWidth(-1.0f);
    TextInput("ANYPS5_GPU", game.env["ANYPS5_GPU"], "Vulkan device name filter, e.g. RX 580 or NVIDIA");
    ImGui::SetNextItemWidth(-1.0f);
    TextInput("shader cache dir", game.env["ANYPS5_SHADER_CACHE_DIR"], "defaults to shader_cache beside the game");

    bool cache = game.env.find("ANYPS5_NO_SHADER_CACHE") == game.env.end() || game.env.at("ANYPS5_NO_SHADER_CACHE").empty();
    if (ImGui::Checkbox("disk shader cache", &cache)) Toggle(game, "ANYPS5_NO_SHADER_CACHE", cache);
    bool stats = game.env.count("APS5_PIPELINE_STATS") != 0 && !game.env.at("APS5_PIPELINE_STATS").empty();
    if (ImGui::Checkbox("pipeline statistics", &stats)) Toggle(game, "APS5_PIPELINE_STATS", stats);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Requires VK_KHR_pipeline_executable_properties; unsupported devices fail");
    }
}

void DrawAdvancedTab(Game& game) {
    ImGui::TextColored(ImVec4(0.4f, 0.67f, 0.97f, 1.0f), "Fonts");
    ImGui::SetNextItemWidth(-1.0f);
    TextInput("ANYPS5_SYSTEM_FONTS", game.env["ANYPS5_SYSTEM_FONTS"], "directory with SST-*.otf or Noto substitutes");

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.4f, 0.67f, 0.97f, 1.0f), "Logging");
    bool trace = game.env.count("ANYPS5_NGS2_TRACE") != 0 && !game.env.at("ANYPS5_NGS2_TRACE").empty();
    if (ImGui::Checkbox("NGS2 voice param trace", &trace)) Toggle(game, "ANYPS5_NGS2_TRACE", trace);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Dumps NGS2 voice params to stderr for debugging missing implementations");
    }
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.4f, 0.67f, 0.97f, 1.0f), "Other");
    ImGui::SetNextItemWidth(-1.0f);
    TextInput("ANYPS5_ENTITLEMENTS", game.env["ANYPS5_ENTITLEMENTS"], "entitlements file path");

    const auto ini = InputPath(game);
    ImGui::TextDisabled("input mapping: %s", ini.string().c_str());
}

}
void DrawInputTab(App& app, Game& game) {
    const auto ini = InputPath(game);
    std::map<std::string, std::vector<std::string>> bindings;
    int bad = 0;
    std::string text;
    if (std::ifstream reader{ini}) {
        std::stringstream buffer;
        buffer << reader.rdbuf();
        text = buffer.str();
    }
    ParseInputText(text, bindings, bad);
    ImGui::TextDisabled("Bind sources as KEY:Value, MOUSE:Left or WHEEL:Up; see INPUT_MAPPING.md");
    if (ImGui::BeginChild("actions", ImVec2(0, 300))) {

        for (const auto& action : InputActions()) {
            ImGui::PushID(action.c_str());
            std::string value;
            for (const auto& source : bindings[action]) {
                if (!value.empty()) value += ", ";
                value += source;
            }
            ImGui::SetNextItemWidth(-1.0f);
            TextInput(action.c_str(), value, "e.g. KEY:F or MOUSE:Left");

            bindings[action].clear();
            std::stringstream stream(value);
            std::string part;
            while (std::getline(stream, part, ',')) {
                const auto first = part.find_first_not_of(" ");
                const auto last = part.find_last_not_of(" ");
                if (first == std::string::npos) continue;
                bindings[action].push_back(part.substr(first, last - first + 1));
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    if (!app.inputError.empty()) {
        ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1.0f), "%s", app.inputError.c_str());
    }
    if (ImGui::Button("save input")) {
        app.inputError.clear();
        for (const auto& [action, sources] : bindings) {
            for (const auto& source : sources) {
                if (source.find(':') == std::string::npos) {
                    app.inputError = action + ": use KEY:Value, MOUSE:Value or WHEEL:Up|Down";
                    break;
                }
            }
            if (!app.inputError.empty()) break;
        }
        if (app.inputError.empty()) {
            std::error_code error;
            std::filesystem::create_directories(ini.parent_path(), error);
            if (std::ofstream save{ini, std::ios::binary | std::ios::trunc}) save << RenderInputText(bindings);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("reset input")) {
        std::error_code error;
        std::filesystem::remove(ini, error);
    }
}

void DrawSettingsDialog(App& app) {
    if (!app.dialog.showSettings) return;
    if (app.selected < 0 || app.selected >= static_cast<int>(app.games.size())) {
        app.dialog.showSettings = false;
        return;
    }
    auto& game = app.games[app.selected];
    ImGui::SetNextWindowSize(ImVec2(560, 520), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Settings", &app.dialog.showSettings)) {

        ImGui::TextColored(ImVec4(0.902f, 0.925f, 0.953f, 1.0f), "%s", game.title.c_str());
        ImGui::Separator();
        if (ImGui::BeginTabBar("settings")) {
            const bool wantInput = app.dialog.focusInput;
            app.dialog.focusInput = false;
            if (ImGui::BeginTabItem("Graphics")) {
                DrawGraphicsTab(game);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Input", nullptr, wantInput ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None)) {
                DrawInputTab(app, game);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Advanced")) {
                DrawAdvancedTab(game);
                ImGui::EndTabItem();
            }
            app.dialog.focusInput = false;
            ImGui::EndTabBar();
        }
        ImGui::Separator();
        if (ImGui::Button("save settings")) {
            app.Save();
            app.dialog.showSettings = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("cancel")) app.dialog.showSettings = false;
    }
    ImGui::End();
}

void DrawSetup(App& app) {
    if (app.IsConfigured()) return;
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float contentWidth = std::min(avail.x - 40.0f, 760.0f);
    ImGui::Dummy(ImVec2(0, 20));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10, 14));
    ImGui::BeginGroup();
    ImGui::PushTextWrapPos(contentWidth);
    ImGui::SetWindowFontScale(1.6f);
    ImGui::TextColored(ImVec4(0.4f, 0.67f, 0.97f, 1.0f), "Welcome to AnyPS5");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::TextWrapped("Pick where your dumped games live and where converted output goes. "
                       "Both can be changed later; nothing is written outside the directories you choose.");
    ImGui::PopTextWrapPos();
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.4f, 0.67f, 0.97f, 1.0f), "Default dump folder");
    ImGui::PushTextWrapPos(contentWidth);
    ImGui::TextDisabled("%s", app.dialog.dump.empty() ? "no folder chosen yet" : app.dialog.dump.c_str());
    ImGui::PopTextWrapPos();
    if (ImGui::Button("Choose dump folder", ImVec2(220, 0))) {
        app.dialog.browserTarget = "dump";
        app.dialog.browserPath = app.dialog.dump.empty() ? app.Config("lastDump", "") : app.dialog.dump;
        app.dialog.showBrowser = true;
    }

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.4f, 0.67f, 0.97f, 1.0f), "Default output folder");
    ImGui::PushTextWrapPos(contentWidth);
    ImGui::TextDisabled("%s", app.dialog.out.empty() ? "no folder chosen yet" : app.dialog.out.c_str());
    ImGui::PopTextWrapPos();
    if (ImGui::Button("Choose output folder", ImVec2(220, 0))) {
        app.dialog.browserTarget = "out";
        app.dialog.browserPath = app.dialog.out.empty() ? app.Config("lastOut", "") : app.dialog.out;
        app.dialog.showBrowser = true;
    }

    ImGui::Separator();
    const bool ready = !app.dialog.dump.empty() && !app.dialog.out.empty();
    ImGui::BeginDisabled(!ready);
    if (ImGui::Button("Get started", ImVec2(220, 0))) {
        app.SetConfig("lastDump", app.dialog.dump);
        app.SetConfig("lastOut", app.dialog.out);
        app.dialog.name = "";
        app.dialog.showBrowser = false;
    }
    ImGui::EndDisabled();
    ImGui::EndGroup();
    ImGui::PopStyleVar();
}

void DrawDialogs(App& app) {
    DrawAddDialog(app);
    DrawSettingsDialog(app);
    DrawFileBrowser(app);
}

}

