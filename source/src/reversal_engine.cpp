#include "reversal_timing_policy.hpp"
#include "reversal_engine.hpp"
#include "frame_data.hpp"
#include "combat_state.hpp"
#include "simulation.hpp"
#include <cstdlib>

namespace MK9TradeFix {

    static bool s_KeyHeldState[256] = { false };

    static void SafeKeyDown(WORD vk, int targetPlayer = 0) {
        if (vk == 0 || vk >= 256) return;
        if (targetPlayer != 0 && g_KeyboardAssignedPlayer != 0 && g_KeyboardAssignedPlayer != targetPlayer) {
            return;
        }
        if (!s_KeyHeldState[vk]) {
            SimulateKeyDown(vk);
            s_KeyHeldState[vk] = true;
        }
    }

    static void SafeKeyUp(WORD vk, int targetPlayer = 0) {
        if (vk == 0 || vk >= 256) return;
        if (targetPlayer != 0 && g_KeyboardAssignedPlayer != 0 && g_KeyboardAssignedPlayer != targetPlayer) {
            return;
        }
        if (s_KeyHeldState[vk]) {
            SimulateKeyUp(vk);
            s_KeyHeldState[vk] = false;
        }
    }

    static void ReleaseAllSimulationKeys() {
        for (int i = 0; i < 256; i++) {
            if (s_KeyHeldState[i]) {
                SimulateKeyUp((WORD)i);
                s_KeyHeldState[i] = false;
            }
        }
    }

    const char* GetReversalActionName(ReversalActionType action) {
        switch (action) {
            // Neutral
            case ReversalActionType::ACTION_NEUTRAL:    return "Neutral (No Action)";

            // General Movement
            case ReversalActionType::MOVE_WALK_FWD:     return "Walk Forward (8F)";
            case ReversalActionType::MOVE_WALK_BACK:    return "Walk Back (8F)";
            case ReversalActionType::MOVE_JUMP_UP:      return "Jump Up (Neutral Jump)";
            case ReversalActionType::MOVE_JUMP_FWD:     return "Jump Forward";
            case ReversalActionType::MOVE_JUMP_BACK:    return "Jump Back";
            case ReversalActionType::MOVE_CROUCH:       return "Crouch (8F)";
            case ReversalActionType::MOVE_DASH_FWD:     return "Dash Forward (F, F)";
            case ReversalActionType::MOVE_BACKDASH:     return "Backdash (B, B)";
            case ReversalActionType::MOVE_STAND_BLOCK:  return "Stand Block (8F)";
            case ReversalActionType::MOVE_CROUCH_BLOCK: return "Crouch Block (8F)";

            // Normal Attacks
            case ReversalActionType::NORM_S1:    return "Stand 1 (Front Punch)";
            case ReversalActionType::NORM_S2:    return "Stand 2 (Back Punch)";
            case ReversalActionType::NORM_S3:    return "Stand 3 (Front Kick)";
            case ReversalActionType::NORM_S4:    return "Stand 4 (Back Kick)";
            case ReversalActionType::NORM_F1:    return "Forward + 1";
            case ReversalActionType::NORM_F2:    return "Forward + 2";
            case ReversalActionType::NORM_F3:    return "Forward + 3";
            case ReversalActionType::NORM_F4:    return "Forward + 4";
            case ReversalActionType::NORM_B1:    return "Back + 1";
            case ReversalActionType::NORM_B2:    return "Back + 2";
            case ReversalActionType::NORM_B3:    return "Back + 3";
            case ReversalActionType::NORM_B4:    return "Back + 4";
            case ReversalActionType::NORM_D1:    return "Down + 1 (Poke)";
            case ReversalActionType::NORM_D2:    return "Down + 2 (Uppercut)";
            case ReversalActionType::NORM_D3:    return "Down + 3 (Low Kick)";
            case ReversalActionType::NORM_D4:    return "Down + 4 (Sweep)";
            case ReversalActionType::NORM_J1:    return "Jump Attack 1 (J1)";
            case ReversalActionType::NORM_J2:    return "Jump Attack 2 (J2)";
            case ReversalActionType::NORM_J3:    return "Jump Attack 3 (J3)";
            case ReversalActionType::NORM_J4:    return "Jump Attack 4 (J4)";
            case ReversalActionType::NORM_THROW: return "Throw (1 + 3)";

            // Special Moves
            case ReversalActionType::SPEC_DF1:  return "Special: D, F + 1";
            case ReversalActionType::SPEC_DF2:  return "Special: D, F + 2";
            case ReversalActionType::SPEC_DF3:  return "Special: D, F + 3";
            case ReversalActionType::SPEC_DF4:  return "Special: D, F + 4";
            case ReversalActionType::SPEC_DB1:  return "Special: D, B + 1";
            case ReversalActionType::SPEC_DB2:  return "Special: D, B + 2";
            case ReversalActionType::SPEC_DB3:  return "Special: D, B + 3";
            case ReversalActionType::SPEC_DB4:  return "Special: D, B + 4";
            case ReversalActionType::SPEC_BF1:  return "Special: B, F + 1";
            case ReversalActionType::SPEC_BF2:  return "Special: B, F + 2";
            case ReversalActionType::SPEC_BF3:  return "Special: B, F + 3";
            case ReversalActionType::SPEC_BF4:  return "Special: B, F + 4";
            case ReversalActionType::SPEC_BB1:  return "Special: B, B + 1";
            case ReversalActionType::SPEC_BB2:  return "Special: B, B + 2";
            case ReversalActionType::SPEC_BB3:  return "Special: B, B + 3";
            case ReversalActionType::SPEC_BB4:  return "Special: B, B + 4";
            case ReversalActionType::SPEC_DU2:  return "Special: D, U + 2";
            case ReversalActionType::SPEC_UD4:  return "Special: U, D + 4";
            case ReversalActionType::SPEC_FF3:  return "Special: F, F + 3";
            case ReversalActionType::SPEC_FF4:  return "Special: F, F + 4";
            case ReversalActionType::SPEC_DFB2: return "Special: D, F, B + 2";
            case ReversalActionType::SPEC_DBF1: return "Special: D, B, F + 1";
            case ReversalActionType::SPEC_DBF2: return "Special: D, B, F + 2";
            case ReversalActionType::SPEC_DBF3: return "Special: D, B, F + 3";

            // EX Specials
            case ReversalActionType::EX_DF1:  return "EX Special: D, F + 1 + Block";
            case ReversalActionType::EX_DF2:  return "EX Special: D, F + 2 + Block";
            case ReversalActionType::EX_DF3:  return "EX Special: D, F + 3 + Block";
            case ReversalActionType::EX_DF4:  return "EX Special: D, F + 4 + Block";
            case ReversalActionType::EX_DB1:  return "EX Special: D, B + 1 + Block";
            case ReversalActionType::EX_DB2:  return "EX Special: D, B + 2 + Block";
            case ReversalActionType::EX_DB3:  return "EX Special: D, B + 3 + Block";
            case ReversalActionType::EX_DB4:  return "EX Special: D, B + 4 + Block";
            case ReversalActionType::EX_BF1:  return "EX Special: B, F + 1 + Block";
            case ReversalActionType::EX_BF2:  return "EX Special: B, F + 2 + Block";
            case ReversalActionType::EX_BF3:  return "EX Special: B, F + 3 + Block";
            case ReversalActionType::EX_BF4:  return "EX Special: B, F + 4 + Block";
            case ReversalActionType::EX_BB1:  return "EX Special: B, B + 1 + Block";
            case ReversalActionType::EX_BB2:  return "EX Special: B, B + 2 + Block";
            case ReversalActionType::EX_BB3:  return "EX Special: B, B + 3 + Block";
            case ReversalActionType::EX_BB4:  return "EX Special: B, B + 4 + Block";
            case ReversalActionType::EX_DU2:  return "EX Special: D, U + 2 + Block";
            case ReversalActionType::EX_UD4:  return "EX Special: U, D + 4 + Block";
            case ReversalActionType::EX_FF3:  return "EX Special: F, F + 3 + Block";
            case ReversalActionType::EX_FF4:  return "EX Special: F, F + 4 + Block";
            case ReversalActionType::EX_DFB2: return "EX Special: D, F, B + 2 + Block";
            case ReversalActionType::EX_DBF1: return "EX Special: D, B, F + 1 + Block";
            case ReversalActionType::EX_DBF2: return "EX Special: D, B, F + 2 + Block";
            case ReversalActionType::EX_DBF3: return "EX Special: D, B, F + 3 + Block";

            // X-Ray
            case ReversalActionType::SUPER_XRAY: return "X-Ray Super (3 + 4 + Block)";
            default: return "Unknown Action";
        }
    }

    const char* GetReversalActionCategory(ReversalActionType action) {
        if (action == ReversalActionType::ACTION_NEUTRAL) {
            return "NEUTRAL";
        }
        int idx = (int)action;
        if (idx >= (int)ReversalActionType::MOVE_WALK_FWD && idx <= (int)ReversalActionType::MOVE_CROUCH_BLOCK) {
            return "GENERAL MOVEMENT";
        }
        if (idx >= (int)ReversalActionType::NORM_S1 && idx <= (int)ReversalActionType::NORM_THROW) {
            return "NORMAL ATTACKS";
        }
        if (idx >= (int)ReversalActionType::SPEC_DF1 && idx <= (int)ReversalActionType::SPEC_DBF3) {
            return "SPECIAL MOVES";
        }
        if (idx >= (int)ReversalActionType::EX_DF1 && idx <= (int)ReversalActionType::EX_DBF3) {
            return "EX SPECIAL MOVES (ARMOR)";
        }
        if (action == ReversalActionType::SUPER_XRAY) {
            return "X-RAY / SUPER";
        }
        return "OTHER";
    }

    ReversalEngine& ReversalEngine::Get() {
        static ReversalEngine instance;
        return instance;
    }

    ReversalEngine::ReversalEngine() {
        Initialize();
    }

    void ReversalEngine::Initialize() {
        m_GlobalReversalEnabled = true;
        m_WakeupInvincibleConfig = ReversalCategoryConfig();
        m_WakeupInvincibleConfig.slots[0].actionType = ReversalActionType::EX_BF4;
        m_WakeupInvincibleConfig.slots[1].actionType = ReversalActionType::EX_DF1;
        m_WakeupInvincibleConfig.slots[2].actionType = ReversalActionType::EX_DB1;
        m_WakeupInvincibleConfig.slots[3].actionType = ReversalActionType::EX_DF2;
        m_WakeupInvincibleConfig.slots[4].actionType = ReversalActionType::SPEC_BF4;
        m_WakeupInvincibleConfig.slots[5].actionType = ReversalActionType::SPEC_DF1;
        m_WakeupInvincibleConfig.slots[6].actionType = ReversalActionType::SPEC_DB1;
        m_WakeupInvincibleConfig.slots[7].actionType = ReversalActionType::SPEC_DF2;

        m_WakeupRegularConfig = ReversalCategoryConfig();
        m_QuickRollRegularConfig = ReversalCategoryConfig();
        m_QuickRollInvincibleConfig = ReversalCategoryConfig();
        m_QuickRollInvincibleConfig.slots[0].actionType = ReversalActionType::EX_BF4;
        m_QuickRollInvincibleConfig.slots[1].actionType = ReversalActionType::EX_DF1;
        m_QuickRollInvincibleConfig.slots[2].actionType = ReversalActionType::EX_DB1;
        m_QuickRollInvincibleConfig.slots[3].actionType = ReversalActionType::EX_DF2;
        m_QuickRollInvincibleConfig.slots[4].actionType = ReversalActionType::SPEC_BF4;
        m_QuickRollInvincibleConfig.slots[5].actionType = ReversalActionType::SPEC_DF1;
        m_QuickRollInvincibleConfig.slots[6].actionType = ReversalActionType::SPEC_DB1;
        m_QuickRollInvincibleConfig.slots[7].actionType = ReversalActionType::SPEC_DF2;

        m_DelayedWakeupConfigSlots = ReversalCategoryConfig();
        m_BlockConfig = ReversalCategoryConfig();
        m_AfterDamageConfig = ReversalCategoryConfig();

        m_QuickRollSettings = QuickRollConfig();
        m_DelayedWakeupSettings = DelayedWakeupConfig();

        m_ActiveSequence.ReleaseAllHeld();
        m_ActiveSequence.active = false;
        ReleaseAllSimulationKeys();

        m_Def_WasInBlockstun = false;
        m_Def_WasInHitstun = false;
        m_Def_WasInKnockdown = false;
        m_Def_PendingKnockdown = false;
        m_Def_KnockdownRxID = 0;
        m_WakeupArmed = false;
        m_WakeupReversalTriggered = false;
        m_Def_TouchedGroundInKnockdown = false;
        m_ArmedSlotIndex = -1;
        m_KnockdownBufferFrame = 0;
        m_GroundFloorFrames = 0;
        m_RisingFrames = 0;
        m_DelayedHoldFramesCount = 0;
        m_DelayedDownHeld = false;
        m_DelayedWakeupFinished = false;
        m_DelayedRollStep = 0;
        m_DelayedRollKey = 0;
        m_QuickRollTriggered = false;
        m_QuickRollActive = false;
        m_QuickRollFrames = 0;
        m_QuickRollStep = 0;
        m_Def_PrevAction = 0;
        m_LastTriggerFrame = 0;
        m_DummyIdleStance = DummyIdleStance::STANDING;
        m_StanceTransitionFrames = 0;
        m_ActiveWakeupCategory = WakeupCategory::REGULAR;
    }

