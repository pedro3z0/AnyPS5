#include "Library.hpp"
#include "Json.hpp"

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

}

int main() {
    CheckJson();
    CheckInputText();
    CheckCandidates();
    CheckNestedDump();
    CheckToggles();
    std::cout << "launcher tests ok\n";
}
