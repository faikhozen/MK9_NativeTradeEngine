#include "frame_data.hpp"
#include "reversal_engine.hpp"
#include "cancel_detector.hpp"
#include "profiler.hpp"
#include "stun_modifier.hpp"
#include <cstring>
#include <cmath>

namespace MK9TradeFix {

    // Forward declaration from combat_state
    bool QueryCharacterAttackInfo(void* pChar, float* pOutDamage, int* pOutRxID);

    bool g_FrameDataVisualizerEnabled = false;
    float g_FrameBarOpacity = 0.88f;

    MoveLifecycleTracker g_P1_Lifecycle;
    MoveLifecycleTracker g_P2_Lifecycle;

    LiveAdvantageState s_p1AdvState;
    LiveAdvantageState s_p2AdvState;

    StunLifecycleTracker g_P1_StunTracker;
    StunLifecycleTracker g_P2_StunTracker;

    FighterFrameTimeline s_p1Timeline;
    FighterFrameTimeline s_p2Timeline;

    uint32_t g_P1_LastAttackToken = 0;
    uint32_t g_P2_LastAttackToken = 0;

    static uint32_t s_p1PrevAction = 0;
    static uint32_t s_p2PrevAction = 0;
    static uint64_t s_p1LastKnockdownFrame = 0;
    static uint64_t s_p2LastKnockdownFrame = 0;
    static uint64_t s_p1LastWakeupAttackFrame = 0;
    static uint64_t s_p2LastWakeupAttackFrame = 0;
    static bool     s_p1WasLastWakeupInvincible = false;
    static bool     s_p2WasLastWakeupInvincible = false;

    uint32_t ResolveAuthoredBlockstun(int attPlr, const MoveLifecycleTracker& attTracker, void* pDefender) {
        uint32_t token = attTracker.matchedToken;
        if (token == 0) {
            token = (attPlr == 1) ? g_P1_LastAttackToken : g_P2_LastAttackToken;
        }

        // 1. Stand and Crouch Normal Tokens (Scorpion & universal MK9 core attack tokens)
        switch (token) {
            case 0x8000000F: return 14; // FP (1 / Front Punch) -> 14f total blockstun (Advantage: 0)
            case 0x80000010: return 18; // BP (2 / Back Punch)  -> 18f total blockstun (Advantage: 0)
            case 0x80000011: return 21; // FK (3 / Front Kick)  -> 21f total blockstun (Advantage: -7)
            case 0x80000012: return 28; // BK (4 / Back Kick)   -> 28f total blockstun (Advantage: -5)
            case 0x80000023: return 12; // D+1 (Crouch Jab)     -> 12f total blockstun (Advantage: -3)
            case 0x80000029: return 18; // D+4 (Sweep)          -> 18f total blockstun (Advantage: -6)
            case 0x8000003A: return 18; // D+2 (Uppercut) / D+3 -> 18f total blockstun
            default: break;
        }

        // 2. Fallback heuristic based on authored attack damage
        float dmg = attTracker.baseDamage;
        if (dmg > 0.001f) {
            if (dmg <= 0.014f) return 14; // Light Normal (1.0% - 1.4%) -> 14f blockstun
            if (dmg <= 0.025f) return 18; // Medium Normal (1.5% - 2.5%) -> 18f blockstun
            return 24;                    // Heavy Normal / Special (> 2.5%) -> 24f blockstun
        }

        return 14; // Safe universal fallback
    }

    void ToggleFrameDataVisualizer() {
        g_FrameDataVisualizerEnabled = !g_FrameDataVisualizerEnabled;
    }

    bool IsFrameDataVisualizerEnabled() {
        return g_FrameDataVisualizerEnabled;
    }

    void GetLiveFrameDataTimelines(FighterFrameTimeline& outP1, FighterFrameTimeline& outP2) {
        outP1 = s_p1Timeline;
        outP2 = s_p2Timeline;
    }

    void OnHitOrBlockEvent(int attPlr, int defPlr, bool isBlocked, uint64_t hitFrame, int param_8) {
        if (attPlr == 1) {
            s_p1AdvState.hasHit = true;
            s_p1AdvState.isBlocked = isBlocked;
            s_p1AdvState.hitFrame = hitFrame;
            s_p1AdvState.defenderReleaseFrame = 0;
            s_p1AdvState.advantageComputed = false;

            if (g_P1_Lifecycle.inMove) {
                if (g_P1_Lifecycle.firstActiveFrame == 0) {
                    g_P1_Lifecycle.firstActiveFrame = hitFrame;
                }
                g_P1_Lifecycle.lastActiveFrame = hitFrame;
                if (s_p1Timeline.blocks[FighterFrameTimeline::CAPACITY - 1].state == 1) {
                    s_p1Timeline.blocks[FighterFrameTimeline::CAPACITY - 1] = { 2, 1 };
                }
            } else if (g_P1_Lifecycle.lastFinishedEndFrame > 0 && (hitFrame >= g_P1_Lifecycle.lastFinishedEndFrame) && (hitFrame - g_P1_Lifecycle.lastFinishedEndFrame <= 240)) {
                // Detached projectile impact after attacker's body animation has already finished!
                s_p1AdvState.attackerEndFrame = g_P1_Lifecycle.lastFinishedEndFrame;
                s_p1AdvState.inMove = false;
                LogTrace("[PROJECTILE IMPACT] P1 Detached Projectile hit P%d on frame %llu (Attacker neutral on frame %llu, Head-start: +%lluf)",
                         defPlr, hitFrame, g_P1_Lifecycle.lastFinishedEndFrame, hitFrame - g_P1_Lifecycle.lastFinishedEndFrame);
            }
        } else if (attPlr == 2) {
            s_p2AdvState.hasHit = true;
            s_p2AdvState.isBlocked = isBlocked;
            s_p2AdvState.hitFrame = hitFrame;
            s_p2AdvState.defenderReleaseFrame = 0;
            s_p2AdvState.advantageComputed = false;

            if (g_P2_Lifecycle.inMove) {
                if (g_P2_Lifecycle.firstActiveFrame == 0) {
                    g_P2_Lifecycle.firstActiveFrame = hitFrame;
                }
                g_P2_Lifecycle.lastActiveFrame = hitFrame;
                if (s_p2Timeline.blocks[FighterFrameTimeline::CAPACITY - 1].state == 1) {
                    s_p2Timeline.blocks[FighterFrameTimeline::CAPACITY - 1] = { 2, 1 };
                }
            } else if (g_P2_Lifecycle.lastFinishedEndFrame > 0 && (hitFrame >= g_P2_Lifecycle.lastFinishedEndFrame) && (hitFrame - g_P2_Lifecycle.lastFinishedEndFrame <= 240)) {
                // Detached projectile impact after attacker's body animation has already finished!
                s_p2AdvState.attackerEndFrame = g_P2_Lifecycle.lastFinishedEndFrame;
                s_p2AdvState.inMove = false;
                LogTrace("[PROJECTILE IMPACT] P2 Detached Projectile hit P%d on frame %llu (Attacker neutral on frame %llu, Head-start: +%lluf)",
                         defPlr, hitFrame, g_P2_Lifecycle.lastFinishedEndFrame, hitFrame - g_P2_Lifecycle.lastFinishedEndFrame);
            }
        }

        if (defPlr == 1) {
            g_P1_Lifecycle.inMove = false;
            g_P1_Lifecycle.startFrame = 0;
            g_P1_Lifecycle.endFrame = 0;
            g_P1_Lifecycle.firstActiveFrame = 0;
            g_P1_Lifecycle.recoveryStartFrame = 0;
            g_P1_Lifecycle.wasInterrupted = false;
            g_P1_StunTracker.inStun = true;
            g_P1_StunTracker.isBlockstun = isBlocked;
            g_P1_StunTracker.startFrame = hitFrame;
            g_P1_StunTracker.endFrame = 0;
            g_P1_StunTracker.attackerPlr = attPlr;
            if (isBlocked) {
                uint32_t baseStun = ResolveAuthoredBlockstun(attPlr, g_P2_Lifecycle, GetP1Char());
                int delta = StunModifier::g_GeneralBlockstunDelta;
                int finalStun = (int)baseStun + delta;
                g_P1_StunTracker.authoredFrames = (finalStun > 1) ? (uint32_t)finalStun : 1;
            } else {
                int delta = StunModifier::g_PlayerStun[1].totalExtraFrames;
                if (delta == 0) delta = StunModifier::g_GeneralHitstunDelta;
                uint32_t baseStun = (StunModifier::g_PlayerStun[1].naturalAnimFrames > 0) ? 
                                    StunModifier::g_PlayerStun[1].naturalAnimFrames : 24;
                int finalStun = (int)baseStun + delta;
                g_P1_StunTracker.authoredFrames = (finalStun > 1) ? (uint32_t)finalStun : 1;
            }
            s_p1Timeline.blocks[FighterFrameTimeline::CAPACITY - 1] = { isBlocked ? (uint8_t)5 : (uint8_t)4, 1 };

            // Attack receiver telemetry metrics: clear attack startup/active and set totalFrames to stun duration
            s_p1Timeline.startupFrames = 0;
            s_p1Timeline.activeFrames = 0;
            s_p1Timeline.totalFrames = (int)g_P1_StunTracker.authoredFrames;
        } else if (defPlr == 2) {
            g_P2_Lifecycle.inMove = false;
            g_P2_Lifecycle.startFrame = 0;
            g_P2_Lifecycle.endFrame = 0;
            g_P2_Lifecycle.firstActiveFrame = 0;
            g_P2_Lifecycle.recoveryStartFrame = 0;
            g_P2_Lifecycle.wasInterrupted = false;
            g_P2_StunTracker.inStun = true;
            g_P2_StunTracker.isBlockstun = isBlocked;
            g_P2_StunTracker.startFrame = hitFrame;
            g_P2_StunTracker.endFrame = 0;
            g_P2_StunTracker.attackerPlr = attPlr;
            if (isBlocked) {
                uint32_t baseStun = ResolveAuthoredBlockstun(attPlr, g_P1_Lifecycle, GetP2Char());
                int delta = StunModifier::g_GeneralBlockstunDelta;
                int finalStun = (int)baseStun + delta;
                g_P2_StunTracker.authoredFrames = (finalStun > 1) ? (uint32_t)finalStun : 1;
            } else {
                int delta = StunModifier::g_PlayerStun[2].totalExtraFrames;
                if (delta == 0) delta = StunModifier::g_GeneralHitstunDelta;
                uint32_t baseStun = (StunModifier::g_PlayerStun[2].naturalAnimFrames > 0) ? 
                                    StunModifier::g_PlayerStun[2].naturalAnimFrames : 24;
                int finalStun = (int)baseStun + delta;
                g_P2_StunTracker.authoredFrames = (finalStun > 1) ? (uint32_t)finalStun : 1;
            }
            s_p2Timeline.blocks[FighterFrameTimeline::CAPACITY - 1] = { isBlocked ? (uint8_t)5 : (uint8_t)4, 1 };

            // Attack receiver telemetry metrics: clear attack startup/active and set totalFrames to stun duration
            s_p2Timeline.startupFrames = 0;
            s_p2Timeline.activeFrames = 0;
            s_p2Timeline.totalFrames = (int)g_P2_StunTracker.authoredFrames;
        }

        LogTrace("[FRAME DATA EVENT] P%d %s P%d on frame %llu",
                 attPlr, isBlocked ? "BLOCKED by" : "HIT", defPlr, hitFrame);
    }

