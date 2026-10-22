#include <windows.h>
#include "trade_fix.hpp"
#include "asi_loader.hpp"
#include "load_profiler.hpp"
#include "display_window.hpp"

static DWORD WINAPI InitThread(LPVOID lpParam) {
    // Install Load Profiler I/O and timing hooks immediately
    MK9TradeFix::LoadProfiler::InstallHooks();

    // Ensure ASI Loader executes as early as possible
    ASILoader::Initialize();

    // Wait for main executable modules to initialize
    Sleep(1000);
    MK9TradeFix::Initialize();
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        MK9TradeFix::EarlyInitDisplayPatches();
        MK9TradeFix::LoadProfiler::InitProcessStartTime();
        DisableThreadLibraryCalls(hModule);
        CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
        break;
    case DLL_PROCESS_DETACH:
        MK9TradeFix::Shutdown();
        break;
    }
    return TRUE;
}
