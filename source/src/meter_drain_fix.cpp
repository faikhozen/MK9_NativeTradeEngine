#include "extra_gameplay.hpp"
#include "meter_drain_fix.hpp"
#include "frame_data.hpp"
#include "cancel_detector.hpp"
#include "engine_common.hpp"
#include "simulation.hpp"
#include "combat_state.hpp"
#include "profiler.hpp"
#include "minhook/include/MinHook.h"
#include <windows.h>
#include <intrin.h>
#include <cstdio>
#include <cstring>
#include <cmath>

namespace MK9TradeFix {
    namespace MeterDrainFix {

        static bool g_MeterDrainFixEnabled = true;
        static uint32_t s_ProtectedThreads = 0;
        static uint32_t s_AbsorbedHits = 0;

        // Custom Super Meter Costs Configuration
        bool g_CustomMeterCostsEnabled = false;
        float g_CustomEXCost = 0.333333f; // 33.3% (1.00 bar)
        float g_CustomBreakerCost = 0.66667f; // 66.7% (2.00 bars)
        float g_CustomXRayCost = 1.00f; // 100.0% (3.00 bars)
        bool g_ShowMeterPercentHUD = false;
        float g_P1MeterHUDOffsetX = 0.0f;
        float g_P1MeterHUDOffsetY = 0.0f;
        float g_P2MeterHUDOffsetX = 0.0f;
        float g_P2MeterHUDOffsetY = 0.0f;

        // Combat Meter Gains
        bool  g_EnableAttackerHitMeterGain      = true;
        float g_AttackerHitMeterRewardNormals   = 0.008f; // +0.8% per normal landed strike
        float g_AttackerHitMeterRewardSpecials  = 0.000f; // 0.0% per special landed strike
        float g_AttackerHitMeterReward          = 0.008f;
        int   g_AttackerHitMeterMoveFilter      = 1;

        bool  g_EnableDefenderBlockMeterGain    = false;
        float g_DefenderBlockMeterRewardNormals  = 0.015f; // +1.5% per normal blocked strike
        float g_DefenderBlockMeterRewardSpecials = 0.015f; // +1.5% per special blocked strike
        float g_DefenderBlockMeterReward        = 0.015f;
        int   g_DefenderBlockMeterMoveFilter    = 0;

        bool  g_EnableDefenderHitMeterGain      = true;
        float g_DefenderHitMeterRewardNormals   = 0.008f; // +0.8% per normal hit taken
        float g_DefenderHitMeterRewardSpecials  = 0.000f; // 0.0% per special hit taken
        float g_DefenderHitMeterReward          = 0.008f;
        int   g_DefenderHitMeterMoveFilter      = 1;

        // Combat Meter Expenditure
        bool  g_EnableSpecialMoveMeterCost       = false;
        float g_SpecialMoveMeterCost             = 0.250f; // 25.0% (-0.250 per special)
        bool  g_EnableNormalToSpecialCancelCost  = false;
        float g_NormalToSpecialCancelCost        = 0.100f; // default 10.0%
        bool  g_EnableNormalToEXCancelCost       = false;
        float g_NormalToEXCancelCost             = 0.100f; // default 10.0% (+0.30 bar extra)
        bool  g_EnableNormalToXRayCancelCost     = false;
        float g_NormalToXRayCancelCost           = 0.100f; // default 10.0% (+0.30 bar extra)
        bool  g_EnableNormalToDashCancelCost     = false;
        float g_NormalToDashCancelCost           = 0.100f; // default 10.0%
        // Practice Super Meter Mode (NORMAL = 0, FULL = 1, REFILL = 2)
        SuperMeterPracticeMode g_SuperMeterPracticeMode = SuperMeterPracticeMode::NORMAL;
        static uint64_t s_LastAttackFrame[3] = { 0, 0, 0 };

        bool CheckMoveFilterMatch(int filter, bool isSpecial) {
            if (filter == 0) return true; // Both (All Moves)
            if (filter == 1) return !isSpecial; // Normals Only
            if (filter == 2) return isSpecial;  // Specials Only
            return true;
        }

        static float g_EffectiveEXThreshold = 0.25f;
        static float g_EffectiveBreakerThreshold = 0.666670978f;
        static float g_EffectiveXRayThreshold = 1.0f;
        static thread_local bool s_InsideXferMove = false;

        struct EXMoveTracker {
            uint64_t startFrame;
            void*    pChar;
            void*    pMoveInfo;
            float    meterCost;
            bool     isArmored;
            bool     isActive;
            bool     hitAbsorbed;
            bool     isPendingExecution;
        };

        static EXMoveTracker s_EXTracker[3] = {};

        bool IsEXMoveActive(int plr) {
            if (plr < 1 || plr > 2) return false;
            return s_EXTracker[plr].isActive && (g_CurrentFrame <= s_EXTracker[plr].startFrame + 75);
        }

        struct PlayerExpenditureState {
            uint32_t lastAction = 0;
            int      lastScriptFunc = -1;
            uint32_t lastMoveCategory = 0xFFFFFFFF;
            bool     inNormal = false;
            uint64_t lastNormalFrame = 0;
            int      normalScriptFunc = -1;
            bool     inSpecial = false;
            uint64_t lastSpecialDeductedFrame = 0;
            int      lastSpecialScriptFunc = -1;
            bool     inDash = false;
        };
        static PlayerExpenditureState s_ExpendState[3] = {};

        typedef void (__thiscall *tAddSuperMeter)(void* pPlayerInfo, float deltaMeter);
        static tAddSuperMeter fnAddSuperMeter = (tAddSuperMeter)0x0089CE40;
        static tAddSuperMeter oAddSuperMeter = nullptr;

        static void __fastcall hkAddSuperMeter(void* pPlayerInfo, void* edxUnused, float deltaMeter) {
            if (!oAddSuperMeter) return;

            if (g_CustomMeterCostsEnabled && deltaMeter < 0.0f) {
                if (!s_InsideXferMove) {
                    // Only intercept external/script calls (e.g. Breaker reaction)
                    // Kombo Breaker deduction: vanilla passes around -0.66667f
                    if (fabsf(deltaMeter - (-0.66667f)) < 0.05f || (deltaMeter <= -0.60f && deltaMeter >= -0.75f)) {
                        float orig = deltaMeter;
                        deltaMeter = -g_CustomBreakerCost;
                        LogTrace(">>> [CUSTOM METER] Intercepted Breaker: replaced %.4f with -%.4f (-%.1f%%) <<<",
                                 orig, g_CustomBreakerCost, g_CustomBreakerCost * 100.0f);
                    }
                    // External vanilla EX deduction from script: around -0.33333f
                    else if (fabsf(deltaMeter - (-0.33333f)) < 0.05f) {
                        float orig = deltaMeter;
                        deltaMeter = -g_CustomEXCost;
                        LogTrace(">>> [CUSTOM METER] Intercepted EX Script: replaced %.4f with -%.4f (-%.1f%%) <<<",
                                 orig, g_CustomEXCost, g_CustomEXCost * 100.0f);
                    }
                    // External vanilla X-Ray deduction from script: around -1.0f
                    else if (fabsf(deltaMeter - (-1.0f)) < 0.05f) {
                        float orig = deltaMeter;
                        deltaMeter = -g_CustomXRayCost;
                        LogTrace(">>> [CUSTOM METER] Intercepted X-Ray Script: replaced %.4f with -%.4f (-%.1f%%) <<<",
                                 orig, g_CustomXRayCost, g_CustomXRayCost * 100.0f);
                    }
                } else {
                    LogTrace(">>> [CUSTOM METER] AddSuperMeter (from XferMove): deducting %.4f (%.1f%%) <<<",
                             deltaMeter, -deltaMeter * 100.0f);
                }
            }

            oAddSuperMeter(pPlayerInfo, deltaMeter);
        }

        void SetEnabled(bool enabled) {
            g_MeterDrainFixEnabled = enabled;
            LogTrace(">>> [METER DRAIN FIX] Subsystem: %s <<<", enabled ? "ENABLED" : "DISABLED");
        }

