#include "display_window.hpp"
#include "imgui_overlay.hpp"
#include "free_camera.hpp"
#include "extra_gameplay.hpp"
#include "minhook/include/MinHook.h"
#include <d3d9.h>
#include <atomic>

namespace MK9TradeFix {

    // Forward declarations from collision_visualizer and simulation
    void SafeFlushDebugPrimitives();
    void RenderDebugCollisionPrimitives(void* pChar, int playerIdx);
    bool IsDrawDebugHitboxes();
    bool IsDrawDebugHurtboxes();
    bool IsDrawDebugPushboxes();

    extern std::atomic<bool> g_GamePaused;
    extern std::atomic<bool> g_StepOneFrame;

    DisplayMode g_DisplayMode = DISPLAY_MODE_WINDOWED;
    bool g_AutoFullscreenOn720p = false; // Setting: Lock to Fullscreen on 720p monitor (disabled by default so 720p users are not trapped)
    bool g_UnlockAllResolutions = true; // Setting: Unlock 4:3, 5:4, and non-16:9 resolutions
    static volatile bool g_PendingDisplayToggle = false;

    void EarlyInitDisplayPatches() {
        DWORD oldProt = 0;
        // 1. NOP out memory restriction jump in AddResolutionCandidate (0x005691EB: je 0x569353 -> 6 NOPs)
        if (VirtualProtect((void*)0x005691eb, 6, PAGE_EXECUTE_READWRITE, &oldProt)) {
            memset((void*)0x005691eb, 0x90, 6);
            VirtualProtect((void*)0x005691eb, 6, oldProt, &oldProt);
        }
        // 2. NOP out 0.15 aspect ratio difference jump in AddResolutionCandidate (0x005692F7: jae 0x569352 -> 2 NOPs)
        if (VirtualProtect((void*)0x005692f7, 2, PAGE_EXECUTE_READWRITE, &oldProt)) {
            *(uint8_t*)0x005692f7 = 0x90; // NOP
            *(uint8_t*)0x005692f8 = 0x90; // NOP
            VirtualProtect((void*)0x005692f7, 2, oldProt, &oldProt);
        }
        // 3. Bypass 0.15 aspect ratio purge in InitVideoResolutions (0x0058C4F5: jae 0x58c5ca -> jmp 0x58c5c1 + nop)
        // Original bytes: 0F 83 CF 00 00 00 (jae +0xCF)
        // Replacement:    E9 C7 00 00 00 90 (jmp +0xC7, nop -> unconditionally accepts all resolutions into list)
        if (VirtualProtect((void*)0x0058c4f5, 6, PAGE_EXECUTE_READWRITE, &oldProt)) {
            static const uint8_t s_bypassFilter[] = { 0xE9, 0xC7, 0x00, 0x00, 0x00, 0x90 };
            memcpy((void*)0x0058c4f5, s_bypassFilter, sizeof(s_bypassFilter));
            VirtualProtect((void*)0x0058c4f5, 6, oldProt, &oldProt);
        }
    }

    void InjectAllStandardResolutions() {
        EarlyInitDisplayPatches();

        static const struct { uint32_t w; uint32_t h; } s_extraResolutions[] = {
            // 4:3 Aspect Ratio
            { 800, 600 },
            { 1024, 768 },
            { 1152, 864 },
            { 1280, 960 },
            { 1400, 1050 },
            { 1600, 1200 },
            // 5:4 Aspect Ratio
            { 1280, 1024 },
            // 16:10 Aspect Ratio
            { 1280, 800 },
            { 1440, 900 },
            { 1680, 1050 },
            { 1920, 1200 }
        };

        typedef void (__cdecl *tAddResolution)(uint32_t w, uint32_t h);
        tAddResolution AddResolution = (tAddResolution)0x00569190;

        for (size_t i = 0; i < sizeof(s_extraResolutions)/sizeof(s_extraResolutions[0]); ++i) {
            AddResolution(s_extraResolutions[i].w, s_extraResolutions[i].h);
        }

        typedef void (__cdecl *tSortResolutions)(void* pBegin, void* pEnd, int count);
        tSortResolutions SortResolutions = (tSortResolutions)0x005662C0;

        void* pBegin = *(void**)0x00E53D24;
        void* pEnd   = *(void**)0x00E53D28;
        if (pBegin && pEnd && pEnd > pBegin) {
            int count = (int)((uintptr_t)pEnd - (uintptr_t)pBegin) / 8;
            SortResolutions(pBegin, pEnd, count);
            LogTrace("[DISPLAY] Successfully injected 4:3 & non-16:9 resolutions (total entries in list: %d)", count);
        }
    }

