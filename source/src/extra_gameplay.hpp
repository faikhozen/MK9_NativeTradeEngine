#pragma once
#include <windows.h>
#include <xinput.h>
#include <cstdint>

namespace MK9TradeFix {
namespace ExtraGameplay {

    // Configuration Toggles
    extern bool g_DisableBlood;                 // Suppress blood particles and cosmetic battle damage on both fighters
    extern bool g_DisableBackground;            // Disable Stage Background (Void / Black Screen)
    extern float g_VoidBgColor[3];              // Void Background / Green Screen Color (RGB)
    extern bool g_EnableFloorGrid;              // Enable 3D Floor Training Grid
    extern float g_FloorGridColor[3];           // Floor Grid Color (RGB)
    extern float g_FloorGridCenterColor[3];     // Floor Grid Center Division Lines Color (RGB, default White)
    extern bool g_ShowGridCornerWalls;          // Render Corner Wall Boundaries in Grid Mode (Dynamic Stage Bounds)
    extern float g_GridWallColor[3];            // Corner Wall Color (RGB)
    extern int   g_LightingWarmupFrames;        // Lighting Probe Warmup Delay in Step 1 (Frames, default: 15)
    extern bool g_DisableQuickWakeup;           // 1. Disable Quick Wakeup / Tech Roll
    extern bool g_RollWakeupGuardReady;
    extern bool g_RollTransferHooksReady;
    bool ShouldBlockRollSpecialTransfer(void* owner);
    void NotifyRecoveryAnimation(int player,const char* name);
    extern volatile unsigned g_RollWakeupBlocksP1,g_RollWakeupBlocksP2,g_RollWakeupArms;
    extern bool g_DisableRollWakeupAttack;       // 2. Disable Wakeup Attack when coming from Quick Roll (Option B)
    extern bool g_DisableDelayedWakeup;         // 3. Disable Delayed Wakeup
    extern bool g_DelayedWakeupFixedHold;       // 4. Delayed Wakeup fixed amount of hold when initiated
    extern int  g_DelayedWakeupFixedFrames;     // Fixed hold duration (30 to 96 frames, default: 60)
    extern bool g_DisableDelayedWakeupRoll;     // 5. Disable Delayed Wakeup Roll (rising roll getup)
    extern int  g_TargetScope;                  // 0 = Both Players, 1 = Player 1 Only, 2 = Player 2 Only

    // Move Startup & Recovery Frame Pacing
    extern bool  g_EnableCustomStartupSpeed;    // Move Startup Speed Multiplier toggle
    extern float g_StartupSpeedMultiplier;      // Speed multiplier for startup animations (0.50x to 2.00x, default 1.00x)
    extern bool  g_EnableCustomRecoveryFrames;  // Move Recovery Frame Delta toggle
    extern int   g_RecoveryFramesDelta;         // Frames added/subtracted to move recovery (-15F to +30F, default 0)
    extern int   g_MovePacingFilter;            // 0 = All Moves, 1 = Normals Only, 2 = Specials Only

    // Subsystem Lifecycle
    void Initialize();
    void OnFrameTick(void* p1Char, void* p2Char);
    void RenderFloorGrid();
    void UpdateBackgroundVisibility(bool forceUpdate = false);
    void RenderStageVisibilityUI();
    void RenderCleanFightersUI();
    void FinalizeCleanFighters();
    bool IsSoulStealOngoing();

    // Controller Input Interception (Invoked from hkXInputGetState & hkXInputGetStateEx)
    void FilterControllerInput(DWORD dwUserIndex, XINPUT_STATE* pState);

    // Input Suppression Checks (Invoked from PlayerInfo hooks and negative_edge.cpp)
    bool ShouldSuppressDirectionHeld(void* pPlayerInfo, uint32_t dirToken);
    bool ShouldSuppressDirectionPressed(void* pPlayerInfo, uint32_t tag);

    // Roll Wakeup Attack Rejection (Invoked from hkMatchAndExecuteMove, hkIsCommandInputSatisfied, etc. - Option B)
    bool ShouldSuppressSpecialOutRoll(void* pChar);

    // Query helpers
    bool IsPlayerApplicable(int playerNum);
    bool IsPlayerRolling(int playerNum);
    const char* GetAnimSequenceName(void* pAnimSeq);
    const char* GetFNameString(uint32_t nameIdx);
    float GetMoveAnimRateMultiplier(void* pChar, const char* animName);

} // namespace ExtraGameplay
} // namespace MK9TradeFix

