#include "App.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <system_error>

namespace Launcher {

namespace {

std::string shellQuote(const std::string& text) {
    std::string out = "'";
    for (const char value : text) {
        if (value == '\'') out += "'\\''";
        else out += value;
    }
    return out + "'";
}

std::string join(const std::vector<std::string>& parts) {
    std::string out;
    bool first = true;
    for (const auto& part : parts) {
        if (!first) out += " ";
        first = false;
        out += shellQuote(part);
    }
    return out;
}

std::string envPrefix(const std::map<std::string, std::string>& env) {
    std::string out;
    for (const auto& [key, value] : env) {
        if (!value.empty()) out += key + "=" + shellQuote(value) + " ";
    }
    return out;
}

std::string stamp() {
    const std::time_t time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &time);
#else
    localtime_r(&time, &local);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &local);
    return buffer;
}

std::filesystem::path newestRegistry(const std::filesystem::path& out) {
    std::filesystem::path newest;
    std::error_code error;
    if (!std::filesystem::is_directory(out, error)) return newest;
    auto newestTime = std::filesystem::file_time_type::min();
    for (const auto& item : std::filesystem::directory_iterator(out)) {
        if (!item.is_regular_file() || item.path().extension() != ".registry.json") continue;
        const auto time = item.last_write_time(error);
        if (error) continue;
        if (time > newestTime) {
            newestTime = time;
            newest = item.path();
        }
    }
    return newest;
}

}

void App::Load() {
    games = LoadLibrary();
    config = ::Launcher::LoadConfig();
    if (Config("lastDump", "").empty() || Config("lastOut", "").empty()) {
        const char* homeValue = std::getenv("HOME");
        if (homeValue == nullptr || homeValue[0] == '\0') homeValue = std::getenv("USERPROFILE");
        const std::filesystem::path base = homeValue != nullptr && homeValue[0] != '\0' ? std::filesystem::path(homeValue) : std::filesystem::current_path();
        std::error_code error;
        const std::filesystem::path launcher = base / "anyps5-launcher";
        std::filesystem::create_directories(launcher / "dumps", error);
        std::filesystem::create_directories(launcher / "games", error);
        if (Config("lastDump", "").empty()) config["lastDump"] = (launcher / "dumps").string();
        if (Config("lastOut", "").empty()) config["lastOut"] = (launcher / "games").string();
        ::Launcher::SaveConfig(config);
    }
}

void App::Save() const {
    SaveLibrary(games);
}

void App::SaveConfig() {
    ::Launcher::SaveConfig(config);
}

std::string App::Config(const std::string& key, const std::string& fallback) const {
    const auto it = config.find(key);
    return it == config.end() ? fallback : it->second;
}

void App::SetConfig(const std::string& key, const std::string& value) {
    config[key] = value;
    ::Launcher::SaveConfig(config);
}

bool App::IsConfigured() const {
    return !games.empty() || !Config("lastDump", "").empty();
}

std::filesystem::path App::RelinkerPath() const {
    const auto beside = executableDir / ("relinker"
#ifdef _WIN32
        ".exe"
#endif
    );
    if (std::filesystem::is_regular_file(beside)) return beside;
    const auto build = root / "build" / "core" / "relinker" / "relinker";
    if (std::filesystem::is_regular_file(build)) return build;
    const auto stage = root / "build-relinker" / "core" / "relinker" / "relinker";
    if (std::filesystem::is_regular_file(stage)) return stage;
    return {};
}

std::filesystem::path App::LibrariesPath() const {
    const auto source = root / "build" / "core" / "libs" / "libs";
    if (std::filesystem::is_directory(source)) return source;
    const auto installed = executableDir / ".." / "lib" / "anyps5" / "libs";
    if (std::filesystem::is_directory(installed)) return installed;
    const auto sibling = executableDir / "libs";
    if (std::filesystem::is_directory(sibling)) return sibling;
    return {};
}

void App::AppendLog(const std::string& line) {
    std::lock_guard<std::mutex> lock(logMutex);
    logLines.push_back(line);
    if (logLines.size() > 500) logLines.erase(logLines.begin(), logLines.begin() + 250);
}

std::vector<std::string> App::SnapshotLog() {
    std::lock_guard<std::mutex> lock(logMutex);
    return logLines;
}

