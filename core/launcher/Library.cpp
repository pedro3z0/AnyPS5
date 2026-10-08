#include "Library.hpp"

#include "Json.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace Launcher {

namespace {

const std::vector<std::string>& actions() {
    static const std::vector<std::string> value = {
        "Cross", "Circle", "Triangle", "Square", "L1", "R1", "L2", "R2", "L3", "R3",
        "Options", "Up", "Right", "Down", "Left",
        "LeftStickLeft", "LeftStickRight", "LeftStickUp", "LeftStickDown",
        "RightStickLeft", "RightStickRight", "RightStickUp", "RightStickDown",
        "TouchLeft", "TouchRight", "ToggleMouse", "ToggleFullscreen"};
    return value;
}

std::string lower(std::string text) {
    for (char& value : text) value = static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
    return text;
}

std::string readFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("cannot read " + path.string());
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

void writeFile(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) throw std::runtime_error("cannot write " + path.string());
    file << text;
}

std::string home() {
    const char* value = std::getenv("HOME");
    if (value != nullptr && value[0] != '\0') return value;
#ifdef _WIN32
    const char* profile = std::getenv("USERPROFILE");
    if (profile != nullptr && profile[0] != '\0') return profile;
#endif
    return ".";
}

std::string classify(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return "unreadable";
    char data[4] = {};
    file.read(data, sizeof(data));
    const auto count = static_cast<std::size_t>(file.gcount());
    const auto is = [&](char a, char b, char c, char d) {
        return count == 4 && data[0] == a && data[1] == b && data[2] == c && data[3] == d;
    };
    if (is('\x7f', 'E', 'L', 'F')) return "elf";
    if (is('\x4f', '\x15', '\x3d', '\x1d')) return "self";
    if (is('\x54', '\x14', '\xf5', '\xee')) return "self-kernel";
    if (is('\x7f', 'C', 'N', 'T')) return "pkg";
    if (count < 4) return "too-small";
    return "unknown";
}

Json::Value gameToJson(const Game& game) {
    Json::Value value;
    value.type = Json::Type::Object;
    auto put = [&](const std::string& key, const std::string& text) {
        Json::Value item;
        item.type = Json::Type::String;
        item.string = text;
        value.object[key] = item;
    };
    auto putBool = [&](const std::string& key, bool flag) {
        Json::Value item;
        item.type = Json::Type::Boolean;
        item.boolean = flag;
        value.object[key] = item;
    };
    put("name", game.name);
    put("dump", game.dump);
    put("input", game.input);
    put("out", game.out);
    putBool("windows", game.windows);
    putBool("intel", game.intel);
    put("filter", game.filter);
    put("status", game.status);
    put("created", game.created);
    put("last_run", game.lastRun);
    put("title", game.title);
    put("title_id", game.titleId);
    put("version", game.version);
    put("icon", game.icon);
    Json::Value env;
    env.type = Json::Type::Object;
    for (const auto& [key, item] : game.env) {
        Json::Value entry;
        entry.type = Json::Type::String;
        entry.string = item;
        env.object[key] = entry;
    }
    value.object["env"] = env;
    return value;
}

Game gameFromJson(const Json::Value& value) {
    Game game;
    auto get = [&](const std::string& key) {
        const Json::Value* item = value.find(key);
        return item == nullptr ? std::string() : item->asString();
    };
    game.name = get("name");
    game.dump = get("dump");
    game.input = get("input");
    game.out = get("out");
    const Json::Value* windows = value.find("windows");
    game.windows = windows != nullptr && windows->asBool(false);
    const Json::Value* intel = value.find("intel");
    game.intel = intel != nullptr && intel->asBool(false);
    game.filter = get("filter");
    if (game.filter.empty()) game.filter = "0";
    game.status = get("status");
    game.created = get("created");
    game.lastRun = get("last_run");
    game.title = get("title");
    game.titleId = get("title_id");
    game.version = get("version");
    game.icon = get("icon");
    if (const Json::Value* env = value.find("env"); env != nullptr && env->type == Json::Type::Object) {
        for (const auto& [key, item] : env->object) game.env[key] = item.asString();
    }
    return game;
}

}

std::filesystem::path LibraryPath() {
    return std::filesystem::path(home()) / ".config" / "anyps5" / "library.json";
}

std::filesystem::path ConfigPath() {
    return std::filesystem::path(home()) / ".config" / "anyps5" / "launcher.json";
}

std::map<std::string, std::string> LoadConfig() {
    std::map<std::string, std::string> config;
    try {
        const auto value = Json::Parse(readFile(ConfigPath()));
        if (value.type == Json::Type::Object) {
            for (const auto& [key, item] : value.object) config[key] = item.asString();
        }
    } catch (const std::exception&) {
        return config;
    }
    return config;
}

