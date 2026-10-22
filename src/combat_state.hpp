#pragma once
#include "engine_common.hpp"

namespace MK9TradeFix {

    struct AttackClassification {
        const char* category;
        MoveClass   moveClass;
        bool        isProp;
        bool        isChildActor;
        bool        hasHitbox;
        bool        isExcludedFromTrades; // True for Projectile Specials and X-Rays
        const char* reason;
        bool        isProjectile;         // true for atkType == 19 (dist_fireball)
        void*       pCollisionContext;    // Pointer to engine CollisionContext (param_8)
        uint32_t    atkType;              // Native NetherRealm property ID (0-31)
        const char* propertyName;         // Human-readable taxonomy description
        bool        isUppercut;           // Normal Uppercut launcher (D+2)
        bool        isJumpIn;             // Jump-In aerial strike
    };

    struct CachedAttackState {
        uint64_t frameCaptured = 0;
        int      scriptFunc    = -1;
        float    damage        = 0.0f;
        int      rxID          = 0;
        bool     isValid       = false;
    };
    extern CachedAttackState g_PlayerAttackCache[3]; // Index 1 = P1, Index 2 = P2

    const char* GetAttackPropertyTaxonomyName(uint32_t atkType);

    bool ClassifySpecialOrSuper(
        void* pFighter, 
        void* pOpponent, 
        int* param_1, 
        uint8_t* param_5, 
        float damage, 
        MoveClass* outClass, 
        const char** outReason,
        uint32_t* outAtkType = nullptr
    );

    AttackClassification AnalyzeAttack(
        void* pAttacker, 
        void* pDefender, 
        int* param_1, 
        uint8_t* param_5, 
        float damage, 
        int param_8 = 0
    );

    bool IsFighterExecutingUppercut(void* pChar);
    CharacterCombatState GetCharacterCombatState(void* pChar, const char** outReason = nullptr);
    bool IsDefenderInTradeWindow(void* pDefender);

    bool IsCharacterInActiveAttack(void* pChar);
    bool IsCharacterHitboxActive(void* pChar);
    bool IsCharacterInActualActiveFrames(void* pChar);
    bool IsPredictiveSimultaneousTrade();

    bool QueryCharacterAttackInfo(void* pChar, float* pOutDamage, int* pOutRxID);
    void CacheActiveFighterState(void* pChar, int playerIdx);
    const char* ClassifyHitOutcome(int attPlr, int defPlr, bool isTrade, uint64_t* pOutElapsed, const char** pOutDetail);

    void* GetPlayerActiveAttackAnimSeq(int playerIdx);
    void  SetPlayerActiveAttackAnimSeq(int playerIdx, void* pSeq);
    bool  IsSoulStealActiveAndHit();

    namespace MoveTelemetryHUD {
        extern bool g_ShowMoveHUD;
        extern bool g_ShowActionHex;
        extern float g_P1MoveHUDOffsetX;
        extern float g_P1MoveHUDOffsetY;
        extern float g_P2MoveHUDOffsetX;
        extern float g_P2MoveHUDOffsetY;

        void InstallHooks();
        void NotifyPlayAnimSeq(void* pChar, void* pAnimSeq);
        void NotifyCombatPlayAnim(void* pChar, void* pAnimSeq);
        void NotifySkelMeshPlayAnim(void* pSkelComp, uint32_t nameIndex);
        bool GetPlayerMoveTelemetry(int playerIdx, char* outText, size_t maxLen, uint32_t* outColorRGBA);
    }

    namespace HitIndicationHUD {
        enum class HitIndicationType {
            NONE = 0,
            COUNTER_HIT,
            TRADE_HIT,
            FATAL_PUNISH
        };

        extern bool  g_ShowHitIndicationHUD;
        extern float g_P1IndicationOffsetX;
        extern float g_P1IndicationOffsetY;
        extern float g_P2IndicationOffsetX;
        extern float g_P2IndicationOffsetY;

        void TriggerHitIndication(int playerIdx, HitIndicationType type);
        bool GetPlayerPopup(int playerIdx, HitIndicationType* outType, float* outAlpha, float* outSlideProgress);
        void Reset();
    }
}