void App::StartCommand(const std::string& label, const std::vector<std::string>& args, const std::map<std::string, std::string>& env) {
    if (busy) return;
    busy = true;
    lastCommand = label;
    AppendLog("$ " + label);
    const auto log = LogDirectory() / (LogStamp() + "-" + label.substr(0, label.find(' ')) + ".log");
    lastLog = log.string();
    std::error_code error;
    std::filesystem::create_directories(LogDirectory(), error);
    std::thread([this, args, env, log] {
        const std::string command = envPrefix(env) + join(args);
        std::FILE* pipe = popen(command.c_str(), "r");
        if (pipe != nullptr) {
            std::ofstream file(log, std::ios::binary | std::ios::trunc);
            char buffer[4096];
            while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) {
                std::string line(buffer);
                while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
                AppendLog(line);
                if (file) file << stamp() << " " << line << "\n";
            }
            if (file) file.flush();
            pclose(pipe);
        }
        busy = false;
    }).detach();
}

bool App::Convert(const Game& game) {
    const auto relinker = RelinkerPath();
    if (relinker.empty()) {
        AppendLog("FAIL: relinker binary not found beside the launcher or under " + root.string());
        return false;
    }
    const std::string kind = ClassifyKind(std::filesystem::path(game.input));
    if (kind != "elf") {
        AppendLog("FAIL: " + game.input + " is " + kind + "; the relinker needs a clean ELF (decrypt the eboot first)");
        return false;
    }
    const auto out = std::filesystem::path(game.out);
    std::error_code error;
    std::filesystem::create_directories(out, error);
    std::vector<std::string> args = {relinker.string()};
    if (game.windows) args.push_back("--windows");
    if (game.intel) args.push_back("--to-intel");
    args.push_back("unused-filter=" + game.filter);
    args.push_back("--registry");
    args.push_back(game.input);
    args.push_back(ExecutablePath(game).string());
    StartCommand("convert", args, {});
    return true;
}

bool App::Audit(const Game& game) {
    const auto registry = newestRegistry(std::filesystem::path(game.out));
    if (registry.empty()) {
        AppendLog("FAIL: no .registry.json in " + game.out);
        return false;
    }
    const auto audit = root / "tools" / "import_audit.py";
    const auto libs = LibrariesPath();
    if (!std::filesystem::is_regular_file(audit)) {
        AppendLog("audit needs the source tree (tools/import_audit.py); skipped");
        return false;
    }
    if (libs.empty()) {
        AppendLog("FAIL: no built .prx directory; run cmake --build build --target libs first");
        return false;
    }
    std::vector<std::string> args = {"python3", audit.string(), registry.string(), "--libs", libs.string()};
    const auto modules = ContainerDir(game) / "sce_module";
    if (std::filesystem::is_directory(modules)) {
        args.push_back("--modules");
        args.push_back(modules.string());
    }
    StartCommand("audit", args, {});
    return true;
}

bool App::Launch(const Game& game) {
    const auto exe = ExecutablePath(game);
    if (!std::filesystem::is_regular_file(exe)) {
        AppendLog("FAIL: " + exe.string() + " missing; convert first");
        return false;
    }
    std::error_code error;
    const auto libs = LibrariesPath();
    if (!libs.empty()) {
        std::filesystem::create_directories(exe.parent_path() / "libs", error);
        for (const auto& item : std::filesystem::directory_iterator(libs)) {
            if (item.path().extension() != ".prx") continue;
            const auto target = exe.parent_path() / "libs" / item.path().filename();
            std::filesystem::copy_file(item.path(), target, std::filesystem::copy_options::skip_existing, error);
        }
    }
    const auto sys = ContainerDir(game) / "sce_sys";
    if (std::filesystem::is_directory(sys, error)) {
        std::filesystem::create_directories(exe.parent_path() / "app0" / "sce_sys", error);
        for (const auto& item : std::filesystem::directory_iterator(sys)) {
            const auto target = exe.parent_path() / "app0" / "sce_sys" / item.path().filename();
            std::filesystem::copy(item.path(), target, std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, error);
        }
    }
    std::map<std::string, std::string> env = GameEnv(game);
    if (env.find("ANYPS5_SHADER_CACHE_DIR") == env.end() || env.at("ANYPS5_SHADER_CACHE_DIR").empty()) {
        env["ANYPS5_SHADER_CACHE_DIR"] = (exe.parent_path() / "shader_cache").string();
    }
    StartCommand("run", {exe.string()}, env);
    return true;
}

}
