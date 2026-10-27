#pragma once
#include <cstdint>

namespace MK9TradeFix {
    namespace JumpInputFix {
        // Initializes MinHook hooks for the Jump Input Threshold Fix
        void InstallHooks();

        // Enable or disable the Jump Input Threshold Fix
        void SetEnabled(bool enabled);

        // Check if the Jump Input Threshold Fix is currently active
        bool IsEnabled();

        // Toggle the Jump Input Threshold Fix
        void Toggle();

        // Called on each simulation frame tick to sync live fighter structures
        void OnFighterTick(void* pChar);
    }
}