    void SetUnlockAllResolutions(bool enable) {
        g_UnlockAllResolutions = enable;
        if (enable) {
            InjectAllStandardResolutions();
        } else {
            DWORD resProt = 0;
            if (VirtualProtect((void*)0x005692f7, 2, PAGE_EXECUTE_READWRITE, &resProt)) {
                *(uint8_t*)0x005692f7 = 0x73;
                *(uint8_t*)0x005692f8 = 0x59;
                VirtualProtect((void*)0x005692f7, 2, resProt, &resProt);
            }
            if (VirtualProtect((void*)0x0058c4f5, 6, PAGE_EXECUTE_READWRITE, &resProt)) {
                static const uint8_t s_origFilter[] = { 0x0F, 0x83, 0xCF, 0x00, 0x00, 0x00 };
                memcpy((void*)0x0058c4f5, s_origFilter, sizeof(s_origFilter));
                VirtualProtect((void*)0x0058c4f5, 6, resProt, &resProt);
            }
        }
        LogTrace("[DISPLAY] Resolution aspect ratio unlock: %s", enable ? "ENABLED (4:3 / 16:10 unlocked)" : "DISABLED");
    }

    int GetMonitorWidth() {
        HWND hGame = GetGameWindow();
        if (hGame && IsWindow(hGame)) {
            HMONITOR hMon = MonitorFromWindow(hGame, MONITOR_DEFAULTTONEAREST);
            MONITORINFO mi = { sizeof(mi) };
            if (GetMonitorInfoA(hMon, &mi)) {
                return (mi.rcMonitor.right - mi.rcMonitor.left);
            }
        }
        return GetSystemMetrics(SM_CXSCREEN);
    }

    int GetMonitorHeight() {
        HWND hGame = GetGameWindow();
        if (hGame && IsWindow(hGame)) {
            HMONITOR hMon = MonitorFromWindow(hGame, MONITOR_DEFAULTTONEAREST);
            MONITORINFO mi = { sizeof(mi) };
            if (GetMonitorInfoA(hMon, &mi)) {
                return (mi.rcMonitor.bottom - mi.rcMonitor.top);
            }
        }
        return GetSystemMetrics(SM_CYSCREEN);
    }

    bool Is720pMonitor() {
        int w = GetMonitorWidth();
        int h = GetMonitorHeight();
        return (h <= 720 || w <= 1280);
    }

    typedef void (__cdecl *tGetDisplayParams)(uint32_t* pWidth, uint32_t* pHeight, uint8_t* pFullscreen, uint32_t* pAA, uint32_t* pAF);
    static tGetDisplayParams oGetDisplayParams = nullptr;

    static void __cdecl hkGetDisplayParams(uint32_t* pWidth, uint32_t* pHeight, uint8_t* pFullscreen, uint32_t* pAA, uint32_t* pAF) {
        if (oGetDisplayParams) {
            oGetDisplayParams(pWidth, pHeight, pFullscreen, pAA, pAF);
        }
        if (g_AutoFullscreenOn720p && Is720pMonitor()) {
            g_DisplayMode = DISPLAY_MODE_FULLSCREEN;
        }
        uint8_t d3dFs = (g_DisplayMode == DISPLAY_MODE_FULLSCREEN) ? 1 : 0;
        *(uint8_t*)0x00DE9B80 = d3dFs;
        if (pFullscreen) {
            *pFullscreen = d3dFs;
        }
    }

    HWND GetGameWindow() {
        HWND hGame = *(HWND*)0x00E53BF4;
        if (hGame && IsWindow(hGame)) return hGame;
        IDirect3DDevice9* pDev = *(IDirect3DDevice9**)0x00E84534;
        if (pDev) {
            D3DDEVICE_CREATION_PARAMETERS cp;
            if (SUCCEEDED(pDev->GetCreationParameters(&cp)) && cp.hFocusWindow && IsWindow(cp.hFocusWindow)) {
                return cp.hFocusWindow;
            }
        }
        return NULL;
    }

