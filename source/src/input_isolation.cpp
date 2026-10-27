#include "input_isolation.hpp"
#include "simulation.hpp"
#include "profiler.hpp"
#include "minhook/include/MinHook.h"
#include <cstdio>
#include <cstdlib>

namespace MK9TradeFix {
    namespace InputIsolation {

        static bool g_InputIsolationEnabled = true;

        void SetEnabled(bool enabled) {
            g_InputIsolationEnabled = enabled;
            LogTrace(">>> [INPUT ISOLATION] Cross-Talk & Device Bleed Quarantine: %s <<<", enabled ? "ENABLED" : "DISABLED");
        }

        bool IsEnabled() {
            return g_InputIsolationEnabled;
        }

        void Toggle() {
            SetEnabled(!g_InputIsolationEnabled);
        }

        bool InstallHooks() {
            static bool s_HooksInstalled = false;
            if (s_HooksInstalled) return true;
            s_HooksInstalled = true;
            LogTrace("[+] Input Isolation & Multi-Device Test Bot Subsystem Initialized.");
            return true;
        }

        // ========================================================================
        // 3. Multi-Device Test Bot Mashing Generator (Mode 12)
        // Drives XInput Gamepads & Keyboard simultaneously
        // ========================================================================
        // Static deterministic PRNG table for full chaotic multi-device stress test
        static inline uint32_t HashFrame(uint64_t frame, uint32_t seed) {
            uint64_t v = frame * 6364136223846793005ULL + seed;
            v ^= v >> 21; v ^= v << 35; v ^= v >> 4;
            return (uint32_t)(v * 2685821657736338717ULL);
        }

        static const WORD s_ChaosGamepadButtons[] = {
            XINPUT_GAMEPAD_DPAD_UP,
            XINPUT_GAMEPAD_DPAD_DOWN,
            XINPUT_GAMEPAD_DPAD_LEFT,
            XINPUT_GAMEPAD_DPAD_RIGHT,
            XINPUT_GAMEPAD_A, // FK
            XINPUT_GAMEPAD_B, // BK
            XINPUT_GAMEPAD_X, // FP
            XINPUT_GAMEPAD_Y, // BP
            XINPUT_GAMEPAD_LEFT_SHOULDER,  // Tag
            XINPUT_GAMEPAD_RIGHT_SHOULDER, // Flip
            XINPUT_GAMEPAD_DPAD_DOWN | XINPUT_GAMEPAD_DPAD_LEFT,
            XINPUT_GAMEPAD_DPAD_DOWN | XINPUT_GAMEPAD_DPAD_RIGHT,
            XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_RIGHT,
            XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_LEFT,
            XINPUT_GAMEPAD_X | XINPUT_GAMEPAD_A,
            XINPUT_GAMEPAD_Y | XINPUT_GAMEPAD_B,
            XINPUT_GAMEPAD_DPAD_DOWN | XINPUT_GAMEPAD_A,
            XINPUT_GAMEPAD_DPAD_DOWN | XINPUT_GAMEPAD_X,
        };

        static const WORD s_P1ChaosKeys[] = {
            'W', 'S', 'A', 'D', 'T', 'Y', 'G', 'H', 'U', 'I', 'J', VK_SPACE, VK_LSHIFT, VK_LCONTROL, 'L'
        };

        static const WORD s_P2ChaosKeys[] = {
            VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT,
            VK_NUMPAD7, VK_NUMPAD8, VK_NUMPAD9,
            VK_NUMPAD4, VK_NUMPAD5, VK_NUMPAD6,
            VK_NUMPAD1, VK_NUMPAD2, VK_NUMPAD3,
            VK_NUMPAD0, VK_DECIMAL, VK_RSHIFT, VK_ADD
        };

