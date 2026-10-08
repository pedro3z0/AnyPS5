#ifndef CORE_LAUNCHER_UI_HPP
#define CORE_LAUNCHER_UI_HPP

#include "App.hpp"

namespace Launcher {

std::vector<std::string> EnvFields();
std::string EnvHint(const std::string& key);
bool TextInput(const char* label, std::string& value);

void DrawUi(App& app, void* renderer, void* window);
void DrawDialogs(App& app);

}

#endif