    void DumpAttackLifecycleSummary(int playerIdx, const MoveLifecycleTracker& tracker) {
        if (!tracker.inMove && tracker.startFrame == 0) return;

        uint64_t start = tracker.startFrame;
        uint64_t fActive = tracker.firstActiveFrame;
        uint64_t lActive = tracker.lastActiveFrame;
        uint64_t recStart = tracker.recoveryStartFrame;
        uint64_t end = (tracker.endFrame >= start) ? tracker.endFrame : g_CurrentFrame;

        uint64_t startupFrames = (fActive >= start) ? (fActive - start + 1) : (end >= start ? end - start : 0);
        uint64_t activeFrames  = (fActive > 0 && lActive >= fActive) ? (lActive - fActive + 1) : 0;
        uint64_t recoveryFrames = (recStart > 0 && end >= recStart) ? (end - recStart) : 
                                  (lActive > 0 && end >= lActive) ? (end - lActive) : 0;
        uint64_t totalDuration = (end >= start) ? (end - start) : 0;

        LogTrace("====================================================================");
        LogTrace(" [FRAME DATA LIFECYCLE SUMMARY] Player %d", playerIdx);
        LogTrace("====================================================================");
        LogTrace("   Script Function: %d", tracker.scriptFunc);
        LogTrace("   Action State:    0x%04X | Stance: 0x%04X | Combat: 0x%04X", 
                 tracker.actionState, tracker.initialStance, tracker.initialCombat);
        LogTrace("   Authored Damage: %.1f%% (%.4f) | Reaction ID: %d", 
                 tracker.baseDamage * 100.0f, tracker.baseDamage, tracker.rxID);
        LogTrace("   Max Hitboxes:    %u volumes deployed", tracker.maxHitboxes);
        LogTrace("   Native SubState: %u | Move Result: %d", tracker.nativeSubState, tracker.nativeMoveRes);
        LogTrace("   Timeline:        Start: %llu | Active: %llu..%llu | Recovery: %llu | End: %llu",
                 start, fActive, lActive, recStart, end);
        LogTrace("   >>> FRAME DATA:  STARTUP: %lluf | ACTIVE: %lluf | RECOVERY: %lluf | TOTAL: %lluf <<<",
                 startupFrames, activeFrames, recoveryFrames, totalDuration);
        if (tracker.wasCancelled) {
            LogTrace("   Status:          [CANCELLED INTO %s (%d frames recovery stolen)]", 
                     tracker.cancelTargetName ? tracker.cancelTargetName : "SPECIAL", tracker.cancelRecoveryStolen);
        } else if (tracker.wasInterrupted) {
            LogTrace("   Status:          [INTERRUPTED / COUNTER-HIT ON FRAME %llu]", end);
        } else {
            LogTrace("   Status:          [NATURAL EXECUTION COMPLETED]");
        }
        LogTrace("====================================================================");
    }

    void NotifyNormalCancelled(int plr, int recoveryStolen, const char* targetMoveName) {
        if (plr < 1 || plr > 2) return;
        MoveLifecycleTracker& tracker = (plr == 1) ? g_P1_Lifecycle : g_P2_Lifecycle;
        if (tracker.inMove) {
            tracker.wasCancelled = true;
            tracker.cancelRecoveryStolen = recoveryStolen;
            tracker.cancelTargetName = targetMoveName;
            tracker.endFrame = g_CurrentFrame;
            if (tracker.firstActiveFrame > 0 && tracker.recoveryStartFrame == 0) {
                tracker.recoveryStartFrame = (tracker.lastActiveFrame > 0) ? (tracker.lastActiveFrame + 1) : g_CurrentFrame;
            }
            DumpAttackLifecycleSummary(plr, tracker);
            tracker.inMove = false;
        }
    }

