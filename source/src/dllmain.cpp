#include <windows.h>
#include "trade_fix.hpp"
#include "asi_loader.hpp"
#include "load_profiler.hpp"
#include "display_window.hpp"
#include "executable_compatibility.hpp"

static DWORD WINAPI InitThread(LPVOID lpParam) {
    // Direct engine jumps and global callbacks require process-lifetime residency.
    // Pin before installing any hook that can point back into this DLL.
    HMODULE pinnedModule = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&InitThread), &pinnedModule)) {
        OutputDebugStringA("[MK9] Module pin failed; hook installation aborted.\n");
        return 1;
    }
    // Install Load Profiler I/O and timing hooks immediately
    MK9TradeFix::LoadProfiler::InstallHooks();

    // Ensure ASI Loader executes as early as possible
    ASILoader::Initialize();

    // Wait for main executable modules to initialize
    Sleep(1000);
    if (!MK9TradeFix::ValidateExecutable(true)) {
        OutputDebugStringA("[MK9] Executable patch sites changed during startup; gameplay hooks aborted.\n");
        return 1;
    }
    MK9TradeFix::Initialize();
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        MK9TradeFix::g_ExecutableAccepted = MK9TradeFix::ValidateExecutable();
        if (!MK9TradeFix::g_ExecutableAccepted) {
            OutputDebugStringA("[MK9] Unsupported executable or overlapping patches; mod initialization skipped.\n");
            break;
        }
        MK9TradeFix::EarlyInitDisplayPatches();
        MK9TradeFix::LoadProfiler::InitProcessStartTime();
        DisableThreadLibraryCalls(hModule);
        if (HANDLE thread = CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr)) {
            CloseHandle(thread);
        } else {
            OutputDebugStringA("[MK9] Initialization thread creation failed.\n");
        }
        break;
    case DLL_PROCESS_DETACH:
        // The hooked module is pinned until process termination. Windows reclaims
        // its resources; avoid hook teardown/thread suspension under loader lock.
        break;
    }
    return TRUE;
}