    void ApplyWindowStyleAndPlacement() {
        HWND hGame = GetGameWindow();
        if (!hGame || !IsWindow(hGame)) return;

        uint32_t resX = *(uint32_t*)0x00DE9B78;
        uint32_t resY = *(uint32_t*)0x00DE9B7C;
        if (resX == 0) resX = 1920;
        if (resY == 0) resY = 1080;

        HMONITOR hMon = MonitorFromWindow(hGame, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = { sizeof(mi) };
        BOOL hasMon = GetMonitorInfoA(hMon, &mi);

        if (g_DisplayMode == DISPLAY_MODE_BORDERLESS) {
            LONG style = GetWindowLongA(hGame, GWL_STYLE);
            style &= ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU | WS_BORDER | WS_DLGFRAME);
            style |= (WS_POPUP | WS_VISIBLE);
            SetWindowLongA(hGame, GWL_STYLE, style);

            LONG exStyle = GetWindowLongA(hGame, GWL_EXSTYLE);
            exStyle &= ~(WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE);
            SetWindowLongA(hGame, GWL_EXSTYLE, exStyle);

            int screenX = hasMon ? mi.rcMonitor.left : 0;
            int screenY = hasMon ? mi.rcMonitor.top : 0;
            int screenW = hasMon ? (mi.rcMonitor.right - mi.rcMonitor.left) : GetSystemMetrics(SM_CXSCREEN);
            int screenH = hasMon ? (mi.rcMonitor.bottom - mi.rcMonitor.top) : GetSystemMetrics(SM_CYSCREEN);

            SetWindowPos(hGame, HWND_TOP, screenX, screenY, screenW, screenH,
                         SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_SHOWWINDOW);
            SetForegroundWindow(hGame);
            SetActiveWindow(hGame);
            SetFocus(hGame);
            PostMessageA(hGame, WM_ACTIVATEAPP, TRUE, GetCurrentProcessId());
            PostMessageA(hGame, WM_SETFOCUS, (WPARAM)hGame, 0);
        } else if (g_DisplayMode == DISPLAY_MODE_WINDOWED) {
            LONG style = GetWindowLongA(hGame, GWL_STYLE);
            style &= ~WS_POPUP;
            style |= (WS_OVERLAPPEDWINDOW | WS_VISIBLE);
            SetWindowLongA(hGame, GWL_STYLE, style);

            LONG exStyle = GetWindowLongA(hGame, GWL_EXSTYLE);
            exStyle |= WS_EX_WINDOWEDGE;
            SetWindowLongA(hGame, GWL_EXSTYLE, exStyle);

            RECT rc = { 0, 0, (LONG)resX, (LONG)resY };
            AdjustWindowRectEx(&rc, style, FALSE, exStyle);
            int winW = rc.right - rc.left;
            int winH = rc.bottom - rc.top;

            int workX = hasMon ? mi.rcWork.left : 0;
            int workY = hasMon ? mi.rcWork.top : 0;
            int workW = hasMon ? (mi.rcWork.right - mi.rcWork.left) : GetSystemMetrics(SM_CXSCREEN);
            int workH = hasMon ? (mi.rcWork.bottom - mi.rcWork.top) : GetSystemMetrics(SM_CYSCREEN);

            int posX = workX + (workW - winW) / 2;
            int posY = workY + (workH - winH) / 2;
            if (posX < workX) posX = workX;
            if (posY < workY) posY = workY;

            SetWindowPos(hGame, HWND_TOP, posX, posY, winW, winH,
                         SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_SHOWWINDOW);
            SetForegroundWindow(hGame);
            SetActiveWindow(hGame);
            SetFocus(hGame);
            PostMessageA(hGame, WM_ACTIVATEAPP, TRUE, GetCurrentProcessId());
            PostMessageA(hGame, WM_SETFOCUS, (WPARAM)hGame, 0);
        }
    }

    void UpdateNativeViewportStyle(DWORD style) {
        DWORD oldProt = 0;
        if (VirtualProtect((void*)0x0053cb4b, sizeof(DWORD), PAGE_EXECUTE_READWRITE, &oldProt)) {
            *(DWORD*)0x0053cb4b = style;
            VirtualProtect((void*)0x0053cb4b, sizeof(DWORD), oldProt, &oldProt);
        }
    }