    void ReversalEngine::SetDummyIdleStance(DummyIdleStance stance) {
        m_DummyIdleStance = stance;
        m_StanceTransitionFrames = 2; // Always force 2 frames of pure neutral before asserting new stance
        m_AutoBlockEngaged = false;
        m_AutoBlockLastHitFrame = 0;

        // Release all simulated stance keyboard keys immediately on both players
        SafeKeyUp(VK_DOWN);
        SafeKeyUp(VK_NUMPAD0);
        SafeKeyUp(VK_DECIMAL);
        SafeKeyUp('S');
        SafeKeyUp(VK_SPACE);
        SafeKeyUp('A');
        SafeKeyUp('D');
        SafeKeyUp(VK_LEFT);
        SafeKeyUp(VK_RIGHT);
    }

    void ReversalEngine::Reset() {
        Initialize();
    }

    ReversalCategoryConfig& ReversalEngine::GetConfig(ReversalType type) {
        switch (type) {
            case ReversalType::WAKEUP_INVINCIBLE:     return m_WakeupInvincibleConfig;
            case ReversalType::WAKEUP_REGULAR:        return m_WakeupRegularConfig;
            case ReversalType::QUICK_ROLL_REGULAR:    return m_QuickRollRegularConfig;
            case ReversalType::QUICK_ROLL_INVINCIBLE: return m_QuickRollInvincibleConfig;
            case ReversalType::DELAYED_WAKEUP:        return m_DelayedWakeupConfigSlots;
            case ReversalType::BLOCK:                 return m_BlockConfig;
            case ReversalType::AFTER_DAMAGE:          return m_AfterDamageConfig;
            default: break;
        }
        return m_BlockConfig;
    }

    const ReversalCategoryConfig& ReversalEngine::GetConfig(ReversalType type) const {
        switch (type) {
            case ReversalType::WAKEUP_INVINCIBLE:     return m_WakeupInvincibleConfig;
            case ReversalType::WAKEUP_REGULAR:        return m_WakeupRegularConfig;
            case ReversalType::QUICK_ROLL_REGULAR:    return m_QuickRollRegularConfig;
            case ReversalType::QUICK_ROLL_INVINCIBLE: return m_QuickRollInvincibleConfig;
            case ReversalType::DELAYED_WAKEUP:        return m_DelayedWakeupConfigSlots;
            case ReversalType::BLOCK:                 return m_BlockConfig;
            case ReversalType::AFTER_DAMAGE:          return m_AfterDamageConfig;
            default: break;
        }
        return m_BlockConfig;
    }

    void ReversalEngine::ExecutePresetAction(ReversalActionType action, int targetPlayer, int delayFrames) {
        m_ActiveSequence.ReleaseAllHeld();
        ReleaseAllSimulationKeys();
        m_ActiveSequence.active = true;
        m_ActiveSequence.action = action;
        m_ActiveSequence.targetPlayer = targetPlayer;
        m_ActiveSequence.frameIndex = 0;
        m_ActiveSequence.delayRemaining = delayFrames;

        if (delayFrames > 0 && targetPlayer == m_TargetPlayer) {
            if (g_KeyboardAssignedPlayer == 0 || g_KeyboardAssignedPlayer == targetPlayer) {
                bool blockLockout = (m_BlockRecoveryLockout > 0);
                if (targetPlayer == 2) {
                    if (m_DummyIdleStance == DummyIdleStance::CROUCH_BLOCKING) {
                        SafeKeyDown(VK_DOWN, 2);
                        if (!blockLockout) {
                            SafeKeyDown(VK_NUMPAD0, 2);
                            SafeKeyDown(VK_DECIMAL, 2);
                        }
                    } else if (m_DummyIdleStance == DummyIdleStance::STAND_BLOCKING) {
                        if (!blockLockout) {
                            SafeKeyDown(VK_NUMPAD0, 2);
                            SafeKeyDown(VK_DECIMAL, 2);
                        }
                    } else if (m_DummyIdleStance == DummyIdleStance::CROUCHING) {
                        SafeKeyDown(VK_DOWN, 2);
                    }
                } else if (targetPlayer == 1) {
                    if (m_DummyIdleStance == DummyIdleStance::CROUCH_BLOCKING) {
                        SafeKeyDown('S', 1);
                        if (!blockLockout) {
                            SafeKeyDown(VK_SPACE, 1);
                        }
                    } else if (m_DummyIdleStance == DummyIdleStance::STAND_BLOCKING) {
                        if (!blockLockout) {
                            SafeKeyDown(VK_SPACE, 1);
                        }
                    } else if (m_DummyIdleStance == DummyIdleStance::CROUCHING) {
                        SafeKeyDown('S', 1);
                    }
                }
            }
        }
    }

