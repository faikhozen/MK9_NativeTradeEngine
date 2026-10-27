#include "trade_engine.hpp"
#include "hook_pair.hpp"
#include "combat_state.hpp"
#include "frame_data.hpp"
#include "cancel_detector.hpp"
#include "toasty_training.hpp"
#include "reversal_engine.hpp"
#include "simulation.hpp"
#include "meter_drain_fix.hpp"
#include "move_priority.hpp"
#include "profiler.hpp"
#include "stun_modifier.hpp"
#include "free_camera.hpp"
#include "minhook/include/MinHook.h"


namespace MK9TradeFix {
    volatile bool g_JadeStaffRecoveryReady=false;
    static uintptr_t JadeStaffReference(void* fighter) {
        if(!fighter) return 0;
        uintptr_t pkg=*(uintptr_t*)((uintptr_t)fighter+0x2A8);
        if(pkg<0x01000000 || pkg==0xDEADBEEF || *(unsigned*)(pkg+0x20)<=153) return 0;
        if(*(uint32_t*)(pkg+4)!=*(uint32_t*)((uintptr_t)fighter+0x2AC)) return 0;
        uintptr_t desc=*(uintptr_t*)(*(uintptr_t*)(pkg+0x8C)+153*4);
        if(*(uint32_t*)(desc+4)!=0xED8472EE) return 0; // jade_reaction_cleanup
        return *(uintptr_t*)(*(uintptr_t*)(pkg+0x84)+0x1638);
    }
    volatile unsigned g_JadeStaffRecoveries=0,g_JadeStaffRecoveryFailures=0;
    volatile unsigned g_JadeStaffVisualsHidden=0;
    static bool ReturnJadeStaff(void* fighter,void* dyingParent) {
        __try {
            uintptr_t staff=JadeStaffReference(fighter);
            if(!staff || staff==0xDEADBEEF || *(uintptr_t*)staff!=0x00C67450 ||
               *(uint32_t*)(staff+4)==0xFFFFFFFF) return false;
            uintptr_t parent=*(uintptr_t*)(staff+0xB8);
            if(parent<0x01000000 || parent==0xDEADBEEF || parent==(uintptr_t)fighter || parent==staff ||
               *(uint32_t*)(parent+4)!=*(uint32_t*)(staff+0xBC)) return false;
            if(dyingParent) {
                if(parent!=(uintptr_t)dyingParent || dyingParent==fighter) return false;
            } else {
                const CharacterCombatState state=GetCharacterCombatState(fighter,nullptr);
                if(state!=STATE_NEUTRAL && state!=STATE_HITSTUN && state!=STATE_KNOCKDOWN) return false;
                // A preserved trade attack must finish before reclaiming its prop.
                if(IsActive5050Winner(fighter) || (g_DeferredHit.isPending &&
                    (g_DeferredHit.pAttacker==fighter || g_DeferredHit.pDefender==fighter))) return false;
            }
            // Same attachment and hidden holster used by jade_game_init and
            // set_staff_hidden_neutral. Native attachment removes the old child link.
            typedef void (__thiscall *Attach)(void*,void*,int,int);
            ((Attach)0x0043E2D0)((void*)staff,fighter,12,1);
            if(*(uintptr_t*)(staff+0xB8)!=(uintptr_t)fighter) { ++g_JadeStaffRecoveryFailures; return false; }
            typedef void (__cdecl *Position)(void*,void*,int,int);
            ((Position)0x004373E0)((void*)staff,fighter,12,1);
            typedef void (__thiscall *Hidden)(void*,int);
            (*(Hidden*)(*(uintptr_t*)staff+0x34))((void*)staff,1);
            ++g_JadeStaffRecoveries;
            return true;
        } __except(EXCEPTION_EXECUTE_HANDLER) { ++g_JadeStaffRecoveryFailures; return false; }
    }
    typedef void (__thiscall *tCleanupChildren)(void*,int);
    static tCleanupChildren oCleanupChildren=nullptr;
    static void __fastcall hkCleanupChildren(void* parent,void*,int destroy) {
        if(destroy && g_FixEnabled && IsMatchLoadedUp()) {
            ReturnJadeStaff(GetP1Char(),parent);
            ReturnJadeStaff(GetP2Char(),parent);
        }
        oCleanupChildren(parent,destroy);
    }
    static void HideInterruptedJadeStaffVisuals(void* fighter,bool recovered) {
        __try {
            uintptr_t staff=JadeStaffReference(fighter);
            if(!staff || *(uintptr_t*)(staff+0xB8)!=(uintptr_t)fighter) return;
            CharacterCombatState state=GetCharacterCombatState(fighter,nullptr);
            if(!recovered && state!=STATE_HITSTUN && state!=STATE_KNOCKDOWN) return;
            if(IsActive5050Winner(fighter) || (g_DeferredHit.isPending &&
                (g_DeferredHit.pAttacker==fighter || g_DeferredHit.pDefender==fighter))) return;
            uintptr_t globals=*(uintptr_t*)(*(uintptr_t*)((uintptr_t)fighter+0x2A8)+0x84);
            // These are Jade's transient staff animation proxy, beam and glow,
            // verified in staff_appear_chores / jade_reaction_cleanup. Keep their
            // native destruction and script references intact; only hide them.
            static const unsigned offsets[]={0x1640,0x1644,0x1648};
            static const uintptr_t types[]={0x00C67450,0x00C67088,0x00C671D0};
            for(unsigned i=0;i<3;++i) {
                uintptr_t object=*(uintptr_t*)(globals+offsets[i]);
                if(object<0x01000000 || object==0xDEADBEEF || object==staff ||
                   *(uintptr_t*)object!=types[i] || *(uint32_t*)(object+4)==0xFFFFFFFF) continue;
                uintptr_t actor=*(uintptr_t*)(object+8);
                if(actor<0x01000000 || actor==0xDEADBEEF || (*(uint32_t*)(actor+0x80)&2)) continue;
                typedef void (__thiscall *Hidden)(void*,int);
                (*(Hidden*)(*(uintptr_t*)object+0x34))((void*)object,1);
                ++g_JadeStaffVisualsHidden;
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) { ++g_JadeStaffRecoveryFailures; }
    }
    void FinalizeJadeStaffRecovery() {
        if(!g_JadeStaffRecoveryReady || !g_FixEnabled || !IsMatchLoadedUp()) return;
        void* p1=GetP1Char(); void* p2=GetP2Char();
        bool p1Recovered=ReturnJadeStaff(p1,nullptr);
        bool p2Recovered=ReturnJadeStaff(p2,nullptr);
        HideInterruptedJadeStaffVisuals(p1,p1Recovered);
        HideInterruptedJadeStaffVisuals(p2,p2Recovered);
    }
    static bool s_TradeHooksReady = false;
    bool AreTradeEngineHooksReady() { return s_TradeHooksReady; }

    DeferredHitContext g_DeferredHit;
    bool g_InCase1Resolution = false;
    bool g_InHitResolution = false;

    tApplyDamageAndReactions oApplyDamageAndReactions = nullptr;
    tAbortAttackerThread_OnHit oAbortAttackerThread_OnHit = nullptr;

    typedef void(__thiscall* tCleanupActivePropsAndHitboxes)(void* pChar);
    static tCleanupActivePropsAndHitboxes CleanupActivePropsAndHitboxes = (tCleanupActivePropsAndHitboxes)0x0089DC30;

    typedef void(__thiscall* tCombatClearMove)(void* pCombatController);
    static tCombatClearMove CombatClearMove1 = (tCombatClearMove)0x00446000;
    static tCombatClearMove CombatClearMove2 = (tCombatClearMove)0x00446010;

    typedef void(__thiscall* tCombatClearWeaponStance)(void* pChar, int zero);
    static tCombatClearWeaponStance CombatClearWeaponStance = (tCombatClearWeaponStance)0x004475F0;

    void SafeCleanupProps(void* pChar) {
        if (!pChar || (uintptr_t)pChar < 0x01000000) return;
        __try {
            void* pPropList = *(void**)((uintptr_t)pChar + 0x2F1C);
            if (pPropList && (uintptr_t)pPropList > 0x01000000) {
                if (CleanupActivePropsAndHitboxes) {
                    CleanupActivePropsAndHitboxes(pChar);
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    void SanitizeFighterWeaponState(void* pChar) {
        if (!pChar || (uintptr_t)pChar < 0x01000000) return;

        if (IsActive5050Winner(pChar)) {
            LogTrace("[50/50 PROP PRESERVATION] Protected Winner P%d weapon & prop state from sanitization", g_5050_WinnerPlr);
            return;
        }

        __try {
            void* pPropList = *(void**)((uintptr_t)pChar + 0x2F1C);
            uint32_t stanceFlags = *(uint32_t*)((uintptr_t)pChar + 0x2E8);
            
            bool isWeaponActive = (pPropList != nullptr) || ((stanceFlags & 0x200) != 0);
            if (!isWeaponActive) {
                return;
            }

            void* pCombat = *(void**)((uintptr_t)pChar + 0x7638);

            // 1. Clear weapon active stance flag (0x200) at Character + 0x2E8
            if (CombatClearWeaponStance) {
                CombatClearWeaponStance(pChar, 0);
            }
            *(uint32_t*)((uintptr_t)pChar + 0x2E8) &= ~0x200;

            // 2. Clear dangling move animation tracks on combat controller
            if (pCombat && (uintptr_t)pCombat > 0x01000000) {
                if (CombatClearMove1) CombatClearMove1(pCombat);
                if (CombatClearMove2) CombatClearMove2(pCombat);
            }

            // 3. Reset active weapon hitboxes
            *(uint32_t*)((uintptr_t)pChar + 0x7A3C) = 0;

            // 4. Holster/clean up weapon prop if spawned (Type A)
            if (pPropList != nullptr) {
                SafeCleanupProps(pChar);
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            LogTrace("[!] Caught exception in SanitizeFighterWeaponState.");
        }
    }

    void __fastcall hkAbortAttackerThread_OnHit(void* in_ECX, void* edxUnused, void* param_1, void* param_2) {
        if (!g_FixEnabled || IsCurrentMatchTag()) {
            if (oAbortAttackerThread_OnHit) {
                oAbortAttackerThread_OnHit(in_ECX, param_1, param_2);
            }
            return;
        }

        if (IsActive5050Winner(in_ECX)) {
            LogTrace("[50/50 THREAD PRESERVATION] Protected Winner P%d attack thread from cancellation", g_5050_WinnerPlr);
            return;
        }

        if (g_InCase1Resolution) {
            if (oAbortAttackerThread_OnHit) {
                oAbortAttackerThread_OnHit(in_ECX, param_1, param_2);
            }
            return;
        }

        void* pDefender = in_ECX;
        int defPlr = (pDefender == GetP2Char()) ? 2 : 1;

        bool isDefInHitWindow = IsDefenderInTradeWindow(pDefender);
        bool shouldPreserve = (g_DeferredHit.isPending && (g_DeferredHit.pDefender == pDefender || g_DeferredHit.pAttacker == pDefender)) || isDefInHitWindow;

        if (g_FixEnabled && shouldPreserve) {
            LogTrace("[CLASH] Preserved active attack hitboxes for P%d (Bypassed 0x008AB040)", defPlr);
            return;
        }

        if (MeterDrainFix::ShouldPreserveThreadOnHit(pDefender)) {
            return;
        }

        if (g_DiagnosticMode == 10 || (g_SimSeqType >= MACRO_SEQ_MODE10_F1_JC_EX_FORCEBALL_SOLO && g_SimSeqType <= MACRO_SEQ_MODE10_F8_JC_EX_FORCEBALL_VS_P1_STAND)) {
            __try {
                uint32_t flags2EC = *(uint32_t*)((uintptr_t)pDefender + 0x2EC);
                int func = *(int*)((uintptr_t)pDefender + 0x2F88);
                bool hasArmor = (flags2EC & 0x800000) != 0;
                LogTrace("[MODE 10 THREAD ABORT | Frame #%llu] P%d attack script thread ABORTED | Func: %d | Armor Bit (0x800000): %s",
                         g_CurrentFrame, defPlr, func, hasArmor ? "ACTIVE" : "INACTIVE");
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
        }

        if (oAbortAttackerThread_OnHit) {
            oAbortAttackerThread_OnHit(in_ECX, param_1, param_2);
        }
    }

    void FlushDeferredHit(const char* reason) {
        if (!g_DeferredHit.isPending) return;

        DeferredHitContext hit = g_DeferredHit;
        g_DeferredHit.isPending = false;
        g_InHitResolution = false;

        void* pDefender = hit.pDefender;
        void* pAttacker = hit.pAttacker;
        if (!pDefender || (uintptr_t)pDefender < 0x01000000) return;

        int attPlr = (pAttacker == GetP2Char()) ? 2 : 1;
        int defPlr = (attPlr == 1) ? 2 : 1;

        LogTrace("[CLASH FLUSH%s%s] Executing deferred P%d hit on P%d (Damage: %.1f%%, RxID: %d)",
                 reason ? " - " : "", reason ? reason : "",
                 attPlr, defPlr, hit.damage * 100.0f, hit.rxID);

        g_LastHitFrame = g_CurrentFrame;
        g_LastHitAttacker = attPlr;

        bool isBlocked = (hit.param_6 != 0 || hit.rxID == 0x00 || hit.rxID == 0x01);

        AttackClassification atk;
        memset(&atk, 0, sizeof(atk));
        atk.moveClass = hit.moveClass;
        atk.isUppercut = hit.isUppercut;
        atk.isJumpIn = hit.isJumpIn;
        atk.isProjectile = hit.isProjectile;

        // Stun modifier evaluated BEFORE native damage / reaction alters character state
        StunModifier::OnHitEvaluation(attPlr, defPlr, pDefender, pAttacker, isBlocked, hit.rxID, hit.damage, &atk);

        if (oApplyDamageAndReactions) {
            int* defParam1 = (hit.param_1 && (uintptr_t)hit.param_1 > 0x01000000) ? 
                             hit.param_1 : (int*)hit.param_1_storage;
            void* defECX = (hit.in_ECX && (uintptr_t)hit.in_ECX > 0x01000000) ? 
                           hit.in_ECX : nullptr;
            oApplyDamageAndReactions(defECX, nullptr, defParam1, pDefender, hit.param_3,
                                     hit.rxID, (uint8_t*)&hit.param_5_storage,
                                     hit.param_6, hit.damage, hit.param_8);
        }

        // Synchronize native reaction timer (+0x2F0) with added stun frames and apply block pushback
        StunModifier::OnPostDamageEvaluation(defPlr, pDefender, isBlocked, attPlr, pAttacker);

        // Combat Meter Gains
        bool isSpecial = (atk.moveClass == MOVE_CLASS_SPECIAL || atk.moveClass == MOVE_CLASS_EX_SPECIAL || atk.moveClass == MOVE_CLASS_XRAY);
        if (!isBlocked) {
            if (MeterDrainFix::g_EnableAttackerHitMeterGain) {
                float r = isSpecial ? MeterDrainFix::g_AttackerHitMeterRewardSpecials : MeterDrainFix::g_AttackerHitMeterRewardNormals;
                if (r > 0.0001f) {
                    MeterDrainFix::AwardPlayerMeter(attPlr, r);
                }
            }
            if (MeterDrainFix::g_EnableDefenderHitMeterGain) {
                float r = isSpecial ? MeterDrainFix::g_DefenderHitMeterRewardSpecials : MeterDrainFix::g_DefenderHitMeterRewardNormals;
                if (r > 0.0001f) {
                    MeterDrainFix::AwardPlayerMeter(defPlr, r);
                }
            }
        } else {
            if (MeterDrainFix::g_EnableDefenderBlockMeterGain) {
                float r = isSpecial ? MeterDrainFix::g_DefenderBlockMeterRewardSpecials : MeterDrainFix::g_DefenderBlockMeterRewardNormals;
                if (r > 0.0001f) {
                    MeterDrainFix::AwardPlayerMeter(defPlr, r);
                }
            }
        }

        // Notify Frame Data Subsystem and Toasty Training of authentic hit / block event
        {
            OnHitOrBlockEvent(attPlr, defPlr, isBlocked, g_CurrentFrame, hit.param_8);
            ToastyTraining::OnAttackHit(attPlr, defPlr, atk.isUppercut, isBlocked, hit.damage, hit.rxID);
            ReversalEngine::Get().NotifyIncomingHit(defPlr, attPlr, isBlocked, hit.rxID, atk, hit.damage);
            if (isBlocked && pDefender) {
                MeterDrainFix::OnBlockOrStunCheck(pDefender, g_CurrentFrame, true);
            }
        }
    }

    static void __fastcall hkApplyDamageAndReactions_Impl(
        void* in_ECX,
        void* edxUnused,
        int* param_1,
        void* param_2,
        uint32_t param_3,
        int param_4,
        uint8_t* param_5,
        int param_6,
        float param_7,
        int param_8
    ) {
        if (!g_FixEnabled || IsCurrentMatchTag()) {
            void* pDefender = param_2;
            int defPlr = (pDefender == GetP2Char()) ? 2 : 1;
            int attPlr = (defPlr == 1) ? 2 : 1;
            void* pAttacker = (attPlr == 1) ? GetP1Char() : GetP2Char();
            bool isBlocked = (param_6 != 0 || param_4 == 0x00 || param_4 == 0x01);

            AttackClassification atk = AnalyzeAttack(pAttacker, pDefender, param_1, param_5, param_7, param_8);

            // Stun modifier evaluated BEFORE native damage / reaction alters character state
            StunModifier::OnHitEvaluation(attPlr, defPlr, pDefender, pAttacker, isBlocked, param_4, param_7, &atk);

            if (oApplyDamageAndReactions) {
                oApplyDamageAndReactions(in_ECX, edxUnused, param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8);
            }

            // Synchronize native reaction timer (+0x2F0) with added stun frames and apply block pushback
            StunModifier::OnPostDamageEvaluation(defPlr, pDefender, isBlocked, attPlr, pAttacker);

            // Combat Meter Gains
            bool isSpecial = (atk.moveClass == MOVE_CLASS_SPECIAL || atk.moveClass == MOVE_CLASS_EX_SPECIAL || atk.moveClass == MOVE_CLASS_XRAY);
            if (!isBlocked) {
                if (MeterDrainFix::g_EnableAttackerHitMeterGain) {
                    float r = isSpecial ? MeterDrainFix::g_AttackerHitMeterRewardSpecials : MeterDrainFix::g_AttackerHitMeterRewardNormals;
                    if (r > 0.0001f) {
                        MeterDrainFix::AwardPlayerMeter(attPlr, r);
                    }
                }
                if (MeterDrainFix::g_EnableDefenderHitMeterGain) {
                    float r = isSpecial ? MeterDrainFix::g_DefenderHitMeterRewardSpecials : MeterDrainFix::g_DefenderHitMeterRewardNormals;
                    if (r > 0.0001f) {
                        MeterDrainFix::AwardPlayerMeter(defPlr, r);
                    }
                }
            } else {
                if (MeterDrainFix::g_EnableDefenderBlockMeterGain) {
                    float r = isSpecial ? MeterDrainFix::g_DefenderBlockMeterRewardSpecials : MeterDrainFix::g_DefenderBlockMeterRewardNormals;
                    if (r > 0.0001f) {
                        MeterDrainFix::AwardPlayerMeter(defPlr, r);
                    }
                }
            }

            if (atk.moveClass == MOVE_CLASS_NORMAL_LIMB || atk.moveClass == MOVE_CLASS_NORMAL_WEAPON) {
                MeterDrainFix::NotifyNormalAttack(attPlr);
                CancelDetector::OnNormalImpact(attPlr, isBlocked, param_4);
            }

            // Always notify Frame Data Subsystem of authentic hit / block events in Vanilla mode
            OnHitOrBlockEvent(attPlr, defPlr, isBlocked, g_CurrentFrame, param_8);
            ToastyTraining::OnAttackHit(attPlr, defPlr, atk.isUppercut, isBlocked, param_7, param_4);
            ReversalEngine::Get().NotifyIncomingHit(defPlr, attPlr, isBlocked, param_4, atk, param_7);

            g_LastHitFrame = g_CurrentFrame;
            g_LastHitAttacker = attPlr;
            return;
        }

        g_HitEventCount++;

        void* pDefender = param_2;
        int defPlr = (pDefender == GetP2Char()) ? 2 : 1;
        int attPlr = (defPlr == 1) ? 2 : 1;
        void* pAttacker = (attPlr == 1) ? GetP1Char() : GetP2Char();

        // If there's an existing deferred hit from the SAME attacker, flush it first
        if (g_DeferredHit.isPending && g_DeferredHit.pAttacker == pAttacker) {
            FlushDeferredHit("NEW HIT FROM SAME ATTACKER");
        }

        // Meter Drain Glitch Fix: Neutralize disabling hitstun and inject Frame-1 Super Armor
        if (MeterDrainFix::HandleIncomingHitOnEXStartup(pDefender, pAttacker, param_1, &param_4, param_5, param_6, param_7, param_8)) {
            OnHitOrBlockEvent(attPlr, defPlr, false, g_CurrentFrame, param_8);
            g_LastHitFrame = g_CurrentFrame;
            g_LastHitAttacker = attPlr;
            if (g_DeferredHit.isPending && g_DeferredHit.pDefender == pDefender) {
                g_DeferredHit.isPending = false;
            }
            return;
        }

        // 50/50 Winner Immunity
        if (g_5050_WinnerPlr != 0 && g_CurrentFrame <= g_5050_ExpireFrame) {
            if (defPlr == g_5050_WinnerPlr || IsActive5050Winner(pDefender)) {
                LogTrace("[50/50 PROTECTION] Blocked damage/reaction on Winner P%d on frame %llu (Exp: %llu)",
                         g_5050_WinnerPlr, g_CurrentFrame, g_5050_ExpireFrame);
                return;
            }
        }

        AttackClassification atk = AnalyzeAttack(pAttacker, pDefender, param_1, param_5, param_7, param_8);

        const char* classLabel = "[NORMAL ATTACK (Limb)]";
        if (atk.moveClass == MOVE_CLASS_NORMAL_WEAPON) {
            classLabel = "[NORMAL ATTACK (Weapon)]";
        } else if (atk.moveClass == MOVE_CLASS_SPECIAL) {
            classLabel = "[SPECIAL MOVE]";
        } else if (atk.moveClass == MOVE_CLASS_EX_SPECIAL) {
            classLabel = "[EX SPECIAL MOVE]";
        } else if (atk.moveClass == MOVE_CLASS_XRAY) {
            classLabel = "[X-RAY]";
        }

        LogTrace("[HIT #%d] P%d hit P%d (Damage: %.1f%%) | RxID: %d | Move Class: %s", 
                 g_HitEventCount, attPlr, defPlr, param_7 * 100.0f, param_4, classLabel);
        LogTrace("   Category:       %s", atk.category);
        LogTrace("   Delivery:       %s", atk.isProjectile ? "Spawned Projectile Entity (Detached Actor)" : "Physical Skeleton Hitbox (Bone Strike)");
        LogTrace("   Property:       %s [ID: %u]", atk.propertyName, atk.atkType);
        LogTrace("   Reason:         %s", atk.reason);
        if (atk.isProp) {
            LogTrace("   Hierarchy:      %s", atk.isChildActor ? "Child Actor (Spawned Attached Entity)" : "Parent Actor (Direct Skeletal Rig)");
            LogTrace("   Hitbox State:   %s", atk.hasHitbox ? "ACTIVE (3D Physical Hitbox Deployed)" : "INACTIVE (No Physical Hitbox)");
        } else if (atk.moveClass == MOVE_CLASS_NORMAL_LIMB) {
            LogTrace("   Hitbox State:   %s", atk.hasHitbox ? "ACTIVE (Limb Physical Hitbox)" : "INACTIVE");
        }
        LogTrace("   Trade System:   %s", atk.isExcludedFromTrades ? "EXCLUDED (Special / Super Move)" : "ELIGIBLE (Normal Attack Trade Engine Active)");
        if (atk.moveClass == MOVE_CLASS_NORMAL_LIMB || atk.moveClass == MOVE_CLASS_NORMAL_WEAPON) {
            bool isBlockedHit = (param_6 != 0 || param_4 == 0x00 || param_4 == 0x01);
            CancelDetector::OnNormalImpact(attPlr, isBlockedHit, param_4);
        }

        if (g_SimSeqType == MACRO_SEQ_MODE18_NORMAL_CANCEL && attPlr == 1) {
            // FP hits at g_SimTriggerFrame + 11 with RxID 22.
            // Genuine follow-up hits (Spear, EX Spear, X-Ray) connect after frame +15 with param_4 != 22.
            if (g_CurrentFrame > g_SimTriggerFrame + 15 && param_4 != 22 && !g_Mode18_FollowUpHitDetected) {
                g_Mode18_FollowUpHitDetected = true;
                g_Mode18_FollowUpHitFrame = g_CurrentFrame;
                g_Mode18_FollowUpDamage = param_7 * 100.0f;
                g_Mode18_FollowUpRxID = param_4;
                g_Mode18_FollowUpMoveClass = classLabel;
            }
        }

        if (g_SimSeqType == MACRO_SEQ_MODE19_KABAL_BUG && attPlr == 1) {
            g_Mode19_HitCount++;
            g_Mode19_TotalDamage += (param_7 * 100.0f);
            if (g_Mode19_HitCount == 1) g_Mode19_Hit1Frame = g_CurrentFrame;
            else if (g_Mode19_HitCount == 2) g_Mode19_Hit2Frame = g_CurrentFrame;
            else if (g_Mode19_HitCount == 3) g_Mode19_Hit3Frame = g_CurrentFrame;
            else if (g_Mode19_HitCount >= 4) g_Mode19_Hit4Frame = g_CurrentFrame;
        }

        if (g_DiagnosticMode == 10 || (g_SimSeqType >= MACRO_SEQ_MODE10_F1_JC_EX_FORCEBALL_SOLO && g_SimSeqType <= MACRO_SEQ_MODE10_F8_JC_EX_FORCEBALL_VS_P1_STAND)) {
            __try {
                uint32_t defFlags2EC = *(uint32_t*)((uintptr_t)pDefender + 0x2EC);
                uint32_t defAction   = *(uint32_t*)((uintptr_t)pDefender + 0x1D4);
                int      defFunc     = *(int*)((uintptr_t)pDefender + 0x2F88);
                bool     defHasArmor = (defFlags2EC & 0x800000) != 0;
                bool     defIsSpecial= (defFlags2EC & 0x200) != 0;
                bool     defIsNormal = (defFlags2EC & 0x400) != 0;

                LogTrace("   --------------------------------------------------------------------");
                LogTrace("   >>> [MODE 10 METER DRAIN / ARMOR INTERRUPT ANALYSIS | Frame #%llu] <<<", g_CurrentFrame);
                LogTrace("      Attacker P%d Move Class: %s (Property: %s)", attPlr, classLabel, atk.propertyName);
                LogTrace("      Defender P%d State at Impact:", defPlr);
                LogTrace("         Action State:   0x%04X", defAction);
                LogTrace("         Script Func:    %d", defFunc);
                LogTrace("         Combat Flags:   0x%08X", defFlags2EC);
                LogTrace("         Super Armor:    %s (Bit 0x800000)", defHasArmor ? "ACTIVE [ARMORED]" : "INACTIVE [VULNERABLE]");
                LogTrace("         Special Bit:    %s (Bit 0x200)", defIsSpecial ? "YES" : "NO");
                LogTrace("         Normal Bit:     %s (Bit 0x400)", defIsNormal ? "YES" : "NO");

                if (!defHasArmor && (defIsSpecial || defFunc > 0)) {
                    LogTrace("      >>> ROOT CAUSE (METER DRAIN GLITCH DETECTED):");
                    LogTrace("      >>> Defender P%d spent EX Meter to initiate an EX Special move,", defPlr);
                    LogTrace("      >>> BUT Attacker P%d's Normal (%s) connected during Defender's early startup PRE-ARMOR window!", attPlr, atk.propertyName);
                    LogTrace("      >>> NetherRealm engine applies full hit reaction (RxID: %d) and aborts move thread, draining 1 bar of meter without armor absorbing!", param_4);
                } else if (defHasArmor) {
                    LogTrace("      >>> Super Armor bit (0x800000) WAS ACTIVE at impact! Investigating why reaction/damage applied.");
                }
                LogTrace("   --------------------------------------------------------------------");
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
        }

        // ============================================================================
        // CASE 1: MUTUAL CLASH RESOLUTION (Both authentic hits landed)
        // ============================================================================
        if (!g_InCase1Resolution && g_DeferredHit.isPending && 
            g_DeferredHit.pAttacker == pDefender && g_DeferredHit.pDefender == pAttacker) {
            
            bool curIsXRay     = (atk.moveClass == MOVE_CLASS_XRAY);
            bool defIsXRay     = g_DeferredHit.isXRay || (g_DeferredHit.moveClass == MOVE_CLASS_XRAY);

            bool curIsSpecial  = (atk.moveClass == MOVE_CLASS_SPECIAL || atk.moveClass == MOVE_CLASS_EX_SPECIAL);
            bool defIsSpecial  = g_DeferredHit.isSpecial || (g_DeferredHit.moveClass == MOVE_CLASS_SPECIAL || g_DeferredHit.moveClass == MOVE_CLASS_EX_SPECIAL);

            bool curIsUppercut = atk.isUppercut;
            bool defIsUppercut = g_DeferredHit.isUppercut;

            bool curIsJumpIn   = atk.isJumpIn;
            bool defIsJumpIn   = g_DeferredHit.isJumpIn;

            bool curIsNormal   = !curIsSpecial && !curIsXRay && !curIsJumpIn && !curIsUppercut;
            bool defIsNormal   = !defIsSpecial && !defIsXRay && !defIsJumpIn && !defIsUppercut;

            bool curIsSpecAttached = curIsSpecial && !atk.isProjectile;
            bool curIsSpecDetached = curIsSpecial && atk.isProjectile;
            bool defIsSpecAttached = defIsSpecial && !g_DeferredHit.isProjectile;
            bool defIsSpecDetached = defIsSpecial && g_DeferredHit.isProjectile;

            // Priority 0: X-Ray Absolute Priority
            if (MovePriority::g_XRayAbsolutePriority && (curIsXRay || defIsXRay)) {
                if (curIsXRay) {
                    g_DeferredHit.isPending = false;
                    LogTrace(">>> [X-RAY PRIORITY] P%d X-Ray cleanly beat deferred P%d hit! <<<", attPlr, defPlr);
                    if (oApplyDamageAndReactions && pDefender) {
                        oApplyDamageAndReactions(in_ECX, edxUnused, param_1, pDefender, param_3, param_4, param_5, param_6, param_7, param_8);
                    }
                    if (oAbortAttackerThread_OnHit && pDefender) {
                        oAbortAttackerThread_OnHit(pDefender, nullptr, nullptr);
                    }
                    return;
                } else {
                    g_DeferredHit.isPending = false;
                    LogTrace(">>> [X-RAY PRIORITY] Deferred P%d X-Ray cleanly beat incoming P%d hit! <<<", defPlr, attPlr);
                    if (oApplyDamageAndReactions && g_DeferredHit.pDefender) {
                        int* defParam1 = (g_DeferredHit.param_1 && (uintptr_t)g_DeferredHit.param_1 > 0x01000000) ? 
                                         g_DeferredHit.param_1 : param_1;
                        void* defECX = (g_DeferredHit.in_ECX && (uintptr_t)g_DeferredHit.in_ECX > 0x01000000) ? 
                                       g_DeferredHit.in_ECX : in_ECX;
                        oApplyDamageAndReactions(defECX, nullptr, defParam1, g_DeferredHit.pDefender, g_DeferredHit.param_3,
                                                 g_DeferredHit.rxID, (uint8_t*)&g_DeferredHit.param_5_storage,
                                                 g_DeferredHit.param_6, g_DeferredHit.damage, g_DeferredHit.param_8);
                    }
                    if (oAbortAttackerThread_OnHit && pAttacker) {
                        oAbortAttackerThread_OnHit(pAttacker, nullptr, nullptr);
                    }
                    return;
                }
            }

            // Priority Rule 1A: Uppercut vs Uppercut (Later inputted D+2 wins by default)
            if (MovePriority::g_UppercutVsUppercut && curIsUppercut && defIsUppercut) {
                g_DeferredHit.isPending = false;
                
                int curPlr = attPlr;
                int defPlr = (g_DeferredHit.pAttacker == GetP2Char()) ? 2 : 1;
                
                uint64_t curStart = (curPlr == 2) ? g_P2_AttackStartFrame : g_P1_AttackStartFrame;
                uint64_t defStart = (defPlr == 2) ? g_P2_AttackStartFrame : g_P1_AttackStartFrame;

                bool curWins = false;
                if (curStart > defStart) {
                    curWins = true;
                } else if (defStart > curStart) {
                    curWins = false;
                } else {
                    curWins = true; 
                }

                if (curWins) {
                    LogTrace("====================================================================");
                    LogTrace(" >>> [UPPERCUT VS UPPERCUT] P%d Later Inputted Uppercut WINS over P%d! <<<", curPlr, defPlr);
                    LogTrace("====================================================================");
                    if (oApplyDamageAndReactions && pDefender) {
                        oApplyDamageAndReactions(in_ECX, edxUnused, param_1, pDefender, param_3, param_4, param_5, param_6, param_7, param_8);
                    }
                    ToastyTraining::OnAttackHit(curPlr, defPlr, true, false, param_7, param_4);
                    if (oAbortAttackerThread_OnHit && pDefender) {
                        oAbortAttackerThread_OnHit(pDefender, nullptr, nullptr);
                    }
                    return;
                } else {
                    LogTrace("====================================================================");
                    LogTrace(" >>> [UPPERCUT VS UPPERCUT] P%d Later Inputted Uppercut WINS over P%d! <<<", defPlr, curPlr);
                    LogTrace("====================================================================");
                    if (oApplyDamageAndReactions && g_DeferredHit.pDefender) {
                        int* defParam1 = (g_DeferredHit.param_1 && (uintptr_t)g_DeferredHit.param_1 > 0x01000000) ? 
                                         g_DeferredHit.param_1 : param_1;
                        void* defECX = (g_DeferredHit.in_ECX && (uintptr_t)g_DeferredHit.in_ECX > 0x01000000) ? 
                                       g_DeferredHit.in_ECX : in_ECX;
                        oApplyDamageAndReactions(defECX, nullptr, defParam1, g_DeferredHit.pDefender, g_DeferredHit.param_3,
                                                 g_DeferredHit.rxID, (uint8_t*)&g_DeferredHit.param_5_storage,
                                                 g_DeferredHit.param_6, g_DeferredHit.damage, g_DeferredHit.param_8);
                    }
                    ToastyTraining::OnAttackHit(defPlr, curPlr, true, false, g_DeferredHit.damage, g_DeferredHit.rxID);
                    if (oAbortAttackerThread_OnHit && pAttacker) {
                        oAbortAttackerThread_OnHit(pAttacker, nullptr, nullptr);
                    }
                    return;
                }
            }

            // Priority Rule 1B: Uppercut (D+2) Anti-Air Priority (D+2 cleanly beats incoming Jump-In attacks during active anti-air frames)
            if (MovePriority::g_UppercutAntiAir) {
                if (curIsUppercut && defIsJumpIn) {
                    g_DeferredHit.isPending = false;
                    LogTrace("====================================================================");
                    LogTrace(" >>> [UPPERCUT ANTI-AIR PRIORITY] P%d Normal Uppercut (D+2) beat P%d Jump-In Attack! <<<", attPlr, defPlr);
                    LogTrace("====================================================================");
                    if (oApplyDamageAndReactions && pDefender) {
                        oApplyDamageAndReactions(in_ECX, edxUnused, param_1, pDefender, param_3, param_4, param_5, param_6, param_7, param_8);
                    }
                    ToastyTraining::OnAttackHit(attPlr, defPlr, true, false, param_7, param_4);
                    if (oAbortAttackerThread_OnHit && pDefender) {
                        oAbortAttackerThread_OnHit(pDefender, nullptr, nullptr);
                    }
                    return;
                }
                if (defIsUppercut && curIsJumpIn) {
                    g_DeferredHit.isPending = false;
                    LogTrace("====================================================================");
                    LogTrace(" >>> [UPPERCUT ANTI-AIR PRIORITY] P%d Normal Uppercut (D+2) beat P%d Jump-In Attack! <<<", defPlr, attPlr);
                    LogTrace("====================================================================");
                    if (oApplyDamageAndReactions && g_DeferredHit.pDefender) {
                        int* defParam1 = (g_DeferredHit.param_1 && (uintptr_t)g_DeferredHit.param_1 > 0x01000000) ? 
                                         g_DeferredHit.param_1 : param_1;
                        void* defECX = (g_DeferredHit.in_ECX && (uintptr_t)g_DeferredHit.in_ECX > 0x01000000) ? 
                                       g_DeferredHit.in_ECX : in_ECX;
                        oApplyDamageAndReactions(defECX, nullptr, defParam1, g_DeferredHit.pDefender, g_DeferredHit.param_3,
                                                 g_DeferredHit.rxID, (uint8_t*)&g_DeferredHit.param_5_storage,
                                                 g_DeferredHit.param_6, g_DeferredHit.damage, g_DeferredHit.param_8);
                    }
                    ToastyTraining::OnAttackHit(defPlr, attPlr, true, false, g_DeferredHit.damage, g_DeferredHit.rxID);
                    if (oAbortAttackerThread_OnHit && pAttacker) {
                        oAbortAttackerThread_OnHit(pAttacker, nullptr, nullptr);
                    }
                    return;
                }
            }

            // Priority Rule 1C: Uppercut vs Special (D+2 cleanly overpowers grounded special moves during active clash frames)
            if (MovePriority::g_UppercutVsSpecial) {
                if (curIsUppercut && defIsSpecial) {
                    g_DeferredHit.isPending = false;
                    LogTrace("====================================================================");
                    LogTrace(" >>> [UPPERCUT VS SPECIAL PRIORITY] P%d Normal Uppercut (D+2) beat P%d Special Move! <<<", attPlr, defPlr);
                    LogTrace("====================================================================");
                    if (oApplyDamageAndReactions && pDefender) {
                        oApplyDamageAndReactions(in_ECX, edxUnused, param_1, pDefender, param_3, param_4, param_5, param_6, param_7, param_8);
                    }
                    ToastyTraining::OnAttackHit(attPlr, defPlr, true, false, param_7, param_4);
                    if (oAbortAttackerThread_OnHit && pDefender) {
                        oAbortAttackerThread_OnHit(pDefender, nullptr, nullptr);
                    }
                    return;
                }
                if (defIsUppercut && curIsSpecial) {
                    g_DeferredHit.isPending = false;
                    LogTrace("====================================================================");
                    LogTrace(" >>> [UPPERCUT VS SPECIAL PRIORITY] P%d Normal Uppercut (D+2) beat P%d Special Move! <<<", defPlr, attPlr);
                    LogTrace("====================================================================");
                    if (oApplyDamageAndReactions && g_DeferredHit.pDefender) {
                        int* defParam1 = (g_DeferredHit.param_1 && (uintptr_t)g_DeferredHit.param_1 > 0x01000000) ? 
                                         g_DeferredHit.param_1 : param_1;
                        void* defECX = (g_DeferredHit.in_ECX && (uintptr_t)g_DeferredHit.in_ECX > 0x01000000) ? 
                                       g_DeferredHit.in_ECX : in_ECX;
                        oApplyDamageAndReactions(defECX, nullptr, defParam1, g_DeferredHit.pDefender, g_DeferredHit.param_3,
                                                 g_DeferredHit.rxID, (uint8_t*)&g_DeferredHit.param_5_storage,
                                                 g_DeferredHit.param_6, g_DeferredHit.damage, g_DeferredHit.param_8);
                    }
                    ToastyTraining::OnAttackHit(defPlr, attPlr, true, false, g_DeferredHit.damage, g_DeferredHit.rxID);
                    if (oAbortAttackerThread_OnHit && pAttacker) {
                        oAbortAttackerThread_OnHit(pAttacker, nullptr, nullptr);
                    }
                    return;
                }
            }


            // Priority Rule 3: Jump-In Attacks (Jump-ins trade or cleanly beat grounded standing normals during active falling frames)
            if (MovePriority::g_JumpInPriority) {
                if (curIsJumpIn && defIsNormal) {
                    g_DeferredHit.isPending = false;
                    LogTrace("====================================================================");
                    LogTrace(" >>> [JUMP-IN PRIORITY WIN] P%d Jump-In Attack beat P%d Standing Normal! <<<", attPlr, defPlr);
                    LogTrace("====================================================================");
                    if (oApplyDamageAndReactions && pDefender) {
                        oApplyDamageAndReactions(in_ECX, edxUnused, param_1, pDefender, param_3, param_4, param_5, param_6, param_7, param_8);
                    }
                    if (oAbortAttackerThread_OnHit && pDefender) {
                        oAbortAttackerThread_OnHit(pDefender, nullptr, nullptr);
                    }
                    return;
                }
                if (defIsJumpIn && curIsNormal) {
                    g_DeferredHit.isPending = false;
                    LogTrace("====================================================================");
                    LogTrace(" >>> [JUMP-IN PRIORITY WIN] P%d Jump-In Attack beat P%d Standing Normal! <<<", defPlr, attPlr);
                    LogTrace("====================================================================");
                    if (oApplyDamageAndReactions && g_DeferredHit.pDefender) {
                        int* defParam1 = (g_DeferredHit.param_1 && (uintptr_t)g_DeferredHit.param_1 > 0x01000000) ? 
                                         g_DeferredHit.param_1 : param_1;
                        void* defECX = (g_DeferredHit.in_ECX && (uintptr_t)g_DeferredHit.in_ECX > 0x01000000) ? 
                                       g_DeferredHit.in_ECX : in_ECX;
                        oApplyDamageAndReactions(defECX, nullptr, defParam1, g_DeferredHit.pDefender, g_DeferredHit.param_3,
                                                 g_DeferredHit.rxID, (uint8_t*)&g_DeferredHit.param_5_storage,
                                                 g_DeferredHit.param_6, g_DeferredHit.damage, g_DeferredHit.param_8);
                    }
                    if (oAbortAttackerThread_OnHit && pAttacker) {
                        oAbortAttackerThread_OnHit(pAttacker, nullptr, nullptr);
                    }
                    return;
                }
            }

            // Priority Rule 3.5: 50/50 Simultaneous Special / EX / X-Ray Clash Resolution (Strict 0f Delta)
            bool curIsSpecialOrXRay = (curIsSpecial || curIsXRay);
            bool defIsSpecialOrXRay = (defIsSpecial || defIsXRay);
            uint64_t curSpecialStart = (attPlr == 2) ? g_P2_AttackStartFrame : g_P1_AttackStartFrame;
            uint64_t defSpecialStart = (defPlr == 2) ? g_P2_AttackStartFrame : g_P1_AttackStartFrame;

            if (curIsSpecialOrXRay && defIsSpecialOrXRay &&
                curSpecialStart != 0 && defSpecialStart != 0 && curSpecialStart == defSpecialStart) {

                g_DeferredHit.isPending = false;

                int winnerPlr = (g_CurrentFrame % 2 == 0) ? 1 : 2;
                int loserPlr  = (winnerPlr == 1) ? 2 : 1;

                g_5050_WinnerPlr   = winnerPlr;
                g_5050_LoserPlr    = loserPlr;
                g_5050_ExpireFrame = g_CurrentFrame + 60;

                LogTrace("====================================================================");
                LogTrace("[50/50] Simultaneous Special Clash detected – Winner = P%d, Loser = P%d – prop preserved on winner",
                         g_5050_WinnerPlr, g_5050_LoserPlr);
                LogTrace("====================================================================");

                if (winnerPlr == attPlr) {
                    if (oApplyDamageAndReactions && pDefender) {
                        oApplyDamageAndReactions(in_ECX, edxUnused, param_1, pDefender, param_3, param_4, param_5, param_6, param_7, param_8);
                    }
                    if (oAbortAttackerThread_OnHit && pDefender) {
                        oAbortAttackerThread_OnHit(pDefender, nullptr, nullptr);
                    }
                    SanitizeFighterWeaponState(pDefender);
                    if (loserPlr == 1) { g_P1_AttackStartFrame = 0; g_P1_LastScriptFunc = -1; }
                    else { g_P2_AttackStartFrame = 0; g_P2_LastScriptFunc = -1; }

                    s_p1Timeline.advantage = (winnerPlr == 1) ? 24 : -24;
                    s_p1Timeline.hasAdvantage = true;
                    s_p2Timeline.advantage = (winnerPlr == 2) ? 24 : -24;
                    s_p2Timeline.hasAdvantage = true;

                    g_LastHitFrame = g_CurrentFrame;
                    g_LastHitAttacker = winnerPlr;
                    return;
                } else {
                    if (oApplyDamageAndReactions && g_DeferredHit.pDefender) {
                        int* defParam1 = (g_DeferredHit.param_1 && (uintptr_t)g_DeferredHit.param_1 > 0x01000000) ? 
                                         g_DeferredHit.param_1 : param_1;
                        void* defECX = (g_DeferredHit.in_ECX && (uintptr_t)g_DeferredHit.in_ECX > 0x01000000) ? 
                                       g_DeferredHit.in_ECX : in_ECX;
                        oApplyDamageAndReactions(defECX, nullptr, defParam1, g_DeferredHit.pDefender, g_DeferredHit.param_3,
                                                 g_DeferredHit.rxID, (uint8_t*)&g_DeferredHit.param_5_storage,
                                                 g_DeferredHit.param_6, g_DeferredHit.damage, g_DeferredHit.param_8);
                    }
                    if (oAbortAttackerThread_OnHit && pAttacker) {
                        oAbortAttackerThread_OnHit(pAttacker, nullptr, nullptr);
                    }
                    SanitizeFighterWeaponState(pAttacker);
                    if (loserPlr == 1) { g_P1_AttackStartFrame = 0; g_P1_LastScriptFunc = -1; }
                    else { g_P2_AttackStartFrame = 0; g_P2_LastScriptFunc = -1; }

                    s_p1Timeline.advantage = (winnerPlr == 1) ? 24 : -24;
                    s_p1Timeline.hasAdvantage = true;
                    s_p2Timeline.advantage = (winnerPlr == 2) ? 24 : -24;
                    s_p2Timeline.hasAdvantage = true;

                    g_LastHitFrame = g_CurrentFrame;
                    g_LastHitAttacker = winnerPlr;
                    return;
                }
            }

            // Priority Rule 4: Special Attached vs Special Detached (Instant Projectile Impacts)
            if (MovePriority::g_InstantProjectiles) {
                if (curIsSpecAttached && defIsSpecDetached) {
                    g_DeferredHit.isPending = false;
                    LogTrace("====================================================================");
                    LogTrace(" >>> [SPECIAL ATTACHED PRIORITY WIN] P%d Special Attached (Bone Strike) beat P%d Special Detached (Projectile)! <<<", attPlr, defPlr);
                    LogTrace("====================================================================");
                    if (oApplyDamageAndReactions && pDefender) {
                        oApplyDamageAndReactions(in_ECX, edxUnused, param_1, pDefender, param_3, param_4, param_5, param_6, param_7, param_8);
                    }
                    if (oAbortAttackerThread_OnHit && pDefender) {
                        oAbortAttackerThread_OnHit(pDefender, nullptr, nullptr);
                    }
                    return;
                }
                if (defIsSpecAttached && curIsSpecDetached) {
                    g_DeferredHit.isPending = false;
                    LogTrace("====================================================================");
                    LogTrace(" >>> [SPECIAL ATTACHED PRIORITY WIN] P%d Special Attached (Bone Strike) beat P%d Special Detached (Projectile)! <<<", defPlr, attPlr);
                    LogTrace("====================================================================");
                    if (oApplyDamageAndReactions && g_DeferredHit.pDefender) {
                        int* defParam1 = (g_DeferredHit.param_1 && (uintptr_t)g_DeferredHit.param_1 > 0x01000000) ? 
                                         g_DeferredHit.param_1 : param_1;
                        void* defECX = (g_DeferredHit.in_ECX && (uintptr_t)g_DeferredHit.in_ECX > 0x01000000) ? 
                                       g_DeferredHit.in_ECX : in_ECX;
                        oApplyDamageAndReactions(defECX, nullptr, defParam1, g_DeferredHit.pDefender, g_DeferredHit.param_3,
                                                 g_DeferredHit.rxID, (uint8_t*)&g_DeferredHit.param_5_storage,
                                                 g_DeferredHit.param_6, g_DeferredHit.damage, g_DeferredHit.param_8);
                    }
                    if (oAbortAttackerThread_OnHit && pAttacker) {
                        oAbortAttackerThread_OnHit(pAttacker, nullptr, nullptr);
                    }
                    return;
                }
            }

            // Priority Rule 5: Normal vs Normal Start-Frame Comparison & Mutual Trades
            if (curIsNormal && defIsNormal) {
                int curPlr = attPlr;
                int defPlr = (g_DeferredHit.pAttacker == GetP2Char()) ? 2 : 1;
                uint64_t curStart = (curPlr == 2) ? g_P2_AttackStartFrame : g_P1_AttackStartFrame;
                uint64_t defStart = (defPlr == 2) ? g_P2_AttackStartFrame : g_P1_AttackStartFrame;

                if (curStart != 0 && defStart != 0 && curStart != defStart) {
                    g_DeferredHit.isPending = false;
                    if (curStart < defStart) {
                        LogTrace("====================================================================");
                        LogTrace(" >>> [STARTUP INTERRUPT – earlier attack wins] P%d (start %llu) cleanly beat P%d (start %llu, +%lldf delta)! <<<",
                                 curPlr, curStart, defPlr, defStart, (int64_t)defStart - (int64_t)curStart);
                        LogTrace("====================================================================");
                        StunModifier::OnHitEvaluation(curPlr, defPlr, pDefender, pAttacker, false, param_4, param_7, &atk);
                        HitIndicationHUD::TriggerHitIndication(curPlr, HitIndicationHUD::HitIndicationType::COUNTER_HIT);
                        if (oApplyDamageAndReactions && pDefender) {
                            oApplyDamageAndReactions(in_ECX, edxUnused, param_1, pDefender, param_3, param_4, param_5, param_6, param_7, param_8);
                        }
                        OnHitOrBlockEvent(curPlr, defPlr, false, g_CurrentFrame, param_8);
                        if (oAbortAttackerThread_OnHit && pDefender) {
                            oAbortAttackerThread_OnHit(pDefender, nullptr, nullptr);
                        }
                        if (atk.moveClass == MOVE_CLASS_NORMAL_WEAPON) {
                            SanitizeFighterWeaponState(pDefender);
                        }
                        g_P1_AttackStartFrame = 0;
                        g_P2_AttackStartFrame = 0;
                        g_P1_LastScriptFunc = -1;
                        g_P2_LastScriptFunc = -1;
                        g_LastHitFrame = g_CurrentFrame;
                        g_LastHitAttacker = curPlr;
                        return;
                    } else {
                        LogTrace("====================================================================");
                        LogTrace(" >>> [STARTUP INTERRUPT – earlier attack wins] P%d (start %llu) cleanly beat P%d (start %llu, +%lldf delta)! <<<",
                                 defPlr, defStart, curPlr, curStart, (int64_t)curStart - (int64_t)defStart);
                        LogTrace("====================================================================");
                        StunModifier::OnHitEvaluation(defPlr, curPlr, pAttacker, pDefender, false, g_DeferredHit.rxID, g_DeferredHit.damage, nullptr);
                        HitIndicationHUD::TriggerHitIndication(defPlr, HitIndicationHUD::HitIndicationType::COUNTER_HIT);
                        if (oApplyDamageAndReactions && g_DeferredHit.pDefender) {
                            int* defParam1 = (g_DeferredHit.param_1 && (uintptr_t)g_DeferredHit.param_1 > 0x01000000) ? 
                                             g_DeferredHit.param_1 : param_1;
                            void* defECX = (g_DeferredHit.in_ECX && (uintptr_t)g_DeferredHit.in_ECX > 0x01000000) ? 
                                           g_DeferredHit.in_ECX : in_ECX;
                            oApplyDamageAndReactions(defECX, nullptr, defParam1, g_DeferredHit.pDefender, g_DeferredHit.param_3,
                                                     g_DeferredHit.rxID, (uint8_t*)&g_DeferredHit.param_5_storage,
                                                     g_DeferredHit.param_6, g_DeferredHit.damage, g_DeferredHit.param_8);
                        }
                        OnHitOrBlockEvent(defPlr, curPlr, false, g_CurrentFrame, g_DeferredHit.param_8);
                        if (oAbortAttackerThread_OnHit && pAttacker) {
                            oAbortAttackerThread_OnHit(pAttacker, nullptr, nullptr);
                        }
                        if (g_DeferredHit.moveClass == MOVE_CLASS_NORMAL_WEAPON) {
                            SanitizeFighterWeaponState(pAttacker);
                        }
                        g_P1_AttackStartFrame = 0;
                        g_P2_AttackStartFrame = 0;
                        g_P1_LastScriptFunc = -1;
                        g_P2_LastScriptFunc = -1;
                        g_LastHitFrame = g_CurrentFrame;
                        g_LastHitAttacker = defPlr;
                        return;
                    }
                }
            }

            // Mutual Trades
            g_InCase1Resolution = true;
            g_TradeEventCount++;
            HitIndicationHUD::TriggerHitIndication(1, HitIndicationHUD::HitIndicationType::TRADE_HIT);
            HitIndicationHUD::TriggerHitIndication(2, HitIndicationHUD::HitIndicationType::TRADE_HIT);
            if (StunModifier::g_TradeCameraMode != 0) {
                FreeCamera::TriggerCinematicZoom(StunModifier::g_TradeZoomAmount, StunModifier::g_TradeZoomHoldFrames, 1, 2, StunModifier::g_TradeCameraMode, StunModifier::g_TradeCamShiftY, StunModifier::g_TradeCamYawOffset);
            }
            if (StunModifier::g_EnableTradeMeterReward && StunModifier::g_TradeMeterReward > 0.0001f) {
                bool isSpecialTrade = (atk.moveClass == MOVE_CLASS_SPECIAL || atk.moveClass == MOVE_CLASS_EX_SPECIAL || atk.moveClass == MOVE_CLASS_XRAY ||
                                       g_DeferredHit.moveClass == MOVE_CLASS_SPECIAL || g_DeferredHit.moveClass == MOVE_CLASS_EX_SPECIAL || g_DeferredHit.moveClass == MOVE_CLASS_XRAY || g_DeferredHit.isSpecial);
                if (MeterDrainFix::CheckMoveFilterMatch(StunModifier::g_TradeMeterMoveFilter, isSpecialTrade)) {
                    MeterDrainFix::AwardPlayerMeter(1, StunModifier::g_TradeMeterReward);
                    MeterDrainFix::AwardPlayerMeter(2, StunModifier::g_TradeMeterReward);
                    LogTrace(">>> [NATURAL TRADE] Meter Reward: +%.3f (%.1f%%) awarded to both characters! <<<",
                             StunModifier::g_TradeMeterReward, StunModifier::g_TradeMeterReward * 100.0f);
                }
            }

            int curAttPlr = attPlr;
            int defAttPlr = (g_DeferredHit.pAttacker == GetP2Char()) ? 2 : 1;

            float curDmg = param_7;
            int   curRx  = param_4;
            float defDmg = g_DeferredHit.damage;
            int   defRx  = g_DeferredHit.rxID;

            LogTrace(">>> [NATURAL TRADE RESOLVED #%d] Frame %llu <<<", g_TradeEventCount, g_CurrentFrame);
            LogTrace("    P%d authentic hit on P%d: %.1f%% (RxID: %d)", curAttPlr, defAttPlr, curDmg * 100.0f, curRx);
            LogTrace("    P%d authentic hit on P%d: %.1f%% (RxID: %d)", defAttPlr, curAttPlr, defDmg * 100.0f, defRx);

            uint64_t tradeElapsed = 0;
            const char* tradeDetail = nullptr;
            const char* tradeClass = ClassifyHitOutcome(curAttPlr, defAttPlr, true, &tradeElapsed, &tradeDetail);
            LogTrace("====================================================================");
            LogTrace(" [MOVE EVALUATION] >>> %s <<<", tradeClass);
            LogTrace("   Frame:       %llu", g_CurrentFrame);
            LogTrace("   Detail:      %s", tradeDetail);
            if (g_ActiveMacroDesc && g_ActiveMacroDesc[0] != '\0' && (g_CurrentFrame <= g_ActiveMacroTriggerFrame + 60)) {
                LogTrace("   Macro:       %s", g_ActiveMacroDesc);
            }
            LogTrace("====================================================================");

            if (oApplyDamageAndReactions && pDefender) {
                oApplyDamageAndReactions(in_ECX, edxUnused, param_1, pDefender, param_3, curRx, param_5, param_6, curDmg, param_8);
            }

            if (oApplyDamageAndReactions && g_DeferredHit.pDefender) {
                int* defParam1 = (g_DeferredHit.param_1 && (uintptr_t)g_DeferredHit.param_1 > 0x01000000) ? 
                                 g_DeferredHit.param_1 : param_1;
                void* defECX = (g_DeferredHit.in_ECX && (uintptr_t)g_DeferredHit.in_ECX > 0x01000000) ? 
                               g_DeferredHit.in_ECX : in_ECX;
                oApplyDamageAndReactions(
                    defECX,
                    nullptr,
                    defParam1,
                    g_DeferredHit.pDefender,
                    g_DeferredHit.param_3,
                    defRx,
                    (uint8_t*)&g_DeferredHit.param_5_storage,
                    g_DeferredHit.param_6,
                    defDmg,
                    g_DeferredHit.param_8
                );
            }

            if (atk.moveClass == MOVE_CLASS_NORMAL_WEAPON) {
                SanitizeFighterWeaponState(pDefender);
                SanitizeFighterWeaponState(pAttacker);
            }

            if (oAbortAttackerThread_OnHit) {
                if (pDefender) oAbortAttackerThread_OnHit(pDefender, nullptr, nullptr);
                if (pAttacker) oAbortAttackerThread_OnHit(pAttacker, nullptr, nullptr);
            }

            g_P1_AttackStartFrame = 0;
            g_P2_AttackStartFrame = 0;
            g_P1_LastScriptFunc = -1;
            g_P2_LastScriptFunc = -1;

            g_DeferredHit.isPending = false;
            g_LastHitFrame = g_CurrentFrame;
            g_LastHitAttacker = attPlr;
            g_InCase1Resolution = false;
            return;
        }

        // ============================================================================
        // CASE 2: CLASH DEFERRAL GATE
        // ============================================================================
        bool isAttExcluded = (atk.moveClass == MOVE_CLASS_XRAY) || (MovePriority::g_InstantProjectiles && atk.isProjectile);
        bool isDefInHitWindow = !isAttExcluded && IsDefenderInTradeWindow(pDefender);
        bool shouldDefer = g_FixEnabled && !g_DeferredHit.isPending && isDefInHitWindow;

        if (shouldDefer) {
            g_DeferredHit.isPending     = true;
            g_DeferredHit.frameCaptured = g_CurrentFrame;
            g_DeferredHit.in_ECX        = in_ECX;
            if (param_1 && (uintptr_t)param_1 > 0x01000000) {
                __try {
                    memcpy(g_DeferredHit.param_1_storage, param_1, sizeof(g_DeferredHit.param_1_storage));
                    g_DeferredHit.param_1 = (int*)g_DeferredHit.param_1_storage;
                } __except (EXCEPTION_EXECUTE_HANDLER) {
                    g_DeferredHit.param_1 = param_1;
                }
            } else {
                g_DeferredHit.param_1 = param_1;
            }

            g_DeferredHit.pDefender     = pDefender;
            g_DeferredHit.pAttacker     = pAttacker;
            g_DeferredHit.param_3       = param_3;
            g_DeferredHit.rxID          = param_4;

            if (param_5 && (uintptr_t)param_5 > 0x01000000) {
                __try { g_DeferredHit.param_5_storage = *(uint32_t*)param_5; }
                __except (EXCEPTION_EXECUTE_HANDLER) { g_DeferredHit.param_5_storage = 0; }
            } else {
                g_DeferredHit.param_5_storage = 0;
            }
            g_DeferredHit.param_5       = (uint8_t*)&g_DeferredHit.param_5_storage;
            g_DeferredHit.param_6       = param_6;
            g_DeferredHit.damage        = param_7;
            g_DeferredHit.param_8       = param_8;

            g_DeferredHit.isUppercut    = atk.isUppercut;
            g_DeferredHit.isJumpIn      = atk.isJumpIn;
            g_DeferredHit.isSpecial     = (atk.moveClass == MOVE_CLASS_SPECIAL || atk.moveClass == MOVE_CLASS_EX_SPECIAL);
            g_DeferredHit.isProjectile  = atk.isProjectile;
            g_DeferredHit.isXRay        = (atk.moveClass == MOVE_CLASS_XRAY);
            g_DeferredHit.moveClass     = atk.moveClass;

            LogTrace("[CLASH HOLD] Deferred P%d hit on P%d (Damage: %.1f%%, RxID: %d). Awaiting return hit...",
                     attPlr, defPlr, param_7 * 100.0f, param_4);
            return;
        }

        // ============================================================================
        // CASE 3: STANDARD HIT (Clean Punishes, Delays, & Interrupted Startups)
        // ============================================================================
        g_LastHitFrame = g_CurrentFrame;
        g_LastHitAttacker = attPlr;
        
        g_DeferredHit.isPending = false;

        bool isBlocked = (param_6 != 0 || param_4 == 0x00 || param_4 == 0x01);

        // Stun modifier evaluated BEFORE native damage / reaction alters character state
        StunModifier::OnHitEvaluation(attPlr, defPlr, pDefender, pAttacker, isBlocked, param_4, param_7, &atk);

        if (oApplyDamageAndReactions) {
            oApplyDamageAndReactions(in_ECX, edxUnused, param_1, pDefender, param_3, param_4, param_5, param_6, param_7, param_8);
        }

        // Synchronize native reaction timer (+0x2F0) with added stun frames and apply block pushback
        StunModifier::OnPostDamageEvaluation(defPlr, pDefender, isBlocked, attPlr, pAttacker);

        // Combat Meter Gains
        bool isSpecial = (atk.moveClass == MOVE_CLASS_SPECIAL || atk.moveClass == MOVE_CLASS_EX_SPECIAL || atk.moveClass == MOVE_CLASS_XRAY);
        if (!isBlocked) {
            if (MeterDrainFix::g_EnableAttackerHitMeterGain) {
                float r = isSpecial ? MeterDrainFix::g_AttackerHitMeterRewardSpecials : MeterDrainFix::g_AttackerHitMeterRewardNormals;
                if (r > 0.0001f) {
                    MeterDrainFix::AwardPlayerMeter(attPlr, r);
                }
            }
            if (MeterDrainFix::g_EnableDefenderHitMeterGain) {
                float r = isSpecial ? MeterDrainFix::g_DefenderHitMeterRewardSpecials : MeterDrainFix::g_DefenderHitMeterRewardNormals;
                if (r > 0.0001f) {
                    MeterDrainFix::AwardPlayerMeter(defPlr, r);
                }
            }
        } else {
            if (MeterDrainFix::g_EnableDefenderBlockMeterGain) {
                float r = isSpecial ? MeterDrainFix::g_DefenderBlockMeterRewardSpecials : MeterDrainFix::g_DefenderBlockMeterRewardNormals;
                if (r > 0.0001f) {
                    MeterDrainFix::AwardPlayerMeter(defPlr, r);
                }
            }
        }

        if (atk.moveClass == MOVE_CLASS_NORMAL_LIMB || atk.moveClass == MOVE_CLASS_NORMAL_WEAPON) {
            MeterDrainFix::NotifyNormalAttack(attPlr);
        }

        // Notify Frame Data Subsystem and Toasty Training of authentic hit / block event
        {
            OnHitOrBlockEvent(attPlr, defPlr, isBlocked, g_CurrentFrame, param_8);
            ToastyTraining::OnAttackHit(attPlr, defPlr, atk.isUppercut, isBlocked, param_7, param_4);
            ReversalEngine::Get().NotifyIncomingHit(defPlr, attPlr, isBlocked, param_4, atk, param_7);
            if (isBlocked && pDefender) {
                MeterDrainFix::OnBlockOrStunCheck(pDefender, g_CurrentFrame, true);
            }
        }

        uint64_t defMoveElapsed = 0;
        const char* hitDetail = nullptr;
        const char* hitClass = nullptr;
        if (isBlocked) {
            hitClass = "BLOCKED ATTACK";
            hitDetail = "Defender successfully blocked attacker's move";
        } else {
            hitClass = ClassifyHitOutcome(attPlr, defPlr, false, &defMoveElapsed, &hitDetail);
        }

        LogTrace("====================================================================");
        LogTrace(" [MOVE EVALUATION] >>> %s <<<", hitClass);
        LogTrace("   Attacker:    P%d | Defender: P%d", attPlr, defPlr);
        LogTrace("   Move Timing: Frame +%llu into Defender's action", defMoveElapsed);
        LogTrace("   Detail:      %s", hitDetail);
        if (g_ActiveMacroDesc && g_ActiveMacroDesc[0] != '\0' && (g_CurrentFrame <= g_ActiveMacroTriggerFrame + 60)) {
            LogTrace("   Macro:       %s", g_ActiveMacroDesc);
        }
        LogTrace("====================================================================");

        if (defPlr == 1) {
            g_P1_AttackStartFrame = 0;
            g_P1_LastScriptFunc = -1;
        } else {
            g_P2_AttackStartFrame = 0;
            g_P2_LastScriptFunc = -1;
        }

        if (!atk.isExcludedFromTrades && atk.moveClass != MOVE_CLASS_SPECIAL && atk.moveClass != MOVE_CLASS_EX_SPECIAL && atk.moveClass != MOVE_CLASS_XRAY) {
            if (pDefender && (uintptr_t)pDefender > 0x01000000) {
                const char* defStateReason = nullptr;
                CharacterCombatState defState = GetCharacterCombatState(pDefender, &defStateReason);
                // Only sanitize defender if they were actually interrupted during an attack!
                if (defState == STATE_NORMAL_STARTUP || defState == STATE_NORMAL_ACTIVE || defState == STATE_NORMAL_RECOVERY) {
                    void* pProp = *(void**)((uintptr_t)pDefender + 0x2F1C);
                    if (pProp != nullptr) {
                        SanitizeFighterWeaponState(pDefender);
                    }
                }
            }
        }
    }

    void __fastcall hkApplyDamageAndReactions(
        void* in_ECX,
        void* edxUnused,
        int* param_1,
        void* param_2,
        uint32_t param_3,
        int param_4,
        uint8_t* param_5,
        int param_6,
        float param_7,
        int param_8
    ) {
        uint64_t t0 = ModProfiler::StartTimer();
        hkApplyDamageAndReactions_Impl(in_ECX, edxUnused, param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8);
        double totalUs = ModProfiler::CalculateElapsedUs(t0);
        // Inclusive callback time includes synchronous native engine work. Preserve slow samples.
        ModProfiler::RecordSample(PROF_TRADE_ENGINE, totalUs);
    }

    bool InstallTradeEngineHooks() {
        if (s_TradeHooksReady) return true;
        const HookSpec hooks[] = {
            { (void*)0x008AB040, (void*)hkAbortAttackerThread_OnHit, (void**)&oAbortAttackerThread_OnHit },
            { (void*)0x008A5490, (void*)hkApplyDamageAndReactions, (void**)&oApplyDamageAndReactions }
        };
        if (!InstallHookPair(hooks, MH_CreateHook, MH_EnableHook, MH_DisableHook, MH_RemoveHook,
                [](const char* stage, void* target, MH_STATUS status) {
                    LogTrace("[!] Trade hook %s failed at %p (code=%d)", stage, target, status);
                })) return false;
        s_TradeHooksReady = true;
        if(MH_CreateHook((void*)0x00433CF0,(void*)hkCleanupChildren,(void**)&oCleanupChildren)==MH_OK)
            g_JadeStaffRecoveryReady=MH_EnableHook((void*)0x00433CF0)==MH_OK;
        LogTrace("[JADE] Persistent staff recovery: %s",g_JadeStaffRecoveryReady?"READY":"FAILED");
        LogTrace("[+] Both trade engine hooks installed and enabled.");
        return true;
    }
}