    void ProcessFighterLifecycleTick(void* pChar, int playerIdx, MoveLifecycleTracker& tracker) {
        if (!pChar || (uintptr_t)pChar < 0x01000000) return;

        __try {
            uint32_t actionState = *(uint32_t*)((uintptr_t)pChar + 0x1D4);
            uintptr_t scriptPkg  = *(uintptr_t*)((uintptr_t)pChar + 0x2F84);
            int       scriptFunc = *(int*)((uintptr_t)pChar + 0x2F88);
            uint32_t  hitboxes   = *(uint32_t*)((uintptr_t)pChar + 0x7A3C);
            uint32_t  stance     = *(uint32_t*)((uintptr_t)pChar + 0x2E8);
            uint32_t  combat     = *(uint32_t*)((uintptr_t)pChar + 0x2EC);

            // Query PlayerInfo active move record sub-state
            uint32_t subState = 0;
            int32_t  moveRes = 0;
            uint32_t recAtkType = 0;
            void* pInfo = *(void**)((uintptr_t)pChar + 0x7638);
            if (pInfo && (uintptr_t)pInfo >= 0x00400000) {
                uint32_t curr = *(uint32_t*)((uintptr_t)pInfo + 0x574);
                if (curr == 0) curr = *(uint32_t*)((uintptr_t)pInfo + 0x570);
                if (curr > 0) {
                    uintptr_t pRec = (uintptr_t)pInfo + 0x48 + (curr - 1) * 0x2C;
                    moveRes = *(int32_t*)(pRec + 0x1C);
                    recAtkType = *(uint32_t*)(pRec + 0x20);
                    subState = *(uint32_t*)(pRec + 0x24);
                }
            }

            // Query Native Animation Player (pCombat + 0xB40)
            void* pAnimPlayer = (pInfo && (uintptr_t)pInfo > 0x01000000) ? *(void**)((uintptr_t)pInfo + 0xB40) : nullptr;
            uint32_t animFlags = 0;
            if (pAnimPlayer && (uintptr_t)pAnimPlayer > 0x01000000) {
                __try { animFlags = *(uint32_t*)((uintptr_t)pAnimPlayer + 0xC); }
                __except (EXCEPTION_EXECUTE_HANDLER) {}
            }

            // Query Native PLAYER_COL_INFO (pChar + 0x2F10)
            uint32_t activeVols = 0;
            void* pColInfo = *(void**)((uintptr_t)pChar + 0x2F10);
            if (pColInfo && (uintptr_t)pColInfo > 0x01000000) {
                __try {
                    uint32_t* pVolList = *(uint32_t**)((uintptr_t)pColInfo + 0x14);
                    if (pVolList && (uintptr_t)pVolList > 0x01000000) {
                        activeVols = pVolList[0];
                    }
                } __except (EXCEPTION_EXECUTE_HANDLER) {}
            }

            bool isDash = (actionState >= 0x2010 && actionState <= 0x201F) || (actionState == 0x0300 || actionState == 0x0301) || recAtkType == 6;
            bool isMobility = isDash || (actionState >= 0x0200 && actionState <= 0x04FF) || (actionState >= 0x2000 && actionState <= 0x20FF);

            bool isHurtOrBlockOrGetup = (actionState >= 0x4000 && actionState <= 0x4FFF) ||
                                        (actionState >= 0x0A00 && actionState <= 0x0A0F) ||
                                        (actionState >= 0x0900 && actionState <= 0x090F) || // Crouch Block (0x0900, 0x0901)
                                        (actionState >= 0x6000 && actionState <= 0x62FF) ||
                                        (actionState == 0x0000 || actionState == 0x0100 || actionState == 0x0101);

            bool inAttackThread = !isMobility && !isHurtOrBlockOrGetup && (scriptPkg != 0 && scriptFunc != -1);

            if (inAttackThread) {
                if (!tracker.inMove || tracker.scriptFunc != scriptFunc) {
                    // Previous move ended/switched:
                    if (tracker.inMove) {
                        tracker.endFrame = g_CurrentFrame;
                        if (tracker.firstActiveFrame > 0 && tracker.recoveryStartFrame == 0) {
                            tracker.recoveryStartFrame = (tracker.lastActiveFrame > 0) ? (tracker.lastActiveFrame + 1) : g_CurrentFrame;
                        }
                        DumpAttackLifecycleSummary(playerIdx, tracker);
                    }

                    // Start new attack lifecycle tracking:
                    uint64_t prevEnd = tracker.lastFinishedEndFrame;
                    tracker = MoveLifecycleTracker();
                    tracker.lastFinishedEndFrame = prevEnd;
                    tracker.inMove = true;
                    tracker.startFrame = g_CurrentFrame;
                    tracker.scriptFunc = scriptFunc;
                    tracker.actionState = actionState;
                    tracker.initialStance = stance;
                    tracker.initialCombat = combat;
                    tracker.nativeSubState = subState;
                    tracker.nativeMoveRes = moveRes;
                    tracker.isProjectile = (recAtkType == 19 || recAtkType == 14);

                    // Detect Wakeup Attack (Invincible or Regular):
                    uint32_t prevAction = (playerIdx == 1) ? s_p1PrevAction : s_p2PrevAction;
                    uint64_t lastKD = (playerIdx == 1) ? s_p1LastKnockdownFrame : s_p2LastKnockdownFrame;
                    bool isFromGetup = (prevAction >= 0x6000 && prevAction <= 0x62FF) || (prevAction == 0x460D) ||
                                       (lastKD > 0 && (g_CurrentFrame >= lastKD) && (g_CurrentFrame - lastKD <= 20));
                    bool isNativeWakeupProp = (recAtkType == 27);
                    bool hasArmor = (combat & 0x800000) != 0;
                    int revTarget = ReversalEngine::Get().GetTargetPlayer();
                    bool isReversalInvWakeup = (playerIdx == revTarget && ReversalEngine::Get().IsInvincibleWakeupReversalTriggered());
                    bool isReversalRegWakeup = (playerIdx == revTarget && ReversalEngine::Get().IsRegularWakeupReversalTriggered());

                    bool isWakeup = isFromGetup || isNativeWakeupProp || isReversalInvWakeup || isReversalRegWakeup;
                    if (isWakeup) {
                        tracker.isWakeupAttack = true;
                        tracker.isWakeupInvincible = (isReversalInvWakeup || hasArmor || isNativeWakeupProp);
                        if (playerIdx == 1) {
                            s_p1LastWakeupAttackFrame = g_CurrentFrame;
                            s_p1WasLastWakeupInvincible = tracker.isWakeupInvincible;
                        } else {
                            s_p2LastWakeupAttackFrame = g_CurrentFrame;
                            s_p2WasLastWakeupInvincible = tracker.isWakeupInvincible;
                        }
                        LogTrace("[FRAME DATA] P%d initiated %s! (Func: %d, PrevAct: 0x%04X, RecType: %u, Armor: %s)",
                                 playerIdx, tracker.isWakeupInvincible ? "INVINCIBLE WAKE-UP ATTACK" : "WAKE-UP ATTACK",
                                 scriptFunc, prevAction, recAtkType, tracker.isWakeupInvincible ? "YES (0x800000)" : "NATIVE/REGULAR");
                    } else {
                        tracker.isWakeupAttack = false;
                        tracker.isWakeupInvincible = false;
                    }

                    if (playerIdx == 1) {
                        s_p1Timeline.activeFrames = 0;
                        s_p1AdvState.isProjectile = tracker.isProjectile;
                    } else if (playerIdx == 2) {
                        s_p2Timeline.activeFrames = 0;
                        s_p2AdvState.isProjectile = tracker.isProjectile;
                    }
                    QueryCharacterAttackInfo(pChar, &tracker.baseDamage, &tracker.rxID);
                    tracker.matchedToken = (playerIdx == 1) ? g_P1_LastAttackToken : g_P2_LastAttackToken;
                }

                if (hitboxes > tracker.maxHitboxes) {
                    tracker.maxHitboxes = hitboxes;
                }
                if (activeVols > tracker.maxHitboxes) {
                    tracker.maxHitboxes = activeVols;
                }

                // Active phase triggers on live weapon/prop collision, active physical volumes, or anim active tags
                bool isNativeActive = (activeVols > 0) || (hitboxes > 0) || ((animFlags & 0x02) != 0);

                if (isNativeActive) {
                    if (tracker.firstActiveFrame == 0) {
                        tracker.firstActiveFrame = g_CurrentFrame;
                    }
                    tracker.lastActiveFrame = g_CurrentFrame;
                } else if (tracker.firstActiveFrame > 0 && tracker.recoveryStartFrame == 0) {
                    if (subState == 0 || g_CurrentFrame > tracker.lastActiveFrame) {
                        tracker.recoveryStartFrame = g_CurrentFrame;
                    }
                } else if (tracker.firstActiveFrame == 0 && subState == 0 && tracker.nativeSubState != 0 && g_CurrentFrame > tracker.startFrame + 2) {
                    // Move transitioned from execution to recovery without a separate volume trigger
                    tracker.firstActiveFrame = g_CurrentFrame;
                    tracker.lastActiveFrame = g_CurrentFrame;
                    tracker.recoveryStartFrame = g_CurrentFrame + 1;
                } else if (tracker.firstActiveFrame == 0 && tracker.isProjectile) {
                    // Projectile Launch Transition (subState transition or startup threshold)
                    if (subState != tracker.nativeSubState && tracker.nativeSubState != 0 && g_CurrentFrame > tracker.startFrame + 3) {
                        tracker.firstActiveFrame = g_CurrentFrame;
                        tracker.lastActiveFrame = g_CurrentFrame;
                        tracker.recoveryStartFrame = g_CurrentFrame + 1;
                        tracker.projectileSpawnFrame = g_CurrentFrame;
                        LogTrace("[PROJECTILE LAUNCH] P%d Projectile Spawned on frame %llu (Startup: %lluf) via subState (%u -> %u)",
                                 playerIdx, g_CurrentFrame, tracker.firstActiveFrame - tracker.startFrame + 1, tracker.nativeSubState, subState);
                    }
                }

                tracker.nativeSubState = subState;
                tracker.nativeMoveRes = moveRes;

            } else {
                // Character exited attack thread (returned to neutral, dash, mobility, hitstun, or knockdown):
                if (tracker.inMove) {
                    tracker.endFrame = g_CurrentFrame;
                    tracker.lastFinishedEndFrame = tracker.endFrame;

                    if (tracker.firstActiveFrame == 0 && tracker.isProjectile) {
                        // If no volume or subState event fired, allocate 40% startup / 60% recovery so visualizer represents true phases
                        uint64_t total = (tracker.endFrame >= tracker.startFrame) ? (tracker.endFrame - tracker.startFrame) : 0;
                        uint64_t launch = tracker.startFrame + (total * 2 / 5);
                        if (launch >= tracker.startFrame && launch <= tracker.endFrame) {
                            tracker.firstActiveFrame = launch;
                            tracker.lastActiveFrame = launch;
                            tracker.recoveryStartFrame = launch + 1;
                            tracker.projectileSpawnFrame = launch;
                        }
                    }

                    if (tracker.firstActiveFrame > 0 && tracker.recoveryStartFrame == 0) {
                        tracker.recoveryStartFrame = (tracker.lastActiveFrame > 0) ? (tracker.lastActiveFrame + 1) : g_CurrentFrame;
                    }
                    tracker.wasInterrupted = (actionState >= 0x4000 && actionState <= 0x4FFF) || 
                                              (actionState >= 0x6200 && actionState <= 0x620B);
                    if (isDash) {
                        LogTrace("[ATTACK -> DASH TRANSITION] P%d attack ended by DASH on frame %llu (Total attack: %lluf, Recovery: %lluf)",
                                 playerIdx, g_CurrentFrame, 
                                 tracker.endFrame - tracker.startFrame,
                                 (tracker.endFrame >= tracker.recoveryStartFrame) ? (tracker.endFrame - tracker.recoveryStartFrame) : 0);
                    }
                    DumpAttackLifecycleSummary(playerIdx, tracker);
                    tracker.inMove = false;
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    void RecordFrameDataTick(void* p1, void* p2) {
        ScopedProfiler prof(PROF_FRAME_DATA);
        CancelDetector::Tick();

        // Quick Roll (Tech Roll) Tracking Statics
        static float    s_p1FloorContactX = 0.0f;
        static bool     s_p1InFloorKD = false;
        static uint32_t s_p1RollFrames = 0;
        static bool     s_p1Rolling = false;
        static bool     s_p1WasKnockedDown = false;
        static uint32_t s_p1DelayedFrames = 0;
        static bool     s_p1DelayedWakeup = false;

        static float    s_p2FloorContactX = 0.0f;
        static bool     s_p2InFloorKD = false;
        static uint32_t s_p2RollFrames = 0;
        static bool     s_p2Rolling = false;
        static bool     s_p2WasKnockedDown = false;
        static uint32_t s_p2DelayedFrames = 0;
        static bool     s_p2DelayedWakeup = false;

        // -------------------------------------------------------------
        // Player 1 State Evaluation
        // -------------------------------------------------------------
        uint8_t p1State = 0; // Idle / Neutral (0)
        uint8_t p1Num = 0;
        if (p1 && (uintptr_t)p1 > 0x01000000) {
            uint32_t lock1   = *(uint32_t*)((uintptr_t)p1 + 0xAAB0);
            uint32_t action1 = *(uint32_t*)((uintptr_t)p1 + 0x1D4);
            uint32_t combat1 = *(uint32_t*)((uintptr_t)p1 + 0x2EC);
            float hitTimer1  = *(float*)((uintptr_t)p1 + 0x2F0);
            bool inBlockReaction1 = (action1 >= 0x0A02 && action1 <= 0x0A0F);
            bool inIdleBlock1     = (action1 == 0x0A00 || action1 == 0x0A01);
            bool isHitReaction1   = (action1 >= 0x4000 && action1 <= 0x4FFF);
            bool isKnockdown1     = (action1 == 0x4201) || (action1 >= 0x6200 && action1 <= 0x620B) ||
                                    (action1 >= 0x4200 && action1 <= 0x45FF && action1 != 0x4205 && action1 != 0x4206) || (action1 >= 0x460A && action1 <= 0x460F);
            bool isActLocked1     = ((combat1 & 0x400) == 0);

            // Native Reaction Script Thread (+0x2FB0 thread ptr, +0x2FB4 func index)
            uintptr_t rxThread1 = *(uintptr_t*)((uintptr_t)p1 + 0x2FB0);
            int       rxFunc1   = *(int*)((uintptr_t)p1 + 0x2FB4);
            bool rxActive1 = (rxThread1 != 0 && rxFunc1 != -1);

            if (g_P1_Lifecycle.inMove && g_P1_Lifecycle.startFrame > g_P1_StunTracker.startFrame) {
                g_P1_StunTracker.inStun = false;
            }

            if (g_P1_StunTracker.inStun) {
                uint64_t elapsed = (g_CurrentFrame >= g_P1_StunTracker.startFrame) ? 
                                   (g_CurrentFrame - g_P1_StunTracker.startFrame + 1) : 1;
                bool stillLocked = false;
                uint64_t currAuth1 = (hitTimer1 > 0.05f && hitTimer1 <= 2.0f) ? 
                                     (uint64_t)(hitTimer1 * 60.0f + 0.5f) : 0;
                if (currAuth1 > g_P1_StunTracker.authoredFrames) {
                    g_P1_StunTracker.authoredFrames = (uint32_t)currAuth1;
                }

                if (g_P1_StunTracker.isBlockstun) {
                    uint32_t targetFrames = (g_P1_StunTracker.authoredFrames > 0) ? g_P1_StunTracker.authoredFrames : 14;
                    if (elapsed <= targetFrames) {
                        stillLocked = true;
                    } else {
                        stillLocked = false;
                    }
                    if (elapsed >= 4 && action1 != 0x0A01 && (action1 < 0x0A02 || action1 > 0x0A0F)) {
                        stillLocked = false;
                    }
                    uint64_t maxBlockWindow = StunModifier::IsInExtendedStun(1) ? 200 : 60;
                    if (elapsed > maxBlockWindow) {
                        stillLocked = false;
                    }
                } else {
                    // Hitstun: locked during native hurt animation
                    bool inHurtAnim = (action1 >= 0x4000 && action1 <= 0x4FFF && action1 != 0x4F00);
                    if (elapsed <= 3) {
                        stillLocked = true; // Initial hit impact anchor
                    } else if (isKnockdown1) {
                        stillLocked = true; // Hard knockdown / floor tumble
                    } else if (inHurtAnim) {
                        stillLocked = true; // Active hurt animation flinch
                    }
                    uint64_t maxHitWindow = StunModifier::IsInExtendedStun(1) ? 200 : 90;
                    if (elapsed > maxHitWindow) {
                        stillLocked = false; // Safety ceiling
                    }
                }

                // Allow StunModifier to extend the stun lock or force-release early
                StunModifier::ProcessStunLockTick(1, p1, action1, isKnockdown1, g_P1_StunTracker.isBlockstun, (uint32_t)elapsed, &stillLocked);

                if (stillLocked) {
                    p1State = g_P1_StunTracker.isBlockstun ? 5 : 4;
                    p1Num = (uint8_t)elapsed;
                } else {
                    // Stun lock released by engine!
                    g_P1_StunTracker.inStun = false;
                    g_P1_StunTracker.isBlockstun = false;
                    g_P1_StunTracker.endFrame = g_CurrentFrame;
                    uint32_t finalStun = (uint32_t)(elapsed > 1 ? elapsed - 1 : 1);
                    g_P1_StunTracker.authoredFrames = finalStun;
                    s_p1Timeline.totalFrames = (int)finalStun;
                    StunModifier::ResetAnimRateScale(1, p1);
                    if (g_P1_StunTracker.attackerPlr == 2) {
                        s_p2AdvState.defenderReleaseFrame = g_CurrentFrame;
                    }
                    LogTrace("[STUN RELEASE] P1 released from %s on frame %llu (Total: %u)",
                             g_P1_StunTracker.isBlockstun ? "BLOCKSTUN" : "HITSTUN",
                             g_CurrentFrame, finalStun);
                }
            }

            bool isDash1 = (action1 >= 0x2010 && action1 <= 0x201F) || (action1 == 0x0300 || action1 == 0x0301);
            static uint32_t s_p1DashFrames = 0;
            if (isDash1) {
                s_p1DashFrames++;
            } else {
                s_p1DashFrames = 0;
            }

            // Quick Roll (Tech Roll) Tracking for Player 1
            float p1_x = *(float*)((uintptr_t)p1 + 0x1E8);
            float p1_h = *(float*)((uintptr_t)p1 + 0x1F0);

            // Knockdown history tracking (strictly excludes standing/crouch stun)
            bool isP1StandingStun = (action1 == 0x4600 || action1 == 0x4700 || action1 == 0x4000 || action1 == 0x4100 || action1 == 0x4F00);
            bool isP1HitKnockdown = (action1 == 0x4201) || 
                                    (action1 >= 0x4200 && action1 <= 0x45FF) ||
                                    (action1 >= 0x460A && action1 <= 0x460F) ||
                                    (p1_h > 2.0f && action1 >= 0x4000 && action1 <= 0x4FFF);
            if (isP1HitKnockdown) {
                s_p1WasKnockedDown = true;
            } else if (isP1StandingStun && p1_h <= 1.5f) {
                s_p1WasKnockedDown = false;
            }

            // Floor impact ONLY occurs from authentic knockdown (0x460D), NEVER standing hitstun (0x4600)
            bool isP1FloorContact = (action1 == 0x460D && s_p1WasKnockedDown);
            if (isP1FloorContact) {
                if (!s_p1InFloorKD) {
                    s_p1InFloorKD = true;
                    s_p1FloorContactX = p1_x;
                    s_p1Rolling = false;
                    s_p1RollFrames = 0;
                } else if (!s_p1Rolling) {
                    // During roll animation, character travels horizontally across ground (>= 20.0f units) or action is 0x5200
                    if (action1 == 0x5200 || fabsf(p1_x - s_p1FloorContactX) >= 20.0f) {
                        s_p1Rolling = true;
                    }
                }
            } else if (action1 >= 0x6200 && action1 <= 0x620B) {
                if (!s_p1Rolling && s_p1InFloorKD && (action1 == 0x5200 || fabsf(p1_x - s_p1FloorContactX) >= 20.0f)) {
                    s_p1Rolling = true;
                }
            } else if (action1 == 0x5200) {
                s_p1Rolling = true;
            } else {
                s_p1InFloorKD = false;
                s_p1Rolling = false;
                s_p1RollFrames = 0;
                s_p1DelayedWakeup = false;
                s_p1DelayedFrames = 0;
                if (p1_h <= 1.0f && (action1 == 0x0000 || action1 == 0x0100 || action1 == 0x0101 || isP1StandingStun)) {
                    s_p1WasKnockedDown = false;
                }
            }

            if (s_p1Rolling) {
                s_p1RollFrames++;
            }

            // Delayed Wakeup tracking for Player 1:
            bool isP1Down = ((GetAsyncKeyState('S') & 0x8000) != 0) || IsSimulatedKeyHeld('S');
            if (!s_p1Rolling && (s_p1InFloorKD || isP1FloorContact || (s_p1WasKnockedDown && action1 >= 0x6000 && action1 <= 0x61FF))) {
                if (isP1Down) {
                    s_p1DelayedWakeup = true;
                    s_p1DelayedFrames++;
                } else if (s_p1DelayedWakeup) {
                    s_p1DelayedWakeup = false;
                }
            } else {
                s_p1DelayedWakeup = false;
                s_p1DelayedFrames = 0;
            }

            if (p1State == 0) {
                if (g_P1_Lifecycle.inMove) {
                    if (g_P1_Lifecycle.firstActiveFrame == 0 || g_CurrentFrame < g_P1_Lifecycle.firstActiveFrame) {
                        p1State = (g_P1_Lifecycle.isWakeupInvincible || g_P1_Lifecycle.isWakeupAttack) ? 8 : 1; // 8 = Wakeup Attack (Royal Violet), 1 = Startup (Pink)
                        p1Num = (uint8_t)(g_CurrentFrame - g_P1_Lifecycle.startFrame + 1);
                    } else if (g_P1_Lifecycle.recoveryStartFrame == 0 || g_CurrentFrame < g_P1_Lifecycle.recoveryStartFrame) {
                        p1State = 2; // Active (Red)
                        p1Num = (uint8_t)(g_CurrentFrame - g_P1_Lifecycle.firstActiveFrame + 1);
                    } else {
                        p1State = 3; // Recovery (Blue)
                        p1Num = (uint8_t)(g_CurrentFrame - g_P1_Lifecycle.recoveryStartFrame + 1);
                    }
                } else if (s_p1Rolling) {
                    p1State = 7; // Quick Roll / Tech Roll (Emerald Green)
                    p1Num = (uint8_t)s_p1RollFrames;
                } else if (s_p1DelayedWakeup) {
                    p1State = 9; // Delayed Wakeup (Deep Sea Teal)
                    p1Num = (uint8_t)s_p1DelayedFrames;
                } else if (isKnockdown1) {
                    p1State = 4; // Floor Knockdown / Stumble (Yellow)
                } else if (isDash1) {
                    p1State = 6; // Dash / Cancel (Vibrant Orange)
                    p1Num = (uint8_t)s_p1DashFrames;
                }
            }

            if (isP1HitKnockdown || isP1FloorContact || (action1 >= 0x6000 && action1 <= 0x62FF)) {
                s_p1LastKnockdownFrame = g_CurrentFrame;
            }
            s_p1PrevAction = action1;
        }

        // -------------------------------------------------------------
        // Player 2 State Evaluation
        // -------------------------------------------------------------
        uint8_t p2State = 0; // Idle / Neutral (0)
        uint8_t p2Num = 0;
        if (p2 && (uintptr_t)p2 > 0x01000000) {
            uint32_t lock2   = *(uint32_t*)((uintptr_t)p2 + 0xAAB0);
            uint32_t action2 = *(uint32_t*)((uintptr_t)p2 + 0x1D4);
            uint32_t combat2 = *(uint32_t*)((uintptr_t)p2 + 0x2EC);
            float hitTimer2  = *(float*)((uintptr_t)p2 + 0x2F0);
            bool inBlockReaction2 = (action2 >= 0x0A02 && action2 <= 0x0A0F);
            bool inIdleBlock2     = (action2 == 0x0A00 || action2 == 0x0A01);
            bool isHitReaction2   = (action2 >= 0x4000 && action2 <= 0x4FFF);
            bool isKnockdown2     = (action2 == 0x4201) || (action2 >= 0x6200 && action2 <= 0x620B) ||
                                    (action2 >= 0x4200 && action2 <= 0x45FF && action2 != 0x4205 && action2 != 0x4206) || (action2 >= 0x460A && action2 <= 0x460F);
            bool isActLocked2     = ((combat2 & 0x400) == 0);

            // Native Reaction Script Thread (+0x2FB0 thread ptr, +0x2FB4 func index)
            uintptr_t rxThread2 = *(uintptr_t*)((uintptr_t)p2 + 0x2FB0);
            int       rxFunc2   = *(int*)((uintptr_t)p2 + 0x2FB4);
            bool rxActive2 = (rxThread2 != 0 && rxFunc2 != -1);

            if (g_P2_Lifecycle.inMove && g_P2_Lifecycle.startFrame > g_P2_StunTracker.startFrame) {
                g_P2_StunTracker.inStun = false;
            }

            if (g_P2_StunTracker.inStun) {
                uint64_t elapsed = (g_CurrentFrame >= g_P2_StunTracker.startFrame) ? 
                                   (g_CurrentFrame - g_P2_StunTracker.startFrame + 1) : 1;
                bool stillLocked = false;
                uint64_t currAuth2 = (hitTimer2 > 0.05f && hitTimer2 <= 2.0f) ? 
                                     (uint64_t)(hitTimer2 * 60.0f + 0.5f) : 0;
                if (currAuth2 > g_P2_StunTracker.authoredFrames) {
                    g_P2_StunTracker.authoredFrames = (uint32_t)currAuth2;
                }

                if (g_P2_StunTracker.isBlockstun) {
                    uint32_t targetFrames = (g_P2_StunTracker.authoredFrames > 0) ? g_P2_StunTracker.authoredFrames : 14;
                    if (elapsed <= targetFrames) {
                        stillLocked = true;
                    } else {
                        stillLocked = false;
                    }
                    if (elapsed >= 4 && action2 != 0x0A01 && (action2 < 0x0A02 || action2 > 0x0A0F)) {
                        stillLocked = false;
                    }
                    uint64_t maxBlockWindow = StunModifier::IsInExtendedStun(2) ? 200 : 60;
                    if (elapsed > maxBlockWindow) {
                        stillLocked = false;
                    }
                } else {
                    // Hitstun: locked during native hurt animation
                    bool inHurtAnim = (action2 >= 0x4000 && action2 <= 0x4FFF && action2 != 0x4F00);
                    if (elapsed <= 3) {
                        stillLocked = true; // Initial hit impact anchor
                    } else if (isKnockdown2) {
                        stillLocked = true; // Hard knockdown / floor tumble
                    } else if (inHurtAnim) {
                        stillLocked = true; // Active hurt animation flinch
                    }
                    uint64_t maxHitWindow = StunModifier::IsInExtendedStun(2) ? 200 : 90;
                    if (elapsed > maxHitWindow) {
                        stillLocked = false; // Safety ceiling
                    }
                }

                // Allow StunModifier to extend the stun lock or force-release early
                StunModifier::ProcessStunLockTick(2, p2, action2, isKnockdown2, g_P2_StunTracker.isBlockstun, (uint32_t)elapsed, &stillLocked);

                if (stillLocked) {
                    p2State = g_P2_StunTracker.isBlockstun ? 5 : 4;
                    p2Num = (uint8_t)elapsed;
                } else {
                    // Stun lock released by engine!
                    g_P2_StunTracker.inStun = false;
                    g_P2_StunTracker.isBlockstun = false;
                    g_P2_StunTracker.endFrame = g_CurrentFrame;
                    uint32_t finalStun = (uint32_t)(elapsed > 1 ? elapsed - 1 : 1);
                    g_P2_StunTracker.authoredFrames = finalStun;
                    s_p2Timeline.totalFrames = (int)finalStun;
                    StunModifier::ResetAnimRateScale(2, p2);
                    if (g_P2_StunTracker.attackerPlr == 1) {
                        s_p1AdvState.defenderReleaseFrame = g_CurrentFrame;
                    }
                    LogTrace("[STUN RELEASE] P2 released from %s on frame %llu (Total: %u)",
                             g_P2_StunTracker.isBlockstun ? "BLOCKSTUN" : "HITSTUN",
                             g_CurrentFrame, finalStun);
                }
            }

            bool isDash2 = (action2 >= 0x2010 && action2 <= 0x201F) || (action2 == 0x0300 || action2 == 0x0301);
            static uint32_t s_p2DashFrames = 0;
            if (isDash2) {
                s_p2DashFrames++;
            } else {
                s_p2DashFrames = 0;
            }

            // Quick Roll (Tech Roll) Tracking for Player 2
            float p2_x = *(float*)((uintptr_t)p2 + 0x1E8);
            float p2_h = *(float*)((uintptr_t)p2 + 0x1F0);

            // Knockdown history tracking (strictly excludes standing/crouch stun)
            bool isP2StandingStun = (action2 == 0x4600 || action2 == 0x4700 || action2 == 0x4000 || action2 == 0x4100 || action2 == 0x4F00);
            bool isP2HitKnockdown = (action2 == 0x4201) || 
                                    (action2 >= 0x4200 && action2 <= 0x45FF) ||
                                    (action2 >= 0x460A && action2 <= 0x460F) ||
                                    (p2_h > 2.0f && action2 >= 0x4000 && action2 <= 0x4FFF);
            if (isP2HitKnockdown) {
                s_p2WasKnockedDown = true;
            } else if (isP2StandingStun && p2_h <= 1.5f) {
                s_p2WasKnockedDown = false;
            }

            // Floor impact ONLY occurs from authentic knockdown (0x460D), NEVER standing hitstun (0x4600)
            bool isP2FloorContact = (action2 == 0x460D && s_p2WasKnockedDown);
            bool isReversalRolling = (ReversalEngine::Get().GetTargetPlayer() == 2 && ReversalEngine::Get().IsQuickRollActive());
            bool isReversalDelayedWakeup = (ReversalEngine::Get().GetTargetPlayer() == 2 && ReversalEngine::Get().IsDelayedWakeupActive());

            if (isP2FloorContact) {
                if (!s_p2InFloorKD) {
                    s_p2InFloorKD = true;
                    s_p2FloorContactX = p2_x;
                    s_p2Rolling = false;
                    s_p2RollFrames = 0;
                } else if (!s_p2Rolling) {
                    // Active roll triggered either by ReversalEngine Rocker, action 0x5200, or actual roll displacement (>= 20.0f)
                    if (isReversalRolling || action2 == 0x5200 || (fabsf(p2_x - s_p2FloorContactX) >= 20.0f)) {
                        s_p2Rolling = true;
                    }
                }
            } else if (action2 >= 0x6200 && action2 <= 0x620B) {
                if (isReversalRolling || (s_p2InFloorKD && (action2 == 0x5200 || fabsf(p2_x - s_p2FloorContactX) >= 20.0f))) {
                    s_p2Rolling = true;
                }
            } else if (action2 == 0x5200) {
                s_p2Rolling = true;
            } else {
                s_p2InFloorKD = false;
                s_p2Rolling = false;
                s_p2RollFrames = 0;
                s_p2DelayedWakeup = false;
                s_p2DelayedFrames = 0;
                if (p2_h <= 1.0f && (action2 == 0x0000 || action2 == 0x0100 || action2 == 0x0101 || isP2StandingStun)) {
                    s_p2WasKnockedDown = false;
                }
            }

            if (s_p2Rolling) {
                s_p2RollFrames++;
            }

            // Delayed Wakeup tracking for Player 2:
            bool isP2Down = isReversalDelayedWakeup || ((GetAsyncKeyState(VK_DOWN) & 0x8000) != 0) || IsSimulatedKeyHeld(VK_DOWN);
            if (!s_p2Rolling && (s_p2InFloorKD || isP2FloorContact || isReversalDelayedWakeup || (s_p2WasKnockedDown && action2 >= 0x6000 && action2 <= 0x61FF))) {
                if (isP2Down) {
                    s_p2DelayedWakeup = true;
                    s_p2DelayedFrames++;
                } else if (s_p2DelayedWakeup) {
                    s_p2DelayedWakeup = false;
                }
            } else {
                s_p2DelayedWakeup = false;
                s_p2DelayedFrames = 0;
            }

            if (p2State == 0) {
                if (g_P2_Lifecycle.inMove) {
                    if (g_P2_Lifecycle.firstActiveFrame == 0 || g_CurrentFrame < g_P2_Lifecycle.firstActiveFrame) {
                        p2State = (g_P2_Lifecycle.isWakeupInvincible || g_P2_Lifecycle.isWakeupAttack) ? 8 : 1; // 8 = Wakeup Attack (Royal Violet), 1 = Startup (Pink)
                        p2Num = (uint8_t)(g_CurrentFrame - g_P2_Lifecycle.startFrame + 1);
                    } else if (g_P2_Lifecycle.recoveryStartFrame == 0 || g_CurrentFrame < g_P2_Lifecycle.recoveryStartFrame) {
                        p2State = 2; // Active (Red)
                        p2Num = (uint8_t)(g_CurrentFrame - g_P2_Lifecycle.firstActiveFrame + 1);
                    } else {
                        p2State = 3; // Recovery (Blue)
                        p2Num = (uint8_t)(g_CurrentFrame - g_P2_Lifecycle.recoveryStartFrame + 1);
                    }
                } else if (s_p2Rolling) {
                    p2State = 7; // Quick Roll / Tech Roll (Emerald Green)
                    p2Num = (uint8_t)s_p2RollFrames;
                } else if (s_p2DelayedWakeup) {
                    p2State = 9; // Delayed Wakeup (Deep Sea Teal)
                    p2Num = (uint8_t)s_p2DelayedFrames;
                } else if (isKnockdown2) {
                    p2State = 4; // Floor Knockdown / Stumble (Yellow)
                } else if (isDash2) {
                    p2State = 6; // Dash / Cancel (Vibrant Orange)
                    p2Num = (uint8_t)s_p2DashFrames;
                }
            }

            if (isP2HitKnockdown || isP2FloorContact || (action2 >= 0x6000 && action2 <= 0x62FF)) {
                s_p2LastKnockdownFrame = g_CurrentFrame;
            }
            s_p2PrevAction = action2;
        }

        // Push into rolling buffers
        memmove(&s_p1Timeline.blocks[0], &s_p1Timeline.blocks[1], (FighterFrameTimeline::CAPACITY - 1) * sizeof(FrameDataBlock));
        s_p1Timeline.blocks[FighterFrameTimeline::CAPACITY - 1] = { p1State, p1Num };

        memmove(&s_p2Timeline.blocks[0], &s_p2Timeline.blocks[1], (FighterFrameTimeline::CAPACITY - 1) * sizeof(FrameDataBlock));
        s_p2Timeline.blocks[FighterFrameTimeline::CAPACITY - 1] = { p2State, p2Num };

        // Update P1 metrics (Tournament Notation: strikes on frame N = NF startup)
        if (g_P1_Lifecycle.inMove && g_P1_Lifecycle.startFrame > 0) {
            if (g_P1_Lifecycle.firstActiveFrame > 0 && g_P1_Lifecycle.firstActiveFrame >= g_P1_Lifecycle.startFrame) {
                s_p1Timeline.startupFrames = (int)(g_P1_Lifecycle.firstActiveFrame - g_P1_Lifecycle.startFrame + 1);
            } else {
                s_p1Timeline.startupFrames = (int)(g_CurrentFrame - g_P1_Lifecycle.startFrame + 1);
            }

            if (g_P1_Lifecycle.recoveryStartFrame > 0 && g_P1_Lifecycle.firstActiveFrame > 0 && g_P1_Lifecycle.recoveryStartFrame >= g_P1_Lifecycle.firstActiveFrame) {
                s_p1Timeline.activeFrames = (int)(g_P1_Lifecycle.recoveryStartFrame - g_P1_Lifecycle.firstActiveFrame);
            } else if (g_P1_Lifecycle.firstActiveFrame > 0 && g_CurrentFrame >= g_P1_Lifecycle.firstActiveFrame) {
                s_p1Timeline.activeFrames = (int)(g_CurrentFrame - g_P1_Lifecycle.firstActiveFrame + 1);
            }

            s_p1Timeline.totalFrames = (int)(g_CurrentFrame - g_P1_Lifecycle.startFrame);
        } else if (g_P1_StunTracker.inStun || (s_p2AdvState.hasHit && !s_p1AdvState.inMove)) {
            // P1 is the attack receiver: clear attack startup/active, set totalFrames to stun duration
            s_p1Timeline.startupFrames = 0;
            s_p1Timeline.activeFrames = 0;
            if (g_P1_StunTracker.authoredFrames > 0) {
                s_p1Timeline.totalFrames = (int)g_P1_StunTracker.authoredFrames;
            }
        } else if (g_P1_Lifecycle.startFrame > 0 && g_P1_Lifecycle.endFrame >= g_P1_Lifecycle.startFrame) {
            if (g_P1_Lifecycle.firstActiveFrame > 0) {
                s_p1Timeline.startupFrames = (int)(g_P1_Lifecycle.firstActiveFrame - g_P1_Lifecycle.startFrame + 1);
            }
            if (g_P1_Lifecycle.recoveryStartFrame > 0 && g_P1_Lifecycle.firstActiveFrame > 0) {
                s_p1Timeline.activeFrames = (int)(g_P1_Lifecycle.recoveryStartFrame - g_P1_Lifecycle.firstActiveFrame);
            }
            s_p1Timeline.totalFrames = (int)(g_P1_Lifecycle.endFrame - g_P1_Lifecycle.startFrame);
        }

        // Update P2 metrics (Tournament Notation: strikes on frame N = NF startup)
        if (g_P2_Lifecycle.inMove && g_P2_Lifecycle.startFrame > 0) {
            if (g_P2_Lifecycle.firstActiveFrame > 0 && g_P2_Lifecycle.firstActiveFrame >= g_P2_Lifecycle.startFrame) {
                s_p2Timeline.startupFrames = (int)(g_P2_Lifecycle.firstActiveFrame - g_P2_Lifecycle.startFrame + 1);
            } else {
                s_p2Timeline.startupFrames = (int)(g_CurrentFrame - g_P2_Lifecycle.startFrame + 1);
            }

            if (g_P2_Lifecycle.recoveryStartFrame > 0 && g_P2_Lifecycle.firstActiveFrame > 0 && g_P2_Lifecycle.recoveryStartFrame >= g_P2_Lifecycle.firstActiveFrame) {
                s_p2Timeline.activeFrames = (int)(g_P2_Lifecycle.recoveryStartFrame - g_P2_Lifecycle.firstActiveFrame);
            } else if (g_P2_Lifecycle.firstActiveFrame > 0 && g_CurrentFrame >= g_P2_Lifecycle.firstActiveFrame) {
                s_p2Timeline.activeFrames = (int)(g_CurrentFrame - g_P2_Lifecycle.firstActiveFrame + 1);
            }

            s_p2Timeline.totalFrames = (int)(g_CurrentFrame - g_P2_Lifecycle.startFrame);
        } else if (g_P2_StunTracker.inStun || (s_p1AdvState.hasHit && !s_p2AdvState.inMove)) {
            // P2 is the attack receiver: clear attack startup/active, set totalFrames to stun duration
            s_p2Timeline.startupFrames = 0;
            s_p2Timeline.activeFrames = 0;
            if (g_P2_StunTracker.authoredFrames > 0) {
                s_p2Timeline.totalFrames = (int)g_P2_StunTracker.authoredFrames;
            }
        } else if (g_P2_Lifecycle.startFrame > 0 && g_P2_Lifecycle.endFrame >= g_P2_Lifecycle.startFrame) {
            if (g_P2_Lifecycle.firstActiveFrame > 0) {
                s_p2Timeline.startupFrames = (int)(g_P2_Lifecycle.firstActiveFrame - g_P2_Lifecycle.startFrame + 1);
            }
            if (g_P2_Lifecycle.recoveryStartFrame > 0 && g_P2_Lifecycle.firstActiveFrame > 0) {
                s_p2Timeline.activeFrames = (int)(g_P2_Lifecycle.recoveryStartFrame - g_P2_Lifecycle.firstActiveFrame);
            }
            s_p2Timeline.totalFrames = (int)(g_P2_Lifecycle.endFrame - g_P2_Lifecycle.startFrame);
        }

        s_p1Timeline.isWakeupAttack = (g_P1_Lifecycle.inMove && g_P1_Lifecycle.isWakeupAttack) || 
                                      ((g_CurrentFrame >= s_p1LastWakeupAttackFrame) && (g_CurrentFrame - s_p1LastWakeupAttackFrame <= 120));
        s_p1Timeline.isWakeupInvincible = (g_P1_Lifecycle.inMove && g_P1_Lifecycle.isWakeupInvincible) || 
                                          ((g_CurrentFrame >= s_p1LastWakeupAttackFrame) && (g_CurrentFrame - s_p1LastWakeupAttackFrame <= 120) && s_p1WasLastWakeupInvincible);
        s_p1Timeline.isQuickRolling = s_p1Rolling;
        s_p1Timeline.isDelayedWakeup = s_p1DelayedWakeup;

        s_p2Timeline.isWakeupAttack = (g_P2_Lifecycle.inMove && g_P2_Lifecycle.isWakeupAttack) || 
                                      ((g_CurrentFrame >= s_p2LastWakeupAttackFrame) && (g_CurrentFrame - s_p2LastWakeupAttackFrame <= 120));
        s_p2Timeline.isWakeupInvincible = (g_P2_Lifecycle.inMove && g_P2_Lifecycle.isWakeupInvincible) || 
                                          ((g_CurrentFrame >= s_p2LastWakeupAttackFrame) && (g_CurrentFrame - s_p2LastWakeupAttackFrame <= 120) && s_p2WasLastWakeupInvincible);
        s_p2Timeline.isQuickRolling = s_p2Rolling;
        s_p2Timeline.isDelayedWakeup = s_p2DelayedWakeup;

        // -------------------------------------------------------------
        // Authentic Advantage Calculation (P1 Attacking)
        // -------------------------------------------------------------
        if (g_P1_Lifecycle.inMove) {
            if (!s_p1AdvState.inMove) {
                s_p1AdvState.inMove = true;
                s_p1AdvState.startFrame = g_CurrentFrame;
                s_p1AdvState.hasHit = false;
                s_p1AdvState.isBlocked = false;
                s_p1AdvState.hitFrame = 0;
                s_p1AdvState.attackerEndFrame = 0;
                s_p1AdvState.defenderReleaseFrame = 0;
                s_p1AdvState.advantageComputed = false;
            }
        } else if (s_p1AdvState.inMove) {
            if (!s_p1AdvState.hasHit) {
                if (g_P1_Lifecycle.isProjectile) {
                    s_p1AdvState.attackerEndFrame = (g_P1_Lifecycle.endFrame > 0) ? g_P1_Lifecycle.endFrame : g_CurrentFrame;
                    if (g_CurrentFrame > s_p1AdvState.attackerEndFrame + 180) {
                        s_p1AdvState.inMove = false; // Projectile whiffed off-screen
                    }
                } else {
                    s_p1Timeline.advantage = 0;
                    s_p1Timeline.hasAdvantage = false;
                    s_p1Timeline.isInfiniteAdvantage = false;

                    s_p2Timeline.advantage = 0;
                    s_p2Timeline.hasAdvantage = false;
                    s_p2Timeline.isInfiniteAdvantage = false;
                    s_p1AdvState.inMove = false;
                }
            } else {
                s_p1AdvState.attackerEndFrame = (g_P1_Lifecycle.endFrame > 0) ? g_P1_Lifecycle.endFrame : g_CurrentFrame;
                if (s_p1AdvState.defenderReleaseFrame > 0 && !s_p1AdvState.advantageComputed) {
                    int adv = (int)s_p1AdvState.defenderReleaseFrame - (int)s_p1AdvState.attackerEndFrame;
                    s_p1Timeline.advantage = adv;
                    s_p1Timeline.hasAdvantage = true;
                    s_p2Timeline.advantage = -adv;
                    s_p2Timeline.hasAdvantage = true;
                    s_p1AdvState.advantageComputed = true;
                    s_p1AdvState.inMove = false;
                }
            }
        }

        // If P1 move ended before P2 stun lock released (or detached projectile hit):
        if (s_p1AdvState.hasHit && !s_p1AdvState.advantageComputed && s_p1AdvState.attackerEndFrame > 0 && s_p1AdvState.defenderReleaseFrame > 0) {
            int adv = (int)s_p1AdvState.defenderReleaseFrame - (int)s_p1AdvState.attackerEndFrame;
            s_p1Timeline.advantage = adv;
            s_p1Timeline.hasAdvantage = true;
            s_p2Timeline.advantage = -adv;
            s_p2Timeline.hasAdvantage = true;
            s_p1AdvState.advantageComputed = true;
            s_p1AdvState.inMove = false;
            LogTrace("[AUTHENTIC ADVANTAGE COMPUTED] P1 Advantage: %+d | P2 Advantage: %+d (DefRelease: #%llu - AtkEnd: #%llu)",
                     adv, -adv, s_p1AdvState.defenderReleaseFrame, s_p1AdvState.attackerEndFrame);
        }

        // -------------------------------------------------------------
        // Authentic Advantage Calculation (P2 Attacking)
        // -------------------------------------------------------------
        if (g_P2_Lifecycle.inMove) {
            if (!s_p2AdvState.inMove) {
                s_p2AdvState.inMove = true;
                s_p2AdvState.startFrame = g_CurrentFrame;
                s_p2AdvState.hasHit = false;
                s_p2AdvState.isBlocked = false;
                s_p2AdvState.hitFrame = 0;
                s_p2AdvState.attackerEndFrame = 0;
                s_p2AdvState.defenderReleaseFrame = 0;
                s_p2AdvState.advantageComputed = false;
            }
        } else if (s_p2AdvState.inMove) {
            if (!s_p2AdvState.hasHit) {
                if (g_P2_Lifecycle.isProjectile) {
                    s_p2AdvState.attackerEndFrame = (g_P2_Lifecycle.endFrame > 0) ? g_P2_Lifecycle.endFrame : g_CurrentFrame;
                    if (g_CurrentFrame > s_p2AdvState.attackerEndFrame + 180) {
                        s_p2AdvState.inMove = false; // Projectile whiffed off-screen
                    }
                } else {
                    s_p1Timeline.advantage = 0;
                    s_p1Timeline.hasAdvantage = false;
                    s_p1Timeline.isInfiniteAdvantage = false;

                    s_p2Timeline.advantage = 0;
                    s_p2Timeline.hasAdvantage = false;
                    s_p2Timeline.isInfiniteAdvantage = false;
                    s_p2AdvState.inMove = false;
                }
            } else {
                s_p2AdvState.attackerEndFrame = (g_P2_Lifecycle.endFrame > 0) ? g_P2_Lifecycle.endFrame : g_CurrentFrame;
                if (s_p2AdvState.defenderReleaseFrame > 0 && !s_p2AdvState.advantageComputed) {
                    int adv = (int)s_p2AdvState.defenderReleaseFrame - (int)s_p2AdvState.attackerEndFrame;
                    s_p2Timeline.advantage = adv;
                    s_p2Timeline.hasAdvantage = true;
                    s_p1Timeline.advantage = -adv;
                    s_p1Timeline.hasAdvantage = true;
                    s_p2AdvState.advantageComputed = true;
                    s_p2AdvState.inMove = false;
                    LogTrace("[AUTHENTIC ADVANTAGE COMPUTED] P2 Advantage: %+d | P1 Advantage: %+d (DefRelease: #%llu - AtkEnd: #%llu)",
                             adv, -adv, s_p2AdvState.defenderReleaseFrame, s_p2AdvState.attackerEndFrame);
                }
            }
        }

        // If P2 move ended before P1 stun lock released, finalize when P1 releases:
        if (s_p2AdvState.hasHit && !s_p2AdvState.advantageComputed && s_p2AdvState.attackerEndFrame > 0 && s_p2AdvState.defenderReleaseFrame > 0) {
            int adv = (int)s_p2AdvState.defenderReleaseFrame - (int)s_p2AdvState.attackerEndFrame;
            s_p2Timeline.advantage = adv;
            s_p2Timeline.hasAdvantage = true;
            s_p1Timeline.advantage = -adv;
            s_p1Timeline.hasAdvantage = true;
            s_p2AdvState.advantageComputed = true;
            s_p2AdvState.inMove = false;
            LogTrace("[AUTHENTIC ADVANTAGE COMPUTED] P2 Advantage: %+d | P1 Advantage: %+d (DefRelease: #%llu - AtkEnd: #%llu)",
                     adv, -adv, s_p2AdvState.defenderReleaseFrame, s_p2AdvState.attackerEndFrame);
        }
    }
}
