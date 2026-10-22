#include "negative_edge.hpp"
#include "crouch_buffer_fix.hpp"
#include "extra_gameplay.hpp"
#include "stun_modifier.hpp"
#include "engine_common.hpp"
#include "simulation.hpp"
#include "profiler.hpp"
#include "frame_data.hpp"
#include "cancel_detector.hpp"
#include "minhook/include/MinHook.h"
#include <cstdio>
#include <intrin.h>

namespace MK9TradeFix {
    namespace NegativeEdge {

        void SetP1Enabled(bool enabled) {
            g_P1_NegativeEdgeEnabled = enabled;
            LogTrace(">>> [RELEASE CHECK] Player 1 Negative Edge: %s <<<", enabled ? "ENABLED" : "DISABLED");
        }

        void SetP2Enabled(bool enabled) {
            g_P2_NegativeEdgeEnabled = enabled;
            LogTrace(">>> [RELEASE CHECK] Player 2 Negative Edge: %s <<<", enabled ? "ENABLED" : "DISABLED");
        }

        bool IsP1Enabled() {
            return g_P1_NegativeEdgeEnabled;
        }

        bool IsP2Enabled() {
            return g_P2_NegativeEdgeEnabled;
        }

        void ToggleP1() {
            SetP1Enabled(!g_P1_NegativeEdgeEnabled);
        }

        void ToggleP2() {
            SetP2Enabled(!g_P2_NegativeEdgeEnabled);
        }

        void SetBoth(bool enabled) {
            g_P1_NegativeEdgeEnabled = enabled;
            g_P2_NegativeEdgeEnabled = enabled;
            LogTrace(">>> [RELEASE CHECK] Both Players Negative Edge: %s <<<", enabled ? "ENABLED" : "DISABLED");
        }

        // ========================================================================
        // Fast Per-Frame Player Cache & Resolve
        // ========================================================================
        static void* s_CachedP1Char = nullptr;
        static void* s_CachedP2Char = nullptr;
        static void* s_CachedP1Info = nullptr;
        static void* s_CachedP2Info = nullptr;
        static uint64_t s_LastCacheFrame = (uint64_t)-1;

        void UpdatePlayerCache(void* p1, void* p2, void* p1Info, void* p2Info) {
            s_CachedP1Char = p1;
            s_CachedP2Char = p2;
            s_CachedP1Info = p1Info ? p1Info : ((p1 && (uintptr_t)p1 > 0x01000000) ? *(void**)((uintptr_t)p1 + 0x7638) : nullptr);
            s_CachedP2Info = p2Info ? p2Info : ((p2 && (uintptr_t)p2 > 0x01000000) ? *(void**)((uintptr_t)p2 + 0x7638) : nullptr);
            s_LastCacheFrame = g_CurrentFrame;
        }

        static void EnsurePlayerCache() {
            if (s_LastCacheFrame == g_CurrentFrame && s_CachedP1Char) return;
            void* p1 = GetP1Char();
            void* p2 = GetP2Char();
            void* p1Info = (p1 && (uintptr_t)p1 > 0x01000000) ? *(void**)((uintptr_t)p1 + 0x7638) : nullptr;
            void* p2Info = (p2 && (uintptr_t)p2 > 0x01000000) ? *(void**)((uintptr_t)p2 + 0x7638) : nullptr;
            UpdatePlayerCache(p1, p2, p1Info, p2Info);
        }

