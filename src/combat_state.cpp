#include "combat_state.hpp"
#include "frame_data.hpp"
#include "stun_modifier.hpp"
#include "extra_gameplay.hpp"
#include "attack_frame_modifier.hpp"
#include "meter_drain_fix.hpp"
#include "minhook/include/MinHook.h"
#include <cmath>

namespace MK9TradeFix {

    // Forward declarations from simulation
    extern uint64_t    g_ActiveMacroTriggerFrame;
    extern int         g_ActiveMacroP1Delay;
    extern int         g_ActiveMacroP2Delay;
    extern const char* g_ActiveMacroDesc;

    CachedAttackState g_PlayerAttackCache[3];

    typedef int(__cdecl* tGetAttackDamageAndRxID)(void* pAttackState, uint32_t* pOutFlags, float* pOutDamage, int* pOutRxID);
    static tGetAttackDamageAndRxID GetAttackDamageAndRxID = (tGetAttackDamageAndRxID)0x00492CF0;

    bool QueryCharacterAttackInfo(void* pChar, float* pOutDamage, int* pOutRxID) {
        if (!pChar || (uintptr_t)pChar < 0x01000000) return false;

        __try {
            // Guard 1: Character must not be receiving damage (Hitstun, Knockdown, Cinematic)
            uint32_t actionState = *(uint32_t*)((uintptr_t)pChar + 0x1D4);
            if (actionState == 0x4600 || actionState == 0x4201 || actionState == 0x4203 || 
                (actionState >= 0x6200 && actionState <= 0x620B)) {
                return false;
            }

            // Guard 2: Character must have an active attack script bytecode thread
            uintptr_t scriptPkg  = *(uintptr_t*)((uintptr_t)pChar + 0x2F84);
            int       scriptFunc = *(int*)((uintptr_t)pChar + 0x2F88);
            if (scriptPkg == 0 || scriptFunc == -1) {
                return false;
            }

            // Priority 1: Native Character Move Registers (Authored for active move)
            float regDmg = *(float*)((uintptr_t)pChar + 0x79BC);
            int   regRx  = *(int*)((uintptr_t)pChar + 0x79B0); // Ground Reaction ID
            if (regDmg > 0.001f && regDmg <= 0.60f && regRx > 0 && regRx < 6000) {
                if (pOutDamage) *pOutDamage = regDmg;
                if (pOutRxID)   *pOutRxID   = regRx;
                return true;
            }

            // Priority 2: Active Combat Hit Context Descriptor (0x7A38)
            uintptr_t pActionDef = *(uintptr_t*)((uintptr_t)pChar + 0x7A38);
            if (pActionDef > 0x01000000 && pActionDef != (uintptr_t)-1) {
                float dmg = *(float*)(pActionDef + 0x24);
                int   rx  = *(int*)(pActionDef + 0x28);
                if (dmg > 0.001f && dmg <= 0.60f && rx > 0 && rx < 6000) {
                    if (pOutDamage) *pOutDamage = dmg;
                    if (pOutRxID)   *pOutRxID   = rx;
                    return true;
                }
            }

            // Priority 3: Active Action Descriptor in Combat Controller (0x7638 -> 0x30)
            void* pCombat = *(void**)((uintptr_t)pChar + 0x7638);
            if (pCombat && (uintptr_t)pCombat > 0x01000000) {
                uintptr_t pActiveDef = *(uintptr_t*)((uintptr_t)pCombat + 0x30);
                if (pActiveDef > 0x01000000 && pActiveDef != (uintptr_t)-1) {
                    float dmg = *(float*)(pActiveDef + 0x24);
                    int   rx  = *(int*)(pActiveDef + 0x28);
                    if (dmg > 0.001f && dmg <= 0.60f && rx > 0 && rx < 6000) {
                        if (pOutDamage) *pOutDamage = dmg;
                        if (pOutRxID)   *pOutRxID   = rx;
                        return true;
                    }
                }
            }

            // Priority 4: Engine Move Definition Resolver (0x00492CF0)
            void* pAttackState = (void*)((uintptr_t)pChar + 0x78F0);
            if (pAttackState && (uintptr_t)pAttackState > 0x01000000) {
                uint32_t outFlags = 0;
                float outDmg = 0.0f;
                int outRx = 0;
                if (GetAttackDamageAndRxID && GetAttackDamageAndRxID(pAttackState, &outFlags, &outDmg, &outRx)) {
                    if (outDmg > 0.001f && outDmg <= 0.60f && outRx > 0 && outRx < 6000) {
                        if (pOutDamage) *pOutDamage = outDmg;
                        if (pOutRxID)   *pOutRxID   = outRx;
                        return true;
                    }
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}

        return false;
    }

    void CacheActiveFighterState(void* pChar, int playerIdx) {
        if (!pChar || (uintptr_t)pChar < 0x01000000) return;

        __try {
            uintptr_t scriptPkg  = *(uintptr_t*)((uintptr_t)pChar + 0x2F84);
            int       scriptFunc = *(int*)((uintptr_t)pChar + 0x2F88);
            void*     pProp      = *(void**)((uintptr_t)pChar + 0x2F1C);

            // Track if character is running an attack thread or has a prop unholstered
            if (scriptPkg != 0 || pProp != nullptr || scriptFunc != -1) {
                float liveDmg = 0.0f;
                int   liveRx  = 0;
                if (QueryCharacterAttackInfo(pChar, &liveDmg, &liveRx)) {
                    g_PlayerAttackCache[playerIdx].frameCaptured = g_CurrentFrame;
                    g_PlayerAttackCache[playerIdx].scriptFunc    = scriptFunc;
                    g_PlayerAttackCache[playerIdx].damage        = liveDmg;
                    g_PlayerAttackCache[playerIdx].rxID          = liveRx;
                    g_PlayerAttackCache[playerIdx].isValid       = true;
                    return;
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}

        // Retain cache across clash window (invalidate after 45 frames)
        if (g_CurrentFrame > g_PlayerAttackCache[playerIdx].frameCaptured + 45) {
            g_PlayerAttackCache[playerIdx].isValid = false;
        }
    }

    bool IsPredictiveSimultaneousTrade() {
        if (g_P1_AttackStartFrame == 0 || g_P2_AttackStartFrame == 0) return false;
        int64_t diff = (int64_t)g_P1_AttackStartFrame - (int64_t)g_P2_AttackStartFrame;
        if (diff < 0) diff = -diff;
        // Strict 0-frame delta: Only exact simultaneous attacks trade!
        return (diff == 0);
    }

    bool IsCharacterInActiveAttack(void* pChar) {
        if (!pChar || (uintptr_t)pChar < 0x01000000) return false;

        __try {
            uint32_t actionState = *(uint32_t*)((uintptr_t)pChar + 0x1D4);
            if (actionState == 0x4600 || actionState == 0x4201 || actionState == 0x4203 || 
                (actionState >= 0x6200 && actionState <= 0x620B)) {
                return false;
            }

            uintptr_t scriptPkg  = *(uintptr_t*)((uintptr_t)pChar + 0x2F84);
            int       scriptFunc = *(int*)((uintptr_t)pChar + 0x2F88);

            if (scriptPkg != 0 && scriptFunc != -1) {
                int plr = (pChar == GetP2Char()) ? 2 : 1;
                uint64_t startFrame = (plr == 2) ? g_P2_AttackStartFrame : g_P1_AttackStartFrame;

                if (startFrame != 0 && (g_CurrentFrame > startFrame + 32)) {
                    return false; // In Recovery: vulnerable to punish, NOT active to trade!
                }

                return true;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }

        return false;
    }

    bool IsCharacterHitboxActive(void* pChar) {
        return IsCharacterInActiveAttack(pChar);
    }

    bool IsCharacterInActualActiveFrames(void* pChar) {
        if (!pChar || (uintptr_t)pChar < 0x01000000) return false;

        __try {
            uintptr_t scriptPkg  = *(uintptr_t*)((uintptr_t)pChar + 0x2F84);
            int       scriptFunc = *(int*)((uintptr_t)pChar + 0x2F88);

            if (scriptPkg != 0 && scriptFunc != -1) {
                uint32_t activeHitboxes = *(uint32_t*)((uintptr_t)pChar + 0x7A3C);
                if (activeHitboxes > 0) {
                    return true;
                }

                int plr = (pChar == GetP2Char()) ? 2 : 1;
                uint64_t startFrame = (plr == 2) ? g_P2_AttackStartFrame : g_P1_AttackStartFrame;

                if (startFrame != 0) {
                    uint64_t elapsed = g_CurrentFrame - startFrame;
                    if (elapsed < 3) {
                        return false;
                    }
                    if (elapsed > 36) {
                        return false;
                    }
                }
                return true;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
        return false;
    }

    const char* GetAttackPropertyTaxonomyName(uint32_t atkType) {
        switch (atkType) {
            case 0:  return "dist_touching (Close Body/Limb Strike)";
            case 1:  return "dist_sweep (Sweep-Distance Body Strike)";
            case 2:  return "dist_jump (Jump-Distance Lunging Strike)";
            case 3:  return "medium_attack (Mid Strike)";
            case 4:  return "low_attack (Low Strike)";
            case 5:  return "threeD_attack (3D Weapon Normal String)";
            case 6:  return "escape (Evasive Roll/Dash)";
            case 7:  return "taunt_attack (Taunt)";
            case 8:  return "charge_attack (Charging Special Strike)";
            case 9:  return "knock_down_attack (Hard Knockdown Strike)";
            case 10: return "pop_up_attack (Launcher / Juggle)";
            case 11: return "throw_attack (Command Grab / Cinematic Trigger)";
            case 12: return "anti_air_attack (Anti-Air Strike)";
            case 13: return "test (Engine Test Slot)";
            case 14: return "dizzy_attack (Stun / Freeze / Telekinesis Hold)";
            case 15: return "fatality_attack (Fatality Finisher)";
            case 16: return "anti_jump_away_attack (Anti-Escape Pursuit)";
            case 17: return "jumping_attack (Aerial Normal Strike)";
            case 18: return "parry_table (Counter / Parry Stance)";
            case 19: return "dist_fireball (Ranged Projectile Attack)";
            case 20: return "anti_laydown (Ground OTG Attack)";
            case 21: return "boss_special_attacks (Boss Special Attack)";
            case 22: return "tag_team_special_attack (Tag-In Special Attack)";
            case 23: return "tag_team_assist_attack (Tag Partner Assist)";
            case 24: return "ballup_counter_attack (Guard-Break Counter)";
            case 25: return "dist_jump_counter_attack (Anti-Jump Counter)";
            case 26: return "reflect_projectile (Projectile Reflection)";
            case 27: return "wakeup_attack (Invincible Wakeup Special)";
            case 28: return "xray_attack (Cinematic X-Ray Super Move)";
            case 29: return "sweep_dist_counter_attack (Anti-Sweep Counter)";
            case 30: return "armor_attack (EX Super Armor Move)";
            case 31: return "quick_counter_attack (Frame-Trap Counter)";
            default: return "generic_strike";
        }
    }

    struct PlayerAnimRecord {
        uint32_t activeAttackID = 0;           // Unique FName index of active attack
        char     activeAttackName[64] = { 0 }; // Name of active attack
        uint64_t attackStartFrame = 0;         // Frame when attack was started

        uint32_t currentAnimID = 0;            // Most recent anim of any type
        char     currentAnimName[64] = { 0 };
        uint64_t currentAnimFrame = 0;
    };
    static PlayerAnimRecord s_PlayerAnim[3]; // 1 = P1, 2 = P2

    bool ClassifySpecialOrSuper(
        void* pFighter, 
        void* pOpponent, 
        int* param_1, 
        uint8_t* param_5, 
        float damage, 
        MoveClass* outClass, 
        const char** outReason,
        uint32_t* outAtkType
    ) {
        if (!pFighter || (uintptr_t)pFighter < 0x01000000) return false;

        __try {
            uint32_t actionState1D4 = *(uint32_t*)((uintptr_t)pFighter + 0x1D4);
            uint32_t prevAction2F9C = *(uint32_t*)((uintptr_t)pFighter + 0x2F9C);
            uint32_t flags2EC       = *(uint32_t*)((uintptr_t)pFighter + 0x2EC);
            uint32_t stanceFlags2E8 = *(uint32_t*)((uintptr_t)pFighter + 0x2E8);

            uint32_t oppAction1D4 = 0;
            if (pOpponent && (uintptr_t)pOpponent > 0x01000000) {
                oppAction1D4 = *(uint32_t*)((uintptr_t)pOpponent + 0x1D4);
            }

            uint32_t moveCategory = 0xFFFFFFFF;
            uint32_t recRxID      = 0;
            uint32_t recAtkType   = 0;
            bool     hasRecord    = false;
            void* pPlayerInfo = *(void**)((uintptr_t)pFighter + 0x7638);
            if (!pPlayerInfo || (uintptr_t)pPlayerInfo < 0x00400000) {
                pPlayerInfo = (pFighter == GetP2Char()) ? (void*)0x00e8aa08 : (void*)0x00e89d7c;
            }
            if (pPlayerInfo && (uintptr_t)pPlayerInfo >= 0x00400000 && (uintptr_t)pPlayerInfo < 0x7FFF0000) {
                uint32_t curr = *(uint32_t*)((uintptr_t)pPlayerInfo + 0x574);
                if (curr == 0) curr = *(uint32_t*)((uintptr_t)pPlayerInfo + 0x570);
                if (curr > 0) {
                    uint32_t idx = curr - 1;
                    uintptr_t pRec = (uintptr_t)pPlayerInfo + 0x48 + idx * 0x2C;
                    moveCategory = *(uint32_t*)(pRec + 0x08);
                    recRxID      = *(uint32_t*)(pRec + 0x18);
                    recAtkType   = *(uint32_t*)(pRec + 0x20);
                    hasRecord    = true;
                }
            }
            if (!hasRecord) {
                if (param_1 && (uintptr_t)param_1 >= 0x00400000 && (uintptr_t)param_1 < 0x7FFF0000) {
                    uint32_t p1_prop = *(uint32_t*)((uintptr_t)param_1 + 0x5C);
                    if (p1_prop <= 31) {
                        recAtkType = p1_prop;
                    }
                }
            }
            if (outAtkType) *outAtkType = recAtkType;

            bool isEngineSpecialBit = (flags2EC & 0x200) != 0;
            bool isEngineNormalBit  = (flags2EC & 0x400) != 0;
            bool hasSuperArmor      = (flags2EC & 0x800000) != 0;

            int actionID = *(int*)((uintptr_t)pFighter + 0x2E0);

            uint32_t moveType = 0;
            uintptr_t pActionDef = *(uintptr_t*)((uintptr_t)pFighter + 0x7A38);
            if (pActionDef >= 0x00400000 && pActionDef != (uintptr_t)-1 && pActionDef < 0x7FFF0000) {
                moveType = *(uint32_t*)(pActionDef + 0x44);
            }

            uint32_t moveDescType = 0;
            uintptr_t pMoveInfo = *(uintptr_t*)((uintptr_t)pFighter + 0x75F0);
            if (pMoveInfo >= 0x00400000 && pMoveInfo != (uintptr_t)-1 && pMoveInfo < 0x7FFF0000) {
                moveDescType = *(uint32_t*)(pMoveInfo + 0x14);
            }

            // PRIORITY 1: X-RAY SUPER MOVE IDENTIFICATION
            bool isXRayState = (actionState1D4 == 0x4203) || 
                               (oppAction1D4 == 0x4203) ||
                               (actionState1D4 == 0x5208) ||
                               (oppAction1D4 == 0x5208) ||
                               (prevAction2F9C == 0x5208) ||
                               (moveCategory == 5 && recAtkType == 11 && hasSuperArmor) ||
                               (prevAction2F9C == 0x5208 && hasSuperArmor) ||
                               (recAtkType == 28) ||
                               (moveType >= 3) ||
                               (moveDescType >= 3) ||
                               (actionID == 0x1C) ||
                               (damage >= 0.25f);

            if (isXRayState) {
                if (outClass)  *outClass  = MOVE_CLASS_XRAY;
                if (outReason) {
                    if (actionState1D4 == 0x4203 || oppAction1D4 == 0x4203) {
                        *outReason = "X-Ray Super Move (Engine Cinematic Action State 0x4203 Active)";
                    } else if (actionState1D4 == 0x5208 || oppAction1D4 == 0x5208 || prevAction2F9C == 0x5208) {
                        *outReason = "X-Ray Super Move (Cinematic Action State 0x5208 Active)";
                    } else if (moveCategory == 5 && recAtkType == 11 && hasSuperArmor) {
                        *outReason = "X-Ray Super Move (Move Category 5 + Throw Trigger + Super Armor Active)";
                    } else if (recAtkType == 28) {
                        *outReason = "X-Ray Super Move (Native Property 28: xray_attack Active)";
                    } else if (moveType >= 3 || moveDescType >= 3) {
                        *outReason = "X-Ray Super Move (Native Move Taxonomy Type 3 Active)";
                    } else if (damage >= 0.25f) {
                        *outReason = "X-Ray Super Move (Cinematic Bone Breaker Damage >= 25%)";
                    } else {
                        *outReason = "X-Ray Super Move (Move Taxonomy 0x1C Active)";
                    }
                }
                return true;
            }

            // PRIORITY 2: EX ENHANCED SPECIAL MOVE IDENTIFICATION
            int fighterPlr = (pFighter == GetP2Char()) ? 2 : 1;
            bool isEXTrackerActive = MeterDrainFix::IsEXMoveActive(fighterPlr);
            bool isD2 = (actionID == 0x20) || IsFighterExecutingUppercut(pFighter);

            bool isEX = (isEXTrackerActive && !isD2) || (moveCategory == 5) || (hasSuperArmor && !isEngineNormalBit) || (actionID == 0x1E) || (actionID == 0x1F) || (moveType == 2);
            if (isEX) {
                if (outClass)  *outClass  = MOVE_CLASS_EX_SPECIAL;
                if (outReason) {
                    if (isEXTrackerActive && !isD2) {
                        *outReason = "EX Enhanced Special Move (Active EX Move Execution)";
                    } else if (hasSuperArmor) {
                        *outReason = "EX Enhanced Special Move (Native Super Armor +0x2EC:0x800000 Active)";
                    } else {
                        *outReason = "EX Enhanced Special Move (Move Category 5 Active)";
                    }
                }
                return true;
            }

            // PRIORITY 3: SPECIAL MOVE IDENTIFICATION
            bool isSpec = (moveCategory == 1) || (isEngineSpecialBit && !isEngineNormalBit) || (moveType == 1) ||
                          (actionID >= 0x01 && actionID <= 0x1B && !isEngineNormalBit);
            if (isSpec) {
                if (outClass)  *outClass  = MOVE_CLASS_SPECIAL;
                if (outReason) {
                    if (moveCategory == 1) {
                        *outReason = "Special Move (Active Move Category 1 Active)";
                    } else if (isEngineSpecialBit) {
                        *outReason = "Special Move (Native Special Flag +0x2EC:0x200 Active)";
                    } else {
                        *outReason = "Special Move (Native Move Taxonomy Type 1 Active)";
                    }
                }
                return true;
            }

        } __except (EXCEPTION_EXECUTE_HANDLER) {}

        return false;
    }

    AttackClassification AnalyzeAttack(
        void* pAttacker, 
        void* pDefender, 
        int* param_1, 
        uint8_t* param_5, 
        float damage, 
        int param_8
    ) {
        AttackClassification result = {
            "Normal Attack (Unarmed Limb)",
            MOVE_CLASS_NORMAL_LIMB,
            false,
            false,
            true,
            false,
            "Normal Limb Strike",
            false,
            (void*)param_8,
            0,
            "dist_touching (Close Body/Limb Strike)",
            false,
            false
        };
        if (!pAttacker) return result;

        __try {
            uint32_t nativeAtkType = 0;
            MoveClass moveCls = MOVE_CLASS_NORMAL_LIMB;
            const char* reason = nullptr;
            if (ClassifySpecialOrSuper(pAttacker, pDefender, param_1, param_5, damage, &moveCls, &reason, &nativeAtkType)) {
                result.moveClass = moveCls;
                result.reason = reason ? reason : "Special / EX / X-Ray Move";
                result.hasHitbox = (param_1 != nullptr);
                result.atkType = nativeAtkType;
                result.propertyName = GetAttackPropertyTaxonomyName(nativeAtkType);
                result.isUppercut = false;
                result.isJumpIn = false;

                result.isProjectile = (nativeAtkType == 19 /* dist_fireball */) || (param_8 != 0);
                bool isProjectileSpecial = result.isProjectile;

                if (moveCls == MOVE_CLASS_XRAY) {
                    result.category = "X-Ray (Cinematic Super Move)";
                    result.isExcludedFromTrades = true;
                } else if (moveCls == MOVE_CLASS_EX_SPECIAL) {
                    result.category = isProjectileSpecial ? 
                        "EX Special Move (Spawned Projectile Entity)" : 
                        "EX Special Move (Physical Bone Strike)";
                    result.isExcludedFromTrades = true;
                } else {
                    result.category = isProjectileSpecial ? 
                        "Special Move (Spawned Projectile Entity)" : 
                        "Special Move (Physical Bone Strike)";
                    result.isExcludedFromTrades = true;
                }
                return result;
            }

            result.atkType = nativeAtkType;
            result.propertyName = GetAttackPropertyTaxonomyName(nativeAtkType);
            result.isProjectile = (nativeAtkType == 19);

            uint32_t v_04 = 1;
            if (param_1 && (uintptr_t)param_1 > 0x01000000) {
                uint32_t* p = (uint32_t*)param_1;
                v_04 = p[1];
            }

            void* pPropList = *(void**)((uintptr_t)pAttacker + 0x2F1C);
            uint32_t stanceFlags = *(uint32_t*)((uintptr_t)pAttacker + 0x2E8);
            bool isWeaponStance = (stanceFlags & 0x200) != 0;
            bool hasWeaponProp = (pPropList != nullptr) || isWeaponStance;

            bool isProp = isWeaponStance || ((v_04 == 0) && hasWeaponProp);
            int attachedCount = *(int*)((uintptr_t)pAttacker + 0x68);
            void* pVolOwner = (param_1 && (uintptr_t)param_1 > 0x01000000) ? *(void**)((uintptr_t)param_1 + 0x04) : nullptr;
            bool isChild = (attachedCount > 0) && (pVolOwner != nullptr && IsAttachedChildActor(pAttacker, pVolOwner));

            result.isProp = isProp;
            result.isChildActor = isChild;
            result.hasHitbox = (param_1 != nullptr);

            if (isProp) {
                result.moveClass = MOVE_CLASS_NORMAL_WEAPON;
                result.isExcludedFromTrades = false;
                result.reason = "Normal Weapon Strike";
                if (isChild) {
                    result.category = "Normal Weapon (Child Actor attached to Parent)";
                } else {
                    result.category = "Normal Weapon (Detached Weapon / Socket Rig)";
                }
            } else {
                result.moveClass = MOVE_CLASS_NORMAL_LIMB;
                result.isExcludedFromTrades = false;
                result.reason = "Normal Limb Strike";
                result.category = "Normal Attack (Unarmed Limb)";
            }

            // D+2 Normal Uppercut Detection via Native SubState & Move Taxonomy
            int actionID = *(int*)((uintptr_t)pAttacker + 0x2E0);
            bool isD2 = (actionID == 0x20) || IsExecutingD2Script(pAttacker) || IsFighterExecutingUppercut(pAttacker);

            bool isUppercut = (result.moveClass == MOVE_CLASS_NORMAL_LIMB || result.moveClass == MOVE_CLASS_NORMAL_WEAPON) && isD2;
            result.isUppercut = isUppercut;
            if (isUppercut) {
                result.category = "Normal Attack (Uppercut Launcher [D+2])";
                result.reason = "Normal Uppercut (Pop-Up Anti-Air Strike)";
            }

            // Jump-In Attack Detection
            float rootZ = *(float*)((uintptr_t)pAttacker + 0x1F0);
            bool isAirborne = (nativeAtkType == 17 /* jumping_attack */) || (rootZ > 15.0f) || ((stanceFlags & 0x01) != 0);
            bool isJumpIn = !isUppercut && (result.moveClass == MOVE_CLASS_NORMAL_LIMB || result.moveClass == MOVE_CLASS_NORMAL_WEAPON) && isAirborne;
            result.isJumpIn = isJumpIn;
            if (isJumpIn) {
                result.category = "Normal Attack (Jump-In Aerial Strike)";
                result.reason = "Jump-In Aerial Strike";
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            result.category = "Normal Attack (Unarmed Limb)";
        }

        return result;
    }

    bool IsFighterExecutingUppercut(void* pChar) {
        if (!pChar || (uintptr_t)pChar < 0x01000000) return false;
        __try {
            uint32_t actionState = *(uint32_t*)((uintptr_t)pChar + 0x1D4);
            if (actionState == 0x4600 || actionState == 0x4201 || actionState == 0x4203 || 
                (actionState >= 0x6200 && actionState <= 0x620B)) {
                return false;
            }

            // STRICT D+2 GUARD: Reject if the engine classifies this as a special move.
            // Special flag (+0x2EC bit 0x200) set WITHOUT normal flag (+0x2EC bit 0x400)
            // means the fighter is executing a special/EX move, NOT a d+2 normal.
            // This prevents false positives on Sektor Telepunch, Cyrax EX Bomb, etc.
            uint32_t flags2EC = *(uint32_t*)((uintptr_t)pChar + 0x2EC);
            bool isEngineSpecialBit = (flags2EC & 0x200) != 0;
            bool isEngineNormalBit  = (flags2EC & 0x400) != 0;
            if (isEngineSpecialBit && !isEngineNormalBit) {
                return false;
            }

            // Also reject if the action ID falls in the special move range (0x01-0x1F)
            int actionID = *(int*)((uintptr_t)pChar + 0x2E0);
            if (actionID >= 0x01 && actionID <= 0x1F) {
                return false;
            }

            // 1. Explicit Action ID (0x20 = Attack2Down)
            if (actionID == 0x20) {
                return true;
            }

            // 2. Active MKScript Function Symbol Check (Active package descriptor matches attack2down and activeRx == descRx)
            if (IsExecutingD2Script(pChar)) {
                return true;
            }

            // 3. Active Animation Name Check
            int playerIdx = (pChar == GetP2Char()) ? 2 : 1;
            const char* curAnim = s_PlayerAnim[playerIdx].currentAnimName;
            const char* atkAnim = s_PlayerAnim[playerIdx].activeAttackName;
            if (IsD2SymbolName(curAnim) || IsD2SymbolName(atkAnim)) {
                return true;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
        return false;
    }

    CharacterCombatState GetCharacterCombatState(void* pChar, const char** outReason) {
        if (!pChar || (uintptr_t)pChar < 0x01000000) {
            if (outReason) *outReason = "Invalid/Null Fighter Pointer";
            return STATE_NEUTRAL;
        }

        __try {
            uint32_t actionState = *(uint32_t*)((uintptr_t)pChar + 0x1D4);
            if (actionState == 0x4600) {
                if (outReason) *outReason = "In Hit Stumble (0x4600)";
                return STATE_HITSTUN;
            }
            if (actionState == 0x4201 || (actionState >= 0x6200 && actionState <= 0x620B)) {
                if (outReason) *outReason = "In Knockdown / Floor Reaction";
                return STATE_KNOCKDOWN;
            }
            if (actionState == 0x4203) {
                if (outReason) *outReason = "In Cinematic X-Ray Sequence (0x4203)";
                return STATE_CINEMATIC_XRAY;
            }

            uintptr_t scriptPkg  = *(uintptr_t*)((uintptr_t)pChar + 0x2F84);
            int       scriptFunc = *(int*)((uintptr_t)pChar + 0x2F88);
            if (scriptPkg == 0 || scriptFunc == -1) {
                if (outReason) *outReason = "Neutral / Idle (No Active Script Thread)";
                return STATE_NEUTRAL;
            }

            MoveClass moveCls = MOVE_CLASS_NORMAL_LIMB;
            const char* specialReason = nullptr;
            uint32_t atkType = 0;
            if (ClassifySpecialOrSuper(pChar, nullptr, nullptr, nullptr, 0.0f, &moveCls, &specialReason, &atkType)) {
                if (moveCls == MOVE_CLASS_XRAY) {
                    if (outReason) *outReason = specialReason ? specialReason : "Cinematic X-Ray Super Move";
                    return STATE_CINEMATIC_XRAY;
                }
            }

            uint32_t hitboxes = *(uint32_t*)((uintptr_t)pChar + 0x7A3C);
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

            int plr = (pChar == GetP2Char()) ? 2 : 1;
            const MoveLifecycleTracker& tracker = (plr == 2) ? g_P2_Lifecycle : g_P1_Lifecycle;

            if (hitboxes > 0 || activeVols > 0) {
                if (outReason) *outReason = "Active Strike Window (Live 3D Hitboxes Deployed)";
                return STATE_NORMAL_ACTIVE;
            }

            if (IsPredictiveSimultaneousTrade()) {
                if (outReason) *outReason = "Simultaneous Mutual Strike Active (Exact 0-frame delta)";
                return STATE_NORMAL_ACTIVE;
            }

            if (tracker.inMove && tracker.scriptFunc == scriptFunc && tracker.firstActiveFrame > 0) {
                if (outReason) *outReason = "Attack in RECOVERY (Hitbox active window elapsed)";
                return STATE_NORMAL_RECOVERY;
            }

            if (outReason) *outReason = "Attack in STARTUP (Pre-Active Windup)";
            return STATE_NORMAL_STARTUP;

        } __except (EXCEPTION_EXECUTE_HANDLER) {
            if (outReason) *outReason = "Exception During State Evaluation";
            return STATE_NEUTRAL;
        }
    }

    bool IsDefenderInTradeWindow(void* pDefender) {
        if (!pDefender || (uintptr_t)pDefender < 0x01000000) return false;

        const char* stateReason = nullptr;
        CharacterCombatState defState = GetCharacterCombatState(pDefender, &stateReason);

        int defPlr = (pDefender == GetP2Char()) ? 2 : 1;
        uint64_t defStart = (defPlr == 2) ? g_P2_AttackStartFrame : g_P1_AttackStartFrame;
        uint64_t elapsed = (defStart != 0 && g_CurrentFrame >= defStart) ? (g_CurrentFrame - defStart) : 0;

        bool result = (defState == STATE_NORMAL_ACTIVE);
        switch (defState) {
            case STATE_NORMAL_ACTIVE:
                result = true;
                break;
            case STATE_NORMAL_STARTUP:
            case STATE_NORMAL_RECOVERY:
            case STATE_SPECIAL_OR_SUPER:
            case STATE_HITSTUN:
            case STATE_KNOCKDOWN:
            case STATE_CINEMATIC_XRAY:
            case STATE_NEUTRAL:
            default:
                result = false;
                break;
        }

        LogTrace("   [STATE EVAL] P%d Defender State: %s | Elapsed: %lluf | %s",
                 defPlr, 
                 (defState == STATE_NORMAL_ACTIVE) ? "NORMAL_ACTIVE (TRADE ELIGIBLE)" :
                 (defState == STATE_NORMAL_STARTUP) ? "NORMAL_STARTUP (CLEAN INTERRUPT)" :
                 (defState == STATE_NORMAL_RECOVERY) ? "NORMAL_RECOVERY (WHIFF PUNISH)" :
                 (defState == STATE_SPECIAL_OR_SUPER) ? "SPECIAL_OR_SUPER (EXCLUDED)" :
                 (defState == STATE_HITSTUN) ? "HITSTUN (COMBO/STUMBLE)" :
                 (defState == STATE_KNOCKDOWN) ? "KNOCKDOWN" :
                 (defState == STATE_CINEMATIC_XRAY) ? "CINEMATIC_XRAY" : "NEUTRAL/IDLE",
                 elapsed, stateReason ? stateReason : "");

        return result;
    }

    const char* ClassifyHitOutcome(int attPlr, int defPlr, bool isTrade, uint64_t* pOutElapsed, const char** pOutDetail) {
        if (isTrade) {
            if (pOutElapsed) *pOutElapsed = 0;
            if (pOutDetail) *pOutDetail = "Simultaneous mutual collision resolved as two-way trade";
            return "TRADE";
        }

        // Retrieve defender's current action state directly from character pointer
        uint32_t defAction = 0;
        void* pDefender = (defPlr == 2) ? GetP2Char() : GetP1Char();
        if (pDefender && (uintptr_t)pDefender > 0x01000000) {
            __try {
                defAction = *(uint32_t*)((uintptr_t)pDefender + 0x1D4);
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
        }

        // Defenders already in hitstun or juggle are receiving a combo follow-up, NOT a counter or punish!
        bool isDefInHitstun = (defAction >= 0x4000 && defAction <= 0x4FFF && defAction != 0x4205 && defAction != 0x4206);
        if (isDefInHitstun) {
            if (pOutElapsed) *pOutElapsed = 0;
            if (pOutDetail) *pOutDetail = "Defender struck during hitstun (Combo Follow-up)";
            return "COMBO HIT";
        }

        // Defenders in neutral idle, crouch idle, walk mobility, or guard are NOT in recovery!
        bool isDefInNeutralOrGuard = (defAction == 0x0000) || 
                                     (defAction == 0x0100 || defAction == 0x0101) || // Crouch
                                     (defAction >= 0x0200 && defAction <= 0x020F) || // Walk
                                     (defAction >= 0x0900 && defAction <= 0x0A01);   // Block

        uint64_t defStartFrame = (defPlr == 2) ? g_P2_AttackStartFrame : g_P1_AttackStartFrame;
        uint64_t defLastStart  = (defPlr == 2) ? g_P2_LastAttackStartFrame : g_P1_LastAttackStartFrame;
        uint64_t defLastEnd    = (defPlr == 2) ? g_P2_LastAttackEndFrame : g_P1_LastAttackEndFrame;
        const MoveLifecycleTracker& defTracker = (defPlr == 2) ? g_P2_Lifecycle : g_P1_Lifecycle;

        uint64_t defMacroFrame = 0;
        if (g_ActiveMacroTriggerFrame > 0 && g_CurrentFrame <= g_ActiveMacroTriggerFrame + 60) {
            int delay = (defPlr == 2) ? g_ActiveMacroP2Delay : g_ActiveMacroP1Delay;
            if (delay >= 0) {
                defMacroFrame = g_ActiveMacroTriggerFrame + (uint64_t)delay;
            }
        }

        // 1. Prioritize active MoveLifecycleTracker
        if (defTracker.inMove) {
            uint64_t elapsed = (defTracker.startFrame > 0 && g_CurrentFrame >= defTracker.startFrame) ?
                               (g_CurrentFrame - defTracker.startFrame) : 0;
            if (pOutElapsed) *pOutElapsed = elapsed;

            if (defTracker.firstActiveFrame > 0 && g_CurrentFrame < defTracker.firstActiveFrame) {
                if (pOutDetail) *pOutDetail = "Defender struck during move startup before active hit frames";
                return "STARTUP INTERRUPT";
            } else if (defTracker.recoveryStartFrame > 0 && g_CurrentFrame >= defTracker.recoveryStartFrame) {
                if (!isDefInNeutralOrGuard) {
                    if (pOutDetail) *pOutDetail = "Defender struck during move recovery frames";
                    return "RECOVERY PUNISH";
                }
            } else if (defTracker.firstActiveFrame > 0) {
                if (pOutDetail) *pOutDetail = "Defender struck during active attack swing / hitbox window";
                return "ACTIVE INTERRUPT";
            } else {
                if (pOutDetail) *pOutDetail = "Defender struck during move startup";
                return "STARTUP INTERRUPT";
            }
        }

        // 2. Tightened recent whiff / move recovery:
        // Must be within 14 frames of move end AND still committed to move animation (not in neutral/guard)
        if (!isDefInNeutralOrGuard) {
            if (defTracker.lastFinishedEndFrame > 0 && g_CurrentFrame >= defTracker.lastFinishedEndFrame &&
                (g_CurrentFrame - defTracker.lastFinishedEndFrame <= 14)) {
                uint64_t elapsed = g_CurrentFrame - defTracker.lastFinishedEndFrame;
                if (pOutElapsed) *pOutElapsed = elapsed;
                if (pOutDetail) *pOutDetail = "Defender struck during tight whiff recovery window";
                return "RECOVERY PUNISH";
            }

            if (defLastEnd > 0 && g_CurrentFrame >= defLastEnd && (g_CurrentFrame - defLastEnd <= 14)) {
                uint64_t elapsed = g_CurrentFrame - defLastEnd;
                if (pOutElapsed) *pOutElapsed = elapsed;
                if (pOutDetail) *pOutDetail = "Defender struck during attack recovery window";
                return "RECOVERY PUNISH";
            }

            // If defender is explicitly in an attack action state (0x5000-0x5FFF) with no active hitboxes
            if (defAction >= 0x5000 && defAction <= 0x5FFF) {
                uint32_t hitboxes = 0;
                if (pDefender) {
                    __try { hitboxes = *(uint32_t*)((uintptr_t)pDefender + 0x7A3C); } __except (EXCEPTION_EXECUTE_HANDLER) {}
                }
                if (hitboxes == 0 && defStartFrame > 0 && g_CurrentFrame > defStartFrame + 14) {
                    if (pOutDetail) *pOutDetail = "Defender struck during move recovery animation";
                    return "RECOVERY PUNISH";
                }
            }
        }

        // 3. Fallback to start frame timestamps
        uint64_t effectiveStart = 0;
        if (defStartFrame > 0 && g_CurrentFrame >= defStartFrame) {
            effectiveStart = defStartFrame;
        } else if (defMacroFrame > 0 && g_CurrentFrame >= defMacroFrame) {
            effectiveStart = defMacroFrame;
        } else if (defLastStart > 0 && g_CurrentFrame >= defLastStart && (g_CurrentFrame - defLastStart <= 30)) {
            effectiveStart = defLastStart;
        }

        if (effectiveStart == 0 || isDefInNeutralOrGuard) {
            if (pOutElapsed) *pOutElapsed = 0;
            if (pOutDetail) *pOutDetail = "Defender was in neutral / idle stance (not attacking)";
            return "CLEAN HIT (Neutral)";
        }

        uint64_t elapsed = g_CurrentFrame - effectiveStart;
        if (pOutElapsed) *pOutElapsed = elapsed;

        if (elapsed <= 14) {
            if (pOutDetail) *pOutDetail = "Defender struck during move startup before active hit frames";
            return "STARTUP INTERRUPT";
        } else if (elapsed <= 24) {
            if (pOutDetail) *pOutDetail = "Defender struck during active attack swing / hitbox window";
            return "ACTIVE INTERRUPT";
        } else if (elapsed <= 34 && (defAction >= 0x5000 && defAction <= 0x5FFF)) {
            if (pOutDetail) *pOutDetail = "Defender struck during move recovery frames";
            return "RECOVERY PUNISH";
        } else {
            if (pOutDetail) *pOutDetail = "Defender recovered to neutral";
            return "CLEAN HIT (Neutral)";
        }
    }

    // =========================================================================
    // Move ID & Move Name Telemetry HUD
    // =========================================================================
    static void* s_PlayerActiveAttackAnimSeq[3] = { nullptr, nullptr, nullptr };

    void* GetPlayerActiveAttackAnimSeq(int playerIdx) {
        if (playerIdx >= 1 && playerIdx <= 2) return s_PlayerActiveAttackAnimSeq[playerIdx];
        return nullptr;
    }

    void SetPlayerActiveAttackAnimSeq(int playerIdx, void* pSeq) {
        if (playerIdx >= 1 && playerIdx <= 2) s_PlayerActiveAttackAnimSeq[playerIdx] = pSeq;
    }

    namespace MoveTelemetryHUD {
        bool  g_ShowMoveHUD        = false;  // Disabled by default on startup
        bool  g_ShowActionHex      = true;   // Display [0x5200] prefix
        float g_P1MoveHUDOffsetX   = 100.0f; // Default 100 px (Dramatic standard)
        float g_P1MoveHUDOffsetY   = 50.0f;  // Default 50 px
        float g_P2MoveHUDOffsetX   = -100.0f; // Default -100 px (Dramatic standard)
        float g_P2MoveHUDOffsetY   = 50.0f;  // Default 50 px

        static inline uint32_t MakeColorRGBA(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
            return ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r;
        }

        // Safe resolution of FName index to engine string from GNames table
        static bool SafeGetFName(int nameIndex, int nameNumber, char* outBuf, size_t maxLen) {
            if (!outBuf || maxLen == 0 || nameIndex <= 0) return false;
            outBuf[0] = '\0';
            __try {
                // Priority 1: Direct GNames TArray (Used in anim_viewer.cpp)
                void*** pGNamesPtr = (void***)0x00E7C058;
                int* pGNamesCount = (int*)0x00E7C05C;

                if (pGNamesPtr && *pGNamesPtr && pGNamesCount && nameIndex >= 0 && nameIndex < *pGNamesCount) {
                    void** gnames = *pGNamesPtr;
                    void* entry = gnames[nameIndex];
                    if (entry && (uintptr_t)entry >= 0x00400000 && (uintptr_t)entry < 0x7FFE0000) {
                        const char* rawName = (const char*)((uintptr_t)entry + 0x10);
                        if (rawName && (uintptr_t)rawName >= 0x00400000 && (uintptr_t)rawName < 0x7FFE0000 && rawName[0] != '\0') {
                            if (nameNumber > 0) {
                                snprintf(outBuf, maxLen, "%s_%d", rawName, nameNumber - 1);
                            } else {
                                strncpy_s(outBuf, maxLen, rawName, _TRUNCATE);
                            }
                            return true;
                        }
                    }
                }

                // Priority 2: Chunked GNames table (0x00E7C0A4)
                uint32_t* gnamesChunked = *(uint32_t**)0x00E7C0A4;
                if (gnamesChunked && (uintptr_t)gnamesChunked > 0x00400000) {
                    uint32_t chunk = *(uint32_t*)((uintptr_t)gnamesChunked + (nameIndex / 0x4000) * 4);
                    if (chunk && (uintptr_t)chunk > 0x00400000) {
                        uint32_t entry = *(uint32_t*)(chunk + (nameIndex % 0x4000) * 4);
                        if (entry && (uintptr_t)entry > 0x00400000) {
                            const char* rawName = (const char*)(entry + 0x10);
                            if (rawName && (uintptr_t)rawName >= 0x00400000 && (uintptr_t)rawName < 0x7FFE0000 && rawName[0] != '\0') {
                                if (nameNumber > 0) {
                                    snprintf(outBuf, maxLen, "%s_%d", rawName, nameNumber - 1);
                                } else {
                                    strncpy_s(outBuf, maxLen, rawName, _TRUNCATE);
                                }
                                return true;
                            }
                        }
                    }
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return false;
            }
            return false;
        }

        static void NotifyAnimPlayed(int playerIdx, int nameIndex, int nameNumber) {
            if (playerIdx < 1 || playerIdx > 2 || nameIndex <= 0) return;

            char rawName[64] = { 0 };
            if (!SafeGetFName(nameIndex, nameNumber, rawName, sizeof(rawName))) {
                return;
            }
            if (rawName[0] == '\0') return;

            // Detect whether this is a non-combat locomotion / defensive reaction animation
            bool isNonCombat = false;
            if (_stricmp(rawName, "idle") == 0 || _stricmp(rawName, "stance") == 0 ||
                _stricmp(rawName, "duck") == 0 || _stricmp(rawName, "crouch") == 0 ||
                strstr(rawName, "Stance") || strstr(rawName, "stance") ||
                strstr(rawName, "Idle") || strstr(rawName, "idle") ||
                strstr(rawName, "Walk") || strstr(rawName, "walk") ||
                strstr(rawName, "Turn") || strstr(rawName, "turn") ||
                strstr(rawName, "duck_") || strstr(rawName, "Duck_") ||
                strstr(rawName, "crouch_idle") || strstr(rawName, "crouch_in") || strstr(rawName, "crouch_out") ||
                strstr(rawName, "duck_in") || strstr(rawName, "duck_out") ||
                strstr(rawName, "Flinch") || strstr(rawName, "flinch") ||
                strstr(rawName, "Hit_") || strstr(rawName, "hit_") ||
                strstr(rawName, "React") || strstr(rawName, "react") ||
                strstr(rawName, "Hurt") || strstr(rawName, "hurt") ||
                strstr(rawName, "Knockdown") || strstr(rawName, "knockdown") ||
                strstr(rawName, "Getup") || strstr(rawName, "getup")) {
                isNonCombat = true;
            }

            // Always track current animation
            s_PlayerAnim[playerIdx].currentAnimID = (uint32_t)nameIndex;
            strncpy_s(s_PlayerAnim[playerIdx].currentAnimName, sizeof(s_PlayerAnim[playerIdx].currentAnimName), rawName, _TRUNCATE);
            s_PlayerAnim[playerIdx].currentAnimFrame = g_CurrentFrame;

            // If it is a combat attack animation, track it as the active attack
            if (!isNonCombat) {
                s_PlayerAnim[playerIdx].activeAttackID = (uint32_t)nameIndex;
                strncpy_s(s_PlayerAnim[playerIdx].activeAttackName, sizeof(s_PlayerAnim[playerIdx].activeAttackName), rawName, _TRUNCATE);
                s_PlayerAnim[playerIdx].attackStartFrame = g_CurrentFrame;
            }
        }

        // 1. Direct MKCharacter Animation Function Hook (0x00446E20)
        typedef int (__fastcall *tPlayAnimSeq)(void* pChar, void* edxUnused, void* pAnimSeq, float startTime, float blendIn, int flags);
        static tPlayAnimSeq oPlayAnimSeq = nullptr;

        static int __fastcall hkPlayAnimSeq(void* pChar, void* edxUnused, void* pAnimSeq, float startTime, float blendIn, int flags) {
            if (MK9TradeFix::IsMatchActive() && pChar && (uintptr_t)pChar > 0x01000000 && pAnimSeq && (uintptr_t)pAnimSeq > 0x01000000) {
                int playerIdx = (pChar == GetP2Char()) ? 2 : 1;
                int nameIndex = 0;
                int nameNumber = 0;
                char animNameBuf[64] = { 0 };
                __try {
                    nameIndex = *(int*)((uintptr_t)pAnimSeq + 0x38);
                    nameNumber = *(int*)((uintptr_t)pAnimSeq + 0x3C);
                    if (nameIndex <= 0) {
                        nameIndex = *(int*)((uintptr_t)pAnimSeq + 0x28);
                        nameNumber = *(int*)((uintptr_t)pAnimSeq + 0x2C);
                    }
                } __except (EXCEPTION_EXECUTE_HANDLER) {}

                if (nameIndex > 0) {
                    SafeGetFName(nameIndex, nameNumber, animNameBuf, sizeof(animNameBuf));
                    NotifyAnimPlayed(playerIdx, nameIndex, nameNumber);
                }
                s_PlayerActiveAttackAnimSeq[playerIdx] = pAnimSeq;

                // Calculate and apply scaled playback rate for hurt animation recovery
                float stunMult = StunModifier::CalculateHurtAnimRate(pChar, pAnimSeq, animNameBuf);
                if (stunMult > 0.01f && fabsf(stunMult - 1.0f) > 0.01f) {
                    __try {
                        if (StunModifier::g_PlayerStun[playerIdx].pLastAnimSeq && 
                            StunModifier::g_PlayerStun[playerIdx].pLastAnimSeq != pAnimSeq && 
                            (uintptr_t)StunModifier::g_PlayerStun[playerIdx].pLastAnimSeq > 0x01000000) {
                            *(float*)((uintptr_t)StunModifier::g_PlayerStun[playerIdx].pLastAnimSeq + 0x48) = 1.0f;
                        }
                        *(float*)((uintptr_t)pAnimSeq + 0x48) = stunMult;
                        StunModifier::g_PlayerStun[playerIdx].pLastAnimSeq = pAnimSeq;
                    } __except (EXCEPTION_EXECUTE_HANDLER) {}
                } else {
                    __try {
                        if (StunModifier::g_PlayerStun[playerIdx].pLastAnimSeq && 
                            (uintptr_t)StunModifier::g_PlayerStun[playerIdx].pLastAnimSeq > 0x01000000) {
                            *(float*)((uintptr_t)StunModifier::g_PlayerStun[playerIdx].pLastAnimSeq + 0x48) = 1.0f;
                            StunModifier::g_PlayerStun[playerIdx].pLastAnimSeq = nullptr;
                        }
                        *(float*)((uintptr_t)pAnimSeq + 0x48) = 1.0f;
                    } __except (EXCEPTION_EXECUTE_HANDLER) {}
                }
            }

            if (oPlayAnimSeq) {
                return oPlayAnimSeq(pChar, edxUnused, pAnimSeq, startTime, blendIn, flags);
            }
            return 0;
        }

        // 2. Combat Animation Dispatcher Hook (0x008A4080)
        typedef void (__fastcall *tCombatPlayAnim)(void* pChar, void* edxUnused, void* pAnimSeq, float rate, void* p3, float blendIn, uint32_t flags, void* p6, void* p7);
        static tCombatPlayAnim oCombatPlayAnim = nullptr;

        static void __fastcall hkCombatPlayAnim(void* pChar, void* edxUnused, void* pAnimSeq, float rate, void* p3, float blendIn, uint32_t flags, void* p6, void* p7) {
            if (MK9TradeFix::IsMatchActive() && pChar && (uintptr_t)pChar > 0x01000000 && pAnimSeq && (uintptr_t)pAnimSeq > 0x01000000) {
                int playerIdx = (pChar == GetP2Char()) ? 2 : 1;
                int nameIndex = 0;
                int nameNumber = 0;
                char animNameBuf[64] = { 0 };
                __try {
                    nameIndex = *(int*)((uintptr_t)pAnimSeq + 0x38);
                    nameNumber = *(int*)((uintptr_t)pAnimSeq + 0x3C);
                    if (nameIndex <= 0) {
                        nameIndex = *(int*)((uintptr_t)pAnimSeq + 0x28);
                        nameNumber = *(int*)((uintptr_t)pAnimSeq + 0x2C);
                    }
                } __except (EXCEPTION_EXECUTE_HANDLER) {}

                if (nameIndex > 0) {
                    SafeGetFName(nameIndex, nameNumber, animNameBuf, sizeof(animNameBuf));
                    NotifyAnimPlayed(playerIdx, nameIndex, nameNumber);
                }
                s_PlayerActiveAttackAnimSeq[playerIdx] = pAnimSeq;

                // Calculate and apply scaled playback rate for hurt animation recovery
                float stunMult = StunModifier::CalculateHurtAnimRate(pChar, pAnimSeq, animNameBuf);
                if (stunMult > 0.01f && fabsf(stunMult - 1.0f) > 0.01f) {
                    rate *= stunMult;
                    __try {
                        if (StunModifier::g_PlayerStun[playerIdx].pLastAnimSeq && 
                            StunModifier::g_PlayerStun[playerIdx].pLastAnimSeq != pAnimSeq && 
                            (uintptr_t)StunModifier::g_PlayerStun[playerIdx].pLastAnimSeq > 0x01000000) {
                            *(float*)((uintptr_t)StunModifier::g_PlayerStun[playerIdx].pLastAnimSeq + 0x48) = 1.0f;
                        }
                        *(float*)((uintptr_t)pAnimSeq + 0x48) = stunMult;
                        StunModifier::g_PlayerStun[playerIdx].pLastAnimSeq = pAnimSeq;
                    } __except (EXCEPTION_EXECUTE_HANDLER) {}
                } else {
                    __try {
                        if (StunModifier::g_PlayerStun[playerIdx].pLastAnimSeq && 
                            (uintptr_t)StunModifier::g_PlayerStun[playerIdx].pLastAnimSeq > 0x01000000) {
                            *(float*)((uintptr_t)StunModifier::g_PlayerStun[playerIdx].pLastAnimSeq + 0x48) = 1.0f;
                            StunModifier::g_PlayerStun[playerIdx].pLastAnimSeq = nullptr;
                        }
                        *(float*)((uintptr_t)pAnimSeq + 0x48) = 1.0f;
                    } __except (EXCEPTION_EXECUTE_HANDLER) {}

                    // Apply move startup speed scaling if active attack
                    float moveMult = ExtraGameplay::GetMoveAnimRateMultiplier(pChar, animNameBuf);
                    if (moveMult > 0.01f && fabsf(moveMult - 1.0f) > 0.01f) {
                        rate *= moveMult;
                    }

                    // Apply Attack Frame Modifier initial startup rate
                    float afmStartupRate = AttackFrameModifier::GetInitialStartupRate(playerIdx);
                    if (afmStartupRate > 0.01f && fabsf(afmStartupRate - 1.0f) > 0.01f) {
                        rate *= afmStartupRate;
                        __try {
                            *(float*)((uintptr_t)pAnimSeq + 0x48) = afmStartupRate;
                        } __except (EXCEPTION_EXECUTE_HANDLER) {}
                    }
                }
            }

            if (oCombatPlayAnim) {
                oCombatPlayAnim(pChar, edxUnused, pAnimSeq, rate, p3, blendIn, flags, p6, p7);
            }
        }

        // 3. UE3 Skeletal Mesh PlayAnim Hook (0x006BA740)
        typedef void (__fastcall *tSkelMeshPlayAnim)(void* pSkelComp, void* edxUnused, uint32_t nameIndex, float rate, float blendIn, int bLoop, float blendOut);
        static tSkelMeshPlayAnim oSkelMeshPlayAnim = nullptr;

        static void __fastcall hkSkelMeshPlayAnim(void* pSkelComp, void* edxUnused, uint32_t nameIndex, float rate, float blendIn, int bLoop, float blendOut) {
            if (MK9TradeFix::IsMatchActive() && pSkelComp && nameIndex > 0) {
                void* p1 = GetP1Char();
                void* p2 = GetP2Char();
                int playerIdx = 0;
                __try {
                    if (p1 && (uintptr_t)p1 > 0x01000000 && *(void**)((uintptr_t)p1 + 0x010C) == pSkelComp) {
                        playerIdx = 1;
                    } else if (p2 && (uintptr_t)p2 > 0x01000000 && *(void**)((uintptr_t)p2 + 0x010C) == pSkelComp) {
                        playerIdx = 2;
                    }
                } __except (EXCEPTION_EXECUTE_HANDLER) {}

                if (playerIdx != 0) {
                    NotifyAnimPlayed(playerIdx, (int)nameIndex, 0);
                }
            }

            if (oSkelMeshPlayAnim) {
                oSkelMeshPlayAnim(pSkelComp, edxUnused, nameIndex, rate, blendIn, bLoop, blendOut);
            }
        }

        void NotifyPlayAnimSeq(void* pChar, void* pAnimSeq) {
            if (!pChar || !pAnimSeq) return;
            int playerIdx = (pChar == GetP2Char()) ? 2 : 1;
            int nameIndex = 0;
            int nameNumber = 0;
            __try {
                nameIndex = *(int*)((uintptr_t)pAnimSeq + 0x38);
                nameNumber = *(int*)((uintptr_t)pAnimSeq + 0x3C);
                if (nameIndex <= 0) {
                    nameIndex = *(int*)((uintptr_t)pAnimSeq + 0x28);
                    nameNumber = *(int*)((uintptr_t)pAnimSeq + 0x2C);
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {}

            if (nameIndex > 0) {
                NotifyAnimPlayed(playerIdx, nameIndex, nameNumber);
            }
        }

        void NotifyCombatPlayAnim(void* pChar, void* pAnimSeq) {
            NotifyPlayAnimSeq(pChar, pAnimSeq);
        }

        void NotifySkelMeshPlayAnim(void* pSkelComp, uint32_t nameIndex) {
            if (!pSkelComp || nameIndex == 0) return;
            void* p1 = GetP1Char();
            void* p2 = GetP2Char();
            int playerIdx = 0;
            __try {
                if (p1 && (uintptr_t)p1 > 0x01000000 && *(void**)((uintptr_t)p1 + 0x010C) == pSkelComp) playerIdx = 1;
                else if (p2 && (uintptr_t)p2 > 0x01000000 && *(void**)((uintptr_t)p2 + 0x010C) == pSkelComp) playerIdx = 2;
            } __except (EXCEPTION_EXECUTE_HANDLER) {}

            if (playerIdx != 0) {
                NotifyAnimPlayed(playerIdx, (int)nameIndex, 0);
            }
        }

        void InstallHooks() {
            // 1. Hook MKCharacter::PlayAnimSeq (0x00446E20)
            if (MH_CreateHook((void*)0x00446E20, (void*)hkPlayAnimSeq, (void**)&oPlayAnimSeq) == MH_OK) {
                MH_EnableHook((void*)0x00446E20);
                LogTrace("[MOVE HUD] Hooked MKCharacter::PlayAnimSeq at 0x00446E20");
            } else {
                LogTrace("[MOVE HUD] Failed to hook MKCharacter::PlayAnimSeq at 0x00446E20");
            }

            // 2. Hook Combat_PlayAnim (0x008A4080)
            if (MH_CreateHook((void*)0x008A4080, (void*)hkCombatPlayAnim, (void**)&oCombatPlayAnim) == MH_OK) {
                MH_EnableHook((void*)0x008A4080);
                LogTrace("[MOVE HUD] Hooked Combat_PlayAnim at 0x008A4080");
            } else {
                LogTrace("[MOVE HUD] Failed to hook Combat_PlayAnim at 0x008A4080");
            }
        }

        bool GetPlayerMoveTelemetry(int playerIdx, char* outText, size_t maxLen, uint32_t* outColorRGBA) {
            if (!outText || maxLen == 0) return false;
            outText[0] = '\0';

            void* pChar = (playerIdx == 1) ? GetP1Char() : GetP2Char();
            if (!pChar || (uintptr_t)pChar < 0x01000000) return false;

            uint32_t actionState = 0;
            int      actionID    = 0;
            uintptr_t funcPtr    = 0;

            __try {
                actionState = *(uint32_t*)((uintptr_t)pChar + 0x1D4);
                actionID    = *(int*)((uintptr_t)pChar + 0x2E0);
                funcPtr     = *(uintptr_t*)((uintptr_t)pChar + 0x2F88);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return false;
            }

            // Determine if character is currently executing an attack
            bool isAttack = (actionState == 0x5200 || actionState == 0x5300 || actionState == 0x5208 || 
                             actionState == 0x4203 || actionState == 0x5400 ||
                             IsCharacterInActiveAttack(pChar));
            if (!isAttack && s_PlayerAnim[playerIdx].activeAttackName[0] != '\0' && 
                (g_CurrentFrame >= s_PlayerAnim[playerIdx].attackStartFrame) &&
                (g_CurrentFrame - s_PlayerAnim[playerIdx].attackStartFrame <= 45)) {
                // Keep showing attack during recovery window unless transitioned to clean stance/defensive state
                if (actionState != 0x0000 && actionState != 0x0001 && actionState != 0x0100 && actionState != 0x0101 &&
                    actionState != 0x0102 && actionState != 0x0103 && actionState != 0x0104 &&
                    actionState != 0x0A00 && actionState != 0x0900 && actionState != 0x4600 && actionState != 0x4700 &&
                    actionState != 0x4201 && actionState != 0x460D) {
                    isAttack = true;
                }
            }

            char moveName[64] = { 0 };
            uint32_t moveID = 0;
            uint32_t color = MakeColorRGBA(195, 205, 220, 230);

            if (isAttack) {
                // Priority 1: Use captured authoritative animation name & FName ID from hooks
                if (s_PlayerAnim[playerIdx].activeAttackName[0] != '\0') {
                    strncpy_s(moveName, sizeof(moveName), s_PlayerAnim[playerIdx].activeAttackName, _TRUNCATE);
                    moveID = s_PlayerAnim[playerIdx].activeAttackID;
                }

                // Priority 2: Character's MKScript function table (for special moves and supers)
                if (moveName[0] == '\0' && funcPtr > 0) {
                    __try {
                        uintptr_t pkg = *(uintptr_t*)((uintptr_t)pChar + 0x02A8);
                        if (!pkg || pkg < 0x00400000 || (pkg & 3) != 0) pkg = *(uintptr_t*)((uintptr_t)pChar + 0x7624);
                        if (!pkg || pkg < 0x00400000 || (pkg & 3) != 0) pkg = *(uintptr_t*)((uintptr_t)pChar + 0x2F84);
                        if (pkg >= 0x00400000 && pkg < 0x7FFE0000 && (pkg & 3) == 0) {
                            int fnCount = *(int*)(pkg + 0x20);
                            uintptr_t pFnTable = *(uintptr_t*)(pkg + 0x8C);
                            if (!pFnTable) pFnTable = *(uintptr_t*)(pkg + 0x94);
                            if (pFnTable >= 0x00400000 && pFnTable < 0x7FFE0000 && fnCount > 0 && fnCount < 5000) {
                                if (funcPtr > 0 && funcPtr <= (uintptr_t)fnCount) {
                                    uintptr_t pFn = *(uintptr_t*)(pFnTable - 4 + funcPtr * 4);
                                    if (pFn >= 0x00400000 && pFn < 0x7FFE0000 && (pFn & 3) == 0) {
                                        const char* pSym = *(const char**)pFn;
                                        if (pSym && (uintptr_t)pSym >= 0x00400000 && (uintptr_t)pSym < 0x7FFE0000 &&
                                            pSym[0] >= 32 && pSym[0] <= 126) {
                                            strncpy_s(moveName, sizeof(moveName), pSym, _TRUNCATE);
                                            moveID = (uint32_t)funcPtr;
                                        }
                                    }
                                }
                            }
                        }
                    } __except (EXCEPTION_EXECUTE_HANDLER) {}
                }

                // Priority 3: Native Move Record in Combat Controller PlayerInfo (pRec reaction/substate IDs)
                if (moveName[0] == '\0' || moveID == 0) {
                    __try {
                        void* pInfo = *(void**)((uintptr_t)pChar + 0x7638);
                        if (pInfo && (uintptr_t)pInfo >= 0x00400000 && (uintptr_t)pInfo < 0x7FFF0000) {
                            uint32_t curr = *(uint32_t*)((uintptr_t)pInfo + 0x574);
                            if (curr == 0) curr = *(uint32_t*)((uintptr_t)pInfo + 0x570);
                            if (curr > 0) {
                                uintptr_t pRec = (uintptr_t)pInfo + 0x48 + (curr - 1) * 0x2C;
                                uint32_t recMove = *(uint32_t*)(pRec + 0x00);
                                uint32_t recRx   = *(uint32_t*)(pRec + 0x18);
                                uint32_t recAtk  = *(uint32_t*)(pRec + 0x20);

                                if (moveID == 0) {
                                    moveID = (recMove != 0) ? recMove : ((recRx != 0) ? recRx : recAtk);
                                }
                                if (moveName[0] == '\0') {
                                    if (recRx == 176 || recRx == 177 || recRx == 178 || IsExecutingD2Script(pChar)) {
                                        strncpy_s(moveName, sizeof(moveName), "Attack2Down", _TRUNCATE);
                                    } else if (recRx == 0x39 || recRx == 0x3D) {
                                        strncpy_s(moveName, sizeof(moveName), "Attack1", _TRUNCATE);
                                    } else if (recRx == 0x3A || recRx == 0x3E) {
                                        strncpy_s(moveName, sizeof(moveName), "Attack2", _TRUNCATE);
                                    } else if (recRx == 0x3B || recRx == 0x3F) {
                                        strncpy_s(moveName, sizeof(moveName), "Attack3", _TRUNCATE);
                                    } else if (recRx == 0x3C || recRx == 0x40) {
                                        strncpy_s(moveName, sizeof(moveName), "Attack4", _TRUNCATE);
                                    } else if (recRx == 180 || recRx == 181) {
                                        strncpy_s(moveName, sizeof(moveName), "Attack4Down", _TRUNCATE);
                                    } else if (recRx == 38 || recRx == 40 || recRx == 292) {
                                        strncpy_s(moveName, sizeof(moveName), (actionState == 0x5300) ? "Attack1Down" : "Attack3Down", _TRUNCATE);
                                    }
                                }
                            }
                        }
                    } __except (EXCEPTION_EXECUTE_HANDLER) {}
                }

                // Priority 4: Action ID & State mapping
                if (moveName[0] == '\0') {
                    if (actionID == 0x01) strncpy_s(moveName, sizeof(moveName), "Attack1", _TRUNCATE);
                    else if (actionID == 0x02) strncpy_s(moveName, sizeof(moveName), "Attack2", _TRUNCATE);
                    else if (actionID == 0x03) strncpy_s(moveName, sizeof(moveName), "Attack3", _TRUNCATE);
                    else if (actionID == 0x04) strncpy_s(moveName, sizeof(moveName), "Attack4", _TRUNCATE);
                    else if (actionID == 0x20) strncpy_s(moveName, sizeof(moveName), "Attack2Down", _TRUNCATE);
                    else if (actionID == 0x21) strncpy_s(moveName, sizeof(moveName), "Attack4Down", _TRUNCATE);
                    else if (actionID == 0x18 || actionState == 0x4203 || actionState == 0x5208) strncpy_s(moveName, sizeof(moveName), "XRay", _TRUNCATE);
                    else if (actionState == 0x5300) {
                        strncpy_s(moveName, sizeof(moveName), IsFighterExecutingUppercut(pChar) ? "Attack2Down" : "Attack1Down", _TRUNCATE);
                    } else {
                        strncpy_s(moveName, sizeof(moveName), "Attack", _TRUNCATE);
                    }
                }
                if (moveID == 0) {
                    moveID = (actionID != 0) ? (uint32_t)actionID : actionState;
                }

                // Color selection
                if (actionState == 0x4203 || actionState == 0x5208 || strstr(moveName, "xray") || strstr(moveName, "XRay")) {
                    color = MakeColorRGBA(0, 240, 255, 255); // Cyan for X-Ray
                } else {
                    color = MakeColorRGBA(255, 185, 45, 255); // Amber gold for active attack
                }

                // Construct text for attack move
                if (g_ShowActionHex) {
                    snprintf(outText, maxLen, "[0x%04X] [ID: %u] %s", actionState, moveID, moveName);
                } else {
                    snprintf(outText, maxLen, "[ID: %u] %s", moveID, moveName);
                }
            } else {
                // Non-attack state (Neutral, Locomotion, Block, Hitstun, Knockdown)
                char stateName[64] = { 0 };
                switch (actionState) {
                    case 0x0000:
                        strncpy_s(stateName, sizeof(stateName), "Neutral / Idle", _TRUNCATE);
                        color = MakeColorRGBA(180, 185, 195, 220);
                        break;
                    case 0x0001:
                        strncpy_s(stateName, sizeof(stateName), "Crouch Idle", _TRUNCATE);
                        color = MakeColorRGBA(180, 185, 195, 220);
                        break;
                    case 0x0100:
                        strncpy_s(stateName, sizeof(stateName), "Standing to Crouching", _TRUNCATE);
                        color = MakeColorRGBA(180, 185, 195, 220);
                        break;
                    case 0x0101:
                        strncpy_s(stateName, sizeof(stateName), "Full Crouch", _TRUNCATE);
                        color = MakeColorRGBA(180, 185, 195, 220);
                        break;
                    case 0x0102:
                        strncpy_s(stateName, sizeof(stateName), "Crouch Block", _TRUNCATE);
                        color = MakeColorRGBA(50, 255, 120, 255); // Emerald Green
                        break;
                    case 0x0103:
                        strncpy_s(stateName, sizeof(stateName), "Crouch Hitstun", _TRUNCATE);
                        color = MakeColorRGBA(235, 110, 255, 255); // Magenta
                        break;
                    case 0x0104:
                        strncpy_s(stateName, sizeof(stateName), "Crouch to Stand", _TRUNCATE);
                        color = MakeColorRGBA(180, 185, 195, 220);
                        break;
                    case 0x0200:
                        strncpy_s(stateName, sizeof(stateName), "Neutral Jump", _TRUNCATE);
                        color = MakeColorRGBA(140, 210, 255, 220);
                        break;
                    case 0x0201:
                        strncpy_s(stateName, sizeof(stateName), "Forward Jump", _TRUNCATE);
                        color = MakeColorRGBA(140, 210, 255, 220);
                        break;
                    case 0x0202:
                        strncpy_s(stateName, sizeof(stateName), "Backward Jump", _TRUNCATE);
                        color = MakeColorRGBA(140, 210, 255, 220);
                        break;
                    case 0x0300:
                        strncpy_s(stateName, sizeof(stateName), "Forward Dash", _TRUNCATE);
                        color = MakeColorRGBA(255, 160, 50, 230);
                        break;
                    case 0x0301:
                        strncpy_s(stateName, sizeof(stateName), "Backdash", _TRUNCATE);
                        color = MakeColorRGBA(255, 160, 50, 230);
                        break;
                    case 0x0900:
                    case 0x0901:
                        strncpy_s(stateName, sizeof(stateName), "Crouch Block", _TRUNCATE);
                        color = MakeColorRGBA(50, 255, 120, 255); // Emerald Green
                        break;
                    case 0x0A00:
                    case 0x0A01:
                        strncpy_s(stateName, sizeof(stateName), "Stand Block", _TRUNCATE);
                        color = MakeColorRGBA(50, 255, 120, 255); // Emerald Green
                        break;
                    case 0x4600:
                        strncpy_s(stateName, sizeof(stateName), "High Hitstun", _TRUNCATE);
                        color = MakeColorRGBA(235, 110, 255, 255); // Magenta
                        break;
                    case 0x4700:
                        strncpy_s(stateName, sizeof(stateName), "Low / Mid Hitstun", _TRUNCATE);
                        color = MakeColorRGBA(235, 110, 255, 255); // Magenta
                        break;
                    case 0x4201:
                        strncpy_s(stateName, sizeof(stateName), "Air Knockdown", _TRUNCATE);
                        color = MakeColorRGBA(235, 90, 220, 255);
                        break;
                    case 0x460D:
                        strncpy_s(stateName, sizeof(stateName), "Floor Knockdown", _TRUNCATE);
                        color = MakeColorRGBA(235, 90, 220, 255);
                        break;
                    case 0x4F00:
                        strncpy_s(stateName, sizeof(stateName), "Blockstun Recoil", _TRUNCATE);
                        color = MakeColorRGBA(0, 215, 240, 230);
                        break;
                    case 0x6200:
                        strncpy_s(stateName, sizeof(stateName), "Face-Up Getup", _TRUNCATE);
                        color = MakeColorRGBA(120, 220, 140, 240);
                        break;
                    case 0x6201:
                        strncpy_s(stateName, sizeof(stateName), "Face-Down Getup", _TRUNCATE);
                        color = MakeColorRGBA(120, 220, 140, 240);
                        break;
                    case 0x6202:
                    case 0x6203:
                        strncpy_s(stateName, sizeof(stateName), "Roll Getup (F-Up)", _TRUNCATE);
                        color = MakeColorRGBA(120, 220, 140, 240);
                        break;
                    case 0x6204:
                    case 0x6205:
                        strncpy_s(stateName, sizeof(stateName), "Roll Getup (F-Down)", _TRUNCATE);
                        color = MakeColorRGBA(120, 220, 140, 240);
                        break;
                    default:
                        if (actionState >= 0x0A02 && actionState <= 0x0A0F) {
                            strncpy_s(stateName, sizeof(stateName), "Blockstun Recoil", _TRUNCATE);
                            color = MakeColorRGBA(0, 215, 240, 230);
                        } else if (actionState >= 0x2010 && actionState <= 0x201F) {
                            strncpy_s(stateName, sizeof(stateName), "Dash Mobility", _TRUNCATE);
                            color = MakeColorRGBA(255, 160, 50, 230);
                        } else if (actionState >= 0x4000 && actionState <= 0x45FF) {
                            strncpy_s(stateName, sizeof(stateName), "Air Juggle Hitstun", _TRUNCATE);
                            color = MakeColorRGBA(235, 110, 255, 255);
                        } else {
                            snprintf(stateName, sizeof(stateName), "Action 0x%04X", actionState);
                            color = MakeColorRGBA(195, 205, 220, 230);
                        }
                        break;
                }

                if (g_ShowActionHex) {
                    snprintf(outText, maxLen, "[0x%04X] %s", actionState, stateName);
                } else {
                    strncpy_s(outText, maxLen, stateName, _TRUNCATE);
                }
            }

            if (outColorRGBA) *outColorRGBA = color;
            return true;
        }
    }

    // =========================================================================
    // Hit Outcome Pop-up Indication HUD (Counter Hit, Trade Hit, Fatal Punish)
    // =========================================================================
    namespace HitIndicationHUD {
        bool  g_ShowHitIndicationHUD = false;  // Disabled by default; auto-enabled when hitstun extensions > 0
        float g_P1IndicationOffsetX  = -49.0f;
        float g_P1IndicationOffsetY  = 75.0f;
        float g_P2IndicationOffsetX  = 46.0f;
        float g_P2IndicationOffsetY  = 75.0f;

        struct PopupState {
            HitIndicationType type = HitIndicationType::NONE;
            uint64_t triggerFrame = 0;
            uint32_t durationFrames = 75; // 1.25s
        };

        static PopupState s_Popups[3]; // Index 1 = P1, Index 2 = P2

        void TriggerHitIndication(int playerIdx, HitIndicationType type) {
            if (!g_ShowHitIndicationHUD) return;
            if (playerIdx < 1 || playerIdx > 2 || type == HitIndicationType::NONE) return;

            s_Popups[playerIdx].type = type;
            s_Popups[playerIdx].triggerFrame = g_CurrentFrame;
            s_Popups[playerIdx].durationFrames = 75;

            const char* typeStr = (type == HitIndicationType::COUNTER_HIT) ? "COUNTER HIT" :
                                  (type == HitIndicationType::TRADE_HIT)   ? "TRADE HIT" :
                                  (type == HitIndicationType::FATAL_PUNISH) ? "FATAL PUNISH" : "UNKNOWN";
            LogTrace("[HIT INDICATION | F%llu] Triggered '%s' for Player %d (Duration: %u frames)",
                     g_CurrentFrame, typeStr, playerIdx, s_Popups[playerIdx].durationFrames);
        }

        bool GetPlayerPopup(int playerIdx, HitIndicationType* outType, float* outAlpha, float* outSlideProgress) {
            if (!IsMatchActive()) return false;
            if (playerIdx < 1 || playerIdx > 2) return false;
            const PopupState& pop = s_Popups[playerIdx];
            if (pop.type == HitIndicationType::NONE || pop.triggerFrame == 0) return false;

            if (g_CurrentFrame < pop.triggerFrame) return false;
            uint64_t elapsed = g_CurrentFrame - pop.triggerFrame;
            if (elapsed >= pop.durationFrames) return false;

            if (outType) *outType = pop.type;

            // Opacity calculation:
            // Frames 0..55: 1.0 (Full opacity)
            // Frames 55..75: Linear fade from 1.0 to 0.0
            float alpha = 1.0f;
            if (elapsed > 55) {
                alpha = 1.0f - ((float)(elapsed - 55) / 20.0f);
                if (alpha < 0.0f) alpha = 0.0f;
            }
            if (outAlpha) *outAlpha = alpha;

            // Slide progress for entrance pop:
            // Frames 0..6: 0.0 -> 1.0
            float slide = 1.0f;
            if (elapsed < 6) {
                slide = (float)elapsed / 6.0f;
            }
            if (outSlideProgress) *outSlideProgress = slide;

            return true;
        }

        void Reset() {
            memset(s_Popups, 0, sizeof(s_Popups));
        }
    }

    bool IsSoulStealActiveAndHit() {
        if (!MK9TradeFix::IsMatchActive()) return false;

        for (int attacker = 1; attacker <= 2; attacker++) {
            int victim = (attacker == 1) ? 2 : 1;

            const char* atkAttack = s_PlayerAnim[attacker].activeAttackName;
            const char* atkCurr   = s_PlayerAnim[attacker].currentAnimName;
            const char* vicCurr   = s_PlayerAnim[victim].currentAnimName;

            bool isSoulStealAtk = (atkAttack[0] != '\0' && (strstr(atkAttack, "Soul_Steal") || strstr(atkAttack, "soul_steal"))) ||
                                  (atkCurr[0] != '\0' && (strstr(atkCurr, "Soul_Steal") || strstr(atkCurr, "soul_steal")));

            void* pAtkChar = (attacker == 1) ? GetP1Char() : GetP2Char();
            void* pVicChar = (victim == 1) ? GetP1Char() : GetP2Char();
            if (!pAtkChar || !pVicChar) continue;

            uint32_t atkAction = 0;
            uint32_t vicAction = 0;
            __try {
                atkAction = *(uint32_t*)((uintptr_t)pAtkChar + 0x1D4);
                vicAction = *(uint32_t*)((uintptr_t)pVicChar + 0x1D4);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                continue;
            }

            // Attacker must be in active move execution (0x5200, 0x5300, etc.)
            bool atkActive = (atkAction == 0x5200 || atkAction == 0x5300 || atkAction == 0x5208 ||
                              atkAction == 0x4203 || atkAction == 0x5400);

            if (isSoulStealAtk) {
                // If attacker entered Miss/Whiff animation, move did not hit
                if (atkCurr[0] != '\0' && (strstr(atkCurr, "Miss") || strstr(atkCurr, "miss"))) {
                    continue;
                }

                bool vicInSoulStealReaction = (vicCurr[0] != '\0' && (strstr(vicCurr, "Soul_Steal") || strstr(vicCurr, "soul_steal")));
                bool vicHit = vicInSoulStealReaction || (vicAction == 0x4600 || vicAction == 0x460D);

                if (atkActive && vicHit) {
                    return true;
                }
            }

            // Fallback: Check attacker's PlayerInfo script function record
            if (atkActive && (vicAction == 0x4600 || vicAction == 0x460D)) {
                __try {
                    void* pInfo = *(void**)((uintptr_t)pAtkChar + 0x7638);
                    if (pInfo && (uintptr_t)pInfo > 0x00400000) {
                        uint32_t curr = *(uint32_t*)((uintptr_t)pInfo + 0x574);
                        if (curr == 0) curr = *(uint32_t*)((uintptr_t)pInfo + 0x570);
                        if (curr > 0) {
                            uintptr_t pRec = (uintptr_t)pInfo + 0x48 + (curr - 1) * 0x2C;
                            uint32_t recRx = *(uint32_t*)(pRec + 0x18);
                            // 0xD8 = p_shangtsung_soul_steal_ex, 0xD9 = p_shangtsung_soul_steal, 0xDB = soul_steal_hit_ex, 0xDC = soul_steal_hit
                            if (recRx == 0xD8 || recRx == 0xD9 || recRx == 0xDB || recRx == 0xDC) {
                                return true;
                            }
                        }
                    }
                } __except (EXCEPTION_EXECUTE_HANDLER) {}
            }
        }

        return false;
    }
}