    void ReversalEngine::TickActionSequence(void* p1Char, void* p2Char) {
        if (!m_ActiveSequence.active) return;

        int targetPlayer = m_ActiveSequence.targetPlayer;

        if (m_ActiveSequence.delayRemaining > 0) {
            m_ActiveSequence.delayRemaining--;
            if (m_ActiveSequence.delayRemaining > 0) {
                if (targetPlayer == m_TargetPlayer && (g_KeyboardAssignedPlayer == 0 || g_KeyboardAssignedPlayer == targetPlayer)) {
                    bool blockLockout = (m_BlockRecoveryLockout > 0);
                    WORD downKey = (targetPlayer == 1) ? 'S' : VK_DOWN;
                    // If delayRemaining == 1, release DOWN for 1 frame so that Frame 0 (next frame) gets a clean fresh press edge!
                    if (m_ActiveSequence.delayRemaining == 1) {
                        SafeKeyUp(downKey, targetPlayer);
                    } else {
                        if (m_DummyIdleStance == DummyIdleStance::CROUCH_BLOCKING || m_DummyIdleStance == DummyIdleStance::CROUCHING) {
                            SafeKeyDown(downKey, targetPlayer);
                        }
                    }
                    if (m_DummyIdleStance == DummyIdleStance::STAND_BLOCKING || m_DummyIdleStance == DummyIdleStance::CROUCH_BLOCKING) {
                        if (targetPlayer == 1) {
                            if (blockLockout) SafeKeyUp(VK_SPACE, 1);
                            else SafeKeyDown(VK_SPACE, 1);
                        } else {
                            if (blockLockout) {
                                SafeKeyUp(VK_NUMPAD0, 2);
                                SafeKeyUp(VK_DECIMAL, 2);
                            } else {
                                SafeKeyDown(VK_NUMPAD0, 2);
                                SafeKeyDown(VK_DECIMAL, 2);
                            }
                        }
                    }
                }
                return;
            }
            // delayRemaining just reached 0! Fall through so Frame 0 executes on this exact frame
        }

        if (m_ActiveSequence.frameIndex == 0) {
            m_ActiveSequence.ReleaseAllHeld();
            ReleaseAllSimulationKeys();
        }

        // Opponent-relative directional orientation
        float p1_x = 0.0f, p2_x = 0.0f;
        if (p1Char && (uintptr_t)p1Char > 0x01000000) {
            p1_x = *(float*)((uintptr_t)p1Char + 0x1E8);
        }
        if (p2Char && (uintptr_t)p2Char > 0x01000000) {
            p2_x = *(float*)((uintptr_t)p2Char + 0x1E8);
        }

        bool p1FacesRight = (p2_x >= p1_x);
        bool p2FacesRight = (p1_x >= p2_x);

        auto HoldUp = [&]() {
            if (targetPlayer == 1) {
                m_ActiveSequence.HoldKey('W');
                m_ActiveSequence.HoldKey(VK_UP);
            } else {
                m_ActiveSequence.HoldKey(VK_UP);
            }
        };

        auto HoldDown = [&]() {
            if (targetPlayer == 1) {
                m_ActiveSequence.HoldKey('S');
                m_ActiveSequence.HoldKey(VK_DOWN);
            } else {
                m_ActiveSequence.HoldKey(VK_DOWN);
            }
        };

        auto HoldLeft = [&]() {
            if (targetPlayer == 1) {
                m_ActiveSequence.HoldKey('A');
                m_ActiveSequence.HoldKey(VK_LEFT);
            } else {
                m_ActiveSequence.HoldKey(VK_LEFT);
            }
        };

        auto HoldRight = [&]() {
            if (targetPlayer == 1) {
                m_ActiveSequence.HoldKey('D');
                m_ActiveSequence.HoldKey(VK_RIGHT);
            } else {
                m_ActiveSequence.HoldKey(VK_RIGHT);
            }
        };

        auto HoldFwd = [&]() {
            if (targetPlayer == 1) {
                m_ActiveSequence.HoldKey(p1FacesRight ? 'D' : 'A');
                m_ActiveSequence.HoldKey(p1FacesRight ? VK_RIGHT : VK_LEFT);
            } else {
                m_ActiveSequence.HoldKey(p2FacesRight ? VK_RIGHT : VK_LEFT);
            }
        };

        auto HoldBack = [&]() {
            if (targetPlayer == 1) {
                m_ActiveSequence.HoldKey(p1FacesRight ? 'A' : 'D');
                m_ActiveSequence.HoldKey(p1FacesRight ? VK_LEFT : VK_RIGHT);
            } else {
                m_ActiveSequence.HoldKey(p2FacesRight ? VK_LEFT : VK_RIGHT);
            }
        };

        enum class StepDir { UP, DOWN, LEFT, RIGHT, FWD, BACK };

        auto HoldStepDir = [&](StepDir dir) {
            switch (dir) {
                case StepDir::UP:    HoldUp(); break;
                case StepDir::DOWN:  HoldDown(); break;
                case StepDir::LEFT:  HoldLeft(); break;
                case StepDir::RIGHT: HoldRight(); break;
                case StepDir::FWD:   HoldFwd(); break;
                case StepDir::BACK:  HoldBack(); break;
            }
        };

        auto HoldFP = [&]() {
            if (targetPlayer == 1) {
                m_ActiveSequence.HoldKey('U');
                m_ActiveSequence.HoldKey('T');
            } else {
                m_ActiveSequence.HoldKey(VK_NUMPAD7);
            }
        };

        auto HoldBP = [&]() {
            if (targetPlayer == 1) {
                m_ActiveSequence.HoldKey('I');
                m_ActiveSequence.HoldKey('Y');
            } else {
                m_ActiveSequence.HoldKey(VK_NUMPAD9);
                m_ActiveSequence.HoldKey(VK_NUMPAD8);
            }
        };

        auto HoldFK = [&]() {
            if (targetPlayer == 1) {
                m_ActiveSequence.HoldKey('J');
                m_ActiveSequence.HoldKey('G');
            } else {
                m_ActiveSequence.HoldKey(VK_NUMPAD4);
            }
        };

        auto HoldBK = [&]() {
            if (targetPlayer == 1) {
                m_ActiveSequence.HoldKey('K');
                m_ActiveSequence.HoldKey('H');
                m_ActiveSequence.HoldKey('L');
            } else {
                m_ActiveSequence.HoldKey(VK_NUMPAD6);
                m_ActiveSequence.HoldKey(VK_NUMPAD5);
            }
        };

        auto HoldBlock = [&]() {
            if (targetPlayer == 1) {
                m_ActiveSequence.HoldKey(VK_SPACE);
            } else {
                m_ActiveSequence.HoldKey(VK_NUMPAD0);
                m_ActiveSequence.HoldKey(VK_DECIMAL);
            }
        };

        auto HoldStance = [&]() {
            if (targetPlayer == 1) {
                m_ActiveSequence.HoldKey('P');
                m_ActiveSequence.HoldKey(VK_LCONTROL);
            } else {
                m_ActiveSequence.HoldKey(VK_ADD);
            }
        };

        auto HoldBtn = [&](int btnIdx) {
            if (btnIdx == 1) HoldFP();
            else if (btnIdx == 2) HoldBP();
            else if (btnIdx == 3) HoldFK();
            else if (btnIdx == 4) HoldBK();
        };

        int f = m_ActiveSequence.frameIndex;
        ReversalActionType act = m_ActiveSequence.action;

        auto EndAction = [&]() {
            m_ActiveSequence.ReleaseAllHeld();
            ReleaseAllSimulationKeys();
            m_ActiveSequence.active = false;
        };

        // 0. NEUTRAL (Stand / No Action)
        if (act == ReversalActionType::ACTION_NEUTRAL) {
            EndAction();
            return;
        }

        // 1. GENERAL MOVEMENT
        if (act == ReversalActionType::MOVE_WALK_FWD) {
            if (f == 0) HoldFwd();
            if (f >= 30) EndAction();
        } else if (act == ReversalActionType::MOVE_WALK_BACK) {
            if (f == 0) HoldBack();
            if (f >= 30) EndAction();
        } else if (act == ReversalActionType::MOVE_JUMP_UP) {
            if (f == 0) HoldUp();
            if (f >= 30) EndAction();
        } else if (act == ReversalActionType::MOVE_JUMP_FWD) {
            if (f == 0) { HoldUp(); HoldFwd(); }
            if (f >= 30) EndAction();
        } else if (act == ReversalActionType::MOVE_JUMP_BACK) {
            if (f == 0) { HoldUp(); HoldBack(); }
            if (f >= 30) EndAction();
        } else if (act == ReversalActionType::MOVE_CROUCH) {
            if (f == 0) HoldDown();
            if (f >= 30) EndAction();
        } else if (act == ReversalActionType::MOVE_DASH_FWD) {
            if (f <= 1) HoldFwd();
            else if (f == 2) m_ActiveSequence.ReleaseAllHeld();
            else if (f >= 3 && f < 30) HoldFwd();
            if (f >= 30) EndAction();
        } else if (act == ReversalActionType::MOVE_BACKDASH) {
            if (f <= 1) HoldBack();
            else if (f == 2) m_ActiveSequence.ReleaseAllHeld();
            else if (f >= 3 && f < 30) HoldBack();
            if (f >= 30) EndAction();
        } else if (act == ReversalActionType::MOVE_STAND_BLOCK) {
            if (f <= 7) HoldBlock();
            if (f >= 8) EndAction();
        } else if (act == ReversalActionType::MOVE_CROUCH_BLOCK) {
            if (f <= 7) { HoldDown(); HoldBlock(); }
            if (f >= 8) EndAction();
        }
        // 2. NORMAL ATTACKS & COMMAND NORMALS
        else if (act == ReversalActionType::NORM_S1) {
            if (f <= 5) HoldFP();
            if (f > 5) EndAction();
        } else if (act == ReversalActionType::NORM_S2) {
            if (f <= 5) HoldBP();
            if (f > 5) EndAction();
        } else if (act == ReversalActionType::NORM_S3) {
            if (f <= 5) HoldFK();
            if (f > 5) EndAction();
        } else if (act == ReversalActionType::NORM_S4) {
            if (f <= 5) HoldBK();
            if (f > 5) EndAction();
        } else if (act == ReversalActionType::NORM_F1) {
            if (f <= 5) { HoldFwd(); HoldFP(); }
            if (f > 5) EndAction();
        } else if (act == ReversalActionType::NORM_F2) {
            if (f <= 5) { HoldFwd(); HoldBP(); }
            if (f > 5) EndAction();
        } else if (act == ReversalActionType::NORM_F3) {
            if (f <= 5) { HoldFwd(); HoldFK(); }
            if (f > 5) EndAction();
        } else if (act == ReversalActionType::NORM_F4) {
            if (f <= 5) { HoldFwd(); HoldBK(); }
            if (f > 5) EndAction();
        } else if (act == ReversalActionType::NORM_B1) {
            if (f <= 5) { HoldBack(); HoldFP(); }
            if (f > 5) EndAction();
        } else if (act == ReversalActionType::NORM_B2) {
            if (f <= 5) { HoldBack(); HoldBP(); }
            if (f > 5) EndAction();
        } else if (act == ReversalActionType::NORM_B3) {
            if (f <= 5) { HoldBack(); HoldFK(); }
            if (f > 5) EndAction();
        } else if (act == ReversalActionType::NORM_B4) {
            if (f <= 5) { HoldBack(); HoldBK(); }
            if (f > 5) EndAction();
        } else if (act == ReversalActionType::NORM_D1) {
            if (f <= 5) { HoldDown(); HoldFP(); }
            if (f > 5) EndAction();
        } else if (act == ReversalActionType::NORM_D2) {
            if (f <= 5) { HoldDown(); HoldBP(); }
            if (f > 5) EndAction();
        } else if (act == ReversalActionType::NORM_D3) {
            if (f <= 5) { HoldDown(); HoldFK(); }
            if (f > 5) EndAction();
        } else if (act == ReversalActionType::NORM_D4) {
            if (f <= 5) { HoldDown(); HoldBK(); }
            if (f > 5) EndAction();
        } else if (act == ReversalActionType::NORM_J1) {
            if (f <= 5) { HoldUp(); HoldFwd(); }
            else if (f >= 6 && f <= 10) { if (f == 6) m_ActiveSequence.ReleaseAllHeld(); HoldFP(); }
            if (f >= 10) EndAction();
        } else if (act == ReversalActionType::NORM_J2) {
            if (f <= 5) { HoldUp(); HoldFwd(); }
            else if (f >= 6 && f <= 10) { if (f == 6) m_ActiveSequence.ReleaseAllHeld(); HoldBP(); }
            if (f >= 10) EndAction();
        } else if (act == ReversalActionType::NORM_J3) {
            if (f <= 5) { HoldUp(); HoldFwd(); }
            else if (f >= 6 && f <= 10) { if (f == 6) m_ActiveSequence.ReleaseAllHeld(); HoldFK(); }
            if (f >= 10) EndAction();
        } else if (act == ReversalActionType::NORM_J4) {
            if (f <= 5) { HoldUp(); HoldFwd(); }
            else if (f >= 6 && f <= 10) { if (f == 6) m_ActiveSequence.ReleaseAllHeld(); HoldBK(); }
            if (f >= 10) EndAction();
        } else if (act == ReversalActionType::NORM_THROW) {
            if (f == 0) { HoldFP(); HoldFK(); }
            if (f >= 3) EndAction();
        }
        // 3. SPECIALS & EX SPECIALS (Multi-step motion sequence held into recovery)
        else {
            auto Do2StepSpecial = [&](StepDir d1, StepDir d2, int btnIdx, bool isEx) {
                bool isQuarterCircle = (d1 == StepDir::DOWN && (d2 == StepDir::FWD || d2 == StepDir::BACK));
                bool isOppositeCardinals = ((d1 == StepDir::DOWN && d2 == StepDir::UP) ||
                                            (d1 == StepDir::UP && d2 == StepDir::DOWN) ||
                                            (d1 == StepDir::BACK && d2 == StepDir::FWD) ||
                                            (d1 == StepDir::FWD && d2 == StepDir::BACK));

                if (isQuarterCircle) {
                    // Quarter circle motion (D, F or D, B):
                    // Frame 0: Down
                    // Frame 1: Down + Fwd/Back (Diagonal) -> Required for Alternate Control mode and smooth buffer rolling
                    // Frame 2..10: Fwd/Back + Button (+ Block if EX)
                    if (f == 0) {
                        HoldStepDir(d1);
                    } else if (f == 1) {
                        HoldStepDir(d1);
                        HoldStepDir(d2);
                    } else if (f >= 2 && f <= 10) {
                        if (f == 2) m_ActiveSequence.ReleaseAllHeld();
                        HoldStepDir(d2);
                        HoldBtn(btnIdx);
                        if (isEx) HoldBlock();
                    } else if (f > 10) {
                        EndAction();
                    }
                } else if (d1 == d2 || isOppositeCardinals) {
                    // Double-taps (B, B or F, F) or opposite cardinals (D, U or U, D):
                    // Frame 0: d1
                    // Frame 1: Neutral release
                    // Frame 2..10: d2 + Button (+ Block if EX)
                    if (f == 0) {
                        HoldStepDir(d1);
                    } else if (f == 1) {
                        m_ActiveSequence.ReleaseAllHeld();
                    } else if (f >= 2 && f <= 10) {
                        HoldStepDir(d2);
                        HoldBtn(btnIdx);
                        if (isEx) HoldBlock();
                    } else if (f > 10) {
                        EndAction();
                    }
                } else {
                    // Direct 2-cardinal moves (B, F or F, B):
                    // Frame 0: d1
                    // Frame 1..9: d2 + Button (+ Block if EX)
                    if (f == 0) {
                        HoldStepDir(d1);
                    } else if (f >= 1 && f <= 9) {
                        if (f == 1) m_ActiveSequence.ReleaseAllHeld();
                        HoldStepDir(d2);
                        HoldBtn(btnIdx);
                        if (isEx) HoldBlock();
                    } else if (f > 9) {
                        EndAction();
                    }
                }
            };

            auto Do3StepSpecial = [&](StepDir d1, StepDir d2, StepDir d3, int btnIdx, bool isEx) {
                // 3-step specials (D, F, B or D, B, F):
                // Frame 0: Down (d1)
                // Frame 1: Down + d2 (Diagonal)
                // Frame 2: d2
                // Frame 3..11: d3 + Button (+ Block if EX)
                if (f == 0) {
                    HoldStepDir(d1);
                } else if (f == 1) {
                    HoldStepDir(d1);
                    HoldStepDir(d2);
                } else if (f == 2) {
                    m_ActiveSequence.ReleaseAllHeld();
                    HoldStepDir(d2);
                } else if (f >= 3 && f <= 11) {
                    if (f == 3) m_ActiveSequence.ReleaseAllHeld();
                    HoldStepDir(d3);
                    HoldBtn(btnIdx);
                    if (isEx) HoldBlock();
                } else if (f > 11) {
                    EndAction();
                }
            };

            switch (act) {
                // Regular Specials
                case ReversalActionType::SPEC_DF1: Do2StepSpecial(StepDir::DOWN, StepDir::FWD, 1, false); break;
                case ReversalActionType::SPEC_DF2: Do2StepSpecial(StepDir::DOWN, StepDir::FWD, 2, false); break;
                case ReversalActionType::SPEC_DF3: Do2StepSpecial(StepDir::DOWN, StepDir::FWD, 3, false); break;
                case ReversalActionType::SPEC_DF4: Do2StepSpecial(StepDir::DOWN, StepDir::FWD, 4, false); break;

                case ReversalActionType::SPEC_DB1: Do2StepSpecial(StepDir::DOWN, StepDir::BACK, 1, false); break;
                case ReversalActionType::SPEC_DB2: Do2StepSpecial(StepDir::DOWN, StepDir::BACK, 2, false); break;
                case ReversalActionType::SPEC_DB3: Do2StepSpecial(StepDir::DOWN, StepDir::BACK, 3, false); break;
                case ReversalActionType::SPEC_DB4: Do2StepSpecial(StepDir::DOWN, StepDir::BACK, 4, false); break;

                case ReversalActionType::SPEC_BF1: Do2StepSpecial(StepDir::BACK, StepDir::FWD, 1, false); break;
                case ReversalActionType::SPEC_BF2: Do2StepSpecial(StepDir::BACK, StepDir::FWD, 2, false); break;
                case ReversalActionType::SPEC_BF3: Do2StepSpecial(StepDir::BACK, StepDir::FWD, 3, false); break;
                case ReversalActionType::SPEC_BF4: Do2StepSpecial(StepDir::BACK, StepDir::FWD, 4, false); break;

                case ReversalActionType::SPEC_BB1: Do2StepSpecial(StepDir::BACK, StepDir::BACK, 1, false); break;
                case ReversalActionType::SPEC_BB2: Do2StepSpecial(StepDir::BACK, StepDir::BACK, 2, false); break;
                case ReversalActionType::SPEC_BB3: Do2StepSpecial(StepDir::BACK, StepDir::BACK, 3, false); break;
                case ReversalActionType::SPEC_BB4: Do2StepSpecial(StepDir::BACK, StepDir::BACK, 4, false); break;

                case ReversalActionType::SPEC_DU2: Do2StepSpecial(StepDir::DOWN, StepDir::UP, 2, false); break;
                case ReversalActionType::SPEC_UD4: Do2StepSpecial(StepDir::UP, StepDir::DOWN, 4, false); break;
                case ReversalActionType::SPEC_FF3: Do2StepSpecial(StepDir::FWD, StepDir::FWD, 3, false); break;
                case ReversalActionType::SPEC_FF4: Do2StepSpecial(StepDir::FWD, StepDir::FWD, 4, false); break;

                case ReversalActionType::SPEC_DFB2: Do3StepSpecial(StepDir::DOWN, StepDir::FWD, StepDir::BACK, 2, false); break;
                case ReversalActionType::SPEC_DBF1: Do3StepSpecial(StepDir::DOWN, StepDir::BACK, StepDir::FWD, 1, false); break;
                case ReversalActionType::SPEC_DBF2: Do3StepSpecial(StepDir::DOWN, StepDir::BACK, StepDir::FWD, 2, false); break;
                case ReversalActionType::SPEC_DBF3: Do3StepSpecial(StepDir::DOWN, StepDir::BACK, StepDir::FWD, 3, false); break;

                // EX Specials (+ Block)
                case ReversalActionType::EX_DF1: Do2StepSpecial(StepDir::DOWN, StepDir::FWD, 1, true); break;
                case ReversalActionType::EX_DF2: Do2StepSpecial(StepDir::DOWN, StepDir::FWD, 2, true); break;
                case ReversalActionType::EX_DF3: Do2StepSpecial(StepDir::DOWN, StepDir::FWD, 3, true); break;
                case ReversalActionType::EX_DF4: Do2StepSpecial(StepDir::DOWN, StepDir::FWD, 4, true); break;

                case ReversalActionType::EX_DB1: Do2StepSpecial(StepDir::DOWN, StepDir::BACK, 1, true); break;
                case ReversalActionType::EX_DB2: Do2StepSpecial(StepDir::DOWN, StepDir::BACK, 2, true); break;
                case ReversalActionType::EX_DB3: Do2StepSpecial(StepDir::DOWN, StepDir::BACK, 3, true); break;
                case ReversalActionType::EX_DB4: Do2StepSpecial(StepDir::DOWN, StepDir::BACK, 4, true); break;

                case ReversalActionType::EX_BF1: Do2StepSpecial(StepDir::BACK, StepDir::FWD, 1, true); break;
                case ReversalActionType::EX_BF2: Do2StepSpecial(StepDir::BACK, StepDir::FWD, 2, true); break;
                case ReversalActionType::EX_BF3: Do2StepSpecial(StepDir::BACK, StepDir::FWD, 3, true); break;
                case ReversalActionType::EX_BF4: Do2StepSpecial(StepDir::BACK, StepDir::FWD, 4, true); break;

                case ReversalActionType::EX_BB1: Do2StepSpecial(StepDir::BACK, StepDir::BACK, 1, true); break;
                case ReversalActionType::EX_BB2: Do2StepSpecial(StepDir::BACK, StepDir::BACK, 2, true); break;
                case ReversalActionType::EX_BB3: Do2StepSpecial(StepDir::BACK, StepDir::BACK, 3, true); break;
                case ReversalActionType::EX_BB4: Do2StepSpecial(StepDir::BACK, StepDir::BACK, 4, true); break;

                case ReversalActionType::EX_DU2: Do2StepSpecial(StepDir::DOWN, StepDir::UP, 2, true); break;
                case ReversalActionType::EX_UD4: Do2StepSpecial(StepDir::UP, StepDir::DOWN, 4, true); break;
                case ReversalActionType::EX_FF3: Do2StepSpecial(StepDir::FWD, StepDir::FWD, 3, true); break;
                case ReversalActionType::EX_FF4: Do2StepSpecial(StepDir::FWD, StepDir::FWD, 4, true); break;

                case ReversalActionType::EX_DFB2: Do3StepSpecial(StepDir::DOWN, StepDir::FWD, StepDir::BACK, 2, true); break;
                case ReversalActionType::EX_DBF1: Do3StepSpecial(StepDir::DOWN, StepDir::BACK, StepDir::FWD, 1, true); break;
                case ReversalActionType::EX_DBF2: Do3StepSpecial(StepDir::DOWN, StepDir::BACK, StepDir::FWD, 2, true); break;
                case ReversalActionType::EX_DBF3: Do3StepSpecial(StepDir::DOWN, StepDir::BACK, StepDir::FWD, 3, true); break;

                // X-Ray Super
                case ReversalActionType::SUPER_XRAY:
                    if (f == 0) {
                        HoldFK();
                        HoldBK();
                        HoldBlock();
                        HoldStance();
                    }
                    if (f >= 4) EndAction();
                    break;

                default:
                    EndAction();
                    break;
            }
        }

        if (m_ActiveSequence.active) {
            m_ActiveSequence.SendHeldDown();

            char heldStr[128] = { 0 };
            for (int i = 0; i < m_ActiveSequence.heldKeyCount; i++) {
                char kName[16] = { 0 };
                WORD vk = m_ActiveSequence.heldKeys[i];
                if (vk == VK_UP || vk == 'W') strcpy_s(kName, "UP");
                else if (vk == VK_DOWN || vk == 'S') strcpy_s(kName, "DOWN");
                else if (vk == VK_LEFT || vk == 'A') strcpy_s(kName, "LEFT");
                else if (vk == VK_RIGHT || vk == 'D') strcpy_s(kName, "RIGHT");
                else if (vk == VK_NUMPAD7 || vk == 'T') strcpy_s(kName, "1/FP");
                else if (vk == VK_NUMPAD8 || vk == VK_NUMPAD9 || vk == 'U' || vk == 'Y' || vk == 'I') strcpy_s(kName, "2/BP");
                else if (vk == VK_NUMPAD4 || vk == VK_NUMPAD1 || vk == 'G') strcpy_s(kName, "3/FK");
                else if (vk == VK_NUMPAD5 || vk == VK_NUMPAD6 || vk == VK_NUMPAD2 || vk == VK_NUMPAD3 || vk == 'J' || vk == 'H' || vk == 'K') strcpy_s(kName, "4/BK");
                else if (vk == VK_NUMPAD0 || vk == VK_DECIMAL || vk == VK_SPACE) strcpy_s(kName, "BLOCK");
                else if (vk == VK_ADD || vk == VK_LCONTROL || vk == VK_RCONTROL) strcpy_s(kName, "STANCE");
                else sprintf_s(kName, "0x%X", vk);

                if (i > 0) strcat_s(heldStr, "+");
                strcat_s(heldStr, kName);
            }

            if (f <= 8 || f % 5 == 0) {
                LogTrace("[REVERSAL SEQ | F%llu] P%d %s f=%d: Held=[%s]",
                         g_CurrentFrame, targetPlayer, GetReversalActionName(act), f, (heldStr[0] ? heldStr : "NONE"));
            }
        }

        m_ActiveSequence.frameIndex++;
    }

