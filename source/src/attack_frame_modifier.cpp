#include "attack_frame_modifier.hpp"
#include "frame_data.hpp"
#include "combat_state.hpp"
#include "engine_common.hpp"
#include <algorithm>
#include <cstring>
#include <cmath>

namespace MK9TradeFix {
namespace AttackFrameModifier {

    bool g_EnableAttackFrameModifier = true;
    int  g_TargetScope = 0; // 0 = Both Players, 1 = P1 Only, 2 = P2 Only

    // Normals Global Deltas (-12 to +12 frames)
    int  g_NormalsStartupDelta  = 0;
    int  g_NormalsActiveDelta   = 0;
    int  g_NormalsRecoveryDelta = 0;

    // Specials Global Deltas (-12 to +12 frames)
    int  g_SpecialsStartupDelta  = 0;
    int  g_SpecialsActiveDelta   = 0;
    int  g_SpecialsRecoveryDelta = 0;

    // Per-Attack Custom Overrides
    bool g_EnablePerAttackOverrides = false;

    int  g_FP_StartupDelta  = 0;
    int  g_FP_ActiveDelta   = 0;
    int  g_FP_RecoveryDelta = 0;

    int  g_BP_StartupDelta  = 0;
    int  g_BP_ActiveDelta   = 0;
    int  g_BP_RecoveryDelta = 0;

    int  g_FK_StartupDelta  = 0;
    int  g_FK_ActiveDelta   = 0;
    int  g_FK_RecoveryDelta = 0;

    int  g_BK_StartupDelta  = 0;
    int  g_BK_ActiveDelta   = 0;
    int  g_BK_RecoveryDelta = 0;

    enum AttackPhase {
        PHASE_IDLE = 0,
        PHASE_STARTUP,
        PHASE_ACTIVE,
        PHASE_RECOVERY
    };

    struct MoveBaseline {
        int startup;
        int active;
        int recovery;
    };

    static MoveBaseline GetMoveBaseline(uint32_t token, bool isSpecial) {
        if (isSpecial) {
            return { 16, 5, 35 }; // Default special move baseline
        }
        // Standing Normals baselines measured via Mode 17:
        if (token == 0x8000000F || token == 0x80000001) return { 10, 3, 17 }; // FP
        if (token == 0x80000010 || token == 0x80000002) return { 14, 3, 24 }; // BP
        if (token == 0x80000011 || token == 0x80000003) return { 14, 3, 23 }; // FK
        if (token == 0x80000012 || token == 0x80000004) return { 21, 4, 47 }; // BK
        return { 14, 4, 25 }; // Generic normal move baseline
    }

    struct AttackModifierTracker {
        bool        inMove = false;
        bool        isSpecial = false;
        AttackPhase phase = PHASE_IDLE;
        uint32_t    initialAction = 0;
        int         scriptFunc = -1;
        uint32_t    matchedToken = 0;

        int         resolvedStartupDelta = 0;
        int         resolvedActiveDelta = 0;
        int         resolvedRecoveryDelta = 0;

        float       targetStartupRate = 1.0f;
        float       targetActiveRate = 1.0f;
        float       targetRecoveryRate = 1.0f;

        void Reset() {
            inMove = false;
            isSpecial = false;
            phase = PHASE_IDLE;
            initialAction = 0;
            scriptFunc = -1;
            matchedToken = 0;
            resolvedStartupDelta = 0;
            resolvedActiveDelta = 0;
            resolvedRecoveryDelta = 0;
            targetStartupRate = 1.0f;
            targetActiveRate = 1.0f;
            targetRecoveryRate = 1.0f;
        }
    };

    static AttackModifierTracker s_P1Tracker;
    static AttackModifierTracker s_P2Tracker;

