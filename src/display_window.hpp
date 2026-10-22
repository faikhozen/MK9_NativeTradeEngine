#pragma once
#include "engine_common.hpp"

namespace MK9TradeFix {

    extern DisplayMode g_DisplayMode;
    extern bool g_AutoFullscreenOn720p;
    extern bool g_UnlockAllResolutions;

    bool Is720pMonitor();
    int GetMonitorWidth();
    int GetMonitorHeight();
    const char* GetDisplayModeName();
    void CycleDisplayMode();
    void SetDisplayMode(DisplayMode mode);
    void SetUnlockAllResolutions(bool enable);
    void EarlyInitDisplayPatches();
    void InjectAllStandardResolutions();
    HWND GetGameWindow();
    void ApplyWindowStyleAndPlacement();
    void UpdateNativeViewportStyle(DWORD style);
    void ToggleFullscreenWindowed();

    bool InstallDisplayHooks();
}
