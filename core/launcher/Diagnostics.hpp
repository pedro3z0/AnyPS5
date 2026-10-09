#ifndef CORE_LAUNCHER_DIAGNOSTICS_HPP
#define CORE_LAUNCHER_DIAGNOSTICS_HPP

#include <string>
#include <vector>

namespace Launcher {

namespace Diagnostics {

std::vector<std::string> SystemLines();

std::string VersionStamp();

std::string ReadLogTail(const std::string& path, std::size_t maxBytes);

std::string BuildFailureReport(const std::string& gameTitle, const std::string& titleId,
                               const std::string& message, const std::string& logPath,
                               const std::vector<std::string>& systemLines,
                               const std::string& logContents);

}

}

#endif