#pragma once
#include <cstdint>

namespace MK9TradeFix {

    struct AttackClassification;

namespace StunModifier {

    // -------------------------------------------------------------------------
    // Configuration Sliders
    // -------------------------------------------------------------------------
    extern int g_GeneralBlockstunDelta;         // -12 to +12 frames (default 0)
    extern int g_GeneralHitstunDelta;            // -15 to +60 frames (default 0, standing hits only)
    extern int g_CounterhitExtraHitstun;       // -15 to +60 frames (default 0, requires Trade Fix)
    extern int g_CounterhitMinStartup;         // 0 to 60 frames (default 0 = no threshold)
    extern int g_CounterhitMoveTypeFilter;     // 0 = Both, 1 = Normals Only, 2 = Specials Only
    extern bool g_CounterhitZoomEnabled;       // Backwards-compat alias (true if g_CounterhitCameraMode > 0)
    extern int  g_CounterhitCameraMode;        // 0 = Off, 1 = FOV Zoom, 2 = Angled Cinematic
    extern float g_CounterhitZoomAmount;       // FOV offset in degrees (default -12.0 deg)
    extern float g_CounterhitCamShiftY;        // Lateral shift towards attacker (default 330.0f)
    extern float g_CounterhitCamYawOffset;     // Yaw angle towards defender (default 19.226 deg = 3500 units)

    extern int g_PunishExtraHitstun;           // -15 to +60 frames (default 0, requires Trade Fix)
    extern int g_PunishMinStartup;             // 0 to 60 frames (default 0 = no threshold)
    extern int g_PunishMoveTypeFilter;         // 0 = Both, 1 = Normals Only, 2 = Specials Only
    extern bool g_PunishZoomEnabled;           // Backwards-compat alias (true if g_PunishCameraMode > 0)
    extern int  g_PunishCameraMode;            // 0 = Off, 1 = FOV Zoom, 2 = Angled Cinematic
    extern float g_PunishZoomAmount;           // FOV offset in degrees (default -18.0 deg)
    extern float g_PunishCamShiftY;            // Lateral shift towards attacker (default 330.0f)
    extern float g_PunishCamYawOffset;         // Yaw angle towards defender (default 19.226 deg = 3500 units)

    extern bool g_TradeZoomEnabled;            // Backwards-compat alias (true if g_TradeCameraMode > 0)
    extern int  g_TradeCameraMode;             // 0 = Off, 1 = FOV Zoom / Pullback, 2 = Angled Dynamic
    extern float g_TradeZoomAmount;            // FOV offset in degrees (default +8.0 deg = pull back / widen)
    extern int   g_TradeZoomHoldFrames;        // Number of frames to hold Trade FOV (default 30f)
    extern float g_TradeCamShiftY;             // Lateral shift (default 0.0f)
    extern float g_TradeCamYawOffset;          // Yaw angle (default 0.0f)

    // Trade Engine Outcome Meter Rewards (when Trade Engine is Active)
    extern bool  g_EnableCounterhitMeterReward; // Enable Counter Hit meter reward
    extern float g_CounterhitMeterReward;       // Super Meter awarded on Counter Hit (default 0.0f, Dramatic: 0.05f)
    extern int   g_CounterhitMeterMoveFilter;   // 0 = Both, 1 = Normals Only, 2 = Specials Only
    extern int   g_CounterhitMeterRecipient;    // 0 = Hit Receiver (Defender), 1 = Attacker, 2 = Both Characters

    extern bool  g_EnableTradeMeterReward;      // Enable Mutual Trade meter reward
    extern float g_TradeMeterReward;            // Super Meter awarded on Mutual Trade to both (default 0.0f, Dramatic: 0.05f)
    extern int   g_TradeMeterMoveFilter;        // 0 = Both, 1 = Normals Only, 2 = Specials Only

    extern bool  g_EnablePunishMeterReward;     // Enable Fatal Punish meter reward
    extern float g_PunishMeterReward;           // Super Meter awarded on Fatal Punish (default 0.0f, Dramatic: 0.085f)
    extern int   g_PunishMeterMoveFilter;       // 0 = Both, 1 = Normals Only, 2 = Specials Only
    extern int   g_PunishMeterRecipient;        // 0 = Hit Receiver (Defender), 1 = Attacker, 2 = Both Characters
    extern int   g_HitOutcomeMeterRecipient;    // Backwards-compat alias

