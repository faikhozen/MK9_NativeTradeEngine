#pragma once
#include "engine_common.hpp"
#include <vector>
#include <xinput.h>

namespace MK9TradeFix {

    struct AttackClassification;

    enum class ReversalType {
        WAKEUP_INVINCIBLE = 0,
        WAKEUP_REGULAR,
        QUICK_ROLL_REGULAR,
        QUICK_ROLL_INVINCIBLE,
        DELAYED_WAKEUP,
        BLOCK,
        AFTER_DAMAGE,
        COUNT
    };

    enum class QuickRollReversalMode {
        NONE = 0,
        REGULAR_REVERSAL,
        INVINCIBLE_SPECIAL
    };

    struct QuickRollConfig {
        bool enabled = false;
        QuickRollReversalMode reversalMode = QuickRollReversalMode::NONE;
    };

    enum class WakeupCategory {
        INVINCIBLE = 0,
        REGULAR = 1
    };

    enum class WakeupRecoveryOption {
        NORMAL = 0,             // Normal getup (Default)
        QUICK_ROLL,             // Tech roll on knockdown floor impact (0x460D)
        DELAYED_WAKEUP,         // Hold Down on floor (slider: 30 to 96 frames)
        DELAYED_WAKEUP_ROLL     // Hold Down on floor (slider: 30 to 96 frames), then Back for 3 frames
    };

    inline const char* GetWakeupRecoveryOptionName(WakeupRecoveryOption opt) {
        switch (opt) {
            case WakeupRecoveryOption::NORMAL: return "Normal Wakeup";
            case WakeupRecoveryOption::QUICK_ROLL: return "Quick Roll";
            case WakeupRecoveryOption::DELAYED_WAKEUP: return "Delayed Wakeup";
            case WakeupRecoveryOption::DELAYED_WAKEUP_ROLL: return "Delayed Wakeup Roll";
            default: return "Normal Wakeup";
        }
    }

    struct WakeupRecoveryConfig {
        WakeupRecoveryOption option = WakeupRecoveryOption::NORMAL;
        WakeupRecoveryOption invincibleOption = WakeupRecoveryOption::NORMAL;
        WakeupRecoveryOption regularOption = WakeupRecoveryOption::NORMAL;
        int delayedHoldFrames = 60; // 30 to 96 frames
    };

    struct DelayedWakeupConfig {
        bool enabled = false;
        int holdFrames = 60; // 1 to 120 frames (up to 2.0s)
        bool holdBackOnRelease = true; // Guard on rising
    };

    enum class ReversalActionType {
        // --- NEUTRAL / NONE ---
        ACTION_NEUTRAL = 0, // Neutral (Stand / No Action)

        // --- GENERAL MOVEMENT (Held 8 Frames on Recovery) ---
        MOVE_WALK_FWD,
        MOVE_WALK_BACK,
        MOVE_JUMP_UP,
        MOVE_JUMP_FWD,
        MOVE_JUMP_BACK,
        MOVE_CROUCH,
        MOVE_DASH_FWD,
        MOVE_BACKDASH,
        MOVE_STAND_BLOCK,
        MOVE_CROUCH_BLOCK,

        // --- NORMAL ATTACKS ---
        NORM_S1,            // Stand 1 (Front Punch)
        NORM_S2,            // Stand 2 (Back Punch)
        NORM_S3,            // Stand 3 (Front Kick)
        NORM_S4,            // Stand 4 (Back Kick)
        NORM_F1,            // Forward + 1
        NORM_F2,            // Forward + 2
        NORM_F3,            // Forward + 3
        NORM_F4,            // Forward + 4
        NORM_B1,            // Back + 1
        NORM_B2,            // Back + 2
        NORM_B3,            // Back + 3
        NORM_B4,            // Back + 4
        NORM_D1,            // Down + 1 (Poke)
        NORM_D2,            // Down + 2 (Uppercut)
        NORM_D3,            // Down + 3
        NORM_D4,            // Down + 4
        NORM_J1,            // Jump-in 1
        NORM_J2,            // Jump-in 2
        NORM_J3,            // Jump-in 3
        NORM_J4,            // Jump-in 4
        NORM_THROW,         // Throw (1 + 3)

