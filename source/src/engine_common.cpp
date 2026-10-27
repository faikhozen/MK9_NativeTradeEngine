#include "engine_common.hpp"
#include "imgui_overlay.hpp"
#include "input_isolation.hpp"
#include <cstdarg>
#include <cstdio>
#include <share.h>

namespace MK9TradeFix {

    bool     g_FixEnabled = true;
    bool     g_LoggingEnabled = false;
    bool     g_P1_NegativeEdgeEnabled = false; // Tournament Standard: Release Check OFF by default
    bool     g_P2_NegativeEdgeEnabled = false; // Tournament Standard: Release Check OFF by default
    int      g_P1_XInputPort = 0;              // Default Controller Port 1 (Index 0)
    int      g_P2_XInputPort = 1;              // Default Controller Port 2 (Index 1)
    int      g_KeyboardAssignedPlayer = 2;     // Default to P2 (Dummy) on PC to prevent controller lock
    int      g_DetectingControllerForPlayer = 0; // 0 = Idle, 1 = Listening for P1, 2 = Listening for P2
    uint64_t g_DetectionStartFrame = 0;
    uint64_t g_CurrentFrame = 0;
    uint64_t g_LastHitFrame = 0;
    int      g_LastHitAttacker = 0;
    int      g_HitEventCount = 0;
    int      g_TradeEventCount = 0;

    int      g_Mode13_BaseOffset = 30;
    int      g_Mode13_OffsetStep = 2;
    int      g_Mode13_RollDirection = 0; // 0: Back, 1: Forward, 2: Rocker Fwd/Back, 3: Rocker Back/Fwd
    int      g_Mode13_RollPattern = 0;   // 0: Single Tap (3f), 1: Double Tap, 2: Mash (Rapid Tap)

    int      g_Mode14_KnockdownMove = 0;   // 0: Sweep (B+4), 1: Uppercut (D+2)
    int      g_Mode14_FloorRelOffset = 0;  // Offset relative to floor contact 0x460D (-25 to +30)
    int      g_Mode14_HoldFrames = 90;     // Hold duration for window test (default: 90 frames)
    int      g_Mode14_BaseOffset = 38;      // Sweep Down offset (+24f post-hit)
    int      g_Mode14_UppercutOffset = 63;  // Uppercut Down offset (+54f post-hit)
    int      g_Mode14_ActiveFNum = 1;
    int      g_Mode15_ActiveFNum = 1;
    int      g_Mode16_ActiveFNum = 1;
    int      g_Mode17_ActiveFNum = 1;
    int         g_Mode18_ActiveFNum = 1;
    bool        g_Mode18_DefenderBlocks = false;
    bool        g_Mode18_SpecialTriggered = false;
    uint64_t    g_Mode18_SpecialTriggerFrame = 0;
    void*       g_Mode18_SpecialMoveInfo = nullptr;
    bool        g_Mode18_SpecialIsEX = false;
    bool        g_Mode18_SpecialIsXRay = false;
    bool        g_Mode18_FollowUpHitDetected = false;
    uint64_t    g_Mode18_FollowUpHitFrame = 0;
    float       g_Mode18_FollowUpDamage = 0.0f;
    int         g_Mode18_FollowUpRxID = 0;
    const char* g_Mode18_FollowUpMoveClass = "";

    int         g_Mode19_ActiveFNum = 1;
    int         g_Mode19_DefenderBlockMode = 1; // 0: Clean Hit, 1: Auto Block (Block on Gap), 2: Constant Block
    int         g_Mode19_HitCount = 0;
    uint64_t    g_Mode19_Hit1Frame = 0;
    uint64_t    g_Mode19_Hit2Frame = 0;
    uint64_t    g_Mode19_Hit3Frame = 0;
    uint64_t    g_Mode19_Hit4Frame = 0;
    float       g_Mode19_TotalDamage = 0.0f;
    bool        g_Mode19_NDCCancelDetected = false;
    uint64_t    g_Mode19_NDCCancelFrame = 0;

    int         g_Mode20_ActiveFNum = 1;

    int GetMode14RelativeOffset(int fNum) {
        switch (fNum) {
            case 1: return -15; // Airborne Fall (Late hitstun)
            case 2: return -10; // Late Airborne Fall
            case 3: return -5;  // Just before floor contact
            case 4: return 0;   // Exact floor impact (0x460D)
            case 5: return 5;   // Early ground settle
            case 6: return 10;  // Mid ground settle
            case 7: return 15;  // Prone ground entry (0x620A)
            case 8: return 20;  // Lying flat in prone
            default: return 0;
        }
    }

    int GetMode14HoldDuration(int fNum) {
        return g_Mode14_HoldFrames;
    }

    uint64_t g_P1_AttackStartFrame = 0;
    uint64_t g_P2_AttackStartFrame = 0;
    uint64_t g_P1_LastAttackStartFrame = 0;
    uint64_t g_P1_LastAttackEndFrame = 0;
    uint64_t g_P2_LastAttackStartFrame = 0;
    uint64_t g_P2_LastAttackEndFrame = 0;
    int      g_P1_LastScriptFunc = -1;
    int      g_P2_LastScriptFunc = -1;