void SaveConfig(const std::map<std::string, std::string>& config) {
    Json::Value value;
    value.type = Json::Type::Object;
    for (const auto& [key, item] : config) {
        Json::Value entry;
        entry.type = Json::Type::String;
        entry.string = item;
        value.object[key] = entry;
    }
    writeFile(ConfigPath(), Json::Dump(value) + "\n");
}

std::filesystem::path LogDirectory() {
#ifdef _WIN32
    const char* local = std::getenv("LOCALAPPDATA");
    if (local != nullptr && local[0] != '\0') return std::filesystem::path(local) / "anyps5" / "logs";
#endif
    return std::filesystem::path(home()) / ".local" / "share" / "anyps5" / "logs";
}

std::filesystem::path DefaultRoot() {
    std::error_code error;
    const auto cwd = std::filesystem::current_path(error);
    if (!error && std::filesystem::exists(cwd / "tools" / "convert.sh")) return cwd;
    return std::filesystem::path(home()) / "Projetos" / "AnyPS5";
}

std::vector<Game> LoadLibrary() {
    std::vector<Game> games;
    try {
        const auto value = Json::Parse(readFile(LibraryPath()));
        if (const Json::Value* list = value.find("games"); list != nullptr && list->type == Json::Type::Array) {
            for (const auto& item : list->array) games.push_back(gameFromJson(item));
        }
    } catch (const std::exception&) {
        return {};
    }
    return games;
}

void SaveLibrary(const std::vector<Game>& games) {
    Json::Value list;
    list.type = Json::Type::Array;
    for (const auto& game : games) list.array.push_back(gameToJson(game));
    Json::Value root;
    root.type = Json::Type::Object;
    root.object["games"] = list;
    writeFile(LibraryPath(), Json::Dump(root) + "\n");
}

std::string Timestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &time);
#else
    gmtime_r(&time, &utc);
#endif
    std::tm local = utc;
    std::mktime(&local);
    char buffer[40];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S%z", &local);
    return buffer;
}

std::string LogStamp() {
    const std::time_t time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &time);
#else
    localtime_r(&time, &local);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y%m%d-%H%M%S", &local);
    return buffer;
}

Game ReadTitleMeta(const std::filesystem::path& dump, Game game) {
    game.dump = dump.string();
    std::filesystem::path base = dump;
    std::error_code scanError;
    if (std::filesystem::is_directory(dump, scanError) && !std::filesystem::is_regular_file(dump / "sce_sys" / "param.json", scanError)) {
        for (const auto& item : std::filesystem::directory_iterator(dump, scanError)) {
            if (!item.is_directory()) continue;
            if (std::filesystem::is_regular_file(item.path() / "sce_sys" / "param.json", scanError)) {
                base = item.path();
                break;
            }
        }
    }
    const auto param = base / "sce_sys" / "param.json";
    try {
        const auto value = Json::Parse(readFile(param));
        if (const Json::Value* id = value.find("titleId"); id != nullptr) game.titleId = id->asString();
        if (const Json::Value* version = value.find("contentVersion"); version != nullptr) game.version = version->asString();
        if (const Json::Value* localized = value.find("localizedParameters"); localized != nullptr) {
            std::string name;
            if (const Json::Value* fallback = localized->find("defaultLanguage"); fallback != nullptr) {
                if (const Json::Value* entry = localized->find(fallback->asString()); entry != nullptr) {
                    if (const Json::Value* title = entry->find("titleName"); title != nullptr) name = title->asString();
                }
            }
            if (name.empty()) {
                if (const Json::Value* entry = localized->find("en-US"); entry != nullptr) {
                    if (const Json::Value* title = entry->find("titleName"); title != nullptr) name = title->asString();
                }
            }
            if (!name.empty()) game.title = name;
        }
    } catch (const std::exception&) {
    }
    if (game.title.empty()) game.title = dump.filename().string();
    const auto icon = base / "sce_sys" / "icon0.png";
    if (std::filesystem::is_regular_file(icon)) game.icon = icon.string();
    return game;
}