    int ReversalEngine::PickWeightedSlot(const ReversalCategoryConfig& cfg) {
        int totalWeight = 0;
        std::vector<int> candidates;
        for (int i = 0; i < ReversalCategoryConfig::MAX_CONFIG_SLOTS; i++) {
            if (cfg.slots[i].enabled) {
                int w = (cfg.slots[i].weight > 0) ? cfg.slots[i].weight : 1;
                totalWeight += w;
                candidates.push_back(i);
            }
        }
        if (candidates.empty() || totalWeight <= 0) return -1;

        int r = rand() % totalWeight;
        int acc = 0;
        for (int idx : candidates) {
            int w = (cfg.slots[idx].weight > 0) ? cfg.slots[idx].weight : 1;
            acc += w;
            if (r < acc) return idx;
        }
        return candidates.front();
    }

    void ReversalEngine::TriggerReversal(ReversalType type, int targetPlayer) {
        const char* revName = (type == ReversalType::WAKEUP_INVINCIBLE) ? "INVINCIBLE WAKE-UP" :
                              (type == ReversalType::WAKEUP_REGULAR)    ? "REGULAR WAKE-UP" :
                              (type == ReversalType::BLOCK)             ? "BLOCK" : "DAMAGE RECOVERY";

        LogTrace("[REVERSAL ENGINE | F%llu] TriggerReversal invoked for %s on P%d", g_CurrentFrame, revName, targetPlayer);

        if (!m_GlobalReversalEnabled) {
            LogTrace("[REVERSAL ENGINE | F%llu] TriggerReversal aborted: m_GlobalReversalEnabled is false", g_CurrentFrame);
            return;
        }
        if (!IsTrainingMode()) {
            LogTrace("[REVERSAL ENGINE | F%llu] TriggerReversal aborted: IsTrainingMode() is false", g_CurrentFrame);
            return;
        }
        if (targetPlayer == 2 && !IsP2HumanControl()) {
            LogTrace("[REVERSAL ENGINE | F%llu] TriggerReversal aborted: IsP2HumanControl() is false", g_CurrentFrame);
            return;
        }
        if (g_CurrentFrame <= m_LastTriggerFrame + 3) {
            LogTrace("[REVERSAL ENGINE | F%llu] TriggerReversal debounced: F%llu <= last %llu + 3", g_CurrentFrame, g_CurrentFrame, m_LastTriggerFrame);
            return;
        }

        const ReversalCategoryConfig& cfg = GetConfig(type);
        int chosenIndex = PickWeightedSlot(cfg);
        if (chosenIndex < 0) {
            LogTrace("[REVERSAL ENGINE | F%llu] TriggerReversal aborted: No slots enabled in category config for %s", g_CurrentFrame, revName);
            return;
        }

        const ReversalSlotConfig& selected = cfg.slots[chosenIndex];

        int delay = selected.delayFrames;

        LogTrace("[REVERSAL ENGINE | F%llu] Triggering %s Reversal on P%d (Slot #%d, Action: %s, Delay: %dF, Weight: %d)",
                 g_CurrentFrame, revName, targetPlayer, chosenIndex + 1, GetReversalActionName(selected.actionType), delay, selected.weight);

        ExecutePresetAction(selected.actionType, targetPlayer, delay);
        m_LastTriggerFrame = g_CurrentFrame;
    }

    void ReversalEngine::NotifyBlockstunEnd(int playerNum) {
        LogTrace("[REVERSAL ENGINE | F%llu] NotifyBlockstunEnd called for P%d", g_CurrentFrame, playerNum);
        if (playerNum == m_TargetPlayer) {
            m_BlockRecoveryLockout = 15; // User requested: give extra 15f before blocking again after a blocked attack
            if (playerNum == 2) {
                SafeKeyUp(VK_DOWN);
                SafeKeyUp(VK_NUMPAD0);
                SafeKeyUp(VK_DECIMAL);
            } else {
                SafeKeyUp('S');
                SafeKeyUp(VK_SPACE);
                SafeKeyUp(VK_DOWN);
            }
        }
        TriggerReversal(ReversalType::BLOCK, playerNum);
    }

    void ReversalEngine::NotifyHitstunEnd(int playerNum) {
        LogTrace("[REVERSAL ENGINE | F%llu] NotifyHitstunEnd called for P%d", g_CurrentFrame, playerNum);
        TriggerReversal(ReversalType::AFTER_DAMAGE, playerNum);
    }

    void ReversalEngine::NotifyWakeupReady(int playerNum) {
        // Handled directly by OnFrameTick for early buffering (Invincible) and neutral recovery (Regular)
    }

