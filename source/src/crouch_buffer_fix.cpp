#include "crouch_buffer_fix.hpp"
#include "engine_common.hpp"
#include "simulation.hpp"
#include <windows.h>
#include <xinput.h>
#include <cstdio>

namespace MK9TradeFix {
    namespace CrouchBufferFix {

        static bool g_CrouchBufferFixEnabled = true;

        void SetEnabled(bool enabled) {
            g_CrouchBufferFixEnabled = enabled;
            LogTrace(">>> [CROUCH BUFFER FIX] Subsystem: %s <<<", enabled ? "ENABLED" : "DISABLED");
        }

        bool IsEnabled() {
            return g_CrouchBufferFixEnabled;
        }

        void Toggle() {
            SetEnabled(!g_CrouchBufferFixEnabled);
        }

        // Dynamically resolve XInputGetState to avoid hard dependency on specific DLL version
        typedef DWORD (WINAPI *tXInputGetState)(DWORD dwUserIndex, XINPUT_STATE* pState);
        static tXInputGetState pfnXInputGetState = nullptr;
        static bool g_XInputInitialized = false;

        static void EnsureXInputLoaded() {
            if (g_XInputInitialized) return;
            g_XInputInitialized = true;

            const char* dlls[] = { "xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll" };
            for (int i = 0; i < 3; ++i) {
                HMODULE h = LoadLibraryA(dlls[i]);
                if (h) {
                    pfnXInputGetState = (tXInputGetState)GetProcAddress(h, "XInputGetState");
                    if (pfnXInputGetState) break;
                }
            }
        }

        // ========================================================================
        // Clean Directional Buffer Timeout Resolver (Mode 9 F8 Fix):
        // When Down is held for > 8 frames, NetherRealm's directional lookback buffer
        // search times out, causing MatchAction to send button == 0x80000000 (Neutral).
        // If the fighter is crouched or holding Down, RemapCrouchAttackToken remaps
        // the button to the exact native crouch move token:
        //   FP (0x8000000F) -> 0x80000023 (Crouch Jab / Low Poke D+1)
        //   BP (0x80000010) -> 0x8000003A (Uppercut D+2)
        //   FK (0x80000011) -> 0x8000003A (Low Kick D+3)
        //   BK (0x80000012) -> 0x80000029 (Sweep D+4)
        //
        // ZERO SIDE EFFECTS:
        // Does NOT hook 0x008953B0 (IsCharacterDucking).
        // Does NOT touch bone matrices, skeletal mirroring, or stance flags.
        // The game's native animation graph runs 100% naturally.
        // ========================================================================
        uint32_t RemapCrouchAttackToken(void* pChar, uint32_t dirMotion, uint32_t button) {
            if (!g_CrouchBufferFixEnabled || !pChar || (uintptr_t)pChar < 0x01000000) {
                return button;
            }

            // Only intervene when the engine evaluated the button as Neutral (0x80000000)
            // and the candidate attack is one of the 4 standard attack buttons
            if (button != 0x80000000) {
                return button;
            }
            if (dirMotion < 0x8000000F || dirMotion > 0x80000012) {
                return button;
            }

            bool isCrouched = false;
            bool isDownHeld = false;

            __try {
                // 1. Check active character posture / action state:
                // 0x0100: Duck startup / transition (ACT_DUCK_START)
                // 0x0101: Active ducking / crouching idle (ACT_DUCK)
                // 0x0102: Duck blocking (ACT_DUCK_BLOCK)
                // 0x0103: Duck hit stun (ACT_DUCK_HIT)
                // 0x5300: Duck attack (ACT_UPPERCUT)
                // 0x6200..0x620B: Ducking attacks (ACT_DUCK_ATTACK)
                uint32_t act = *(uint32_t*)((uintptr_t)pChar + 0x1D4);
                if (act == 0x0100 || act == 0x0101 || act == 0x0102 || act == 0x0103 ||
                    act == 0x5300 || (act >= 0x6200 && act <= 0x620B)) {
                    isCrouched = true;
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {}

            // 2. Check physical Down input strictly for THIS player (Keyboard or Gamepad):
            if (!isCrouched) {
                int pNum = GetPlayerNumber(pChar);
                if (pNum == 1) {
                    isDownHeld = ((GetAsyncKeyState('S') & 0x8000) != 0) || IsSimulatedKeyHeld('S');
                    if (!isDownHeld) {
                        XINPUT_STATE xiState;
                        DWORD p1Port = (DWORD)GetPlayerXInputPort(1);
                        bool gotState = GetActiveDiagnosticGamepad(p1Port, &xiState);
                        if (!gotState) {
                            EnsureXInputLoaded();
                            if (pfnXInputGetState && pfnXInputGetState(p1Port, &xiState) == ERROR_SUCCESS) {
                                gotState = true;
                            }
                        }
                        if (gotState) {
                            if ((xiState.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN) != 0 ||
                                xiState.Gamepad.sThumbLY < -15000) {
                                isDownHeld = true;
                            }
                        }
                    }
                } else if (pNum == 2) {
                    isDownHeld = ((GetAsyncKeyState(VK_DOWN) & 0x8000) != 0) || IsSimulatedKeyHeld(VK_DOWN);
                    if (!isDownHeld) {
                        XINPUT_STATE xiState;
                        DWORD p2Port = (DWORD)GetPlayerXInputPort(2);
                        bool gotState = GetActiveDiagnosticGamepad(p2Port, &xiState);
                        if (!gotState) {
                            EnsureXInputLoaded();
                            if (pfnXInputGetState && pfnXInputGetState(p2Port, &xiState) == ERROR_SUCCESS) {
                                gotState = true;
                            }
                        }
                        if (gotState) {
                            if ((xiState.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN) != 0 ||
                                xiState.Gamepad.sThumbLY < -15000) {
                                isDownHeld = true;
                            }
                        }
                    }
                }
            }

            if (isCrouched || isDownHeld) {
                // Native NetherRealm Down Directional Token:
                // When Down is recognized in the buffer, 0x893200 returns 0x80000004.
                // In MoveTable, Down attacks for all 4 buttons use token 0x....0004:
                //   FP (0x8000000F) + 0x80000004 -> 0x000F0004 (D+1, Move 80, act=0x5300)
                //   BP (0x80000010) + 0x80000004 -> 0x00100004 (D+2, Move 84, act=0x5300)
                //   FK (0x80000011) + 0x80000004 -> 0x00110004 (D+3, Move 88, act=0x5300)
                //   BK (0x80000012) + 0x80000004 -> 0x00120004 (D+4, Move 92, act=0x5300)
                return 0x80000004;
            }

            return button;
        }

    } // namespace CrouchBufferFix
} // namespace MK9TradeFix