    static void SetAttackAnimRate(void* pChar, int playerNum, float rate) {
        if (!pChar || (uintptr_t)pChar < 0x01000000) return;
        __try {
            // 1. Set USkeletalMeshComponent rate (+0x2F0)
            void* pSkelComp = *(void**)((uintptr_t)pChar + 0x010C);
            if (pSkelComp && (uintptr_t)pSkelComp >= 0x00400000 && (uintptr_t)pSkelComp < 0x7FFE0000) {
                *(float*)((uintptr_t)pSkelComp + 0x2F0) = rate;
            }

            // 2. Set active UAnimNodeSequence playback rate (+0x48)
            void* pAnimSeq = GetPlayerActiveAttackAnimSeq(playerNum);
            if (pAnimSeq && (uintptr_t)pAnimSeq >= 0x00400000 && (uintptr_t)pAnimSeq < 0x7FFE0000) {
                *(float*)((uintptr_t)pAnimSeq + 0x48) = rate;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    bool IsPlayerApplicable(int playerNum) {
        if (g_TargetScope == 1 && playerNum != 1) return false;
        if (g_TargetScope == 2 && playerNum != 2) return false;
        return true;
    }

    void Initialize() {
        ResetAll();
    }

    void ResetAll() {
        s_P1Tracker.Reset();
        s_P2Tracker.Reset();
        SetAttackAnimRate(GetP1Char(), 1, 1.0f);
        SetAttackAnimRate(GetP2Char(), 2, 1.0f);
        SetPlayerActiveAttackAnimSeq(1, nullptr);
        SetPlayerActiveAttackAnimSeq(2, nullptr);
    }

    void ResetToVanilla() {
        g_NormalsStartupDelta = 0;
        g_NormalsActiveDelta = 0;
        g_NormalsRecoveryDelta = 0;

        g_SpecialsStartupDelta = 0;
        g_SpecialsActiveDelta = 0;
        g_SpecialsRecoveryDelta = 0;

        g_EnablePerAttackOverrides = false;
        g_FP_StartupDelta = 0; g_FP_ActiveDelta = 0; g_FP_RecoveryDelta = 0;
        g_BP_StartupDelta = 0; g_BP_ActiveDelta = 0; g_BP_RecoveryDelta = 0;
        g_FK_StartupDelta = 0; g_FK_ActiveDelta = 0; g_FK_RecoveryDelta = 0;
        g_BK_StartupDelta = 0; g_BK_ActiveDelta = 0; g_BK_RecoveryDelta = 0;

        ResetAll();
    }

    static void ResolveDeltasForMove(int playerNum, uint32_t token, bool isSpecial,
                                    int& outStartup, int& outActive, int& outRecovery) {
        if (isSpecial) {
            outStartup = g_SpecialsStartupDelta;
            outActive = g_SpecialsActiveDelta;
            outRecovery = g_SpecialsRecoveryDelta;
            return;
        }

        // Standing Normals:
        if (g_EnablePerAttackOverrides) {
            // FP: 0x8000000F or 0x80000001
            if (token == 0x8000000F || token == 0x80000001) {
                outStartup = g_FP_StartupDelta;
                outActive = g_FP_ActiveDelta;
                outRecovery = g_FP_RecoveryDelta;
                return;
            }
            // BP: 0x80000010 or 0x80000002
            if (token == 0x80000010 || token == 0x80000002) {
                outStartup = g_BP_StartupDelta;
                outActive = g_BP_ActiveDelta;
                outRecovery = g_BP_RecoveryDelta;
                return;
            }
            // FK: 0x80000011 or 0x80000003
            if (token == 0x80000011 || token == 0x80000003) {
                outStartup = g_FK_StartupDelta;
                outActive = g_FK_ActiveDelta;
                outRecovery = g_FK_RecoveryDelta;
                return;
            }
            // BK: 0x80000012 or 0x80000004
            if (token == 0x80000012 || token == 0x80000004) {
                outStartup = g_BK_StartupDelta;
                outActive = g_BK_ActiveDelta;
                outRecovery = g_BK_RecoveryDelta;
                return;
            }
        }

        // Global Normals fallback
        outStartup = g_NormalsStartupDelta;
        outActive = g_NormalsActiveDelta;
        outRecovery = g_NormalsRecoveryDelta;
    }

    float GetInitialStartupRate(int playerNum) {
        if (!g_EnableAttackFrameModifier || !IsPlayerApplicable(playerNum)) return 1.0f;
        const AttackModifierTracker& tracker = (playerNum == 1) ? s_P1Tracker : s_P2Tracker;
        if (tracker.inMove && tracker.phase == PHASE_STARTUP) {
            return tracker.targetStartupRate;
        }
        return 1.0f;
    }

    static void ProcessPlayerFrameModifier(void* pChar, int playerNum, AttackModifierTracker& tracker) {
        if (!g_EnableAttackFrameModifier || !pChar || (uintptr_t)pChar < 0x01000000) return;
        if (!IsPlayerApplicable(playerNum)) return;

        uint32_t actionState = 0;
        uint32_t combat = 0;
        int scriptFunc = -1;
        uintptr_t scriptPkg = 0;
        uint32_t hitboxes = 0;
        uint32_t animFlags = 0;
        uint32_t activeVols = 0;

        __try {
            actionState = *(uint32_t*)((uintptr_t)pChar + 0x1D4);
            combat = *(uint32_t*)((uintptr_t)pChar + 0x2EC);
            scriptPkg = *(uintptr_t*)((uintptr_t)pChar + 0x2F84);
            scriptFunc = *(int*)((uintptr_t)pChar + 0x2F88);
            hitboxes = *(uint32_t*)((uintptr_t)pChar + 0x7A3C);

            void* pInfo = *(void**)((uintptr_t)pChar + 0x7638);
            if (pInfo && (uintptr_t)pInfo > 0x01000000) {
                void* pAnimPlayer = *(void**)((uintptr_t)pInfo + 0xB40);
                if (pAnimPlayer && (uintptr_t)pAnimPlayer > 0x01000000) {
                    animFlags = *(uint32_t*)((uintptr_t)pAnimPlayer + 0xC);
                }
            }

            void* pColInfo = *(void**)((uintptr_t)pChar + 0x2F10);
            if (pColInfo && (uintptr_t)pColInfo > 0x01000000) {
                uint32_t* pVolList = *(uint32_t**)((uintptr_t)pColInfo + 0x14);
                if (pVolList && (uintptr_t)pVolList > 0x01000000) {
                    activeVols = pVolList[0];
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return;
        }

        bool isAttackAction = (actionState >= 0x5000 && actionState <= 0x5FFF) ||
                              (actionState >= 0x1000 && actionState <= 0x3FFF);
        bool isHurtOrBlock = (actionState >= 0x4000 && actionState <= 0x4FFF) ||
                             (actionState >= 0x0A00 && actionState <= 0x0A0F) ||
                             (actionState >= 0x6000 && actionState <= 0x62FF);
        bool inAttack = isAttackAction && !isHurtOrBlock && (scriptPkg != 0 || scriptFunc != -1);
        bool isHitboxActive = (hitboxes > 0) || (activeVols > 0) || ((animFlags & 0x02) != 0);

        // Detect New Attack Initiation
        if (inAttack && (!tracker.inMove || tracker.scriptFunc != scriptFunc)) {
            tracker.Reset();
            tracker.inMove = true;
            tracker.scriptFunc = scriptFunc;
            tracker.initialAction = actionState;
            tracker.matchedToken = (playerNum == 1) ? g_P1_LastAttackToken : g_P2_LastAttackToken;
            tracker.isSpecial = ((combat & 0x200) != 0) || (actionState >= 0x1000 && actionState < 0x4000);

            ResolveDeltasForMove(playerNum, tracker.matchedToken, tracker.isSpecial,
                                tracker.resolvedStartupDelta,
                                tracker.resolvedActiveDelta,
                                tracker.resolvedRecoveryDelta);

            // Clamp deltas to [-12 .. +12]
            auto ClampI = [](int v, int lo, int hi) -> int { return (v < lo) ? lo : ((v > hi) ? hi : v); };
            auto ClampF = [](float v, float lo, float hi) -> float { return (v < lo) ? lo : ((v > hi) ? hi : v); };
            auto MaxI = [](int a, int b) -> int { return (a > b) ? a : b; };

            tracker.resolvedStartupDelta  = ClampI(tracker.resolvedStartupDelta, -12, 12);
            tracker.resolvedActiveDelta   = ClampI(tracker.resolvedActiveDelta, -12, 12);
            tracker.resolvedRecoveryDelta = ClampI(tracker.resolvedRecoveryDelta, -12, 12);

            // Compute Smooth Distribution Rates: Rate = F_orig / (F_orig + Delta)
            MoveBaseline base = GetMoveBaseline(tracker.matchedToken, tracker.isSpecial);
            
            int targetStartup = MaxI(1, base.startup + tracker.resolvedStartupDelta);
            tracker.targetStartupRate = ClampF((float)base.startup / (float)targetStartup, 0.15f, 3.0f);

            int targetActive = MaxI(1, base.active + tracker.resolvedActiveDelta);
            tracker.targetActiveRate = ClampF((float)base.active / (float)targetActive, 0.15f, 3.0f);

            int targetRecovery = MaxI(1, base.recovery + tracker.resolvedRecoveryDelta);
            tracker.targetRecoveryRate = ClampF((float)base.recovery / (float)targetRecovery, 0.15f, 3.0f);

            tracker.phase = PHASE_STARTUP;
            SetAttackAnimRate(pChar, playerNum, tracker.targetStartupRate);
        }

        if (!tracker.inMove) return;

        // Exit attack cleanly if returned to neutral, interrupted, or hurt
        if (!inAttack || isHurtOrBlock) {
            SetAttackAnimRate(pChar, playerNum, 1.0f);
            SetPlayerActiveAttackAnimSeq(playerNum, nullptr);
            tracker.Reset();
            return;
        }

        // ====================================================================
        // PHASE 1: STARTUP (Smooth Time-Dilation Distribution)
        // ====================================================================
        if (tracker.phase == PHASE_STARTUP) {
            if (isHitboxActive) {
                // First active frame reached: transition smoothly to Active Phase
                tracker.phase = PHASE_ACTIVE;
                SetAttackAnimRate(pChar, playerNum, tracker.targetActiveRate);
            } else {
                // Continue enforcing smooth startup rate
                SetAttackAnimRate(pChar, playerNum, tracker.targetStartupRate);
            }
        }

        // ====================================================================
        // PHASE 2: ACTIVE (Smooth Time-Dilation Distribution Across All Strike Frames)
        // ====================================================================
        if (tracker.phase == PHASE_ACTIVE) {
            if (isHitboxActive) {
                // Continue enforcing smooth active strike rate
                SetAttackAnimRate(pChar, playerNum, tracker.targetActiveRate);
            } else {
                // Hitboxes naturally expired: transition smoothly to Recovery Phase
                tracker.phase = PHASE_RECOVERY;
                SetAttackAnimRate(pChar, playerNum, tracker.targetRecoveryRate);
            }
        }

        // ====================================================================
        // PHASE 3: RECOVERY (Smooth Time-Dilation Distribution Across Recovery Arc)
        // ====================================================================
        if (tracker.phase == PHASE_RECOVERY) {
            SetAttackAnimRate(pChar, playerNum, tracker.targetRecoveryRate);

            // Clean natural neutral return check (never touch combat flags or health!)
            bool p_canAct = ((combat & 0x400) == 0);
            if ((actionState == 0x0000 || actionState == 0x0100 || actionState == 0x0101) && p_canAct) {
                SetAttackAnimRate(pChar, playerNum, 1.0f);
                SetPlayerActiveAttackAnimSeq(playerNum, nullptr);
                tracker.Reset();
            }
        }
    }

    void OnFrameTick(void* p1Char, void* p2Char) {
        if (!g_EnableAttackFrameModifier) {
            if (s_P1Tracker.inMove || s_P2Tracker.inMove) {
                ResetAll();
            }
            return;
        }

        ProcessPlayerFrameModifier(p1Char, 1, s_P1Tracker);
        ProcessPlayerFrameModifier(p2Char, 2, s_P2Tracker);
    }

} // namespace AttackFrameModifier
} // namespace MK9TradeFix