    static DisplayMode g_TargetDisplayMode = DISPLAY_MODE_WINDOWED;
    static volatile bool g_PendingDisplaySet = false;

    void SetDisplayMode(DisplayMode mode) {
        if (g_AutoFullscreenOn720p && Is720pMonitor()) {
            mode = DISPLAY_MODE_FULLSCREEN;
        }
        if (g_DisplayMode == mode) return;
        g_TargetDisplayMode = mode;
        g_PendingDisplaySet = true;
    }

    static void ApplyDisplayModeInternal(DisplayMode targetMode) {
        uint8_t* pFullscreen = (uint8_t*)0x00DE9B80;
        uint32_t* pTargetFullscreen = (uint32_t*)0x00DE9938;
        uint32_t* pResX = (uint32_t*)0x00DE9B78;
        uint32_t* pResY = (uint32_t*)0x00DE9B7C;
        uint32_t* pViewportCount = (uint32_t*)0x00E53D10;
        void*** pViewportList = (void***)0x00E53D0C;

        if (!pFullscreen || !pResX || !pResY) return;

        g_DisplayMode = targetMode;

        uint8_t d3dFs = (g_DisplayMode == DISPLAY_MODE_FULLSCREEN) ? 1 : 0;
        *pFullscreen = d3dFs;
        if (pTargetFullscreen) *pTargetFullscreen = d3dFs;

        // Dynamically patch UE3's internal viewport creation/resize style (0x0053CB4B)
        if (g_DisplayMode == DISPLAY_MODE_BORDERLESS) {
            UpdateNativeViewportStyle(WS_POPUP | WS_VISIBLE);
        } else {
            UpdateNativeViewportStyle(0x00CA0000);
        }

        const char* modeName = (g_DisplayMode == DISPLAY_MODE_BORDERLESS) ? "BORDERLESS WINDOWED" :
                               (g_DisplayMode == DISPLAY_MODE_WINDOWED)   ? "WINDOWED (BORDERED)" : "EXCLUSIVE FULLSCREEN";

        LogTrace("[DISPLAY] >>> Switched Display Mode to %s (%ux%u) <<<",
                 modeName, *pResX, *pResY);

        if (pViewportCount && *pViewportCount > 0 && pViewportList && *pViewportList) {
            __try {
                typedef void(*tApplyVideoSettings)(void);
                tApplyVideoSettings ApplySettings = (tApplyVideoSettings)0x00586730;
                ApplySettings();
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                LogTrace("[!] Exception caught in ApplyVideoSettings during display switch.");
            }

            if (g_DisplayMode != DISPLAY_MODE_FULLSCREEN) {
                ApplyWindowStyleAndPlacement();
            }
        }
    }

        // Hook declarations
    typedef void (__cdecl *tCalcViewportDims)(float w, float h);
    static tCalcViewportDims oCalcViewportDims = nullptr;

    static void __cdecl hkCalcViewportDims(float w, float h) {
        // Enforce true full-frame render target dimensions without letterboxing/pillarboxing
        *(float*)0x00DE9924 = w;
        *(float*)0x00DE9928 = h;
        *(float*)0x00DE992C = w;
        *(float*)0x00DE9930 = h;
        *(uint8_t*)0x00DE9921 = 0; // letterbox flag = false
        *(uint8_t*)0x00DE9922 = 0;

        if (w > 0.0f && h > 0.0f) {
            float aspect = w / h;
            *(float*)0x00E92B48 = aspect;

            DWORD oldProt = 0;
            if (VirtualProtect((void*)0x00C6D9F4, sizeof(float), PAGE_READWRITE, &oldProt)) {
                *(float*)0x00C6D9F4 = aspect;
                VirtualProtect((void*)0x00C6D9F4, sizeof(float), oldProt, &oldProt);
            }
            LogTrace("[DISPLAY] hkCalcViewportDims: %.0fx%.0f (Aspect: %.4f) - True Full-Frame enforced", w, h, aspect);
        }
    }

    typedef float (__cdecl *tGetYScale)(void);
    static tGetYScale oGetYScale = nullptr;

