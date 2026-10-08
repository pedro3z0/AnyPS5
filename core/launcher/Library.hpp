#ifndef CORE_LAUNCHER_LIBRARY_HPP
#define CORE_LAUNCHER_LIBRARY_HPP

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace Launcher {

struct Game {
    std::string name;
    std::string dump;
    std::string input;
    std::string out;
    bool windows = false;
    bool intel = false;
    std::string filter = "0";
    std::string status;
    std::string created;
    std::string lastRun;
    std::string title;
    std::string titleId;
    std::string version;
    std::string icon;
    std::map<std::string, std::string> env;
};

struct Candidate {
    std::filesystem::path path;
    std::string kind;
};

std::filesystem::path LibraryPath();
std::filesystem::path ConfigPath();
std::filesystem::path LogDirectory();
std::filesystem::path DefaultRoot();

std::map<std::string, std::string> LoadConfig();
void SaveConfig(const std::map<std::string, std::string>& config);

std::vector<Game> LoadLibrary();
void SaveLibrary(const std::vector<Game>& games);

std::string Timestamp();
std::string LogStamp();

Game ReadTitleMeta(const std::filesystem::path& dump, Game game);
std::vector<Candidate> InputCandidates(const std::filesystem::path& dump);
std::string ClassifyKind(const std::filesystem::path& path);
std::filesystem::path ContainerDir(const Game& game);
bool HostPrefersWindows();
bool HostIsIntel();

std::filesystem::path ExecutablePath(const Game& game);
std::filesystem::path InputPath(const Game& game);

std::map<std::string, std::string> GameEnv(const Game& game);

bool ParseInputText(const std::string& text, std::map<std::string, std::vector<std::string>>& bindings, int& badLine);
std::string RenderInputText(const std::map<std::string, std::vector<std::string>>& bindings);

const std::vector<std::string>& InputActions();

}

#endif
