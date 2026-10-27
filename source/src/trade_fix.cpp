// ============================================================================
// Mortal Kombat 9 (Komplete Edition) - Native Physical Trade Engine Mod
// Version: 0.6710a (Void Mode, 3D Floor Grid, Stage Bounds & VFX Lighting Overhaul)
// Release Build Architecture: x86 MSVC Dynamic Hook (dinput8.dll wrapper)
// Target Executable: Mortal Kombat Komplete Edition (MKKE.exe - PC Steam)

#include "trade_fix.hpp"
#include "trade_engine.hpp"
#include "collision_visualizer.hpp"
#include "frame_data.hpp"
#include "display_window.hpp"
#include "negative_edge.hpp"
#include "crouch_buffer_fix.hpp"
#include "meter_drain_fix.hpp"
#include "toasty_training.hpp"
#include "input_recording.hpp"
#include "reversal_engine.hpp"
#include "input_isolation.hpp"
#include "profiler.hpp"
#include "free_camera.hpp"
#include "anim_viewer.hpp"
#include "extra_gameplay.hpp"
#include "juggle_deterioration.hpp"
#include "jump_input_fix.hpp"
#include "stun_modifier.hpp"
#include "combat_state.hpp"
#include "minhook/include/MinHook.h"
#include <cstdio>
#include <windows.h>

namespace MK9TradeFix {

    void Initialize() {
        LogTrace("====================================================================");
        LogTrace(" Mortal Kombat 9 Native Physical Trade Engine Mod (v0.6710a)");
        LogTrace("====================================================================");
        LogTrace("===============================================================================");

        MH_STATUS mhStatus = MH_Initialize();
        if (mhStatus != MH_OK && mhStatus != MH_ERROR_ALREADY_INITIALIZED) {
            LogTrace("[!] MinHook initialization failed (code: %d)!", (int)mhStatus);
            return;
        }

        ModProfiler::Init();
        InstallEngineStabilityHooks();
        InstallDisplayHooks();
        const bool tradeHooksReady = InstallTradeEngineHooks();
        if (!InstallFrameDataHooks()) LogTrace("[!] Native active-frame observation unavailable; frame data remains approximate.");
        if (!tradeHooksReady) {
            g_FixEnabled = false;
            LogTrace("[!] Trade engine unavailable; physical trade fix disabled.");
        }
        NegativeEdge::InstallHooks();
        ToastyTraining::InstallHooks();
        MeterDrainFix::InstallHooks();
        InputIsolation::InstallHooks();
        JumpInputFix::InstallHooks();
        InstallSimulationHooks();
        InputRecordingManager::InstallXInputHooks();
        FreeCamera::InitHooks();
        AnimViewer::Initialize();
        ExtraGameplay::Initialize();
        MoveTelemetryHUD::InstallHooks();
        JuggleDeterioration::Initialize();

        // Initialize Extras Presets and enforce Vanilla Preset as default
        StunModifier::InitPresets();
        StunModifier::ApplyVanillaPreset();

        LogTrace("[*] Module initialization attempts complete. Trade hooks: %s. Other module status requires individual diagnostics.", tradeHooksReady ? "READY" : "FAILED");
    }

    void Shutdown() {
        JuggleDeterioration::Shutdown();
        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();
        LogTrace("[*] MK9 Native Trade Engine hooks disabled and uninitialized.");
    }

    void ToggleFix() {
        if (!AreTradeEngineHooksReady()) {
            LogTrace("[!] Cannot toggle trade fix: required hooks unavailable.");
            return;
        }
        g_FixEnabled = !g_FixEnabled;
        if (!g_FixEnabled) {
            FlushDeferredHit("TRADE ENGINE DISABLED");
        }
        LogTrace(">>> Physical Trade Engine: %s <<<", g_FixEnabled ? "ENABLED" : "DISABLED");
    }

    bool IsFixEnabled() {
        return g_FixEnabled;
    }
}














