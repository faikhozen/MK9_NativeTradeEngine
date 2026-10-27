#pragma once
#include <cstdint>
#include <cstddef>

namespace MK9TradeFix {
    namespace ToastyTraining {
        
        struct ToastyBoostEntry {
            const char* name;
            const char* boostInput;
            const char* inputDetails;
        };

        extern const ToastyBoostEntry g_ToastyBoostGuide[32];
        extern const size_t g_ToastyBoostGuideCount;

        // Configuration & Initialization
        void InstallHooks();
        void UnlockTrainingMode();
        void SetEnabled(bool enabled);
        bool IsEnabled();
        bool ShouldSuppressScriptStart();
        void Toggle();

        // Dan Forden Customization
        enum ToastyCorner {
            TOASTY_CORNER_BOTTOM_RIGHT = 0, // Vanilla Default
            TOASTY_CORNER_BOTTOM_LEFT  = 1,
            TOASTY_CORNER_TOP_RIGHT    = 2,
            TOASTY_CORNER_TOP_LEFT     = 3,
            TOASTY_CORNER_RANDOM       = 4
        };

        extern float g_ToastyScale;         // 1.00x to 3.00x (default 1.00x)
        extern int   g_ToastyCorner;        // ToastyCorner enum (default 0)
        extern bool  g_ToastyAutoFlip;      // Automatically flip horizontally from left corners (default false)
        extern bool  g_ToastyFlipTop;       // Automatically flip upside-down from top corners (default false)
        extern bool  g_ToastyRandomScale;   // Randomize Dan Forden's size (1.00x - 3.80x) dynamically (default false)

        // Query whether custom placement/scaling is disabled (in 720p or 4:3 resolutions)
        bool IsExtraToastySettingsDisabled();

        // Direct manual invocation of Dan Forden's "Toasty!" cameo (0x008D6180)
        void TriggerToastyCameo(bool force = false);
        void TriggerToastyCameo(int attPlr, bool force = false);

        // Called on attack hit event
        void OnAttackHit(int attPlr, int defPlr, bool isUppercut, bool isBlocked, float damage = 0.0f, int rxID = 0);

        // Dynamic Character Detection & Toasty Boost lookup
        const char* DetectCharacterName(void* pChar);
        const ToastyBoostEntry* GetCharacterToastyBoost(void* pChar);
        const ToastyBoostEntry* FindToastyBoostByName(const char* name);

    }
}