        static int ResolvePlayerNumber(void* pChar, void* pPlayerInfo = nullptr) {
            // Fast path 1: Compare with cached pointers (2 instructions, no SEH, no pointer chasing)
            if (pChar) {
                if (pChar == s_CachedP1Char) return 1;
                if (pChar == s_CachedP2Char) return 2;
            }
            if (pPlayerInfo) {
                if (pPlayerInfo == s_CachedP1Info) return 1;
                if (pPlayerInfo == s_CachedP2Info) return 2;
            }

            // Fast path 2: Refresh cache if frame advanced and retry direct comparison
            if (s_LastCacheFrame != g_CurrentFrame || !s_CachedP1Char) {
                EnsurePlayerCache();
                if (pChar) {
                    if (pChar == s_CachedP1Char) return 1;
                    if (pChar == s_CachedP2Char) return 2;
                }
                if (pPlayerInfo) {
                    if (pPlayerInfo == s_CachedP1Info) return 1;
                    if (pPlayerInfo == s_CachedP2Info) return 2;
                }
            }

            // Slow fallback: real miss / tag character
            if (pChar) {
                int pNum = GetPlayerNumber(pChar);
                if (pNum != 0) return pNum;
            }
            if (pPlayerInfo) {
                __try {
                    uint32_t pIdx = *(uint32_t*)((uintptr_t)pPlayerInfo + 0xB3C);
                    if (pIdx == 0) return 1;
                    if (pIdx == 1) return 2;
                } __except (EXCEPTION_EXECUTE_HANDLER) {}
            }
            if (g_SimSeqType >= MACRO_SEQ_MODE8_F1_RAW_NEG_EDGE && g_SimSeqType <= MACRO_SEQ_MODE8_F8_STRING_BUFFER) {
                return (g_SimSeqType == MACRO_SEQ_MODE8_F5_P2_RAW_NEG_EDGE ||
                        g_SimSeqType == MACRO_SEQ_MODE8_F6_P2_NORMAL_SPECIAL ||
                        g_SimSeqType == MACRO_SEQ_MODE8_F7_P2_HIT_CONFIRM_CANCEL) ? 2 : 1;
            }
            return 1;
        }

        // ========================================================================
        // 1. Root Release Check Query Filter (0x00446950)
        // NetherRealm native button release check function:
        // CheckButtonReleasedTag_Buffer(pPlayerInfo, tag)
        // Calls 0x004255A0 (IsButtonReleased: prev_down && !curr_down).
        // This is the sole bottleneck through which all button release queries
        // for buffered special moves and attacks pass.
        // When Release Check (Negative Edge) is OFF, all button releases are suppressed (return 0).
        // ========================================================================
        typedef int (__fastcall *tCheckButtonReleasedTag)(void* pPlayerInfo, void* edxUnused, uint32_t tag);
        static tCheckButtonReleasedTag oCheckButtonReleasedTag_Buffer = nullptr;

