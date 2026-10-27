#pragma once
#include <windows.h>
#include <cstdint>

namespace MK9TradeFix {
namespace AttackFrameModifier {

    // Master Toggle
    extern bool g_EnableAttackFrameModifier;

    // Target Scope: 0 = Both Players, 1 = P1 Only, 2 = P2 Only
    extern int  g_TargetScope;

    // Normals Global Deltas (-12 to +12 frames)
    extern int  g_NormalsStartupDelta;
    extern int  g_NormalsActiveDelta;
    extern int  g_NormalsRecoveryDelta;

    // Specials Global Deltas (-12 to +12 frames)
    extern int  g_SpecialsStartupDelta;
    extern int  g_SpecialsActiveDelta;
    extern int  g_SpecialsRecoveryDelta;

    // Per-Attack Custom Overrides for Standing Normals (FP, BP, FK, BK)
    extern bool g_EnablePerAttackOverrides;

    // FP (Front Punch - 1 / 'T')
    extern int  g_FP_StartupDelta;
    extern int  g_FP_ActiveDelta;
    extern int  g_FP_RecoveryDelta;

    // BP (Back Punch - 2 / 'U')
    extern int  g_BP_StartupDelta;
    extern int  g_BP_ActiveDelta;
    extern int  g_BP_RecoveryDelta;

    // FK (Front Kick - 3 / 'G')
    extern int  g_FK_StartupDelta;
    extern int  g_FK_ActiveDelta;
    extern int  g_FK_RecoveryDelta;

    // BK (Back Kick - 4 / 'J')
    extern int  g_BK_StartupDelta;
    extern int  g_BK_ActiveDelta;
    extern int  g_BK_RecoveryDelta;

    // Lifecycle
    void Initialize();
    void ResetAll();
    void ResetToVanilla();
    void OnFrameTick(void* p1Char, void* p2Char);

    // Queries
    bool  IsPlayerApplicable(int playerNum);
    float GetInitialStartupRate(int playerNum);

} // namespace AttackFrameModifier
} // namespace MK9TradeFix