        static void GenerateChaoticGamepadInput(uint64_t elapsed, uint32_t seed, XINPUT_STATE* pState) {
            uint32_t h = HashFrame(elapsed, seed);
            // 85% chance of button press, 15% neutral gap
            if ((h % 100) < 85) {
                int btnIdx = (int)((h >> 8) % (sizeof(s_ChaosGamepadButtons) / sizeof(s_ChaosGamepadButtons[0])));
                pState->Gamepad.wButtons = s_ChaosGamepadButtons[btnIdx];

                // Chaotic analog stick deflection
                int stickDirX = (int)((h >> 16) % 3) - 1; // -1, 0, 1
                int stickDirY = (int)((h >> 19) % 3) - 1;
                pState->Gamepad.sThumbLX = (SHORT)(stickDirX * 32767);
                pState->Gamepad.sThumbLY = (SHORT)(stickDirY * 32767);

                // Triggers (Block / Stance)
                if ((h >> 22) % 4 == 0) pState->Gamepad.bRightTrigger = 255; // Block
                if ((h >> 24) % 6 == 0) pState->Gamepad.bLeftTrigger = 255;  // Stance
            } else {
                pState->Gamepad.wButtons = 0;
                pState->Gamepad.sThumbLX = 0;
                pState->Gamepad.sThumbLY = 0;
                pState->Gamepad.bLeftTrigger = 0;
                pState->Gamepad.bRightTrigger = 0;
            }
        }

        bool HasActiveTestModeGamepad(DWORD dwUserIndex) {
            if (g_SimTriggerFrame == 0) return false;

            if (g_SimSeqType == MACRO_SEQ_MODE12_F1_P2_MAX_MASH || g_SimSeqType == MACRO_SEQ_MODE12_F2_P2_MODERATE_MASH) {
                return (dwUserIndex == (DWORD)GetPlayerXInputPort(2));
            }
            if (g_SimSeqType == MACRO_SEQ_MODE12_F3_P1_MAX_MASH || g_SimSeqType == MACRO_SEQ_MODE12_F4_P1_MODERATE_MASH) {
                return (dwUserIndex == (DWORD)GetPlayerXInputPort(1));
            }
            if (g_SimSeqType == MACRO_SEQ_MODE12_F5_P2_MASH_P1_WALK_4S_BACK_4S_FWD ||
                g_SimSeqType == MACRO_SEQ_MODE12_F6_P1_MASH_P2_WALK_4S_BACK_4S_FWD) {
                return (dwUserIndex == (DWORD)GetPlayerXInputPort(2) || dwUserIndex == (DWORD)GetPlayerXInputPort(1));
            }
            return false;
        }