    extern int g_TargetScope;                 // 0 = Both Players, 1 = P1 Only, 2 = P2 Only
    enum StunVisualMode {
        STUN_VISUAL_SLOW_MO = 0,      // Slow-motion hurt animation congruent to added frames (Recommended)
        STUN_VISUAL_GENERIC_ANIM = 1, // Generic Dizzy/Stumble animation (Action 0x4205/0x4206)
        STUN_VISUAL_STATIC_HOLD = 2   // Freeze on last frame of hurt flinch
    };

    extern int  g_StunVisualMode;              // 0 = Slow Mo, 1 = Generic Stun, 2 = Static Hold
    extern bool g_PlayGenericStunAnim;         // Backwards-compat alias / toggle
    extern int  g_GenericStunActionId;         // Action ID for generic stun (0x4205 = Dizzy/Stumble, 0x4206 = Groggy Stagger)

    // Blockstun Pushback Customization
    extern bool  g_EnableCustomBlockPushback;  // Custom block pushback slider toggle
    extern float g_BlockPushbackMultiplier;     // Block pushback distance multiplier (0.00x to 3.00x, default 1.00x)
    extern bool  g_CornerPushbackReflection;    // Transfers pushback to attacker when defender is backed in corner

    // -------------------------------------------------------------------------
    // Extras User Presets (4 Slots) & Import/Export
    // -------------------------------------------------------------------------
    struct ExtrasPreset {
        char name[32];
        bool isPopulated;

        // Stun & Extensions
        int  generalBlockstun;
        int  generalHitstun;
        int  targetScope;
        int  stunVisualMode;

        // Counter Hit
        int   chExtraHitstun;
        int   chMinStartup;
        int   chMoveFilter;
        int   chCameraMode;
        float chZoomAmount;
        float chCamShiftY;
        float chCamYawOffset;
        bool  chEnableMeter;
        float chMeterReward;
        int   chMeterFilter;
        int   chMeterRecipient;

        // Fatal Punish
        int   punishExtraHitstun;
        int   punishMinStartup;
        int   punishMoveFilter;
        int   punishCameraMode;
        float punishZoomAmount;
        float punishCamShiftY;
        float punishCamYawOffset;
        bool  punishEnableMeter;
        float punishMeterReward;
        int   punishMeterFilter;
        int   punishMeterRecipient;

        // Mutual Trade
        int   tradeCameraMode;
        float tradeZoomAmount;
        int   tradeZoomHold;
        bool  tradeEnableMeter;
        float tradeMeterReward;
        int   tradeMeterFilter;

        // Dynamic Camera Slow-Motion & Smoothing
        bool  dynSlowMo;
        float dynSlowMoRate;
        float dynCamSpeedIn;
        float dynCamSpeedOut;

        // Super Meter & Combat Meter System
        bool  customMeterCosts;
        float exCost;
        float breakerCost;
        float xrayCost;
        bool  showMeterHUD;
        bool  enableSpecialCost;
        float specialCost;
        bool  enableAttGain;
        float attGain;
        int   attFilter;
        bool  enableDefBlockGain;
        float defBlockGain;
        int   defBlockFilter;
        bool  enableDefDmgGain;
        float defDmgGain;
        int   defDmgFilter;

        // Visual & HUD & Match
        bool showHitOutcomeBadges;
        bool disableBlood;
        bool disableBackground;
        float voidBgColor[3];
        bool enableFloorGrid;
        float floorGridColor[3];
        float floorGridCenterColor[3];
        bool showGridCornerWalls;
        float gridWallColor[3];

        // Wakeup
        bool disableQuickWakeup;
        bool disableRollWakeup;
        bool disableDelayedWakeup;
        bool delayedWakeupFixedHold;
        int  delayedWakeupFixedFrames;
        bool disableDelayedWakeupRoll;
        int  wakeupScope;

