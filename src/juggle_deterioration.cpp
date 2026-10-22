#include "juggle_deterioration.hpp"
#include "combat_state.hpp"
#include "trade_fix.hpp"
#include "imgui/imgui.h"
#include <cstdio>
#include <cmath>
#include <algorithm>

namespace MK9TradeFix {
namespace JuggleDeterioration {

    bool  g_EnableCustomDecay         = false;
    float g_AirDecayMultiplier        = 0.70f;
    bool  g_EnablePerHitDeterioration = false;
    float g_PerHitDeteriorationDelta  = 0.060f;
    float g_GravityPullScale          = 3.0f;
    bool  g_DisableJuggleLimit        = false;
    int   g_TargetScope               = 0; // 0 = Both, 1 = P1 only, 2 = P2 only

    // Internal Juggle Hit Tracking (tracks consecutive hits per air combo)
    static int g_InternalJuggleHits[2] = { 0, 0 };

    // Air Physics Tracker: tracks relative velocity delta
    struct AirPhysicsTracker {
        float prevZ          = 0.0f;
        int   airborneFrames = 0;
        bool  wasAirborne    = false;
    };
    static AirPhysicsTracker s_AirTracker[2];

    static const uintptr_t HOOK_INC_JUGGLE_COUNTER = 0x00895962;
    static const uintptr_t RET_INC_JUGGLE_COUNTER  = 0x00895969;
    static BYTE g_OrigBytesCounter[7] = { 0 };

    static const uintptr_t HOOK_PUSHBOX_POS        = 0x008B536B;
    static const uintptr_t RET_PUSHBOX_POS         = 0x008B5371;
    static BYTE g_OrigBytesPushbox[6] = { 0 };

    static bool g_HooksInstalled = false;