        // --- SPECIAL MOVES (Held 8 Frames on Recovery) ---
        SPEC_DF1,           // D, F + 1
        SPEC_DF2,           // D, F + 2
        SPEC_DF3,           // D, F + 3
        SPEC_DF4,           // D, F + 4
        SPEC_DB1,           // D, B + 1
        SPEC_DB2,           // D, B + 2
        SPEC_DB3,           // D, B + 3
        SPEC_DB4,           // D, B + 4
        SPEC_BF1,           // B, F + 1
        SPEC_BF2,           // B, F + 2
        SPEC_BF3,           // B, F + 3
        SPEC_BF4,           // B, F + 4
        SPEC_BB1,           // B, B + 1
        SPEC_BB2,           // B, B + 2
        SPEC_BB3,           // B, B + 3
        SPEC_BB4,           // B, B + 4
        SPEC_DU2,           // D, U + 2
        SPEC_UD4,           // U, D + 4
        SPEC_FF3,           // F, F + 3
        SPEC_FF4,           // F, F + 4
        SPEC_DFB2,          // D, F, B + 2
        SPEC_DBF1,          // D, B, F + 1
        SPEC_DBF2,          // D, B, F + 2
        SPEC_DBF3,          // D, B, F + 3

        // --- EX SPECIAL MOVES (Multi-step motion sequence + Block) ---
        EX_DF1,             // D, F + 1 + Block
        EX_DF2,             // D, F + 2 + Block
        EX_DF3,             // D, F + 3 + Block
        EX_DF4,             // D, F + 4 + Block
        EX_DB1,             // D, B + 1 + Block
        EX_DB2,             // D, B + 2 + Block
        EX_DB3,             // D, B + 3 + Block
        EX_DB4,             // D, B + 4 + Block
        EX_BF1,             // B, F + 1 + Block
        EX_BF2,             // B, F + 2 + Block
        EX_BF3,             // B, F + 3 + Block
        EX_BF4,             // B, F + 4 + Block
        EX_BB1,             // B, B + 1 + Block
        EX_BB2,             // B, B + 2 + Block
        EX_BB3,             // B, B + 3 + Block
        EX_BB4,             // B, B + 4 + Block
        EX_DU2,             // D, U + 2 + Block
        EX_UD4,             // U, D + 4 + Block
        EX_FF3,             // F, F + 3 + Block
        EX_FF4,             // F, F + 4 + Block
        EX_DFB2,            // D, F, B + 2 + Block
        EX_DBF1,            // D, B, F + 1 + Block
        EX_DBF2,            // D, B, F + 2 + Block
        EX_DBF3,            // D, B, F + 3 + Block

        // --- X-RAY / SUPER ---
        SUPER_XRAY,         // 3 + 4 + Block (Flip Stance + Block)

        COUNT
    };

    const char* GetReversalActionName(ReversalActionType action);
    const char* GetReversalActionCategory(ReversalActionType action);

    struct ReversalSlotConfig {
        bool enabled = false;
        int slotIndex = 0;
        ReversalActionType actionType = ReversalActionType::EX_DB1; // Default EX Special
        int weight = 1;
        int delayFrames = 0;
        int holdFrames = 60; // Delayed wakeup ground hold duration (30 to 96 frames)
        WakeupRecoveryOption wakeupOption = WakeupRecoveryOption::NORMAL;
    };

    struct ReversalCategoryConfig {
        static const int MAX_CONFIG_SLOTS = 8;
        ReversalSlotConfig slots[MAX_CONFIG_SLOTS];

        ReversalCategoryConfig() {
            for (int i = 0; i < MAX_CONFIG_SLOTS; i++) {
                slots[i].enabled = false;
                slots[i].slotIndex = i;
                slots[i].actionType = (i == 0) ? ReversalActionType::EX_DB1 :
                                      (i == 1) ? ReversalActionType::NORM_D2 :
                                      (i == 2) ? ReversalActionType::NORM_D1 :
                                      (i == 3) ? ReversalActionType::SPEC_DF1 :
                                      (i == 4) ? ReversalActionType::MOVE_BACKDASH :
                                      (i == 5) ? ReversalActionType::MOVE_CROUCH_BLOCK :
                                      (i == 6) ? ReversalActionType::SUPER_XRAY : ReversalActionType::NORM_THROW;
                slots[i].weight = 1;
                slots[i].delayFrames = 0;
                slots[i].holdFrames = 60;
                slots[i].wakeupOption = WakeupRecoveryOption::NORMAL;
            }
        }
    };

    struct ActiveActionSequence {
        bool active = false;
        ReversalActionType action = ReversalActionType::NORM_S1;
        int targetPlayer = 2;
        int frameIndex = 0;
        int delayRemaining = 0;
        WORD heldKeys[16] = { 0 };
        int heldKeyCount = 0;

        void ReleaseAllHeld() {
            for (int i = 0; i < heldKeyCount; i++) {
                if (heldKeys[i] != 0) {
                    SimulateKeyUp(heldKeys[i]);
                    heldKeys[i] = 0;
                }
            }
            heldKeyCount = 0;
        }