        bool IsEnabled() {
            return g_MeterDrainFixEnabled;
        }

        void Toggle() {
            SetEnabled(!g_MeterDrainFixEnabled);
        }

        void SetCustomMeterCostsEnabled(bool enabled) {
            g_CustomMeterCostsEnabled = enabled;
            UpdateEffectiveThresholds();
            LogTrace(">>> [CUSTOM METER] Subsystem: %s (EX: %.1f%%, Breaker: %.1f%%, X-Ray: %.1f%%) <<<",
                     enabled ? "ENABLED" : "DISABLED", g_CustomEXCost * 100.0f, g_CustomBreakerCost * 100.0f, g_CustomXRayCost * 100.0f);
        }

        bool IsCustomMeterCostsEnabled() {
            return g_CustomMeterCostsEnabled;
        }

        void SetCustomEXCost(float cost) {
            if (cost < 0.01f) cost = 0.01f;
            if (cost > 1.0f) cost = 1.0f;
            g_CustomEXCost = cost;
            UpdateEffectiveThresholds();
            LogTrace("[CUSTOM METER] Set EX Cost to %.1f%% (%.4f)", cost * 100.0f, cost);
        }

        float GetCustomEXCost() {
            return g_CustomEXCost;
        }

        void SetCustomBreakerCost(float cost) {
            if (cost < 0.01f) cost = 0.01f;
            if (cost > 1.0f) cost = 1.0f;
            g_CustomBreakerCost = cost;
            UpdateEffectiveThresholds();
            LogTrace("[CUSTOM METER] Set Breaker Cost to %.1f%% (%.4f)", cost * 100.0f, cost);
        }

        float GetCustomBreakerCost() {
            return g_CustomBreakerCost;
        }

        void SetCustomXRayCost(float cost) {
            if (cost < 0.01f) cost = 0.01f;
            if (cost > 1.0f) cost = 1.0f;
            g_CustomXRayCost = cost;
            UpdateEffectiveThresholds();
            LogTrace("[CUSTOM METER] Set X-Ray Cost to %.1f%% (%.4f)", cost * 100.0f, cost);
        }

        float GetCustomXRayCost() {
            return g_CustomXRayCost;
        }

        void UpdateEffectiveThresholds() {
            if (g_CustomMeterCostsEnabled) {
                // Generous 0.005f (0.5%) tolerance margin ensures 80% meter passes 0.80 threshold
                g_EffectiveEXThreshold = (g_CustomEXCost > 0.005f) ? (g_CustomEXCost - 0.005f) : 0.001f;
                g_EffectiveBreakerThreshold = (g_CustomBreakerCost > 0.005f) ? (g_CustomBreakerCost - 0.005f) : 0.001f;
                g_EffectiveXRayThreshold = (g_CustomXRayCost > 0.005f) ? (g_CustomXRayCost - 0.005f) : 0.001f;
            } else {
                g_EffectiveEXThreshold = 0.333333f;
                g_EffectiveBreakerThreshold = 0.666670978f;
                g_EffectiveXRayThreshold = 1.0f;
            }
        }

        float GetPlayerMeter(int plr) {
            if (plr < 1 || plr > 2) return -1.0f;
            void* pChar = (plr == 1) ? GetP1Char() : GetP2Char();
            void* pPlayerInfo = nullptr;
            if (pChar && (uintptr_t)pChar > 0x01000000) {
                __try { pPlayerInfo = *(void**)((uintptr_t)pChar + 0x7638); } __except (EXCEPTION_EXECUTE_HANDLER) {}
            }
            if (!pPlayerInfo || (uintptr_t)pPlayerInfo < 0x00400000) {
                pPlayerInfo = (plr == 1) ? (void*)0x00E89D7C : (void*)0x00E8AA08;
            }
            if (pPlayerInfo && (uintptr_t)pPlayerInfo > 0x00400000) {
                __try {
                    float m = *(float*)((uintptr_t)pPlayerInfo + 0xC14);
                    if (m < 0.0f) m = 0.0f;
                    if (m > 1.0f) m = 1.0f;
                    return m;
                } __except (EXCEPTION_EXECUTE_HANDLER) {}
            }
            return -1.0f;
        }

        void SetPlayerMeter(int plr, float amount) {
            if (plr < 1 || plr > 2) return;
            void* pChar = (plr == 1) ? GetP1Char() : GetP2Char();
            void* pPlayerInfo = nullptr;
            if (pChar && (uintptr_t)pChar > 0x01000000) {
                __try { pPlayerInfo = *(void**)((uintptr_t)pChar + 0x7638); } __except (EXCEPTION_EXECUTE_HANDLER) {}
            }
            if (!pPlayerInfo || (uintptr_t)pPlayerInfo < 0x00400000) {
                pPlayerInfo = (plr == 1) ? (void*)0x00E89D7C : (void*)0x00E8AA08;
            }
            if (pPlayerInfo && (uintptr_t)pPlayerInfo > 0x00400000) {
                __try {
                    if (amount < 0.0f) amount = 0.0f;
                    if (amount > 1.0f) amount = 1.0f;
                    *(float*)((uintptr_t)pPlayerInfo + 0xC14) = amount;
                    *(float*)((uintptr_t)pPlayerInfo + 0xC18) = amount;
                    if (oAddSuperMeter) {
                        oAddSuperMeter(pPlayerInfo, 0.0f);
                    } else if (fnAddSuperMeter) {
                        fnAddSuperMeter(pPlayerInfo, 0.0f);
                    }
                } __except (EXCEPTION_EXECUTE_HANDLER) {}
            }
        }

