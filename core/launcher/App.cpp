#include "App.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <system_error>
#include <vector>
#ifndef _WIN32
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

extern char** environ;

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
        if (!item.is_regular_file()) continue;
        const std::string name = item.path().filename().string();
        if (name.find(".registry.json") == std::string::npos) continue;
        const auto time = item.last_write_time(error);
        if (error) continue;
        if (time > newestTime) {
            newestTime = time;
            newest = item.path();
        }
    }
    return newest;
}

void CollectModuleDirs(const std::filesystem::path& dir, int depth, std::vector<std::filesystem::path>& out) {
    if (depth < 0 || out.size() >= 12) return;
    std::error_code scanError;
    bool hasPrx = false;
    std::vector<std::filesystem::path> children;
    std::filesystem::directory_iterator it(dir, scanError);
    const std::filesystem::directory_iterator end;
    for (; it != end && !scanError; it.increment(scanError)) {
        if (it->is_directory()) {
            children.push_back(it->path());
        } else if (it->path().extension() == ".prx") {
            hasPrx = true;
        }
    }
    if (scanError) return;
    if (hasPrx) out.push_back(dir);
    for (const auto& child : children) CollectModuleDirs(child, depth - 1, out);
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
    AppendLog("ready - " + std::to_string(games.size()) + " game(s), dump " + Config("lastDump", ""));
}

void App::Save() const {
    std::vector<Game> snapshot;
    {
        std::lock_guard<std::mutex> lock(gamesMutex);
        snapshot = games;
    }
    SaveLibrary(snapshot);
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

void App::Fail(const std::string& message) {
    AppendLog("FAIL: " + message);
    std::lock_guard<std::mutex> lock(gamesMutex);
    const int index = commandGame >= 0 && commandGame < static_cast<int>(games.size()) ? commandGame : selected;
    if (index >= 0 && index < static_cast<int>(games.size())) {
        failureGame = games[index].title;
        failureTitleId = games[index].titleId;
    } else {
        failureGame.clear();
        failureTitleId.clear();
    }
    {
        std::lock_guard<std::mutex> logLock(logMutex);
        lastFailure = message;
        lastFailed = true;
    }
}

void App::CheckForUpdates() {
    if (busy) return;
    busy = true;
    lastCommand = "updates";
    std::thread([this] {
        const std::string script = (root / "tools" / "update.sh").string();
        std::FILE* pipe = popen(("bash " + script + " --check 2>&1").c_str(), "r");
        bool available = false;
        if (pipe != nullptr) {
            char buffer[512];
            while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) {
                AppendLog(buffer);
                if (std::string(buffer).rfind("ANYPS5_UPDATE_AVAILABLE", 0) == 0) available = true;
            }
            pclose(pipe);
        }
        if (available) {
            std::lock_guard<std::mutex> guard(logMutex);
            lastFailure = "a launcher update is available (see the update line above); run tools/update.sh to install it";
            lastFailed = true;
        }
        busy = false;
    }).detach();
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
    std::thread([this, label, args, env, log] {
        const std::string command = envPrefix(env) + join(args);
        std::vector<std::string> lines;
        bool ok = false;
        std::FILE* pipe = popen(command.c_str(), "r");
        if (pipe != nullptr) {
            std::ofstream file(log, std::ios::binary | std::ios::trunc);
            char buffer[4096];
            while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) {
                std::string line(buffer);
                while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
                AppendLog(line);
                if (lines.size() < 4000) lines.push_back(line);
                if (file) file << stamp() << " " << line << "\n";
            }
            if (file) file.flush();
            const int status = pclose(pipe);
#ifdef _WIN32
            const int code = status;
#else
            const int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
            bool auditReport = false;
            if (label == "audit") {
                for (const auto& row : lines) {
                    if (row.find("unique imports") != std::string::npos) {
                        auditReport = true;
                        break;
                    }
                }
            }
            const bool auditResult = label == "audit" && code == 1 && auditReport;
            std::string runOutcome;
            if (code != 0 && !auditResult) {
                const std::string failure = label == "run"
                    ? "the game exited with code " + std::to_string(code)
                    : "command exited with code " + std::to_string(code);
                AppendLog("FAIL: " + failure);
                {
                    std::lock_guard<std::mutex> gameLock(gamesMutex);
                    if (commandGame >= 0 && commandGame < static_cast<int>(games.size())) {
                        failureGame = games[commandGame].title;
                        failureTitleId = games[commandGame].titleId;
                    } else {
                        failureGame.clear();
                        failureTitleId.clear();
                    }
                }
                {
                    std::lock_guard<std::mutex> logLock(logMutex);
                    lastFailure = failure;
                    lastFailed = true;
                }
                runOutcome = failure;
            } else {
                if (auditResult) AppendLog("audit reported unresolved imports; see the report above");
                ok = true;
                UpdateGameStatus(label, lines);
            }
            if (label == "run") {
                {
                    std::lock_guard<std::mutex> guard(gamesMutex);
                    if (commandGame >= 0 && commandGame < static_cast<int>(games.size())) {
                        games[commandGame].lastRun = Timestamp();
                        games[commandGame].lastError = runOutcome;
                    }
                }
                Save();
            }
        }
        if (label == "run") runningGame = -1;
        busy = false;
        if (label == "convert" && ok) Audit(commandGame);
    }).detach();
}