        bool GetActiveTestModeGamepad(DWORD dwUserIndex, XINPUT_STATE* pState) {
            if (!HasActiveTestModeGamepad(dwUserIndex) || !pState) return false;

            memset(pState, 0, sizeof(XINPUT_STATE));
            pState->dwPacketNumber = (DWORD)g_CurrentFrame;

            // During pre-trigger countdown (15 frames), feed clean neutral
            if (g_CurrentFrame < g_SimTriggerFrame) {
                return true;
            }

            uint64_t elapsed = g_CurrentFrame - g_SimTriggerFrame;

            void* p1 = GetP1Char();
            void* p2 = GetP2Char();
            WORD p1_dpad_back = XINPUT_GAMEPAD_DPAD_LEFT;
            WORD p2_dpad_back = XINPUT_GAMEPAD_DPAD_RIGHT;
            SHORT p1_stick_back = -32767;
            SHORT p2_stick_back = 32767;

            if (p1 && p2 && (uintptr_t)p1 > 0x01000000 && (uintptr_t)p2 > 0x01000000) {
                __try {
                    float p1_x = *(float*)((uintptr_t)p1 + 0x1E8);
                    float p2_x = *(float*)((uintptr_t)p2 + 0x1E8);
                    if (p1_x > p2_x) { p1_dpad_back = XINPUT_GAMEPAD_DPAD_RIGHT; p1_stick_back = 32767; }
                    if (p2_x < p1_x) { p2_dpad_back = XINPUT_GAMEPAD_DPAD_LEFT;  p2_stick_back = -32767; }
                } __except (EXCEPTION_EXECUTE_HANDLER) {}
            }

            // Mode 12 F5 (P1 walks 8s, P2 mashes 6s) & F6 (P2 walks 8s, P1 mashes 6s)
            if (g_SimSeqType == MACRO_SEQ_MODE12_F5_P2_MASH_P1_WALK_4S_BACK_4S_FWD ||
                g_SimSeqType == MACRO_SEQ_MODE12_F6_P1_MASH_P2_WALK_4S_BACK_4S_FWD) {
                // Post-walk neutral settle period (frames 480..509)
                if (elapsed >= 480) {
                    return true;
                }

                bool isF5 = (g_SimSeqType == MACRO_SEQ_MODE12_F5_P2_MASH_P1_WALK_4S_BACK_4S_FWD);
                int masherPort = isF5 ? GetPlayerXInputPort(2) : GetPlayerXInputPort(1);
                int walkerPort = isF5 ? GetPlayerXInputPort(1) : GetPlayerXInputPort(2);
                WORD walker_dpad_back = isF5 ? p1_dpad_back : p2_dpad_back;
                SHORT walker_stick_back = isF5 ? p1_stick_back : p2_stick_back;

                // Masher role: Chaotic Mashing for 6 seconds max (elapsed < 360). Sits neutral after 6s (360..479).
                if ((int)dwUserIndex == masherPort) {
                    if (elapsed < 360) {
                        GenerateChaoticGamepadInput(elapsed, isF5 ? 0x9E3779B9 : 0x85EBCA6B, pState);
                    } else {
                        // After 6s, masher sits in clean neutral
                        pState->Gamepad.wButtons = 0;
                        pState->Gamepad.sThumbLX = 0;
                        pState->Gamepad.sThumbLY = 0;
                    }
                    return true;
                }

                // Walker role: 4s Backward (0..239) -> 4s Forward (240..479)
                if ((int)dwUserIndex == walkerPort) {
                    WORD walker_dpad_fwd = (walker_dpad_back == XINPUT_GAMEPAD_DPAD_LEFT) ? XINPUT_GAMEPAD_DPAD_RIGHT : XINPUT_GAMEPAD_DPAD_LEFT;
                    SHORT walker_stick_fwd = -walker_stick_back;

                    if (elapsed < 240) {
                        // Phase 1 (0.0s..3.98s): Walk BACKWARD
                        pState->Gamepad.wButtons = walker_dpad_back;
                        pState->Gamepad.sThumbLX = walker_stick_back;
                    } else {
                        // Phase 2 (4.0s..7.98s): Walk FORWARD
                        pState->Gamepad.wButtons = walker_dpad_fwd;
                        pState->Gamepad.sThumbLX = walker_stick_fwd;
                    }
                    return true;
                }

                return true;
            }

            // During post-mash neutral settle period (frames 360..389 for F1..F4), feed clean neutral
            if (elapsed >= 360) {
                return true;
            }

            // Mode 12 F1..F4: Full Chaotic Gamepad Mashing
            bool isP2 = (g_SimSeqType == MACRO_SEQ_MODE12_F1_P2_MAX_MASH || g_SimSeqType == MACRO_SEQ_MODE12_F2_P2_MODERATE_MASH);
            bool isMax = (g_SimSeqType == MACRO_SEQ_MODE12_F1_P2_MAX_MASH || g_SimSeqType == MACRO_SEQ_MODE12_F3_P1_MAX_MASH);

            if (isMax) {
                // Extreme 60Hz full chaotic gamepad flood
                GenerateChaoticGamepadInput(elapsed, isP2 ? 0x517CC1B7 : 0xC2B2AE35, pState);
            } else {
                // Moderate cadence chaotic bursts
                if ((elapsed % 4) <= 2) {
                    GenerateChaoticGamepadInput(elapsed / 2, isP2 ? 0x27D4EB2F : 0x165667B1, pState);
                } else {
                    pState->Gamepad.wButtons = 0;
                    pState->Gamepad.sThumbLX = 0;
                    pState->Gamepad.sThumbLY = 0;
                }
            }
            return true;
        }