    static float __cdecl hkGetYScale(void) {
        // Always return 1.0f so the post-process blit quad covers 100% of the viewport (NDC -1 to +1)
        // and FUN_00666a80 skips clearing black letterbox bars.
        return 1.0f;
    }

    typedef int (__cdecl *tApplyVideoSettings)(void);
    static tApplyVideoSettings oApplyVideoSettings = nullptr;

    static int __cdecl hkApplyVideoSettings(void) {
        int res = oApplyVideoSettings ? oApplyVideoSettings() : 1;
        if (g_DisplayMode != DISPLAY_MODE_FULLSCREEN) {
            ApplyWindowStyleAndPlacement();
        }
        return res;
    }

    typedef void (__cdecl *tInitVideoResolutions)(void);
    static tInitVideoResolutions oInitVideoResolutions = nullptr;

    static void __cdecl hkInitVideoResolutions(void) {
        if (oInitVideoResolutions) {
            oInitVideoResolutions();
        }
        if (g_UnlockAllResolutions) {
            InjectAllStandardResolutions();
        }
    }

    typedef void (__thiscall *tEngineLoopTick)(void* pThis);
    static tEngineLoopTick oEngineLoopTick = nullptr;

    static void __fastcall hkEngineLoopTick(void* pThis, void* edxUnused) {
        MK9Overlay::CheckAndHookD3D9();

        static int s_initialWindowedEnforceCount = 0;
        if (s_initialWindowedEnforceCount < 20) {
            HWND hGame = GetGameWindow();
            if (hGame && IsWindow(hGame)) {
                s_initialWindowedEnforceCount++;
                if (g_DisplayMode == DISPLAY_MODE_WINDOWED) {
                    ApplyWindowStyleAndPlacement();
                }
            }
        }

        // Ensure standard 4:3, 5:4, and 16:10 resolutions are present on startup
        // Injected ONCE to keep the vector sorted and prevent scrambling the UI carousel index
        static bool s_resolutionsInjected = false;
        if (!s_resolutionsInjected) {
            void* pBegin = *(void**)0x00E53D24;
            void* pEnd   = *(void**)0x00E53D28;
            if (pBegin && pEnd && pEnd >= pBegin) {
                if (g_UnlockAllResolutions) {
                    InjectAllStandardResolutions();
                }
                s_resolutionsInjected = true;
            }
        }

        // Detect resolution change to update aspect ratio and True 4:3 projection matrix
        // NOTE: We DO NOT call InjectAllStandardResolutions() here because cycling resolutions
        // in the Video Settings menu modifies activeResX and re-sorting during browsing scrambles
        // the UI menu index, causing resolutions to jump and disappear.
        static uint32_t s_lastResX = 0, s_lastResY = 0;
        uint32_t activeResX = *(uint32_t*)0x00DE9B78;
        uint32_t activeResY = *(uint32_t*)0x00DE9B7C;
        if (activeResX > 0 && activeResY > 0 && (activeResX != s_lastResX || activeResY != s_lastResY)) {
            s_lastResX = activeResX;
            s_lastResY = activeResY;

            float aspect = (float)activeResX / (float)activeResY;
            // Update global camera tracking aspect ratio (0x00E92B48)
            *(float*)0x00E92B48 = aspect;

            // Update 0x00C6D9F4 (aspect ratio multiplier used by 3D projection matrix and render target sizing)
            DWORD oldProt = 0;
            if (VirtualProtect((void*)0x00C6D9F4, sizeof(float), PAGE_READWRITE, &oldProt)) {
                *(float*)0x00C6D9F4 = aspect;
                VirtualProtect((void*)0x00C6D9F4, sizeof(float), oldProt, &oldProt);
            }
            LogTrace("[DISPLAY] Resolution changed: %ux%u (Aspect: %.4f). Updated projection & camera tracking.", activeResX, activeResY, aspect);
        }

        if (g_PendingDisplaySet) {
            g_PendingDisplaySet = false;
            ApplyDisplayModeInternal(g_TargetDisplayMode);
        } else if (g_PendingDisplayToggle) {
            g_PendingDisplayToggle = false;
            int nextMode = ((int)g_DisplayMode + 1) % 3;
            ApplyDisplayModeInternal((DisplayMode)nextMode);
        }

        // Safe match unload / transition detection for free camera reset
        void* p1 = GetP1Char();
        static void* s_LastP1 = nullptr;
        if (s_LastP1 != nullptr && p1 == nullptr) {
            // Match ended / returning to menu or versus screen
            FreeCamera::ResetCameraSettingsForMatchLoad();
        } else if (p1 != nullptr && p1 != s_LastP1) {
            // New match started / character instance changed
            FreeCamera::ResetCameraSettingsForMatchLoad();
        }
        s_LastP1 = p1;

        if (oEngineLoopTick) oEngineLoopTick(pThis);

        if (g_GamePaused.load()) {
            SafeFlushDebugPrimitives();
            if (IsDrawDebugPushboxes() || IsDrawDebugHurtboxes() || IsDrawDebugHitboxes() || ExtraGameplay::g_EnableFloorGrid) {
                void* p1 = GetP1Char();
                void* p2 = GetP2Char();
                if (p1 && (IsDrawDebugPushboxes() || IsDrawDebugHurtboxes() || IsDrawDebugHitboxes())) RenderDebugCollisionPrimitives(p1, 1);
                if (p2 && (IsDrawDebugPushboxes() || IsDrawDebugHurtboxes() || IsDrawDebugHitboxes())) RenderDebugCollisionPrimitives(p2, 2);
                if (ExtraGameplay::g_EnableFloorGrid) ExtraGameplay::RenderFloorGrid();
            }
        }

        if (g_GamePaused.load() && !g_StepOneFrame.load()) {
            *(int*)0x00E43054 = 1;
            *(int*)0x00E43058 = 1;
        }
    }