    // Resolve Player Index: 0 = P1, 1 = P2, -1 = Other
    static int ResolvePlayerIndex(void* pChar) {
        if (!pChar || (uintptr_t)pChar < 0x01000000) return -1;
        void* p1 = MK9TradeFix::GetP1Char();
        void* p2 = MK9TradeFix::GetP2Char();
        if (pChar == p1) return 0;
        if (pChar == p2) return 1;

        __try {
            uintptr_t pInfo = *(uintptr_t*)((uintptr_t)pChar + 0x7638);
            if (pInfo && (uintptr_t)pInfo > 0x00400000) {
                int idx = *(int*)(pInfo + 0xB3C);
                if (idx >= 0 && idx <= 1) return idx;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}

        return -1;
    }

    int GetJuggleCount(void* pChar) {
        if (!pChar) return 0;
        int playerIdx = ResolvePlayerIndex(pChar);
        if (playerIdx >= 0 && playerIdx <= 1) {
            if (g_InternalJuggleHits[playerIdx] > 0) {
                return g_InternalJuggleHits[playerIdx];
            }
            uintptr_t playerInfoBase = 0x00E89D7C + (uintptr_t)playerIdx * 0xC8C;
            __try {
                return *(int*)(playerInfoBase + 0xC64);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return 0;
            }
        }
        return 0;
    }

    float __cdecl GetEffectiveAirDecay(void* pChar) {
        float baseDecay = g_EnableCustomDecay ? g_AirDecayMultiplier : 0.70f;

        if (!pChar) return baseDecay;

        int playerIdx = ResolvePlayerIndex(pChar);

        // Check Target Scope
        if (g_TargetScope == 1 && playerIdx != 0) return 0.70f;
        if (g_TargetScope == 2 && playerIdx != 1) return 0.70f;

        if (!g_EnableCustomDecay && !g_EnablePerHitDeterioration) {
            return 0.70f;
        }

        if (g_EnablePerHitDeterioration) {
            int juggleCount = GetJuggleCount(pChar);
            if (juggleCount > 0) {
                // Deteriorate air decay: subtract per-hit delta for each consecutive juggle hit
                baseDecay -= ((float)juggleCount * g_PerHitDeteriorationDelta);
                if (baseDecay < 0.01f) {
                    baseDecay = 0.01f;
                }
            }
        }

        return baseDecay;
    }

    // Called from assembly hook hkIncJuggleCounter
    extern "C" void __cdecl OnJuggleHitIncrement(void* pPlayerInfo) {
        if (!pPlayerInfo || (uintptr_t)pPlayerInfo < 0x00400000) return;
        __try {
            int playerIdx = *(int*)((uintptr_t)pPlayerInfo + 0xB3C);
            if (playerIdx >= 0 && playerIdx <= 1) {
                g_InternalJuggleHits[playerIdx]++;
            }

            int* pNativeCount = (int*)((uintptr_t)pPlayerInfo + 0xC64);
            if (g_DisableJuggleLimit) {
                *pNativeCount = 1; // Clamp native counter to 1 so moves never refuse to launch
            } else {
                (*pNativeCount)++;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    // Naked Trampoline: Juggle Counter Increment Hook
    static void* g_pRetIncJuggleCounter = (void*)RET_INC_JUGGLE_COUNTER;
    __declspec(naked) static void hkIncJuggleCounter() {
        __asm {
            push eax
            push ecx
            push edx
            push eax // pass pPlayerInfo
            call OnJuggleHitIncrement
            add esp, 4
            pop edx
            pop ecx
            pop eax
            jmp dword ptr [g_pRetIncJuggleCounter]
        }
    }

    // Authoritative Pushbox & Stage Boundary Height Processor
    // Hooked at 0x008B536B (end of combat frame tick, immediately before FUN_008b43a0 and AActor::MoveActor)
    extern "C" void __cdecl ProcessPushboxPositions(void* p1Char, void* p2Char, float* pP1_Z, float* pP2_Z) {
        if (!g_EnableCustomDecay && !g_EnablePerHitDeterioration) {
            return;
        }

        void* chars[2] = { p1Char, p2Char };
        float* pZ_ptrs[2] = { pP1_Z, pP2_Z };

        for (int i = 0; i < 2; ++i) {
            void* pChar = chars[i];
            float* pZ = pZ_ptrs[i];
            if (!pChar || !pZ || (uintptr_t)pChar < 0x01000000) continue;

            // Target Scope check: 0 = Both, 1 = P1 only, 2 = P2 only
            if (g_TargetScope == 1 && i != 0) continue;
            if (g_TargetScope == 2 && i != 1) continue;

            uint32_t action = *(uint32_t*)((uintptr_t)pChar + 0x1D4);
            float rawZ = *pZ;
            uint32_t stance = *(uint32_t*)((uintptr_t)pChar + 0x2E8);
            bool isAirborne = (rawZ > 1.0f) || ((stance & 1) != 0);

            // 1. ABSOLUTE JUMP ISOLATION:
            // Normal neutral (0x0004), forward (0x0005), and backward (0x0006) jumps, air attacks (< 0x4000),
            // and landings (0x2007) all have action < 0x4000.
            // In MK9, all hit reactions strictly have action >= 0x4000.
            // Custom Air Decay MUST NEVER touch normal jumps under any circumstances.
            if (action < 0x4000) {
                s_AirTracker[i].wasAirborne    = false;
                s_AirTracker[i].prevZ          = rawZ;
                s_AirTracker[i].airborneFrames = 0;
                continue;
            }

            // 2. Grounded hit reactions (grounded staggers, sweeps resting on floor):
            if (!isAirborne || rawZ <= 1.0f) {
                s_AirTracker[i].wasAirborne    = false;
                s_AirTracker[i].prevZ          = rawZ;
                s_AirTracker[i].airborneFrames = 0;
                g_InternalJuggleHits[i]        = 0;
                continue;
            }

            // 3. AIRBORNE JUGGLE HIT REACTION (action >= 0x4000 and isAirborne):
            if (!s_AirTracker[i].wasAirborne) {
                // First airborne frame of this launch/juggle
                s_AirTracker[i].wasAirborne    = true;
                s_AirTracker[i].prevZ          = rawZ;
                s_AirTracker[i].airborneFrames = 1;
            } else {
                s_AirTracker[i].airborneFrames++;
            }

            float effDecay = GetEffectiveAirDecay(pChar);
            float nominalDecay = 0.70f;
            float ratio = effDecay / nominalDecay;

            if (fabsf(ratio - 1.0f) < 0.001f) {
                s_AirTracker[i].prevZ = rawZ;
                continue;
            }

            // Compute native displacement step applied by the engine this frame
            float v = rawZ - s_AirTracker[i].prevZ;

            // Clamp v to physical velocity bounds to eliminate any sudden anomalous spikes
            if (v > 80.0f)  v = 80.0f;
            if (v < -80.0f) v = -80.0f;

            // Scale vertical velocity according to decay ratio:
            // Rising (v > 0): scale apex (lower if heavier, higher if floaty)
            // Falling (v <= 0): scale downward drop proportionally
            float v_adj = v;
            if (v > 0.0f) {
                v_adj = v * ratio;
            } else {
                if (ratio > 1.0f) {
                    v_adj = v * (1.0f / ratio); // Floaty: slower descent
                } else {
                    v_adj = v * (1.0f + (1.0f - ratio) * 1.0f); // Deteriorated/Heavy: noticeably faster drop
                }
            }

            float newZ = s_AirTracker[i].prevZ + v_adj;

            // Ground & Ceiling bounds:
            if (newZ <= 1.0f) {
                newZ = 0.0f;
                s_AirTracker[i].wasAirborne    = false;
                s_AirTracker[i].prevZ          = 0.0f;
                s_AirTracker[i].airborneFrames = 0;
            } else {
                if (newZ > 420.0f) {
                    newZ = 420.0f;
                }
                s_AirTracker[i].prevZ = newZ;
            }

            // Authoritative write to pushbox stack coordinate.
            // The engine's FUN_008b43a0 and SetLocation (0x433580) immediately follow our hook
            // and authoritatively update Actor Location.Z (0xE4) and sweep collision physics cleanly.
            *pZ = newZ;
        }
    }

    // Naked Trampoline: Pushbox Height Resolution Hook
    static void* g_pRetPushboxPos = (void*)RET_PUSHBOX_POS;
    __declspec(naked) static void hkPushboxPositionZ() {
        __asm {
            push eax
            push ecx
            push edx
            push ebx
            push esi
            push edi

            lea eax, [ebp - 0x28] // &P2_Z
            push eax
            lea ecx, [ebp - 0x30] // &P1_Z
            push ecx
            mov edx, dword ptr [ebp - 0x24] // P2 MKCharacter*
            push edx
            push edi // P1 MKCharacter*
            call ProcessPushboxPositions
            add esp, 16

            pop edi
            pop esi
            pop ebx
            pop edx
            pop ecx
            pop eax

            // Original instruction: mov edx, dword ptr [edi + 0x1d0] (6 bytes: 8B 97 D0 01 00 00)
            mov edx, dword ptr [edi + 0x1D0]
            jmp dword ptr [g_pRetPushboxPos]
        }
    }

    static void InstallHook(uintptr_t targetAddr, void* hookFunc, BYTE* origBytes, size_t len) {
        DWORD oldProtect = 0;
        VirtualProtect((LPVOID)targetAddr, len, PAGE_EXECUTE_READWRITE, &oldProtect);
        memcpy(origBytes, (void*)targetAddr, len);

        BYTE* p = (BYTE*)targetAddr;
        p[0] = 0xE9; // JMP rel32
        uintptr_t relOffset = (uintptr_t)hookFunc - (targetAddr + 5);
        *(uintptr_t*)(p + 1) = relOffset;

        for (size_t i = 5; i < len; ++i) {
            p[i] = 0x90; // NOP padding
        }

        VirtualProtect((LPVOID)targetAddr, len, oldProtect, &oldProtect);
        FlushInstructionCache(GetCurrentProcess(), (LPCVOID)targetAddr, len);
    }

    static void RemoveHook(uintptr_t targetAddr, BYTE* origBytes, size_t len) {
        if (!origBytes[0]) return;
        DWORD oldProtect = 0;
        VirtualProtect((LPVOID)targetAddr, len, PAGE_EXECUTE_READWRITE, &oldProtect);
        memcpy((void*)targetAddr, origBytes, len);
        VirtualProtect((LPVOID)targetAddr, len, oldProtect, &oldProtect);
        FlushInstructionCache(GetCurrentProcess(), (LPCVOID)targetAddr, len);
    }

    void Initialize() {
        if (g_HooksInstalled) return;

        InstallHook(HOOK_INC_JUGGLE_COUNTER, (void*)hkIncJuggleCounter, g_OrigBytesCounter, 7);
        InstallHook(HOOK_PUSHBOX_POS,        (void*)hkPushboxPositionZ, g_OrigBytesPushbox, 6);

        g_HooksInstalled = true;
        MK9TradeFix::LogTrace("[JUGGLE DETERIORATION] Juggle counter hook (0x00895962) and Pushbox Z hook (0x008B536B) initialized.");
    }

    void Shutdown() {
        if (!g_HooksInstalled) return;

        RemoveHook(HOOK_INC_JUGGLE_COUNTER, g_OrigBytesCounter, 7);
        RemoveHook(HOOK_PUSHBOX_POS,        g_OrigBytesPushbox, 6);

        g_HooksInstalled = false;
        MK9TradeFix::LogTrace("[JUGGLE DETERIORATION] Hooks removed.");
    }

    void OnFrameTick(void* p1Char, void* p2Char) {
        // Maintained for external caller compatibility. Physics modification is authoritatively driven by hkPushboxPositionZ.
    }

    void RenderUI() {
        if (ImGui::CollapsingHeader("Juggle Value & Gravity Deterioration")) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 1.0f, 1.0f), "Air Juggle Physics & Decay Tuning:");
            ImGui::TextDisabled("Controls gravity decay, downward air drag, and consecutive juggle launch deterioration limits.");
            ImGui::Spacing();

            // Target Scope
            ImGui::Text("Target Scope:");
            ImGui::SameLine();
            if (ImGui::RadioButton("Both Players##jd_scope_both", g_TargetScope == 0)) {
                g_TargetScope = 0;
            }
            ImGui::SameLine();
            if (ImGui::RadioButton("Player 1 Only##jd_scope_p1", g_TargetScope == 1)) {
                g_TargetScope = 1;
            }
            ImGui::SameLine();
            if (ImGui::RadioButton("Player 2 Only##jd_scope_p2", g_TargetScope == 2)) {
                g_TargetScope = 2;
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // 1. Custom Air Decay Multiplier
            ImGui::Checkbox("Enable Custom Air Decay Rate##g_custom_decay", &g_EnableCustomDecay);
            ImGui::SameLine();
            ImGui::TextColored(g_EnableCustomDecay ? ImVec4(0.2f, 1.0f, 0.3f, 1.0f) : ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                               g_EnableCustomDecay ? "[ACTIVE]" : "[VANILLA 0.70x]");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Directly tunes character airborne physics & velocity decay (Default: 0.70x).\n"
                                  "Applies ONLY to launched / juggled opponents by default.\n"
                                  "Higher values (e.g. 0.85 - 1.00): Characters float longer for high-juggle combos.\n"
                                  "Lower values (e.g. 0.10 - 0.45): Opponents drop rapidly to ground upon hit.");
            }

            if (g_EnableCustomDecay) {
                ImGui::Indent(20.0f);
                ImGui::SliderFloat("Air Decay Multiplier##slider_air_decay", &g_AirDecayMultiplier, 0.05f, 3.00f, "%.3fx");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Vanilla = 0.700x. Controls how much vertical air velocity remains each physics tick.\n"
                                      "0.100x - 0.400x = Heavy gravity / rapid drop.\n"
                                      "0.700x = Vanilla standard.\n"
                                      "1.000x - 1.500x = High floaty combos.\n"
                                      "2.000x - 3.000x = Super floaty moon gravity.");
                }
                ImGui::TextDisabled("   Air decay factor (Vanilla: 0.700x).");
                ImGui::Unindent(20.0f);
            }

