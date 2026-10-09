#include "Diagnostics.hpp"

#include "anyps5/Version.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#define ANYPS5_POPEN _popen
#define ANYPS5_PCLOSE _pclose
#else
#include <sys/utsname.h>
#define ANYPS5_POPEN popen
#define ANYPS5_PCLOSE pclose
#endif

namespace Launcher {
namespace Diagnostics {
namespace {

std::string Trim(const std::string& value) {
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return {};
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

std::string FirstLine(const std::string& value) {
    const auto end = value.find('\n');
    return Trim(value.substr(0, end == std::string::npos ? value.size() : end));
}

std::string Capture(const std::string& command) {
    std::string result;
    std::FILE* pipe = ANYPS5_POPEN((command + " 2>/dev/null").c_str(), "r");
    if (pipe == nullptr) return result;
    char buffer[512]{};
    while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) result += buffer;
    ANYPS5_PCLOSE(pipe);
    return result;
}

std::string ReadFileFirstMatch(const std::string& path, const std::string& prefix) {
    std::ifstream stream(path);
    if (!stream) return {};
    std::string line;
    while (std::getline(stream, line)) {
        if (line.rfind(prefix, 0) == 0) return Trim(line.substr(prefix.size()));
    }
    return {};
}

std::string OsLine() {
#ifdef _WIN32
    return "Windows " + FirstLine(Capture("ver"));
#else
    struct utsname info{};
    if (uname(&info) != 0) return "unknown";
    std::string result = std::string(info.sysname) + " " + info.release;
    const std::string pretty = ReadFileFirstMatch("/etc/os-release", "PRETTY_NAME=");
    if (!pretty.empty()) {
        std::string cleaned = pretty;
        if (cleaned.size() >= 2 && cleaned.front() == '"' && cleaned.back() == '"') cleaned = cleaned.substr(1, cleaned.size() - 2);
        result += " (" + cleaned + ")";
    }
    return result;
#endif
}

std::string CpuLine() {
#ifdef _WIN32
    return FirstLine(Capture("wmic cpu get name /value"));
#elif defined(__APPLE__)
    return FirstLine(Capture("sysctl -n machdep.cpu.brand_string"));
#else
    return ReadFileFirstMatch("/proc/cpuinfo", "model name");
#endif
}

std::string GpuLine() {
#ifdef _WIN32
    return FirstLine(Capture("wmic path win32_VideoController get name /value"));
#elif defined(__APPLE__)
    return FirstLine(Capture("system_profiler SPDisplaysDataType | grep 'Chipset Model' | head -n 1 | cut -d: -f2"));
#else
    const std::string lspci = Capture("lspci -mm");
    std::istringstream stream(lspci);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.find("VGA") == std::string::npos && line.find("3D") == std::string::npos && line.find("Display") == std::string::npos) continue;
        const auto start = line.find('"', line.find('"') + 1);
        if (start == std::string::npos) continue;
        const auto end = line.find('"', start + 1);
        if (end == std::string::npos || end == start + 1) continue;
        return line.substr(start + 1, end - start - 1);
    }
    return {};
#endif
}

std::string DriverLine() {
#ifdef _WIN32
    return FirstLine(Capture("wmic path win32_VideoController get driverversion /value"));
#elif defined(__APPLE__)
    return FirstLine(Capture("system_profiler SPDisplaysDataType | grep -i 'driver version' | head -n 1 | cut -d: -f2"));
#else
    const std::string nvidia = ReadFileFirstMatch("/proc/driver/nvidia/version", "NVIDIA UNIX");
    if (!nvidia.empty()) {
        const auto space = nvidia.find_last_of(' ');
        return space == std::string::npos ? nvidia : nvidia.substr(space + 1);
    }
    for (const char* module : {"amdgpu", "i915", "xe", "radeon", "nouveau", "virtio-pci"}) {
        std::ifstream stream(std::string("/sys/module/") + module + "/version");
        if (!stream) continue;
        std::string version;
        std::getline(stream, version);
        if (!Trim(version).empty()) return std::string(module) + " " + Trim(version);
    }
    return {};
#endif
}

}
std::vector<std::string> SystemLines() {
    std::vector<std::string> lines;
    lines.push_back("anyps5: " + VersionStamp());
    const std::string os = OsLine();
    if (!os.empty()) lines.push_back("os: " + os);
    const std::string cpu = CpuLine();
    lines.push_back("cpu: " + (cpu.empty() ? std::string("unknown") : cpu));
    const std::string gpu = GpuLine();
    lines.push_back("gpu: " + (gpu.empty() ? std::string("unknown") : gpu));
    const std::string driver = DriverLine();
    lines.push_back("gpu driver: " + (driver.empty() ? std::string("unknown") : driver));
    return lines;
}

std::string VersionStamp() {
    return std::string(ANYPS5_VERSION) + " (" + ANYPS5_COMMIT + ")";
}

std::string ReadLogTail(const std::string& path, std::size_t maxBytes) {
    if (path.empty()) return {};
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return {};
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    std::string content = buffer.str();
    if (content.size() <= maxBytes) return content;
    return content.substr(content.size() - maxBytes);
}

std::string BuildFailureReport(const std::string& gameTitle, const std::string& titleId,
                               const std::string& message, const std::string& logPath,
                               const std::vector<std::string>& systemLines,
                               const std::string& logContents) {
    std::string report;
    for (const auto& line : systemLines) {
        report += line;
        report += '\n';
    }
    if (!gameTitle.empty() || !titleId.empty()) {
        report += "game: " + gameTitle;
        if (!titleId.empty()) report += " [" + titleId + "]";
        report += '\n';
    }
    report += "error: " + message + '\n';
    if (!logPath.empty()) {
        report += "log: " + logPath + '\n';
        if (!logContents.empty()) {
            report += "--- log ---\n";
            report += logContents;
            if (logContents.back() != '\n') report += '\n';
        }
    }
    return report;
}

}
}