        bool IsHoldingKey(WORD vk) const {
            for (int i = 0; i < heldKeyCount; i++) {
                if (heldKeys[i] == vk) return true;
            }
            return false;
        }

        void HoldKey(WORD vk) {
            if (vk == 0) return;
            for (int i = 0; i < heldKeyCount; i++) {
                if (heldKeys[i] == vk) return; // Already held
            }
            if (heldKeyCount < 16) {
                heldKeys[heldKeyCount++] = vk;
                if (g_KeyboardAssignedPlayer == 0 || g_KeyboardAssignedPlayer == targetPlayer) {
                    SimulateKeyDown(vk);
                }
            }
        }

        void ReleaseKey(WORD vk) {
            if (vk == 0) return;
            for (int i = 0; i < heldKeyCount; i++) {
                if (heldKeys[i] == vk) {
                    if (g_KeyboardAssignedPlayer == 0 || g_KeyboardAssignedPlayer == targetPlayer) {
                        SimulateKeyUp(vk);
                    }
                    for (int j = i; j < heldKeyCount - 1; j++) {
                        heldKeys[j] = heldKeys[j + 1];
                    }
                    heldKeys[heldKeyCount - 1] = 0;
                    heldKeyCount--;
                    break;
                }
            }
        }

        void SendHeldDown() {
            if (g_KeyboardAssignedPlayer == 0 || g_KeyboardAssignedPlayer == targetPlayer) {
                for (int i = 0; i < heldKeyCount; i++) {
                    if (heldKeys[i] != 0) {
                        SimulateKeyDown(heldKeys[i]);
                    }
                }
            }
        }
    };

    enum class DummyIdleStance {
        STANDING = 0,
        CROUCHING,
        STAND_BLOCKING,
        CROUCH_BLOCKING,
        AUTO_BLOCK_STAND,
        AUTO_BLOCK_CROUCH
    };

    enum class WakeupTelemetryPhase {
        IDLE = 0,
        AIRBORNE_FALLING,
        GROUND_KNOCKDOWN,
        DELAYED_GROUND_HOLD,
        QUICK_ROLLING,
        RISING_WAKEUP_WINDOW,
        REVERSAL_EXECUTING
    };

    class ReversalEngine {
    public:
        static ReversalEngine& Get();

        void Initialize();
        void Reset();

        bool IsEnabled() const { return m_GlobalReversalEnabled; }
        void SetEnabled(bool enabled) { m_GlobalReversalEnabled = enabled; }

        bool IsIdleStanceHUDActive() const { return m_ShowIdleStanceHUD; }
        void SetIdleStanceHUDActive(bool active) { m_ShowIdleStanceHUD = active; }

        DummyIdleStance GetDummyIdleStance() const { return m_DummyIdleStance; }
        void SetDummyIdleStance(DummyIdleStance stance);

        ReversalCategoryConfig& GetConfig(ReversalType type);
        const ReversalCategoryConfig& GetConfig(ReversalType type) const;

        QuickRollConfig& GetQuickRollSettings() { return m_QuickRollSettings; }
        const QuickRollConfig& GetQuickRollSettings() const { return m_QuickRollSettings; }

        DelayedWakeupConfig& GetDelayedWakeupSettings() { return m_DelayedWakeupSettings; }
        const DelayedWakeupConfig& GetDelayedWakeupSettings() const { return m_DelayedWakeupSettings; }

        WakeupRecoveryConfig& GetWakeupRecoveryConfig() { return m_WakeupRecoveryConfig; }
        const WakeupRecoveryConfig& GetWakeupRecoveryConfig() const { return m_WakeupRecoveryConfig; }

        WakeupCategory GetActiveWakeupCategory() const { return m_ActiveWakeupCategory; }
        void SetActiveWakeupCategory(WakeupCategory cat) { m_ActiveWakeupCategory = cat; }

        int GetTargetPlayer() const { return m_TargetPlayer; }
        void SetTargetPlayer(int p) { m_TargetPlayer = (p == 1) ? 1 : 2; }

        // Wakeup Telemetry Accessors
        WakeupTelemetryPhase GetWakeupPhase() const { return m_TelemetryPhase; }
        int GetRisingFrames() const { return m_RisingFrames; }
        bool IsWakeupArmed() const { return m_WakeupArmed; }
        bool IsQuickRollActive() const { return m_WakeupArmed && (m_QuickRollActive || m_QuickRollTriggered); }
        bool IsDelayedWakeupActive() const { return m_WakeupArmed && (m_DelayedDownHeld || (m_DelayedHoldFramesCount > 0 && !m_DelayedWakeupFinished)); }
        bool IsWakeupReversalTriggered() const { return m_WakeupArmed && m_WakeupReversalTriggered; }
        bool IsInvincibleWakeupReversalTriggered() const { return m_WakeupArmed && m_WakeupReversalTriggered && (m_ArmedInvSlot >= 0); }
        bool IsRegularWakeupReversalTriggered() const { return m_WakeupArmed && m_WakeupReversalTriggered && (m_ArmedRegSlot >= 0); }
        bool HasActiveSequence() const { return m_ActiveSequence.active; }

