#include "cancel_detector.hpp"
#include "frame_data.hpp"
#include "simulation.hpp"
#include <cstdio>
#include <cstring>

namespace MK9TradeFix {
    namespace CancelDetector {

        static ActiveNormalState s_ActiveNormal[3] = {};
        static CancelRecord      s_LastCancel[3]   = {};

        static const char* ResolveNormalAttackName(uint32_t dirMotion, uint32_t button, uint32_t* pOutAuthoredDuration = nullptr) {
            bool isCrouch = (button == 0x80000023 || button == 0x8000003A || button == 0x8000003B || button == 0x80000029 ||
                             dirMotion == 0x80000002 || dirMotion == 0x80000007 || dirMotion == 0x80000008);
            bool isBack = (dirMotion == 0x80000003 || dirMotion == 0x80000005 || dirMotion == 0x80000007);
            bool isFwd  = (dirMotion == 0x80000004 || dirMotion == 0x80000006 || dirMotion == 0x80000008);

            uint32_t btn = (button == 0x80000000) ? dirMotion : button;

            if (btn == 0x8000000F || btn == 0x80000023 || dirMotion == 0x8000000F) {
                if (isCrouch) {
                    if (pOutAuthoredDuration) *pOutAuthoredDuration = 21;
                    return "D1 (Crouch Jab)";
                }
                if (isBack) {
                    if (pOutAuthoredDuration) *pOutAuthoredDuration = 26;
                    return "B1";
                }
                if (isFwd) {
                    if (pOutAuthoredDuration) *pOutAuthoredDuration = 26;
                    return "F1";
                }
                if (pOutAuthoredDuration) *pOutAuthoredDuration = 24;
                return "Standing 1 (FP)";
            }
            if (btn == 0x80000010 || btn == 0x8000003A || dirMotion == 0x80000010) {
                if (isCrouch) {
                    if (pOutAuthoredDuration) *pOutAuthoredDuration = 40;
                    return "D2 (Uppercut)";
                }
                if (isBack) {
                    if (pOutAuthoredDuration) *pOutAuthoredDuration = 28;
                    return "B2";
                }
                if (isFwd) {
                    if (pOutAuthoredDuration) *pOutAuthoredDuration = 28;
                    return "F2";
                }
                if (pOutAuthoredDuration) *pOutAuthoredDuration = 26;
                return "Standing 2 (BP)";
            }
            if (btn == 0x80000011 || btn == 0x8000003B || dirMotion == 0x80000011) {
                if (isCrouch) {
                    if (pOutAuthoredDuration) *pOutAuthoredDuration = 24;
                    return "D3 (Low Poke)";
                }
                if (isBack) {
                    if (pOutAuthoredDuration) *pOutAuthoredDuration = 28;
                    return "B3";
                }
                if (isFwd) {
                    if (pOutAuthoredDuration) *pOutAuthoredDuration = 28;
                    return "F3";
                }
                if (pOutAuthoredDuration) *pOutAuthoredDuration = 26;
                return "Standing 3 (FK)";
            }
            if (btn == 0x80000012 || btn == 0x80000029 || dirMotion == 0x80000012) {
                if (isCrouch) {
                    if (pOutAuthoredDuration) *pOutAuthoredDuration = 28;
                    return "D4 (Sweep Poke)";
                }
                if (isBack) {
                    if (pOutAuthoredDuration) *pOutAuthoredDuration = 32;
                    return "B4";
                }
                if (isFwd) {
                    if (pOutAuthoredDuration) *pOutAuthoredDuration = 30;
                    return "F4";
                }
                if (pOutAuthoredDuration) *pOutAuthoredDuration = 28;
                return "Standing 4 (BK)";
            }

            if (pOutAuthoredDuration) *pOutAuthoredDuration = 24;
            return "Normal Attack";
        }

        static bool IsNormalAttackCandidate(uint32_t dirMotion, uint32_t button) {
            if (dirMotion >= 0x8000000F && dirMotion <= 0x80000012) return true;
            if (button >= 0x8000000F && button <= 0x80000012) return true;
            if (button == 0x80000023 || button == 0x8000003A || button == 0x8000003B || button == 0x80000029) return true;
            return false;
        }

