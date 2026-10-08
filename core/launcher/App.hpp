#ifndef CORE_LAUNCHER_APP_HPP
#define CORE_LAUNCHER_APP_HPP

#include "Library.hpp"

#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace Launcher {

struct DialogBuffers {
    std::string name = "name";
    std::string dump = "dump dir";
    std::string input = "input";
    std::string out = "out dir";
    std::string error;
    std::vector<Candidate> candidates;
    int chosen = 0;
    bool windows = false;
    bool intel = false;
    int filter = 0;
    std::string inputText = "KEY:F, KEY:Space";
    int inputAction = 0;
    std::string cache = "shader_cache dir";
    bool showAdd = false;
    bool showSettings = false;
    bool showInput = false;
    bool showBrowser = false;
    std::string browserPath;
    bool logOpen = false;
};

struct App {
    std::filesystem::path root;
    std::vector<Game> games;
    int selected = -1;
    std::mutex logMutex;
    std::vector<std::string> logLines;
    std::atomic<bool> busy{false};
    std::atomic<bool> exitRequested{false};
    std::string lastCommand;
    std::string lastLog;
    std::string settingsError;
    std::string inputError;
    DialogBuffers dialog;
    void* renderer = nullptr;
    std::map<std::string, std::uint32_t> icons;

    void Load();
    void Save() const;

    bool Convert(const Game& game);
    bool Audit(const Game& game);
    bool Launch(const Game& game);

    void StartCommand(const std::string& label, const std::vector<std::string>& args,
                      const std::map<std::string, std::string>& env);

    void AppendLog(const std::string& line);
    std::vector<std::string> SnapshotLog();
};

}

#endif