            ImGui::Spacing();

            // 2. Per-Hit Juggle Deterioration
            ImGui::Checkbox("Enable Per-Hit Juggle Deterioration##g_per_hit_decay", &g_EnablePerHitDeterioration);
            ImGui::SameLine();
            ImGui::TextColored(g_EnablePerHitDeterioration ? ImVec4(0.2f, 1.0f, 0.3f, 1.0f) : ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                               g_EnablePerHitDeterioration ? "[SCALING ACTIVE]" : "[OFF]");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Dynamically increases gravity / downward pull with each landed juggle hit.\n"
                                  "Formula: Effective Decay = Base Decay - (Juggle Count * Delta).\n"
                                  "Prevents infinite corner loops by making airborne opponents fall progressively faster.");
            }

            if (g_EnablePerHitDeterioration) {
                ImGui::Indent(20.0f);
                ImGui::SliderFloat("Deterioration Delta Per Hit##slider_hit_delta", &g_PerHitDeteriorationDelta, 0.005f, 0.700f, "-%.3fx per hit");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Deterioration subtracted from air decay per juggle hit (Default: 0.060x).\n"
                                      "0.010x - 0.050x: Subtle decay (prolongs combos slightly).\n"
                                      "0.060x - 0.150x: Noticeable decay (-9%% to -21%% apex & drop speed per hit).\n"
                                      "0.200x - 0.700x: Extreme decay (opponents rapidly drop / ground out after 1-2 juggle hits).");
                }
                float pctPerHit = (g_PerHitDeteriorationDelta / 0.70f) * 100.0f;
                ImGui::TextDisabled("   Deducts %.3fx from air decay on each juggle hit (-%.1f%% apex & +%.1f%% gravity/hit).",
                                    g_PerHitDeteriorationDelta, pctPerHit, pctPerHit);
                ImGui::Unindent(20.0f);
            }

            ImGui::Spacing();

            // 3. Move Juggle Limit Bypass
            ImGui::Checkbox("Bypass Move Juggle Limit (Infinite Pop-ups)##g_no_juggle_limit", &g_DisableJuggleLimit);
            ImGui::SameLine();
            ImGui::TextColored(g_DisableJuggleLimit ? ImVec4(0.2f, 1.0f, 0.3f, 1.0f) : ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                               g_DisableJuggleLimit ? "[UNLOCKED]" : "[VANILLA LIMITS]");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("In vanilla MK9, moves deny popup/launcher reactions after 2-3 juggle hits (0x0089DE30).\n"
                                  "When UNLOCKED: Clamps native juggle counter at 0x00895962 so moves never refuse to launch!");
            }
            ImGui::TextDisabled("   Allows repeated pop-up attacks to launch opponents without forced untechable air fall.");

            ImGui::Spacing();

            // Live Match Telemetry
            void* p1 = MK9TradeFix::GetP1Char();
            void* p2 = MK9TradeFix::GetP2Char();
            int p1Juggles = p1 ? GetJuggleCount(p1) : 0;
            int p2Juggles = p2 ? GetJuggleCount(p2) : 0;
            float p1EffDecay = p1 ? GetEffectiveAirDecay(p1) : 0.70f;
            float p2EffDecay = p2 ? GetEffectiveAirDecay(p2) : 0.70f;
            auto GetCharZ = [](void* pChar) -> float {
                if (!pChar || (uintptr_t)pChar < 0x01000000) return 0.0f;
                uintptr_t pActor = *(uintptr_t*)((uintptr_t)pChar + 8);
                if (pActor > 0x01000000) {
                    return *(float*)(pActor + 0xE4);
                }
                return 0.0f;
            };
            float p1Z = GetCharZ(p1);
            float p2Z = GetCharZ(p2);

            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "Live Juggle Telemetry:");
            ImGui::Text("   P1: Z = %6.1f | Hits: %d | Decay: %.3fx | State: %s",
                        p1Z, p1Juggles, p1EffDecay, p1Z <= 1.0f ? "GROUND" : (s_AirTracker[0].wasAirborne ? "JUGGLE_AIR" : "JUMP"));
            ImGui::Text("   P2: Z = %6.1f | Hits: %d | Decay: %.3fx | State: %s",
                        p2Z, p2Juggles, p2EffDecay, p2Z <= 1.0f ? "GROUND" : (s_AirTracker[1].wasAirborne ? "JUGGLE_AIR" : "JUMP"));

            ImGui::Spacing();

            // Quick Preset Buttons
            if (ImGui::Button("Vanilla Standard (0.70x)##btn_vanilla_decay")) {
                g_EnableCustomDecay         = false;
                g_AirDecayMultiplier        = 0.70f;
                g_EnablePerHitDeterioration = false;
                g_PerHitDeteriorationDelta  = 0.01f;
                g_DisableJuggleLimit        = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Floaty Moon (0.90x / No Limit)##btn_floaty_juggles")) {
                g_EnableCustomDecay         = true;
                g_AirDecayMultiplier        = 0.90f;
                g_EnablePerHitDeterioration = false;
                g_DisableJuggleLimit        = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Heavy Juggle (0.35x)##btn_heavy_juggle")) {
                g_EnableCustomDecay         = true;
                g_AirDecayMultiplier        = 0.35f;
                g_EnablePerHitDeterioration = false;
                g_DisableJuggleLimit        = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Aggressive Decay (-0.080/Hit)##btn_aggr_decay")) {
                g_EnableCustomDecay         = false;
                g_AirDecayMultiplier        = 0.70f;
                g_EnablePerHitDeterioration = true;
                g_PerHitDeteriorationDelta  = 0.080f;
                g_DisableJuggleLimit        = false;
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
        }
    }

} // namespace JuggleDeterioration
} // namespace MK9TradeFix