        // Move Priority
        bool uppercutAA;
        bool uppercutVsSpecial;
        bool uppercutVsUppercut;
        bool jumpInPriority;
        bool xrayPriority;
        bool instantProjectiles;
    };

    extern ExtrasPreset g_UserPresets[4];
    extern int g_ActivePresetSlot; // 0..3 or -1 if custom
    extern char g_PresetStatusMsg[128];
    extern uint64_t g_PresetStatusExpireFrame;

    void InitPresets();
    const char* GetCurrentPresetName();
    void ApplyVanillaPreset();
    void ApplyDramaticModePreset(int camMode = 1);
    void SaveUserPreset(int slotIdx, const char* name = nullptr);
    bool LoadUserPreset(int slotIdx);
    bool ExportExtrasPresetsToFile(const char* filepath = "mk9_extras_presets.ini");
    bool ImportExtrasPresetsFromFile(const char* filepath = "mk9_extras_presets.ini");
    void SetPresetStatus(const char* fmt, ...);

    // -------------------------------------------------------------------------
    // Player Stun Tracking State
    // -------------------------------------------------------------------------
    struct PlayerStunState {
        bool     active = false;
        bool     isBlocked = false;
        int      totalExtraFrames = 0;
        uint32_t baseAuthoredStun = 14;
        uint64_t hitFrame = 0;
        uint64_t releaseFrame = 0;
        uint32_t lastStunAction = 0;
        uint32_t naturalAnimFrames = 0;
        int      attackerScriptFunc = 0;
        float    animRateScale = 1.0f;
        bool     isHoldingExtraStun = false;
        bool     isCounterhit = false;
        bool     isPunish = false;
        void*    pLastAnimSeq = nullptr;
    };

    extern PlayerStunState g_PlayerStun[3]; // Index 1 = P1, 2 = P2

    void ApplyAnimRateScale(int defPlr, void* pChar, float rate);
    void ResetAnimRateScale(int defPlr, void* pChar);

    void Init();
    void Reset();

    // Invoked when an attack hits or is blocked (before native damage / reaction alteration)
    void OnHitEvaluation(int attPlr, int defPlr, void* pDefender, void* pAttacker, bool isBlocked, int rxID, float damage, const AttackClassification* pAtk = nullptr);

    // Invoked immediately after native damage / reaction alteration to synchronize native reaction timer & apply pushback
    void OnPostDamageEvaluation(int defPlr, void* pDefender, bool isBlocked, int attPlr = 0, void* pAttacker = nullptr);

    // Frame-by-frame blockstun pushback updater to scale pushback displacement by g_BlockPushbackMultiplier
    void UpdateBlockPushbackTick(int defPlr, void* pDefender);

    // Reset block pushback tracker state cleanly
    void ResetBlockPushback();

    // Invoked from Combat_PlayAnim and PlayAnimSeq to calculate hurt animation recovery frames
    float CalculateHurtAnimRate(void* pChar, void* pAnimSeq, const char* animName);

    // Invoked from Combat_PlayAnim to adjust animation playback rate
    float GetAnimRateMultiplier(void* pChar, const char* animName, uint32_t actionState);

    // Core synchronization with frame_data.cpp RecordFrameDataTick
    void ProcessStunLockTick(int defPlr, void* pChar, uint32_t action, bool isKnockdown, bool isBlockstun, uint32_t elapsed, bool* pStillLocked);

    // Physical lockout enforcement during stun hold
    void EnforceStunHold(int defPlr, void* pChar);

    // Clean neutral release when extra stun ends
    void ReleaseStunHold(int defPlr, void* pChar);

    // Helper to query if a player is currently in extended stun hold
    bool IsInExtendedStun(int playerNum);

    // Helper to query if a player is currently in extended blockstun
    bool IsInExtendedBlockstun(int playerNum);

    // Helper to query if a player is currently in active blockstun
    bool IsPlayerInBlockstun(int playerNum);

    // Helper to query active combo status and hit count
    uint32_t GetComboHitCount(int attPlr);
    bool IsInCombo(int attPlr);

} // namespace StunModifier
} // namespace MK9TradeFix
