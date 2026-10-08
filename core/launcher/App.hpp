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
    std::string name;
    std::string dump;
    std::string out;
    std::string error;
    std::vector<Candidate> candidates;
    int chosen = 0;
    bool windows = false;
    bool intel = false;
    int filter = 0;
    bool showAdd = false;
    bool showSettings = false;
    bool focusInput = false;
    bool showBrowser = false;
    std::string browserPath;
    std::string browserTarget;
    std::string scanKey;
    std::string envText;
    std::map<std::string, std::string> envSnapshot;
    int envGame = -1;
    bool logOpen = false;

};

struct App {
    std::filesystem::path root;
    std::filesystem::path executableDir;
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
    std::map<std::string, std::string> config;
    DialogBuffers dialog;
    void* renderer = nullptr;
    std::map<std::string, std::uint32_t> icons;

    void Load();
    void Save() const;
    void SaveConfig();

    bool IsConfigured() const;
    std::filesystem::path RelinkerPath() const;
    std::filesystem::path LibrariesPath() const;

    std::string Config(const std::string& key, const std::string& fallback) const;
    void SetConfig(const std::string& key, const std::string& value);

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