std::vector<Candidate> InputCandidates(const std::filesystem::path& dump) {
    std::vector<Candidate> entries;
    std::error_code error;
    if (!std::filesystem::is_directory(dump, error)) return entries;
    for (const auto& item : std::filesystem::directory_iterator(dump)) {
        if (!item.is_regular_file()) continue;
        entries.push_back({item.path(), classify(item.path())});
    }
    if (entries.empty()) {
        for (const auto& item : std::filesystem::directory_iterator(dump, error)) {
            if (!item.is_directory()) continue;
            std::error_code nestedError;
            for (const auto& nested : std::filesystem::directory_iterator(item.path(), nestedError)) {
                if (!nested.is_regular_file()) continue;
                const std::string kind = classify(nested.path());
                if (kind == "elf" || kind == "self" || kind == "self-kernel" || kind == "pkg") {
                    entries.push_back({nested.path(), kind});
                }
            }
        }
    }
    std::sort(entries.begin(), entries.end(), [](const Candidate& left, const Candidate& right) {
        if ((left.kind == "elf") != (right.kind == "elf")) return left.kind == "elf";
        return lower(left.path.filename().string()) < lower(right.path.filename().string());
    });
    return entries;
}

std::string ClassifyKind(const std::filesystem::path& path) {
    return classify(path);
}

std::filesystem::path ContainerDir(const Game& game) {
    std::error_code error;
    const std::filesystem::path inputParent = std::filesystem::path(game.input).parent_path();
    for (const auto& base : {inputParent, std::filesystem::path(game.dump)}) {
        if (!base.empty() && std::filesystem::is_directory(base / "sce_sys", error)) return base;
    }
    if (!inputParent.empty()) return inputParent;
    if (std::filesystem::is_directory(game.dump, error)) {
        for (const auto& item : std::filesystem::directory_iterator(game.dump, error)) {
            if (item.is_directory() && std::filesystem::is_directory(item.path() / "sce_sys", error)) return item.path();
        }
    }
    return std::filesystem::path(game.dump);
}

bool HostPrefersWindows() {
#ifdef _WIN32
    return true;
#else
    return false;
#endif
}

bool HostIsIntel() {
#ifdef _WIN32
    const char* identifier = std::getenv("PROCESSOR_IDENTIFIER");
    if (identifier == nullptr) return false;
    return lower(std::string(identifier)).find("intel") != std::string::npos;
#else
    std::ifstream cpuinfo("/proc/cpuinfo");
    std::string line;
    while (std::getline(cpuinfo, line)) {
        if (line.rfind("vendor_id", 0) == 0) return line.find("GenuineIntel") != std::string::npos;
    }
    return false;
#endif
}

std::filesystem::path ExecutablePath(const Game& game) {
    auto name = std::filesystem::path(game.input).stem().string();
    return std::filesystem::path(game.out) / (name + (game.windows ? ".exe" : ".elf"));
}

std::filesystem::path InputPath(const Game& game) {
    const auto it = game.env.find("ANYPS5_INPUT_CONFIG");
    if (it != game.env.end() && !it->second.empty()) return it->second;
    return std::filesystem::path(game.out) / "anyps5-input.ini";
}

std::map<std::string, std::string> GameEnv(const Game& game) {
    return game.env;
}

bool ParseInputText(const std::string& text, std::map<std::string, std::vector<std::string>>& bindings, int& badLine) {
    bindings.clear();
    int lineNumber = 0;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        lineNumber++;
        const auto comment = line.find_first_of("#;");
        if (comment != std::string::npos) line = line.substr(0, comment);
        const auto separator = line.find('=');
        if (separator == std::string::npos) continue;
        std::string action = line.substr(0, separator);
        std::string source = line.substr(separator + 1);
        const auto trim = [](std::string value) {
            const auto first = value.find_first_not_of(" \t\r");
            const auto last = value.find_last_not_of(" \t\r");
            return first == std::string::npos ? std::string() : value.substr(first, last - first + 1);
        };
        action = trim(action);
        source = trim(source);
        if (action.empty() || source.empty()) continue;
        const auto it = std::find_if(actions().begin(), actions().end(), [&](const std::string& known) {
            return lower(known) == lower(action);
        });
        if (it == actions().end()) {
            badLine = lineNumber;
            return false;
        }
        std::string kind = source.substr(0, source.find(':'));
        for (char& value : kind) value = static_cast<char>(std::toupper(static_cast<unsigned char>(value)));
        if (source.find(':') == std::string::npos || (kind != "KEY" && kind != "MOUSE" && kind != "WHEEL")) {
            badLine = lineNumber;
            return false;
        }
        bindings[*it].push_back(source);
    }
    badLine = 0;
    return true;
}

std::string RenderInputText(const std::map<std::string, std::vector<std::string>>& bindings) {
    std::string out;
    for (const auto& action : actions()) {
        const auto it = bindings.find(action);
        if (it == bindings.end()) continue;
        for (const auto& source : it->second) out += action + " = " + source + "\n";
    }
    return out;
}

const std::vector<std::string>& InputActions() {
    return actions();
}

}