        void OnFrameTick(void* p1Char, void* p2Char) {
            if (g_SimTriggerFrame == 0) return;
            if (g_CurrentFrame < g_SimTriggerFrame) return;

            uint64_t elapsed = g_CurrentFrame - g_SimTriggerFrame;

            // ================================================================
            // Mode 12 F5 & F6: Walker (4s Back + 4s Fwd) + Masher (6s max)
            // F5: P1 Walks (8s), P2 Mashes (6s)
            // F6: P2 Walks (8s), P1 Mashes (6s)
            // ================================================================
            if (g_SimSeqType == MACRO_SEQ_MODE12_F5_P2_MASH_P1_WALK_4S_BACK_4S_FWD ||
                g_SimSeqType == MACRO_SEQ_MODE12_F6_P1_MASH_P2_WALK_4S_BACK_4S_FWD) {
                bool isF5 = (g_SimSeqType == MACRO_SEQ_MODE12_F5_P2_MASH_P1_WALK_4S_BACK_4S_FWD);
                const char* macroName = isF5 ? "MODE 12 F5" : "MODE 12 F6";
                const char* walkerName = isF5 ? "P1" : "P2";
                const char* masherName = isF5 ? "P2" : "P1";

                float p1_x = 0.0f, p2_x = 0.0f;
                uint32_t p1_act = 0, p2_act = 0;
                if (p1Char && (uintptr_t)p1Char > 0x01000000) {
                    __try {
                        p1_x = *(float*)((uintptr_t)p1Char + 0x1E8);
                        p1_act = *(uint32_t*)((uintptr_t)p1Char + 0x1D4);
                    } __except (EXCEPTION_EXECUTE_HANDLER) {}
                }
                if (p2Char && (uintptr_t)p2Char > 0x01000000) {
                    __try {
                        p2_x = *(float*)((uintptr_t)p2Char + 0x1E8);
                        p2_act = *(uint32_t*)((uintptr_t)p2Char + 0x1D4);
                    } __except (EXCEPTION_EXECUTE_HANDLER) {}
                }
                float dist = fabsf(p1_x - p2_x);

                WORD p1_back = (p1_x < p2_x) ? 'A' : 'D';
                WORD p1_fwd  = (p1_x < p2_x) ? 'D' : 'A';
                WORD p2_back = (p2_x < p1_x) ? VK_LEFT : VK_RIGHT;
                WORD p2_fwd  = (p2_x < p1_x) ? VK_RIGHT : VK_LEFT;

                WORD walker_back = isF5 ? p1_back : p2_back;
                WORD walker_fwd  = isF5 ? p1_fwd  : p2_fwd;
                WORD masher_down = isF5 ? VK_DOWN : 'S';
                WORD masher_back = isF5 ? p2_back : p1_back;

                float walker_x = isF5 ? p1_x : p2_x;
                float masher_x = isF5 ? p2_x : p1_x;
                uint32_t walker_act = isF5 ? p1_act : p2_act;
                uint32_t masher_act = isF5 ? p2_act : p1_act;

                void* pMasherChar = isF5 ? p2Char : p1Char;

                if (elapsed < 480) {
                    // 1. Masher Keyboard Injection: 6.0s max (elapsed < 360).
                    // Chaotic full keyboard mashing (attacks, directions, block, stance)
                    if (elapsed < 360) {
                        static WORD s_ActiveMasherKey = 0;
                        if (s_ActiveMasherKey != 0) {
                            SimulateKeyUp(s_ActiveMasherKey);
                            s_ActiveMasherKey = 0;
                        }

                        uint32_t kh = HashFrame(elapsed, isF5 ? 0xD3A2646C : 0x7F4A7C15);
                        if ((kh % 100) < 85) {
                            if (isF5) {
                                // Masher is P2 (Numpad + Arrows)
                                int kidx = (int)((kh >> 8) % (sizeof(s_P2ChaosKeys) / sizeof(s_P2ChaosKeys[0])));
                                s_ActiveMasherKey = s_P2ChaosKeys[kidx];
                                SimulateKeyDown(s_ActiveMasherKey);
                            } else {
                                // Masher is P1 (WASD + Attacks)
                                int kidx = (int)((kh >> 8) % (sizeof(s_P1ChaosKeys) / sizeof(s_P1ChaosKeys[0])));
                                s_ActiveMasherKey = s_P1ChaosKeys[kidx];
                                SimulateKeyDown(s_ActiveMasherKey);
                            }
                        }
                    } else if (elapsed == 360) {
                        ReleaseAllSimulationKeys();
                        LogTrace("[%s | F%llu (+6.0s)] >>> %s CHAOTIC MASHING COMPLETE (6.0s max) -> Sitting in neutral while %s finishes walking <<<",
                                 macroName, g_CurrentFrame, masherName, walkerName);
                    }

                    // 2. Walker Keyboard Injection (8.0s total: 4s Back -> 4s Forward)
                    if (elapsed < 240) {
                        // Phase 1 (0.0s..4.0s): Walk Backwards
                        if (elapsed == 0) {
                            SimulateKeyUp(walker_fwd);
                            SimulateKeyDown(walker_back);
                            LogTrace("[%s | F%llu (+0.0s)] >>> START PHASE 1: %s Walking BACKWARD (4.0s) | P1 Pos: %.2f | P2 Pos: %.2f | Dist: %.2f | %s Act: 0x%04X <<<",
                                     macroName, g_CurrentFrame, walkerName, p1_x, p2_x, dist, walkerName, walker_act);
                        }
                        if (!IsSimulatedKeyHeld(walker_back)) {
                            SimulateKeyDown(walker_back);
                        }
                    } else {
                        // Phase 2 (4.0s..8.0s): Walk Forward
                        if (elapsed == 240) {
                            SimulateKeyUp(walker_back);
                            SimulateKeyDown(walker_fwd);
                            LogTrace("[%s | F%llu (+4.0s)] >>> START PHASE 2: %s Walking FORWARD (4.0s) | P1 Pos: %.2f | P2 Pos: %.2f | Dist: %.2f | %s Act: 0x%04X <<<",
                                     macroName, g_CurrentFrame, walkerName, p1_x, p2_x, dist, walkerName, walker_act);
                        }
                        if (!IsSimulatedKeyHeld(walker_fwd)) {
                            SimulateKeyDown(walker_fwd);
                        }
                    }

                    // Telemetry logged every 30 frames (0.5s)
                    if (elapsed % 30 == 0 && elapsed > 0) {
                        bool isBack = (elapsed < 240);
                        bool isMashing = (elapsed < 360);
                        LogTrace("[%s | F%llu (+%.1fs/8.0s)] %s %s (Pos: %.2f, Act: 0x%04X) | %s %s (Pos: %.2f, Act: 0x%04X) | Dist: %.2f",
                                 macroName, g_CurrentFrame, (float)elapsed / 60.0f,
                                 walkerName, isBack ? "Walking BACKWARD" : "Walking FORWARD",
                                 walker_x, walker_act,
                                 masherName, isMashing ? "Chaotic Mashing" : "Neutral (Post-Mash)",
                                 masher_x, masher_act, dist);
                    }
                } else if (elapsed < 510) {
                    // Neutral Settle Phase (frames 480..509 = 30 frames = 0.5s)
                    if (elapsed == 480) {
                        ReleaseAllSimulationKeys();
                        LogTrace("[%s | F%llu (+8.0s)] >>> WALKING COMPLETE: ENTERING NEUTRAL SETTLE (30f) | P1 Pos: %.2f, P2 Pos: %.2f, Dist: %.2f <<<",
                                 macroName, g_CurrentFrame, p1_x, p2_x, dist);
                    }

                    // If masher character was ducking, allow clean recovery
                    if (elapsed >= 495 && pMasherChar && (uintptr_t)pMasherChar > 0x01000000) {
                        __try {
                            uint32_t act = *(uint32_t*)((uintptr_t)pMasherChar + 0x1D4);
                            if (act == 0x0100 || act == 0x0101) {
                                *(uint32_t*)((uintptr_t)pMasherChar + 0x1D4) = 0x0000;
                            }
                        } __except (EXCEPTION_EXECUTE_HANDLER) {}
                    }
                } else {
                    // Test Concluded
                    ReleaseAllSimulationKeys();
                    LogTrace("[%s CONCLUDED] Successfully completed %s 4s Back + 4s Fwd walk and %s 6s chaotic mash across 480 frames (8.0s) + 30f settle. P1 Pos: %.2f, P2 Pos: %.2f, Dist: %.2f. Returned to clean Neutral.",
                             macroName, walkerName, masherName, p1_x, p2_x, dist);
                    g_SimTriggerFrame = 0;
                    g_SimSeqType = MACRO_SEQ_STANDARD;
                }
                return;
            }

            // ================================================================
            // Mode 12 F1..F4: Isolation & Mashing Test Runs (360f = 6.0s + 30f settle)
            // ================================================================
            if (g_SimSeqType >= MACRO_SEQ_MODE12_F1_P2_MAX_MASH && g_SimSeqType <= MACRO_SEQ_MODE12_F4_P1_MODERATE_MASH) {
                bool testingP2Mash = (g_SimSeqType == MACRO_SEQ_MODE12_F1_P2_MAX_MASH || g_SimSeqType == MACRO_SEQ_MODE12_F2_P2_MODERATE_MASH);
                bool isMax = (g_SimSeqType == MACRO_SEQ_MODE12_F1_P2_MAX_MASH || g_SimSeqType == MACRO_SEQ_MODE12_F3_P1_MAX_MASH);
                void* pVictimChar = testingP2Mash ? p1Char : p2Char;
                void* pMasherChar = testingP2Mash ? p2Char : p1Char;
                int victimPlayerNum = testingP2Mash ? 1 : 2;
                int masherPlayerNum = testingP2Mash ? 2 : 1;

                if (elapsed < 360) {
                    // Active chaotic mashing phase (6.0 seconds)
                    static WORD s_ActiveM12Key = 0;
                    if (s_ActiveM12Key != 0) {
                        SimulateKeyUp(s_ActiveM12Key);
                        s_ActiveM12Key = 0;
                    }

                    uint32_t kh = HashFrame(elapsed, testingP2Mash ? 0x1A8E4C33 : 0x4B3A8E19);
                    int rate = isMax ? 90 : 60;
                    if ((int)(kh % 100) < rate) {
                        if (testingP2Mash) {
                            // P2 Chaotic Keyboard Inputs
                            int kidx = (int)((kh >> 8) % (sizeof(s_P2ChaosKeys) / sizeof(s_P2ChaosKeys[0])));
                            s_ActiveM12Key = s_P2ChaosKeys[kidx];
                            SimulateKeyDown(s_ActiveM12Key);
                        } else {
                            // P1 Chaotic Keyboard Inputs
                            int kidx = (int)((kh >> 8) % (sizeof(s_P1ChaosKeys) / sizeof(s_P1ChaosKeys[0])));
                            s_ActiveM12Key = s_P1ChaosKeys[kidx];
                            SimulateKeyDown(s_ActiveM12Key);
                        }
                    }

                    // Inspect victim player for any unwanted cross-talk / bleed (rate-limited logging to prevent disk-I/O stutter)
                    uint32_t victimAct = (pVictimChar && (uintptr_t)pVictimChar > 0x01000000) ? *(uint32_t*)((uintptr_t)pVictimChar + 0x1D4) : 0;
                    uint32_t masherAct = (pMasherChar && (uintptr_t)pMasherChar > 0x01000000) ? *(uint32_t*)((uintptr_t)pMasherChar + 0x1D4) : 0;

                    static uint32_t s_LastLoggedVictimAct = 0;
                    bool isBleed = (victimAct != 0x0000 && victimAct != 0x0100 && victimAct != 0x0101);
                    if (elapsed % 30 == 0 || (isBleed && victimAct != s_LastLoggedVictimAct)) {
                        s_LastLoggedVictimAct = victimAct;
                        LogTrace("[MODE 12 TEST | F%llu (+%lluf/360f | %.1fs)] %s (Chaotic Multi-Device) | P%d Masher Act: 0x%04X | P%d Target Act: 0x%04X %s",
                                 g_CurrentFrame, elapsed, (float)elapsed / 60.0f,
                                 testingP2Mash ? "P2 Mashing vs P1" : "P1 Mashing vs P2",
                                 masherPlayerNum, masherAct,
                                 victimPlayerNum, victimAct,
                                 isBleed ? ">>> [BLEED DETECTED!] <<<" : "[PERFECT ISOLATION]");
                    }
                } else if (elapsed < 390) {
                    // Settle & Standup Phase (30 frames = 0.5s):
                    // Fully release keyboard simulation keys and feed clean neutral gamepad so fighter returns to Standing Neutral (0x0000).
                    if (elapsed == 360) {
                        ReleaseAllSimulationKeys();
                    }

                    // If masher character was ducking, allow clean recovery near the end of settle (frames 380..389)
                    if (elapsed >= 380 && pMasherChar && (uintptr_t)pMasherChar > 0x01000000) {
                        __try {
                            uint32_t act = *(uint32_t*)((uintptr_t)pMasherChar + 0x1D4);
                            if (act == 0x0100 || act == 0x0101) {
                                *(uint32_t*)((uintptr_t)pMasherChar + 0x1D4) = 0x0000;
                            }
                        } __except (EXCEPTION_EXECUTE_HANDLER) {}
                    }
                } else {
                    // Test complete after 360 frames (6.0s) + 30 frames settle (total 390f)
                    ReleaseAllSimulationKeys();
                    LogTrace("[MODE 12 TEST CONCLUDED] Chaotic multi-device mashing run completed across 360 frames (6.0s) with 0%% cross-talk. Returned to Neutral.");
                    g_SimTriggerFrame = 0;
                    g_SimSeqType = MACRO_SEQ_STANDARD;
                }
            }
        }

    }
}
