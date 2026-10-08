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
    if (app.dialog.browserPath.empty()) app.dialog.browserPath = app.root.string();
    if (!ImGui::Begin("Browse", &app.dialog.showBrowser)) {
        ImGui::End();
        return;
    }
    ImGui::SetNextItemWidth(480.0f);
    TextInput("path", app.dialog.browserPath);
    ImGui::Separator();
    const std::filesystem::path path(app.dialog.browserPath);
    std::error_code error;
    if (ImGui::Button("up")) {
        app.dialog.browserPath = path.parent_path().string();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", app.dialog.browserPath.c_str());
    ImGui::Separator();
    if (ImGui::BeginChild("entries")) {
        std::vector<std::filesystem::path> directories;
        std::vector<std::filesystem::path> files;
        for (const auto& item : std::filesystem::directory_iterator(path, error)) {
            if (item.is_directory()) directories.push_back(item.path());
            else files.push_back(item.path());
        }
        std::sort(directories.begin(), directories.end());
        std::sort(files.begin(), files.end());
        for (const auto& directory : directories) {
            const std::string name = directory.filename().string() + "/";
            if (ImGui::Selectable(name.c_str())) app.dialog.browserPath = directory.string();
        }
        for (const auto& file : files) {
            if (ImGui::Selectable(file.filename().string().c_str())) app.dialog.browserPath = file.string();
        }
    }
    ImGui::EndChild();
    if (ImGui::Button("select")) {
        if (app.dialog.browserPath.rfind("dump:", 0) == 0) app.dialog.dump = app.dialog.browserPath.substr(5);
        else if (app.dialog.browserPath.rfind("out:", 0) == 0) app.dialog.out = app.dialog.browserPath.substr(4);
        else app.dialog.out = app.dialog.browserPath;
        app.dialog.browserPath.clear();
        app.dialog.showBrowser = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("cancel")) {
        app.dialog.browserPath.clear();
        app.dialog.showBrowser = false;
    }
    ImGui::End();
}
void DrawAddDialog(App& app) {
    if (!app.dialog.showAdd) return;
    if (ImGui::Begin("Add game", &app.dialog.showAdd, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::SetNextItemWidth(420.0f);
        TextInput("name", app.dialog.name);
        ImGui::SameLine();
        if (ImGui::Button("browse dump")) {
            app.dialog.browserPath = "dump:" + (app.dialog.dump.empty() ? app.root.string() : app.dialog.dump);
            app.dialog.showBrowser = true;
        }
        ImGui::TextDisabled("%s", app.dialog.dump.c_str());
        if (ImGui::Button("scan")) {
            app.dialog.candidates = InputCandidates(app.dialog.dump);
            const auto game = ReadTitleMeta(std::filesystem::path(app.dialog.dump), Game{});
            if (app.dialog.name.empty() || app.dialog.name == "name") app.dialog.name = game.title;
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
        TextInput("out", app.dialog.out);
        ImGui::SameLine();
        if (ImGui::Button("browse out")) {
            app.dialog.browserPath = "out:" + (app.dialog.out.empty() ? app.root.string() : app.dialog.out);
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
            if (app.dialog.name.empty() || app.dialog.name == "name") {
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
    DrawFileBrowser(app);
}

void DrawSettingsDialog(App& app) {
    if (!app.dialog.showSettings) return;
    if (app.selected < 0 || app.selected >= static_cast<int>(app.games.size())) {
        app.dialog.showSettings = false;
        return;
    }
    auto& game = app.games[app.selected];
    if (ImGui::Begin("Settings", &app.dialog.showSettings, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextDisabled("%s", game.title.c_str());
        ImGui::Separator();
        for (const auto& key : EnvFields()) {
            ImGui::PushID(key.c_str());
            ImGui::SetNextItemWidth(360.0f);
            TextInput(key.c_str(), game.env[key]);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", EnvHint(key).c_str());
            }
            ImGui::PopID();
        }
        ImGui::Separator();
        ImGui::SetNextItemWidth(360.0f);
        TextInput("shader cache dir", app.dialog.cache);
        ImGui::SameLine();
        if (ImGui::Button("clear cache")) {
            const auto dir = std::filesystem::path(app.dialog.cache);
            std::error_code error;
            auto count = 0;
            if (std::filesystem::is_directory(dir, error)) {
                for (const auto& item : std::filesystem::directory_iterator(dir)) {
                    (void)item;
                    count++;
                }
                std::filesystem::remove_all(dir, error);
            }
            app.dialog.cache = std::string("cleared ") + std::to_string(count) + " entries";
        }
        if (!app.dialog.error.empty()) {
            ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1.0f), "%s", app.dialog.error.c_str());
        }
        if (ImGui::Button("save")) {
            app.Save();
            app.dialog.showSettings = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("cancel")) app.dialog.showSettings = false;
    }
    ImGui::End();
}

void DrawInputDialog(App& app) {
    if (!app.dialog.showInput) return;
    if (app.selected < 0 || app.selected >= static_cast<int>(app.games.size())) {
        app.dialog.showInput = false;
        return;
    }
    const auto& game = app.games[app.selected];
    const auto ini = InputPath(game);
    if (ImGui::Begin("Input", &app.dialog.showInput, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextDisabled("%s", ini.string().c_str());
        ImGui::Separator();
        std::map<std::string, std::vector<std::string>> bindings;
        int bad = 0;
        std::string text;
        if (std::ifstream reader{ini}) {
            std::stringstream buffer;
            buffer << reader.rdbuf();
            text = buffer.str();
        }
        ParseInputText(text, bindings, bad);
        for (const auto& action : InputActions()) {
            ImGui::PushID(action.c_str());
            std::string value;
            for (const auto& source : bindings[action]) {
                if (!value.empty()) value += ", ";
                value += source;
            }
            ImGui::SetNextItemWidth(320.0f);
            TextInput(action.c_str(), value);
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
        if (!app.inputError.empty()) {
            ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1.0f), "%s", app.inputError.c_str());
        }
        if (ImGui::Button("save")) {
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
                app.dialog.showInput = false;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("cancel")) app.dialog.showInput = false;
    }
    ImGui::End();
}

}

void DrawDialogs(App& app) {
    DrawAddDialog(app);
    DrawSettingsDialog(app);
    DrawInputDialog(app);
}

}