    int      g_5050_WinnerPlr = 0;
    int      g_5050_LoserPlr = 0;
    uint64_t g_5050_ExpireFrame = 0;

    bool IsActive5050Winner(void* pChar) {
        if (g_5050_WinnerPlr == 0 || g_5050_ExpireFrame == 0) return false;
        if (g_CurrentFrame > g_5050_ExpireFrame) {
            g_5050_WinnerPlr = 0;
            g_5050_LoserPlr = 0;
            g_5050_ExpireFrame = 0;
            return false;
        }
        void* pWinner = (g_5050_WinnerPlr == 1) ? GetP1Char() : GetP2Char();
        int pIdx = (pChar == GetP2Char()) ? 2 : 1;
        return (pChar == pWinner || pIdx == g_5050_WinnerPlr);
    }

    void* GetP1Char() {
        __try {
            void* p = *(void**)0x00e89d98;
            if (!p) p = *(void**)(0x00e89d7c + 0x1c);
            if (!p || (uintptr_t)p < 0x01000000 || p == (void*)0xDEADBEEF) return nullptr;
            // Native UE3 actor handle validation (matches 0x00895B70):
            // Check UObject::Index at offset +0x04 against player handle at +0x20
            uint32_t handle = *(uint32_t*)(0x00e89d7c + 0x20);
            if (handle != 0 && handle != 0xFFFFFFFF) {
                uint32_t objIdx = *(uint32_t*)((uintptr_t)p + 4);
                if (objIdx != handle) return nullptr;
            }
            return p;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return nullptr;
        }
    }

    void* GetP2Char() {
        __try {
            void* p = *(void**)0x00e8aa24;
            if (!p) p = *(void**)(0x00e8aa08 + 0x1c);
            if (!p || (uintptr_t)p < 0x01000000 || p == (void*)0xDEADBEEF) return nullptr;
            // Native UE3 actor handle validation (matches 0x00895B70):
            uint32_t handle = *(uint32_t*)(0x00e8aa08 + 0x20);
            if (handle != 0 && handle != 0xFFFFFFFF) {
                uint32_t objIdx = *(uint32_t*)((uintptr_t)p + 4);
                if (objIdx != handle) return nullptr;
            }
            return p;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return nullptr;
        }
    }

    bool IsGamePaused(); // forward declaration from simulation.cpp

    bool IsMatchActive() {
        void* p1 = GetP1Char();
        void* p2 = GetP2Char();
        return (p1 != nullptr && p2 != nullptr && 
                (uintptr_t)p1 >= 0x01000000 && (uintptr_t)p2 >= 0x01000000);
    }