bool App::Convert(int index) {
    if (index < 0 || index >= static_cast<int>(games.size())) return false;
    if (runningGame >= 0) {
        Fail("a game is running; close it before converting");
        return false;
    }
    if (busy) {
        Fail("another task is already running");
        return false;
    }
    const auto relinker = RelinkerPath();
    if (relinker.empty()) {
        Fail("relinker binary not found beside the launcher or under " + root.string());
        return false;
    }
    const Game& game = games[index];
    const std::string kind = ClassifyKind(std::filesystem::path(game.input));
    if (kind != "elf") {
        Fail(game.input + " is " + kind + "; the relinker needs a clean ELF (decrypt the eboot first)");
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
    commandGame = index;
    StartCommand("convert", args, {});
    return true;
}

bool App::Audit(int index) {
    if (index < 0 || index >= static_cast<int>(games.size())) return false;
    if (runningGame >= 0) {
        Fail("a game is running; close it before auditing");
        return false;
    }
    if (busy) {
        Fail("another task is already running");
        return false;
    }
    const Game& game = games[index];
    const auto registry = newestRegistry(std::filesystem::path(game.out));
    if (registry.empty()) {
        Fail("no .registry.json in " + game.out);
        return false;
    }
    const auto audit = root / "tools" / "import_audit.py";
    const auto libs = LibrariesPath();
    if (!std::filesystem::is_regular_file(audit)) {
        Fail("audit needs the source tree (tools/import_audit.py); skipped");
        return false;
    }
    if (libs.empty()) {
        Fail("no built .prx directory; run cmake --build build --target libs first");
        return false;
    }
    std::vector<std::string> args = {"python3", audit.string(), registry.string(), "--libs", libs.string()};
    const auto container = ContainerDir(game);
    std::vector<std::filesystem::path> moduleDirs;
    std::error_code scanError;
    if (std::filesystem::is_directory(container, scanError)) {
        std::filesystem::directory_iterator it(container, scanError);
        const std::filesystem::directory_iterator end;
        for (; it != end && !scanError; it.increment(scanError)) {
            if (!it->is_directory()) continue;
            CollectModuleDirs(it->path(), 1, moduleDirs);
        }
    }
    std::sort(moduleDirs.begin(), moduleDirs.end());
    for (const auto& dir : moduleDirs) {
        args.push_back("--modules");
        args.push_back(dir.string());
    }
    commandGame = index;
    StartCommand("audit", args, {});
    return true;
}

bool App::Launch(int index) {
    if (index < 0 || index >= static_cast<int>(games.size())) return false;
    if (runningGame >= 0) {
        Fail("a game is already running; close it before starting another");
        return false;
    }
    if (busy) {
        Fail("another task is already running; wait for it to finish");
        return false;
    }
    const Game& game = games[index];
    if (game.auditAbsent > 0 || game.auditMissing > 0) {
        std::string message = game.title + " cannot start: ";
        if (game.auditAbsent > 0) message += std::to_string(game.auditAbsent) + " imports have no implementation";
        if (game.auditAbsent > 0 && game.auditMissing > 0) message += " and ";
        if (game.auditMissing > 0) message += std::to_string(game.auditMissing) + " libraries were not found in the dump or the built libs";
        message += "; run Audit after adding the missing modules";
        Fail(message);
        return false;
    }
    const auto exe = ExecutablePath(game);
    if (!std::filesystem::is_regular_file(exe)) {
        Fail(exe.string() + " missing; convert first");
        return false;
    }
    std::error_code error;
    std::filesystem::permissions(exe,
        std::filesystem::perms::owner_exec | std::filesystem::perms::group_exec | std::filesystem::perms::others_exec,
        std::filesystem::perm_options::add, error);
    const auto libs = LibrariesPath();
    if (!libs.empty()) {
        std::filesystem::create_directories(exe.parent_path() / "libs", error);
        for (const auto& item : std::filesystem::directory_iterator(libs)) {
            if (item.path().extension() != ".prx") continue;
            const auto target = exe.parent_path() / "libs" / item.path().filename();
            std::filesystem::copy_file(item.path(), target, std::filesystem::copy_options::skip_existing, error);
        }
    }
    const auto container = ContainerDir(game);
    if (std::filesystem::is_directory(container, error)) {
        std::error_code dirError;
        std::filesystem::create_directories(exe.parent_path() / "app0", error);
        std::filesystem::directory_iterator it(container, dirError);
        const std::filesystem::directory_iterator end;
        for (; it != end && !dirError; it.increment(dirError)) {
            if (!it->is_directory()) continue;
            const auto name = it->path().filename().string();
            if (name.empty() || name[0] == '.') continue;
            std::filesystem::copy(it->path(), exe.parent_path() / "app0" / name,
                std::filesystem::copy_options::recursive | std::filesystem::copy_options::skip_existing, error);
        }
    }
    std::map<std::string, std::string> env = GameEnv(game);
    if (env.find("ANYPS5_SHADER_CACHE_DIR") == env.end() || env.at("ANYPS5_SHADER_CACHE_DIR").empty()) {
        env["ANYPS5_SHADER_CACHE_DIR"] = (exe.parent_path() / "shader_cache").string();
    }
    commandGame = index;
    runningGame = index;
#ifdef _WIN32
    StartCommand("run", {exe.string()}, env);
#else
    SpawnDetached("run", exe.string(), env);
#endif
    return true;
}

void App::SpawnDetached(const std::string& label, const std::string& exe, const std::map<std::string, std::string>& env) {
    if (busy) return;
    busy = true;
    lastCommand = label;
    const auto log = LogDirectory() / (LogStamp() + "-run.log");
    lastLog = log.string();
    std::error_code logError;
    std::filesystem::create_directories(LogDirectory(), logError);
    AppendLog("$ " + label + " " + exe);
    std::thread([this, exe, env, log] {
        int errfd[2];
        if (pipe(errfd) != 0) {
            Fail("could not create a status pipe for " + exe);
            runningGame = -1;
            busy = false;
            return;
        }
        fcntl(errfd[1], F_SETFD, FD_CLOEXEC);
        const pid_t pid = fork();
        if (pid < 0) {
            close(errfd[0]);
            close(errfd[1]);
            Fail("fork failed while starting " + exe);
            runningGame = -1;
            busy = false;
            return;
        }
        if (pid == 0) {
            setsid();
            close(errfd[0]);
            const int devnull = open("/dev/null", O_RDWR);
            const int logfd = open(log.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (devnull >= 0) {
                dup2(devnull, 0);
                if (devnull > 2) close(devnull);
            }
            if (logfd >= 0) {
                dup2(logfd, 1);
                dup2(logfd, 2);
                if (logfd > 2) close(logfd);
            }
            const int keep = errfd[1];
            for (int fd = 3; fd < 4096; ++fd) {
                if (fd != keep) close(fd);
            }
            std::vector<std::string> envStrings;
            for (char** entry = environ; *entry != nullptr; ++entry) envStrings.emplace_back(*entry);
            for (const auto& [key, value] : env) envStrings.push_back(key + "=" + value);
            std::vector<char*> envp;
            for (auto& item : envStrings) envp.push_back(item.data());
            envp.push_back(nullptr);
            char* argv[] = {const_cast<char*>(exe.c_str()), nullptr};
            execve(exe.c_str(), argv, envp.data());
            const int err = errno;
            const ssize_t ignored = write(errfd[1], &err, sizeof(err));
            (void)ignored;
            _exit(127);
        }
        close(errfd[1]);
        int err = 0;
        const ssize_t count = read(errfd[0], &err, sizeof(err));
        close(errfd[0]);
        if (count == static_cast<ssize_t>(sizeof(err))) {
            const std::string failure = "could not start " + exe + ": " + std::strerror(err);
            {
                std::lock_guard<std::mutex> guard(gamesMutex);
                if (commandGame >= 0 && commandGame < static_cast<int>(games.size())) {
                    games[commandGame].lastError = failure;
                }
            }
            Save();
            Fail(failure);
            runningGame = -1;
            busy = false;
            return;
        }
        AppendLog("game started: " + exe);
        int status = 0;
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
        }
        std::string outcome;
        if (WIFSIGNALED(status)) {
            const int sig = WTERMSIG(status);
            std::string name;
            if (sig == 11) name = " (segmentation fault)";
            else if (sig == 6) name = " (abort)";
            else if (sig == 7) name = " (bus error)";
            else if (sig == 8) name = " (floating point exception)";
            else if (sig == 4) name = " (illegal instruction)";
            outcome = "the game was terminated by signal " + std::to_string(sig) + name;
        } else if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
            outcome = "the game exited with code " + std::to_string(WEXITSTATUS(status));
        }
        {
            std::lock_guard<std::mutex> guard(gamesMutex);
            if (commandGame >= 0 && commandGame < static_cast<int>(games.size())) {
                games[commandGame].lastRun = Timestamp();
                games[commandGame].lastError = outcome;
            }
        }
        Save();
        if (!outcome.empty()) {
            Fail(outcome);
        } else {
            AppendLog("game exited normally");
        }
        runningGame = -1;
        busy = false;
    }).detach();
}