    void ToggleFullscreenWindowed() {
        if (g_AutoFullscreenOn720p && Is720pMonitor()) {
            LogTrace("[DISPLAY] Mode switching disabled: 720p monitor detected (Locked to Fullscreen)");
            return;
        }
        g_PendingDisplayToggle = true;
    }

    const char* GetDisplayModeName() {
        return (g_DisplayMode == DISPLAY_MODE_BORDERLESS) ? "BORDERLESS" :
               (g_DisplayMode == DISPLAY_MODE_WINDOWED)   ? "WINDOWED" : "FULLSCREEN";
    }

    void CycleDisplayMode() { ToggleFullscreenWindowed(); }

    static int g_BootLogoSkipsRemaining = 4;

    typedef void* (__stdcall *tBinkOpen)(const char* name, unsigned int flags);
    static tBinkOpen oBinkOpen = nullptr;

    static void* __stdcall hkBinkOpen(const char* name, unsigned int flags) {
        if (name && g_BootLogoSkipsRemaining > 0) {
            if (strstr(name, "WBIntroLogo") || strstr(name, "NRIntroLogo") || strstr(name, "HVSThunderLogoFIX")) {
                g_BootLogoSkipsRemaining--;
                LogTrace("[BINK] >>> Instant Logo Movie Bypass: %s <<<", name);
                return nullptr;
            }
        }
        if (oBinkOpen) return oBinkOpen(name, flags);
        return nullptr;
    }

