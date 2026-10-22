#include "extra_gameplay.hpp"
#include "engine_common.hpp"
#include "stun_modifier.hpp"
#include "combat_state.hpp"
#include "meter_drain_fix.hpp"
#include "collision_visualizer.hpp"
#include "input_recording.hpp"
#include "reversal_engine.hpp"
#include "imgui_overlay.hpp"
#include "minhook/include/MinHook.h"
#include <cmath>
#include <cstring>

namespace MK9TradeFix {
namespace ExtraGameplay {

    bool g_DisableBlood = false;
    bool g_DisableBackground = false;
    float g_VoidBgColor[3] = { 0.0f, 0.0f, 0.0f }; // Pure Black default (can be set to 0, 1, 0 for Green Screen)
    bool g_EnableFloorGrid = false;
    float g_FloorGridColor[3] = { 0.0f, 0.85f, 1.0f };
    float g_FloorGridCenterColor[3] = { 1.0f, 1.0f, 1.0f }; // Center lines default: Pure White
    bool g_ShowGridCornerWalls = true;
    float g_GridWallColor[3] = { 1.0f, 0.25f, 0.25f }; // High-visibility Warning Red
    int   g_LightingWarmupFrames = 15; // Lighting Probe Warmup Delay (Step 1 Frames before Step 2 Void Mode, default: 15 frames)
    bool g_DisableQuickWakeup = false;
    bool g_DisableRollWakeupAttack = false;
    bool g_DisableDelayedWakeup = false;
    bool g_DelayedWakeupFixedHold = false;
    int  g_DelayedWakeupFixedFrames = 60;
    bool g_DisableDelayedWakeupRoll = false;
    int  g_TargetScope = 0; // 0 = Both, 1 = P1 Only, 2 = P2 Only

    // Move Startup & Recovery Frame Pacing
    bool  g_EnableCustomStartupSpeed   = false;
    float g_StartupSpeedMultiplier     = 1.00f;
    bool  g_EnableCustomRecoveryFrames = false;
    int   g_RecoveryFramesDelta        = 0;
    int   g_MovePacingFilter           = 0; // 0 = All Moves, 1 = Normals Only, 2 = Specials Only

    struct PlayerWakeupState {
        bool wasKnockedDown = false;
        bool inFloorImpact = false;
        bool inProne = false;
        bool inGetup = false;
        bool isRolling = false;
        int  postRollLockoutFrames = 0;
        float floorContactX = 0.0f;
        
        bool wasDelayedProne = false;
        int  delayedProneCooldown = 0;
        
        bool fixedDelayedActive = false;
        int  fixedDelayedFrames = 0;
        int  fixedDelayedCooldown = 0;
    };

    static PlayerWakeupState s_P1State;
    static PlayerWakeupState s_P2State;