bool App::IsRunning(int index) const {
    return runningGame.load() == index;
}

void ApplyAuditLines(const std::vector<std::string>& lines, Game& game) {
    auto numberAfter = [](const std::string& row, const std::string& word) {
        const auto at = row.find(word);
        if (at == std::string::npos) return -1;
        std::size_t pos = at + word.size();
        while (pos < row.size() && !std::isdigit(static_cast<unsigned char>(row[pos]))) pos++;
        if (pos >= row.size()) return -1;
        std::size_t end = pos;
        while (end < row.size() && std::isdigit(static_cast<unsigned char>(row[end]))) end++;
        return std::atoi(row.substr(pos, end - pos).c_str());
    };
    auto numberBefore = [](const std::string& row, const std::string& word) {
        const auto at = row.find(word);
        if (at == std::string::npos || at == 0) return -1;
        std::size_t pos = at;
        while (pos > 0 && !std::isdigit(static_cast<unsigned char>(row[pos - 1]))) pos--;
        if (pos == 0) return -1;
        std::size_t start = pos;
        while (start > 0 && std::isdigit(static_cast<unsigned char>(row[start - 1]))) start--;
        return std::atoi(row.substr(start, pos - start).c_str());
    };
    int absent = -1;
    int stub = -1;
    int total = -1;
    int missing = -1;
    for (const auto& row : lines) {
        if (row.find("unique imports") != std::string::npos) {
            total = numberBefore(row, "unique imports");
        } else if (row.find("  absent") != std::string::npos && row.find("imports") != std::string::npos) {
            absent = numberAfter(row, "absent");
        } else if (row.find("  stub") != std::string::npos && row.find("imports") != std::string::npos) {
            stub = numberAfter(row, "stub");
        }
        if (row.find("Needed libraries") != std::string::npos) missing = numberAfter(row, "modules");
    }
    if (total < 0 && absent < 0 && missing < 0) return;
    if (total >= 0 && absent >= 0 && missing < 0) missing = 0;
    game.auditTotal = total;
    game.auditAbsent = absent;
    game.auditStub = stub;
    game.auditMissing = missing;
    std::string summary;
    if (total >= 0) summary = std::to_string(total) + " unique imports";
    if (absent > 0) summary += ", " + std::to_string(absent) + " missing implementations";
    if (stub > 0) summary += ", " + std::to_string(stub) + " stubs";
    if (missing > 0) summary += ", " + std::to_string(missing) + " libraries not found";
    if (summary.empty()) summary = "audited";
    game.auditSummary = summary;
    if (absent == 0 && missing == 0 && total >= 0 && stub <= 0) game.status = "ready";
}

void App::UpdateGameStatus(const std::string& label, const std::vector<std::string>& lines) {
    if (commandGame < 0 || commandGame >= static_cast<int>(games.size())) return;
    if (label == "audit") {
        std::lock_guard<std::mutex> guard(gamesMutex);
        ApplyAuditLines(lines, games[commandGame]);
    } else if (label == "convert") {
        std::lock_guard<std::mutex> guard(gamesMutex);
        games[commandGame].status = "converted";
    } else {
        return;
    }
    Save();
}

}
