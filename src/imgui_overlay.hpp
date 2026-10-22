#pragma once
#include <windows.h>
#include <d3d9.h>
#include <string>
#include <vector>
#include <mutex>
#include "imgui/imgui.h"

namespace MK9Overlay {

    struct ConsoleLogEntry {
        std::string text;
        ImVec4 color;
        uint64_t frame;
    };

    void Initialize();
    void Shutdown();
    void AppendLog(const char* text, uint64_t frame = 0);
    void ToggleConsole();
    bool IsConsoleOpen();
    void SetConsoleOpen(bool open);
    void CheckAndHookD3D9();

}