        // Active State Detection & Tick
        void OnFrameTick(void* p1Char, void* p2Char);

        // Event notifications from game engine hooks
        void NotifyIncomingHit(int defPlr, int attPlr, bool isBlocked, int rxID, const AttackClassification& atk, float damage);
        void NotifyIncomingKnockdown(int defPlr, int rxID);
        void NotifyBlockstunEnd(int playerNum);
        void NotifyHitstunEnd(int playerNum);
        void NotifyWakeupReady(int playerNum);

        // XInput Controller Simulation (Full Gamepad Support for Target Player & Reversals)
        bool HasActiveGamepadOverride(DWORD dwUserIndex) const;
        bool GetActiveGamepad(DWORD dwUserIndex, XINPUT_STATE* pState);

    private:
        ReversalEngine();
        ~ReversalEngine() = default;

        int PickWeightedSlot(const ReversalCategoryConfig& cfg);
        void TriggerReversal(ReversalType type, int targetPlayer);
        void ExecutePresetAction(ReversalActionType action, int targetPlayer, int delayFrames);
        void TickActionSequence(void* p1Char, void* p2Char);

        bool m_GlobalReversalEnabled = true;
        int m_TargetPlayer = 2; // 2 = Player 2 (Dummy), 1 = Player 1
        bool m_ShowIdleStanceHUD = false;
        DummyIdleStance m_DummyIdleStance = DummyIdleStance::STANDING;
        int m_StanceTransitionFrames = 0; // Frames of pure neutral before asserting new stance
        bool m_AutoBlockEngaged = false;
        uint64_t m_AutoBlockLastHitFrame = 0;
        WakeupCategory m_ActiveWakeupCategory = WakeupCategory::REGULAR;
        ReversalCategoryConfig m_WakeupInvincibleConfig;
        ReversalCategoryConfig m_WakeupRegularConfig;
        ReversalCategoryConfig m_QuickRollRegularConfig;
        ReversalCategoryConfig m_QuickRollInvincibleConfig;
        ReversalCategoryConfig m_DelayedWakeupConfigSlots;
        ReversalCategoryConfig m_BlockConfig;
        ReversalCategoryConfig m_AfterDamageConfig;

        QuickRollConfig m_QuickRollSettings;
        DelayedWakeupConfig m_DelayedWakeupSettings;
        WakeupRecoveryConfig m_WakeupRecoveryConfig;

        // Active Action Sequence Engine
        ActiveActionSequence m_ActiveSequence;

        // Defender State Tracking Registers
        bool m_Def_WasInBlockstun = false;
        bool m_Def_WasInHitstun = false;
        bool m_Def_WasInKnockdown = false;
        bool m_Def_PendingKnockdown = false;
        int  m_Def_KnockdownRxID = 0;
        bool m_WakeupArmed = false;
        bool m_WakeupReversalTriggered = false;
        bool m_Def_TouchedGroundInKnockdown = false;
        int m_ArmedSlotIndex = -1;
        int m_ArmedInvSlot = -1;
        int m_ArmedRegSlot = -1;
        WakeupRecoveryOption m_CurrentWakeupOption = WakeupRecoveryOption::NORMAL;
        WakeupTelemetryPhase m_TelemetryPhase = WakeupTelemetryPhase::IDLE;
        int m_KnockdownBufferFrame = 0;
        int m_GroundFloorFrames = 0;
        int m_RisingFrames = 0;
        int m_DelayedHoldFramesCount = 0;
        bool m_DelayedDownHeld = false;
        bool m_DelayedWakeupFinished = false;
        int m_DelayedRollStep = 0;
        WORD m_DelayedRollKey = 0;
        bool m_QuickRollTriggered = false;
        bool m_QuickRollActive = false;
        int m_QuickRollFrames = 0;
        int m_QuickRollStep = 0;
        WORD m_QuickRollKey = 0;
        uint32_t m_Def_PrevAction = 0;

        // Post-block recovery lockout before resuming idle block (defaults to 15 frames)
        int m_BlockRecoveryLockout = 0;

        // Execution cooldown / safety debouncer
        uint64_t m_LastTriggerFrame = 0;
    };

}