        void Initialize() {
            memset(s_ActiveNormal, 0, sizeof(s_ActiveNormal));
            memset(s_LastCancel, 0, sizeof(s_LastCancel));
        }

        void OnNormalStarted(int playerNum, uint32_t dirMotion, uint32_t button, uint32_t token) {
            if (playerNum < 1 || playerNum > 2) return;
            if (!IsNormalAttackCandidate(dirMotion, button)) return;

            ActiveNormalState& norm = s_ActiveNormal[playerNum];
            norm.inNormal = true;
            norm.startFrame = g_CurrentFrame;
            norm.impactFrame = 0;
            norm.hasImpact = false;
            norm.isBlocked = false;
            norm.buttonToken = button;
            norm.dirMotion = dirMotion;
            norm.cancelAlreadyTriggered = false;

            uint32_t authDuration = 24;
            const char* name = ResolveNormalAttackName(dirMotion, button, &authDuration);
            strncpy(norm.moveName, name, sizeof(norm.moveName) - 1);
            norm.authoredTotalDuration = authDuration;
        }

        static void TriggerCancel(int plr, CancelType type, const char* targetName, const char* typeDesc) {
            if (plr < 1 || plr > 2) return;
            ActiveNormalState& norm = s_ActiveNormal[plr];
            if (!norm.inNormal || norm.cancelAlreadyTriggered) return;

            norm.cancelAlreadyTriggered = true;
            norm.inNormal = false; // Normal completed by cancel!

            uint64_t elapsedFromStart = (g_CurrentFrame >= norm.startFrame) ? (g_CurrentFrame - norm.startFrame) : 0;
            int elapsedFromImpact = norm.hasImpact ? (int)(g_CurrentFrame - norm.impactFrame) : (int)elapsedFromStart;
            int recoveryStolen = (norm.authoredTotalDuration > elapsedFromStart) ? (int)(norm.authoredTotalDuration - elapsedFromStart) : 0;

            CancelRecord rec = {};
            rec.frame = g_CurrentFrame;
            rec.playerNum = plr;
            rec.type = type;
            strncpy(rec.normalName, norm.moveName[0] ? norm.moveName : "Normal Attack", sizeof(rec.normalName) - 1);
            strncpy(rec.targetName, targetName, sizeof(rec.targetName) - 1);
            strncpy(rec.cancelTypeDesc, typeDesc, sizeof(rec.cancelTypeDesc) - 1);
            rec.timingRelStart = (int)elapsedFromStart;
            rec.timingRelImpact = elapsedFromImpact;
            rec.recoveryStolen = recoveryStolen;
            rec.onHit = norm.hasImpact && !norm.isBlocked;
            rec.onBlock = norm.hasImpact && norm.isBlocked;
            rec.onWhiff = !norm.hasImpact;

            s_LastCancel[plr] = rec;

            const char* condStr = rec.onHit ? "CLEAN HIT" : (rec.onBlock ? "ON BLOCK" : "WHIFF CANCEL");
            LogTrace("====================================================================");
            LogTrace("[LIVE CANCEL DETECTED | F#%llu] >>> PLAYER %d NORMAL CANCEL <<<", g_CurrentFrame, plr);
            LogTrace("  Normal Move:        %s", rec.normalName);
            LogTrace("  Cancel Target:      %s: %s", rec.cancelTypeDesc, rec.targetName);
            LogTrace("  Cancel Timing:      Frame F%+02d (+%df post-impact)", rec.timingRelStart, rec.timingRelImpact);
            LogTrace("  Condition:          %s", condStr);
            if (recoveryStolen > 0) {
                LogTrace("  Recovery Stolen:    %d frames of normal recovery cancelled early!", recoveryStolen);
            } else {
                LogTrace("  Recovery Stolen:    0 frames (Natural transition / link)");
            }
            LogTrace("====================================================================");

            // Separate frame data lifecycle cleanly
            NotifyNormalCancelled(plr, recoveryStolen, rec.targetName);
        }