    bool IsMatchFightActive() {
        if (!IsMatchActive()) return false;
        __try {
            uint32_t phase = *(uint32_t*)0x00E89B74;
            // Phase 8: Active combat round (Health bars visible, fighting)
            // Phase 9: Round KO / Decided (between rounds of match)
            // Phase 10: Next round reset / countdown (between rounds)
            // Phase 11: Finish Him (Fatality window)
            // Phase 12+: Match End / Outro / Rematch Menu / Transition (Health bars gone)
            return (phase >= 8 && phase <= 11);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    static bool s_MatchLoadedUp = false;

    bool IsMatchLoadedUp() {
        if (!IsMatchActive()) {
            s_MatchLoadedUp = false;
            return false;
        }
        __try {
            uint32_t phase = *(uint32_t*)0x00E89B74;
            // When match ends (phase 12+ or 0), immediately disengage Void Mode & Floor Grid
            // so post-match victory outro, rematch menus, and transitions run in clean vanilla mode
            if (phase == 0 || phase >= 12) {
                s_MatchLoadedUp = false;
                return false;
            }
            // Engage when fight is active / health bars appear (phase 8..11)
            if (phase >= 8 && phase <= 11) {
                s_MatchLoadedUp = true;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            s_MatchLoadedUp = false;
            return false;
        }
        return s_MatchLoadedUp;
    }

    bool IsNativeGamePaused() {
        __try {
            int p1 = *(int*)0x00E43054;
            int p2 = *(int*)0x00E43058;
            return (p1 != 0 || p2 != 0);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    bool IsGameOrSimulationPaused() {
        return IsGamePaused() || IsNativeGamePaused();
    }

    typedef int(__cdecl* tIsTagMatch)();
    static tIsTagMatch fnIsTagMatch = (tIsTagMatch)0x008F0D00;

    bool IsCurrentMatchTag() {
        __try {
            if (fnIsTagMatch && fnIsTagMatch()) return true;
            if (*(uint8_t*)0x00E89B84 & 0x20) return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
        return false;
    }

    bool IsAttachedChildActor(void* pParent, void* pCandidate) {
        if (!pParent || !pCandidate || pCandidate == pParent) return false;
        if ((uintptr_t)pCandidate < 0x01000000 || (uintptr_t)pParent < 0x01000000) return false;

        __try {
            // Check UE3 AActor Owner (+0x80 / +0x7C) and Base (+0xA0 / +0x9C)
            void* owner = *(void**)((uintptr_t)pCandidate + 0x80);
            void* base  = *(void**)((uintptr_t)pCandidate + 0xA0);
            if (owner == pParent || base == pParent) {
                return true;
            }

            void* owner2 = *(void**)((uintptr_t)pCandidate + 0x7C);
            void* base2  = *(void**)((uintptr_t)pCandidate + 0x9C);
            if (owner2 == pParent || base2 == pParent) {
                return true;
            }

            // Check Parent Actor's Attached TArray (+0x64 Data, +0x68 Count)
            void** pAttachedData = *(void***)((uintptr_t)pParent + 0x64);
            int attachedCount = *(int*)((uintptr_t)pParent + 0x68);
            if (pAttachedData && attachedCount > 0 && attachedCount < 64) {
                for (int i = 0; i < attachedCount; ++i) {
                    if (pAttachedData[i] == pCandidate) {
                        return true;
                    }
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
        return false;
    }

    const char* GetCurrentStageName() {
        __try {
            // Check primary active stage FString at 0x00ED66A4
            uint32_t ptr = *(uint32_t*)0x00ED66A4;
            if (ptr >= 0x00400000 && ptr < 0x70000000) {
                const char* s = (const char*)ptr;
                if (strncmp(s, "BGND_", 5) == 0) return s;
            }
            // Check secondary stage FString at 0x00E86AD4
            ptr = *(uint32_t*)0x00E86AD4;
            if (ptr >= 0x00400000 && ptr < 0x70000000) {
                const char* s = (const char*)ptr;
                if (strncmp(s, "BGND_", 5) == 0) return s;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
        return "";
    }

    bool IsTrainingMode() {
        void* p1 = GetP1Char();
        void* p2 = GetP2Char();
        if (!p1 || !p2) return false; // Menu, Title, Character Select, or Loading

        // 1. Stage Check: In MK9, the Practice Room (Dojo with Dragon Gong) is "BGND_ChallengeTower"
        // and is exclusively accessible in Practice / Training Mode.
        const char* stage = GetCurrentStageName();
        if (stage && strstr(stage, "ChallengeTower") != nullptr) {
            return true;
        }

        // 2. Global Match Mode & Practice GameInfo Check
        __try {
            uint32_t g_b78 = *(uint32_t*)0x00E89B78;
            uint32_t mFlags = *(uint32_t*)0x00E89B80;
            uint32_t g_bd4 = *(uint32_t*)0x00E89BD4;
            uint32_t vtable = *(uint32_t*)0x00E89B70;

            bool isPracticeFlags = (g_b78 == 2 || g_bd4 == 2 || vtable == 0x00D5E668 || mFlags == 0x00008001 || mFlags == 0x00048001);
            bool isArcadeLadder = (mFlags & 0x00080000) != 0;

            if (isPracticeFlags && !isArcadeLadder) {
                return true;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}

        return false;
    }

    bool IsP2HumanControl() {
        void* p2 = GetP2Char();
        if (!p2) return false;
        // P2 Control Type: 0 = Human, 3 = AI/Dummy
        return (*(uint32_t*)(0x00E8AA08 + 0x95C) == 0);
    }

    bool IsNativeSuperMeterFull() {
        if (!IsTrainingMode()) return false;
        __try {
            // MK9 Native Practice Options Flags:
            // 1. 0x00E8DF64 (Global Practice Options bitfield):
            //    Evaluated in engine meter update function at 0x0089CF0A:
            //      test byte ptr [0x00E8DF64], 0x02
            //      je   0x0089CF1B
            //      movss dword ptr [esi + 0xC14], 1.0f  ; Forces 100% Super Meter
            //    When vanilla Super Meter is FULL: Bit 1 (0x02) is SET (1).
            //    When vanilla Super Meter is NORMAL: Bit 1 (0x02) is CLEARED (0).
            // 2. 0x00E0B8D8: Global full-meter override evaluated at 0x0089CF1B.
            if ((*(uint8_t*)0x00E8DF64 & 0x02) != 0) return true;
            if (*(uint32_t*)0x00E0B8D8 != 0) return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
        return false;
    }

    static bool SafeHasSubstring(const char* str, const char* sub, size_t maxScan = 64) {
        if (!str || (uintptr_t)str < 0x00400000 || (uintptr_t)str >= 0x7FFE0000) return false;
        __try {
            size_t subLen = strlen(sub);
            if (subLen == 0) return false;
            for (size_t i = 0; i < maxScan; ++i) {
                char c = str[i];
                if (c == '\0') return false;
                if ((unsigned char)c < 0x20 || (unsigned char)c > 0x7E) return false;
                if (tolower((unsigned char)c) == tolower((unsigned char)sub[0])) {
                    bool match = true;
                    for (size_t j = 1; j < subLen; ++j) {
                        if (i + j >= maxScan) { match = false; break; }
                        char sc = str[i + j];
                        if (sc == '\0') { match = false; break; }
                        if (tolower((unsigned char)sc) != tolower((unsigned char)sub[j])) {
                            match = false;
                            break;
                        }
                    }
                    if (match) return true;
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
        return false;
    }

    bool IsD2SymbolName(const char* sym) {
        if (!sym || (uintptr_t)sym < 0x00400000 || (uintptr_t)sym >= 0x7FFE0000) return false;
        // STRICT D+2 ONLY: Match explicit attack2/duck/crouch patterns.
        // DO NOT match bare "uppercut" — it catches special moves like Sektor's Telepunch
        // ("teleport_uppercut") and other character-specific special attack animations.
        return SafeHasSubstring(sym, "attack2down") || 
               SafeHasSubstring(sym, "attack2_down") || 
               SafeHasSubstring(sym, "attack_2_down") || 
               SafeHasSubstring(sym, "attack_2down") || 
               SafeHasSubstring(sym, "atk2_down") || 
               SafeHasSubstring(sym, "atk2down") || 
               SafeHasSubstring(sym, "d_attack2") || 
               SafeHasSubstring(sym, "duck_attack2") || 
               SafeHasSubstring(sym, "crouch_attack2") || 
               SafeHasSubstring(sym, "duck_bp") || 
               SafeHasSubstring(sym, "crouch_bp") || 
               SafeHasSubstring(sym, "d_bp") ||
               SafeHasSubstring(sym, "attack2d") ||
               SafeHasSubstring(sym, "atk2d");
    }

    bool IsExecutingD2Script(void* pChar) {
        if (!pChar || (uintptr_t)pChar < 0x01000000) return false;
        __try {
            int actionID = *(int*)((uintptr_t)pChar + 0x2E0);
            if (actionID == 0x20) {
                return true;
            }

            uintptr_t funcPtr = *(uintptr_t*)((uintptr_t)pChar + 0x2F88);
            if (funcPtr > 0x00400000 && funcPtr < 0x7FFE0000 && (funcPtr & 3) == 0) {
                const char* pSym = *(const char**)funcPtr;
                if (IsD2SymbolName(pSym)) {
                    return true;
                }
                const char* pSym2 = *(const char**)(funcPtr + 0x18);
                if (IsD2SymbolName(pSym2)) {
                    return true;
                }
            }

            // Resolve package context from multiple known offsets (+0x02A8, +0x7624, +0x2F84)
            uintptr_t pkg = *(uintptr_t*)((uintptr_t)pChar + 0x02A8);
            if (!pkg || pkg < 0x00400000) pkg = *(uintptr_t*)((uintptr_t)pChar + 0x7624);
            if (!pkg || pkg < 0x00400000) pkg = *(uintptr_t*)((uintptr_t)pChar + 0x2F84);

            if (pkg > 0x00400000 && pkg < 0x7FFE0000 && (pkg & 3) == 0) {
                uint32_t fnCount = *(uint32_t*)(pkg + 0x20);
                uintptr_t pFnTable = *(uintptr_t*)(pkg + 0x8C);
                if (!pFnTable) pFnTable = *(uintptr_t*)(pkg + 0x94);

                uint32_t activeRx = *(uint32_t*)((uintptr_t)pChar + 0x2F8C);

                if (pFnTable > 0x00400000 && pFnTable < 0x7FFE0000 && (pFnTable & 3) == 0 && fnCount > 0 && fnCount < 5000) {
                    for (uint32_t i = 0; i < fnCount; ++i) {
                        uintptr_t pFn = *(uintptr_t*)(pFnTable + i * 4);
                        if (!pFn || pFn < 0x00400000 || pFn >= 0x7FFE0000 || (pFn & 3) != 0) continue;

                        const char* pSym = *(const char**)pFn;
                        if (IsD2SymbolName(pSym)) {
                            uint32_t descRx = *(uint32_t*)(pFn + 0x20);
                            if (activeRx != 0 && (activeRx == descRx || activeRx == (i + 1))) {
                                return true;
                            }
                            if (pFn == funcPtr || *(uint32_t*)(pFn + 0x04) == (uint32_t)funcPtr) {
                                return true;
                            }
                        }
                    }
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
        return false;
    }

    static inline bool IsExtendedKey(WORD vk) {
        return (vk == VK_UP || vk == VK_DOWN || vk == VK_LEFT || vk == VK_RIGHT ||
                vk == VK_INSERT || vk == VK_DELETE || vk == VK_HOME || vk == VK_END ||
                vk == VK_PRIOR || vk == VK_NEXT || vk == VK_RCONTROL || vk == VK_RSHIFT ||
                vk == VK_RMENU || vk == VK_DIVIDE);
    }

#include "input_isolation.hpp"

    static bool s_SimulatedKeysHeld[256] = { false };
    static HWND s_hCachedTargetWindow = NULL;

    HWND GetTargetWindow() {
        if (s_hCachedTargetWindow && IsWindow(s_hCachedTargetWindow)) return s_hCachedTargetWindow;
        __try {
            HWND h = *(HWND*)0x00E53BF4;
            if (h && IsWindow(h)) {
                s_hCachedTargetWindow = h;
                return h;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
        s_hCachedTargetWindow = FindWindowA("MK9", NULL);
        if (!s_hCachedTargetWindow) {
            s_hCachedTargetWindow = GetActiveWindow();
        }
        return s_hCachedTargetWindow;
    }

    bool IsGameWindowFocused() {
        HWND hTarget = GetTargetWindow();
        if (!hTarget || !IsWindow(hTarget)) return true; // Fallback if handle not yet captured
        HWND hFg = GetForegroundWindow();
        if (!hFg) return false;
        return (hFg == hTarget || IsChild(hTarget, hFg));
    }

    void SimulateKeyDown(WORD vk) {
        if (vk < 256) s_SimulatedKeysHeld[vk] = true;
        INPUT input = { 0 };
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = vk;
        input.ki.wScan = (WORD)MapVirtualKeyA(vk, MAPVK_VK_TO_VSC);
        input.ki.dwFlags = IsExtendedKey(vk) ? KEYEVENTF_EXTENDEDKEY : 0;
        SendInput(1, &input, sizeof(INPUT));

        HWND hWnd = GetTargetWindow();
        if (hWnd && IsWindow(hWnd)) {
            LPARAM lParam = 1 | ((LPARAM)input.ki.wScan << 16) | (IsExtendedKey(vk) ? (1 << 24) : 0);
            PostMessageA(hWnd, WM_KEYDOWN, (WPARAM)vk, lParam);
        }
    }

    void SimulateKeyUp(WORD vk) {
        if (vk < 256) s_SimulatedKeysHeld[vk] = false;
        INPUT input = { 0 };
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = vk;
        input.ki.wScan = (WORD)MapVirtualKeyA(vk, MAPVK_VK_TO_VSC);
        input.ki.dwFlags = KEYEVENTF_KEYUP | (IsExtendedKey(vk) ? KEYEVENTF_EXTENDEDKEY : 0);
        SendInput(1, &input, sizeof(INPUT));

        HWND hWnd = GetTargetWindow();
        if (hWnd && IsWindow(hWnd)) {
            LPARAM lParam = 1 | ((LPARAM)input.ki.wScan << 16) | (IsExtendedKey(vk) ? (1 << 24) : 0) | (1 << 30) | (1 << 31);
            PostMessageA(hWnd, WM_KEYUP, (WPARAM)vk, lParam);
        }
    }

    void ReleaseAllSimulationKeys() {
        for (int i = 0; i < 256; i++) {
            if (s_SimulatedKeysHeld[i]) {
                SimulateKeyUp((WORD)i);
                s_SimulatedKeysHeld[i] = false;
            }
        }
    }

    bool IsSimulatedKeyHeld(WORD vk) {
        return (vk < 256) ? s_SimulatedKeysHeld[vk] : false;
    }

    bool HasActiveDiagnosticGamepad(DWORD dwUserIndex) {
        if (InputIsolation::HasActiveTestModeGamepad(dwUserIndex)) return true;
        extern uint64_t g_SimTriggerFrame;
        if (g_SimTriggerFrame != 0) {
            int p1Port = GetPlayerXInputPort(1);
            int p2Port = GetPlayerXInputPort(2);
            if ((int)dwUserIndex == p1Port || (int)dwUserIndex == p2Port) {
                return true;
            }
        }
        return false;
    }

    bool GetActiveDiagnosticGamepad(DWORD dwUserIndex, XINPUT_STATE* pState) {
        if (!pState) return false;

        // 1. Direct Mode 12 Test Bot Override (if active)
        if (InputIsolation::GetActiveTestModeGamepad(dwUserIndex, pState)) {
            return true;
        }

        // 2. Check if any Diagnostic Macro (Modes 1..16) is active
        extern uint64_t g_SimTriggerFrame;
        if (g_SimTriggerFrame == 0) return false;

        int p1Port = GetPlayerXInputPort(1);
        int p2Port = GetPlayerXInputPort(2);

        if ((int)dwUserIndex == p1Port) {
            memset(pState, 0, sizeof(XINPUT_STATE));
            pState->dwPacketNumber = (DWORD)g_CurrentFrame;

            // During pre-trigger countdown (15 frames), feed clean neutral to quarantine hardware
            if (g_CurrentFrame < g_SimTriggerFrame) {
                return true;
            }

            // P1 Directions
            if (IsSimulatedKeyHeld('W')) { pState->Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_UP; pState->Gamepad.sThumbLY = 32767; }
            if (IsSimulatedKeyHeld('S')) { pState->Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_DOWN; pState->Gamepad.sThumbLY = -32767; }
            if (IsSimulatedKeyHeld('A')) { pState->Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_LEFT; pState->Gamepad.sThumbLX = -32767; }
            if (IsSimulatedKeyHeld('D')) { pState->Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_RIGHT; pState->Gamepad.sThumbLX = 32767; }

            // P1 Attacks & Modifiers (1=FP/X, 2=BP/Y, 3=FK/A, 4=BK/B)
            if (IsSimulatedKeyHeld('T')) { pState->Gamepad.wButtons |= XINPUT_GAMEPAD_X; } // FP (1)
            if (IsSimulatedKeyHeld('U') || IsSimulatedKeyHeld('Y') || IsSimulatedKeyHeld('I')) { pState->Gamepad.wButtons |= XINPUT_GAMEPAD_Y; } // BP (2)
            if (IsSimulatedKeyHeld('G')) { pState->Gamepad.wButtons |= XINPUT_GAMEPAD_A; } // FK (3)
            if (IsSimulatedKeyHeld('H') || IsSimulatedKeyHeld('J')) { pState->Gamepad.wButtons |= XINPUT_GAMEPAD_B; } // BK (4)
            if (IsSimulatedKeyHeld(VK_SPACE)) { pState->Gamepad.bRightTrigger = 255; } // Block (RT)
            if (IsSimulatedKeyHeld(VK_LSHIFT)) { pState->Gamepad.wButtons |= XINPUT_GAMEPAD_LEFT_SHOULDER; } // Tag
            if (IsSimulatedKeyHeld(VK_LCONTROL) || IsSimulatedKeyHeld('L')) { pState->Gamepad.bLeftTrigger = 255; } // Stance (LT)

            return true;
        } else if ((int)dwUserIndex == p2Port) {
            memset(pState, 0, sizeof(XINPUT_STATE));
            pState->dwPacketNumber = (DWORD)g_CurrentFrame;

            // During pre-trigger countdown (15 frames), feed clean neutral to quarantine hardware
            if (g_CurrentFrame < g_SimTriggerFrame) {
                return true;
            }

            // P2 Directions
            if (IsSimulatedKeyHeld(VK_UP)) { pState->Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_UP; pState->Gamepad.sThumbLY = 32767; }
            if (IsSimulatedKeyHeld(VK_DOWN)) { pState->Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_DOWN; pState->Gamepad.sThumbLY = -32767; }
            if (IsSimulatedKeyHeld(VK_LEFT)) { pState->Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_LEFT; pState->Gamepad.sThumbLX = -32767; }
            if (IsSimulatedKeyHeld(VK_RIGHT)) { pState->Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_RIGHT; pState->Gamepad.sThumbLX = 32767; }

            // P2 Attacks & Modifiers (1=FP/X, 2=BP/Y, 3=FK/A, 4=BK/B)
            if (IsSimulatedKeyHeld(VK_NUMPAD7)) { pState->Gamepad.wButtons |= XINPUT_GAMEPAD_X; } // FP (1)
            if (IsSimulatedKeyHeld(VK_NUMPAD9) || IsSimulatedKeyHeld(VK_NUMPAD8)) { pState->Gamepad.wButtons |= XINPUT_GAMEPAD_Y; } // BP (2)
            if (IsSimulatedKeyHeld(VK_NUMPAD4) || IsSimulatedKeyHeld(VK_NUMPAD1)) { pState->Gamepad.wButtons |= XINPUT_GAMEPAD_A; } // FK (3)
            if (IsSimulatedKeyHeld(VK_NUMPAD6) || IsSimulatedKeyHeld(VK_NUMPAD3) || IsSimulatedKeyHeld(VK_NUMPAD5) || IsSimulatedKeyHeld(VK_NUMPAD2)) { pState->Gamepad.wButtons |= XINPUT_GAMEPAD_B; } // BK (4)
            if (IsSimulatedKeyHeld(VK_NUMPAD0) || IsSimulatedKeyHeld(VK_DECIMAL)) { pState->Gamepad.bRightTrigger = 255; } // Block (RT)
            if (IsSimulatedKeyHeld(VK_RSHIFT)) { pState->Gamepad.wButtons |= XINPUT_GAMEPAD_LEFT_SHOULDER; } // Tag
            if (IsSimulatedKeyHeld(VK_ADD) || IsSimulatedKeyHeld(VK_RCONTROL)) { pState->Gamepad.bLeftTrigger = 255; } // Stance (LT)

            return true;
        }

        return false;
    }

    void LogTrace(const char* fmt, ...) {
        if (!g_LoggingEnabled) return;

        char buf[1024];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);

        MK9Overlay::AppendLog(buf, g_CurrentFrame);

        static FILE* s_LogFile = nullptr;
        static FILE* s_LogFileRuntime = nullptr;
        static FILE* s_LogFileMirror = nullptr;
        static bool s_LogInit = false;
        static uint32_t s_LinesSinceFlush = 0;
        if (!s_LogInit) {
            s_LogInit = true;
            s_LogFile = _fsopen("MK9_TRADE_RUNTIME.LOG", "a+", _SH_DENYNO);
            s_LogFileRuntime = _fsopen("runtime.log", "a+", _SH_DENYNO);
            s_LogFileMirror = _fsopen("C:\\Users\\casan\\Documents\\github\\MK9_RE\\MK9_LIVE_RUNTIME.LOG", "a+", _SH_DENYNO);
        }
        if (s_LogFile) {
            fprintf(s_LogFile, "[F%llu] %s\n", g_CurrentFrame, buf);
        }
        if (s_LogFileRuntime) {
            fprintf(s_LogFileRuntime, "[F%llu] %s\n", g_CurrentFrame, buf);
            fflush(s_LogFileRuntime); // Immediate flush guarantees runtime.log is always 100% up-to-date
        }
        if (s_LogFileMirror) {
            fprintf(s_LogFileMirror, "[F%llu] %s\n", g_CurrentFrame, buf);
        }

        // Periodic flush every 64 lines for file mirrors
        if (++s_LinesSinceFlush >= 64) {
            s_LinesSinceFlush = 0;
            if (s_LogFile) fflush(s_LogFile);
            if (s_LogFileMirror) fflush(s_LogFileMirror);
        }
    }

    LONG WINAPI MK9CrashHandler(PEXCEPTION_POINTERS pExceptionInfo) {
        PEXCEPTION_RECORD pRec = pExceptionInfo->ExceptionRecord;
        PCONTEXT pCtx = pExceptionInfo->ContextRecord;

        auto ResolveAddress = [](uintptr_t addr, char* outBuf, size_t outBufSize) {
            if (addr >= 0x00400000 && addr <= 0x01500000) {
                snprintf(outBuf, outBufSize, "MKKE.exe + 0x%08X", addr - 0x00400000);
                return;
            }
            HMODULE hMod = NULL;
            if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)addr, &hMod) && hMod) {
                char modPath[MAX_PATH] = { 0 };
                GetModuleFileNameA(hMod, modPath, sizeof(modPath));
                char* pBase = strrchr(modPath, '\\');
                if (pBase) pBase++; else pBase = modPath;
                snprintf(outBuf, outBufSize, "%s + 0x%08X (Base: 0x%08X)", pBase, addr - (uintptr_t)hMod, (uintptr_t)hMod);
            } else {
                snprintf(outBuf, outBufSize, "Unknown @ 0x%08X", addr);
            }
        };

        char crashLocStr[256] = { 0 };
        ResolveAddress((uintptr_t)pRec->ExceptionAddress, crashLocStr, sizeof(crashLocStr));

        LogTrace("====================================================================");
        LogTrace(" [CRASH] Exception: 0x%08X @ %s", pRec->ExceptionCode, crashLocStr);
        
        if (pRec->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && pRec->NumberParameters >= 2) {
            LogTrace(" Access Violation: %s @ 0x%08X",
                     pRec->ExceptionInformation[0] ? "WRITE" : "READ",
                     (uint32_t)pRec->ExceptionInformation[1]);
        }
        LogTrace(" Registers: EAX=0x%08X EBX=0x%08X ECX=0x%08X EDX=0x%08X", pCtx->Eax, pCtx->Ebx, pCtx->Ecx, pCtx->Edx);
        LogTrace("            ESI=0x%08X EDI=0x%08X EBP=0x%08X ESP=0x%08X EIP=0x%08X", pCtx->Esi, pCtx->Edi, pCtx->Ebp, pCtx->Esp, pCtx->Eip);
        LogTrace("====================================================================");

        // Dump to MK9_CRASH_DUMP.LOG
        FILE* fDump = nullptr;
        fopen_s(&fDump, "MK9_CRASH_DUMP.LOG", "a+");
        if (!fDump) {
            fopen_s(&fDump, "V:\\SteamLibrary\\steamapps\\common\\MortalKombat_KompleteEdition\\DiscContentPC\\MK9_CRASH_DUMP.LOG", "a+");
        }
        if (fDump) {
            fprintf(fDump, "====================================================================\n");
            fprintf(fDump, "Exception: 0x%08X @ 0x%08X (%s)\n",
                    pRec->ExceptionCode, (uint32_t)pRec->ExceptionAddress, crashLocStr);
            if (pRec->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && pRec->NumberParameters >= 2) {
                fprintf(fDump, "Access Violation: %s @ 0x%08X\n",
                        pRec->ExceptionInformation[0] ? "WRITE" : "READ",
                        (uint32_t)pRec->ExceptionInformation[1]);
            }
            fprintf(fDump, "Thread ID: %lu | Simulation Frame: %llu\n", GetCurrentThreadId(), g_CurrentFrame);
            fprintf(fDump, "Registers:\n");
            fprintf(fDump, "  EAX: 0x%08X  EBX: 0x%08X  ECX: 0x%08X  EDX: 0x%08X\n", pCtx->Eax, pCtx->Ebx, pCtx->Ecx, pCtx->Edx);
            fprintf(fDump, "  ESI: 0x%08X  EDI: 0x%08X  EBP: 0x%08X  ESP: 0x%08X\n", pCtx->Esi, pCtx->Edi, pCtx->Ebp, pCtx->Esp);
            fprintf(fDump, "  EIP: 0x%08X  EFLAGS: 0x%08X\n", pCtx->Eip, pCtx->EFlags);

            // Code bytes at EIP
            __try {
                uint8_t* pCode = (uint8_t*)pCtx->Eip;
                fprintf(fDump, "Code at EIP: ");
                for (int i = 0; i < 16; i++) {
                    fprintf(fDump, "%02X ", pCode[i]);
                }
                fprintf(fDump, "\n");
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                fprintf(fDump, "Code at EIP: [Inaccessible]\n");
            }

            // Stack Backtrace (16 frames)
            fprintf(fDump, "Call Stack Backtrace:\n");
            void* stackFrames[16] = { 0 };
            USHORT frameCount = RtlCaptureStackBackTrace(0, 16, stackFrames, NULL);
            for (USHORT i = 0; i < frameCount; i++) {
                uintptr_t addr = (uintptr_t)stackFrames[i];
                char frameLoc[256] = { 0 };
                ResolveAddress(addr, frameLoc, sizeof(frameLoc));
                fprintf(fDump, "  [#%02d] 0x%08X : %s\n", i, addr, frameLoc);
            }
            fprintf(fDump, "====================================================================\n\n");
            fflush(fDump);
            fclose(fDump);
        }

        // Force immediate log flush so crash trace is 100% preserved
        FILE* fFlush0 = _fsopen("runtime.log", "a+", _SH_DENYNO);
        if (fFlush0) { fflush(fFlush0); fclose(fFlush0); }
        FILE* fFlush1 = _fsopen("MK9_TRADE_RUNTIME.LOG", "a+", _SH_DENYNO);
        if (fFlush1) { fflush(fFlush1); fclose(fFlush1); }
        FILE* fFlush2 = _fsopen("C:\\Users\\casan\\Documents\\github\\MK9_RE\\MK9_LIVE_RUNTIME.LOG", "a+", _SH_DENYNO);
        if (fFlush2) { fflush(fFlush2); fclose(fFlush2); }

        return EXCEPTION_CONTINUE_SEARCH;
    }

    void ApplyLateralPosition(void* pChar, float lateralY) {
        if (!pChar || (uintptr_t)pChar < 0x01000000) return;
        __try {
            // 1. Authoritative 2D game simulation coordinate along the fight plane
            *(float*)((uintptr_t)pChar + 0x1E8) = lateralY;

            // 2. UE3 AActor coordinate synchronization
            uintptr_t pActor = *(uintptr_t*)((uintptr_t)pChar + 8);
            if (pActor >= 0x00400000 && pActor < 0x7FFE0000) {
                *(float*)(pActor + 0xE0) = lateralY; // 0xE0 is Y (Lateral Stage Position)

                // If native Practice Reset is currently in progress (0x00E8DF64 + 0x40 != 0),
                // DO NOT invoke SetLocation or octree manipulation routines during teardown/recreation!
                bool isResetting = false;
                if (!IsBadReadPtr((void*)0x00E8DF64, 0x48)) {
                    isResetting = (*(uint32_t*)(0x00E8DF64 + 0x40) != 0);
                }

                if (!isResetting) {
                    // Safety check before invoking native AActor::SetLocation (0x00433580).
                    // SetLocation calls 0x007898C0 which traverses the octree elements array ([pOctree + 0x68]).
                    // If the component is uninitialized, detached, or octree array is null, calling it causes
                    // Exception 0xC0000005 Access Violation at 0x007898D7 (cmp [edx], ecx).
                    uintptr_t pComp = *(uintptr_t*)(pActor + 8);
                    if (pComp >= 0x00400000 && pComp < 0x7FFE0000 && !IsBadReadPtr((void*)pComp, 0x70)) {
                        uintptr_t pOctree = *(uintptr_t*)(pComp + 0x60);
                        if (pOctree >= 0x00400000 && pOctree < 0x7FFE0000 && !IsBadReadPtr((void*)pOctree, 0x70)) {
                            uintptr_t pElements = *(uintptr_t*)(pOctree + 0x68);
                            int count = *(int*)(pOctree + 0x6C);
                            if (count > 0 && pElements >= 0x00400000 && pElements < 0x7FFE0000 && !IsBadReadPtr((void*)pElements, 4)) {
                                float newLoc[3] = {
                                    *(float*)(pActor + 0xDC), // Depth X (normally 0.0f)
                                    lateralY,                 // Lateral Y
                                    *(float*)(pActor + 0xE4)  // Height Z
                                };
                                typedef void (__thiscall *tSetLocation)(void* thisPtr, const float* loc);
                                ((tSetLocation)0x00433580)((void*)pActor, newLoc);
                            }
                        }
                    }
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
}