        static int __fastcall hkCheckButtonReleasedTag_Buffer(void* pPlayerInfo, void* edxUnused, uint32_t tag) {
            if (!oCheckButtonReleasedTag_Buffer) return 0;
            ScopedProfiler prof(PROF_INPUT_HOOKS);

            // Global decision bypass when both players have the same Negative Edge setting
            if (g_P1_NegativeEdgeEnabled == g_P2_NegativeEdgeEnabled) {
                if (!g_P1_NegativeEdgeEnabled) {
                    // Both OFF (Tournament standard default) -> instant zero overhead suppression
                    if (g_SimSeqType >= MACRO_SEQ_MODE8_F1_RAW_NEG_EDGE && g_SimSeqType <= MACRO_SEQ_MODE8_F8_STRING_BUFFER) {
                        int playerNum = ResolvePlayerNumber(nullptr, pPlayerInfo);
                        int wouldHaveMatched = 0;
                        __asm {
                            push tag
                            mov ecx, pPlayerInfo
                            call oCheckButtonReleasedTag_Buffer
                            mov wouldHaveMatched, eax
                        }
                        if (wouldHaveMatched) {
                            LogTrace("[RELEASE CHECK] >>> SUPPRESSED P%d Negative Edge Release Tag 0x%08X in 0x00446950! <<<", playerNum, tag);
                        }
                    }
                    return 0; // Completely suppressed (Release Check: OFF)
                }

                // Both ON (Vanilla behavior) -> execute native check directly
                int res = 0;
                __asm {
                    push tag
                    mov ecx, pPlayerInfo
                    call oCheckButtonReleasedTag_Buffer
                    mov res, eax
                }
                if (res != 0 && g_SimSeqType >= MACRO_SEQ_MODE8_F1_RAW_NEG_EDGE && g_SimSeqType <= MACRO_SEQ_MODE8_F8_STRING_BUFFER) {
                    int playerNum = ResolvePlayerNumber(nullptr, pPlayerInfo);
                    LogTrace("[RELEASE CHECK MATCH!] P%d Buffer Release Tag 0x%08X MATCHED (res=%d)!", playerNum, tag, res);
                }
                return res;
            }

            int playerNum = ResolvePlayerNumber(nullptr, pPlayerInfo);
            bool enabled = (playerNum == 1) ? g_P1_NegativeEdgeEnabled : (playerNum == 2 ? g_P2_NegativeEdgeEnabled : true);

            if (!enabled && playerNum != 0) {
                if (g_SimSeqType >= MACRO_SEQ_MODE8_F1_RAW_NEG_EDGE && g_SimSeqType <= MACRO_SEQ_MODE8_F8_STRING_BUFFER) {
                    int wouldHaveMatched = 0;
                    __asm {
                        push tag
                        mov ecx, pPlayerInfo
                        call oCheckButtonReleasedTag_Buffer
                        mov wouldHaveMatched, eax
                    }
                    if (wouldHaveMatched) {
                        LogTrace("[RELEASE CHECK] >>> SUPPRESSED P%d Negative Edge Release Tag 0x%08X in 0x00446950! <<<", playerNum, tag);
                    }
                }
                return 0; // Completely suppressed (Release Check: OFF)
            }

            int res = 0;
            __asm {
                push tag
                mov ecx, pPlayerInfo
                call oCheckButtonReleasedTag_Buffer
                mov res, eax
            }
            if (res != 0 && g_SimSeqType >= MACRO_SEQ_MODE8_F1_RAW_NEG_EDGE && g_SimSeqType <= MACRO_SEQ_MODE8_F8_STRING_BUFFER) {
                LogTrace("[RELEASE CHECK MATCH!] P%d Buffer Release Tag 0x%08X MATCHED (res=%d)!", playerNum, tag, res);
            }
            return res;
        }

