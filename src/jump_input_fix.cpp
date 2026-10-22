#include "jump_input_fix.hpp"
#include "engine_common.hpp"
#include "simulation.hpp"
#include "minhook/include/MinHook.h"
#include <windows.h>
#include <cstdio>

namespace MK9TradeFix {
    namespace JumpInputFix {

        static bool g_JumpInputFixEnabled = true;

        // --------------------------------------------------------------------
        // Hook 1: CheckTokenHoldDuration (0x00449400)
        // Prototype: int __thiscall CheckTokenHoldDuration(void* pChar, uint32_t token)
        // NetherRealm checks if (cur_frame - start_frame) >= threshold.
        // For Up (0x80000003) and UpDiag (0x8000002D), threshold is hardcoded to 3.
        // Tapping Up for 1f..3f causes elapsed < 3, and releasing clears start_frame,
        // which eats the jump input entirely. When enabled, we return 1 immediately.
        // --------------------------------------------------------------------
        typedef int (__fastcall *tCheckTokenHoldDuration)(void* pChar, void* edxUnused, uint32_t token);
        static tCheckTokenHoldDuration oCheckTokenHoldDuration = nullptr;

        static int __fastcall hkCheckTokenHoldDuration(void* pChar, void* edxUnused, uint32_t token) {
            if (g_JumpInputFixEnabled && (token == 0x80000003 || token == 0x8000002D)) {
                return 1; // Instant takeoff threshold satisfied!
            }
            if (oCheckTokenHoldDuration) {
                return oCheckTokenHoldDuration(pChar, edxUnused, token);
            }
            return 1;
        }

        // --------------------------------------------------------------------
        // Hook 2: InitTokenHoldDefaults (0x0044C580)
        // Prototype: void __thiscall InitTokenHoldDefaults(void* pCharOffset218)
        // Initializes [ecx + 8] = 3 (the hardcoded Up hold threshold).
        // When enabled, we override this to 0.
        // --------------------------------------------------------------------
        typedef void (__fastcall *tInitTokenHoldDefaults)(void* pCharOffset218, void* edxUnused);
        static tInitTokenHoldDefaults oInitTokenHoldDefaults = nullptr;

        static void __fastcall hkInitTokenHoldDefaults(void* pCharOffset218, void* edxUnused) {
            if (oInitTokenHoldDefaults) {
                oInitTokenHoldDefaults(pCharOffset218, edxUnused);
            }
            if (g_JumpInputFixEnabled && pCharOffset218 && (uintptr_t)pCharOffset218 > 0x01000000) {
                __try {
                    *(uint32_t*)((uintptr_t)pCharOffset218 + 8) = 0;
                } __except (EXCEPTION_EXECUTE_HANDLER) {}
            }
        }

        // --------------------------------------------------------------------
        // Hook 3: SetTokenHoldThreshold (0x00449310)
        // Prototype: void __thiscall SetTokenHoldThreshold(void* pChar, uint32_t token, uint32_t threshold)
        // Stores the hold threshold in pChar + 0x3038 table.
        // When enabled, force threshold = 0 for Up and UpDiag.
        // --------------------------------------------------------------------
        typedef void (__fastcall *tSetTokenHoldThreshold)(void* pChar, void* edxUnused, uint32_t token, uint32_t threshold);
        static tSetTokenHoldThreshold oSetTokenHoldThreshold = nullptr;

        static void __fastcall hkSetTokenHoldThreshold(void* pChar, void* edxUnused, uint32_t token, uint32_t threshold) {
            if (g_JumpInputFixEnabled && (token == 0x80000003 || token == 0x8000002D)) {
                threshold = 0;
            }
            if (oSetTokenHoldThreshold) {
                oSetTokenHoldThreshold(pChar, edxUnused, token, threshold);
            }
        }

        void OnFighterTick(void* pChar) {
            if (!pChar || (uintptr_t)pChar < 0x01000000) return;
            __try {
                uint32_t targetThreshold = g_JumpInputFixEnabled ? 0 : 3;
                *(uint32_t*)((uintptr_t)pChar + 0x220) = targetThreshold;

                uint32_t cnt = *(uint32_t*)((uintptr_t)pChar + 0x3038);
                if (cnt <= 10) {
                    for (uint32_t i = 0; i < cnt; i++) {
                        uintptr_t entry = (uintptr_t)pChar + 0x303C + i * 12;
                        uint32_t token = *(uint32_t*)entry;
                        if (token == 0x80000003 || token == 0x8000002D) {
                            *(uint32_t*)(entry + 8) = targetThreshold;
                        }
                    }
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
        }

        void SetEnabled(bool enabled) {
            g_JumpInputFixEnabled = enabled;
            LogTrace(">>> [JUMP INPUT FIX] Subsystem: %s <<<", enabled ? "ENABLED (Instant 1f-3f Tap Jump)" : "DISABLED (Vanilla 4f Hold Required)");

            // Live synchronization: update active fighters immediately
            void* p1 = GetP1Char();
            void* p2 = GetP2Char();
            if (p1) OnFighterTick(p1);
            if (p2) OnFighterTick(p2);
        }

        bool IsEnabled() {
            return g_JumpInputFixEnabled;
        }

        void Toggle() {
            SetEnabled(!g_JumpInputFixEnabled);
        }

        void InstallHooks() {
            if (MH_CreateHook((void*)0x00449400, (void*)hkCheckTokenHoldDuration, (void**)&oCheckTokenHoldDuration) == MH_OK) {
                MH_EnableHook((void*)0x00449400);
                LogTrace("[+] JumpInputFix: Hooked CheckTokenHoldDuration (0x00449400)");
            } else {
                LogTrace("[-] JumpInputFix: FAILED to hook CheckTokenHoldDuration (0x00449400)");
            }

            if (MH_CreateHook((void*)0x0044C580, (void*)hkInitTokenHoldDefaults, (void**)&oInitTokenHoldDefaults) == MH_OK) {
                MH_EnableHook((void*)0x0044C580);
                LogTrace("[+] JumpInputFix: Hooked InitTokenHoldDefaults (0x0044C580)");
            } else {
                LogTrace("[-] JumpInputFix: FAILED to hook InitTokenHoldDefaults (0x0044C580)");
            }

            if (MH_CreateHook((void*)0x00449310, (void*)hkSetTokenHoldThreshold, (void**)&oSetTokenHoldThreshold) == MH_OK) {
                MH_EnableHook((void*)0x00449310);
                LogTrace("[+] JumpInputFix: Hooked SetTokenHoldThreshold (0x00449310)");
            } else {
                LogTrace("[-] JumpInputFix: FAILED to hook SetTokenHoldThreshold (0x00449310)");
            }
        }
    }
}
