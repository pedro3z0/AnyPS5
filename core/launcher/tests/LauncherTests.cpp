#undef NDEBUG

#include "App.hpp"
#include "Library.hpp"
#include "Json.hpp"
#include "Ui.hpp"

#include "imgui.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <system_error>

namespace {

void CheckJson() {
    auto value = Json::Parse("{\"title\":\"ANIMAL WELL\",\"windows\":false,\"count\":2,\"ratio\":0.5,\"tags\":[\"a\",\"b\"]}");
    assert(value.find("title")->asString() == "ANIMAL WELL");
    assert(value.find("windows")->asBool(true) == false);
    assert(value.find("count")->asString() == "2");
    assert(value.find("tags")->array.size() == 2);
    auto again = Json::Parse(Json::Dump(value));
    assert(again.find("title")->asString() == "ANIMAL WELL");
    assert(again.find("ratio")->number == 0.5);
    std::cout << "json ok\n";
}

void CheckInputText() {
    std::map<std::string, std::vector<std::string>> bindings;
    int bad = 0;
    const std::string text = "Cross = KEY:F\nCross = KEY:Space\nSquare = MOUSE:Left\nToggleMouse = MOUSE:Middle\n";
    assert(Launcher::ParseInputText(text, bindings, bad));
    assert(bad == 0);
    assert(bindings.at("Cross").size() == 2);
    assert(Launcher::RenderInputText(bindings) == text);
    assert(!Launcher::ParseInputText("Bogus = KEY:A\n", bindings, bad));
    assert(bad == 1);
    assert(!Launcher::ParseInputText("Cross = KEY\n", bindings, bad));
    std::cout << "input ok\n";
}

void CheckCandidates() {
    const auto dump = std::filesystem::temp_directory_path() / "anyps5-launcher-fixture";
    std::error_code error;
    std::filesystem::remove_all(dump, error);
    std::filesystem::create_directories(dump / "sce_module", error);
    std::ofstream(dump / "eboot.bin", std::ios::binary) << "\x4f\x15\x3d\x1d" << std::string(8, '\0');
    std::ofstream(dump / "input.elf", std::ios::binary) << "\x7f" "ELF" << std::string(8, '\0');
    const auto candidates = Launcher::InputCandidates(dump);
    assert(candidates.size() == 2);
    assert(candidates.front().kind == "elf");
    assert(candidates.back().kind == "self");
    const auto game = Launcher::ReadTitleMeta(dump, Launcher::Game{});
    assert(game.title == dump.filename().string());
    std::filesystem::remove_all(dump, error);
    std::cout << "candidates ok\n";
}

void CheckNestedDump() {
    const auto dump = std::filesystem::temp_directory_path() / "anyps5-nested-fixture";
    std::error_code error;
    std::filesystem::remove_all(dump, error);
    std::filesystem::create_directories(dump / "APP0" / "sce_sys", error);
    std::ofstream(dump / "APP0" / "eboot.bin", std::ios::binary) << "\x7f" "ELF" << std::string(8, '\0');
    const auto candidates = Launcher::InputCandidates(dump);
    assert(candidates.size() == 1);
    assert(candidates.front().kind == "elf");
    assert(candidates.front().path == dump / "APP0" / "eboot.bin");
    std::ofstream(dump / "APP0" / "sce_sys" / "param.json") << "{\"titleId\":\"PPSA00000\",\"contentVersion\":\"01.000.001\"}";
    const auto game = Launcher::ReadTitleMeta(dump, Launcher::Game{});
    assert(game.titleId == "PPSA00000");
    Launcher::Game withInput;
    withInput.dump = dump.string();
    withInput.input = (dump / "APP0" / "eboot.bin").string();
    assert(Launcher::ContainerDir(withInput) == dump / "APP0");
    std::filesystem::remove_all(dump, error);
    std::cout << "nested dump ok\n";
}

void CheckToggles() {
    Launcher::Game game;
    assert(game.env.count("ANYPS5_VSYNC") == 0);
    std::cout << "toggles ok\n";
}

void CheckIconButton() {
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(800.0f, 600.0f);
    io.DeltaTime = 1.0f / 60.0f;
    io.Fonts->AddFontDefault();
    io.Fonts->Build();
    bool clicked = false;
    ImVec2 center(0.0f, 0.0f);
    for (int frame = 0; frame < 4; ++frame) {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(50.0f, 50.0f));
        ImGui::SetNextWindowSize(ImVec2(400.0f, 300.0f));
        ImGui::Begin("t");
        clicked = clicked || Launcher::IconButton("##test", "Test", Launcher::Glyph::Play);
        if (frame == 0) {
            const ImVec2 lo = ImGui::GetItemRectMin();
            const ImVec2 hi = ImGui::GetItemRectMax();
            center = ImVec2((lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f);
        }
        ImGui::End();
        ImGui::Render();
        if (frame > 0) io.AddMousePosEvent(center.x, center.y);
        if (frame == 1) io.AddMouseButtonEvent(0, true);
        if (frame == 2) io.AddMouseButtonEvent(0, false);
    }
    ImGui::DestroyContext();
    assert(clicked);
    std::cout << "icon button ok\n";
}

void CheckFailure() {
    Launcher::App app;
    app.Fail("probe failure");
    assert(app.lastFailed);
    assert(app.lastFailure == "probe failure");
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(800.0f, 600.0f);
    io.DeltaTime = 1.0f / 60.0f;
    io.Fonts->AddFontDefault();
    io.Fonts->Build();
    app.busy = true;
    app.lastCommand = "probe command";
    for (int frame = 0; frame < 3; ++frame) {
        ImGui::NewFrame();
        Launcher::DrawModals(app);
        ImGui::Render();
    }
    app.busy = false;
    for (int frame = 0; frame < 3; ++frame) {
        ImGui::NewFrame();
        Launcher::DrawModals(app);
        ImGui::Render();
    }
    ImGui::DestroyContext();
    std::cout << "failure popup ok\n";
}

void CheckAuditParse() {
    Launcher::Game game;
    game.status = "converted";
    const std::vector<std::string> lines = {
        "1753 references, 604 unique imports",
        "  implemented    443 imports     443 references",
        "  stub            18 imports      18 references",
        "  absent           4 imports       4 references",
        "  module         139 imports    1288 references",
        "",
        "Absent: no built library exports these, so the loader fails (4)",
        "  [Il2CppUserAssemblies.prx]",
        "    -pnj3-7a6QA",
        "",
        "Needed libraries with no file in --libs or --modules (1)",
        "  Il2CppUserAssemblies.prx: 5 imports",
    };
    Launcher::ApplyAuditLines(lines, game);
    assert(game.auditTotal == 604);
    assert(game.auditAbsent == 4);
    assert(game.auditStub == 18);
    assert(game.auditMissing == 1);
    assert(game.auditSummary.find("4 missing implementations") != std::string::npos);
    assert(game.auditSummary.find("1 libraries not found") != std::string::npos);
    Launcher::Game ready;
    ready.status = "converted";
    const std::vector<std::string> clean = {
        "1753 references, 604 unique imports",
        "  implemented    604 imports     604 references",
        "  stub             0 imports       0 references",
        "  absent           0 imports       0 references",
        "  module         139 imports    1288 references",
    };
    Launcher::ApplyAuditLines(clean, ready);
    assert(ready.auditAbsent == 0);
    assert(ready.auditMissing == 0);
    assert(ready.auditStub == 0);
    assert(ready.status == "ready");
    std::cout << "audit parse ok\n";
}

void CheckAuditRoundtrip() {
    const auto path = std::filesystem::temp_directory_path() / "anyps5-audit-fixture.json";
    Launcher::Game game;
    game.title = "Fixture";
    game.auditAbsent = 4;
    game.auditStub = 18;
    game.auditTotal = 604;
    game.auditMissing = 1;
    game.auditSummary = "604 unique imports, 4 missing implementations";
    game.lastError = "the game exited with code 1";
    Launcher::SaveLibraryAt(path, {game});
    const auto games = Launcher::LoadLibraryFrom(path);
    assert(games.size() == 1);
    assert(games[0].auditAbsent == 4);
    assert(games[0].auditMissing == 1);
    assert(games[0].auditStub == 18);
    assert(games[0].auditTotal == 604);
    assert(games[0].auditSummary == game.auditSummary);
    assert(games[0].lastError == game.lastError);
    std::error_code error;
    std::filesystem::remove(path, error);
    std::cout << "audit roundtrip ok\n";
}

void CheckLaunchGuards() {
    Launcher::App app;
    Launcher::Game blocked;
    blocked.title = "Blocked";
    blocked.auditAbsent = 4;
    app.games.push_back(blocked);
    assert(!app.Launch(0));
    assert(app.lastFailed);
    assert(app.lastFailure.find("cannot start") != std::string::npos);
    app.lastFailed = false;
    Launcher::Game clean;
    clean.title = "NoFile";
    app.games.push_back(clean);
    assert(!app.Launch(1));
    assert(app.lastFailed);
    assert(app.lastFailure.find("convert first") != std::string::npos);
    app.lastFailed = false;
    app.runningGame = 0;
    assert(!app.Launch(1));
    assert(app.lastFailed);
    assert(app.lastFailure.find("already running") != std::string::npos);
    app.lastFailed = false;
    assert(!app.Convert(1));
    assert(app.lastFailed);
    assert(app.lastFailure.find("is running") != std::string::npos);
    app.lastFailed = false;
    assert(!app.Audit(1));
    assert(app.lastFailed);
    app.runningGame = -1;
    app.lastFailed = false;
    app.busy = true;
    assert(!app.Launch(1));
    assert(app.lastFailed);
    assert(app.lastFailure.find("another task") != std::string::npos);
    app.busy = false;
    app.lastFailed = false;
    assert(app.IsRunning(0) == false);
    app.runningGame = 3;
    assert(app.IsRunning(3));
    assert(!app.IsRunning(1));
    std::cout << "launch guards ok\n";
}

}

int main() {
    CheckJson();
    CheckInputText();
    CheckCandidates();
    CheckNestedDump();
    CheckToggles();
    CheckIconButton();
    CheckFailure();
    CheckAuditParse();
    CheckAuditRoundtrip();
    CheckLaunchGuards();
    std::cout << "launcher tests ok\n";
}