        // ========================================================================
        // 2. Script Bytecode Command Engine Filters (0x0088BAD0 & 0x0088A1D0)
        // Evaluates special moves authored in .mko bytecode.
        // Command mode 0xE (+0x74) is explicitly the button release / negative edge trigger.
        // ========================================================================
        static void* GetCharFromCommand(void* pCommand) {
            if (!pCommand || (uintptr_t)pCommand < 0x00400000) return nullptr;
            void* pChar = nullptr;
            __try {
                uint32_t charToken = *(uint32_t*)((uintptr_t)pCommand + 0x3C);
                if (charToken) {
                    __asm {
                        mov ecx, charToken
                        push 1
                        mov eax, 0x00895B70
                        call eax
                        mov pChar, eax
                    }
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return nullptr;
            }
            return pChar;
        }

        typedef int (__cdecl *tIsCommandInputSatisfied)(void* pCommand);
        static tIsCommandInputSatisfied oIsCommandInputSatisfied = nullptr;

        static int __cdecl hkIsCommandInputSatisfied(void* pCommand) {
            if (!oIsCommandInputSatisfied) return 0;
            int satisfied = oIsCommandInputSatisfied(pCommand);

            if (pCommand && (uintptr_t)pCommand > 0x00400000) {
                __try {
                    void* pChar = GetCharFromCommand(pCommand);
                    if (satisfied && ExtraGameplay::ShouldSuppressSpecialOutRoll(pChar)) {
                        return 0;
                    }

                    uint32_t cmdMode = *(uint32_t*)((uintptr_t)pCommand + 0x74);
                    // Fast path: Only command mode 0xE is button release, or if diagnostic Mode 8 active
                    if (cmdMode == 0xE || (g_SimSeqType >= MACRO_SEQ_MODE8_F1_RAW_NEG_EDGE && g_SimSeqType <= MACRO_SEQ_MODE8_F8_STRING_BUFFER)) {
                        bool bothSame = (g_P1_NegativeEdgeEnabled == g_P2_NegativeEdgeEnabled);
                        if (cmdMode == 0xE && bothSame && !g_P1_NegativeEdgeEnabled) {
                            if (g_SimSeqType >= MACRO_SEQ_MODE8_F1_RAW_NEG_EDGE && g_SimSeqType <= MACRO_SEQ_MODE8_F8_STRING_BUFFER) {
                                int playerNum = ResolvePlayerNumber(pChar);
                                LogTrace("[CMD_SUPPRESS | #%llu] Suppressed P%d mode 0xE release command!", g_CurrentFrame, playerNum);
                            }
                            return 0; // Suppress release command mode 0xE without player resolve
                        }

                        int playerNum = ResolvePlayerNumber(pChar);
                        if (g_SimSeqType >= MACRO_SEQ_MODE8_F1_RAW_NEG_EDGE && g_SimSeqType <= MACRO_SEQ_MODE8_F8_STRING_BUFFER && satisfied) {
                            LogTrace("[CMD_SATISFIED | #%llu] P%d pCmd=0x%p mode=0x%X res=%d", g_CurrentFrame, playerNum, pCommand, cmdMode, satisfied);
                        }
                        if (cmdMode == 0xE) { // Button release command
                            bool enabled = (playerNum == 1) ? g_P1_NegativeEdgeEnabled : (playerNum == 2 ? g_P2_NegativeEdgeEnabled : true);
                            if (!enabled && playerNum != 0) {
                                LogTrace("[CMD_SUPPRESS | #%llu] Suppressed P%d mode 0xE release command!", g_CurrentFrame, playerNum);
                                return 0; // Suppress release command mode 0xE
                            }
                        }
                    }
                } __except (EXCEPTION_EXECUTE_HANDLER) {}
            }
            return satisfied;
        }

        typedef int (__cdecl *tGetSatisfiedInputButton)(void* pCommand, uint32_t flag);
        static tGetSatisfiedInputButton oGetSatisfiedInputButton = nullptr;

        static int __cdecl hkGetSatisfiedInputButton(void* pCommand, uint32_t flag) {
            if (!oGetSatisfiedInputButton) return 0;
            int btn = oGetSatisfiedInputButton(pCommand, flag);

            if (pCommand && (uintptr_t)pCommand > 0x00400000) {
                __try {
                    void* pChar = GetCharFromCommand(pCommand);
                    if (btn && ExtraGameplay::ShouldSuppressSpecialOutRoll(pChar)) {
                        return 0;
                    }

                    uint32_t cmdMode = *(uint32_t*)((uintptr_t)pCommand + 0x74);
                    // Fast path: Only command mode 0xE is button release, or if diagnostic Mode 8 active
                    if (cmdMode == 0xE || (g_SimSeqType >= MACRO_SEQ_MODE8_F1_RAW_NEG_EDGE && g_SimSeqType <= MACRO_SEQ_MODE8_F8_STRING_BUFFER)) {
                        bool bothSame = (g_P1_NegativeEdgeEnabled == g_P2_NegativeEdgeEnabled);
                        if (cmdMode == 0xE && bothSame && !g_P1_NegativeEdgeEnabled) {
                            return 0; // Suppress release command mode 0xE without player resolve
                        }

                        void* pChar = GetCharFromCommand(pCommand);
                        int playerNum = ResolvePlayerNumber(pChar);
                        if (g_SimSeqType >= MACRO_SEQ_MODE8_F1_RAW_NEG_EDGE && g_SimSeqType <= MACRO_SEQ_MODE8_F8_STRING_BUFFER && btn) {
                            LogTrace("[CMD_BTN | #%llu] P%d pCmd=0x%p mode=0x%X btn=0x%X", g_CurrentFrame, playerNum, pCommand, cmdMode, btn);
                        }
                        if (cmdMode == 0xE) {
                            bool enabled = (playerNum == 1) ? g_P1_NegativeEdgeEnabled : (playerNum == 2 ? g_P2_NegativeEdgeEnabled : true);
                            if (!enabled && playerNum != 0) {
                                return 0;
                            }
                        }
                    }
                } __except (EXCEPTION_EXECUTE_HANDLER) {}
            }
            return btn;
        }

        // ========================================================================
        // 3. Match And Execute Move (0x008A0600)
        // Dispatches matched command moves. Blocks release button tags
        // when Negative Edge is disabled, and resolves Crouch Buffer Timeouts.
        // ========================================================================
        typedef int (__fastcall *tMatchAndExecuteMove)(void* pChar, void* edxUnused, uint32_t dirMotion, uint32_t button, uint32_t flag, uint32_t* pOutToken, uint32_t* pOut1, uint32_t* pOut2, uint32_t* pOut3);
        static tMatchAndExecuteMove oMatchAndExecuteMove = nullptr;

        static int __fastcall hkMatchAndExecuteMove_Impl(void* pChar, void* edxUnused, uint32_t dirMotion, uint32_t button, uint32_t flag, uint32_t* pOutToken, uint32_t* pOut1, uint32_t* pOut2, uint32_t* pOut3) {
            if (!oMatchAndExecuteMove) return 0;

            int playerNum = ResolvePlayerNumber(pChar);
            if (StunModifier::IsInExtendedStun(playerNum)) {
                return 0; // Suppress all move executions while held in extended stun
            }

            // Extra Gameplay Option B: Disable Wakeup Attack directly out of Quick Roll
            if (ExtraGameplay::ShouldSuppressSpecialOutRoll(pChar)) {
                return 0;
            }

            if (g_SimSeqType != MACRO_SEQ_STANDARD) {
                int playerNum = ResolvePlayerNumber(pChar);
                LogTrace("[MATCH_EXEC | #%llu] P%d: arg1=0x%08X, arg2=0x%08X, flag=%u",
                         g_CurrentFrame, playerNum, dirMotion, button, flag);
            }

            // In 0x008A0600, param 1 [ebp+8] is the button tag (passed as dirMotion) and param 2 [ebp+0xC] is motion.
            // Check both arg1 and arg2 for any release tags.
            bool isReleaseMove = (dirMotion >= 0x8000001B && dirMotion <= 0x8000001E) ||
                                 (button >= 0x8000001B && button <= 0x8000001E) || 
                                 (dirMotion >= 0x1B && dirMotion <= 0x1E) || 
                                 (button >= 0x1B && button <= 0x1E) || 
                                 dirMotion == 0x32 || dirMotion == 0x33 || 
                                 button == 0x32 || button == 0x33 || 
                                 dirMotion == 0x80000032 || dirMotion == 0x80000033 ||
                                 button == 0x80000032 || button == 0x80000033;

            if (isReleaseMove) {
                if (g_P1_NegativeEdgeEnabled == g_P2_NegativeEdgeEnabled) {
                    if (!g_P1_NegativeEdgeEnabled) {
                        if (g_SimSeqType != MACRO_SEQ_STANDARD) {
                            int playerNum = ResolvePlayerNumber(pChar);
                            LogTrace("[RELEASE CHECK] Suppressed P%d Negative Edge move in MatchAndExecuteMove! arg1=0x%08X, arg2=0x%08X",
                                     playerNum, dirMotion, button);
                        }
                        return 0; // Negative Edge disabled! Suppress move execution!
                    }
                } else {
                    int playerNum = ResolvePlayerNumber(pChar);
                    bool enabled = (playerNum == 1) ? g_P1_NegativeEdgeEnabled : (playerNum == 2 ? g_P2_NegativeEdgeEnabled : true);
                    if (!enabled && playerNum != 0) {
                        LogTrace("[RELEASE CHECK] Suppressed P%d Negative Edge move in MatchAndExecuteMove! arg1=0x%08X, arg2=0x%08X",
                                 playerNum, dirMotion, button);
                        return 0; // Negative Edge disabled! Suppress move execution!
                    }
                }
            }

            // Crouch Buffer Timeout Resolver (Mode 9 F8 Fix):
            // When Down has been held for > 8 frames, MatchAction passes Neutral (0x80000000).
            // If the character is in duck posture or holding Down, remap to the native crouch token.
            uint32_t effectiveButton = button;
            if (MK9TradeFix::CrouchBufferFix::IsEnabled() && button == 0x80000000) {
                effectiveButton = MK9TradeFix::CrouchBufferFix::RemapCrouchAttackToken(pChar, dirMotion, button);
                if (effectiveButton != button && g_SimSeqType != MACRO_SEQ_STANDARD) {
                    int playerNum = ResolvePlayerNumber(pChar);
                    LogTrace("[CROUCH BUFFER FIX | #%llu] P%d Remapped Neutral button (0x%08X) -> Crouch Token (0x%08X) for dirMotion 0x%08X",
                             g_CurrentFrame, playerNum, button, effectiveButton, dirMotion);
                }
            }

            int result = 0;
            __asm {
                push pOut3
                push pOut2
                push pOut1
                push pOutToken
                push flag
                push effectiveButton
                push dirMotion
                mov ecx, pChar
                call oMatchAndExecuteMove
                mov result, eax
            }

            // Safe fallback: if remapped crouch token failed to match, try original button
            if (result == 0 && effectiveButton != button) {
                __asm {
                    push pOut3
                    push pOut2
                    push pOut1
                    push pOutToken
                    push flag
                    push button
                    push dirMotion
                    mov ecx, pChar
                    call oMatchAndExecuteMove
                    mov result, eax
                }
            }

            if (result != 0) {
                int playerNum = ResolvePlayerNumber(pChar);
                if (playerNum == 1) {
                    g_P1_LastAttackToken = dirMotion;
                } else if (playerNum == 2) {
                    g_P2_LastAttackToken = dirMotion;
                }
                uint32_t tok = 0;
                __try {
                    if (pOutToken) tok = *pOutToken;
                } __except (EXCEPTION_EXECUTE_HANDLER) {}

                CancelDetector::OnMoveMatched(playerNum, dirMotion, effectiveButton, tok);

                if (g_SimSeqType != MACRO_SEQ_STANDARD) {
                    LogTrace("[MOVE_MATCHED_SUCCESS | #%llu] P%d move MATCHED! arg1=0x%08X, arg2=0x%08X, token=0x%08X",
                             g_CurrentFrame, playerNum, dirMotion, effectiveButton, tok);
                }
            }
            return result;
        }

        static int __fastcall hkMatchAndExecuteMove(void* pChar, void* edxUnused, uint32_t dirMotion, uint32_t button, uint32_t flag, uint32_t* pOutToken, uint32_t* pOut1, uint32_t* pOut2, uint32_t* pOut3) {
            PROFILE_START();
            int res = hkMatchAndExecuteMove_Impl(pChar, edxUnused, dirMotion, button, flag, pOutToken, pOut1, pOut2, pOut3);
            PROFILE_END(PROF_INPUT_HOOKS);
            return res;
        }

        // ========================================================================
        // 4. CheckBufferedMove (0x00453A80 - VTable Slot 5) & (0x00454710 - Slot 6)
        // Evaluates buffered special move satisfaction from tag.
        // When Release Check is OFF, suppresses button release matches (returns 0).
        // ========================================================================
        typedef int (__fastcall *tCheckBufferedMove)(void* pPlayerInfo, void* edxUnused, uint32_t tag);
        static tCheckBufferedMove oCheckBufferedMove = nullptr;
        static tCheckBufferedMove oCheckBufferedMove_Slot6 = nullptr;

        static int __fastcall hkCheckBufferedMove(void* pPlayerInfo, void* edxUnused, uint32_t tag) {
            if (!oCheckBufferedMove) return 0;

            int pNum = ResolvePlayerNumber(nullptr, pPlayerInfo);
            if (StunModifier::IsInExtendedStun(pNum)) {
                return 0; // Suppress buffered moves while in extended stun
            }

            // Extra Gameplay: Filter Quick Wakeup / Delayed Wakeup Roll direction taps
            if (ExtraGameplay::ShouldSuppressDirectionPressed(pPlayerInfo, tag)) {
                return 0;
            }

            // Extra Gameplay: Suppress buffered move out of quick roll (Option B)
            void* pChar = (pNum == 2) ? GetP2Char() : GetP1Char();
            if (pChar && ExtraGameplay::ShouldSuppressSpecialOutRoll(pChar)) {
                return 0;
            }

            int res = 0;
            __asm {
                push tag
                mov ecx, pPlayerInfo
                call oCheckBufferedMove
                mov res, eax
            }
            if (res != 0) {
                if (g_P1_NegativeEdgeEnabled == g_P2_NegativeEdgeEnabled) {
                    if (!g_P1_NegativeEdgeEnabled) {
                        if (g_SimSeqType >= MACRO_SEQ_MODE8_F1_RAW_NEG_EDGE && g_SimSeqType <= MACRO_SEQ_MODE8_F8_STRING_BUFFER) {
                            int playerNum = ResolvePlayerNumber(nullptr, pPlayerInfo);
                            LogTrace("[RELEASE CHECK] >>> SUPPRESSED P%d Negative Edge Buffered Move (Tag 0x%08X) in 0x00453A80! <<<", playerNum, tag);
                        }
                        return 0; // Release Check OFF: suppress buffered release match!
                    }
                } else {
                    int playerNum = ResolvePlayerNumber(nullptr, pPlayerInfo);
                    bool enabled = (playerNum == 1) ? g_P1_NegativeEdgeEnabled : (playerNum == 2 ? g_P2_NegativeEdgeEnabled : true);
                    if (!enabled && playerNum != 0) {
                        LogTrace("[RELEASE CHECK] >>> SUPPRESSED P%d Negative Edge Buffered Move (Tag 0x%08X) in 0x00453A80! <<<", playerNum, tag);
                        return 0; // Release Check OFF: suppress buffered release match!
                    }
                }
                if (g_SimSeqType >= MACRO_SEQ_MODE8_F1_RAW_NEG_EDGE && g_SimSeqType <= MACRO_SEQ_MODE8_F8_STRING_BUFFER) {
                    int playerNum = ResolvePlayerNumber(nullptr, pPlayerInfo);
                    LogTrace("[BUFFER_CHECK_MATCH | #%llu] P%d tag=0x%08X satisfied!", g_CurrentFrame, playerNum, tag);
                }
            }
            return res;
        }

        static int __fastcall hkCheckBufferedMove_Slot6(void* pPlayerInfo, void* edxUnused, uint32_t tag) {
            if (!oCheckBufferedMove_Slot6) return 0;

            int pNumSlot6 = ResolvePlayerNumber(nullptr, pPlayerInfo);
            if (StunModifier::IsInExtendedStun(pNumSlot6)) {
                return 0; // Suppress buffered moves while in extended stun
            }

            // Extra Gameplay: Filter Quick Wakeup / Delayed Wakeup Roll direction taps
            if (ExtraGameplay::ShouldSuppressDirectionPressed(pPlayerInfo, tag)) {
                return 0;
            }

            // Extra Gameplay: Suppress buffered move out of quick roll (Option B)
            void* pCharSlot6 = (pNumSlot6 == 2) ? GetP2Char() : GetP1Char();
            if (pCharSlot6 && ExtraGameplay::ShouldSuppressSpecialOutRoll(pCharSlot6)) {
                return 0;
            }

            int res = 0;
            __asm {
                push tag
                mov ecx, pPlayerInfo
                call oCheckBufferedMove_Slot6
                mov res, eax
            }
            if (res != 0) {
                if (g_P1_NegativeEdgeEnabled == g_P2_NegativeEdgeEnabled) {
                    if (!g_P1_NegativeEdgeEnabled) {
                        if (g_SimSeqType >= MACRO_SEQ_MODE8_F1_RAW_NEG_EDGE && g_SimSeqType <= MACRO_SEQ_MODE8_F8_STRING_BUFFER) {
                            int playerNum = ResolvePlayerNumber(nullptr, pPlayerInfo);
                            LogTrace("[RELEASE CHECK] >>> SUPPRESSED P%d Negative Edge Buffered Move (Tag 0x%08X) in 0x00454710! <<<", playerNum, tag);
                        }
                        return 0; // Release Check OFF: suppress buffered release match!
                    }
                } else {
                    int playerNum = ResolvePlayerNumber(nullptr, pPlayerInfo);
                    bool enabled = (playerNum == 1) ? g_P1_NegativeEdgeEnabled : (playerNum == 2 ? g_P2_NegativeEdgeEnabled : true);
                    if (!enabled && playerNum != 0) {
                        LogTrace("[RELEASE CHECK] >>> SUPPRESSED P%d Negative Edge Buffered Move (Tag 0x%08X) in 0x00454710! <<<", playerNum, tag);
                        return 0; // Release Check OFF: suppress buffered release match!
                    }
                }
                if (g_SimSeqType >= MACRO_SEQ_MODE8_F1_RAW_NEG_EDGE && g_SimSeqType <= MACRO_SEQ_MODE8_F8_STRING_BUFFER) {
                    int playerNum = ResolvePlayerNumber(nullptr, pPlayerInfo);
                    LogTrace("[BUFFER_CHECK_MATCH_SLOT6 | #%llu] P%d tag=0x%08X satisfied!", g_CurrentFrame, playerNum, tag);
                }
            }
            return res;
        }

        // ========================================================================
        // Install All Negative Edge Engine Hooks
        // ========================================================================
        bool InstallHooks() {
            bool success = true;

            // 1. Native MatchBufferedMove Release Query (0x00446950)
            if (MH_CreateHook((void*)0x00446950, (void*)hkCheckButtonReleasedTag_Buffer, (void**)&oCheckButtonReleasedTag_Buffer) == MH_OK) {
                MH_EnableHook((void*)0x00446950);
                LogTrace("[+] MatchBufferedMove Release Query Hooked (0x00446950 - Buffer Release Check Gate)");
            } else success = false;

            // 2. Script Command Evaluator (0x0088BAD0)
            if (MH_CreateHook((void*)0x0088bad0, (void*)hkIsCommandInputSatisfied, (void**)&oIsCommandInputSatisfied) == MH_OK) {
                MH_EnableHook((void*)0x0088bad0);
                LogTrace("[+] Script Command Evaluator Hooked (0x0088BAD0 - Negative Edge Bytecode Filter)");
            } else success = false;

            // 3. Script Command Button (0x0088A1D0)
            if (MH_CreateHook((void*)0x0088a1d0, (void*)hkGetSatisfiedInputButton, (void**)&oGetSatisfiedInputButton) == MH_OK) {
                MH_EnableHook((void*)0x0088a1d0);
                LogTrace("[+] Script Command Button Hooked (0x0088A1D0 - Negative Edge Return Filter)");
            } else success = false;

            // 4. Command Match & Execute Move (0x008A0600)
            if (MH_CreateHook((void*)0x008a0600, (void*)hkMatchAndExecuteMove, (void**)&oMatchAndExecuteMove) == MH_OK) {
                MH_EnableHook((void*)0x008a0600);
                LogTrace("[+] Command Match & Execute Hooked (0x008A0600 - Release Move Guard)");
            } else success = false;

            // 5. Check Buffered Move (0x00453A80 - VTable Slot 5)
            if (MH_CreateHook((void*)0x00453a80, (void*)hkCheckBufferedMove, (void**)&oCheckBufferedMove) == MH_OK) {
                MH_EnableHook((void*)0x00453a80);
                LogTrace("[+] Check Buffered Move Hooked (0x00453A80 - VTable Slot 5)");
            } else success = false;

            // 6. Check Buffered Move Slot 6 (0x00454710 - VTable Slot 6)
            if (MH_CreateHook((void*)0x00454710, (void*)hkCheckBufferedMove_Slot6, (void**)&oCheckBufferedMove_Slot6) == MH_OK) {
                MH_EnableHook((void*)0x00454710);
                LogTrace("[+] Check Buffered Move Slot 6 Hooked (0x00454710 - VTable Slot 6)");
            } else success = false;

            LogTrace("[+] Negative Edge (Release Check) Modular Subsystem Initialized successfully!");
            return success;
        }

    } // namespace NegativeEdge
} // namespace MK9TradeFix
