#pragma once
#include "engine_common.hpp"

namespace MK9TradeFix {

    struct FrameDataBlock {
        uint8_t state; // 0=Idle, 1=Startup, 2=Active, 3=Recovery, 4=Hitstun, 5=Blockstun, 6=Dash/Cancel, 7=Quick Roll, 8=Invincible Wakeup, 9=Delayed Wakeup
        uint8_t frameNumberInPhase;
    };

    struct FighterFrameTimeline {
        static const int CAPACITY = 75;
        FrameDataBlock blocks[CAPACITY];
        int count = 0;
        int startupFrames = 0;
        int activeFrames = 0;
        int totalFrames = 0;
        int advantage = 0;
        bool hasAdvantage = false;
        bool isInfiniteAdvantage = false;
        bool isWakeupAttack = false;
        bool isWakeupInvincible = false;
        bool isQuickRolling = false;
        bool isDelayedWakeup = false;
    };

    struct MoveLifecycleTracker {
        bool     inMove = false;
        uint64_t startFrame = 0;
        int32_t  scriptFunc = -1;
        uint32_t actionState = 0;
        uint32_t initialStance = 0;
        uint32_t initialCombat = 0;
        
        uint64_t firstActiveFrame = 0;
        uint64_t lastActiveFrame = 0;
        uint64_t recoveryStartFrame = 0;
        uint64_t endFrame = 0;
        uint64_t lastFinishedEndFrame = 0;
        bool     isProjectile = false;
        bool     isWakeupAttack = false;
        bool     isWakeupInvincible = false;
        uint64_t projectileSpawnFrame = 0;
        
        uint32_t maxHitboxes = 0;
        uint32_t nativeSubState = 0;
        int32_t  nativeMoveRes = 0;
        float    baseDamage = 0.0f;
        int      rxID = 0;
        uint32_t matchedToken = 0;
        bool     wasInterrupted = false;
        bool     wasCancelled = false;
        int      cancelRecoveryStolen = 0;
        const char* cancelTargetName = nullptr;
    };

    struct StunLifecycleTracker {
        bool     inStun = false;
        bool     isBlockstun = false;
        uint64_t startFrame = 0;
        uint64_t endFrame = 0;
        int      attackerPlr = 0;
        uint32_t authoredFrames = 0;
    };

    struct LiveAdvantageState {
        bool inMove = false;
        uint64_t startFrame = 0;
        bool hasHit = false;
        bool isBlocked = false;
        uint64_t hitFrame = 0;
        uint64_t attackerEndFrame = 0;
        uint64_t defenderReleaseFrame = 0;
        int  finalAdvantage = 0;
        bool advantageComputed = false;
        bool isProjectile = false;
    };

    extern bool g_FrameDataVisualizerEnabled;
    extern float g_FrameBarOpacity;
    extern MoveLifecycleTracker g_P1_Lifecycle;
    extern MoveLifecycleTracker g_P2_Lifecycle;
    extern StunLifecycleTracker g_P1_StunTracker;
    extern StunLifecycleTracker g_P2_StunTracker;
    extern LiveAdvantageState s_p1AdvState;
    extern LiveAdvantageState s_p2AdvState;
    extern FighterFrameTimeline s_p1Timeline;
    extern FighterFrameTimeline s_p2Timeline;

    extern uint32_t g_P1_LastAttackToken;
    extern uint32_t g_P2_LastAttackToken;
    uint32_t ResolveAuthoredBlockstun(int attPlr, const MoveLifecycleTracker& attTracker, void* pDefender = nullptr);

    void ToggleFrameDataVisualizer();
    bool IsFrameDataVisualizerEnabled();
    void GetLiveFrameDataTimelines(FighterFrameTimeline& outP1, FighterFrameTimeline& outP2);

    void ProcessFighterLifecycleTick(void* pChar, int playerIdx, MoveLifecycleTracker& tracker);
    void RecordFrameDataTick(void* p1, void* p2);
    void OnHitOrBlockEvent(int attPlr, int defPlr, bool isBlocked, uint64_t hitFrame, int param_8 = 0);
    void DumpAttackLifecycleSummary(int playerIdx, const MoveLifecycleTracker& tracker);
    void NotifyNormalCancelled(int plr, int recoveryStolen, const char* targetMoveName);
}
