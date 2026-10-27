#pragma once
#include "engine_common.hpp"
#include <xinput.h>

namespace MK9TradeFix {
    namespace InputIsolation {

        void SetEnabled(bool enabled);
        bool IsEnabled();
        void Toggle();

        // Subsystem Lifecycle
        bool InstallHooks();

        // Multi-Device Test Bot Gamepad Injection (Mode 12)
        bool HasActiveTestModeGamepad(DWORD dwUserIndex);
        bool GetActiveTestModeGamepad(DWORD dwUserIndex, XINPUT_STATE* pState);

        // Frame update / telemetry
        void OnFrameTick(void* p1Char, void* p2Char);

    }
}