        void OnSpecialXfer(int playerNum, void* pMoveInfo, bool isEX, bool isXRay) {
            if (playerNum < 1 || playerNum > 2) return;
            ActiveNormalState& norm = s_ActiveNormal[playerNum];
            if (!norm.inNormal) return;

            CancelType cType = isXRay ? CancelType::SUPER_XRAY : (isEX ? CancelType::EX_SPECIAL : CancelType::SPECIAL);
            const char* typeDesc = isXRay ? "SUPER CANCEL" : (isEX ? "EX SPECIAL CANCEL" : "SPECIAL CANCEL");
            const char* targetName = isXRay ? "X-Ray (3+4+Block)" : (isEX ? "EX Enhanced Special" : "Special Move (Spear/Special)");
            
            if (g_SimSeqType == MACRO_SEQ_MODE18_NORMAL_CANCEL && playerNum == 1) {
                if (g_Mode18_ActiveFNum == 1) {
                    cType = CancelType::SPECIAL;
                    typeDesc = "SPECIAL CANCEL";
                    targetName = "b,b,1 (Spear)";
                } else if (g_Mode18_ActiveFNum == 2) {
                    cType = CancelType::EX_SPECIAL;
                    typeDesc = "EX SPECIAL CANCEL";
                    targetName = "b,b,1+BL (EX Spear)";
                } else if (g_Mode18_ActiveFNum == 5) {
                    cType = CancelType::SUPER_XRAY;
                    typeDesc = "SUPER CANCEL";
                    targetName = "X-Ray (3+4+BL)";
                }
            }

            TriggerCancel(playerNum, cType, targetName, typeDesc);
        }

        void OnMoveMatched(int playerNum, uint32_t dirMotion, uint32_t button, uint32_t token) {
            if (playerNum < 1 || playerNum > 2) return;
            ActiveNormalState& norm = s_ActiveNormal[playerNum];

            if (!norm.inNormal) {
                // If not currently in a normal, check if this is the start of a candidate normal attack
                if (IsNormalAttackCandidate(dirMotion, button)) {
                    OnNormalStarted(playerNum, dirMotion, button, token);
                }
                return;
            }

            // Already in a normal attack:
            // Check if this matched move is a cancel!
            uint64_t elapsed = (g_CurrentFrame >= norm.startFrame) ? (g_CurrentFrame - norm.startFrame) : 0;
            if (elapsed < 2) {
                return; // Ignore duplicate input frame of the initial move
            }

            // Check if it's a string follow-up branch (normal into normal command chain)
            if (IsNormalAttackCandidate(dirMotion, button)) {
                const char* followUpName = ResolveNormalAttackName(dirMotion, button, nullptr);
                TriggerCancel(playerNum, CancelType::STRING_BRANCH, followUpName, "STRING FOLLOW-UP");
                // Immediately transition active normal to the follow-up normal
                OnNormalStarted(playerNum, dirMotion, button, token);
                return;
            }

            // Check if it's a special move command
            bool hasMotion = (dirMotion != 0x80000000 && dirMotion != 0);
            if (hasMotion || button == 0x80000015 || button == 0x80000013) {
                const char* targetName = "Special Move";
                const char* typeDesc = "SPECIAL CANCEL";
                CancelType cType = CancelType::SPECIAL;
                if (g_SimSeqType == MACRO_SEQ_MODE18_NORMAL_CANCEL && playerNum == 1) {
                    if (g_Mode18_ActiveFNum == 1) {
                        targetName = "b,b,1 (Spear)";
                        typeDesc = "SPECIAL CANCEL";
                        cType = CancelType::SPECIAL;
                    } else if (g_Mode18_ActiveFNum == 2) {
                        targetName = "b,b,1+BL (EX Spear)";
                        typeDesc = "EX SPECIAL CANCEL";
                        cType = CancelType::EX_SPECIAL;
                    } else if (g_Mode18_ActiveFNum == 5) {
                        targetName = "X-Ray (3+4+BL)";
                        typeDesc = "SUPER CANCEL";
                        cType = CancelType::SUPER_XRAY;
                    }
                }
                TriggerCancel(playerNum, cType, targetName, typeDesc);
            }
        }