    bool InstallDisplayHooks() {
        // Hook Native Display Params Query (0x0055C300) to enforce windowed startup
        if (MH_CreateHook((void*)0x0055c300, (void*)hkGetDisplayParams, (void**)&oGetDisplayParams) == MH_OK) {
            MH_EnableHook((void*)0x0055c300);
            LogTrace("[+] Native Display Params Hooked (0x0055C300 - Enforcing Windowed Startup)");
        }

        // Hook Main Engine Loop Tick (0x004025A0) for safe main-thread display mode switching
        if (MH_CreateHook((void*)0x004025a0, (void*)hkEngineLoopTick, (void**)&oEngineLoopTick) == MH_OK) {
            MH_EnableHook((void*)0x004025a0);
            LogTrace("[+] Main Engine Loop Tick Hooked (0x004025A0)");
        }

        // Hook Viewport Dimensions Calculator (0x0052BC70) to eliminate 4:3 / letterboxing height clamp
        if (MH_CreateHook((void*)0x0052bc70, (void*)hkCalcViewportDims, (void**)&oCalcViewportDims) == MH_OK) {
            MH_EnableHook((void*)0x0052bc70);
            LogTrace("[+] Viewport Dimensions Hooked (0x0052BC70 - True Full-Frame Enforced)");
        }

        // Hook Blit Pass Y-Scale Calculator (0x0052BD40) to force 1.0f scaling (no shrunken quad)
        if (MH_CreateHook((void*)0x0052bd40, (void*)hkGetYScale, (void**)&oGetYScale) == MH_OK) {
            MH_EnableHook((void*)0x0052bd40);
            LogTrace("[+] Blit Pass Y-Scale Hooked (0x0052BD40 - Full-Height Quad)");
        }

        // Hook Apply Video Settings (0x00586730) to re-center window in windowed mode
        if (MH_CreateHook((void*)0x00586730, (void*)hkApplyVideoSettings, (void**)&oApplyVideoSettings) == MH_OK) {
            MH_EnableHook((void*)0x00586730);
            LogTrace("[+] Apply Video Settings Hooked (0x00586730)");
        }

        // Hook Video Resolutions Initialization & Filter (0x0058C240) to re-inject standard resolutions if device resets
        if (MH_CreateHook((void*)0x0058c240, (void*)hkInitVideoResolutions, (void**)&oInitVideoResolutions) == MH_OK) {
            MH_EnableHook((void*)0x0058c240);
            LogTrace("[+] Video Resolutions Initialization Hooked (0x0058C240)");
        }

        // Hook binkw32.dll!_BinkOpen@8 to instantly skip startup logo videos directly to Title Screen
        HMODULE hBink = GetModuleHandleA("binkw32.dll");
        if (!hBink) hBink = LoadLibraryA("binkw32.dll");
        if (hBink) {
            void* pBinkOpen = (void*)GetProcAddress(hBink, "_BinkOpen@8");
            if (pBinkOpen && MH_CreateHook(pBinkOpen, (LPVOID)hkBinkOpen, (LPVOID*)&oBinkOpen) == MH_OK) {
                MH_EnableHook(pBinkOpen);
                LogTrace("[+] Bink Movie Player Hooked (_BinkOpen@8 -> Instant Logo Skip)");
            }
        }

        bool is720p = (g_AutoFullscreenOn720p && Is720pMonitor());
        if (is720p) {
            g_DisplayMode = DISPLAY_MODE_FULLSCREEN;
            *(uint8_t*)0x00DE9B80 = 1;
            *(uint32_t*)0x00DE9938 = 1;
            LogTrace("[DISPLAY] 720p & Below Monitor detected (%dx%d) -> Display options disabled, locked to Exclusive Fullscreen",
                     GetMonitorWidth(), GetMonitorHeight());
        } else {
            // Patch options.ini fullscreen parser (0x005889EF: change 'mov [de9b80], 1' to 'mov [de9b80], 0')
            DWORD patchProt = 0;
            if (VirtualProtect((void*)0x005889ef, 1, PAGE_EXECUTE_READWRITE, &patchProt)) {
                *(uint8_t*)0x005889ef = 0x00;
                VirtualProtect((void*)0x005889ef, 1, patchProt, &patchProt);
                LogTrace("[+] Options Parser Fullscreen Override Patched (0x005889EF -> 0x00)");
            }

            // Force initial global variables to windowed
            *(uint8_t*)0x00DE9B80 = 0;
            *(uint32_t*)0x00DE9938 = 0;
        }

        // Patch UE3 FWindowsViewport native style (0x0053CB4B: change 0x00CA0000 to WS_POPUP | WS_VISIBLE)
        UpdateNativeViewportStyle((g_DisplayMode == DISPLAY_MODE_BORDERLESS) ? (WS_POPUP | WS_VISIBLE) : 0x00CA0000);
        LogTrace("[+] UE3 Viewport Native Window Style Patched (0x0053CB4B -> 0x%08X)",
                 (g_DisplayMode == DISPLAY_MODE_BORDERLESS) ? (WS_POPUP | WS_VISIBLE) : 0x00CA0000);

        HWND hInitWin = GetGameWindow();
        if (hInitWin && IsWindow(hInitWin)) {
            ApplyWindowStyleAndPlacement();
        }

        // Unlock all display resolutions (4:3, 5:4, 16:10, 16:9) in in-game video options menu
        SetUnlockAllResolutions(g_UnlockAllResolutions);

        return true;
    }
}
