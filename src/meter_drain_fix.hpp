#pragma once
#include <cstdint>

namespace MK9TradeFix {
    namespace MeterDrainFix {
        // Subsystem Enable / Disable
        void SetEnabled(bool enabled);
        bool IsEnabled();
        void Toggle();

        // Diagnostic counters
        uint32_t GetProtectedThreadCount();
        uint32_t GetAbsorbedHitCount();
        void ResetCounters();

        // Installs hooks (0x008A76D0 - Character_XferToEXSpecialMove)
        bool InstallHooks();

        // Evaluates if character is in Frame-1 EX move startup
        bool IsInEXStartup(void* pChar);

        // Suppresses 0x008AB040 (AbortAttackerThread_OnHit)
        bool ShouldPreserveThreadOnHit(void* pDefender);

        // Pre-hit evaluation in hkApplyDamageAndReactions (0x008A5490)
        bool HandleIncomingHitOnEXStartup(
            void* pDefender,
            void* pAttacker,
            int* param_1,
            int* pRxID,
            uint8_t* param_5,
            int param_6,
            float damage,
            int param_8
        );

        // Blocked Reversal Meter Drain Protection
        void OnBlockOrStunCheck(void* pDefender, uint64_t frame, bool isBlocked);
        void OnTick(void* p1, void* p2, uint64_t frame);
        void RefundPendingEXMeter(int plr, const char* reason);

        // Custom Super Meter Expenditure & Activation Requirements
        extern bool g_CustomMeterCostsEnabled;
        extern float g_CustomEXCost;         // e.g. 0.25f (25.0%)
        extern float g_CustomBreakerCost;    // e.g. 0.50f (50.0%)
        extern float g_CustomXRayCost;       // e.g. 1.00f (100.0%)
        extern bool g_ShowMeterPercentHUD;   // On-screen HUD indicator toggle
        extern float g_P1MeterHUDOffsetX;    // Custom X offset
        extern float g_P1MeterHUDOffsetY;    // Custom Y offset
        extern float g_P2MeterHUDOffsetX;    // Custom X offset
        extern float g_P2MeterHUDOffsetY;    // Custom Y offset

        // Combat Meter Gains (Attacker Hit Gain, Defender Block Gain, Defender Hit Gain)
        extern bool  g_EnableAttackerHitMeterGain;       // Award super meter to attacker on landed hits
        extern float g_AttackerHitMeterRewardNormals;    // Flat meter awarded to attacker per normal hit (default: 0.008f)
        extern float g_AttackerHitMeterRewardSpecials;   // Flat meter awarded to attacker per special hit (default: 0.000f)
        extern float g_AttackerHitMeterReward;           // Legacy alias / default normal
        extern int   g_AttackerHitMeterMoveFilter;       // Legacy filter

        extern bool  g_EnableDefenderBlockMeterGain;     // Award super meter to defender on blocked hits (meter-on-block)
        extern float g_DefenderBlockMeterRewardNormals;  // Flat meter awarded to defender per normal blocked hit (default: 0.015f)
        extern float g_DefenderBlockMeterRewardSpecials; // Flat meter awarded to defender per special blocked hit (default: 0.015f)
        extern float g_DefenderBlockMeterReward;         // Legacy alias
        extern int   g_DefenderBlockMeterMoveFilter;     // Legacy filter

        extern bool  g_EnableDefenderHitMeterGain;       // Award super meter to defender on getting hit (damage taken)
        extern float g_DefenderHitMeterRewardNormals;    // Flat meter awarded to defender per normal hit taken (default: 0.008f)
        extern float g_DefenderHitMeterRewardSpecials;   // Flat meter awarded to defender per special hit taken (default: 0.000f)
        extern float g_DefenderHitMeterReward;           // Legacy alias
        extern int   g_DefenderHitMeterMoveFilter;       // Legacy filter
        // Combat Meter Expenditure (Raw Special, Normal->Special Cancel, Normal->EX Cancel, Normal->XRay Cancel, Normal->Dash Cancel)
        extern bool  g_EnableSpecialMoveMeterCost;       // Deduct meter on raw Special Move execution
        extern float g_SpecialMoveMeterCost;             // Flat meter cost for Special Moves (e.g. 0.050f / 5.0%)
        extern bool  g_EnableNormalToSpecialCancelCost;  // Deduct meter when canceling a normal into a special
        extern float g_NormalToSpecialCancelCost;        // Flat meter cost for Special Cancels (e.g. 0.100f / 10.0%)
        extern bool  g_EnableNormalToEXCancelCost;       // Deduct added meter when canceling a normal into an EX special
        extern float g_NormalToEXCancelCost;             // Flat added meter cost for EX Cancels (e.g. 0.100f / 10.0%)
        extern bool  g_EnableNormalToXRayCancelCost;     // Deduct added meter when canceling a normal into an X-Ray
        extern float g_NormalToXRayCancelCost;           // Flat added meter cost for X-Ray Cancels (e.g. 0.100f / 10.0%)
        extern bool  g_EnableNormalToDashCancelCost;     // Deduct meter when canceling a normal into a dash
        extern float g_NormalToDashCancelCost;           // Flat meter cost for Dash Cancels (e.g. 0.100f / 10.0%)

        bool CheckMoveFilterMatch(int filter, bool isSpecial);
        bool IsEXMoveActive(int plr); // returns true if player is currently performing an EX move

        void SetCustomMeterCostsEnabled(bool enabled);
        bool IsCustomMeterCostsEnabled();
        void SetCustomEXCost(float cost);
        float GetCustomEXCost();
        void SetCustomBreakerCost(float cost);
        float GetCustomBreakerCost();
        void SetCustomXRayCost(float cost);
        float GetCustomXRayCost();

        // Practice Super Meter Mode (NORMAL, FULL, REFILL)
        enum class SuperMeterPracticeMode {
            NORMAL = 0,
            FULL   = 1,
            REFILL = 2
        };
        extern SuperMeterPracticeMode g_SuperMeterPracticeMode;

        void UpdateEffectiveThresholds();
        float GetPlayerMeter(int plr); // returns 0.0f to 1.0f, or -1.0f if invalid
        void SetPlayerMeter(int plr, float amount); // sets exact meter (0.0f to 1.0f)
        void AwardPlayerMeter(int plr, float amount); // awards delta meter (0.0f to 1.0f)
        void DeductPlayerMeter(int plr, float amount); // deducts delta meter (0.0f to 1.0f)
        void NotifyNormalAttack(int plr); // signals normal attack initiation / active strike
        void NotifyActionState(int plr, uint32_t action); // signals action state transition
    }
}