        void AwardPlayerMeter(int plr, float amount) {
            if (plr < 1 || plr > 2 || amount <= 0.0001f) return;
            void* pChar = (plr == 1) ? GetP1Char() : GetP2Char();
            if (!pChar || (uintptr_t)pChar < 0x01000000) return;
            __try {
                void* pPlayerInfo = *(void**)((uintptr_t)pChar + 0x7638);
                if (pPlayerInfo && (uintptr_t)pPlayerInfo > 0x00400000) {
                    float oldM = *(float*)((uintptr_t)pPlayerInfo + 0xC14);
                    if (fnAddSuperMeter) {
                        fnAddSuperMeter(pPlayerInfo, amount);
                    } else {
                        *(float*)((uintptr_t)pPlayerInfo + 0xC14) += amount;
                    }
                    float cur = *(float*)((uintptr_t)pPlayerInfo + 0xC14);
                    if (cur > 1.0f) {
                        *(float*)((uintptr_t)pPlayerInfo + 0xC14) = 1.0f;
                        cur = 1.0f;
                    } else if (cur < 0.0f) {
                        *(float*)((uintptr_t)pPlayerInfo + 0xC14) = 0.0f;
                        cur = 0.0f;
                    }
                    LogTrace("[METER REWARD | F%llu] Awarded +%.3f meter to P%d (Meter: %.3f -> %.3f | %.1f%% of 3 bars)",
                             g_CurrentFrame, amount, plr, oldM, cur, cur * 100.0f);
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
        }

        void DeductPlayerMeter(int plr, float amount) {
            if (plr < 1 || plr > 2 || amount <= 0.0001f) return;
            void* pChar = (plr == 1) ? GetP1Char() : GetP2Char();
            if (!pChar || (uintptr_t)pChar < 0x01000000) return;
            __try {
                void* pPlayerInfo = *(void**)((uintptr_t)pChar + 0x7638);
                if (pPlayerInfo && (uintptr_t)pPlayerInfo > 0x00400000) {
                    float oldM = *(float*)((uintptr_t)pPlayerInfo + 0xC14);
                    float cur = oldM - amount;
                    if (cur < 0.0f) cur = 0.0f;
                    *(float*)((uintptr_t)pPlayerInfo + 0xC14) = cur;
                    *(float*)((uintptr_t)pPlayerInfo + 0xC18) = cur;
                    if (oAddSuperMeter) {
                        oAddSuperMeter(pPlayerInfo, 0.0f);
                    } else if (fnAddSuperMeter) {
                        fnAddSuperMeter(pPlayerInfo, 0.0f);
                    }
                    LogTrace("[METER EXPENDITURE | F%llu] Deducted -%.3f meter from P%d (Meter: %.3f -> %.3f | %.1f%% of 3 bars)",
                             g_CurrentFrame, amount, plr, oldM, cur, cur * 100.0f);
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
        }

        uint32_t GetProtectedThreadCount() {
            return s_ProtectedThreads;
        }

        uint32_t GetAbsorbedHitCount() {
            return s_AbsorbedHits;
        }

        void ResetCounters() {
            s_ProtectedThreads = 0;
            s_AbsorbedHits = 0;
            memset(s_EXTracker, 0, sizeof(s_EXTracker));
        }

        static int GetPlayerNum(void* pChar) {
            if (!pChar) return 1;
            if (pChar == GetP1Char()) return 1;
            if (pChar == GetP2Char()) return 2;
            int num = GetPlayerNumber(pChar);
            if (num != 0) return num;
            return 1;
        }

        static bool IsSafeAscii(const char* str) {
            if (!str || (uintptr_t)str < 0x00400000 || (uintptr_t)str > 0x7FFF0000) return false;
            __try {
                for (int i = 0; i < 64; ++i) {
                    char c = str[i];
                    if (c == '\0') return i >= 2;
                    if ((unsigned char)c < 0x20 || (unsigned char)c > 0x7E) return false;
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
            return false;
        }

        static bool IsMoveUnarmoredProjectile(void* pChar, void* pMoveInfo) {
            if (!pChar || !pMoveInfo) return false;

            __try {
                uintptr_t pkg = *(uintptr_t*)((uintptr_t)pChar + 0x2F84);
                if (!pkg || pkg < 0x00400000) {
                    pkg = *(uintptr_t*)((uintptr_t)pChar + 0x7624);
                }
                if (!pkg || pkg < 0x00400000) return false;

                uint32_t fnCount = *(uint32_t*)(pkg + 0x20);
                uintptr_t pFnTable = *(uintptr_t*)(pkg + 0x8C);
                if (!pFnTable) pFnTable = *(uintptr_t*)(pkg + 0x94);

                if (pFnTable && fnCount > 0 && fnCount < 3000) {
                    uint32_t funcIdx = *(uint32_t*)((uintptr_t)pMoveInfo + 0x34) & 0x7FFFFFFF;
                    uintptr_t pFnEntry = 0;
                    if (funcIdx > 0 && funcIdx <= fnCount) {
                        pFnEntry = *(uintptr_t*)(pFnTable - 4 + funcIdx * 4);
                    }

                    if (pFnEntry && pFnEntry > 0x00400000) {
                        uint32_t propType = *(uint32_t*)(pFnEntry + 0x20);
                        // Property 19 = dist_fireball (unarmored projectile)
                        if (propType == 19) {
                            return true;
                        }
                        // Property 30 = armor_attack or 27 = wakeup_attack
                        if (propType == 30 || propType == 27) {
                            return false;
                        }

                        const char* sym1 = *(const char**)(pFnEntry + 0x00);
                        const char* sym2 = *(const char**)(pFnEntry + 0x18);
                        const char* sym = IsSafeAscii(sym1) ? sym1 : (IsSafeAscii(sym2) ? sym2 : nullptr);

                        if (sym) {
                            // Known armored specials
                            if (strstr(sym, "teleport") || strstr(sym, "Teleport") ||
                                strstr(sym, "slide") || strstr(sym, "Slide") ||
                                strstr(sym, "shadow") || strstr(sym, "Shadow") ||
                                strstr(sym, "punch") || strstr(sym, "Punch") ||
                                strstr(sym, "spin") || strstr(sym, "Spin") ||
                                strstr(sym, "shoulder") || strstr(sym, "Shoulder") ||
                                strstr(sym, "kick") || strstr(sym, "Kick") ||
                                strstr(sym, "torpedo") || strstr(sym, "Torpedo")) {
                                return false;
                            }

                            // Known unarmored projectiles
                            if (strstr(sym, "fireball") || strstr(sym, "Fireball") ||
                                strstr(sym, "forceball") || strstr(sym, "Forceball") ||
                                strstr(sym, "spear") || strstr(sym, "Spear") ||
                                strstr(sym, "projectile") || strstr(sym, "Projectile") ||
                                strstr(sym, "missile") || strstr(sym, "Missile") ||
                                strstr(sym, "iceball") || strstr(sym, "ice_ball") ||
                                strstr(sym, "acid") || strstr(sym, "bomb") ||
                                strstr(sym, "knife") || strstr(sym, "net")) {
                                return true;
                            }
                        }
                    }
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {}

            return false;
        }

        static uint64_t s_LastXferDeductionFrame[3] = { 0, 0, 0 };

        static void ClassifyMoveXfer(void* pChar, void* pMoveInfo, float meterCost, uint32_t param2, int plr, bool& outIsEX, bool& outIsXRay, bool& outIsSpecial) {
            float originalStaticCost = 0.0f;
            uint32_t moveDescType = 0;
            if (pMoveInfo && (uintptr_t)pMoveInfo > 0x00400000 && (uintptr_t)pMoveInfo < 0x7FFF0000) {
                __try {
                    originalStaticCost = *(float*)((uintptr_t)pMoveInfo + 0x98);
                    moveDescType = *(uint32_t*)((uintptr_t)pMoveInfo + 0x14);
                } __except (EXCEPTION_EXECUTE_HANDLER) {}
            }

            uint32_t flags2EC = 0;
            int actionID = -1;
            uint32_t moveCategory = 0xFFFFFFFF;
            uint32_t recAtkType = 0;
            uint32_t moveType = 0;
            uint32_t actionState = 0;

            if (pChar && (uintptr_t)pChar > 0x01000000) {
                __try {
                    flags2EC = *(uint32_t*)((uintptr_t)pChar + 0x2EC);
                    actionID = *(int*)((uintptr_t)pChar + 0x2E0);
                    actionState = *(uint32_t*)((uintptr_t)pChar + 0x1D4);
                    void* pInfo = *(void**)((uintptr_t)pChar + 0x7638);
                    if (pInfo && (uintptr_t)pInfo >= 0x00400000 && (uintptr_t)pInfo < 0x7FFF0000) {
                        uint32_t curr = *(uint32_t*)((uintptr_t)pInfo + 0x574);
                        if (curr == 0) curr = *(uint32_t*)((uintptr_t)pInfo + 0x570);
                        if (curr > 0) {
                            uintptr_t pRec = (uintptr_t)pInfo + 0x48 + (curr - 1) * 0x2C;
                            moveCategory = *(uint32_t*)(pRec + 0x08);
                            recAtkType   = *(uint32_t*)(pRec + 0x20);
                        }
                    }
                    uintptr_t pActionDef = *(uintptr_t*)((uintptr_t)pChar + 0x7A38);
                    if (pActionDef >= 0x00400000 && pActionDef != (uintptr_t)-1 && pActionDef < 0x7FFF0000) {
                        moveType = *(uint32_t*)(pActionDef + 0x44);
                    }
                } __except (EXCEPTION_EXECUTE_HANDLER) {}
            }

            bool hasSuperArmor = (flags2EC & 0x800000) != 0;
            bool isEngineNormalBit = (flags2EC & 0x400) != 0;

            outIsXRay = (originalStaticCost >= 0.60f) || (meterCost >= 0.60f) ||
                        (actionState == 0x5208 || actionState == 0x4203) ||
                        (moveDescType >= 3) || (moveType >= 3) || (actionID == 0x1C) || (recAtkType == 28);

            outIsEX = !outIsXRay && ((originalStaticCost >= 0.20f) || (meterCost >= 0.20f) ||
                                     (moveDescType == 2) || (moveType == 2) || (moveCategory == 5) ||
                                     (actionID == 0x1E || actionID == 0x1F) ||
                                     (hasSuperArmor && !isEngineNormalBit));

            if (g_SimSeqType == MACRO_SEQ_MODE18_NORMAL_CANCEL && plr == 1) {
                if (g_Mode18_ActiveFNum == 1) {
                    outIsXRay = false;
                    outIsEX = false;
                } else if (g_Mode18_ActiveFNum == 2) {
                    outIsXRay = false;
                    outIsEX = true;
                } else if (g_Mode18_ActiveFNum == 5) {
                    outIsXRay = true;
                    outIsEX = false;
                }
            }

            outIsSpecial = !outIsXRay && !outIsEX;
        }

        static void ExecuteXferMeterExpenditure(int plr, bool isEX, bool isXRay, bool isSpecial, float nativeDeducted, void* pMoveInfo, bool isArmored) {
            if (plr < 1 || plr > 2) return;
            if (s_LastXferDeductionFrame[plr] == g_CurrentFrame) return; // Prevent double execution in the same frame
            s_LastXferDeductionFrame[plr] = g_CurrentFrame;

            bool wasCancelFromNormal = CancelDetector::IsInNormal(plr) || 
                                       (s_ExpendState[plr].inNormal && (g_CurrentFrame >= s_ExpendState[plr].lastNormalFrame) && (g_CurrentFrame - s_ExpendState[plr].lastNormalFrame <= 45));

            if (isEX && !isXRay) {
                float baseCost = g_CustomMeterCostsEnabled ? g_CustomEXCost : 0.333333f;
                // 1. Guarantee Base EX Cost (1 full bar = 0.333333f) is deducted
                if (nativeDeducted < 0.15f) {
                    DeductPlayerMeter(plr, baseCost);
                    LogTrace("[METER EXPENDITURE | F%llu] P%d Initiated EX Move! Deducted base EX cost -%.1f%% meter (Cost: %.3f / 1.00 bar)",
                             g_CurrentFrame, plr, baseCost * 100.0f, baseCost);
                }
                // 2. If cancelled from normal attack, deduct added EX cancel cost
                if (wasCancelFromNormal && g_EnableNormalToEXCancelCost && g_NormalToEXCancelCost > 0.001f) {
                    DeductPlayerMeter(plr, g_NormalToEXCancelCost);
                    LogTrace("[METER EXPENDITURE | F%llu] P%d Normal Cancel -> EX Special! Added extra depletion -%.1f%% meter (Extra Cost: %.3f / %.2f bars)",
                             g_CurrentFrame, plr, g_NormalToEXCancelCost * 100.0f, g_NormalToEXCancelCost, g_NormalToEXCancelCost * 3.0f);
                }
                s_EXTracker[plr].meterCost = baseCost;
                float bars = baseCost * 3.0f;
                LogTrace(">>> [METER DRAIN FIX | Frame #%llu] P%d Initiated EX Move (MoveInfo: 0x%p, Depleted: %.2f Bar(s) [%.2f meter], Armored: %s) <<<",
                         g_CurrentFrame, plr, pMoveInfo, bars, baseCost, isArmored ? "YES" : "NO (Projectile)");
            } else if (isXRay) {
                float baseCost = g_CustomMeterCostsEnabled ? g_CustomXRayCost : 1.0f;
                // 1. Guarantee Base X-Ray Cost (3 full bars = 1.000f) is deducted
                if (nativeDeducted < 0.50f) {
                    DeductPlayerMeter(plr, baseCost);
                    LogTrace("[METER EXPENDITURE | F%llu] P%d Initiated X-Ray Super! Deducted base X-Ray cost -%.1f%% meter (Cost: %.3f / 3.00 bars)",
                             g_CurrentFrame, plr, baseCost * 100.0f, baseCost);
                }
                // 2. If cancelled from normal attack, deduct added X-Ray cancel cost
                if (wasCancelFromNormal && g_EnableNormalToXRayCancelCost && g_NormalToXRayCancelCost > 0.001f) {
                    DeductPlayerMeter(plr, g_NormalToXRayCancelCost);
                    LogTrace("[METER EXPENDITURE | F%llu] P%d Normal Cancel -> X-Ray Super! Added extra depletion -%.1f%% meter (Extra Cost: %.3f / %.2f bars)",
                             g_CurrentFrame, plr, g_NormalToXRayCancelCost * 100.0f, g_NormalToXRayCancelCost, g_NormalToXRayCancelCost * 3.0f);
                }
                s_EXTracker[plr].meterCost = baseCost;
                float bars = baseCost * 3.0f;
                LogTrace(">>> [METER DRAIN FIX | Frame #%llu] P%d Initiated X-RAY MOVE (MoveInfo: 0x%p, Depleted: %.2f Bar(s) [%.2f meter], Armored: %s) <<<",
                         g_CurrentFrame, plr, pMoveInfo, bars, baseCost, isArmored ? "YES" : "NO (Projectile)");
            } else if (isSpecial) {
                if (wasCancelFromNormal) {
                    if (g_EnableNormalToSpecialCancelCost && g_NormalToSpecialCancelCost > 0.001f) {
                        DeductPlayerMeter(plr, g_NormalToSpecialCancelCost);
                        LogTrace("[METER EXPENDITURE | F%llu] P%d Normal Cancel -> Special Move! Added extra depletion -%.1f%% meter (Extra Cost: %.3f / %.2f bars)",
                                 g_CurrentFrame, plr, g_NormalToSpecialCancelCost * 100.0f, g_NormalToSpecialCancelCost, g_NormalToSpecialCancelCost * 3.0f);
                    }
                } else {
                    if (g_EnableSpecialMoveMeterCost && g_SpecialMoveMeterCost > 0.001f) {
                        DeductPlayerMeter(plr, g_SpecialMoveMeterCost);
                        LogTrace("[METER EXPENDITURE | F%llu] P%d Raw Special Move! Deducted -%.1f%% meter (Cost: %.3f)",
                                 g_CurrentFrame, plr, g_SpecialMoveMeterCost * 100.0f, g_SpecialMoveMeterCost);
                    }
                }
            }

            s_ExpendState[plr].lastSpecialDeductedFrame = g_CurrentFrame;
            s_ExpendState[plr].inSpecial = true;
            s_ExpendState[plr].inNormal = false;
        }

        // ========================================================================
        // Hook: Character_XferToEXSpecialMove (0x008A76D0)
        // Authentic entry point when a fighter initiates an EX special move and
        // burns 1 bar of Super Meter. Regular specials NEVER call this.
        // ========================================================================
        typedef void (__fastcall *tXferToEXSpecialMove)(void* pChar, void* edxUnused, void* pMoveInfo, float meterCost, uint32_t param3);
        static tXferToEXSpecialMove oXferToEXSpecialMove = nullptr;

        static void __fastcall hkXferToEXSpecialMove_Impl(void* pChar, void* edxUnused, void* pMoveInfo, float meterCost, uint32_t param3) {
            if (!oXferToEXSpecialMove) return;
            if(ExtraGameplay::ShouldBlockRollSpecialTransfer(pChar)) return;

            int plr = GetPlayerNum(pChar);
            float meterBefore = GetPlayerMeter(plr);
            bool isArmored = !IsMoveUnarmoredProjectile(pChar, pMoveInfo);

            bool isEX = false, isXRay = false, isSpecial = false;
            ClassifyMoveXfer(pChar, pMoveInfo, meterCost, 0, plr, isEX, isXRay, isSpecial);

            if (plr == 1) {
                g_Mode18_SpecialTriggered = true;
                g_Mode18_SpecialTriggerFrame = g_CurrentFrame;
                g_Mode18_SpecialMoveInfo = pMoveInfo;
                g_Mode18_SpecialIsEX = isEX;
                g_Mode18_SpecialIsXRay = isXRay;
            }

            CancelDetector::OnSpecialXfer(plr, pMoveInfo, isEX, isXRay);

            if (isEX || isXRay) {
                s_EXTracker[plr].startFrame = g_CurrentFrame;
                s_EXTracker[plr].pChar = pChar;
                s_EXTracker[plr].pMoveInfo = pMoveInfo;
                s_EXTracker[plr].isArmored = isArmored;
                s_EXTracker[plr].isActive = true;
                s_EXTracker[plr].hitAbsorbed = false;
                s_EXTracker[plr].isPendingExecution = true;
            }

            if (g_CustomMeterCostsEnabled) {
                if (isXRay) meterCost = g_CustomXRayCost;
                else if (isEX) meterCost = g_CustomEXCost;
            }

            s_InsideXferMove = true;
            oXferToEXSpecialMove(pChar, edxUnused, pMoveInfo, meterCost, param3);
            s_InsideXferMove = false;

            float meterAfter = GetPlayerMeter(plr);
            float nativeDeducted = (meterBefore >= meterAfter) ? (meterBefore - meterAfter) : 0.0f;

            ExecuteXferMeterExpenditure(plr, isEX, isXRay, isSpecial, nativeDeducted, pMoveInfo, isArmored);
        }

        static void __fastcall hkXferToEXSpecialMove(void* pChar, void* edxUnused, void* pMoveInfo, float meterCost, uint32_t param3) {
            hkXferToEXSpecialMove_Impl(pChar, edxUnused, pMoveInfo, meterCost, param3);
        }

        void NotifyNormalAttack(int plr) {
            if (plr < 1 || plr > 2) return;
            s_ExpendState[plr].inNormal = true;
            s_ExpendState[plr].lastNormalFrame = g_CurrentFrame;
        }

        void NotifyActionState(int plr, uint32_t action) {
            if (plr < 1 || plr > 2) return;

            CancelDetector::OnActionState(plr, action);

            // Reset normal attack on neutral / movement (walk, jump, crouch) / block / stun / getup
            if (action < 0x2000 || (action >= 0x4000 && action <= 0x4FFF) || (action >= 0x6000 && action <= 0x62FF)) {
                s_ExpendState[plr].inNormal = false;
                s_ExpendState[plr].inDash = false;
                return;
            }

            // Normal Attack entry (0x5000..0x5FFF except 0x5208)
            if (action >= 0x5000 && action <= 0x5FFF && action != 0x5208) {
                s_ExpendState[plr].inNormal = true;
                s_ExpendState[plr].lastNormalFrame = g_CurrentFrame;
            }

            // Dash entry & Normal -> Dash Cancel
            bool isDash = (action >= 0x2010 && action <= 0x201F) || (action == 0x0300 || action == 0x0301);
            if (isDash) {
                if (!s_ExpendState[plr].inDash) {
                    if (s_ExpendState[plr].inNormal && (g_CurrentFrame >= s_ExpendState[plr].lastNormalFrame) && (g_CurrentFrame - s_ExpendState[plr].lastNormalFrame <= 45)) {
                        if (g_EnableNormalToDashCancelCost && g_NormalToDashCancelCost > 0.001f) {
                            DeductPlayerMeter(plr, g_NormalToDashCancelCost);
                            LogTrace("[METER EXPENDITURE | F%llu] P%d Normal Attack Cancel to Dash! Deducted -%.1f%% meter (Cost: %.3f)",
                                     g_CurrentFrame, plr, g_NormalToDashCancelCost * 100.0f, g_NormalToDashCancelCost);
                        }
                        s_ExpendState[plr].inNormal = false;
                    }
                    s_ExpendState[plr].inDash = true;
                }
            } else {
                s_ExpendState[plr].inDash = false;
            }
        }

        // ========================================================================
        // Hook: Character_XferToXRayMove (0x008A75F0)
        // Authentic entry point when a fighter initiates a Special Move, EX Move,
        // or X-Ray super move.
        // ========================================================================
        typedef void (__fastcall *tXferToXRayMove)(void* pChar, void* edxUnused, void* pMoveInfo, uint32_t param2, float meterCost);
        static tXferToXRayMove oXferToXRayMove = nullptr;

        static void __fastcall hkXferToXRayMove_Impl(void* pChar, void* edxUnused, void* pMoveInfo, uint32_t param2, float meterCost) {
            if (!oXferToXRayMove) return;
            if(ExtraGameplay::ShouldBlockRollSpecialTransfer(pChar)) return;

            int plr = GetPlayerNum(pChar);
            float meterBefore = GetPlayerMeter(plr);
            bool isArmored = !IsMoveUnarmoredProjectile(pChar, pMoveInfo);

            bool isEX = false, isXRay = false, isSpecial = false;
            ClassifyMoveXfer(pChar, pMoveInfo, meterCost, param2, plr, isEX, isXRay, isSpecial);

            if (plr == 1) {
                g_Mode18_SpecialTriggered = true;
                g_Mode18_SpecialTriggerFrame = g_CurrentFrame;
                g_Mode18_SpecialMoveInfo = pMoveInfo;
                g_Mode18_SpecialIsEX = isEX;
                g_Mode18_SpecialIsXRay = isXRay;
            }

            CancelDetector::OnSpecialXfer(plr, pMoveInfo, isEX, isXRay);

            if (isEX || isXRay) {
                s_EXTracker[plr].startFrame = g_CurrentFrame;
                s_EXTracker[plr].pChar = pChar;
                s_EXTracker[plr].pMoveInfo = pMoveInfo;
                s_EXTracker[plr].isArmored = isXRay || isArmored;
                s_EXTracker[plr].isActive = true;
                s_EXTracker[plr].hitAbsorbed = false;
                s_EXTracker[plr].isPendingExecution = true;
            }

            if (g_CustomMeterCostsEnabled) {
                if (isXRay) {
                    if (meterCost < 0.0f) meterCost = -g_CustomXRayCost;
                    else if (meterCost > 0.0f) meterCost = g_CustomXRayCost;
                } else if (isEX) {
                    if (meterCost < 0.0f) meterCost = -g_CustomEXCost;
                    else if (meterCost > 0.0f) meterCost = g_CustomEXCost;
                }
            }

            s_InsideXferMove = true;
            oXferToXRayMove(pChar, edxUnused, pMoveInfo, param2, meterCost);
            s_InsideXferMove = false;

            float meterAfter = GetPlayerMeter(plr);
            float nativeDeducted = (meterBefore >= meterAfter) ? (meterBefore - meterAfter) : 0.0f;

            ExecuteXferMeterExpenditure(plr, isEX, isXRay, isSpecial, nativeDeducted, pMoveInfo, isArmored);
        }

        static void __fastcall hkXferToXRayMove(void* pChar, void* edxUnused, void* pMoveInfo, uint32_t param2, float meterCost) {
            hkXferToXRayMove_Impl(pChar, edxUnused, pMoveInfo, param2, meterCost);
        }

        void RefundPendingEXMeter(int plr, const char* reason) {
            if (!g_MeterDrainFixEnabled || plr < 1 || plr > 2) return;
            if (!s_EXTracker[plr].isPendingExecution || s_EXTracker[plr].meterCost <= 0.001f) {
                s_EXTracker[plr].isPendingExecution = false;
                s_EXTracker[plr].isActive = false;
                s_EXTracker[plr].meterCost = 0.0f;
                return;
            }

            void* pChar = (plr == 1) ? GetP1Char() : GetP2Char();
            if (!pChar || (uintptr_t)pChar < 0x01000000) return;

            __try {
                void* pPlayerInfo = *(void**)((uintptr_t)pChar + 0x7638);
                if (pPlayerInfo && (uintptr_t)pPlayerInfo > 0x00400000) {
                    float refundAmount = s_EXTracker[plr].meterCost;
                    if (refundAmount > 0.001f) {
                        if (fnAddSuperMeter) {
                            fnAddSuperMeter(pPlayerInfo, refundAmount);
                        } else {
                            *(float*)((uintptr_t)pPlayerInfo + 0xC14) += refundAmount;
                        }

                        float refundBars = refundAmount * 3.0f;
                        LogTrace(">>> [METER DRAIN FIX | Frame #%llu] REFUNDED %.2f Bar(s) of Super Meter to P%d (%s) <<<",
                                 g_CurrentFrame, refundBars, plr, reason ? reason : "Reversal Overridden by Block");
                    }
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {}

            s_EXTracker[plr].isPendingExecution = false;
            s_EXTracker[plr].isActive = false;
            s_EXTracker[plr].meterCost = 0.0f;
        }

        void OnBlockOrStunCheck(void* pDefender, uint64_t frame, bool isBlocked) {
            if (!g_MeterDrainFixEnabled || !pDefender) return;
            int plr = GetPlayerNum(pDefender);
            if (plr < 1 || plr > 2) return;

            if (s_EXTracker[plr].isPendingExecution) {
                // If the defender took a blocked hit within the initiation window (Frame 0 to +3)
                if (frame >= s_EXTracker[plr].startFrame && frame <= s_EXTracker[plr].startFrame + 3) {
                    if (isBlocked) {
                        RefundPendingEXMeter(plr, "Reversal Overridden by Block Reaction 0x4F00 / Blockstun");
                    }
                } else if (frame > s_EXTracker[plr].startFrame + 3) {
                    // Window expired and move was not blocked on transition -> genuine move execution
                    s_EXTracker[plr].isPendingExecution = false;
                }
            }
        }

        void OnTick(void* p1, void* p2, uint64_t frame) {
            if (!g_MeterDrainFixEnabled) return;

            // Check P1 Block Reaction Override
            if (s_EXTracker[1].isPendingExecution && p1 && (uintptr_t)p1 > 0x01000000) {
                __try {
                    uint32_t p1_act = *(uint32_t*)((uintptr_t)p1 + 0x1D4);
                    if (p1_act == 0x4F00 && frame <= s_EXTracker[1].startFrame + 3) {
                        RefundPendingEXMeter(1, "Reversal Overridden by Block Reaction 0x4F00");
                    } else if (frame > s_EXTracker[1].startFrame + 3) {
                        s_EXTracker[1].isPendingExecution = false;
                    }
                } __except (EXCEPTION_EXECUTE_HANDLER) {}
            }

            // Check P2 Block Reaction Override
            if (s_EXTracker[2].isPendingExecution && p2 && (uintptr_t)p2 > 0x01000000) {
                __try {
                    uint32_t p2_act = *(uint32_t*)((uintptr_t)p2 + 0x1D4);
                    if (p2_act == 0x4F00 && frame <= s_EXTracker[2].startFrame + 3) {
                        RefundPendingEXMeter(2, "Reversal Overridden by Block Reaction 0x4F00");
                    } else if (frame > s_EXTracker[2].startFrame + 3) {
                        s_EXTracker[2].isPendingExecution = false;
                    }
                } __except (EXCEPTION_EXECUTE_HANDLER) {}
            }

            // Practice Super Meter Automation (FULL & REFILL)
            for (int p = 1; p <= 2; ++p) {
                void* pChar = (p == 1) ? p1 : p2;
                if (!pChar || (uintptr_t)pChar < 0x01000000) continue;

                __try {
                    uint32_t actionState = *(uint32_t*)((uintptr_t)pChar + 0x1D4);
                    bool isAttackingAction = (actionState >= 0x5000 && actionState <= 0x5FFF && actionState != 0x5208) ||
                                             (actionState >= 0x2000 && actionState <= 0x201F) ||
                                             (p == 1 ? g_P1_Lifecycle.inMove : g_P2_Lifecycle.inMove);
                    if (isAttackingAction) {
                        s_LastAttackFrame[p] = frame;
                    }

                    if (g_SuperMeterPracticeMode == SuperMeterPracticeMode::FULL) {
                        float curM = GetPlayerMeter(p);
                        if (curM >= 0.0f && curM < 0.999f) {
                            SetPlayerMeter(p, 1.0f);
                        }
                    } else if (g_SuperMeterPracticeMode == SuperMeterPracticeMode::REFILL) {
                        // 4 seconds = 240 frames at 60fps
                        if (frame >= s_LastAttackFrame[p] + 240) {
                            float curM = GetPlayerMeter(p);
                            if (curM >= 0.0f && curM < 0.999f) {
                                SetPlayerMeter(p, 1.0f);
                                LogTrace("[SUPER METER REFILL | F%llu] P%d idle without attacking for 4.0s -> Super meter refilled to 100%%", frame, p);
                            }
                        }
                    }
                } __except (EXCEPTION_EXECUTE_HANDLER) {}
            }

            // Combat Meter Expenditure Tracking (Raw Special, Normal->Special Cancel, Normal->Dash Cancel)
            for (int p = 1; p <= 2; ++p) {
                void* pChar = (p == 1) ? p1 : p2;
                if (!pChar || (uintptr_t)pChar < 0x01000000) continue;

                __try {
                    uint32_t actionState = *(uint32_t*)((uintptr_t)pChar + 0x1D4);
                    uintptr_t scriptPkg  = *(uintptr_t*)((uintptr_t)pChar + 0x2F84);
                    int       scriptFunc = *(int*)((uintptr_t)pChar + 0x2F88);
                    int       actionID   = *(int*)((uintptr_t)pChar + 0x2E0);
                    uint32_t  flags2EC   = *(uint32_t*)((uintptr_t)pChar + 0x2EC);

                    void* pInfo = *(void**)((uintptr_t)pChar + 0x7638);
                    uint32_t moveCategory = 0xFFFFFFFF;
                    uint32_t recAtkType = 0;
                    if (pInfo && (uintptr_t)pInfo >= 0x00400000 && (uintptr_t)pInfo < 0x7FFF0000) {
                        uint32_t curr = *(uint32_t*)((uintptr_t)pInfo + 0x574);
                        if (curr == 0) curr = *(uint32_t*)((uintptr_t)pInfo + 0x570);
                        if (curr > 0) {
                            uintptr_t pRec = (uintptr_t)pInfo + 0x48 + (curr - 1) * 0x2C;
                            moveCategory = *(uint32_t*)(pRec + 0x08);
                            recAtkType   = *(uint32_t*)(pRec + 0x20);
                        }
                    }

                    // 1. Check Neutral, Movement (Walk, Jump, Crouch), Defensive, or Stun Reset States
                    // All general movement (walk 0x0200, jump 0x0300, crouch 0x0100, landing 0x0400, block 0x0900/0x0A00) is < 0x2000
                    bool isMovementOrResetState = (actionState < 0x2000) ||
                                                  (actionState >= 0x4000 && actionState <= 0x4FFF) || // Hitstun / KD
                                                  (actionState >= 0x6000 && actionState <= 0x62FF);   // Getup / Floor

                    if (isMovementOrResetState) {
                        s_ExpendState[p].inNormal = false;
                        s_ExpendState[p].inSpecial = false;
                        s_ExpendState[p].inDash = false;
                        s_ExpendState[p].lastAction = actionState;
                        s_ExpendState[p].lastScriptFunc = scriptFunc;
                        s_ExpendState[p].lastMoveCategory = moveCategory;
                        if (frame > s_EXTracker[p].startFrame + 15) {
                            s_EXTracker[p].isActive = false;
                        }
                        continue;
                    }

                    // 2. Dash Detection & Normal -> Dash Cancel
                    bool isDash = (actionState >= 0x2000 && actionState <= 0x201F) || (recAtkType == 6);

                    if (isDash) {
                        if (!s_ExpendState[p].inDash) {
                            // Fresh Dash initiation!
                            if (s_ExpendState[p].inNormal && (frame >= s_ExpendState[p].lastNormalFrame) && (frame - s_ExpendState[p].lastNormalFrame <= 45)) {
                                if (g_EnableNormalToDashCancelCost && g_NormalToDashCancelCost > 0.001f) {
                                    DeductPlayerMeter(p, g_NormalToDashCancelCost);
                                    LogTrace("[METER EXPENDITURE | F%llu] P%d Dash Cancel from Normal! Deducted -%.1f%% meter",
                                             frame, p, g_NormalToDashCancelCost * 100.0f);
                                }
                                s_ExpendState[p].inNormal = false;
                            }
                            s_ExpendState[p].inDash = true;
                        }
                        s_ExpendState[p].inSpecial = false;
                        s_ExpendState[p].lastAction = actionState;
                        s_ExpendState[p].lastScriptFunc = scriptFunc;
                        s_ExpendState[p].lastMoveCategory = moveCategory;
                        continue;
                    } else {
                        s_ExpendState[p].inDash = false;
                    }

                    // 3. Attack Action States (0x5000..0x5FFF)
                    if (actionState >= 0x5000 && actionState <= 0x5FFF && actionState != 0x5208) {
                        bool isEngineSpecialBit = (flags2EC & 0x200) != 0;
                        bool isEngineNormalBit  = (flags2EC & 0x400) != 0;
                        bool hasSuperArmor      = (flags2EC & 0x800000) != 0;
                        bool isEXTrackerActive  = s_EXTracker[p].isActive && (frame <= s_EXTracker[p].startFrame + 75);

                        // STRICT SPECIAL MOVE ISOLATION:
                        // Normal attacks have Engine Normal Bit (0x400) set.
                        // Special attacks have Engine Special Bit (0x200) or moveCategory == 1.
                        // We must NEVER match recAtkType or anything when isEngineNormalBit is set.
                        bool isSpecial = !isEngineNormalBit && (moveCategory == 1 || isEngineSpecialBit);
                        bool isEXorXRay = isEXTrackerActive || hasSuperArmor || (moveCategory == 5) || (actionID == 0x1E) || (actionID == 0x1F);

                        if (isEXorXRay) {
                            // Active EX or X-Ray move: DO NOT treat as normal attack, DO NOT deduct standard special move cost
                            s_ExpendState[p].inSpecial = true;
                            s_ExpendState[p].inNormal = false;
                            s_ExpendState[p].lastSpecialDeductedFrame = frame;
                        } else if (isSpecial) {
                            // Raw Special Move or Normal -> Special Cancel
                            bool isNewSpecial = (!s_ExpendState[p].inSpecial) ||
                                                (scriptFunc != -1 && scriptFunc != s_ExpendState[p].lastSpecialScriptFunc) ||
                                                (actionState != s_ExpendState[p].lastAction);

                            if (isNewSpecial && (frame - s_ExpendState[p].lastSpecialDeductedFrame > 8)) {
                                bool wasCancelFromNormal = s_ExpendState[p].inNormal &&
                                                           (frame >= s_ExpendState[p].lastNormalFrame) &&
                                                           (frame - s_ExpendState[p].lastNormalFrame <= 45) &&
                                                           (scriptFunc == -1 || scriptFunc != s_ExpendState[p].normalScriptFunc);

                                if (wasCancelFromNormal) {
                                    if (g_EnableNormalToSpecialCancelCost && g_NormalToSpecialCancelCost > 0.001f) {
                                        DeductPlayerMeter(p, g_NormalToSpecialCancelCost);
                                        LogTrace("[METER EXPENDITURE | F%llu] P%d Normal->Special Cancel! Deducted -%.1f%% meter",
                                                 frame, p, g_NormalToSpecialCancelCost * 100.0f);
                                    }
                                } else {
                                    if (g_EnableSpecialMoveMeterCost && g_SpecialMoveMeterCost > 0.001f) {
                                        DeductPlayerMeter(p, g_SpecialMoveMeterCost);
                                        LogTrace("[METER EXPENDITURE | F%llu] P%d Raw Special Move! Deducted -%.1f%% meter (Cost: %.3f)",
                                                 frame, p, g_SpecialMoveMeterCost * 100.0f, g_SpecialMoveMeterCost);
                                    }
                                }
                                s_ExpendState[p].lastSpecialDeductedFrame = frame;
                                s_ExpendState[p].lastSpecialScriptFunc = scriptFunc;
                                s_ExpendState[p].inNormal = false;
                            }
                            s_ExpendState[p].inSpecial = true;
                        } else {
                            // Genuine Normal Attack (0x5000..0x5FFF)
                            s_ExpendState[p].inNormal = true;
                            s_ExpendState[p].lastNormalFrame = frame;
                            s_ExpendState[p].normalScriptFunc = scriptFunc;
                            s_ExpendState[p].inSpecial = false;
                        }
                    } else {
                        s_ExpendState[p].inSpecial = false;
                    }

                    s_ExpendState[p].lastAction = actionState;
                    s_ExpendState[p].lastScriptFunc = scriptFunc;
                    s_ExpendState[p].lastMoveCategory = moveCategory;
                } __except (EXCEPTION_EXECUTE_HANDLER) {}
            }
        }

        // ========================================================================
        // Hook: CheckAndExecuteBreaker (0x008955C0)
        // Evaluates Kombo Breaker input (Towards + Block) during hitstun reactions
        // and initiates breaker pushback reaction. Actual meter deduction is handled
        // downstream via AddSuperMeter (intercepted cleanly by hkAddSuperMeter).
        // ========================================================================
        typedef int (__thiscall *tCheckAndExecuteBreaker)(void* pChar, void* pBuffer1, void* pBuffer2);
        static tCheckAndExecuteBreaker oCheckAndExecuteBreaker = nullptr;

        static int __fastcall hkCheckAndExecuteBreaker(void* pChar, void* edxUnused, void* pBuffer1, void* pBuffer2) {
            if (!oCheckAndExecuteBreaker) return 0;
            int result = oCheckAndExecuteBreaker(pChar, pBuffer1, pBuffer2);
            if (result && g_CustomMeterCostsEnabled) {
                LogTrace(">>> [CUSTOM METER] Kombo Breaker Executed! (Configured Breaker Cost: %.1f%%) <<<",
                         g_CustomBreakerCost * 100.0f);
            }
            return result;
        }

        // ========================================================================
        // Codecaves for EX / X-Ray Move Meter Checks
        // 0x008A9702: MatchAndExecuteMove move command verification path 1
        // 0x008A98AF: MatchAndExecuteMove move command verification path 2
        // 0x008A9E0E: Script move command verification
        // ========================================================================
        static const float kXRayCostBoundary = 0.60f;
        static const uintptr_t RET_EX_MOVE_CHECK = 0x008A970A;
        void __declspec(naked) hkEXMoveCheckCave() {
            __asm {
                cmp g_CustomMeterCostsEnabled, 1
                jne orig_ex_cost
                movss xmm0, dword ptr [edi + 0x98]
                ucomiss xmm0, dword ptr ds:[0x00C681F4] // 0.0f
                lahf
                test ah, 0x44
                jp is_zero_cost
                ucomiss xmm0, ds:[kXRayCostBoundary]
                jae is_xray_cost
                movss xmm0, g_EffectiveEXThreshold
                jmp cost_done
            is_xray_cost:
                movss xmm0, g_EffectiveXRayThreshold
                jmp cost_done
            is_zero_cost:
                jmp cost_done
            orig_ex_cost:
                movss xmm0, dword ptr [edi + 0x98]
            cost_done:
                jmp RET_EX_MOVE_CHECK
            }
        }

        static const uintptr_t RET_EX_MOVE_CHECK_2 = 0x008A98B7;
        void __declspec(naked) hkEXMoveCheckCave2() {
            __asm {
                cmp g_CustomMeterCostsEnabled, 1
                jne orig_ex_cost2
                movss xmm0, dword ptr [edi + 0x98]
                ucomiss xmm0, dword ptr ds:[0x00C681F4] // 0.0f
                lahf
                test ah, 0x44
                jp is_zero_cost2
                ucomiss xmm0, ds:[kXRayCostBoundary]
                jae is_xray_cost2
                movss xmm0, g_EffectiveEXThreshold
                jmp cost_done2
            is_xray_cost2:
                movss xmm0, g_EffectiveXRayThreshold
                jmp cost_done2
            is_zero_cost2:
                jmp cost_done2
            orig_ex_cost2:
                movss xmm0, dword ptr [edi + 0x98]
            cost_done2:
                jmp RET_EX_MOVE_CHECK_2
            }
        }

        static const uintptr_t RET_EX_MOVE_SCRIPT = 0x008A9E16;
        void __declspec(naked) hkEXMoveScriptCave() {
            __asm {
                cmp g_CustomMeterCostsEnabled, 1
                jne orig_script_cost
                movss xmm0, dword ptr [ebp + 0x20]
                ucomiss xmm0, dword ptr ds:[0x00C681F4] // 0.0f
                lahf
                test ah, 0x44
                jp orig_script_cost
                ucomiss xmm0, ds:[kXRayCostBoundary]
                jae script_xray
                mov eax, g_EffectiveEXThreshold
                mov dword ptr [ebp + 0x20], eax
                jmp orig_script_cost
            script_xray:
                mov eax, g_EffectiveXRayThreshold
                mov dword ptr [ebp + 0x20], eax
            orig_script_cost:
                movss xmm0, dword ptr [esi + 0xC14]
                jmp RET_EX_MOVE_SCRIPT
            }
        }

        bool InstallHooks() {
            bool rollNormalReady=false,rollEXReady=false;
            if (MH_CreateHook((void*)0x008a76d0, (void*)hkXferToEXSpecialMove, (void**)&oXferToEXSpecialMove) == MH_OK) {
                rollEXReady=MH_EnableHook((void*)0x008a76d0)==MH_OK;
                LogTrace("[+] Special/Super Move Xfer Hooked (0x008A76D0)");
            }
            if (MH_CreateHook((void*)0x008a75f0, (void*)hkXferToXRayMove, (void**)&oXferToXRayMove) == MH_OK) {
                rollNormalReady=MH_EnableHook((void*)0x008a75f0)==MH_OK;
                LogTrace("[+] X-Ray Super Move Xfer Hooked (0x008A75F0)");
            }

            ExtraGameplay::g_RollTransferHooksReady=rollNormalReady && rollEXReady;

            // Hook AddSuperMeter (0x0089CE40) to handle exact meter deductions for Breaker, EX, and X-Ray
            if (MH_CreateHook((void*)0x0089ce40, (void*)hkAddSuperMeter, (void**)&oAddSuperMeter) == MH_OK) {
                MH_EnableHook((void*)0x0089ce40);
                LogTrace("[+] AddSuperMeter Hooked (0x0089CE40)");
            }

            // Patch Breaker threshold pointer (0x008955E3) to point to &g_EffectiveBreakerThreshold
            DWORD oldProt;
            if (VirtualProtect((void*)0x008955E3, 4, PAGE_EXECUTE_READWRITE, &oldProt)) {
                static float* pBreakerThreshPtr = &g_EffectiveBreakerThreshold;
                *(uint32_t*)0x008955E3 = (uint32_t)pBreakerThreshPtr;
                VirtualProtect((void*)0x008955E3, 4, oldProt, &oldProt);
                LogTrace("[+] Breaker Threshold Pointer Patched (0x008955E3 -> &g_EffectiveBreakerThreshold)");
            }

            // Hook CheckAndExecuteBreaker (0x008955C0)
            if (MH_CreateHook((void*)0x008955c0, (void*)hkCheckAndExecuteBreaker, (void**)&oCheckAndExecuteBreaker) == MH_OK) {
                MH_EnableHook((void*)0x008955c0);
                LogTrace("[+] Kombo Breaker Hooked (0x008955C0)");
            }

            // Patch X-Ray threshold check pointers (comparing against 1.0f [0x00C5A698] -> &g_EffectiveXRayThreshold)
            static const uintptr_t kXRayThresholdSites[] = {
                0x0088F0E7,
                0x0088F855,
                0x0088FD9A,
                0x00890571,
                0x008A972C,
                0x008A98D9,
                0x008A9A09,
                0x008A9B10,
                0x008A9C09,
            };
            static float* pXRayThreshPtr = &g_EffectiveXRayThreshold;
            for (uintptr_t site : kXRayThresholdSites) {
                if (VirtualProtect((void*)site, 4, PAGE_EXECUTE_READWRITE, &oldProt)) {
                    *(uint32_t*)site = (uint32_t)pXRayThreshPtr;
                    VirtualProtect((void*)site, 4, oldProt, &oldProt);
                    LogTrace("[+] X-Ray Threshold Pointer Patched (0x%08X -> &g_EffectiveXRayThreshold)", site);
                }
            }

            // Hook EX Move Check Cave (0x008A9702)
            if (VirtualProtect((void*)0x008A9702, 8, PAGE_EXECUTE_READWRITE, &oldProt)) {
                uint8_t patch[8];
                patch[0] = 0xE9;
                *(int32_t*)(patch + 1) = (int32_t)((uintptr_t)hkEXMoveCheckCave - (0x008A9702 + 5));
                patch[5] = 0x90;
                patch[6] = 0x90;
                patch[7] = 0x90;
                memcpy((void*)0x008A9702, patch, 8);
                VirtualProtect((void*)0x008A9702, 8, oldProt, &oldProt);
                LogTrace("[+] EX Move Command Check Hooked (0x008A9702)");
            }

            // Hook EX Move Check Cave 2 (0x008A98AF)
            if (VirtualProtect((void*)0x008A98AF, 8, PAGE_EXECUTE_READWRITE, &oldProt)) {
                uint8_t patch[8];
                patch[0] = 0xE9;
                *(int32_t*)(patch + 1) = (int32_t)((uintptr_t)hkEXMoveCheckCave2 - (0x008A98AF + 5));
                patch[5] = 0x90;
                patch[6] = 0x90;
                patch[7] = 0x90;
                memcpy((void*)0x008A98AF, patch, 8);
                VirtualProtect((void*)0x008A98AF, 8, oldProt, &oldProt);
                LogTrace("[+] EX Move Command Check 2 Hooked (0x008A98AF)");
            }

            // Hook EX Move Script Cave (0x008A9E0E)
            if (VirtualProtect((void*)0x008A9E0E, 8, PAGE_EXECUTE_READWRITE, &oldProt)) {
                uint8_t patch[8];
                patch[0] = 0xE9;
                *(int32_t*)(patch + 1) = (int32_t)((uintptr_t)hkEXMoveScriptCave - (0x008A9E0E + 5));
                patch[5] = 0x90;
                patch[6] = 0x90;
                patch[7] = 0x90;
                memcpy((void*)0x008A9E0E, patch, 8);
                VirtualProtect((void*)0x008A9E0E, 8, oldProt, &oldProt);
                LogTrace("[+] EX Move Script Check Hooked (0x008A9E0E)");
            }

            return true;
        }

        bool IsInEXStartup(void* pChar) {
            if (!g_MeterDrainFixEnabled || !pChar || (uintptr_t)pChar < 0x01000000) return false;

            __try {
                int plr = GetPlayerNum(pChar);
                if (plr < 1 || plr > 2) return false;

                if (s_EXTracker[plr].hitAbsorbed) {
                    return false;
                }

                uint32_t actionState = *(uint32_t*)((uintptr_t)pChar + 0x1D4);
                if (actionState == 0x4600 || actionState == 0x4201 || actionState == 0x4203 ||
                    (actionState >= 0x6200 && actionState <= 0x620B)) {
                    return false;
                }

                uint32_t flags2EC = *(uint32_t*)((uintptr_t)pChar + 0x2EC);
                bool isNormal = (flags2EC & 0x400) != 0;
                if (isNormal) {
                    return false;
                }

                if (s_EXTracker[plr].isActive && s_EXTracker[plr].pChar == pChar) {
                    if (g_CurrentFrame >= s_EXTracker[plr].startFrame &&
                        g_CurrentFrame <= s_EXTracker[plr].startFrame + 18) {
                        return s_EXTracker[plr].isArmored;
                    } else {
                        s_EXTracker[plr].isActive = false;
                    }
                }

            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return false;
            }

            return false;
        }

        bool ShouldPreserveThreadOnHit(void* pDefender) {
            return false;
        }

        bool HandleIncomingHitOnEXStartup(
            void* pDefender,
            void* pAttacker,
            int* param_1,
            int* pRxID,
            uint8_t* param_5,
            int param_6,
            float damage,
            int param_8
        ) {
            return false;
        }

    } // namespace MeterDrainFix
} // namespace MK9TradeFix