    void ReversalEngine::NotifyIncomingHit(int defPlr, int attPlr, bool isBlocked, int rxID, const AttackClassification& atk, float damage) {
        if (defPlr != m_TargetPlayer) return;

        if (isBlocked) {
            // Attack successfully blocked by defender!
            if (m_DummyIdleStance == DummyIdleStance::AUTO_BLOCK_STAND || m_DummyIdleStance == DummyIdleStance::AUTO_BLOCK_CROUCH) {
                m_AutoBlockLastHitFrame = g_CurrentFrame;
                return;
            }
            // Immediately release guard (Block and Crouch) so defender doesn't stay stuck holding Down/Block during guard recoil.
            m_BlockRecoveryLockout = 15;
            if (m_TargetPlayer == 2) {
                SafeKeyUp(VK_DOWN);
                SafeKeyUp(VK_NUMPAD0);
                SafeKeyUp(VK_DECIMAL);
            } else {
                SafeKeyUp('S');
                SafeKeyUp(VK_SPACE);
                SafeKeyUp(VK_DOWN);
            }
            LogTrace("[REVERSAL ENGINE | F%llu] Attack BLOCKED by P%d! Releasing guard immediately", g_CurrentFrame, m_TargetPlayer);
            return;
        }

        // Attack hit defender!
        if (m_DummyIdleStance == DummyIdleStance::AUTO_BLOCK_STAND || m_DummyIdleStance == DummyIdleStance::AUTO_BLOCK_CROUCH) {
            m_AutoBlockEngaged = true;
            m_AutoBlockLastHitFrame = g_CurrentFrame;
            LogTrace("[REVERSAL ENGINE | F%llu] AUTO BLOCK ENGAGED on P%d (Hit taken -> Guard active on earliest recovery frame)", g_CurrentFrame, m_TargetPlayer);
        } else {
            // Release guard (Down and Block) so defender is in clean neutral input during hitstun
            if (m_TargetPlayer == 2) {
                SafeKeyUp(VK_DOWN);
                SafeKeyUp(VK_NUMPAD0);
                SafeKeyUp(VK_DECIMAL);
            } else {
                SafeKeyUp('S');
                SafeKeyUp(VK_SPACE);
                SafeKeyUp(VK_DOWN);
            }
        }

        float charHeight = 0.0f;
        uint32_t stanceFlags = 0;
        void* defChar = (m_TargetPlayer == 1) ? GetP1Char() : GetP2Char();
        if (defChar && (uintptr_t)defChar > 0x01000000) {
            __try {
                charHeight  = *(float*)((uintptr_t)defChar + 0x1F0);
                stanceFlags = *(uint32_t*)((uintptr_t)defChar + 0x2E8);
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
        }

        bool isNativeAirborne = ((stanceFlags & 0x01) != 0) && (charHeight > 25.0f);

        bool isKnockdownHit = (
            rxID == 292 ||             // Sweep (B+4)
            rxID == 176 ||             // Uppercut (D+2)
            atk.isUppercut ||          // Uppercut property
            atk.atkType == 10 ||       // Pop-up launcher
            atk.atkType == 12 ||       // Anti-air launcher
            atk.atkType == 18 ||       // Throw
            isNativeAirborne ||        // Genuinely airborne juggle hit
            (charHeight > 2.0f) ||     // Airborne juggle hit
            (rxID >= 100 && rxID != 39 && rxID != 42) // Knockdown / launch / crumple reaction
        );

        if (isKnockdownHit) {
            NotifyIncomingKnockdown(defPlr, rxID);
        }
    }

    void ReversalEngine::NotifyIncomingKnockdown(int defPlr, int rxID) {
        if (defPlr != m_TargetPlayer) return;

        // Cancel and release any prior active action sequence immediately on knockdown
        if (m_ActiveSequence.active) {
            m_ActiveSequence.ReleaseAllHeld();
            m_ActiveSequence.active = false;
            m_ActiveSequence.delayRemaining = 0;
            m_ActiveSequence.heldKeyCount = 0;
        }
        ReleaseAllSimulationKeys();

        m_Def_PendingKnockdown = true;
        m_Def_KnockdownRxID = rxID;
        m_KnockdownBufferFrame = 0;

        int pickedInv = -1;
        int pickedReg = -1;
        WakeupRecoveryOption optToUse = WakeupRecoveryOption::NORMAL;

        if (m_ActiveWakeupCategory == WakeupCategory::INVINCIBLE) {
            pickedInv = PickWeightedSlot(m_WakeupInvincibleConfig);
            if (pickedInv >= 0) {
                optToUse = m_WakeupInvincibleConfig.slots[pickedInv].wakeupOption;
            }
        } else {
            pickedReg = PickWeightedSlot(m_WakeupRegularConfig);
            if (pickedReg >= 0) {
                optToUse = m_WakeupRegularConfig.slots[pickedReg].wakeupOption;
            }
        }
        m_ArmedInvSlot = pickedInv;
        m_ArmedRegSlot = pickedReg;
        m_CurrentWakeupOption = optToUse;
        m_WakeupRecoveryConfig.option = optToUse;

        if (m_WakeupRecoveryConfig.delayedHoldFrames > 96) {
            m_WakeupRecoveryConfig.delayedHoldFrames = 96;
        }

        bool hasForceRoll = (optToUse == WakeupRecoveryOption::QUICK_ROLL);
        bool hasDelayedWakeup = (optToUse == WakeupRecoveryOption::DELAYED_WAKEUP || 
                                 optToUse == WakeupRecoveryOption::DELAYED_WAKEUP_ROLL);
        bool hasAnyWakeup = (pickedInv >= 0 || pickedReg >= 0);

        if (hasAnyWakeup || hasForceRoll || hasDelayedWakeup) {
            m_WakeupArmed = true;
            m_WakeupReversalTriggered = false;
            m_QuickRollTriggered = false;
            m_QuickRollActive = false;
            m_QuickRollFrames = 0;
            m_QuickRollStep = 0;
            m_QuickRollKey = 0;
            m_DelayedDownHeld = false;
            m_DelayedWakeupFinished = false;
            m_DelayedHoldFramesCount = 0;
            m_DelayedRollStep = 0;
            m_DelayedRollKey = 0;
            m_Def_TouchedGroundInKnockdown = false;
            m_KnockdownBufferFrame = 0;
            m_RisingFrames = 0;
            m_GroundFloorFrames = 0;
            m_TelemetryPhase = WakeupTelemetryPhase::AIRBORNE_FALLING;
            ReleaseAllSimulationKeys();

            if (hasDelayedWakeup) {
                m_DelayedDownHeld = true;
                m_DelayedHoldFramesCount = 0;
                SafeKeyDown(m_TargetPlayer == 1 ? 'S' : VK_DOWN);
                LogTrace("[REVERSAL ENGINE | F%llu] >>> %s on P%d: EARLY PRE-BUFFER ACTIVATED ON HIT (RxID: %d)! Holding DOWN through fall <<<",
                         g_CurrentFrame, 
                         (optToUse == WakeupRecoveryOption::DELAYED_WAKEUP_ROLL) ? "DELAYED WAKEUP ROLL" : "DELAYED WAKEUP",
                         m_TargetPlayer, rxID);
            } else {
                const char* modeName = hasForceRoll ? "Quick Roll Mode" :
                                       (pickedInv >= 0 ? "Invincible Wake-up" : "Regular Wake-up");
                LogTrace("[REVERSAL ENGINE | F%llu] Knockdown armed on hit for P%d: %s (RxID: %d)",
                         g_CurrentFrame, m_TargetPlayer, modeName, rxID);
            }
        }
    }

    void ReversalEngine::OnFrameTick(void* p1Char, void* p2Char) {
        if (!m_GlobalReversalEnabled || !IsTrainingMode()) {
            if (m_ActiveSequence.active) {
                m_ActiveSequence.ReleaseAllHeld();
                m_ActiveSequence.active = false;
            }
            ReleaseAllSimulationKeys();
            return;
        }
        if (m_TargetPlayer == 2 && !IsP2HumanControl()) {
            if (m_ActiveSequence.active) {
                m_ActiveSequence.ReleaseAllHeld();
                m_ActiveSequence.active = false;
            }
            ReleaseAllSimulationKeys();
            return;
        }

        void* defChar = (m_TargetPlayer == 1) ? p1Char : p2Char;
        void* attChar = (m_TargetPlayer == 1) ? p2Char : p1Char;
        if (!defChar || (uintptr_t)defChar < 0x01000000) return;

        auto& defStunTracker = (m_TargetPlayer == 1) ? g_P1_StunTracker : g_P2_StunTracker;

        if (m_BlockRecoveryLockout > 0) {
            m_BlockRecoveryLockout--;
        }

        // While defender is in active blockstun, ensure block lockout remains active
        if (defStunTracker.inStun && defStunTracker.isBlockstun) {
            if (m_BlockRecoveryLockout < 15) {
                m_BlockRecoveryLockout = 15;
            }
        }

        // Progress any active reversal preset action sequence
        TickActionSequence(p1Char, p2Char);

        // 1. Monitor Defender Action State & Knockdown
        uint32_t actionState = 0;
        float charHeight = 0.0f;
        float p1_x = 0.0f, p2_x = 0.0f, def_x = 0.0f, att_x = 0.0f;
        uint32_t stanceFlags = 0;
        __try {
            actionState = *(uint32_t*)((uintptr_t)defChar + 0x1D4);
            charHeight  = *(float*)((uintptr_t)defChar + 0x1F0);
            def_x       = *(float*)((uintptr_t)defChar + 0x1E8);
            stanceFlags = *(uint32_t*)((uintptr_t)defChar + 0x2E8);
            if (attChar && (uintptr_t)attChar > 0x01000000) {
                att_x   = *(float*)((uintptr_t)attChar + 0x1E8);
            }
            if (p1Char && (uintptr_t)p1Char > 0x01000000) {
                p1_x    = *(float*)((uintptr_t)p1Char + 0x1E8);
            }
            if (p2Char && (uintptr_t)p2Char > 0x01000000) {
                p2_x    = *(float*)((uintptr_t)p2Char + 0x1E8);
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}

        // 2. Monitor Defender Blockstun & Hitstun Lifecycle via defStunTracker
        bool currentlyInStun = defStunTracker.inStun;
        bool isBlockstun = (defStunTracker.inStun && defStunTracker.isBlockstun);

        bool isNativeAirborne = ((stanceFlags & 0x01) != 0) && (charHeight > 25.0f);

        bool isRolling = (m_WakeupArmed && (m_QuickRollTriggered || m_QuickRollActive) &&
                          (actionState == 0x5200 || (actionState >= 0x6202 && actionState <= 0x6205)));

        bool isRisingGetup          = (actionState >= 0x6200 && actionState <= 0x620B);
        bool isAirborneHitReaction  = (charHeight > 2.0f && (currentlyInStun || (actionState >= 0x4000 && actionState <= 0x4FFF)));
        bool isKnockdownAction      = (actionState == 0x460D || (actionState >= 0x4200 && actionState <= 0x45FF && actionState != 0x4205 && actionState != 0x4206) ||
                                       (actionState >= 0x460A && actionState <= 0x460F) ||
                                       (actionState >= 0x6000 && actionState <= 0x621F) ||
                                       isRolling);

        bool isHitReactionKnockdown = (isAirborneHitReaction || isKnockdownAction || (isNativeAirborne && currentlyInStun));
        bool isGroundedKnockdown    = (actionState == 0x460D || (actionState >= 0x6000 && actionState <= 0x621F) || isRolling);
        bool isKnockdown            = (isHitReactionKnockdown || isGroundedKnockdown);
        bool isRecoveredNeutral     = (actionState == 0x0000 || actionState == 0x0101 || actionState == 0x0100 ||
                                       actionState == 0x0A00 || actionState == 0x0A01 ||
                                       actionState == 0x0900 || actionState == 0x0901 || actionState == 0x0102) && charHeight <= 5.0f;

        if (isRecoveredNeutral && !m_WakeupArmed && !currentlyInStun) {
            m_Def_PendingKnockdown = false;
            m_Def_KnockdownRxID = 0;
        }

        if (m_Def_WasInBlockstun && !currentlyInStun) {
            LogTrace("[REVERSAL ENGINE | F%llu] Blockstun release transition fired for P%d! (m_Def_WasInBlockstun=1, currentlyInStun=0)", g_CurrentFrame, m_TargetPlayer);
            NotifyBlockstunEnd(m_TargetPlayer);
        } else if (m_Def_WasInHitstun && !currentlyInStun) {
            bool isFloorKnockdown = (actionState == 0x460D || (actionState >= 0x6000 && actionState <= 0x621F) ||
                                     (actionState >= 0x4200 && actionState <= 0x45FF && actionState != 0x4205 && actionState != 0x4206) ||
                                     (actionState >= 0x460A && actionState <= 0x460F) ||
                                     m_Def_TouchedGroundInKnockdown || charHeight > 2.0f);
            if (!isFloorKnockdown) {
                // Genuine grounded hitstun release
                if (m_WakeupArmed) {
                    m_WakeupArmed = false;
                    m_QuickRollTriggered = false;
                    m_QuickRollActive = false;
                    m_ArmedInvSlot = -1;
                    m_ArmedRegSlot = -1;
                    m_CurrentWakeupOption = WakeupRecoveryOption::NORMAL;
                    if (m_DelayedDownHeld) {
                        SafeKeyUp(m_TargetPlayer == 1 ? 'S' : VK_DOWN);
                        m_DelayedDownHeld = false;
                    }
                }
                m_Def_PendingKnockdown = false;
                m_Def_KnockdownRxID = 0;

                LogTrace("[REVERSAL ENGINE | F%llu] Hitstun release transition fired for P%d! (m_Def_WasInHitstun=1, currentlyInStun=0)", g_CurrentFrame, m_TargetPlayer);
                NotifyHitstunEnd(m_TargetPlayer);
            }
        }

        m_Def_WasInBlockstun = (currentlyInStun && isBlockstun);
        m_Def_WasInHitstun = (currentlyInStun && !isBlockstun && !isKnockdown);

        // Opponent-relative directional orientation
        bool defFacesRight = (att_x >= def_x);
        WORD def_down = (m_TargetPlayer == 1) ? 'S' : VK_DOWN;
        WORD def_fwd  = (m_TargetPlayer == 1) ? (defFacesRight ? 'D' : 'A') : (defFacesRight ? VK_RIGHT : VK_LEFT);
        WORD def_back = (m_TargetPlayer == 1) ? (defFacesRight ? 'A' : 'D') : (defFacesRight ? VK_LEFT : VK_RIGHT);

        if (actionState != m_Def_PrevAction) {
            LogTrace("[P%d STATE CHANGE | F%llu] 0x%04X -> 0x%04X | Height: %.2f | Stun: %d (Block: %d) | KD: %d",
                     m_TargetPlayer, g_CurrentFrame, m_Def_PrevAction, actionState, charHeight, currentlyInStun ? 1 : 0, isBlockstun ? 1 : 0, isKnockdown ? 1 : 0);

            // Log outcome when leaving getup state
            if (m_Def_PrevAction >= 0x6200 && m_Def_PrevAction <= 0x620B && (actionState < 0x6200 || actionState > 0x620B)) {
                bool isSpecial = (actionState >= 0x5000 && actionState <= 0x5FFF);
                LogTrace("[WAKEUP OUTCOME | F%llu] P%d Getup 0x%04X -> 0x%04X in %d frames | Result: %s",
                         g_CurrentFrame, m_TargetPlayer, m_Def_PrevAction, actionState, m_RisingFrames,
                         isSpecial ? "SUCCESS (WAKEUP ATTACK INITIATED)" : "REJECTED (FELL TO STAND/BLOCK)");
            }
        }

        // 3. Arm Wakeup Reversal strictly upon knockdown trigger
        if ((isHitReactionKnockdown || isGroundedKnockdown) && !m_WakeupArmed) {
            int pickedInv = -1;
            int pickedReg = -1;
            WakeupRecoveryOption optToUse = WakeupRecoveryOption::NORMAL;

            if (m_ActiveWakeupCategory == WakeupCategory::INVINCIBLE) {
                pickedInv = PickWeightedSlot(m_WakeupInvincibleConfig);
                if (pickedInv >= 0) {
                    optToUse = m_WakeupInvincibleConfig.slots[pickedInv].wakeupOption;
                }
            } else {
                pickedReg = PickWeightedSlot(m_WakeupRegularConfig);
                if (pickedReg >= 0) {
                    optToUse = m_WakeupRegularConfig.slots[pickedReg].wakeupOption;
                }
            }
            m_ArmedInvSlot = pickedInv;
            m_ArmedRegSlot = pickedReg;
            m_CurrentWakeupOption = optToUse;
            m_WakeupRecoveryConfig.option = optToUse;

            if (m_WakeupRecoveryConfig.delayedHoldFrames > 96) {
                m_WakeupRecoveryConfig.delayedHoldFrames = 96;
            }

            bool hasInvSlot = (pickedInv >= 0);
            bool hasRegSlot = (pickedReg >= 0);
            bool hasAnyWakeup = (hasInvSlot || hasRegSlot);
            bool hasForceRoll = (optToUse == WakeupRecoveryOption::QUICK_ROLL);
            bool hasDelayedWakeup = (optToUse == WakeupRecoveryOption::DELAYED_WAKEUP || optToUse == WakeupRecoveryOption::DELAYED_WAKEUP_ROLL);

            if (hasAnyWakeup || hasForceRoll || hasDelayedWakeup) {
                m_WakeupArmed = true;
                m_WakeupReversalTriggered = false;
                m_QuickRollTriggered = false;
                m_QuickRollActive = false;
                m_QuickRollFrames = 0;
                m_QuickRollStep = 0;
                m_QuickRollKey = 0;
                m_DelayedDownHeld = false;
                m_DelayedWakeupFinished = false;
                m_DelayedHoldFramesCount = 0;
                m_DelayedRollStep = 0;
                m_DelayedRollKey = 0;
                m_Def_TouchedGroundInKnockdown = isGroundedKnockdown;
                m_KnockdownBufferFrame = 0;
                m_RisingFrames = 0;
                m_GroundFloorFrames = 0;
                m_TelemetryPhase = isGroundedKnockdown ? WakeupTelemetryPhase::GROUND_KNOCKDOWN : WakeupTelemetryPhase::AIRBORNE_FALLING;
                ReleaseAllSimulationKeys();

                // Pre-buffer DOWN early while falling if Delayed Wakeup or Delayed Wakeup Roll is selected
                if (hasDelayedWakeup) {
                    m_DelayedDownHeld = true;
                    m_DelayedHoldFramesCount = 0;
                    SafeKeyDown(def_down);
                }

                const char* modeName = hasForceRoll ? "Quick Roll Mode" :
                                       hasDelayedWakeup ? (optToUse == WakeupRecoveryOption::DELAYED_WAKEUP_ROLL ? "Delayed Wakeup Roll Mode" : "Delayed Wakeup Mode") :
                                       hasInvSlot ? "Invincible Wake-up" : "Regular Wake-up";
                LogTrace("[REVERSAL ENGINE | F%llu] P%d Knockdown armed: %s (Act: 0x%04X, H: %.1f, HitKD: %d, GroundKD: %d)%s",
                         g_CurrentFrame, m_TargetPlayer, modeName, actionState, charHeight, isHitReactionKnockdown ? 1 : 0, isGroundedKnockdown ? 1 : 0,
                         hasDelayedWakeup ? " [EARLY PRE-BUFFER: Holding DOWN in air/fall]" : "");
            }
        }

        // Active Knockdown Progression & Wakeup Tracking
        if (m_WakeupArmed) {
            m_KnockdownBufferFrame++;

            bool hasInvSlot = (m_ArmedInvSlot >= 0);
            bool hasRegSlot = (m_ArmedRegSlot >= 0);

            WakeupRecoveryOption optToUse = m_CurrentWakeupOption;
            m_WakeupRecoveryConfig.option = optToUse;

            if (m_WakeupRecoveryConfig.delayedHoldFrames > 96) {
                m_WakeupRecoveryConfig.delayedHoldFrames = 96;
            }

            bool triggerQuickRoll = (optToUse == WakeupRecoveryOption::QUICK_ROLL);
            bool triggerDelayedWakeup = (optToUse == WakeupRecoveryOption::DELAYED_WAKEUP);
            bool triggerDelayedRoll = (optToUse == WakeupRecoveryOption::DELAYED_WAKEUP_ROLL);

            // A. Quick Roll: Trigger on floor impact (0x460D or grounded fall)
            if (triggerQuickRoll && !m_QuickRollTriggered) {
                if (actionState == 0x460D || (charHeight <= 1.0f && actionState >= 0x4600 && actionState <= 0x460F && m_Def_TouchedGroundInKnockdown)) {
                    m_QuickRollActive = true;
                    m_QuickRollStep = 0;
                    m_QuickRollFrames = 0;
                    m_QuickRollTriggered = true;
                    LogTrace("====================================================================");
                    LogTrace("[REVERSAL ENGINE | F%llu] >>> P%d QUICK ROLL TRIGGERED on Floor Impact (Act: 0x%04X, H: %.2f) <<<",
                             g_CurrentFrame, m_TargetPlayer, actionState, charHeight);
                    LogTrace("====================================================================");
                }
            }

            // Track quick roll duration
            if (m_QuickRollTriggered) {
                m_QuickRollFrames++;
            }

            // Execute Quick Roll Rocker Sequence
            if (m_QuickRollActive) {
                if (m_QuickRollStep == 0) {
                    SafeKeyDown(def_fwd);
                    m_QuickRollKey = def_fwd;
                    LogTrace("[FORCE ROLL | F%llu] P%d Step 1: Injected FORWARD (0x%02X)", g_CurrentFrame, m_TargetPlayer, def_fwd);
                } else if (m_QuickRollStep == 2) {
                    SafeKeyUp(def_fwd);
                    SafeKeyDown(def_back);
                    m_QuickRollKey = def_back;
                    LogTrace("[FORCE ROLL | F%llu] P%d Step 2: Released FORWARD, Injected BACK (0x%02X)", g_CurrentFrame, m_TargetPlayer, def_back);
                } else if (m_QuickRollStep == 5) {
                    SafeKeyUp(def_back);
                    m_QuickRollKey = 0;
                    m_QuickRollActive = false;
                    LogTrace("[FORCE ROLL | F%llu] P%d Step 3: Released BACK (Rocker Complete)", g_CurrentFrame, m_TargetPlayer);
                }
                m_QuickRollStep++;
            }

            // B. Delayed Wakeup & Delayed Wakeup Roll: Pre-buffer DOWN early in air and hold through ground stay
            if ((triggerDelayedWakeup || triggerDelayedRoll) && !m_DelayedWakeupFinished) {
                // Ensure DOWN is held early while airborne/falling
                if (!m_DelayedDownHeld) {
                    m_DelayedDownHeld = true;
                    m_DelayedHoldFramesCount = 0;
                    SafeKeyDown(def_down);
                    LogTrace("[REVERSAL ENGINE | F%llu] >>> P%d %s: Pre-buffer DOWN (Falling / Knockdown) <<<",
                             g_CurrentFrame, m_TargetPlayer, triggerDelayedRoll ? "DELAYED WAKEUP ROLL" : "DELAYED WAKEUP");
                }

                // Once defender touches the floor / is grounded, count ground hold frames
                if (m_Def_TouchedGroundInKnockdown || actionState == 0x460D || (actionState >= 0x6000 && actionState <= 0x62FF)) {
                    m_TelemetryPhase = WakeupTelemetryPhase::DELAYED_GROUND_HOLD;
                    m_DelayedHoldFramesCount++;
                    int maxHold = 60;
                    if (m_ArmedRegSlot >= 0 && m_ArmedRegSlot < MK9TradeFix::ReversalCategoryConfig::MAX_CONFIG_SLOTS) {
                        maxHold = m_WakeupRegularConfig.slots[m_ArmedRegSlot].holdFrames;
                        if (maxHold < 30) maxHold = 30;
                        if (maxHold > 96) maxHold = 96;
                    } else {
                        maxHold = (m_WakeupRecoveryConfig.delayedHoldFrames > 96) ? 96 : m_WakeupRecoveryConfig.delayedHoldFrames;
                    }
                    if (m_DelayedHoldFramesCount >= maxHold) {
                        SafeKeyUp(def_down);
                        m_DelayedDownHeld = false;
                        m_DelayedWakeupFinished = true;
                        LogTrace("[REVERSAL ENGINE | F%llu] >>> P%d Completed Ground Down Hold (%d frames, Target: %d) <<<",
                                 g_CurrentFrame, m_TargetPlayer, m_DelayedHoldFramesCount, maxHold);

                        // If Delayed Wakeup Roll: Inject BACK for 3 frames immediately upon Down release
                        if (triggerDelayedRoll) {
                            m_DelayedRollStep = 1;
                            SafeKeyDown(def_back);
                            m_DelayedRollKey = def_back;
                            LogTrace("[REVERSAL ENGINE | F%llu] >>> P%d DELAYED WAKEUP ROLL: Injected BACK (0x%02X) for 3 frames <<<",
                                     g_CurrentFrame, m_TargetPlayer, def_back);
                        }
                    }
                }
            } else if (m_DelayedRollStep > 0) {
                m_DelayedRollStep++;
                if (m_DelayedRollStep > 3) {
                    if (m_DelayedRollKey != 0) {
                        SafeKeyUp(m_DelayedRollKey);
                        m_DelayedRollKey = 0;
                    }
                    m_DelayedRollStep = 0;
                    LogTrace("[REVERSAL ENGINE | F%llu] >>> P%d DELAYED WAKEUP ROLL: Released BACK (Completed 3-frame roll buffer) <<<",
                             g_CurrentFrame, m_TargetPlayer);
                }
            }

            // Invincible & Regular Wakeup Reversals
            if (isGroundedKnockdown) {
                m_Def_TouchedGroundInKnockdown = true;
                m_GroundFloorFrames++;

                if (isRisingGetup) {
                    m_RisingFrames++;
                }

                // Log knockdown progress during getup window
                if (m_GroundFloorFrames <= 5 || isRisingGetup || (m_GroundFloorFrames % 10 == 0)) {
                    LogTrace("[WAKEUP TRACK | F%llu] P%d Act:0x%04X H:%.1f GF:%d RF:%d Getup:%s SeqActive:%s (f=%d)",
                             g_CurrentFrame, m_TargetPlayer, actionState, charHeight, m_GroundFloorFrames, m_RisingFrames,
                             isRisingGetup ? "YES" : "NO", m_ActiveSequence.active ? "YES" : "NO", m_ActiveSequence.frameIndex);
                }

                // Quick Roll Wake-up Attack: Trigger during late roll buffer window (roll frame 20) or upon entering roll getup (0x6202..0x6205)
                bool isRollGetup = (actionState >= 0x6202 && actionState <= 0x6205);
                bool triggerRollWakeup = (m_QuickRollTriggered && (m_QuickRollFrames >= 20 || isRollGetup));

                // Invincible Wake-up: Trigger upon entering rising getup (0x6200..0x620B) OR during late quick roll
                if (hasInvSlot && !m_WakeupReversalTriggered && (isRisingGetup || triggerRollWakeup)) {
                    m_WakeupReversalTriggered = true;
                    m_TelemetryPhase = WakeupTelemetryPhase::RISING_WAKEUP_WINDOW;
                    const auto& slot = m_WakeupInvincibleConfig.slots[m_ArmedInvSlot];

                    uint32_t p1_st = (p1Char && (uintptr_t)p1Char > 0x01000000) ? *(uint32_t*)((uintptr_t)p1Char + 0x2E8) : 0;
                    uint32_t p2_st = (p2Char && (uintptr_t)p2Char > 0x01000000) ? *(uint32_t*)((uintptr_t)p2Char + 0x2E8) : 0;

                    LogTrace("====================================================================");
                    LogTrace("[WAKEUP SCENARIO TRIGGER | F%llu] P%d %s: 0x%04X (GF:%d, RF:%d, QRF:%d)",
                             g_CurrentFrame, m_TargetPlayer, triggerRollWakeup ? "Quick Roll Wakeup Buffer" : "Rising Getup",
                             actionState, m_GroundFloorFrames, m_RisingFrames, m_QuickRollFrames);
                    LogTrace("  P1 Stance: 0x%05X | P1 Pos: %.1f", p1_st, p1_x);
                    LogTrace("  P2 Stance: 0x%05X | P2 Pos: %.1f", p2_st, p2_x);
                    LogTrace("  Executing: %s on P%d (Frame 1 Pre-buffered Immediate Invincible Wake-up)", GetReversalActionName(slot.actionType), m_TargetPlayer);
                    LogTrace("====================================================================");

                    ExecutePresetAction(slot.actionType, m_TargetPlayer, 0);
                }

                // Regular wakeups, including Quick Roll at 0F, wait for recovered neutral.
                // They are queued below with the fixed additional simulation frame.

                if (!m_WakeupReversalTriggered) {
                    m_TelemetryPhase = WakeupTelemetryPhase::GROUND_KNOCKDOWN;
                } else {
                    m_TelemetryPhase = WakeupTelemetryPhase::RISING_WAKEUP_WINDOW;
                }
            } else if (!isRecoveredNeutral) {
                m_TelemetryPhase = m_Def_TouchedGroundInKnockdown ? WakeupTelemetryPhase::GROUND_KNOCKDOWN : WakeupTelemetryPhase::AIRBORNE_FALLING;
            }

            // Fallback / Trigger for Regular Wakeup on transition into neutral stand & clean disarm
            if (isRecoveredNeutral) {
                if (m_Def_TouchedGroundInKnockdown) {
                    if (hasRegSlot && !m_WakeupReversalTriggered) {
                        m_WakeupReversalTriggered = true;
                        m_TelemetryPhase = WakeupTelemetryPhase::REVERSAL_EXECUTING;
                        const auto& slot = m_WakeupRegularConfig.slots[m_ArmedRegSlot];
                        LogTrace("[REVERSAL ENGINE] Triggering Regular WAKE-UP Reversal on P%d in neutral (Slot #%d, Action: %s, Delay: %dF)",
                                 m_TargetPlayer, m_ArmedRegSlot + 1, GetReversalActionName(slot.actionType), slot.delayFrames);
                        ExecutePresetAction(slot.actionType, m_TargetPlayer, RegularWakeupInternalDelay(slot.delayFrames));
                    }
                    if (!m_ActiveSequence.active) {
                        m_WakeupArmed = false;
                        m_WakeupReversalTriggered = false;
                        m_QuickRollTriggered = false;
                        m_QuickRollActive = false;
                        m_QuickRollFrames = 0;
                        m_QuickRollStep = 0;
                        m_Def_TouchedGroundInKnockdown = false;
                        m_ArmedInvSlot = -1;
                        m_ArmedRegSlot = -1;
                        m_CurrentWakeupOption = WakeupRecoveryOption::NORMAL;
                        m_GroundFloorFrames = 0;
                        m_TelemetryPhase = WakeupTelemetryPhase::IDLE;
                        if (m_DelayedDownHeld) {
                            SafeKeyUp(def_down);
                            m_DelayedDownHeld = false;
                        }
                        if (m_DelayedRollKey != 0) {
                            SafeKeyUp(m_DelayedRollKey);
                            m_DelayedRollKey = 0;
                        }
                        m_DelayedRollStep = 0;
                        m_DelayedWakeupFinished = false;
                        m_Def_PendingKnockdown = false;
                        m_Def_KnockdownRxID = 0;
                    }
                } else if (!m_ActiveSequence.active && m_KnockdownBufferFrame > 40) {
                    m_WakeupArmed = false;
                    m_WakeupReversalTriggered = false;
                    m_QuickRollTriggered = false;
                    m_QuickRollActive = false;
                    m_QuickRollFrames = 0;
                    m_QuickRollStep = 0;
                    m_Def_TouchedGroundInKnockdown = false;
                    m_ArmedInvSlot = -1;
                    m_ArmedRegSlot = -1;
                    m_CurrentWakeupOption = WakeupRecoveryOption::NORMAL;
                    m_GroundFloorFrames = 0;
                    m_TelemetryPhase = WakeupTelemetryPhase::IDLE;
                    if (m_DelayedDownHeld) {
                        SafeKeyUp(def_down);
                        m_DelayedDownHeld = false;
                    }
                    if (m_DelayedRollKey != 0) {
                        SafeKeyUp(m_DelayedRollKey);
                        m_DelayedRollKey = 0;
                    }
                    m_DelayedRollStep = 0;
                    m_DelayedWakeupFinished = false;
                    m_Def_PendingKnockdown = false;
                    m_Def_KnockdownRxID = 0;
                }
            }

            // Safety timeout (e.g. dummy juggled / reset without touching ground for 400 frames)
            if (m_KnockdownBufferFrame > 400 && !m_ActiveSequence.active) {
                m_WakeupArmed = false;
                m_WakeupReversalTriggered = false;
                m_QuickRollTriggered = false;
                m_QuickRollActive = false;
                m_QuickRollFrames = 0;
                m_QuickRollStep = 0;
                m_Def_TouchedGroundInKnockdown = false;
                m_ArmedInvSlot = -1;
                m_ArmedRegSlot = -1;
                m_CurrentWakeupOption = WakeupRecoveryOption::NORMAL;
                m_GroundFloorFrames = 0;
                m_TelemetryPhase = WakeupTelemetryPhase::IDLE;
                if (m_DelayedDownHeld) {
                    SafeKeyUp(def_down);
                    m_DelayedDownHeld = false;
                }
                if (m_DelayedRollKey != 0) {
                    SafeKeyUp(m_DelayedRollKey);
                    m_DelayedRollKey = 0;
                }
                m_DelayedRollStep = 0;
                m_DelayedWakeupFinished = false;
                m_Def_PendingKnockdown = false;
                m_Def_KnockdownRxID = 0;
            }
        } else {
            if (!m_ActiveSequence.active) {
                m_TelemetryPhase = WakeupTelemetryPhase::IDLE;
            }
        }

        // Maintain Defender Dummy Idle Stance (Standing, Crouching, Stand Blocking, Crouch Blocking)
        if (m_StanceTransitionFrames > 0) {
            m_StanceTransitionFrames--;
            SafeKeyUp(VK_DOWN);
            SafeKeyUp(VK_NUMPAD0);
            SafeKeyUp(VK_DECIMAL);
            SafeKeyUp('S');
            SafeKeyUp(VK_SPACE);
            m_Def_WasInKnockdown = isKnockdown;
            m_Def_PrevAction = actionState;
            return;
        }

        bool reversalRunning = (m_ActiveSequence.active && m_ActiveSequence.targetPlayer == m_TargetPlayer);
        bool blockLockoutActive = (m_BlockRecoveryLockout > 0);
        bool hitstunActive = (currentlyInStun && !isBlockstun);

        // Auto Block lifecycle: active pressure tracking and 2.0s (120 frame) inactivity timeout
        if (m_DummyIdleStance == DummyIdleStance::AUTO_BLOCK_STAND || m_DummyIdleStance == DummyIdleStance::AUTO_BLOCK_CROUCH) {
            bool inActiveStunImpact = currentlyInStun || actionState == 0x4F00 ||
                                     (actionState >= 0x4000 && actionState <= 0x45FF) ||
                                     (actionState >= 0x4700 && actionState <= 0x4FFF);
            if (inActiveStunImpact) {
                if (!m_AutoBlockEngaged) {
                    m_AutoBlockEngaged = true;
                    LogTrace("[REVERSAL ENGINE | F%llu] AUTO BLOCK ENGAGED on P%d (Detected stun/impact state 0x%04X -> Guard active)", g_CurrentFrame, m_TargetPlayer, actionState);
                }
                m_AutoBlockLastHitFrame = g_CurrentFrame;
            } else if (m_AutoBlockEngaged && m_AutoBlockLastHitFrame > 0) {
                // If 120 frames (2.0s at 60fps) pass with no new attacks/stuns hitting the dummy:
                if (g_CurrentFrame >= m_AutoBlockLastHitFrame + 120) {
                    m_AutoBlockEngaged = false;
                    m_AutoBlockLastHitFrame = 0;
                    if (m_TargetPlayer == 2) {
                        SafeKeyUp(VK_DOWN, 2);
                        SafeKeyUp(VK_NUMPAD0, 2);
                        SafeKeyUp(VK_DECIMAL, 2);
                    } else {
                        SafeKeyUp('S', 1);
                        SafeKeyUp(VK_SPACE, 1);
                    }
                    LogTrace("[REVERSAL ENGINE | F%llu] AUTO BLOCK DISENGAGED on P%d (2.0s without attack -> Neutral restored)", g_CurrentFrame, m_TargetPlayer);
                }
            }
        }

        auto ApplyDefenderDown = [&](bool down) {
            if (g_KeyboardAssignedPlayer != 0 && g_KeyboardAssignedPlayer != m_TargetPlayer) return;
            if (m_TargetPlayer == 1) {
                if (down) SafeKeyDown('S', 1); else SafeKeyUp('S', 1);
            } else {
                if (down) SafeKeyDown(VK_DOWN, 2); else SafeKeyUp(VK_DOWN, 2);
            }
        };

        auto ApplyDefenderBlock = [&](bool block) {
            if (g_KeyboardAssignedPlayer != 0 && g_KeyboardAssignedPlayer != m_TargetPlayer) return;
            if (m_TargetPlayer == 1) {
                if (block) SafeKeyDown(VK_SPACE, 1); else SafeKeyUp(VK_SPACE, 1);
            } else {
                if (block) {
                    SafeKeyDown(VK_NUMPAD0, 2);
                    SafeKeyDown(VK_DECIMAL, 2);
                } else {
                    SafeKeyUp(VK_NUMPAD0, 2);
                    SafeKeyUp(VK_DECIMAL, 2);
                }
            }
        };

        if (!reversalRunning && !m_WakeupArmed) {
            if (m_DummyIdleStance == DummyIdleStance::CROUCHING) {
                ApplyDefenderDown(!hitstunActive);
                ApplyDefenderBlock(false);
            } else if (m_DummyIdleStance == DummyIdleStance::STAND_BLOCKING) {
                ApplyDefenderDown(false);
                ApplyDefenderBlock(!blockLockoutActive);
            } else if (m_DummyIdleStance == DummyIdleStance::CROUCH_BLOCKING) {
                if (blockLockoutActive) {
                    ApplyDefenderDown(false);
                    ApplyDefenderBlock(false);
                } else {
                    ApplyDefenderDown(true);
                    if (actionState == 0x0A01 && !currentlyInStun) {
                        ApplyDefenderBlock(false);
                    } else {
                        ApplyDefenderBlock(true);
                    }
                }
            } else if (m_DummyIdleStance == DummyIdleStance::STANDING) {
                ApplyDefenderDown(false);
                ApplyDefenderBlock(false);
            } else if (m_DummyIdleStance == DummyIdleStance::AUTO_BLOCK_STAND) {
                ApplyDefenderDown(false);
                ApplyDefenderBlock(m_AutoBlockEngaged);
            } else if (m_DummyIdleStance == DummyIdleStance::AUTO_BLOCK_CROUCH) {
                if (m_AutoBlockEngaged) {
                    ApplyDefenderDown(true);
                    ApplyDefenderBlock(true);
                } else {
                    ApplyDefenderDown(!hitstunActive);
                    ApplyDefenderBlock(false);
                }
            }
        } else {
            if (m_DummyIdleStance != DummyIdleStance::STANDING) {
                if (m_TargetPlayer == 2) {
                    if (!m_ActiveSequence.IsHoldingKey(VK_DOWN)) SafeKeyUp(VK_DOWN, 2);
                    if (!m_ActiveSequence.IsHoldingKey(VK_NUMPAD0)) SafeKeyUp(VK_NUMPAD0, 2);
                    if (!m_ActiveSequence.IsHoldingKey(VK_DECIMAL)) SafeKeyUp(VK_DECIMAL, 2);
                } else {
                    if (!m_ActiveSequence.IsHoldingKey('S')) SafeKeyUp('S', 1);
                    if (!m_ActiveSequence.IsHoldingKey(VK_SPACE)) SafeKeyUp(VK_SPACE, 1);
                }
            }
        }

        m_Def_WasInKnockdown = isKnockdown;
        m_Def_PrevAction = actionState;
    }

    bool ReversalEngine::HasActiveGamepadOverride(DWORD dwUserIndex) const {
        if (!m_GlobalReversalEnabled || !IsTrainingMode()) return false;
        if (m_TargetPlayer == 2 && !IsP2HumanControl()) return false;

        // Yield immediately to Diagnostic Macros (Modes 1..16) so test bots have full gamepad control
        if (g_SimTriggerFrame != 0) return false;

        int targetPort = GetPlayerXInputPort(m_TargetPlayer);
        int seqPort = m_ActiveSequence.active ? GetPlayerXInputPort(m_ActiveSequence.targetPlayer) : -1;

        if ((int)dwUserIndex == targetPort) {
            if (m_StanceTransitionFrames > 0) return true;
            if (m_ActiveSequence.active && m_ActiveSequence.targetPlayer == m_TargetPlayer) return true;
            if (m_QuickRollActive) return true;
            if (m_DelayedDownHeld) return true;
            if (m_DelayedRollStep > 0 && m_DelayedRollKey != 0) return true;
            if (m_DummyIdleStance == DummyIdleStance::AUTO_BLOCK_STAND) {
                return m_AutoBlockEngaged;
            }
            if (m_DummyIdleStance == DummyIdleStance::AUTO_BLOCK_CROUCH) {
                return true;
            }
            auto& defStun = (m_TargetPlayer == 1) ? g_P1_StunTracker : g_P2_StunTracker;
            if (defStun.inStun && !defStun.isBlockstun) {
                if (m_DummyIdleStance == DummyIdleStance::STAND_BLOCKING || m_DummyIdleStance == DummyIdleStance::CROUCH_BLOCKING) {
                    return true;
                }
                return false;
            }
            if (m_DummyIdleStance != DummyIdleStance::STANDING) return true;
        } else if ((int)dwUserIndex == seqPort) {
            if (m_ActiveSequence.active) return true;
        }

        return false;
    }

    bool ReversalEngine::GetActiveGamepad(DWORD dwUserIndex, XINPUT_STATE* pState) {
        if (!pState || !HasActiveGamepadOverride(dwUserIndex)) return false;

        int targetPort = GetPlayerXInputPort(m_TargetPlayer);
        int seqPort = m_ActiveSequence.active ? GetPlayerXInputPort(m_ActiveSequence.targetPlayer) : -1;

        // 0. Stance Transition Neutral Flushing Override
        if (m_StanceTransitionFrames > 0 && (int)dwUserIndex == targetPort) {
            memset(&pState->Gamepad, 0, sizeof(XINPUT_GAMEPAD));
            pState->dwPacketNumber++;
            return true;
        }

        // 0a. Quick Roll Active Rocker Override
        if (m_QuickRollActive && (int)dwUserIndex == targetPort) {
            memset(&pState->Gamepad, 0, sizeof(XINPUT_GAMEPAD));
            if (m_QuickRollKey == VK_LEFT || m_QuickRollKey == 'A') {
                pState->Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_LEFT;
                pState->Gamepad.sThumbLX = -32767;
            } else if (m_QuickRollKey == VK_RIGHT || m_QuickRollKey == 'D') {
                pState->Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_RIGHT;
                pState->Gamepad.sThumbLX = 32767;
            }
            pState->dwPacketNumber++;
            return true;
        }

        // 0b. Delayed Wakeup Down Override
        if (m_DelayedDownHeld && (int)dwUserIndex == targetPort) {
            memset(&pState->Gamepad, 0, sizeof(XINPUT_GAMEPAD));
            pState->Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_DOWN;
            pState->Gamepad.sThumbLY = -32767;
            pState->dwPacketNumber++;
            return true;
        }

        // 0c. Delayed Wakeup Roll Back Override
        if (m_DelayedRollStep > 0 && m_DelayedRollKey != 0 && (int)dwUserIndex == targetPort) {
            memset(&pState->Gamepad, 0, sizeof(XINPUT_GAMEPAD));
            if (m_DelayedRollKey == VK_LEFT || m_DelayedRollKey == 'A') {
                pState->Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_LEFT;
                pState->Gamepad.sThumbLX = -32767;
            } else if (m_DelayedRollKey == VK_RIGHT || m_DelayedRollKey == 'D') {
                pState->Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_RIGHT;
                pState->Gamepad.sThumbLX = 32767;
            }
            pState->dwPacketNumber++;
            return true;
        }

        // 1. Reversal Preset Active Sequence (P1 or P2)
        if (m_ActiveSequence.active && (int)dwUserIndex == seqPort) {
            // While delay countdown is active for defender, maintain dummy idle stance
            if (m_ActiveSequence.delayRemaining > 0 && m_ActiveSequence.targetPlayer == m_TargetPlayer && (int)dwUserIndex == targetPort) {
                // If delayRemaining <= 2, release Down so Frame 0 has a clean press edge!
                if (m_ActiveSequence.delayRemaining <= 2) {
                    memset(&pState->Gamepad, 0, sizeof(XINPUT_GAMEPAD));
                    pState->dwPacketNumber++;
                    return true;
                }
                void* defChar = (m_TargetPlayer == 1) ? GetP1Char() : GetP2Char();
                auto& defStun = (m_TargetPlayer == 1) ? g_P1_StunTracker : g_P2_StunTracker;

                if (m_DummyIdleStance == DummyIdleStance::CROUCH_BLOCKING) {
                    memset(&pState->Gamepad, 0, sizeof(XINPUT_GAMEPAD));
                    if (m_BlockRecoveryLockout > 0) {
                        pState->Gamepad.bRightTrigger = 0;
                    } else {
                        pState->Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_DOWN;
                        pState->Gamepad.sThumbLY = -32767;
                        uint32_t act = 0;
                        if (defChar && (uintptr_t)defChar > 0x01000000) {
                            __try { act = *(uint32_t*)((uintptr_t)defChar + 0x1D4); } __except (EXCEPTION_EXECUTE_HANDLER) {}
                        }
                        if (act == 0x0A01 && !defStun.inStun) {
                            pState->Gamepad.bRightTrigger = 0;
                        } else {
                            pState->Gamepad.bRightTrigger = 255;
                        }
                    }
                    pState->dwPacketNumber++;
                    return true;
                } else if (m_DummyIdleStance == DummyIdleStance::STAND_BLOCKING) {
                    memset(&pState->Gamepad, 0, sizeof(XINPUT_GAMEPAD));
                    if (m_BlockRecoveryLockout > 0) {
                        pState->Gamepad.bRightTrigger = 0;
                    } else {
                        pState->Gamepad.bRightTrigger = 255;
                    }
                    pState->dwPacketNumber++;
                    return true;
                } else if (m_DummyIdleStance == DummyIdleStance::CROUCHING) {
                    memset(&pState->Gamepad, 0, sizeof(XINPUT_GAMEPAD));
                    pState->Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_DOWN;
                    pState->Gamepad.sThumbLY = -32767;
                    pState->dwPacketNumber++;
                    return true;
                } else if (m_DummyIdleStance == DummyIdleStance::AUTO_BLOCK_STAND) {
                    if (m_AutoBlockEngaged) {
                        memset(&pState->Gamepad, 0, sizeof(XINPUT_GAMEPAD));
                        pState->Gamepad.bRightTrigger = 255;
                        pState->dwPacketNumber++;
                        return true;
                    }
                } else if (m_DummyIdleStance == DummyIdleStance::AUTO_BLOCK_CROUCH) {
                    memset(&pState->Gamepad, 0, sizeof(XINPUT_GAMEPAD));
                    pState->Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_DOWN;
                    pState->Gamepad.sThumbLY = -32767;
                    if (m_AutoBlockEngaged) {
                        pState->Gamepad.bRightTrigger = 255;
                    }
                    pState->dwPacketNumber++;
                    return true;
                }
            }

            memset(&pState->Gamepad, 0, sizeof(XINPUT_GAMEPAD));
            for (int i = 0; i < m_ActiveSequence.heldKeyCount; i++) {
                WORD vk = m_ActiveSequence.heldKeys[i];
                if (vk == VK_DOWN || vk == 'S') {
                    pState->Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_DOWN;
                    pState->Gamepad.sThumbLY = -32767;
                } else if (vk == VK_UP || vk == 'W') {
                    pState->Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_UP;
                    pState->Gamepad.sThumbLY = 32767;
                } else if (vk == VK_LEFT || vk == 'A') {
                    pState->Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_LEFT;
                    pState->Gamepad.sThumbLX = -32767;
                } else if (vk == VK_RIGHT || vk == 'D') {
                    pState->Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_RIGHT;
                    pState->Gamepad.sThumbLX = 32767;
                } else if (vk == VK_NUMPAD7 || vk == 'T' || vk == 'U') {
                    pState->Gamepad.wButtons |= XINPUT_GAMEPAD_X; // FP / 1
                } else if (vk == VK_NUMPAD9 || vk == VK_NUMPAD8 || vk == 'Y' || vk == 'I') {
                    pState->Gamepad.wButtons |= XINPUT_GAMEPAD_Y; // BP / 2
                } else if (vk == VK_NUMPAD4 || vk == VK_NUMPAD1 || vk == 'G' || vk == 'J') {
                    pState->Gamepad.wButtons |= XINPUT_GAMEPAD_A; // FK / 3
                } else if (vk == VK_NUMPAD6 || vk == VK_NUMPAD3 || vk == VK_NUMPAD5 || vk == VK_NUMPAD2 || vk == 'H' || vk == 'K' || vk == 'L') {
                    pState->Gamepad.wButtons |= XINPUT_GAMEPAD_B; // BK / 4
                } else if (vk == VK_NUMPAD0 || vk == VK_DECIMAL || vk == VK_SPACE) {
                    pState->Gamepad.bRightTrigger = 255; // Block (RT) - NEVER RIGHT_SHOULDER
                } else if (vk == VK_ADD || vk == VK_RCONTROL || vk == VK_LCONTROL || vk == 'P') {
                    pState->Gamepad.bLeftTrigger = 255; // Stance / Flip Stance (LT)
                } else if (vk == VK_LSHIFT || vk == VK_RSHIFT || vk == 'O') {
                    pState->Gamepad.wButtons |= XINPUT_GAMEPAD_LEFT_SHOULDER; // Tag (LB)
                }
            }
            pState->dwPacketNumber++;
            return true;
        }

        // 2. Defender Idle Stance for Defender Port
        if ((int)dwUserIndex == targetPort) {
            bool reversalInputting = (m_ActiveSequence.active && m_ActiveSequence.delayRemaining == 0);
            auto& defStun = (m_TargetPlayer == 1) ? g_P1_StunTracker : g_P2_StunTracker;
            void* defChar = (m_TargetPlayer == 1) ? GetP1Char() : GetP2Char();
            bool hitstunActive = (defStun.inStun && !defStun.isBlockstun);
            if (!reversalInputting && !m_WakeupArmed) {
                if (m_DummyIdleStance == DummyIdleStance::CROUCHING && !hitstunActive) {
                    memset(&pState->Gamepad, 0, sizeof(XINPUT_GAMEPAD));
                    pState->Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_DOWN;
                    pState->Gamepad.sThumbLY = -32767;
                    pState->dwPacketNumber++;
                    return true;
                } else if (m_DummyIdleStance == DummyIdleStance::STAND_BLOCKING) {
                    memset(&pState->Gamepad, 0, sizeof(XINPUT_GAMEPAD));
                    if (m_BlockRecoveryLockout > 0) {
                        pState->Gamepad.bRightTrigger = 0;
                    } else {
                        pState->Gamepad.bRightTrigger = 255; // Block (RT) - NEVER RIGHT_SHOULDER
                    }
                    pState->dwPacketNumber++;
                    return true;
                } else if (m_DummyIdleStance == DummyIdleStance::CROUCH_BLOCKING) {
                    memset(&pState->Gamepad, 0, sizeof(XINPUT_GAMEPAD));
                    if (m_BlockRecoveryLockout > 0) {
                        pState->Gamepad.bRightTrigger = 0;
                    } else {
                        pState->Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_DOWN;
                        pState->Gamepad.sThumbLY = -32767;
                        uint32_t act = 0;
                        if (defChar && (uintptr_t)defChar > 0x01000000) {
                            __try { act = *(uint32_t*)((uintptr_t)defChar + 0x1D4); } __except (EXCEPTION_EXECUTE_HANDLER) {}
                        }
                        if (act == 0x0A01 && !defStun.inStun) {
                            pState->Gamepad.bRightTrigger = 0;
                        } else {
                            pState->Gamepad.bRightTrigger = 255;
                        }
                    }
                    pState->dwPacketNumber++;
                    return true;
                } else if (m_DummyIdleStance == DummyIdleStance::AUTO_BLOCK_STAND) {
                    if (m_AutoBlockEngaged) {
                        memset(&pState->Gamepad, 0, sizeof(XINPUT_GAMEPAD));
                        pState->Gamepad.bRightTrigger = 255;
                        pState->dwPacketNumber++;
                        return true;
                    }
                } else if (m_DummyIdleStance == DummyIdleStance::AUTO_BLOCK_CROUCH) {
                    memset(&pState->Gamepad, 0, sizeof(XINPUT_GAMEPAD));
                    if (!hitstunActive) {
                        pState->Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_DOWN;
                        pState->Gamepad.sThumbLY = -32767;
                    }
                    if (m_AutoBlockEngaged) {
                        pState->Gamepad.bRightTrigger = 255;
                    }
                    pState->dwPacketNumber++;
                    return true;
                }
            }
        }

        return false;
    }

}



