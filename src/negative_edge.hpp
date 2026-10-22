#pragma once
#include <windows.h>
#include <cstdint>

namespace MK9TradeFix {
    namespace NegativeEdge {
        bool InstallHooks();

        void SetP1Enabled(bool enabled);
        void SetP2Enabled(bool enabled);
        bool IsP1Enabled();
        bool IsP2Enabled();

        void ToggleP1();
        void ToggleP2();
        void SetBoth(bool enabled);

        void UpdatePlayerCache(void* p1, void* p2, void* p1Info = nullptr, void* p2Info = nullptr);
    }
}
