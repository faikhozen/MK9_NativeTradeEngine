#include "juggle_deterioration.hpp"
#include "juggle_model.hpp"
#include "combat_state.hpp"
#include "trade_fix.hpp"
#include "hook_pair.hpp"
#include "imgui/imgui.h"
#include <cstring>
#include <mutex>

namespace MK9TradeFix { namespace JuggleDeterioration {
std::atomic<bool> g_EnableCustomDecay{false};
std::atomic<float> g_AirDecayMultiplier{1.0f};
std::atomic<bool> g_EnablePerHitDeterioration{false};
std::atomic<float> g_PerHitDeteriorationDelta{0.08f};
std::atomic<bool> g_DisableJuggleLimit{false};
std::atomic<int> g_TargetScope{0};
static bool s_Installed = false;
static std::mutex s_TelemetryMutex;
struct Telemetry { float height = 0, scale = 1; int hits = 0; uint32_t action = 0; bool juggle = false; uint64_t modified = 0, bypasses = 0; };
Telemetry g_JuggleTelemetry[2];
static LaunchGravityState s_Gravity[2];
static void* s_Characters[2] = {};
static int PlayerIndex(void* p) { return p && p == GetP1Char() ? 0 : (p && p == GetP2Char() ? 1 : -1); }
static int* NativeCount(int player) { return (int*)(0x00E89D7C + player * 0xC8C + 0xC64); }
int GetJuggleCount(void* character) {
    const int player = PlayerIndex(character);
    if (player < 0) return 0;
    __try { const int count = *NativeCount(player); return count > 0 ? count : 0; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
float __cdecl GetEffectiveAirDecay(void* character) {
    if (!TargetsPlayer(g_TargetScope.load(), PlayerIndex(character))) return 1;
    return EffectiveMotionScale(g_EnableCustomDecay.load(), g_AirDecayMultiplier.load(),
        g_EnablePerHitDeterioration.load(), g_PerHitDeteriorationDelta.load(), GetJuggleCount(character));
}
static bool ReadCharacter(void* character, float& z, uint32_t& action) {
    __try {
        if (!character) return false;
        const uintptr_t actor = *(uintptr_t*)((uintptr_t)character + 8);
        if (actor < 0x01000000 || actor == 0xDEADBEEF) return false;
        z = *(float*)(actor + 0xE4);
        action = *(uint32_t*)((uintptr_t)character + 0x1D4);
        return std::isfinite(z);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void OnFrameTick(void* p1, void* p2) {
    void* characters[] = {p1, p2};
    Telemetry samples[2];
    for (int i = 0; i < 2; ++i) {
        float z = 0; uint32_t action = 0;
        const bool valid = ReadCharacter(characters[i], z, action);
        if (s_Characters[i]!=characters[i]) s_Gravity[i].active = false;
        s_Characters[i]=characters[i];
        samples[i].height = z; samples[i].action = action;
        samples[i].hits = valid && IsJuggleReaction(action) && z > 1 ? GetJuggleCount(characters[i]) : 0;
        samples[i].scale = valid ? GetEffectiveAirDecay(characters[i]) : 1;
        samples[i].juggle = valid && IsJuggleReaction(action) && z > 1;
    }
    std::lock_guard<std::mutex> lock(s_TelemetryMutex);
    for (int i = 0; i < 2; ++i) {
        samples[i].modified = g_JuggleTelemetry[i].modified;
        samples[i].bypasses = g_JuggleTelemetry[i].bypasses;
        g_JuggleTelemetry[i] = samples[i];
    }
}
typedef void (__thiscall* BallisticFn)(void*);
static BallisticFn s_BallisticOriginal = nullptr;
static void RecordBallistic(int player, float scale, float height) {
    std::lock_guard<std::mutex> lock(s_TelemetryMutex);
    auto& sample=g_JuggleTelemetry[player]; sample.scale=scale; sample.height=height; sample.juggle=true;
    if(scale!=1) ++sample.modified;
}
static void __fastcall BallisticHook(void* character, void*) {
    const int player=PlayerIndex(character);
    uint32_t action=0; float z=0;
    if (player<0 || !ReadCharacter(character,z,action)) { s_BallisticOriginal(character); return; }
    float* gravity=(float*)((uintptr_t)character+0x328);
    if (!std::isfinite(*gravity) || *gravity>=0) {
        s_Gravity[player].active=false; s_BallisticOriginal(character); return;
    }
    const float scale=IsJuggleReaction(action) ? GetEffectiveAirDecay(character) : 1;
    // Apply BEFORE the native launch initializer solves its landing time and
    // synchronizes the reaction animation. The native integrator then owns the whole flight.
    *gravity=s_Gravity[player].Begin(*gravity,scale);
    s_BallisticOriginal(character);
    // Native initialization rounds duration to a whole frame and normalizes gravity.
    s_Gravity[player].Finish(*gravity,scale);
    RecordBallistic(player,scale,*(float*)((uintptr_t)character+0x348));
}
// Native lookup reads the receiving fighter's juggle counter. Zero it ONLY during
// that lookup, then restore it, including when native structured exceptions unwind.
typedef int (__thiscall* LookupFn)(void*, int, uint32_t*, int*, int*, uint32_t*);
static LookupFn s_Lookup = nullptr;
static void RecordBypass(int player) { std::lock_guard<std::mutex> lock(s_TelemetryMutex); ++g_JuggleTelemetry[player].bypasses; }
static int __fastcall LookupReaction(void* character, void*, int move, uint32_t* package,
                                    int* function, int* reaction, uint32_t* extra) {
    const int player = PlayerIndex(character);
    int* count = nullptr; int saved = 0;
    if (g_DisableJuggleLimit.load() && TargetsPlayer(g_TargetScope.load(), player)) {
        count = NativeCount(player); saved = *count; *count = 0;
    }
    int result = 0;
    __try { result = s_Lookup(character, move, package, function, reaction, extra); }
    __finally { if (count) *count = saved; }
    if (count && saved > 0) {
        RecordBypass(player);
    }
    return result;
}
void Initialize() {
    if (s_Installed) return;
    const BYTE lookup[] = {0x55,0x8B,0xEC,0x6A,0xFF,0x68,0x0B,0xD9,0xC1,0};
    const BYTE ballistic[] = {0x55,0x8B,0xEC,0x83,0xEC,0x08,0x56,0x57,0x8B,0xF1};
    __try {
        if (memcmp((void*)0x0089DE30, lookup, sizeof(lookup)) || memcmp((void*)0x0044B820, ballistic, sizeof(ballistic))) {
            LogTrace("[JUGGLE] Native signature mismatch; controls unavailable."); return;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { return; }
    const HookSpec hooks[] = {
        {(void*)0x0089DE30, (void*)LookupReaction, (void**)&s_Lookup},
        {(void*)0x0044B820, (void*)BallisticHook, (void**)&s_BallisticOriginal}
    };
    s_Installed = InstallHookPair(hooks, MH_CreateHook, MH_EnableHook, MH_DisableHook, MH_RemoveHook,
        [](const char* stage, void* target, MH_STATUS status) { LogTrace("[JUGGLE] %s failed at %p (%d)",stage,target,status); });
    LogTrace("[JUGGLE] Scoped reaction lookup + native launch gravity + landing-time hook: %s",s_Installed ? "ready" : "FAILED");
}
void Shutdown() {
    if (!s_Installed) return;
    MH_DisableHook((void*)0x0044B820); MH_DisableHook((void*)0x0089DE30);
    MH_RemoveHook((void*)0x0044B820); MH_RemoveHook((void*)0x0089DE30); s_Installed = false;
}
static void Checkbox(const char* label, std::atomic<bool>& setting) { bool value = setting.load(); if (ImGui::Checkbox(label, &value)) setting.store(value); }
static void Slider(const char* label, std::atomic<float>& setting, float low, float high, const char* format) { float value = setting.load(); if (ImGui::SliderFloat(label,&value,low,high,format)) setting.store(value); }
void RenderUI() {
    if (!ImGui::CollapsingHeader("Juggle Gravity & Limits")) return;
    if (!s_Installed) { ImGui::TextDisabled("Unavailable: native hooks did not install."); return; }
    ImGui::TextWrapped("Target is the fighter receiving the juggle. Normal jumps and air attacks keep native movement.");
    ImGui::Text("Receiving fighter:"); ImGui::SameLine();
    const char* scopes[] = {"Both##j_scope0","Player 1##j_scope1","Player 2##j_scope2"};
    for (int i=0;i<3;++i) { if(i) ImGui::SameLine(); if(ImGui::RadioButton(scopes[i],g_TargetScope.load()==i)) g_TargetScope.store(i); }
    Checkbox("Custom juggle gravity##j_custom",g_EnableCustomDecay);
    if (g_EnableCustomDecay.load()) Slider("Floatiness##j_scale",g_AirDecayMultiplier,0.25f,2,"%.2fx");
    ImGui::TextWrapped("Applies at each new launch. 1.00 = native gravity; higher = lower gravity / longer flight.");
    Checkbox("Deteriorate on follow-up juggle hits##j_perhit",g_EnablePerHitDeterioration);
    if (g_EnablePerHitDeterioration.load()) Slider("Scale lost per follow-up##j_delta",g_PerHitDeteriorationDelta,0.01f,0.30f,"%.2f");
    ImGui::TextWrapped("Each native air-hit count lowers floatiness; first grounded launch is unchanged.");
    Checkbox("Bypass native count-based reaction limits##j_limit",g_DisableJuggleLimit);
    ImGui::TextWrapped("Keeps the real counter. Move-specific script restrictions and gravity still apply; this does not guarantee infinite combos.");
    Telemetry samples[2]; { std::lock_guard<std::mutex> lock(s_TelemetryMutex); memcpy(samples,g_JuggleTelemetry,sizeof(samples)); }
    ImGui::Separator();
    for(int i=0;i<2;++i) ImGui::Text("P%d: Z %.1f | Juggle count %d | Scale %.2f | %s | Launches %llu | Bypass %llu",i+1,
        samples[i].height,samples[i].hits,samples[i].scale,samples[i].juggle ? "JUGGLE" : (samples[i].height>1 ? "OTHER AIR" : "GROUND"),samples[i].modified,samples[i].bypasses);
    if(ImGui::Button("Native##j_native")) { g_EnableCustomDecay=false; g_EnablePerHitDeterioration=false; g_DisableJuggleLimit=false; g_AirDecayMultiplier=1; g_PerHitDeteriorationDelta=0.08f; }
    ImGui::SameLine();
    if(ImGui::Button("Floaty 1.50x##j_floaty")) { g_EnableCustomDecay=true; g_AirDecayMultiplier=1.5f; g_EnablePerHitDeterioration=false; g_DisableJuggleLimit=false; }
    ImGui::SameLine();
    if(ImGui::Button("Heavy 0.50x##j_heavy")) { g_EnableCustomDecay=true; g_AirDecayMultiplier=0.5f; g_EnablePerHitDeterioration=false; g_DisableJuggleLimit=false; }
    ImGui::SameLine();
    if(ImGui::Button("Deterioration##j_deterioration")) { g_EnableCustomDecay=false; g_EnablePerHitDeterioration=true; g_PerHitDeteriorationDelta=0.08f; g_DisableJuggleLimit=false; }
}
} }




