#pragma once
#include "engine_common.hpp"

namespace MK9TradeFix {
    bool AreTradeEngineHooksReady();
    void FinalizeJadeStaffRecovery();

    struct DeferredHitContext {
        bool     isPending = false;
        uint64_t frameCaptured = 0;
        void*    in_ECX = nullptr;
        int*     param_1 = nullptr;
        void*    pDefender = nullptr; // Target taking damage
        void*    pAttacker = nullptr; // Source dealing damage
        uint32_t param_3 = 0;
        int      rxID = 0;
        uint8_t* param_5 = nullptr;
        int      param_6 = 0;
        float    damage = 0.0f;
        int      param_8 = 0;

        uint32_t param_5_storage = 0;
        uint8_t  param_1_storage[512] = { 0 };

        bool     isUppercut = false;
        bool     isJumpIn = false;
        bool     isSpecial = false;
        bool     isProjectile = false;
        bool     isXRay = false;
        bool     defenderWasPropNormal = false;
        MoveClass moveClass = MOVE_CLASS_NORMAL_LIMB;
    };

    extern DeferredHitContext g_DeferredHit;
    extern bool g_InCase1Resolution;
    extern bool g_InHitResolution;

    typedef void(__fastcall* tApplyDamageAndReactions)(
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
    );
    extern tApplyDamageAndReactions oApplyDamageAndReactions;

    typedef void(__thiscall* tAbortAttackerThread_OnHit)(void* pDefender, void* param_1, void* param_2);
    extern tAbortAttackerThread_OnHit oAbortAttackerThread_OnHit;

    void SanitizeFighterWeaponState(void* pChar);
    void SafeCleanupProps(void* pChar);

    void __fastcall hkAbortAttackerThread_OnHit(void* in_ECX, void* edxUnused, void* param_1, void* param_2);
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
    );

    void FlushDeferredHit(const char* reason = nullptr);
    bool InstallTradeEngineHooks();
}