    // FName and Animation Name resolution helpers
    const char* GetFNameString(uint32_t nameIdx) {
        if (nameIdx == 0) return "";
        __try {
            uint32_t* gnames = *(uint32_t**)0x00E7C0A4;
            if (gnames && (uintptr_t)gnames > 0x00400000) {
                uint32_t chunk = *(uint32_t*)((uintptr_t)gnames + (nameIdx / 0x4000) * 4);
                if (chunk && (uintptr_t)chunk > 0x00400000) {
                    uint32_t entry = *(uint32_t*)(chunk + (nameIdx % 0x4000) * 4);
                    if (entry && (uintptr_t)entry > 0x00400000) {
                        return (const char*)(entry + 0x10);
                    }
                }
            }
            uint32_t* pGNames = *(uint32_t**)0x00E7C058;
            if (pGNames && (uintptr_t)pGNames > 0x00400000) {
                uint32_t entry = pGNames[nameIdx];
                if (entry && (uintptr_t)entry > 0x00400000) {
                    return (const char*)(entry + 0x10);
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
        return "";
    }

    const char* GetAnimSequenceName(void* pAnimSeq) {
        if (!pAnimSeq || (uintptr_t)pAnimSeq < 0x01000000) return "";
        __try {
            uint32_t nameIdx = *(uint32_t*)((uintptr_t)pAnimSeq + 0x38);
            if (nameIdx == 0) {
                nameIdx = *(uint32_t*)((uintptr_t)pAnimSeq + 0x28);
            }
            return GetFNameString(nameIdx);
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
        return "";
    }

    // Helper functions for safe memory and action analysis
    inline uint32_t GetCharacterAction(void* pChar) {
        if (!pChar || (uintptr_t)pChar < 0x01000000) return 0;
        __try {
            return *(uint32_t*)((uintptr_t)pChar + 0x1D4);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return 0;
        }
    }

    inline void* GetCharFromPlayerInfo(void* pPlayerInfo) {
        if (!pPlayerInfo || (uintptr_t)pPlayerInfo < 0x00400000) return nullptr;
        void* p1 = GetP1Char();
        void* p2 = GetP2Char();
        if (pPlayerInfo == p1) return p1;
        if (pPlayerInfo == p2) return p2;
        if (p1 && (uintptr_t)p1 > 0x01000000) {
            __try {
                void* p1Info = *(void**)((uintptr_t)p1 + 0x7638);
                if (pPlayerInfo == p1Info) return p1;
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
        if (p2 && (uintptr_t)p2 > 0x01000000) {
            __try {
                void* p2Info = *(void**)((uintptr_t)p2 + 0x7638);
                if (pPlayerInfo == p2Info) return p2;
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
        return nullptr;
    }

    inline bool IsKnockdownOrGetupAction(uint32_t action) {
        return (action >= 0x4200 && action <= 0x460F && action != 0x4205 && action != 0x4206 && action != 0x4600) || (action >= 0x6000 && action <= 0x62FF);
    }

    inline bool IsDelayedProneAction(uint32_t action) {
        return (action == 0x620A || action == 0x620B || action == 0x460D || (action >= 0x6000 && action <= 0x61FF));
    }

    inline bool IsRollGetupAction(uint32_t action) {
        return (action >= 0x6202 && action <= 0x6205);
    }

    // ========================================================================
    // Authoritative Action, Animation & Direction Evaluation Hooks
    // 0x008A4080: void __fastcall hkCombatPlayAnim(pChar, edxUnused, pAnimSeq, ...)
    // 0x006BA740: void __fastcall hkSkelMeshPlayAnim(pSkelComp, edxUnused, nameIndex, ...)
    // 0x008967B0: void __fastcall hkExecTechRoll(pChar, edxUnused, fParam)
    // 0x00446B60: void __fastcall hkSetCharacterAction(pChar, edxUnused, action)
    // 0x00446910: int __fastcall hkIsDirectionHeld_Core(pPlayerInfo, edxUnused, dirToken)
    // 0x00446540: int __fastcall hkIsAnyAttackButtonPressed(pPlayerInfo)
    // 0x004465A0: int __fastcall hkIsAnyAttackButtonHeld(pPlayerInfo)
    // 0x00453030: int __fastcall hkIsDirectionHeld_3030(pPlayerInfo, dirToken)
    // 0x00454310: int __fastcall hkIsDirectionHeld_4310(pPlayerInfo, dirToken)
    // 0x00454510: int __fastcall hkIsDirectionHeld_4510(pPlayerInfo, dirToken)
    // ========================================================================
    typedef void (__fastcall *tCombatPlayAnim)(void* pChar, void* edxUnused, void* pAnimSeq, float rate, void* p3, float blendIn, uint32_t flags, void* p6, void* p7);
    static tCombatPlayAnim oCombatPlayAnim = nullptr;

    typedef void (__fastcall *tSkelMeshPlayAnim)(void* pSkelComp, void* edxUnused, uint32_t nameIndex, float rate, float blendIn, int bLoop, float blendOut);
    static tSkelMeshPlayAnim oSkelMeshPlayAnim = nullptr;

    typedef void (__fastcall *tExecTechRoll)(void* pChar, void* edxUnused);
    static tExecTechRoll oExecTechRoll = nullptr;

    typedef void (__fastcall *tSetCharacterAction)(void* pChar, void* edxUnused, uint32_t action);
    static tSetCharacterAction oSetCharacterAction = nullptr;

    typedef int (__fastcall *tIsDirectionHeld)(void* pPlayerInfo, void* edxUnused, uint32_t dirToken);
    static tIsDirectionHeld oIsDirectionHeldCore = nullptr;
    static tIsDirectionHeld oIsDirectionHeld3030 = nullptr;
    static tIsDirectionHeld oIsDirectionHeld4310 = nullptr;
    static tIsDirectionHeld oIsDirectionHeld4510 = nullptr;

    // Neutral Getup Animation Cache per player
    static void* s_pNeutralGetupAnimP1 = nullptr;
    static void* s_pNeutralGetupAnimP2 = nullptr;
    static uint32_t s_NeutralGetupFName = 0;

    // 0x008A4080: Combat_PlayAnim - Root Combat Animation Dispatcher on pChar
    static void __fastcall hkCombatPlayAnim(void* pChar, void* edxUnused, void* pAnimSeq, float rate, void* p3, float blendIn, uint32_t flags, void* p6, void* p7) {
        if (!pChar || (uintptr_t)pChar < 0x01000000 || !pAnimSeq) {
            if (oCombatPlayAnim) oCombatPlayAnim(pChar, edxUnused, pAnimSeq, rate, p3, blendIn, flags, p6, p7);
            return;
        }

        int playerNum = GetPlayerNumber(pChar);
        if (!IsPlayerApplicable(playerNum)) {
            if (oCombatPlayAnim) oCombatPlayAnim(pChar, edxUnused, pAnimSeq, rate, p3, blendIn, flags, p6, p7);
            return;
        }

        PlayerWakeupState& state = (playerNum == 1) ? s_P1State : s_P2State;
        const char* animName = GetAnimSequenceName(pAnimSeq);

        // Cache neutral getup animation for this player whenever observed
        if (animName && animName[0] != '\0') {
            if ((strstr(animName, "Getup") || strstr(animName, "getup")) &&
                !strstr(animName, "Roll") && !strstr(animName, "roll")) {
                if (playerNum == 1) s_pNeutralGetupAnimP1 = pAnimSeq;
                else s_pNeutralGetupAnimP2 = pAnimSeq;
            }
        }

        // Detect if this is a Roll Getup animation
        bool isRollAnim = false;
        if (animName && animName[0] != '\0') {
            if (strstr(animName, "RollGetup") || strstr(animName, "rollgetup") ||
                strstr(animName, "Roll_Getup") || strstr(animName, "GetupRoll") ||
                strstr(animName, "Generic_RollGetup") ||
                ((strstr(animName, "Roll") || strstr(animName, "roll")) &&
                 (strstr(animName, "Getup") || strstr(animName, "getup") || state.wasKnockedDown || state.inFloorImpact || state.inProne))) {
                isRollAnim = true;
            }
        }

        if (isRollAnim) {
            state.isRolling = true;
            state.postRollLockoutFrames = 15;
            LogTrace("[EXTRA GAMEPLAY] P%d started Roll Animation '%s'! Option B lockout armed for %d frames.",
                     playerNum, animName ? animName : "Unknown", state.postRollLockoutFrames);

            // Feature 1: Disable Quick Wakeup (Tech Roll) - Remap roll animation to neutral getup
            if (g_DisableQuickWakeup) {
                LogTrace("[EXTRA GAMEPLAY] P%d Quick Wakeup (Tech Roll) BLOCKED! Remapping animation '%s' to neutral getup.",
                         playerNum, animName ? animName : "Unknown");

                // Zero horizontal displacement & root motion velocity so character stays in place
                __try {
                    if (state.floorContactX != 0.0f) {
                        *(float*)((uintptr_t)pChar + 0x1E8) = state.floorContactX;
                    }
                    *(float*)((uintptr_t)pChar + 0x2F74) = 0.0f;
                    *(uint32_t*)((uintptr_t)pChar + 0x1D4) = 0x6200;
                } __except (EXCEPTION_EXECUTE_HANDLER) {}

                void* pNeutralAnim = (playerNum == 1) ? s_pNeutralGetupAnimP1 : s_pNeutralGetupAnimP2;
                if (pNeutralAnim && oCombatPlayAnim) {
                    oCombatPlayAnim(pChar, edxUnused, pNeutralAnim, rate, p3, blendIn, flags, p6, p7);
                }
                return;
            }
        }

        float stunMult = StunModifier::GetAnimRateMultiplier(pChar, animName, GetCharacterAction(pChar));
        rate *= stunMult;

        MoveTelemetryHUD::NotifyCombatPlayAnim(pChar, pAnimSeq);
        if (animName && (strstr(animName, "Attack") || strstr(animName, "attack") || strstr(animName, "combo") || strstr(animName, "Punch") || strstr(animName, "Kick") || strstr(animName, "Sweep") || strstr(animName, "Uppercut"))) {
            MeterDrainFix::NotifyNormalAttack(playerNum);
        }

        if (oCombatPlayAnim) {
            oCombatPlayAnim(pChar, edxUnused, pAnimSeq, rate, p3, blendIn, flags, p6, p7);
        }
    }

    // 0x006BA740: USkeletalMeshComponent::PlayAnim - Skeletal Mesh Animation Engine
    static void __fastcall hkSkelMeshPlayAnim(void* pSkelComp, void* edxUnused, uint32_t nameIndex, float rate, float blendIn, int bLoop, float blendOut) {
        if (!pSkelComp || nameIndex == 0) {
            if (oSkelMeshPlayAnim) oSkelMeshPlayAnim(pSkelComp, edxUnused, nameIndex, rate, blendIn, bLoop, blendOut);
            return;
        }

        const char* nameStr = GetFNameString(nameIndex);
        if (nameStr && nameStr[0] != '\0') {
            if ((strstr(nameStr, "Getup") || strstr(nameStr, "getup")) &&
                !strstr(nameStr, "Roll") && !strstr(nameStr, "roll")) {
                s_NeutralGetupFName = nameIndex;
            }

            if (strstr(nameStr, "RollGetup") || strstr(nameStr, "rollgetup") ||
                strstr(nameStr, "Generic_RollGetup") || strstr(nameStr, "GetupRoll")) {
                if (g_DisableQuickWakeup) {
                    if (s_NeutralGetupFName != 0) {
                        nameIndex = s_NeutralGetupFName;
                    } else {
                        return; // Do NOT play roll animation if neutral getup FName not yet known!
                    }
                }
            }
        }

        MoveTelemetryHUD::NotifySkelMeshPlayAnim(pSkelComp, nameIndex);

        if (oSkelMeshPlayAnim) {
            oSkelMeshPlayAnim(pSkelComp, edxUnused, nameIndex, rate, blendIn, bLoop, blendOut);
        }
    }

    // 0x008967B0: ExecTechRoll - Engine root function that pushes 0x5200 and configures roll velocity
    static void __fastcall hkExecTechRoll(void* pChar, void* edxUnused) {
        if (pChar && (uintptr_t)pChar > 0x01000000) {
            int playerNum = GetPlayerNumber(pChar);
            if (IsPlayerApplicable(playerNum)) {
                // Feature 1: Disable Quick Wakeup (Tech Roll)
                if (g_DisableQuickWakeup) {
                    LogTrace("[EXTRA GAMEPLAY] P%d attempted Quick Wakeup (Tech Roll) - BLOCKED by hkExecTechRoll!", playerNum);
                    if (oSetCharacterAction) {
                        oSetCharacterAction(pChar, edxUnused, 0x6200);
                    }
                    return; // Suppress roll execution! Character remains in standard knockdown getup
                }

                // Option B: Track roll initiation when quick roll is executed
                PlayerWakeupState& state = (playerNum == 1) ? s_P1State : s_P2State;
                state.isRolling = true;
                state.postRollLockoutFrames = 15;
                LogTrace("[EXTRA GAMEPLAY] P%d executed Tech Roll! Option B roll lockout armed for %d frames.", playerNum, state.postRollLockoutFrames);
            }
        }
        if (oExecTechRoll) {
            oExecTechRoll(pChar, edxUnused);
        }
    }

    // 0x00446B60: SetCharacterAction - Root action dispatcher that sets pChar+0x1D4
    static void __fastcall hkSetCharacterAction(void* pChar, void* edxUnused, uint32_t action) {
        if (!MK9TradeFix::IsMatchActive() || !pChar || (uintptr_t)pChar < 0x01000000) {
            if (oSetCharacterAction) oSetCharacterAction(pChar, edxUnused, action);
            return;
        }

        int playerNum = GetPlayerNumber(pChar);
        if (!IsPlayerApplicable(playerNum)) {
            if (oSetCharacterAction) oSetCharacterAction(pChar, edxUnused, action);
            return;
        }

        PlayerWakeupState& state = (playerNum == 1) ? s_P1State : s_P2State;
        uint32_t currentAction = GetCharacterAction(pChar);
        uint32_t effectiveAction = action;

        // Feature 1: Disable Quick Wakeup (Tech Roll) - Remap any roll action to neutral getup
        if (g_DisableQuickWakeup) {
            if (effectiveAction == 0x5200) {
                LogTrace("[EXTRA GAMEPLAY] P%d attempted Action 0x5200 - Suppressed by SetCharacterAction!", playerNum);
                effectiveAction = 0x6200;
                __try {
                    *(float*)((uintptr_t)pChar + 0x2F74) = 0.0f;
                    if (state.floorContactX != 0.0f) {
                        *(float*)((uintptr_t)pChar + 0x1E8) = state.floorContactX;
                    }
                } __except (EXCEPTION_EXECUTE_HANDLER) {}
            } else if (effectiveAction == 0x6202 || effectiveAction == 0x6203) {
                LogTrace("[EXTRA GAMEPLAY] P%d attempted Face-Up Roll Getup (0x%04X) - Remapped to Neutral (0x6200)!", playerNum, effectiveAction);
                effectiveAction = 0x6200;
                __try {
                    *(float*)((uintptr_t)pChar + 0x2F74) = 0.0f;
                    if (state.floorContactX != 0.0f) {
                        *(float*)((uintptr_t)pChar + 0x1E8) = state.floorContactX;
                    }
                } __except (EXCEPTION_EXECUTE_HANDLER) {}
            } else if (effectiveAction == 0x6204 || effectiveAction == 0x6205) {
                LogTrace("[EXTRA GAMEPLAY] P%d attempted Face-Down Roll Getup (0x%04X) - Remapped to Neutral (0x6201)!", playerNum, effectiveAction);
                effectiveAction = 0x6201;
                __try {
                    *(float*)((uintptr_t)pChar + 0x2F74) = 0.0f;
                    if (state.floorContactX != 0.0f) {
                        *(float*)((uintptr_t)pChar + 0x1E8) = state.floorContactX;
                    }
                } __except (EXCEPTION_EXECUTE_HANDLER) {}
            }
        }

        // Feature 5: Disable Delayed Wakeup Roll
        if (g_DisableDelayedWakeupRoll && !g_DisableQuickWakeup) {
            bool inDelayed = (IsDelayedProneAction(currentAction) || state.inProne || state.wasDelayedProne || state.delayedProneCooldown > 0);
            if (inDelayed) {
                if (effectiveAction == 0x5200 || effectiveAction == 0x6202 || effectiveAction == 0x6203) {
                    LogTrace("[EXTRA GAMEPLAY] P%d attempted Delayed Roll Getup (0x%04X) - Remapped to Neutral (0x6200)!", playerNum, effectiveAction);
                    effectiveAction = 0x6200;
                    __try {
                        *(float*)((uintptr_t)pChar + 0x2F74) = 0.0f;
                        if (state.floorContactX != 0.0f) {
                            *(float*)((uintptr_t)pChar + 0x1E8) = state.floorContactX;
                        }
                    } __except (EXCEPTION_EXECUTE_HANDLER) {}
                } else if (effectiveAction == 0x6204 || effectiveAction == 0x6205) {
                    LogTrace("[EXTRA GAMEPLAY] P%d attempted Delayed Roll Getup (0x%04X) - Remapped to Neutral (0x6201)!", playerNum, effectiveAction);
                    effectiveAction = 0x6201;
                    __try {
                        *(float*)((uintptr_t)pChar + 0x2F74) = 0.0f;
                        if (state.floorContactX != 0.0f) {
                            *(float*)((uintptr_t)pChar + 0x1E8) = state.floorContactX;
                        }
                    } __except (EXCEPTION_EXECUTE_HANDLER) {}
                }
            }
        }

        // Track roll action entry for Option B (Disable Wakeup Attack out of Quick Roll)
        // Note: 0x5200 is also standard standing attack strike action. Only treat as roll if player was in knockdown/prone.
        if (((effectiveAction == 0x5200) && (state.wasKnockedDown || state.inFloorImpact || state.inProne || state.isRolling)) ||
            (effectiveAction >= 0x6202 && effectiveAction <= 0x6205)) {
            state.isRolling = true;
            state.postRollLockoutFrames = 15;
            LogTrace("[EXTRA GAMEPLAY] P%d entered Roll Action 0x%04X! Option B lockout active for %d frames.",
                     playerNum, effectiveAction, state.postRollLockoutFrames);
        }

        MeterDrainFix::NotifyActionState(playerNum, effectiveAction);

        if (oSetCharacterAction) {
            oSetCharacterAction(pChar, edxUnused, effectiveAction);
        }
    }

    // ========================================================================
    // ProcessDownQuery: Central Down evaluation for Delayed Wakeup features
    // ========================================================================
    static int ProcessDownQuery(void* pChar, int originalResult) {
        if (!pChar || (uintptr_t)pChar < 0x01000000) return originalResult;

        int playerNum = GetPlayerNumber(pChar);
        if (!IsPlayerApplicable(playerNum)) return originalResult;

        uint32_t action = GetCharacterAction(pChar);
        PlayerWakeupState& state = (playerNum == 1) ? s_P1State : s_P2State;

        bool inProneOrKD = IsDelayedProneAction(action) || (action == 0x460D) || 
                            IsKnockdownOrGetupAction(action) || 
                            state.inFloorImpact || state.inProne;

        // Feature 3: Disable Delayed Wakeup
        if (g_DisableDelayedWakeup && inProneOrKD) {
            return 0; // Tell engine Down is NOT held so immediate getup proceeds!
        }

        // Feature 4: Delayed Wakeup: Fixed Amount of Hold when initiated
        if (g_DelayedWakeupFixedHold && !g_DisableDelayedWakeup) {
            bool realDown = (originalResult != 0);
            if (!state.fixedDelayedActive && state.fixedDelayedCooldown == 0 && (inProneOrKD && realDown)) {
                state.fixedDelayedActive = true;
                state.fixedDelayedFrames = 0;
                LogTrace("[EXTRA GAMEPLAY] P%d initiated Delayed Wakeup! Fixed hold locked for %d frames.",
                         playerNum, g_DelayedWakeupFixedFrames);
            }

            if (state.fixedDelayedActive) {
                if (state.fixedDelayedFrames < g_DelayedWakeupFixedFrames) {
                    return 1; // Force Down held!
                } else {
                    state.fixedDelayedActive = false;
                    state.fixedDelayedCooldown = 30; // Prevent immediate re-latch
                    return 0; // Fixed duration elapsed -> force Down released so getup begins!
                }
            } else if (state.fixedDelayedCooldown > 0) {
                return 0; // Force Down released during cooldown
            }
        }

        return originalResult;
    }

    // Direction Held evaluation logic shared across 0x00453030, 0x00454310, 0x00454510
    static int HandleDirectionHeld(void* pPlayerInfo, uint32_t dirToken, int orig) {
        if (!MK9TradeFix::IsMatchActive()) return orig;
        void* pChar = GetCharFromPlayerInfo(pPlayerInfo);
        if (!pChar || (uintptr_t)pChar < 0x01000000) return orig;

        int playerNum = GetPlayerNumber(pChar);
        if (StunModifier::IsInExtendedStun(playerNum)) {
            return 0; // Completely block all directional inputs during extended stun
        }
        if (!IsPlayerApplicable(playerNum)) return orig;

        PlayerWakeupState& state = (playerNum == 1) ? s_P1State : s_P2State;
        uint32_t action = GetCharacterAction(pChar);

        // Feature 4: While fixed delayed hold is active, HARD-SUPPRESS ALL HORIZONTAL AND ROLL DIRECTIONS!
        // The character is in a fixed hold-down state; allowing back/forward/roll directions causes it to become a roll!
        if (state.fixedDelayedActive) {
            if (dirToken == 0x80000004 || dirToken == 0x04) {
                return ProcessDownQuery(pChar, orig);
            }
            if (dirToken == 0x80000001 || dirToken == 0x80000002 ||
                dirToken == 0x8000001F || dirToken == 0x80000020 ||
                dirToken == 0x80000027 || dirToken == 0x80000028 ||
                dirToken == 0x8000002A || dirToken == 0x80000023 ||
                dirToken == 0x01 || dirToken == 0x02 || dirToken == 0x1F || dirToken == 0x20 ||
                dirToken == 0x27 || dirToken == 0x28 || dirToken == 0x2A || dirToken == 0x23) {
                return 0; // Pure DOWN only! Never allow roll during fixed delayed hold!
            }
        }

        // Feature 1: Disable Quick Wakeup (Tech Roll)
        // Block horizontal / roll directions during knockdown, floor impact, or prone
        if (g_DisableQuickWakeup && (state.wasKnockedDown || state.inFloorImpact || state.inProne || (action >= 0x4200 && action <= 0x460F))) {
            if (dirToken == 0x80000001 || dirToken == 0x80000002 ||
                dirToken == 0x8000001F || dirToken == 0x80000020 ||
                dirToken == 0x80000023 || dirToken == 0x80000027 || dirToken == 0x80000028 ||
                dirToken == 0x01 || dirToken == 0x02 || dirToken == 0x1F || dirToken == 0x20 ||
                dirToken == 0x23 || dirToken == 0x27 || dirToken == 0x28) {
                return 0;
            }
        }

        // Feature 5: Disable Delayed Wakeup Roll
        // Block all Away / Back directions (including Down-Away and Up-Away) when rising out of delayed prone
        if (g_DisableDelayedWakeupRoll && (IsDelayedProneAction(action) || state.inProne || state.wasDelayedProne || state.delayedProneCooldown > 0)) {
            if (dirToken == 0x80000002 || dirToken == 0x80000028 || dirToken == 0x8000001F || dirToken == 0x80000027 ||
                dirToken == 0x02 || dirToken == 0x28 || dirToken == 0x1F || dirToken == 0x27) {
                return 0;
            }
        }

        // Down directions (0x80000004 = Down, 0x8000001F = Down-Away, 0x80000020 = Down-Towards)
        if (dirToken == 0x80000004 || dirToken == 0x8000001F || dirToken == 0x80000020 || dirToken == 0x04) {
            return ProcessDownQuery(pChar, orig);
        }

        return orig;
    }

    // 0x00446910: IsDirectionHeld (Core non-virtual)
    static int __fastcall hkIsDirectionHeld_Core(void* pPlayerInfo, void* edxUnused, uint32_t dirToken) {
        int orig = oIsDirectionHeldCore ? oIsDirectionHeldCore(pPlayerInfo, edxUnused, dirToken) : 0;
        return HandleDirectionHeld(pPlayerInfo, dirToken, orig);
    }

    // 0x00453030: IsDirectionHeld (vtable[+0x1C])
    static int __fastcall hkIsDirectionHeld_3030(void* pPlayerInfo, void* edxUnused, uint32_t dirToken) {
        int orig = oIsDirectionHeld3030 ? oIsDirectionHeld3030(pPlayerInfo, edxUnused, dirToken) : 0;
        return HandleDirectionHeld(pPlayerInfo, dirToken, orig);
    }

    // 0x00454310: IsDirectionHeld (vtable[+0x24])
    static int __fastcall hkIsDirectionHeld_4310(void* pPlayerInfo, void* edxUnused, uint32_t dirToken) {
        int orig = oIsDirectionHeld4310 ? oIsDirectionHeld4310(pPlayerInfo, edxUnused, dirToken) : 0;
        return HandleDirectionHeld(pPlayerInfo, dirToken, orig);
    }

    // 0x00454510: IsDirectionHeld (vtable[+0x28])
    static int __fastcall hkIsDirectionHeld_4510(void* pPlayerInfo, void* edxUnused, uint32_t dirToken) {
        int orig = oIsDirectionHeld4510 ? oIsDirectionHeld4510(pPlayerInfo, edxUnused, dirToken) : 0;
        return HandleDirectionHeld(pPlayerInfo, dirToken, orig);
    }

    bool ShouldSuppressDirectionHeld(void* pPlayerInfo, uint32_t dirToken) {
        return (HandleDirectionHeld(pPlayerInfo, dirToken, 1) == 0);
    }

    // Direction Pressed check (invoked from hkCheckBufferedMove and hkCheckBufferedMove_Slot6 in negative_edge.cpp)
    bool ShouldSuppressDirectionPressed(void* pPlayerInfo, uint32_t tag) {
        if (!MK9TradeFix::IsMatchActive()) return false;
        void* pChar = GetCharFromPlayerInfo(pPlayerInfo);
        if (!pChar || (uintptr_t)pChar < 0x01000000) return false;

        int playerNum = GetPlayerNumber(pChar);
        if (StunModifier::IsInExtendedStun(playerNum)) {
            return true; // Block directional presses during extended stun
        }
        if (!IsPlayerApplicable(playerNum)) return false;

        const PlayerWakeupState& state = (playerNum == 1) ? s_P1State : s_P2State;
        uint32_t action = GetCharacterAction(pChar);

        // Feature 4: While fixed delayed hold is active, suppress all horizontal/roll presses!
        if (state.fixedDelayedActive) {
            if (tag == 0x80000001 || tag == 0x80000002 ||
                tag == 0x8000001F || tag == 0x80000020 ||
                tag == 0x80000023 || tag == 0x80000027 || tag == 0x80000028 || tag == 0x8000002A ||
                tag == 0x01 || tag == 0x02 || tag == 0x1F || tag == 0x20 ||
                tag == 0x23 || tag == 0x27 || tag == 0x28 || tag == 0x2A) {
                return true;
            }
        }

        // Feature 1: Disable Quick Wakeup (Tech Roll)
        if (g_DisableQuickWakeup && (state.wasKnockedDown || state.inFloorImpact || state.inProne || (action >= 0x4200 && action <= 0x460F))) {
            if (tag == 0x80000001 || tag == 0x80000002 ||
                tag == 0x80000023 || tag == 0x80000027 || tag == 0x80000028 ||
                tag == 0x01 || tag == 0x02 || tag == 0x23 || tag == 0x27 || tag == 0x28) {
                return true;
            }
        }

        // Feature 5: Disable Delayed Wakeup Roll (covers Back and Down-Back)
        if (g_DisableDelayedWakeupRoll && (IsDelayedProneAction(action) || state.inProne || state.wasDelayedProne || state.delayedProneCooldown > 0)) {
            if (tag == 0x80000002 || tag == 0x80000028 || tag == 0x8000001F || tag == 0x80000027 ||
                tag == 0x02 || tag == 0x28 || tag == 0x1F || tag == 0x27) {
                return true;
            }
        }

        return false;
    }

    // ============================================================================
    // Blood & Battle Damage Suppression Hooks (Versus Kode 909-909 / Clean Immersion)
    // [EXPERIMENTAL / BUG QUEUE INVESTIGATION - See docs/BUG_QUEUE_FIX.md]
    // 0x0084E1E0 and 0x00830300 are __thiscall member initializers for HUD/combat objects.
    // Early returns bypassed required object initialization ([esi+8], [esi+0xC]), causing
    // an Access Violation crash upon post-match deallocation/teardown.
    // The native engine already contains internal checks for [0x00E04B04] in both functions!
    // We forward to original functions while OnFrameTick synchronizes the native engine flag.
    // ============================================================================
    // 1. Main Hit Blood Particle Spawner (0x00830300)
    typedef void (__cdecl *tSpawnHitBlood)(void* param1, void* param2);
    static tSpawnHitBlood oSpawnHitBlood = nullptr;

    static void __cdecl hkSpawnHitBlood(void* param1, void* param2) {
        // Native engine checks [0x00E04B04] at 0x00830318 and jumps cleanly. Always call original.
        if (oSpawnHitBlood) {
            oSpawnHitBlood(param1, param2);
        }
    }

    // 2. Hit Blood Splat (0x0084E1E0)
    typedef void (__thiscall *tSpawnHitBloodSplat)(void* thisPtr, uint32_t param);
    static tSpawnHitBloodSplat oSpawnHitBloodSplat = nullptr;

    static void __fastcall hkSpawnHitBloodSplat(void* thisPtr, void* edxUnused, uint32_t param) {
        // Native engine initializes [esi+8], [esi+0xC] before checking [0x00E04B04]. Always call original.
        if (oSpawnHitBloodSplat) {
            oSpawnHitBloodSplat(thisPtr, param);
        }
    }

    // 4. Blood Drip Spawner (0x0084DE20) - Mid-instruction offset; suppressed from hooking.

    // 5. Native 3D Debug Line Spawner for Colored Floor Grid (0x00722B30)
    typedef void (__stdcall *tDrawDebugLineNative)(
        float sx, float sy, float sz,
        float ex, float ey, float ez,
        uint8_t r, uint8_t g, uint8_t b,
        int bPersistent
    );
    static tDrawDebugLineNative fnDrawGridLine = (tDrawDebugLineNative)0x00722B30;

    static float s_FixedFloorZ = 0.0f;
    static bool  s_FloorCaptured = false;

    void RenderFloorGrid() {
        if (!g_EnableFloorGrid || !fnDrawGridLine || !IsMatchLoadedUp()) {
            s_FloorCaptured = false;
            return;
        }
        void* p1 = GetP1Char();
        void* p2 = GetP2Char();
        if (!p1 || (uintptr_t)p1 < 0x01000000) {
            s_FloorCaptured = false;
            return;
        }

        __try {
            uintptr_t pEngine = *(uintptr_t*)0x00E86AB0;
            if (!pEngine || (uintptr_t)pEngine < 0x01000000) return;
            uintptr_t pBatcher = *(uintptr_t*)(pEngine + 0x140);
            if (!pBatcher || (uintptr_t)pBatcher < 0x01000000) return;

            EnsureLineBatcherVisible();

            // 1. Stage floor height capture from grounded character
            uint32_t a1 = GetCharacterAction(p1);
            float curZ = *(float*)((uintptr_t)p1 + 0x1F0);
            if (!s_FloorCaptured || (a1 <= 0x0101 && std::isfinite(curZ))) {
                if (std::isfinite(curZ) && fabsf(curZ) < 500000.0f) {
                    s_FixedFloorZ = curZ;
                }
                s_FloorCaptured = true;
            }

            // 2. Authoritative fight plane origin (0x00DF0BEC) and lateral unit axis (0x00DF0C14)
            float ox = 0.0f, oy = 0.0f;
            float ax = 0.0f, ay = 1.0f; // Default lateral along Y

            if (!IsBadReadPtr((void*)0x00DF0BEC, 12)) {
                float px = *(float*)0x00DF0BEC;
                float py = *(float*)(0x00DF0BEC + 4);
                if (std::isfinite(px) && std::isfinite(py) && fabsf(px) < 500000.0f && fabsf(py) < 500000.0f) {
                    ox = px;
                    oy = py;
                }
            }
            if (!IsBadReadPtr((void*)0x00DF0C14, 12)) {
                float vax = *(float*)0x00DF0C14;
                float vay = *(float*)(0x00DF0C14 + 4);
                float len = sqrtf(vax * vax + vay * vay);
                if (std::isfinite(len) && len > 0.0001f) {
                    ax = vax / len;
                    ay = vay / len;
                }
            }

            // Dynamic stage bounds from authoritative engine globals:
            // 0x00DF0BEC is Left Wall Point A (u = 0).
            // 0x00DF0C20 is Total Stage Length (e.g. 2600.0 on standard stages, 3200.0 on Bell Tower/Soul Chamber).
            // 0x00DF0BE4 is Stage Half-Width / Center Offset (0.5 * Length).
            float halfWidth = 1300.0f;
            if (!IsBadReadPtr((void*)0x00DF0BE4, 4)) {
                float hw = *(float*)0x00DF0BE4;
                if (std::isfinite(hw) && hw > 400.0f && hw < 10000.0f) {
                    halfWidth = hw;
                }
            } else if (!IsBadReadPtr((void*)0x00DF0C20, 4)) {
                float tw = *(float*)0x00DF0C20;
                if (std::isfinite(tw) && tw > 800.0f && tw < 20000.0f) {
                    halfWidth = tw * 0.5f;
                }
            }

            // Shift origin to true stage center C: C = PointA + halfWidth * A
            ox += halfWidth * ax;
            oy += halfWidth * ay;

            // Normal perpendicular vector in floor plane (depth axis)
            float nx = -ay;
            float ny = ax;

            float floorZ = s_FixedFloorZ;

            int iR = (int)(g_FloorGridColor[0] * 255.0f);
            int iG = (int)(g_FloorGridColor[1] * 255.0f);
            int iB = (int)(g_FloorGridColor[2] * 255.0f);
            uint8_t uR = (uint8_t)(iR < 0 ? 0 : (iR > 255 ? 255 : iR));
            uint8_t uG = (uint8_t)(iG < 0 ? 0 : (iG > 255 ? 255 : iG));
            uint8_t uB = (uint8_t)(iB < 0 ? 0 : (iB > 255 ? 255 : iB));

            int cR = (int)(g_FloorGridCenterColor[0] * 255.0f);
            int cG = (int)(g_FloorGridCenterColor[1] * 255.0f);
            int cB = (int)(g_FloorGridCenterColor[2] * 255.0f);
            uint8_t centerR = (uint8_t)(cR < 0 ? 0 : (cR > 255 ? 255 : cR));
            uint8_t centerG = (uint8_t)(cG < 0 ? 0 : (cG > 255 ? 255 : cG));
            uint8_t centerB = (uint8_t)(cB < 0 ? 0 : (cB > 255 ? 255 : cB));

            // Extended arena grid covers full visible stage area to stage limits
            static const float kLatExtent  = 3800.0f;  // Lateral ±3800 along fighting axis
            static const float kDepthHalf  = 800.0f;   // Depth ±800 perpendicular to fighting axis
            static const float kLatStep    = 60.0f;
            static const float kDepthStep  = 50.0f;

            // Draw longitudinal lines along fighting axis (A)
            for (float v = -kDepthHalf; v <= kDepthHalf; v += kDepthStep) {
                bool isCenter = (fabsf(v) < 1.0f);
                uint8_t r = isCenter ? centerR : uR;
                uint8_t g = isCenter ? centerG : uG;
                uint8_t b = isCenter ? centerB : uB;

                uint8_t dimR = isCenter ? (uint8_t)(centerR / 2) : (uint8_t)(uR / 4);
                uint8_t dimG = isCenter ? (uint8_t)(centerG / 2) : (uint8_t)(uG / 4);
                uint8_t dimB = isCenter ? (uint8_t)(centerB / 2) : (uint8_t)(uB / 4);

                // In-bounds playable section: -halfWidth to +halfWidth
                float inX1 = ox - halfWidth * ax + v * nx;
                float inY1 = oy - halfWidth * ay + v * ny;
                float inX2 = ox + halfWidth * ax + v * nx;
                float inY2 = oy + halfWidth * ay + v * ny;
                fnDrawGridLine(inX1, inY1, floorZ, inX2, inY2, floorZ, r, g, b, 0);

                // Left out-of-bounds: -kLatExtent to -halfWidth
                float oLeftX1 = ox - kLatExtent * ax + v * nx;
                float oLeftY1 = oy - kLatExtent * ay + v * ny;
                fnDrawGridLine(oLeftX1, oLeftY1, floorZ, inX1, inY1, floorZ, dimR, dimG, dimB, 0);

                // Right out-of-bounds: +halfWidth to +kLatExtent
                float oRightX2 = ox + kLatExtent * ax + v * nx;
                float oRightY2 = oy + kLatExtent * ay + v * ny;
                fnDrawGridLine(inX2, inY2, floorZ, oRightX2, oRightY2, floorZ, dimR, dimG, dimB, 0);
            }

            // Draw transverse lines perpendicular to fighting axis (N)
            for (float u = -kLatExtent; u <= kLatExtent; u += kLatStep) {
                bool isCenter = (fabsf(u) < 1.0f);
                bool isOutOfBounds = (fabsf(u) > (halfWidth + 0.5f));
                uint8_t r, g, b;
                if (isCenter) {
                    r = centerR; g = centerG; b = centerB;
                } else if (isOutOfBounds) {
                    r = (uint8_t)(uR / 4); g = (uint8_t)(uG / 4); b = (uint8_t)(uB / 4);
                } else {
                    r = uR; g = uG; b = uB;
                }

                float x1 = ox + u * ax - kDepthHalf * nx;
                float y1 = oy + u * ay - kDepthHalf * ny;
                float x2 = ox + u * ax + kDepthHalf * nx;
                float y2 = oy + u * ay + kDepthHalf * ny;

                fnDrawGridLine(x1, y1, floorZ, x2, y2, floorZ, r, g, b, 0);
            }

            // 3. Draw Corner Wall Boundaries (u = ±halfWidth)
            if (g_ShowGridCornerWalls) {
                int wR = (int)(g_GridWallColor[0] * 255.0f);
                int wG = (int)(g_GridWallColor[1] * 255.0f);
                int wB = (int)(g_GridWallColor[2] * 255.0f);
                uint8_t wallR = (uint8_t)(wR < 0 ? 0 : (wR > 255 ? 255 : wR));
                uint8_t wallG = (uint8_t)(wG < 0 ? 0 : (wG > 255 ? 255 : wG));
                uint8_t wallB = (uint8_t)(wB < 0 ? 0 : (wB > 255 ? 255 : wB));

                float wallPositions[2] = { -halfWidth, halfWidth };
                static const float kWallHeight = 360.0f;
                static const float kWallDepthExtent = 260.0f; // Controlled depth so cage never intersects camera plane
                static const float kPostSpacing = 130.0f;

                for (int side = 0; side < 2; ++side) {
                    float uWall = wallPositions[side];
                    float sign = (uWall > 0.0f) ? 1.0f : -1.0f;

                    // A. Prominent triple floor boundary line
                    float uOffsets[3] = { 0.0f, -6.0f * sign, -12.0f * sign };
                    for (int o = 0; o < 3; ++o) {
                        float curU = uWall + uOffsets[o];
                        float fx1 = ox + curU * ax - kWallDepthExtent * nx;
                        float fy1 = oy + curU * ay - kWallDepthExtent * ny;
                        float fx2 = ox + curU * ax + kWallDepthExtent * nx;
                        float fy2 = oy + curU * ay + kWallDepthExtent * ny;
                        fnDrawGridLine(fx1, fy1, floorZ, fx2, fy2, floorZ, wallR, wallG, wallB, 0);
                    }

                    // B. Vertical Corner Wall Pillars across stage depth
                    for (float v = -kWallDepthExtent; v <= kWallDepthExtent; v += kPostSpacing) {
                        bool isFightPlane = (fabsf(v) < 1.0f);
                        uint8_t pr = isFightPlane ? 255 : wallR;
                        uint8_t pg = isFightPlane ? 255 : wallG;
                        uint8_t pb = isFightPlane ? 255 : wallB;

                        float px = ox + uWall * ax + v * nx;
                        float py = oy + uWall * ay + v * ny;

                        // Vertical barrier pillar from floorZ to floorZ + kWallHeight
                        fnDrawGridLine(px, py, floorZ, px, py, floorZ + kWallHeight, pr, pg, pb, 0);
                    }

                    // C. Horizontal Barrier Rails at multiple heights (waist, head, top rim)
                    static const float kRailHeights[3] = { 120.0f, 240.0f, 360.0f };
                    for (int r = 0; r < 3; ++r) {
                        float rz = kRailHeights[r];
                        float rx1 = ox + uWall * ax - kWallDepthExtent * nx;
                        float ry1 = oy + uWall * ay - kWallDepthExtent * ny;
                        float rx2 = ox + uWall * ax + kWallDepthExtent * nx;
                        float ry2 = oy + uWall * ay + kWallDepthExtent * ny;
                        fnDrawGridLine(rx1, ry1, floorZ + rz, rx2, ry2, floorZ + rz, wallR, wallG, wallB, 0);
                    }

                    // D. Diagonal cross-bracing for cage grid aesthetic
                    for (float v = -kWallDepthExtent; v < kWallDepthExtent; v += kPostSpacing * 2.0f) {
                        float vNext = v + kPostSpacing * 2.0f;
                        float xA1 = ox + uWall * ax + v * nx;
                        float yA1 = oy + uWall * ay + v * ny;
                        float xA2 = ox + uWall * ax + vNext * nx;
                        float yA2 = oy + uWall * ay + vNext * ny;

                        fnDrawGridLine(xA1, yA1, floorZ, xA2, yA2, floorZ + 240.0f, (uint8_t)(wallR / 2), (uint8_t)(wallG / 2), (uint8_t)(wallB / 2), 0);
                        fnDrawGridLine(xA2, yA2, floorZ, xA1, yA1, floorZ + 240.0f, (uint8_t)(wallR / 2), (uint8_t)(wallG / 2), (uint8_t)(wallB / 2), 0);
                    }
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    static bool s_PrevDisableBackground = false;

    void UpdateBackgroundVisibility(bool forceUpdate) {
        bool in3DMatch = IsMatchLoadedUp();
        bool effectiveDisable = (g_DisableBackground && in3DMatch);

        if (!forceUpdate && effectiveDisable == s_PrevDisableBackground) return;
        s_PrevDisableBackground = effectiveDisable;
        LogTrace("[EXTRA GAMEPLAY] Stage background mode: %s",
                 effectiveDisable ? "VOID (BLACK SCREEN ACTIVE)" : "NORMAL (STAGE VISIBLE)");
    }

    bool IsSoulStealOngoing() {
        return MK9TradeFix::IsSoulStealActiveAndHit();
    }

    typedef void(__thiscall* tResetForPractice)(void* pThis);
    static tResetForPractice oResetForPractice = nullptr;

    static void __fastcall hkResetForPractice(void* pThis, void* edx) {
        if (oResetForPractice) {
            oResetForPractice(pThis);
        }
        TriggerLightingProbeWarmup();
    }

    void Initialize() {
        s_P1State = PlayerWakeupState();
        s_P2State = PlayerWakeupState();

#if 0
        // [STASHED] 1. Hook Combat_PlayAnim (0x008A4080) - Root Combat Animation Dispatcher on pChar
        if (MH_CreateHook((void*)0x008A4080, (void*)hkCombatPlayAnim, (void**)&oCombatPlayAnim) == MH_OK) {
            MH_EnableHook((void*)0x008A4080);
            LogTrace("[EXTRA GAMEPLAY] Hooked Combat_PlayAnim at 0x008A4080");
        } else {
            LogTrace("[EXTRA GAMEPLAY] Failed to hook Combat_PlayAnim at 0x008A4080");
        }

        // [STASHED] 2. Hook USkeletalMeshComponent::PlayAnim (0x006BA740) - Skeletal Mesh Animation Engine
        if (MH_CreateHook((void*)0x006BA740, (void*)hkSkelMeshPlayAnim, (void**)&oSkelMeshPlayAnim) == MH_OK) {
            MH_EnableHook((void*)0x006BA740);
            LogTrace("[EXTRA GAMEPLAY] Hooked USkeletalMeshComponent::PlayAnim at 0x006BA740");
        } else {
            LogTrace("[EXTRA GAMEPLAY] Failed to hook USkeletalMeshComponent::PlayAnim at 0x006BA740");
        }

        // [STASHED] 3. Hook ExecTechRoll (0x008967B0) - Authoritative Tech Roll trigger
        if (MH_CreateHook((void*)0x008967B0, (void*)hkExecTechRoll, (void**)&oExecTechRoll) == MH_OK) {
            MH_EnableHook((void*)0x008967B0);
            LogTrace("[EXTRA GAMEPLAY] Hooked ExecTechRoll at 0x008967B0");
        } else {
            LogTrace("[EXTRA GAMEPLAY] Failed to hook ExecTechRoll at 0x008967B0");
        }
#endif

        // 4. Hook SetCharacterAction (0x00446B60) - Authoritative Action Dispatcher
        if (MH_CreateHook((void*)0x00446B60, (void*)hkSetCharacterAction, (void**)&oSetCharacterAction) == MH_OK) {
            MH_EnableHook((void*)0x00446B60);
            LogTrace("[EXTRA GAMEPLAY] Hooked SetCharacterAction at 0x00446B60");
        } else {
            LogTrace("[EXTRA GAMEPLAY] Failed to hook SetCharacterAction at 0x00446B60");
        }

        // 5. Hook PlayerInfo::IsDirectionHeld (0x00446910 - Core non-virtual implementation)
        if (MH_CreateHook((void*)0x00446910, (void*)hkIsDirectionHeld_Core, (void**)&oIsDirectionHeldCore) == MH_OK) {
            MH_EnableHook((void*)0x00446910);
            LogTrace("[EXTRA GAMEPLAY] Hooked IsDirectionHeld (Core) at 0x00446910");
        } else {
            LogTrace("[EXTRA GAMEPLAY] Failed to hook IsDirectionHeld (Core) at 0x00446910");
        }

        // 6. Hook IsDirectionHeld (0x00454310 - vtable[+0x24])
        if (MH_CreateHook((void*)0x00454310, (void*)hkIsDirectionHeld_4310, (void**)&oIsDirectionHeld4310) == MH_OK) {
            MH_EnableHook((void*)0x00454310);
            LogTrace("[EXTRA GAMEPLAY] Hooked IsDirectionHeld at 0x00454310");
        } else {
            LogTrace("[EXTRA GAMEPLAY] Failed to hook IsDirectionHeld at 0x00454310");
        }

        // 7. Hook IsDirectionHeld (0x00453030 - vtable[+0x1C])
        if (MH_CreateHook((void*)0x00453030, (void*)hkIsDirectionHeld_3030, (void**)&oIsDirectionHeld3030) == MH_OK) {
            MH_EnableHook((void*)0x00453030);
            LogTrace("[EXTRA GAMEPLAY] Hooked IsDirectionHeld at 0x00453030");
        } else {
            LogTrace("[EXTRA GAMEPLAY] Failed to hook IsDirectionHeld at 0x00453030");
        }

        // 8. Hook IsDirectionHeld (0x00454510 - vtable[+0x28])
        if (MH_CreateHook((void*)0x00454510, (void*)hkIsDirectionHeld_4510, (void**)&oIsDirectionHeld4510) == MH_OK) {
            MH_EnableHook((void*)0x00454510);
            LogTrace("[EXTRA GAMEPLAY] Hooked IsDirectionHeld at 0x00454510");
        } else {
            LogTrace("[EXTRA GAMEPLAY] Failed to hook IsDirectionHeld at 0x00454510");
        }

        // 9. Hook ResetForPractice (0x00903CD0) - Native Practice Mode Reset Dispatcher
        // Fires automatically whenever player resets in practice mode (Select button, keyboard, or pause menu)
        if (MH_CreateHook((void*)0x00903CD0, (void*)hkResetForPractice, (void**)&oResetForPractice) == MH_OK) {
            MH_EnableHook((void*)0x00903CD0);
            LogTrace("[EXTRA GAMEPLAY] Hooked ResetForPractice at 0x00903CD0");
        } else {
            LogTrace("[EXTRA GAMEPLAY] Failed to hook ResetForPractice at 0x00903CD0");
        }

        // [BUG QUEUE FIX]: Detours at 0x00830300, 0x0084E1E0, and 0x0084DE20 are suppressed.
        // Native engine flag 0x00E04B04 synchronized in OnFrameTick handles particle/splat suppression safely.

    }

    bool IsPlayerApplicable(int playerNum) {
        if (g_TargetScope == 0) return true;
        if (g_TargetScope == 1 && playerNum == 1) return true;
        if (g_TargetScope == 2 && playerNum == 2) return true;
        return false;
    }

    bool IsPlayerRolling(int playerNum) {
        if (playerNum == 1) return (s_P1State.isRolling || s_P1State.postRollLockoutFrames > 0);
        if (playerNum == 2) return (s_P2State.isRolling || s_P2State.postRollLockoutFrames > 0);
        return false;
    }

    static void UpdatePlayerState(void* pChar, int playerNum, PlayerWakeupState& state) {
        if (!pChar || (uintptr_t)pChar < 0x01000000) {
            state = PlayerWakeupState();
            return;
        }

        uint32_t action = GetCharacterAction(pChar);

        bool isStandingStun = (action == 0x4600 || action == 0x4700 || action == 0x4000 || action == 0x4100 || action == 0x4F00 || action == 0x4205 || action == 0x4206);
        bool isHitKD = (action >= 0x4200 && action <= 0x45FF && action != 0x4205 && action != 0x4206) || (action >= 0x460A && action <= 0x460F);

        if (isHitKD) {
            state.wasKnockedDown = true;
        } else if (isStandingStun) {
            state.wasKnockedDown = false;
        } else if (action == 0x0000 || action == 0x0100 || action == 0x0101) {
            state.wasKnockedDown = false;
            if (state.postRollLockoutFrames == 0) {
                state.isRolling = false;
            }
            state.wasDelayedProne = false;
            state.delayedProneCooldown = 0;
            state.fixedDelayedActive = false;
            state.fixedDelayedFrames = 0;
            state.fixedDelayedCooldown = 0;
            state.floorContactX = 0.0f;
        }

        bool isFloorContact = (action == 0x460D);
        state.inFloorImpact = isFloorContact || (action >= 0x460A && action <= 0x460F);
        state.inProne = isFloorContact || IsDelayedProneAction(action) || (action >= 0x6000 && action <= 0x61FF);

        // Track floor contact X coordinate
        if (isFloorContact || state.inProne || IsDelayedProneAction(action)) {
            __try {
                float curX = *(float*)((uintptr_t)pChar + 0x1E8);
                if (curX != 0.0f) {
                    state.floorContactX = curX;
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
        }

        // Delayed prone tracking for Feature 5 (Disable Delayed Wakeup Roll)
        if (IsDelayedProneAction(action)) {
            state.wasDelayedProne = true;
            state.delayedProneCooldown = 90;
        } else if (state.delayedProneCooldown > 0) {
            state.delayedProneCooldown--;
            if (state.delayedProneCooldown == 0) {
                state.wasDelayedProne = false;
            }
        }

        // Getup state tracking (0x6200..0x620B)
        state.inGetup = (action >= 0x6200 && action <= 0x620B);

        // Fixed Delayed Wakeup frame tracking
        if (state.fixedDelayedActive) {
            state.fixedDelayedFrames++;
            if (state.fixedDelayedFrames >= g_DelayedWakeupFixedFrames) {
                state.fixedDelayedActive = false;
                state.fixedDelayedCooldown = 30; // Prevent immediate re-trigger
            }
        } else if (state.fixedDelayedCooldown > 0) {
            state.fixedDelayedCooldown--;
        }

        // Option B Roll Tracking:
        bool isRollingAction = (action == 0x5200 || (action >= 0x6202 && action <= 0x6205));
        if (isRollingAction) {
            state.isRolling = true;
            state.postRollLockoutFrames = 15;
        } else {
            if (state.isRolling) {
                // Just left roll action -> arm 15 frames post-roll lockout (0.25s)
                state.isRolling = false;
                state.postRollLockoutFrames = 15;
            } else if (state.postRollLockoutFrames > 0) {
                state.postRollLockoutFrames--;
            }
        }
    }

    struct PlayerMovePacingTracker {
        uint32_t prevAction = 0;
        bool     recoveryModified = false;
    };
    static PlayerMovePacingTracker s_P1Pacing;
    static PlayerMovePacingTracker s_P2Pacing;

    static void UpdateMoveRecovery(void* pChar, int playerNum, PlayerMovePacingTracker& tracker) {
        if (!g_EnableCustomRecoveryFrames || g_RecoveryFramesDelta == 0 || !pChar || (uintptr_t)pChar < 0x01000000) return;
        if (!IsPlayerApplicable(playerNum)) return;

        uint32_t action = GetCharacterAction(pChar);
        if (action != tracker.prevAction) {
            tracker.prevAction = action;
            tracker.recoveryModified = false;
        }

        // Active move execution / recovery action (0x0100 to 0x3FFF)
        if (action >= 0x0100 && action < 0x4000 && !tracker.recoveryModified) {
            if (g_MovePacingFilter != 0) {
                bool isSpecial = false;
                __try {
                    uint32_t flags2EC = *(uint32_t*)((uintptr_t)pChar + 0x2EC);
                    isSpecial = (flags2EC & 0x400) == 0;
                } __except (EXCEPTION_EXECUTE_HANDLER) {}
                if (g_MovePacingFilter == 1 && isSpecial) return;
                if (g_MovePacingFilter == 2 && !isSpecial) return;
            }

            __try {
                float canAct = *(float*)((uintptr_t)pChar + 0x78C0);
                if (canAct > 0.05f && canAct < 4.0f) {
                    float deltaSec = (float)g_RecoveryFramesDelta / 60.0f;
                    float newCanAct = canAct + deltaSec;
                    if (newCanAct < 0.01f) newCanAct = 0.01f;
                    *(float*)((uintptr_t)pChar + 0x78C0) = newCanAct;
                    *(float*)((uintptr_t)pChar + 0x78C4) = newCanAct;
                    tracker.recoveryModified = true;
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
    }

    static int s_PendingCornerReset = 0; // -1 = Left Corner, 0 = Center/None, 1 = Right Corner
    static int s_PracticeResetCooldown = 0;
    static float s_LastP1Pos = 0.0f;
    static float s_LastP2Pos = 0.0f;

    static void TriggerNativePracticeReset() {
        if (!IsTrainingMode() || !IsMatchActive() || IsNativeGamePaused()) return;
        __try {
            void* pOptionsMgr = (void*)0x00E8DF64;
            if (pOptionsMgr && !IsBadReadPtr(pOptionsMgr, 0x48)) {
                typedef void(__thiscall* tResetForPractice)(void* pThis);
                tResetForPractice fnReset = (tResetForPractice)0x00903CD0;
                fnReset(pOptionsMgr);
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    void OnFrameTick(void* p1Char, void* p2Char) {
        UpdatePlayerState(p1Char, 1, s_P1State);
        UpdatePlayerState(p2Char, 2, s_P2State);
        UpdateMoveRecovery(p1Char, 1, s_P1Pacing);
        UpdateMoveRecovery(p2Char, 2, s_P2Pacing);

        // Sync global engine blood flags: 1 = Blood ON, 0 = Blood OFF (No Blood Kode)
        __try {
            uint32_t* pBloodFlag1 = (uint32_t*)0x00E04B04;
            if (pBloodFlag1 && (uintptr_t)pBloodFlag1 > 0x00400000) {
                uint32_t expected = g_DisableBlood ? 0 : 1;
                if (*pBloodFlag1 != expected) {
                    *pBloodFlag1 = expected;
                }
            }
            uint32_t* pBloodFlag2 = (uint32_t*)0x00FE2000;
            if (pBloodFlag2 && (uintptr_t)pBloodFlag2 > 0x00400000) {
                uint32_t expected = g_DisableBlood ? 0 : 1;
                if (*pBloodFlag2 != expected) {
                    *pBloodFlag2 = expected;
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}

        // Soul Steal cinematic / active grab tracking:
        // When Soul Steal finishes, trigger warmup to re-bake ambient lighting cleanly
        static bool s_WasSoulStealOngoing = false;
        bool soulStealNow = IsSoulStealOngoing();
        if (s_WasSoulStealOngoing && !soulStealNow) {
            TriggerLightingProbeWarmup();
            LogTrace("[EXTRA GAMEPLAY] Soul Steal sequence completed! Step 1 warmup triggered (%d frames).", g_LightingWarmupFrames);
        }
        s_WasSoulStealOngoing = soulStealNow;

        // -----------------------------------------------------------------
        // Character Mesh & Form Change Detection (Shang Tsung Morph / Revert)
        // When a character swaps skeletal meshes, components, or actors mid-match,
        // trigger lighting probe warmup so UE3 samples authentic stage lighting
        // instead of baking the void background clear color (preventing pitch-black unlit models).
        // -----------------------------------------------------------------
        static void* s_LastP1MeshComp = nullptr;
        static void* s_LastP1SkelMesh = nullptr;
        static void* s_LastP1Actor    = nullptr;
        static void* s_LastP1Pkg      = nullptr;
        static void* s_LastP2MeshComp = nullptr;
        static void* s_LastP2SkelMesh = nullptr;
        static void* s_LastP2Actor    = nullptr;
        static void* s_LastP2Pkg      = nullptr;

        if (IsMatchActive()) {
            if (p1Char && (uintptr_t)p1Char > 0x01000000) {
                void* curActor1 = nullptr;
                void* curComp1  = nullptr;
                void* curMesh1  = nullptr;
                void* curPkg1   = nullptr;
                __try {
                    curActor1 = *(void**)((uintptr_t)p1Char + 0x08);
                    curComp1  = *(void**)((uintptr_t)p1Char + 0x10C);
                    curPkg1   = *(void**)((uintptr_t)p1Char + 0x2A8);
                    if (curComp1 && (uintptr_t)curComp1 > 0x01000000) {
                        curMesh1 = *(void**)((uintptr_t)curComp1 + 0x2B4);
                    }
                } __except (EXCEPTION_EXECUTE_HANDLER) {}

                if (s_LastP1MeshComp != nullptr) {
                    bool compChanged  = (curComp1 != nullptr && curComp1 != s_LastP1MeshComp);
                    bool meshChanged  = (curMesh1 != nullptr && s_LastP1SkelMesh != nullptr && curMesh1 != s_LastP1SkelMesh);
                    bool actorChanged = (curActor1 != nullptr && s_LastP1Actor != nullptr && curActor1 != s_LastP1Actor);
                    bool pkgChanged   = (curPkg1 != nullptr && s_LastP1Pkg != nullptr && curPkg1 != s_LastP1Pkg);

                    if (compChanged || meshChanged || actorChanged || pkgChanged) {
                        TriggerLightingProbeWarmup();
                        LogTrace("[EXTRA GAMEPLAY] P1 form / mesh change detected (Shang Tsung morph/revert). Step 1 warmup triggered (%d frames).", g_LightingWarmupFrames);
                    }
                }
                s_LastP1Actor    = curActor1;
                s_LastP1MeshComp = curComp1;
                s_LastP1SkelMesh = curMesh1;
                s_LastP1Pkg      = curPkg1;
            }

            if (p2Char && (uintptr_t)p2Char > 0x01000000) {
                void* curActor2 = nullptr;
                void* curComp2  = nullptr;
                void* curMesh2  = nullptr;
                void* curPkg2   = nullptr;
                __try {
                    curActor2 = *(void**)((uintptr_t)p2Char + 0x08);
                    curComp2  = *(void**)((uintptr_t)p2Char + 0x10C);
                    curPkg2   = *(void**)((uintptr_t)p2Char + 0x2A8);
                    if (curComp2 && (uintptr_t)curComp2 > 0x01000000) {
                        curMesh2 = *(void**)((uintptr_t)curComp2 + 0x2B4);
                    }
                } __except (EXCEPTION_EXECUTE_HANDLER) {}

                if (s_LastP2MeshComp != nullptr) {
                    bool compChanged  = (curComp2 != nullptr && curComp2 != s_LastP2MeshComp);
                    bool meshChanged  = (curMesh2 != nullptr && s_LastP2SkelMesh != nullptr && curMesh2 != s_LastP2SkelMesh);
                    bool actorChanged = (curActor2 != nullptr && s_LastP2Actor != nullptr && curActor2 != s_LastP2Actor);
                    bool pkgChanged   = (curPkg2 != nullptr && s_LastP2Pkg != nullptr && curPkg2 != s_LastP2Pkg);

                    if (compChanged || meshChanged || actorChanged || pkgChanged) {
                        TriggerLightingProbeWarmup();
                        LogTrace("[EXTRA GAMEPLAY] P2 form / mesh change detected (Shang Tsung morph/revert). Step 1 warmup triggered (%d frames).", g_LightingWarmupFrames);
                    }
                }
                s_LastP2Actor    = curActor2;
                s_LastP2MeshComp = curComp2;
                s_LastP2SkelMesh = curMesh2;
                s_LastP2Pkg      = curPkg2;
            }
        } else {
            s_LastP1Actor    = nullptr;
            s_LastP1MeshComp = nullptr;
            s_LastP1SkelMesh = nullptr;
            s_LastP1Pkg      = nullptr;
            s_LastP2Actor    = nullptr;
            s_LastP2MeshComp = nullptr;
            s_LastP2SkelMesh = nullptr;
            s_LastP2Pkg      = nullptr;
        }

        // Practice Mode Position & Teleport Lighting Probe Management
        if (IsTrainingMode() && p1Char && p2Char) {
            float p1X = *(float*)((uintptr_t)p1Char + 0x1E8);
            float p2X = *(float*)((uintptr_t)p2Char + 0x1E8);

            bool isPlayback = InputRecordingManager::Get().IsPlaying();
            bool isReversal = ReversalEngine::Get().HasActiveSequence();

            // Direct Keyboard Reset Polling:
            // P1 Select on PC keyboard is VK_4 ('4', 0x34).
            // P2 Select on PC keyboard is VK_SUBTRACT (Numpad '-', 0x6D).
            // Strictly exclude VK_NUMPAD4 (0x64), which is P2's Front Kick on keyboard.
            // Edge-triggered: only fires on new key press, and never interrupts playback or reversal sequences.
            // Must be focused on the game window, not in console, and not paused.
            static bool s_WasKbReset = false;
            bool isFocused = IsGameWindowFocused();
            bool isConsoleOpen = MK9Overlay::IsConsoleOpen();
            bool isGamePaused = IsNativeGamePaused();
            bool allowKbInput = isFocused && !isConsoleOpen && !isGamePaused && !isPlayback && !isReversal;

            bool isKey4 = allowKbInput && ((GetAsyncKeyState('4') & 0x8000) != 0) && ((GetAsyncKeyState(VK_NUMPAD4) & 0x8000) == 0);
            bool isSubtract = allowKbInput && ((GetAsyncKeyState(VK_SUBTRACT) & 0x8000) != 0);
            bool isKbSelect = isKey4 || isSubtract;
            bool kbResetRaw = isKbSelect;
            bool kbResetEdge = kbResetRaw && !s_WasKbReset;
            s_WasKbReset = kbResetRaw;

            if (kbResetEdge) {
                bool isLeft = ((GetAsyncKeyState(VK_LEFT) & 0x8000) != 0) || ((GetAsyncKeyState('A') & 0x8000) != 0);
                bool isRight = ((GetAsyncKeyState(VK_RIGHT) & 0x8000) != 0) || ((GetAsyncKeyState('D') & 0x8000) != 0);
                bool isDown = ((GetAsyncKeyState(VK_DOWN) & 0x8000) != 0) || ((GetAsyncKeyState('S') & 0x8000) != 0);

                if (isLeft) {
                    s_PendingCornerReset = isDown ? 3 : 2; // 3 = Left Swapped (P1 in corner), 2 = Left Normal (P2 in corner)
                    s_PracticeResetCooldown = 3;
                    TriggerLightingProbeWarmup();
                } else if (isRight) {
                    s_PendingCornerReset = isDown ? 5 : 4; // 5 = Right Swapped (P1 in corner), 4 = Right Normal (P2 in corner)
                    s_PracticeResetCooldown = 3;
                    TriggerLightingProbeWarmup();
                } else if (isDown) {
                    s_PendingCornerReset = 1; // 1 = Center Swapped (P1 Right, P2 Left)
                    s_PracticeResetCooldown = 3;
                    TriggerLightingProbeWarmup();
                } else {
                    s_PendingCornerReset = 0; // Standard neutral reset: let engine reset naturally without override
                    s_PracticeResetCooldown = 0;
                }

                // Trigger native engine practice reset (clears combo, damage info, health, and animations)
                TriggerNativePracticeReset();
            }

            // Detect sudden position jumps / menu resets
            if (s_LastP1Pos != 0.0f && s_LastP2Pos != 0.0f) {
                float delta1 = fabsf(p1X - s_LastP1Pos);
                float delta2 = fabsf(p2X - s_LastP2Pos);
                if (delta1 > 80.0f || delta2 > 80.0f) {
                    TriggerLightingProbeWarmup();
                }
            }

            // Apply Corner / Position Reset override
            if (s_PracticeResetCooldown > 0) {
                s_PracticeResetCooldown--;
                if (s_PendingCornerReset > 0) {
                    float halfWidth = 1300.0f;
                    if (!IsBadReadPtr((void*)0x00DF0BE4, 4)) {
                        float hw = *(float*)0x00DF0BE4;
                        if (std::isfinite(hw) && hw > 400.0f && hw < 10000.0f) halfWidth = hw;
                    } else if (!IsBadReadPtr((void*)0x00DF0C20, 4)) {
                        float tw = *(float*)0x00DF0C20;
                        if (std::isfinite(tw) && tw > 800.0f && tw < 20000.0f) halfWidth = tw * 0.5f;
                    }
                    float cornerDef = halfWidth;
                    float cornerAtt = halfWidth - 160.0f;

                    switch (s_PendingCornerReset) {
                        case 1:
                            // D + Select: Center Swapped (P1 on Right +120, P2 on Left -120)
                            ApplyLateralPosition(p1Char, 120.0f);
                            ApplyLateralPosition(p2Char, -120.0f);
                            break;
                        case 2:
                            // Left + Select: Left End (P2 at corner wall, P1 attacker facing left)
                            ApplyLateralPosition(p1Char, -cornerAtt);
                            ApplyLateralPosition(p2Char, -cornerDef);
                            break;
                        case 3:
                            // D + Left + Select: Left End Swapped (P1 at corner wall, P2 attacker facing left)
                            ApplyLateralPosition(p1Char, -cornerDef);
                            ApplyLateralPosition(p2Char, -cornerAtt);
                            break;
                        case 4:
                            // Right + Select: Right End (P2 at corner wall, P1 attacker facing right)
                            ApplyLateralPosition(p1Char, cornerAtt);
                            ApplyLateralPosition(p2Char, cornerDef);
                            break;
                        case 5:
                            // D + Right + Select: Right End Swapped (P1 at corner wall, P2 attacker facing right)
                            ApplyLateralPosition(p1Char, cornerDef);
                            ApplyLateralPosition(p2Char, cornerAtt);
                            break;
                        default:
                            break;
                    }
                    TriggerLightingProbeWarmup();
                }
                if (s_PracticeResetCooldown == 0) {
                    s_PendingCornerReset = 0;
                }
            }

            s_LastP1Pos = *(float*)((uintptr_t)p1Char + 0x1E8);
            s_LastP2Pos = *(float*)((uintptr_t)p2Char + 0x1E8);
        } else {
            s_LastP1Pos = 0.0f;
            s_LastP2Pos = 0.0f;
            s_PracticeResetCooldown = 0;
            s_PendingCornerReset = 0;
        }

        UpdateBackgroundVisibility();
    }

    float GetMoveAnimRateMultiplier(void* pChar, const char* animName) {
        if (!g_EnableCustomStartupSpeed || !pChar || (uintptr_t)pChar < 0x01000000) return 1.0f;
        int plr = (pChar == GetP2Char()) ? 2 : 1;
        if (!IsPlayerApplicable(plr)) return 1.0f;

        // Skip reaction / stun states
        uint32_t action = GetCharacterAction(pChar);
        if (action >= 0x4000) return 1.0f;

        // Check move category filter
        if (g_MovePacingFilter != 0) {
            bool isSpecial = false;
            __try {
                uint32_t flags2EC = *(uint32_t*)((uintptr_t)pChar + 0x2EC);
                isSpecial = (flags2EC & 0x400) == 0;
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
            if (g_MovePacingFilter == 1 && isSpecial) return 1.0f;
            if (g_MovePacingFilter == 2 && !isSpecial) return 1.0f;
        }

        return g_StartupSpeedMultiplier;
    }

    void FilterControllerInput(DWORD dwUserIndex, XINPUT_STATE* pState) {
        if (!pState) return;

        int playerNum = 0;
        if ((int)dwUserIndex == GetPlayerXInputPort(1)) playerNum = 1;
        else if ((int)dwUserIndex == GetPlayerXInputPort(2)) playerNum = 2;
        else return;

        // Practice Mode Reset Interception (P1 Select / Back Button on Gamepad)
        if (IsTrainingMode() && playerNum == 1) {
            bool isPlayback = InputRecordingManager::Get().IsPlaying();
            bool isReversal = ReversalEngine::Get().HasActiveSequence();
            bool isGamePaused = IsNativeGamePaused();

            static bool s_WasPadReset = false;
            bool resetPressed = !isPlayback && !isReversal && !isGamePaused && ((pState->Gamepad.wButtons & XINPUT_GAMEPAD_BACK) != 0);
            bool resetEdge = resetPressed && !s_WasPadReset;
            s_WasPadReset = resetPressed;

            if (resetEdge) {
                TriggerLightingProbeWarmup();
                LogTrace("[EXTRA GAMEPLAY] Practice reset (Gamepad Select) detected! Step 1 warmup triggered (%d frames).", g_LightingWarmupFrames);

                bool isLeft = ((pState->Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT) != 0) || (pState->Gamepad.sThumbLX < -16000);
                bool isRight = ((pState->Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) != 0) || (pState->Gamepad.sThumbLX > 16000);
                bool isDown = ((pState->Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN) != 0) || (pState->Gamepad.sThumbLY < -16000);

                if (isLeft) {
                    s_PendingCornerReset = isDown ? 3 : 2; // 3 = Left Swapped (P1 in corner), 2 = Left Normal (P2 in corner)
                    s_PracticeResetCooldown = 3;
                } else if (isRight) {
                    s_PendingCornerReset = isDown ? 5 : 4; // 5 = Right Swapped (P1 in corner), 4 = Right Normal (P2 in corner)
                    s_PracticeResetCooldown = 3;
                } else if (isDown) {
                    s_PendingCornerReset = 1; // 1 = Center Swapped (P1 Right, P2 Left)
                    s_PracticeResetCooldown = 3;
                } else {
                    s_PendingCornerReset = 0; // Standard reset: let game engine reset naturally without override
                    s_PracticeResetCooldown = 0;
                }
            }
        }

        if (!IsPlayerApplicable(playerNum)) return;

        PlayerWakeupState& state = (playerNum == 1) ? s_P1State : s_P2State;
        void* pChar = (playerNum == 1) ? GetP1Char() : GetP2Char();
        uint32_t action = GetCharacterAction(pChar);

        // 1. Disable Quick Wakeup (Tech Roll) - Zero horizontal direction on floor impact / knockdown
        if (g_DisableQuickWakeup && (state.inFloorImpact || state.wasKnockedDown || state.inProne || (action >= 0x4200 && action <= 0x460F))) {
            pState->Gamepad.sThumbLX = 0;
            pState->Gamepad.wButtons &= ~(XINPUT_GAMEPAD_DPAD_LEFT | XINPUT_GAMEPAD_DPAD_RIGHT);
        }

        // 2. Disable Wakeup Attack When Coming From Quick Roll (Option B) - Suppress attack buttons while rolling
        if (g_DisableRollWakeupAttack && state.isRolling) {
            pState->Gamepad.wButtons &= ~(XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_B | XINPUT_GAMEPAD_X | XINPUT_GAMEPAD_Y |
                                          XINPUT_GAMEPAD_RIGHT_SHOULDER | XINPUT_GAMEPAD_LEFT_SHOULDER);
        }

        // 5. Disable Delayed Wakeup Roll - Suppress horizontal back directions during delayed prone getup
        if (g_DisableDelayedWakeupRoll && (state.inProne || state.wasDelayedProne || state.delayedProneCooldown > 0 || IsDelayedProneAction(action))) {
            pState->Gamepad.sThumbLX = 0;
            pState->Gamepad.wButtons &= ~(XINPUT_GAMEPAD_DPAD_LEFT | XINPUT_GAMEPAD_DPAD_RIGHT);
        }

        // 3. Disable Delayed Wakeup - Zero Down inputs
        if (g_DisableDelayedWakeup && (state.inProne || state.inFloorImpact || state.wasKnockedDown)) {
            if (pState->Gamepad.sThumbLY < -8000) {
                pState->Gamepad.sThumbLY = 0;
            }
            pState->Gamepad.wButtons &= ~XINPUT_GAMEPAD_DPAD_DOWN;
        }

        // 4. Delayed Wakeup: Fixed Amount of Hold
        if (g_DelayedWakeupFixedHold && !g_DisableDelayedWakeup) {
            if (state.fixedDelayedActive) {
                // Ensure pure DOWN is held, zero all horizontal inputs!
                pState->Gamepad.sThumbLX = 0;
                pState->Gamepad.wButtons &= ~(XINPUT_GAMEPAD_DPAD_LEFT | XINPUT_GAMEPAD_DPAD_RIGHT);
                pState->Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_DOWN;
                if (pState->Gamepad.sThumbLY > -16000) {
                    pState->Gamepad.sThumbLY = -30000;
                }
            } else if (state.fixedDelayedCooldown > 0) {
                // Fixed duration elapsed -> force Down released!
                if (pState->Gamepad.sThumbLY < -8000) {
                    pState->Gamepad.sThumbLY = 0;
                }
                pState->Gamepad.wButtons &= ~XINPUT_GAMEPAD_DPAD_DOWN;
            }
        }
    }

    bool ShouldSuppressSpecialOutRoll(void* pChar) {
        if (!g_DisableRollWakeupAttack || !pChar) return false;

        int playerNum = GetPlayerNumber(pChar);
        if (!IsPlayerApplicable(playerNum)) return false;

        const PlayerWakeupState& state = (playerNum == 1) ? s_P1State : s_P2State;
        uint32_t action = GetCharacterAction(pChar);

        // If player is rolling, in roll action, or within post-roll lockout window
        if (state.isRolling || state.postRollLockoutFrames > 0 || action == 0x5200 || (action >= 0x6202 && action <= 0x6205)) {
            LogTrace("[EXTRA GAMEPLAY] Suppressed P%d special move out of Quick Roll recovery (Option B)!", playerNum);
            return true;
        }

        return false;
    }

} // namespace ExtraGameplay
} // namespace MK9TradeFix
