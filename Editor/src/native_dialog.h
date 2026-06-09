#pragma once
// Thin Win32 wrapper around GetOpenFileName / GetSaveFileName.
// Returns the chosen path, or an empty string if the user cancelled.

#include <string>

namespace Engine::editor {

std::string dialog_open_file(const char* title);
std::string dialog_save_file(const char* title,
                              const char* default_name = "untitled.zworld");

}  // namespace Engine::editor