        void OnActionState(int playerNum, uint32_t actionState) {
            if (playerNum < 1 || playerNum > 2) return;
            ActiveNormalState& norm = s_ActiveNormal[playerNum];
            if (!norm.inNormal) return;

            // Check for dash cancellation or link
            bool isDash = (actionState >= 0x2010 && actionState <= 0x201F) || (actionState == 0x0300 || actionState == 0x0301);
            if (isDash) {
                uint64_t elapsed = (g_CurrentFrame >= norm.startFrame) ? (g_CurrentFrame - norm.startFrame) : 0;
                int recoveryStolen = (norm.authoredTotalDuration > elapsed) ? (int)(norm.authoredTotalDuration - elapsed) : 0;
                const char* dashName = (actionState == 0x2011 || actionState == 0x0301) ? "Backdash (B, B)" : "Front Dash (F, F)";
                const char* dashType = (recoveryStolen > 0) ? "DASH CANCEL" : "DASH LINK/TRANSITION";
                TriggerCancel(playerNum, CancelType::DASH, dashName, dashType);
                return;
            }

            // Returned to neutral, movement, crouch, or hurt
            if (actionState < 0x2000 || (actionState >= 0x4000 && actionState <= 0x4FFF) || (actionState >= 0x6000 && actionState <= 0x62FF)) {
                norm.inNormal = false;
            }
        }

        void OnNormalImpact(int attPlr, bool isBlocked, uint32_t rxID) {
            if (attPlr < 1 || attPlr > 2) return;
            ActiveNormalState& norm = s_ActiveNormal[attPlr];
            if (norm.inNormal && !norm.hasImpact) {
                norm.hasImpact = true;
                norm.impactFrame = g_CurrentFrame;
                norm.isBlocked = isBlocked;
            }
        }

        void Tick() {
            for (int plr = 1; plr <= 2; ++plr) {
                ActiveNormalState& norm = s_ActiveNormal[plr];
                if (norm.inNormal) {
                    uint64_t elapsed = (g_CurrentFrame >= norm.startFrame) ? (g_CurrentFrame - norm.startFrame) : 0;
                    // If normal move exceeded its authored duration by 5 frames, it finished naturally without cancelling
                    if (elapsed > (norm.authoredTotalDuration + 5)) {
                        norm.inNormal = false;
                    }
                }
            }
        }

        bool IsInNormal(int playerNum) {
            if (playerNum < 1 || playerNum > 2) return false;
            return s_ActiveNormal[playerNum].inNormal;
        }

        const CancelRecord& GetLastCancel(int playerNum) {
            static CancelRecord s_Empty = {};
            if (playerNum < 1 || playerNum > 2) return s_Empty;
            return s_LastCancel[playerNum];
        }

        bool HasRecentCancel(int playerNum, uint64_t withinFrames) {
            if (playerNum < 1 || playerNum > 2) return false;
            if (s_LastCancel[playerNum].frame == 0) return false;
            return (g_CurrentFrame >= s_LastCancel[playerNum].frame) &&
                   (g_CurrentFrame - s_LastCancel[playerNum].frame <= withinFrames);
        }

        void FormatCancelNotice(int playerNum, char* outBuf, size_t bufSize) {
            if (!outBuf || bufSize == 0) return;
            outBuf[0] = '\0';
            if (!HasRecentCancel(playerNum, 120)) return;

            const CancelRecord& rec = s_LastCancel[playerNum];
            const char* cond = rec.onHit ? "Hit" : (rec.onBlock ? "Block" : "Whiff");
            snprintf(outBuf, bufSize, "Cancel: %s -> %s (+%df saved | %s)",
                     rec.normalName, rec.targetName, rec.recoveryStolen, cond);
        }
    }
}
