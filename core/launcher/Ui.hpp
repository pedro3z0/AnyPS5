#ifndef CORE_LAUNCHER_UI_HPP
#define CORE_LAUNCHER_UI_HPP

#include "App.hpp"

#include "imgui.h"

namespace Launcher {

enum class Glyph { Play, Convert, Audit, Sliders, Keyboard, Folder, Trash, Plus, Page, Close, Refresh };

void DrawGlyph(Glyph glyph, ImDrawList* draw, const ImVec2& origin, float size, ImU32 color);
bool IconButton(const char* id, const char* label, Glyph glyph);
bool TextInput(const char* label, std::string& value, const char* hint = "");
bool MultilineText(const char* label, std::string& value, float width, float height);
void ApplyStyle();

void DrawUi(App& app, void* renderer, void* window);
void DrawDialogs(App& app);
void DrawModals(App& app);

}

#endif
