#pragma once
#include <cstdint>
#include <atomic>
#include <windows.h>

namespace MK9TradeFix {
    namespace LoadProfiler {
        // High-precision timeline milestones
        void InitProcessStartTime();
        void InstallHooks();
        void RecordD3D9Init();
        void RecordFirstFrame();
        void RecordMainMenu();
        void OnFrameTick();

        // Fast Loading Optimization Toggle
        extern std::atomic<bool> g_FastLoadingEnabled;

        // Diagnostics & Telemetry Queries
        bool   IsMatchLoading();
        double GetLastMatchLoadDuration();
        double GetLastVsScreenDuration();
        double GetLastMatchDiskReadDuration();
        double GetLastMatchSleepDuration();
        double GetLastMatchTotalBytesMB();
        size_t GetTotalPackagesLoaded();
    }
}
