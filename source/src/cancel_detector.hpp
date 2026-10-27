#pragma once
#include "engine_common.hpp"

namespace MK9TradeFix {
    namespace CancelDetector {

        enum class CancelType {
            NONE = 0,
            SPECIAL,
            EX_SPECIAL,
            SUPER_XRAY,
            DASH,
            STRING_BRANCH
        };

        struct CancelRecord {
            uint64_t    frame = 0;
            int         playerNum = 0;
            CancelType  type = CancelType::NONE;
            char        normalName[64] = {};
            char        targetName[64] = {};
            char        cancelTypeDesc[64] = {};
            int         timingRelImpact = 0;
            int         timingRelStart = 0;
            int         recoveryStolen = 0;
            bool        onHit = false;
            bool        onBlock = false;
            bool        onWhiff = false;
        };

        struct ActiveNormalState {
            bool        inNormal = false;
            uint64_t    startFrame = 0;
            uint64_t    impactFrame = 0;
            bool        hasImpact = false;
            bool        isBlocked = false;
            uint32_t    buttonToken = 0;
            uint32_t    dirMotion = 0;
            char        moveName[64] = {};
            uint32_t    authoredTotalDuration = 24;
            bool        cancelAlreadyTriggered = false;
        };

        void Initialize();
        void Tick();

        // Called when a candidate normal attack is matched in 0x008A0600
        void OnNormalStarted(int playerNum, uint32_t dirMotion, uint32_t button, uint32_t token);

        // Called when special transfer hooks fire (0x008A75F0 / 0x008A76D0)
        void OnSpecialXfer(int playerNum, void* pMoveInfo, bool isEX, bool isXRay);

        // Called when 0x008A0600 dispatches a move while normal attack is active
        void OnMoveMatched(int playerNum, uint32_t dirMotion, uint32_t button, uint32_t token);

        // Called when character action state changes
        void OnActionState(int playerNum, uint32_t actionState);

        // Called on physical hit / block impact event
        void OnNormalImpact(int attPlr, bool isBlocked, uint32_t rxID);

        // Accessors for UI, Overlays, and Frame Data
        bool IsInNormal(int playerNum);
        const CancelRecord& GetLastCancel(int playerNum);
        bool HasRecentCancel(int playerNum, uint64_t withinFrames = 120);
        void FormatCancelNotice(int playerNum, char* outBuf, size_t bufSize);
    }
}
