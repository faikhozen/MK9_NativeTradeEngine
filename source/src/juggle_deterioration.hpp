#pragma once
#include <windows.h>
#include <cstdint>
#include <atomic>
namespace MK9TradeFix { namespace JuggleDeterioration {
extern std::atomic<bool> g_EnableCustomDecay;
extern std::atomic<float> g_AirDecayMultiplier; // Relative movement scale; native = 1.
extern std::atomic<bool> g_EnablePerHitDeterioration;
extern std::atomic<float> g_PerHitDeteriorationDelta;
extern std::atomic<bool> g_DisableJuggleLimit;
extern std::atomic<int> g_TargetScope; // Receiving fighter.
void Initialize();
void Shutdown();
void OnFrameTick(void* p1Char, void* p2Char);
float __cdecl GetEffectiveAirDecay(void* pChar);
int GetJuggleCount(void* pChar);
void RenderUI();
} }
