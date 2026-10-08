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
}

void App::Save() const {
    SaveLibrary(games);
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
    const auto relinker = root / "build-relinker" / "core" / "relinker" / "relinker";
    if (!std::filesystem::exists(relinker) && !std::filesystem::exists(root / "build" / "core" / "relinker" / "relinker")) {
        AppendLog("FAIL: relinker binary not found under " + root.string());
        return false;
    }
    std::vector<std::string> args = {"bash", (root / "tools" / "convert.sh").string(),
                                     "--dump", game.dump, "--out", game.out, "--input", game.input,
                                     "--unused-filter", game.filter};
    if (game.windows) args.push_back("--windows");
    if (game.intel) args.push_back("--to-intel");
    StartCommand("convert", args, {});
    return true;
}

bool App::Audit(const Game& game) {
    const auto registry = newestRegistry(std::filesystem::path(game.out));
    if (registry.empty()) {
        AppendLog("FAIL: no .registry.json in " + game.out);
        return false;
    }
    std::vector<std::string> args = {"python3", (root / "tools" / "import_audit.py").string(),
                                     registry.string(), "--libs", (root / "build" / "core" / "libs" / "libs").string()};
    const auto modules = std::filesystem::path(game.dump) / "sce_module";
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
    std::map<std::string, std::string> env = GameEnv(game);
    if (env.find("ANYPS5_SHADER_CACHE_DIR") == env.end() || env.at("ANYPS5_SHADER_CACHE_DIR").empty()) {
        env["ANYPS5_SHADER_CACHE_DIR"] = (exe.parent_path() / "shader_cache").string();
    }
    std::vector<std::string> args = {"bash", (root / "tools" / "run.sh").string(), "--game", exe.string()};
    const auto dump = std::filesystem::path(game.dump);
    if (std::filesystem::is_directory(dump / "sce_sys")) {
        args.push_back("--app0");
        args.push_back(dump.string());
    }
    StartCommand("run", args, env);
    return true;
}

}
