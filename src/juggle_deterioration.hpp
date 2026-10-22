#pragma once
#include <windows.h>
#include <cstdint>

namespace MK9TradeFix {
namespace JuggleDeterioration {

    // Configuration Toggles & Values
    extern bool  g_EnableCustomDecay;           // Override default 0.70f air decay
    extern float g_AirDecayMultiplier;          // Distinct air decay value (default: 0.70f, range: 0.05f - 3.0f)
    extern bool  g_EnablePerHitDeterioration;   // Dynamic gravity scaling per juggle hit
    extern float g_PerHitDeteriorationDelta;    // Deterioration delta subtracted per juggle hit (default: 0.060f, range: 0.005f - 0.700f)
    extern float g_GravityPullScale;            // Vertical gravity acceleration scale (default: 3.0f, range: 0.5f - 15.0f)
    extern bool  g_DisableJuggleLimit;          // Disable move juggle limit (infinite popup launches)
    extern int   g_TargetScope;                 // 0 = Both, 1 = P1 only, 2 = P2 only

    // Subsystem Lifecycle
    void Initialize();
    void Shutdown();

    // Frame Tick (invoked from simulation.cpp, telemetry / compatibility)
    void OnFrameTick(void* p1Char, void* p2Char);

    // Query & Logic Helpers
    float __cdecl GetEffectiveAirDecay(void* pChar);
    int   GetJuggleCount(void* pChar);

    // UI Rendering
    void RenderUI();

} // namespace JuggleDeterioration
} // namespace MK9TradeFix
