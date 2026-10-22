#include "stun_modifier.hpp"
#include "trade_engine.hpp"
#include "combat_state.hpp"
#include "frame_data.hpp"
#include "engine_common.hpp"
#include "anim_viewer.hpp"
#include "free_camera.hpp"
#include "meter_drain_fix.hpp"
#include "extra_gameplay.hpp"
#include "move_priority.hpp"
#include "juggle_deterioration.hpp"
#include "toasty_training.hpp"
#include <cstring>
#include <cstdio>
#include <cstdarg>
#include <unordered_map>
#include <windows.h>

namespace MK9TradeFix {
namespace StunModifier {

    int g_GeneralBlockstunDelta = 0;         // -12 to +12 frames (default 0)
    int g_GeneralHitstunDelta = 0;            // 0 to +60 frames (default 0, standing hits only)
    int g_CounterhitExtraHitstun = 0;       // Vanilla 0 (Dramatic: 9)
    int g_CounterhitMinStartup = 0;         // Vanilla 0 (Dramatic: 16)
    int g_CounterhitMoveTypeFilter = 0;     // Vanilla 0 (Dramatic: 1)
    bool g_CounterhitZoomEnabled = false;   // Backwards-compat alias (true if g_CounterhitCameraMode > 0)
    int  g_CounterhitCameraMode = 0;        // 0 = Off, 1 = FOV Zoom, 2 = Angled Cinematic
    float g_CounterhitZoomAmount = 30.0f;   // FOV offset in degrees (+30.0 deg for Cam 2)
    float g_CounterhitCamShiftY = 302.0f;   // Lateral shift towards attacker (302.0 units)
    float g_CounterhitCamYawOffset = 45.0f; // Yaw angle towards defender (45.0 deg)
    static std::unordered_map<int, uint32_t> s_NativeMoveBlockstun;
    static uint32_t s_LastObservedBlockstun[3] = { 0, 26, 26 };

    int g_PunishExtraHitstun = 0;           // Vanilla 0 (Dramatic: 19)
    int g_PunishMinStartup = 0;             // Vanilla 0 (Dramatic: 20)
    int g_PunishMoveTypeFilter = 0;         // Vanilla 0 (Dramatic: 1)
    bool g_PunishZoomEnabled = false;       // Backwards-compat alias (true if g_PunishCameraMode > 0)
    int  g_PunishCameraMode = 0;            // 0 = Off, 1 = FOV Zoom, 2 = Angled Cinematic
    float g_PunishZoomAmount = 30.0f;       // FOV offset in degrees (+30.0 deg for Cam 2)
    float g_PunishCamShiftY = 302.0f;       // Lateral shift towards attacker (302.0 units)
    float g_PunishCamYawOffset = 45.0f;     // Yaw angle towards defender (45.0 deg)

    bool g_TradeZoomEnabled = false;        // Backwards-compat alias (true if g_TradeCameraMode > 0)
    int  g_TradeCameraMode = 0;             // 0 = Off, 1 = FOV Zoom / Pullback, 2 = Angled Dynamic
    float g_TradeZoomAmount = 8.0f;         // FOV offset in degrees (+8.0 deg)
    int   g_TradeZoomHoldFrames = 30;       // Number of frames to hold Trade FOV before returning (30 Frames)
    float g_TradeCamShiftY = 0.0f;          // Lateral shift (default 0.0f)
    float g_TradeCamYawOffset = 0.0f;       // Yaw angle (default 0.0f)

    bool  g_EnableCounterhitMeterReward = false; // Vanilla false (Dramatic: true)
    float g_CounterhitMeterReward = 0.0f;     // Vanilla 0.0f (Dramatic: 0.050f)
    int   g_CounterhitMeterMoveFilter = 0;
    int   g_CounterhitMeterRecipient = 0;    // 0 = Hit Receiver (Defender), 1 = Attacker, 2 = Both Characters

    bool  g_EnableTradeMeterReward = false;   // Vanilla false (Dramatic: true)
    float g_TradeMeterReward = 0.0f;          // Vanilla 0.0f (Dramatic: 0.050f)
    int   g_TradeMeterMoveFilter = 0;        // 0 = Both, 1 = Normals Only, 2 = Specials Only (Both)

    bool  g_EnablePunishMeterReward = false;  // Vanilla false (Dramatic: true)
    float g_PunishMeterReward = 0.0f;         // Vanilla 0.0f (Dramatic: 0.085f)
    int   g_PunishMeterMoveFilter = 0;       // 0 = Both, 1 = Normals Only, 2 = Specials Only (Normals Only)
    int   g_PunishMeterRecipient = 0;        // 0 = Hit Receiver (Defender), 1 = Attacker, 2 = Both Characters
    int   g_HitOutcomeMeterRecipient = 0;    // Backwards-compat alias

    int  g_TargetScope = 0;                  // 0 = Both Players, 1 = P1 Only, 2 = P2 Only
    int  g_StunVisualMode = STUN_VISUAL_SLOW_MO; // 0 = Slow Mo, 1 = Generic Stun, 2 = Static Hold
    bool g_PlayGenericStunAnim = false;     // Backwards-compat alias / toggle (disabled in favor of slow-mo)
    int  g_GenericStunActionId = 0x4205;    // Action ID for generic stun (0x4205 = Dizzy/Stumble, 0x4206 = Groggy Stagger)

    // Blockstun Pushback Customization
    bool  g_EnableCustomBlockPushback = false;
    float g_BlockPushbackMultiplier   = 1.00f; // Default 1.00x (Vanilla)
    bool  g_CornerPushbackReflection = true;  // Default ON

    // Extras User Presets (4 Slots)
    ExtrasPreset g_UserPresets[4] = { 0 };
    int g_ActivePresetSlot = -1;
    char g_PresetStatusMsg[128] = { 0 };
    uint64_t g_PresetStatusExpireFrame = 0;

    void SetPresetStatus(const char* fmt, ...) {
        va_list args;
        va_start(args, fmt);
        vsnprintf(g_PresetStatusMsg, sizeof(g_PresetStatusMsg), fmt, args);
        va_end(args);
        g_PresetStatusExpireFrame = g_CurrentFrame + 180;
    }

    const char* GetCurrentPresetName() {
        InitPresets();
        if (g_ActivePresetSlot >= 0 && g_ActivePresetSlot < 4) {
            if (g_UserPresets[g_ActivePresetSlot].name[0] != '\0') {
                return g_UserPresets[g_ActivePresetSlot].name;
            }
        }
        return "Vanilla Preset";
    }

    void ApplyVanillaPreset() {
        InitPresets();
        g_ActivePresetSlot = 0; // Vanilla Preset (Slot 0)
        g_GeneralBlockstunDelta = 0;
        g_GeneralHitstunDelta = 0;
        g_TargetScope = 0;
        g_StunVisualMode = STUN_VISUAL_SLOW_MO;

        g_CounterhitExtraHitstun = 0;
        g_CounterhitMinStartup = 0;
        g_CounterhitMoveTypeFilter = 0;
        g_CounterhitCameraMode = 0;
        g_CounterhitZoomEnabled = false;
        g_CounterhitZoomAmount = -12.0f;
        g_CounterhitCamShiftY = 330.0f;
        g_CounterhitCamYawOffset = 19.226f;
        g_EnableCounterhitMeterReward = false;
        g_CounterhitMeterReward = 0.0f;
        g_CounterhitMeterMoveFilter = 0;
        g_CounterhitMeterRecipient = 0;

        g_PunishExtraHitstun = 0;
        g_PunishMinStartup = 0;
        g_PunishMoveTypeFilter = 0;
        g_PunishCameraMode = 0;
        g_PunishZoomEnabled = false;
        g_PunishZoomAmount = -18.0f;
        g_PunishCamShiftY = 330.0f;
        g_PunishCamYawOffset = 19.226f;
        g_EnablePunishMeterReward = false;
        g_PunishMeterReward = 0.0f;
        g_PunishMeterMoveFilter = 0;
        g_PunishMeterRecipient = 0;

        g_TradeCameraMode = 0;
        g_TradeZoomEnabled = false;
        g_TradeZoomAmount = 8.0f;
        g_TradeZoomHoldFrames = 30;
        g_TradeCamShiftY = 0.0f;
        g_TradeCamYawOffset = 0.0f;
        g_EnableTradeMeterReward = false;
        g_TradeMeterReward = 0.0f;
        g_TradeMeterMoveFilter = 0;

        MeterDrainFix::SetCustomMeterCostsEnabled(false);
        MeterDrainFix::SetCustomEXCost(0.33f);
        MeterDrainFix::SetCustomBreakerCost(0.67f);
        MeterDrainFix::SetCustomXRayCost(1.00f);
        MeterDrainFix::g_ShowMeterPercentHUD = false;

        MeterDrainFix::g_EnableSpecialMoveMeterCost = false;
        MeterDrainFix::g_SpecialMoveMeterCost = 0.0f;
        MeterDrainFix::g_EnableAttackerHitMeterGain = false;
        MeterDrainFix::g_AttackerHitMeterReward = 0.020f;
        MeterDrainFix::g_EnableDefenderBlockMeterGain = false;
        MeterDrainFix::g_DefenderBlockMeterReward = 0.015f;
        MeterDrainFix::g_EnableDefenderHitMeterGain = false;
        MeterDrainFix::g_DefenderHitMeterReward = 0.025f;
        MeterDrainFix::g_DefenderHitMeterMoveFilter = 0;

        HitIndicationHUD::g_ShowHitIndicationHUD = false;

        ExtraGameplay::g_DisableQuickWakeup = false;
        ExtraGameplay::g_DisableRollWakeupAttack = false;
        ExtraGameplay::g_DisableDelayedWakeup = false;
        ExtraGameplay::g_DelayedWakeupFixedHold = false;
        ExtraGameplay::g_DelayedWakeupFixedFrames = 48;
        ExtraGameplay::g_DisableDelayedWakeupRoll = false;
        ExtraGameplay::g_TargetScope = 0;

        MovePriority::SetAll(false);

        FreeCamera::g_CinematicSlowMoEnabled = false;
        FreeCamera::g_CinematicSlowMoRate = 0.80f;
        FreeCamera::g_CinematicCameraSpeedIn = 0.23f;
        FreeCamera::g_CinematicCameraSpeedOut = 0.23f;

        ToastyTraining::g_ToastyScale = 1.00f;
        ToastyTraining::g_ToastyCorner = ToastyTraining::TOASTY_CORNER_BOTTOM_RIGHT;
        ToastyTraining::g_ToastyAutoFlip = false;
        ToastyTraining::g_ToastyFlipTop = false;
        ToastyTraining::g_ToastyRandomScale = false;

        SetPresetStatus("Applied Vanilla Preset");
        LogTrace("[PRESETS] Applied Vanilla Preset");
    }

    void ApplyDramaticModePreset(int camMode) {
        InitPresets();
        g_ActivePresetSlot = (camMode == 2) ? 2 : 1; // Slot 1 or 2
        g_GeneralBlockstunDelta = 0;
        g_GeneralHitstunDelta = 0;
        g_TargetScope = 0;
        g_StunVisualMode = STUN_VISUAL_SLOW_MO;

        g_CounterhitExtraHitstun = 9;
        g_CounterhitMinStartup = 16;
        g_CounterhitMoveTypeFilter = 1; // Normals Only
        g_CounterhitCameraMode = camMode;
        g_CounterhitZoomEnabled = (camMode > 0);
        if (camMode == 2) {
            g_CounterhitZoomAmount = 30.0f;
            g_CounterhitCamShiftY = 302.0f;
            g_CounterhitCamYawOffset = 45.0f;
        } else {
            g_CounterhitZoomAmount = -18.0f;
            g_CounterhitCamShiftY = 330.0f;
            g_CounterhitCamYawOffset = 19.226f; // 3500 units
        }
        g_EnableCounterhitMeterReward = true;
        g_CounterhitMeterReward = 0.05f;
        g_CounterhitMeterMoveFilter = 1; // Normals Only
        g_CounterhitMeterRecipient = 0;  // Hit Receiver (Defender)

        g_PunishExtraHitstun = 19;
        g_PunishMinStartup = 16;
        g_PunishMoveTypeFilter = 1; // Normals Only
        g_PunishCameraMode = camMode;
        g_PunishZoomEnabled = (camMode > 0);
        if (camMode == 2) {
            g_PunishZoomAmount = 30.0f;
            g_PunishCamShiftY = 302.0f;
            g_PunishCamYawOffset = 45.0f;
        } else {
            g_PunishZoomAmount = -22.9f;
            g_PunishCamShiftY = 330.0f;
            g_PunishCamYawOffset = 19.226f; // 3500 units
        }
        g_EnablePunishMeterReward = true;
        g_PunishMeterReward = 0.085f;
        g_PunishMeterMoveFilter = 1; // Normals Only
        g_PunishMeterRecipient = 0;  // Hit Receiver (Defender)

        g_TradeCameraMode = 1; // Option 1 Zoom pullback
        g_TradeZoomEnabled = true;
        g_TradeZoomAmount = 8.0f;
        g_TradeZoomHoldFrames = 30;
        g_TradeCamShiftY = 0.0f;
        g_TradeCamYawOffset = 0.0f;
        g_EnableTradeMeterReward = true;
        g_TradeMeterReward = 0.05f;
        g_TradeMeterMoveFilter = 0;

        // Dynamic Camera Slow-Motion & Smoothing (OFF on Cam 1, ON with screenshot settings on Cam 2)
        FreeCamera::g_CinematicSlowMoEnabled = (camMode == 2);
        FreeCamera::g_CinematicSlowMoRate = 0.80f;
        FreeCamera::g_CinematicCameraSpeedIn = 0.23f;
        FreeCamera::g_CinematicCameraSpeedOut = 0.23f;

        // Additional Dramatic Mode Settings:
        MeterDrainFix::SetCustomMeterCostsEnabled(true);
        MeterDrainFix::SetCustomEXCost(0.20f);       // 20.0%
        MeterDrainFix::SetCustomBreakerCost(0.50f);  // 50.0%
        MeterDrainFix::SetCustomXRayCost(1.00f);     // 100.0%
        MeterDrainFix::g_ShowMeterPercentHUD = true; // ON

        MeterDrainFix::g_EnableSpecialMoveMeterCost = false;
        MeterDrainFix::g_SpecialMoveMeterCost = 0.250f; // -25.0% per special
        MeterDrainFix::g_EnableAttackerHitMeterGain = true;
        MeterDrainFix::g_AttackerHitMeterRewardNormals = 0.008f; // +0.8%
        MeterDrainFix::g_AttackerHitMeterRewardSpecials = 0.000f;
        MeterDrainFix::g_AttackerHitMeterReward = 0.008f;
        MeterDrainFix::g_EnableDefenderBlockMeterGain = false;
        MeterDrainFix::g_DefenderBlockMeterRewardNormals = 0.015f;
        MeterDrainFix::g_DefenderBlockMeterRewardSpecials = 0.015f;
        MeterDrainFix::g_DefenderBlockMeterReward = 0.015f;
        MeterDrainFix::g_EnableDefenderHitMeterGain = true;
        MeterDrainFix::g_DefenderHitMeterRewardNormals = 0.008f; // +0.8%
        MeterDrainFix::g_DefenderHitMeterRewardSpecials = 0.000f;
        MeterDrainFix::g_DefenderHitMeterReward = 0.008f;

        // Move Telemetry HUD defaults (Matching Dramatic Mode standard)
        MoveTelemetryHUD::g_ShowMoveHUD = true;
        MoveTelemetryHUD::g_ShowActionHex = true;
        MoveTelemetryHUD::g_P1MoveHUDOffsetX = 100.0f;
        MoveTelemetryHUD::g_P1MoveHUDOffsetY = 50.0f;
        MoveTelemetryHUD::g_P2MoveHUDOffsetX = -100.0f;
        MoveTelemetryHUD::g_P2MoveHUDOffsetY = 50.0f;

        HitIndicationHUD::g_ShowHitIndicationHUD = true;

        SetPresetStatus("Applied Dramatic Mode Preset (Cam %d, SlowMo: %s, EX 20%%, Brk 50%%)",
                        camMode, (camMode == 2) ? "ON" : "OFF");
        LogTrace("[PRESETS] Applied Dramatic Mode Preset (Cam %d, SlowMo: %s)", camMode, (camMode == 2) ? "ON" : "OFF");
    }

    void InitPresets() {
        static bool s_inited = false;
        if (s_inited) return;
        s_inited = true;

        // Slot 1: Vanilla Preset
        {
            ExtrasPreset& p = g_UserPresets[0];
            strncpy_s(p.name, sizeof(p.name), "Vanilla Preset", _TRUNCATE);
            p.isPopulated = true;
            p.generalBlockstun = 0; p.generalHitstun = 0; p.targetScope = 0; p.stunVisualMode = 0;
            p.chExtraHitstun = 0; p.chMinStartup = 0; p.chMoveFilter = 0; p.chCameraMode = 0;
            p.chZoomAmount = -12.0f; p.chCamShiftY = 330.0f; p.chCamYawOffset = 19.226f;
            p.chEnableMeter = false; p.chMeterReward = 0.0f; p.chMeterFilter = 0; p.chMeterRecipient = 0;
            p.punishExtraHitstun = 0; p.punishMinStartup = 0; p.punishMoveFilter = 0; p.punishCameraMode = 0;
            p.punishZoomAmount = -18.0f; p.punishCamShiftY = 330.0f; p.punishCamYawOffset = 19.226f;
            p.punishEnableMeter = false; p.punishMeterReward = 0.0f; p.punishMeterFilter = 0; p.punishMeterRecipient = 0;
            p.tradeCameraMode = 0; p.tradeZoomAmount = 8.0f; p.tradeZoomHold = 30;
            p.tradeEnableMeter = false; p.tradeMeterReward = 0.0f; p.tradeMeterFilter = 0;
            p.dynSlowMo = false; p.dynSlowMoRate = 0.80f; p.dynCamSpeedIn = 0.23f; p.dynCamSpeedOut = 0.23f;
            p.customMeterCosts = false; p.exCost = 0.33f; p.breakerCost = 0.67f; p.xrayCost = 1.00f;
            p.showMeterHUD = false; p.enableSpecialCost = false; p.specialCost = 0.0f;
            p.enableAttGain = false; p.attGain = 0.02f; p.attFilter = 0;
            p.enableDefBlockGain = false; p.defBlockGain = 0.015f; p.defBlockFilter = 0;
            p.enableDefDmgGain = false; p.defDmgGain = 0.025f; p.defDmgFilter = 0;
            p.showHitOutcomeBadges = false;
            p.disableBlood = false;
            p.disableBackground = false; p.enableFloorGrid = false;
            p.floorGridColor[0] = 0.0f; p.floorGridColor[1] = 0.85f; p.floorGridColor[2] = 1.0f;
            p.floorGridCenterColor[0] = 1.0f; p.floorGridCenterColor[1] = 1.0f; p.floorGridCenterColor[2] = 1.0f;
            p.showGridCornerWalls = true; p.gridWallColor[0] = 1.0f; p.gridWallColor[1] = 0.25f; p.gridWallColor[2] = 0.25f;
            p.disableQuickWakeup = false; p.disableRollWakeup = false; p.disableDelayedWakeup = false;
            p.delayedWakeupFixedHold = false; p.delayedWakeupFixedFrames = 48; p.disableDelayedWakeupRoll = false; p.wakeupScope = 0;
            p.uppercutAA = false; p.uppercutVsSpecial = false; p.uppercutVsUppercut = false; p.jumpInPriority = false; p.xrayPriority = false; p.instantProjectiles = false;
        }

        // Slot 2: Dramatic Mode (Cam 1 FOV)
        {
            ExtrasPreset& p = g_UserPresets[1];
            strncpy_s(p.name, sizeof(p.name), "Dramatic (Cam 1 FOV)", _TRUNCATE);
            p.isPopulated = true;
            p.generalBlockstun = 0; p.generalHitstun = 0; p.targetScope = 0; p.stunVisualMode = 0;
            p.chExtraHitstun = 9; p.chMinStartup = 0; p.chMoveFilter = 1; p.chCameraMode = 1; // Cam 1 FOV
            p.chZoomAmount = -18.0f; p.chCamShiftY = 330.0f; p.chCamYawOffset = 19.226f;
            p.chEnableMeter = true; p.chMeterReward = 0.05f; p.chMeterFilter = 1; p.chMeterRecipient = 0;
            p.punishExtraHitstun = 19; p.punishMinStartup = 0; p.punishMoveFilter = 1; p.punishCameraMode = 1; // Cam 1 FOV
            p.punishZoomAmount = -22.9f; p.punishCamShiftY = 330.0f; p.punishCamYawOffset = 19.226f;
            p.punishEnableMeter = true; p.punishMeterReward = 0.085f; p.punishMeterFilter = 1; p.punishMeterRecipient = 0;
            p.tradeCameraMode = 1; p.tradeZoomAmount = 8.0f; p.tradeZoomHold = 30;
            p.tradeEnableMeter = true; p.tradeMeterReward = 0.05f; p.tradeMeterFilter = 0;
            p.dynSlowMo = false; p.dynSlowMoRate = 0.80f; p.dynCamSpeedIn = 0.23f; p.dynCamSpeedOut = 0.23f; // SlowMo OFF on Cam 1
            p.customMeterCosts = true; p.exCost = 0.20f; p.breakerCost = 0.50f; p.xrayCost = 1.00f;
            p.showMeterHUD = true; p.enableSpecialCost = false; p.specialCost = 0.250f;
            p.enableAttGain = true; p.attGain = 0.008f; p.attFilter = 1;
            p.enableDefBlockGain = false; p.defBlockGain = 0.015f; p.defBlockFilter = 0;
            p.enableDefDmgGain = true; p.defDmgGain = 0.008f; p.defDmgFilter = 1;
            p.showHitOutcomeBadges = true;
            p.disableBlood = false;
            p.disableBackground = false; p.enableFloorGrid = false;
            p.floorGridColor[0] = 0.0f; p.floorGridColor[1] = 0.85f; p.floorGridColor[2] = 1.0f;
            p.floorGridCenterColor[0] = 1.0f; p.floorGridCenterColor[1] = 1.0f; p.floorGridCenterColor[2] = 1.0f;
            p.showGridCornerWalls = true; p.gridWallColor[0] = 1.0f; p.gridWallColor[1] = 0.25f; p.gridWallColor[2] = 0.25f;
            p.disableQuickWakeup = false; p.disableRollWakeup = false; p.disableDelayedWakeup = false;
            p.delayedWakeupFixedHold = false; p.delayedWakeupFixedFrames = 48; p.disableDelayedWakeupRoll = false; p.wakeupScope = 0;
            p.uppercutAA = false; p.uppercutVsSpecial = false; p.uppercutVsUppercut = false; p.jumpInPriority = false; p.xrayPriority = false; p.instantProjectiles = false;
        }

        // Slot 3: Dramatic Mode (Cam 2 Angled)
        {
            ExtrasPreset& p = g_UserPresets[2];
            strncpy_s(p.name, sizeof(p.name), "Dramatic (Cam 2 Angled)", _TRUNCATE);
            p.isPopulated = true;
            p.generalBlockstun = 0; p.generalHitstun = 0; p.targetScope = 0; p.stunVisualMode = 0;
            p.chExtraHitstun = 9; p.chMinStartup = 0; p.chMoveFilter = 1; p.chCameraMode = 2; // Cam 2 Angled
            p.chZoomAmount = 30.0f; p.chCamShiftY = 302.0f; p.chCamYawOffset = 45.0f;
            p.chEnableMeter = true; p.chMeterReward = 0.05f; p.chMeterFilter = 1; p.chMeterRecipient = 0;
            p.punishExtraHitstun = 19; p.punishMinStartup = 0; p.punishMoveFilter = 1; p.punishCameraMode = 2; // Cam 2 Angled
            p.punishZoomAmount = 30.0f; p.punishCamShiftY = 302.0f; p.punishCamYawOffset = 45.0f;
            p.punishEnableMeter = true; p.punishMeterReward = 0.085f; p.punishMeterFilter = 1; p.punishMeterRecipient = 0;
            p.tradeCameraMode = 1; p.tradeZoomAmount = 8.0f; p.tradeZoomHold = 30;
            p.tradeEnableMeter = true; p.tradeMeterReward = 0.05f; p.tradeMeterFilter = 0;
            p.dynSlowMo = true; p.dynSlowMoRate = 0.80f; p.dynCamSpeedIn = 0.23f; p.dynCamSpeedOut = 0.23f; // SlowMo ON on Cam 2
            p.customMeterCosts = true; p.exCost = 0.20f; p.breakerCost = 0.50f; p.xrayCost = 1.00f;
            p.showMeterHUD = true; p.enableSpecialCost = false; p.specialCost = 0.250f;
            p.enableAttGain = true; p.attGain = 0.008f; p.attFilter = 1;
            p.enableDefBlockGain = false; p.defBlockGain = 0.015f; p.defBlockFilter = 0;
            p.enableDefDmgGain = true; p.defDmgGain = 0.008f; p.defDmgFilter = 1;
            p.showHitOutcomeBadges = true;
            p.disableBlood = false;
            p.disableBackground = false; p.enableFloorGrid = false;
            p.floorGridColor[0] = 0.0f; p.floorGridColor[1] = 0.85f; p.floorGridColor[2] = 1.0f;
            p.floorGridCenterColor[0] = 1.0f; p.floorGridCenterColor[1] = 1.0f; p.floorGridCenterColor[2] = 1.0f;
            p.showGridCornerWalls = true; p.gridWallColor[0] = 1.0f; p.gridWallColor[1] = 0.25f; p.gridWallColor[2] = 0.25f;
            p.disableQuickWakeup = false; p.disableRollWakeup = false; p.disableDelayedWakeup = false;
            p.delayedWakeupFixedHold = false; p.delayedWakeupFixedFrames = 48; p.disableDelayedWakeupRoll = false; p.wakeupScope = 0;
            p.uppercutAA = false; p.uppercutVsSpecial = false; p.uppercutVsUppercut = false; p.jumpInPriority = false; p.xrayPriority = false; p.instantProjectiles = false;
        }

        // Slot 4: User Custom
        {
            ExtrasPreset& p = g_UserPresets[3];
            strncpy_s(p.name, sizeof(p.name), "Custom Preset 4", _TRUNCATE);
            p.isPopulated = false;
        }

        // Default preset on Extras is Vanilla Preset (Slot 0)
        g_ActivePresetSlot = 0;
    }

    void SaveUserPreset(int slotIdx, const char* name) {
        InitPresets();
        if (slotIdx < 0 || slotIdx >= 4) return;
        ExtrasPreset& p = g_UserPresets[slotIdx];
        if (name && name[0] != '\0') {
            strncpy_s(p.name, sizeof(p.name), name, _TRUNCATE);
        } else if (p.name[0] == '\0') {
            snprintf(p.name, sizeof(p.name), "Preset %d", slotIdx + 1);
        }
        p.isPopulated = true;

        p.generalBlockstun = g_GeneralBlockstunDelta;
        p.generalHitstun = g_GeneralHitstunDelta;
        p.targetScope = g_TargetScope;
        p.stunVisualMode = g_StunVisualMode;

        p.chExtraHitstun = g_CounterhitExtraHitstun;
        p.chMinStartup = g_CounterhitMinStartup;
        p.chMoveFilter = g_CounterhitMoveTypeFilter;
        p.chCameraMode = g_CounterhitCameraMode;
        p.chZoomAmount = g_CounterhitZoomAmount;
        p.chCamShiftY = g_CounterhitCamShiftY;
        p.chCamYawOffset = g_CounterhitCamYawOffset;
        p.chEnableMeter = g_EnableCounterhitMeterReward;
        p.chMeterReward = g_CounterhitMeterReward;
        p.chMeterFilter = g_CounterhitMeterMoveFilter;
        p.chMeterRecipient = g_CounterhitMeterRecipient;

        p.punishExtraHitstun = g_PunishExtraHitstun;
        p.punishMinStartup = g_PunishMinStartup;
        p.punishMoveFilter = g_PunishMoveTypeFilter;
        p.punishCameraMode = g_PunishCameraMode;
        p.punishZoomAmount = g_PunishZoomAmount;
        p.punishCamShiftY = g_PunishCamShiftY;
        p.punishCamYawOffset = g_PunishCamYawOffset;
        p.punishEnableMeter = g_EnablePunishMeterReward;
        p.punishMeterReward = g_PunishMeterReward;
        p.punishMeterFilter = g_PunishMeterMoveFilter;
        p.punishMeterRecipient = g_PunishMeterRecipient;

        p.tradeCameraMode = g_TradeCameraMode;
        p.tradeZoomAmount = g_TradeZoomAmount;
        p.tradeZoomHold = g_TradeZoomHoldFrames;
        p.tradeEnableMeter = g_EnableTradeMeterReward;
        p.tradeMeterReward = g_TradeMeterReward;
        p.tradeMeterFilter = g_TradeMeterMoveFilter;

        p.dynSlowMo = FreeCamera::g_CinematicSlowMoEnabled;
        p.dynSlowMoRate = FreeCamera::g_CinematicSlowMoRate;
        p.dynCamSpeedIn = FreeCamera::g_CinematicCameraSpeedIn;
        p.dynCamSpeedOut = FreeCamera::g_CinematicCameraSpeedOut;

        p.customMeterCosts = MeterDrainFix::g_CustomMeterCostsEnabled;
        p.exCost = MeterDrainFix::g_CustomEXCost;
        p.breakerCost = MeterDrainFix::g_CustomBreakerCost;
        p.xrayCost = MeterDrainFix::g_CustomXRayCost;
        p.showMeterHUD = MeterDrainFix::g_ShowMeterPercentHUD;

        p.enableSpecialCost = MeterDrainFix::g_EnableSpecialMoveMeterCost;
        p.specialCost = MeterDrainFix::g_SpecialMoveMeterCost;
        p.enableAttGain = MeterDrainFix::g_EnableAttackerHitMeterGain;
        p.attGain = MeterDrainFix::g_AttackerHitMeterReward;
        p.attFilter = MeterDrainFix::g_AttackerHitMeterMoveFilter;
        p.enableDefBlockGain = MeterDrainFix::g_EnableDefenderBlockMeterGain;
        p.defBlockGain = MeterDrainFix::g_DefenderBlockMeterReward;
        p.defBlockFilter = MeterDrainFix::g_DefenderBlockMeterMoveFilter;
        p.enableDefDmgGain = MeterDrainFix::g_EnableDefenderHitMeterGain;
        p.defDmgGain = MeterDrainFix::g_DefenderHitMeterReward;
        p.defDmgFilter = MeterDrainFix::g_DefenderHitMeterMoveFilter;

        p.showHitOutcomeBadges = HitIndicationHUD::g_ShowHitIndicationHUD;
        p.disableBlood = ExtraGameplay::g_DisableBlood;
        p.disableBackground = ExtraGameplay::g_DisableBackground;
        p.voidBgColor[0] = ExtraGameplay::g_VoidBgColor[0];
        p.voidBgColor[1] = ExtraGameplay::g_VoidBgColor[1];
        p.voidBgColor[2] = ExtraGameplay::g_VoidBgColor[2];
        p.enableFloorGrid = ExtraGameplay::g_EnableFloorGrid;
        p.floorGridColor[0] = ExtraGameplay::g_FloorGridColor[0];
        p.floorGridColor[1] = ExtraGameplay::g_FloorGridColor[1];
        p.floorGridColor[2] = ExtraGameplay::g_FloorGridColor[2];
        p.floorGridCenterColor[0] = ExtraGameplay::g_FloorGridCenterColor[0];
        p.floorGridCenterColor[1] = ExtraGameplay::g_FloorGridCenterColor[1];
        p.floorGridCenterColor[2] = ExtraGameplay::g_FloorGridCenterColor[2];
        p.showGridCornerWalls = ExtraGameplay::g_ShowGridCornerWalls;
        p.gridWallColor[0] = ExtraGameplay::g_GridWallColor[0];
        p.gridWallColor[1] = ExtraGameplay::g_GridWallColor[1];
        p.gridWallColor[2] = ExtraGameplay::g_GridWallColor[2];

        p.disableQuickWakeup = ExtraGameplay::g_DisableQuickWakeup;
        p.disableRollWakeup = ExtraGameplay::g_DisableRollWakeupAttack;
        p.disableDelayedWakeup = ExtraGameplay::g_DisableDelayedWakeup;
        p.delayedWakeupFixedHold = ExtraGameplay::g_DelayedWakeupFixedHold;
        p.delayedWakeupFixedFrames = ExtraGameplay::g_DelayedWakeupFixedFrames;
        p.disableDelayedWakeupRoll = ExtraGameplay::g_DisableDelayedWakeupRoll;
        p.wakeupScope = ExtraGameplay::g_TargetScope;

        p.uppercutAA = MovePriority::g_UppercutAntiAir;
        p.uppercutVsSpecial = MovePriority::g_UppercutVsSpecial;
        p.uppercutVsUppercut = MovePriority::g_UppercutVsUppercut;
        p.jumpInPriority = MovePriority::g_JumpInPriority;
        p.xrayPriority = MovePriority::g_XRayAbsolutePriority;
        p.instantProjectiles = MovePriority::g_InstantProjectiles;

        g_ActivePresetSlot = slotIdx;
        SetPresetStatus("Saved settings to %s (Slot %d)", p.name, slotIdx + 1);
        LogTrace("[PRESETS] Saved settings to %s (Slot %d)", p.name, slotIdx + 1);
    }

    bool LoadUserPreset(int slotIdx) {
        InitPresets();
        if (slotIdx < 0 || slotIdx >= 4) return false;
        const ExtrasPreset& p = g_UserPresets[slotIdx];
        if (!p.isPopulated) {
            SetPresetStatus("Slot %d is empty", slotIdx + 1);
            return false;
        }

        g_GeneralBlockstunDelta = p.generalBlockstun;
        g_GeneralHitstunDelta = p.generalHitstun;
        g_TargetScope = p.targetScope;
        g_StunVisualMode = p.stunVisualMode;

        g_CounterhitExtraHitstun = p.chExtraHitstun;
        g_CounterhitMinStartup = p.chMinStartup;
        g_CounterhitMoveTypeFilter = p.chMoveFilter;
        g_CounterhitCameraMode = p.chCameraMode;
        g_CounterhitZoomEnabled = (p.chCameraMode > 0);
        g_CounterhitZoomAmount = p.chZoomAmount;
        g_CounterhitCamShiftY = p.chCamShiftY;
        g_CounterhitCamYawOffset = p.chCamYawOffset;
        g_EnableCounterhitMeterReward = p.chEnableMeter;
        g_CounterhitMeterReward = p.chMeterReward;
        g_CounterhitMeterMoveFilter = p.chMeterFilter;
        g_CounterhitMeterRecipient = p.chMeterRecipient;

        g_PunishExtraHitstun = p.punishExtraHitstun;
        g_PunishMinStartup = p.punishMinStartup;
        g_PunishMoveTypeFilter = p.punishMoveFilter;
        g_PunishCameraMode = p.punishCameraMode;
        g_PunishZoomEnabled = (p.punishCameraMode > 0);
        g_PunishZoomAmount = p.punishZoomAmount;
        g_PunishCamShiftY = p.punishCamShiftY;
        g_PunishCamYawOffset = p.punishCamYawOffset;
        g_EnablePunishMeterReward = p.punishEnableMeter;
        g_PunishMeterReward = p.punishMeterReward;
        g_PunishMeterMoveFilter = p.punishMeterFilter;
        g_PunishMeterRecipient = p.punishMeterRecipient;

        g_TradeCameraMode = p.tradeCameraMode;
        g_TradeZoomEnabled = (p.tradeCameraMode > 0);
        g_TradeZoomAmount = p.tradeZoomAmount;
        g_TradeZoomHoldFrames = p.tradeZoomHold;
        g_EnableTradeMeterReward = p.tradeEnableMeter;
        g_TradeMeterReward = p.tradeMeterReward;
        g_TradeMeterMoveFilter = p.tradeMeterFilter;

        FreeCamera::g_CinematicSlowMoEnabled = p.dynSlowMo;
        FreeCamera::g_CinematicSlowMoRate = p.dynSlowMoRate > 0.05f ? p.dynSlowMoRate : 0.80f;
        FreeCamera::g_CinematicCameraSpeedIn = p.dynCamSpeedIn > 0.01f ? p.dynCamSpeedIn : 0.23f;
        FreeCamera::g_CinematicCameraSpeedOut = p.dynCamSpeedOut > 0.01f ? p.dynCamSpeedOut : 0.23f;

        MeterDrainFix::SetCustomMeterCostsEnabled(p.customMeterCosts);
        MeterDrainFix::SetCustomEXCost(p.exCost > 0.01f ? p.exCost : 0.33f);
        MeterDrainFix::SetCustomBreakerCost(p.breakerCost > 0.01f ? p.breakerCost : 0.67f);
        MeterDrainFix::SetCustomXRayCost(p.xrayCost > 0.01f ? p.xrayCost : 1.00f);
        MeterDrainFix::g_ShowMeterPercentHUD = p.showMeterHUD;

        MeterDrainFix::g_EnableSpecialMoveMeterCost = p.enableSpecialCost;
        MeterDrainFix::g_SpecialMoveMeterCost = p.specialCost;
        MeterDrainFix::g_EnableAttackerHitMeterGain = p.enableAttGain;
        MeterDrainFix::g_AttackerHitMeterRewardNormals = p.attGain;
        MeterDrainFix::g_AttackerHitMeterRewardSpecials = (p.attFilter == 1) ? 0.0f : p.attGain;
        MeterDrainFix::g_AttackerHitMeterReward = p.attGain;
        MeterDrainFix::g_EnableDefenderBlockMeterGain = p.enableDefBlockGain;
        MeterDrainFix::g_DefenderBlockMeterRewardNormals = p.defBlockGain;
        MeterDrainFix::g_DefenderBlockMeterRewardSpecials = (p.defBlockFilter == 1) ? 0.0f : p.defBlockGain;
        MeterDrainFix::g_DefenderBlockMeterReward = p.defBlockGain;
        MeterDrainFix::g_EnableDefenderHitMeterGain = p.enableDefDmgGain;
        MeterDrainFix::g_DefenderHitMeterRewardNormals = p.defDmgGain;
        MeterDrainFix::g_DefenderHitMeterRewardSpecials = (p.defDmgFilter == 1) ? 0.0f : p.defDmgGain;
        MeterDrainFix::g_DefenderHitMeterReward = p.defDmgGain;

        HitIndicationHUD::g_ShowHitIndicationHUD = p.showHitOutcomeBadges;
        ExtraGameplay::g_DisableBlood = p.disableBlood;
        ExtraGameplay::g_DisableBackground = p.disableBackground;
        ExtraGameplay::g_VoidBgColor[0] = p.voidBgColor[0];
        ExtraGameplay::g_VoidBgColor[1] = p.voidBgColor[1];
        ExtraGameplay::g_VoidBgColor[2] = p.voidBgColor[2];
        ExtraGameplay::g_EnableFloorGrid = p.enableFloorGrid;
        ExtraGameplay::g_FloorGridColor[0] = p.floorGridColor[0];
        ExtraGameplay::g_FloorGridColor[1] = p.floorGridColor[1];
        ExtraGameplay::g_FloorGridColor[2] = p.floorGridColor[2];
        ExtraGameplay::g_FloorGridCenterColor[0] = p.floorGridCenterColor[0];
        ExtraGameplay::g_FloorGridCenterColor[1] = p.floorGridCenterColor[1];
        ExtraGameplay::g_FloorGridCenterColor[2] = p.floorGridCenterColor[2];
        ExtraGameplay::g_ShowGridCornerWalls = p.showGridCornerWalls;
        ExtraGameplay::g_GridWallColor[0] = p.gridWallColor[0];
        ExtraGameplay::g_GridWallColor[1] = p.gridWallColor[1];
        ExtraGameplay::g_GridWallColor[2] = p.gridWallColor[2];

        ExtraGameplay::g_DisableQuickWakeup = p.disableQuickWakeup;
        ExtraGameplay::g_DisableRollWakeupAttack = p.disableRollWakeup;
        ExtraGameplay::g_DisableDelayedWakeup = p.disableDelayedWakeup;
        ExtraGameplay::g_DelayedWakeupFixedHold = p.delayedWakeupFixedHold;
        ExtraGameplay::g_DelayedWakeupFixedFrames = p.delayedWakeupFixedFrames;
        ExtraGameplay::g_DisableDelayedWakeupRoll = p.disableDelayedWakeupRoll;
        ExtraGameplay::g_TargetScope = p.wakeupScope;

        MovePriority::g_UppercutAntiAir = p.uppercutAA;
        MovePriority::g_UppercutVsSpecial = p.uppercutVsSpecial;
        MovePriority::g_UppercutVsUppercut = p.uppercutVsUppercut;
        MovePriority::g_JumpInPriority = p.jumpInPriority;
        MovePriority::g_XRayAbsolutePriority = p.xrayPriority;
        MovePriority::g_InstantProjectiles = p.instantProjectiles;

        g_ActivePresetSlot = slotIdx;
        SetPresetStatus("Loaded %s (Slot %d)", p.name, slotIdx + 1);
        LogTrace("[PRESETS] Loaded %s (Slot %d)", p.name, slotIdx + 1);
        return true;
    }

    bool ExportExtrasPresetsToFile(const char* filepath) {
        InitPresets();
        FILE* f = nullptr;
        fopen_s(&f, filepath ? filepath : "mk9_extras_presets.ini", "w");
        if (!f) {
            SetPresetStatus("Failed to open file for export: %s", filepath ? filepath : "mk9_extras_presets.ini");
            return false;
        }

        fprintf(f, "# MK9 Extras Preset Configuration File\n\n");
        for (int i = 0; i < 4; ++i) {
            const ExtrasPreset& p = g_UserPresets[i];
            fprintf(f, "[Preset_%d]\n", i + 1);
            fprintf(f, "name=%s\n", p.name);
            fprintf(f, "isPopulated=%d\n", p.isPopulated ? 1 : 0);
            fprintf(f, "generalBlockstun=%d\n", p.generalBlockstun);
            fprintf(f, "generalHitstun=%d\n", p.generalHitstun);
            fprintf(f, "targetScope=%d\n", p.targetScope);
            fprintf(f, "stunVisualMode=%d\n", p.stunVisualMode);
            fprintf(f, "chExtraHitstun=%d\n", p.chExtraHitstun);
            fprintf(f, "chMinStartup=%d\n", p.chMinStartup);
            fprintf(f, "chMoveFilter=%d\n", p.chMoveFilter);
            fprintf(f, "chCameraMode=%d\n", p.chCameraMode);
            fprintf(f, "chZoomAmount=%.3f\n", p.chZoomAmount);
            fprintf(f, "chCamShiftY=%.3f\n", p.chCamShiftY);
            fprintf(f, "chCamYawOffset=%.3f\n", p.chCamYawOffset);
            fprintf(f, "chEnableMeter=%d\n", p.chEnableMeter ? 1 : 0);
            fprintf(f, "chMeterReward=%.4f\n", p.chMeterReward);
            fprintf(f, "chMeterFilter=%d\n", p.chMeterFilter);
            fprintf(f, "chMeterRecipient=%d\n", p.chMeterRecipient);
            fprintf(f, "punishExtraHitstun=%d\n", p.punishExtraHitstun);
            fprintf(f, "punishMinStartup=%d\n", p.punishMinStartup);
            fprintf(f, "punishMoveFilter=%d\n", p.punishMoveFilter);
            fprintf(f, "punishCameraMode=%d\n", p.punishCameraMode);
            fprintf(f, "punishZoomAmount=%.3f\n", p.punishZoomAmount);
            fprintf(f, "punishCamShiftY=%.3f\n", p.punishCamShiftY);
            fprintf(f, "punishCamYawOffset=%.3f\n", p.punishCamYawOffset);
            fprintf(f, "punishEnableMeter=%d\n", p.punishEnableMeter ? 1 : 0);
            fprintf(f, "punishMeterReward=%.4f\n", p.punishMeterReward);
            fprintf(f, "punishMeterFilter=%d\n", p.punishMeterFilter);
            fprintf(f, "punishMeterRecipient=%d\n", p.punishMeterRecipient);
            fprintf(f, "tradeCameraMode=%d\n", p.tradeCameraMode);
            fprintf(f, "tradeZoomAmount=%.3f\n", p.tradeZoomAmount);
            fprintf(f, "tradeZoomHold=%d\n", p.tradeZoomHold);
            fprintf(f, "tradeEnableMeter=%d\n", p.tradeEnableMeter ? 1 : 0);
            fprintf(f, "tradeMeterReward=%.4f\n", p.tradeMeterReward);
            fprintf(f, "tradeMeterFilter=%d\n", p.tradeMeterFilter);
            fprintf(f, "dynSlowMo=%d\n", p.dynSlowMo ? 1 : 0);
            fprintf(f, "dynSlowMoRate=%.3f\n", p.dynSlowMoRate);
            fprintf(f, "dynCamSpeedIn=%.3f\n", p.dynCamSpeedIn);
            fprintf(f, "dynCamSpeedOut=%.3f\n", p.dynCamSpeedOut);
            fprintf(f, "customMeterCosts=%d\n", p.customMeterCosts ? 1 : 0);
            fprintf(f, "exCost=%.4f\n", p.exCost);
            fprintf(f, "breakerCost=%.4f\n", p.breakerCost);
            fprintf(f, "xrayCost=%.4f\n", p.xrayCost);
            fprintf(f, "showMeterHUD=%d\n", p.showMeterHUD ? 1 : 0);
            fprintf(f, "enableSpecialCost=%d\n", p.enableSpecialCost ? 1 : 0);
            fprintf(f, "specialCost=%.4f\n", p.specialCost);
            fprintf(f, "enableAttGain=%d\n", p.enableAttGain ? 1 : 0);
            fprintf(f, "attGain=%.4f\n", p.attGain);
            fprintf(f, "attFilter=%d\n", p.attFilter);
            fprintf(f, "enableDefBlockGain=%d\n", p.enableDefBlockGain ? 1 : 0);
            fprintf(f, "defBlockGain=%.4f\n", p.defBlockGain);
            fprintf(f, "defBlockFilter=%d\n", p.defBlockFilter);
            fprintf(f, "enableDefDmgGain=%d\n", p.enableDefDmgGain ? 1 : 0);
            fprintf(f, "defDmgGain=%.4f\n", p.defDmgGain);
            fprintf(f, "defDmgFilter=%d\n", p.defDmgFilter);
            fprintf(f, "showHitOutcomeBadges=%d\n", p.showHitOutcomeBadges ? 1 : 0);
            fprintf(f, "disableBlood=%d\n", p.disableBlood ? 1 : 0);
            fprintf(f, "disableBackground=%d\n", p.disableBackground ? 1 : 0);
            fprintf(f, "voidBgColor=%.3f,%.3f,%.3f\n", p.voidBgColor[0], p.voidBgColor[1], p.voidBgColor[2]);
            fprintf(f, "enableFloorGrid=%d\n", p.enableFloorGrid ? 1 : 0);
            fprintf(f, "floorGridColor=%.3f,%.3f,%.3f\n", p.floorGridColor[0], p.floorGridColor[1], p.floorGridColor[2]);
            fprintf(f, "floorGridCenterColor=%.3f,%.3f,%.3f\n", p.floorGridCenterColor[0], p.floorGridCenterColor[1], p.floorGridCenterColor[2]);
            fprintf(f, "showGridCornerWalls=%d\n", p.showGridCornerWalls ? 1 : 0);
            fprintf(f, "gridWallColor=%.3f,%.3f,%.3f\n", p.gridWallColor[0], p.gridWallColor[1], p.gridWallColor[2]);
            fprintf(f, "disableQuickWakeup=%d\n", p.disableQuickWakeup ? 1 : 0);
            fprintf(f, "disableRollWakeup=%d\n", p.disableRollWakeup ? 1 : 0);
            fprintf(f, "disableDelayedWakeup=%d\n", p.disableDelayedWakeup ? 1 : 0);
            fprintf(f, "delayedWakeupFixedHold=%d\n", p.delayedWakeupFixedHold ? 1 : 0);
            fprintf(f, "delayedWakeupFixedFrames=%d\n", p.delayedWakeupFixedFrames);
            fprintf(f, "disableDelayedWakeupRoll=%d\n", p.disableDelayedWakeupRoll ? 1 : 0);
            fprintf(f, "wakeupScope=%d\n", p.wakeupScope);
            fprintf(f, "uppercutAA=%d\n", p.uppercutAA ? 1 : 0);
            fprintf(f, "uppercutVsSpecial=%d\n", p.uppercutVsSpecial ? 1 : 0);
            fprintf(f, "uppercutVsUppercut=%d\n", p.uppercutVsUppercut ? 1 : 0);
            fprintf(f, "jumpInPriority=%d\n", p.jumpInPriority ? 1 : 0);
            fprintf(f, "xrayPriority=%d\n", p.xrayPriority ? 1 : 0);
            fprintf(f, "instantProjectiles=%d\n\n", p.instantProjectiles ? 1 : 0);
        }

        fclose(f);
        SetPresetStatus("Exported presets to %s", filepath ? filepath : "mk9_extras_presets.ini");
        LogTrace("[PRESETS] Exported presets to %s", filepath ? filepath : "mk9_extras_presets.ini");
        return true;
    }

    bool ImportExtrasPresetsFromFile(const char* filepath) {
        InitPresets();
        FILE* f = nullptr;
        fopen_s(&f, filepath ? filepath : "mk9_extras_presets.ini", "r");
        if (!f) {
            SetPresetStatus("Failed to open file for import: %s", filepath ? filepath : "mk9_extras_presets.ini");
            return false;
        }

        char line[256];
        int currentPreset = -1;
        while (fgets(line, sizeof(line), f)) {
            // Trim whitespace
            char* p = line;
            while (*p == ' ' || *p == '\t') p++;
            if (*p == '#' || *p == ';' || *p == '\0' || *p == '\r' || *p == '\n') continue;

            if (*p == '[') {
                int num = 0;
                if (sscanf_s(p, "[Preset_%d]", &num) == 1 && num >= 1 && num <= 4) {
                    currentPreset = num - 1;
                } else {
                    currentPreset = -1;
                }
                continue;
            }

            if (currentPreset >= 0 && currentPreset < 4) {
                ExtrasPreset& ep = g_UserPresets[currentPreset];
                char key[64], val[128];
                if (sscanf_s(p, "%63[^=]=%127[^\r\n]", key, (unsigned int)sizeof(key), val, (unsigned int)sizeof(val)) == 2) {
                    if (strcmp(key, "name") == 0) strncpy_s(ep.name, sizeof(ep.name), val, _TRUNCATE);
                    else if (strcmp(key, "isPopulated") == 0) ep.isPopulated = (atoi(val) != 0);
                    else if (strcmp(key, "generalBlockstun") == 0) ep.generalBlockstun = atoi(val);
                    else if (strcmp(key, "generalHitstun") == 0) ep.generalHitstun = atoi(val);
                    else if (strcmp(key, "targetScope") == 0) ep.targetScope = atoi(val);
                    else if (strcmp(key, "stunVisualMode") == 0) ep.stunVisualMode = atoi(val);
                    else if (strcmp(key, "chExtraHitstun") == 0) ep.chExtraHitstun = atoi(val);
                    else if (strcmp(key, "chMinStartup") == 0) ep.chMinStartup = atoi(val);
                    else if (strcmp(key, "chMoveFilter") == 0) ep.chMoveFilter = atoi(val);
                    else if (strcmp(key, "chCameraMode") == 0) ep.chCameraMode = atoi(val);
                    else if (strcmp(key, "chZoomAmount") == 0) ep.chZoomAmount = (float)atof(val);
                    else if (strcmp(key, "chCamShiftY") == 0) ep.chCamShiftY = (float)atof(val);
                    else if (strcmp(key, "chCamYawOffset") == 0) ep.chCamYawOffset = (float)atof(val);
                    else if (strcmp(key, "chEnableMeter") == 0) ep.chEnableMeter = (atoi(val) != 0);
                    else if (strcmp(key, "chMeterReward") == 0) ep.chMeterReward = (float)atof(val);
                    else if (strcmp(key, "chMeterFilter") == 0) ep.chMeterFilter = atoi(val);
                    else if (strcmp(key, "chMeterRecipient") == 0) ep.chMeterRecipient = atoi(val);
                    else if (strcmp(key, "punishExtraHitstun") == 0) ep.punishExtraHitstun = atoi(val);
                    else if (strcmp(key, "punishMinStartup") == 0) ep.punishMinStartup = atoi(val);
                    else if (strcmp(key, "punishMoveFilter") == 0) ep.punishMoveFilter = atoi(val);
                    else if (strcmp(key, "punishCameraMode") == 0) ep.punishCameraMode = atoi(val);
                    else if (strcmp(key, "punishZoomAmount") == 0) ep.punishZoomAmount = (float)atof(val);
                    else if (strcmp(key, "punishCamShiftY") == 0) ep.punishCamShiftY = (float)atof(val);
                    else if (strcmp(key, "punishCamYawOffset") == 0) ep.punishCamYawOffset = (float)atof(val);
                    else if (strcmp(key, "punishEnableMeter") == 0) ep.punishEnableMeter = (atoi(val) != 0);
                    else if (strcmp(key, "punishMeterReward") == 0) ep.punishMeterReward = (float)atof(val);
                    else if (strcmp(key, "punishMeterFilter") == 0) ep.punishMeterFilter = atoi(val);
                    else if (strcmp(key, "punishMeterRecipient") == 0) ep.punishMeterRecipient = atoi(val);
                    else if (strcmp(key, "tradeCameraMode") == 0) ep.tradeCameraMode = atoi(val);
                    else if (strcmp(key, "tradeZoomAmount") == 0) ep.tradeZoomAmount = (float)atof(val);
                    else if (strcmp(key, "tradeZoomHold") == 0) ep.tradeZoomHold = atoi(val);
                    else if (strcmp(key, "tradeEnableMeter") == 0) ep.tradeEnableMeter = (atoi(val) != 0);
                    else if (strcmp(key, "tradeMeterReward") == 0) ep.tradeMeterReward = (float)atof(val);
                    else if (strcmp(key, "tradeMeterFilter") == 0) ep.tradeMeterFilter = atoi(val);
                    else if (strcmp(key, "dynSlowMo") == 0) ep.dynSlowMo = (atoi(val) != 0);
                    else if (strcmp(key, "dynSlowMoRate") == 0) ep.dynSlowMoRate = (float)atof(val);
                    else if (strcmp(key, "dynCamSpeedIn") == 0) ep.dynCamSpeedIn = (float)atof(val);
                    else if (strcmp(key, "dynCamSpeedOut") == 0) ep.dynCamSpeedOut = (float)atof(val);
                    else if (strcmp(key, "customMeterCosts") == 0) ep.customMeterCosts = (atoi(val) != 0);
                    else if (strcmp(key, "exCost") == 0) ep.exCost = (float)atof(val);
                    else if (strcmp(key, "breakerCost") == 0) ep.breakerCost = (float)atof(val);
                    else if (strcmp(key, "xrayCost") == 0) ep.xrayCost = (float)atof(val);
                    else if (strcmp(key, "showMeterHUD") == 0) ep.showMeterHUD = (atoi(val) != 0);
                    else if (strcmp(key, "enableSpecialCost") == 0) ep.enableSpecialCost = (atoi(val) != 0);
                    else if (strcmp(key, "specialCost") == 0) ep.specialCost = (float)atof(val);
                    else if (strcmp(key, "enableAttGain") == 0) ep.enableAttGain = (atoi(val) != 0);
                    else if (strcmp(key, "attGain") == 0) ep.attGain = (float)atof(val);
                    else if (strcmp(key, "attFilter") == 0) ep.attFilter = atoi(val);
                    else if (strcmp(key, "enableDefBlockGain") == 0) ep.enableDefBlockGain = (atoi(val) != 0);
                    else if (strcmp(key, "defBlockGain") == 0) ep.defBlockGain = (float)atof(val);
                    else if (strcmp(key, "defBlockFilter") == 0) ep.defBlockFilter = atoi(val);
                    else if (strcmp(key, "enableDefDmgGain") == 0) ep.enableDefDmgGain = (atoi(val) != 0);
                    else if (strcmp(key, "defDmgGain") == 0) ep.defDmgGain = (float)atof(val);
                    else if (strcmp(key, "defDmgFilter") == 0) ep.defDmgFilter = atoi(val);
                    else if (strcmp(key, "showHitOutcomeBadges") == 0) ep.showHitOutcomeBadges = (atoi(val) != 0);
                    else if (strcmp(key, "disableBlood") == 0) ep.disableBlood = (atoi(val) != 0);
                    else if (strcmp(key, "disableBackground") == 0) ep.disableBackground = (atoi(val) != 0);
                    else if (strcmp(key, "voidBgColor") == 0) {
                        float r = 0.0f, g = 0.0f, b = 0.0f;
                        if (sscanf_s(val, "%f,%f,%f", &r, &g, &b) == 3) {
                            ep.voidBgColor[0] = r; ep.voidBgColor[1] = g; ep.voidBgColor[2] = b;
                        }
                    }
                    else if (strcmp(key, "enableFloorGrid") == 0) ep.enableFloorGrid = (atoi(val) != 0);
                    else if (strcmp(key, "floorGridColor") == 0) {
                        float r = 0.0f, g = 0.85f, b = 1.0f;
                        if (sscanf_s(val, "%f,%f,%f", &r, &g, &b) == 3) {
                            ep.floorGridColor[0] = r; ep.floorGridColor[1] = g; ep.floorGridColor[2] = b;
                        }
                    }
                    else if (strcmp(key, "floorGridCenterColor") == 0) {
                        float r = 1.0f, g = 1.0f, b = 1.0f;
                        if (sscanf_s(val, "%f,%f,%f", &r, &g, &b) == 3) {
                            ep.floorGridCenterColor[0] = r; ep.floorGridCenterColor[1] = g; ep.floorGridCenterColor[2] = b;
                        }
                    }
                    else if (strcmp(key, "showGridCornerWalls") == 0) ep.showGridCornerWalls = (atoi(val) != 0);
                    else if (strcmp(key, "gridWallColor") == 0) {
                        float r = 1.0f, g = 0.25f, b = 0.25f;
                        if (sscanf_s(val, "%f,%f,%f", &r, &g, &b) == 3) {
                            ep.gridWallColor[0] = r; ep.gridWallColor[1] = g; ep.gridWallColor[2] = b;
                        }
                    }
                    else if (strcmp(key, "disableQuickWakeup") == 0) ep.disableQuickWakeup = (atoi(val) != 0);
                    else if (strcmp(key, "disableRollWakeup") == 0) ep.disableRollWakeup = (atoi(val) != 0);
                    else if (strcmp(key, "disableDelayedWakeup") == 0) ep.disableDelayedWakeup = (atoi(val) != 0);
                    else if (strcmp(key, "delayedWakeupFixedHold") == 0) ep.delayedWakeupFixedHold = (atoi(val) != 0);
                    else if (strcmp(key, "delayedWakeupFixedFrames") == 0) ep.delayedWakeupFixedFrames = atoi(val);
                    else if (strcmp(key, "disableDelayedWakeupRoll") == 0) ep.disableDelayedWakeupRoll = (atoi(val) != 0);
                    else if (strcmp(key, "wakeupScope") == 0) ep.wakeupScope = atoi(val);
                    else if (strcmp(key, "uppercutAA") == 0) ep.uppercutAA = (atoi(val) != 0);
                    else if (strcmp(key, "uppercutVsSpecial") == 0) ep.uppercutVsSpecial = (atoi(val) != 0);
                    else if (strcmp(key, "uppercutVsUppercut") == 0) ep.uppercutVsUppercut = (atoi(val) != 0);
                    else if (strcmp(key, "jumpInPriority") == 0) ep.jumpInPriority = (atoi(val) != 0);
                    else if (strcmp(key, "xrayPriority") == 0) ep.xrayPriority = (atoi(val) != 0);
                    else if (strcmp(key, "instantProjectiles") == 0) ep.instantProjectiles = (atoi(val) != 0);
                }
            }
        }

        fclose(f);
        SetPresetStatus("Imported presets from %s", filepath ? filepath : "mk9_extras_presets.ini");
        LogTrace("[PRESETS] Imported presets from %s", filepath ? filepath : "mk9_extras_presets.ini");
        return true;
    }

    PlayerStunState g_PlayerStun[3];

    struct ComboTracker {
        bool     inCombo = false;
        uint32_t hitCount = 0;
        float    totalDamage = 0.0f;
        uint64_t lastHitFrame = 0;
        int      targetPlayer = 0;
    };
    static ComboTracker s_ComboTracker[3];

    void ApplyAnimRateScale(int defPlr, void* pChar, float rate) {
        if (!pChar || (uintptr_t)pChar < 0x01000000) return;
        __try {
            void* pSkelComp = *(void**)((uintptr_t)pChar + 0x010C);
            if (pSkelComp && (uintptr_t)pSkelComp >= 0x00400000 && (uintptr_t)pSkelComp < 0x7FFE0000) {
                *(float*)((uintptr_t)pSkelComp + 0x2F0) = rate;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    void ResetAnimRateScale(int defPlr, void* pChar) {
        ApplyAnimRateScale(defPlr, pChar, 1.0f);
    }

    struct BlockPushbackTracker {
        bool active = false;
        int defPlr = 0;
        int attPlr = 0;
        void* pDefender = nullptr;
        void* pAttacker = nullptr;
        float startDefX = 0.0f;
        float lastWrittenX = 0.0f;
        float accumulatedPushback = 0.0f;
        uint64_t startFrame = 0;
        uint32_t activeFrames = 0;
    };
    static BlockPushbackTracker s_BlockPushback[3] = {};

    void Init() {
        Reset();
    }

    void Reset() {
        for (int i = 1; i <= 2; i++) {
            if (g_PlayerStun[i].pLastAnimSeq && (uintptr_t)g_PlayerStun[i].pLastAnimSeq > 0x01000000) {
                __try { *(float*)((uintptr_t)g_PlayerStun[i].pLastAnimSeq + 0x48) = 1.0f; } __except (EXCEPTION_EXECUTE_HANDLER) {}
                g_PlayerStun[i].pLastAnimSeq = nullptr;
            }
        }
        memset(g_PlayerStun, 0, sizeof(g_PlayerStun));
        memset(s_ComboTracker, 0, sizeof(s_ComboTracker));
        memset(s_BlockPushback, 0, sizeof(s_BlockPushback));
        for (int i = 1; i <= 2; i++) {
            g_PlayerStun[i].animRateScale = 1.0f;
        }
        ResetAnimRateScale(1, GetP1Char());
        ResetAnimRateScale(2, GetP2Char());
        FreeCamera::ReleaseCinematicZoom();
    }

    static int ResolveMoveStartupFrames(int attPlr, const MoveLifecycleTracker& attLifecycle, void* pAttacker) {
        if (attLifecycle.inMove && attLifecycle.startFrame > 0 && g_CurrentFrame >= attLifecycle.startFrame) {
            if (attLifecycle.firstActiveFrame > 0 && attLifecycle.firstActiveFrame >= attLifecycle.startFrame) {
                return (int)(attLifecycle.firstActiveFrame - attLifecycle.startFrame + 1);
            }
            return (int)(g_CurrentFrame - attLifecycle.startFrame + 1);
        }
        const FighterFrameTimeline& tl = (attPlr == 1) ? s_p1Timeline : s_p2Timeline;
        if (tl.startupFrames > 0) {
            return tl.startupFrames;
        }
        uint32_t token = attLifecycle.matchedToken;
        if (token == 0) {
            token = (attPlr == 1) ? g_P1_LastAttackToken : g_P2_LastAttackToken;
        }
        switch (token) {
            case 0x8000000F: return 9;  // FP (1) -> 9f
            case 0x80000010: return 11; // BP (2) -> 11f
            case 0x80000011: return 11; // FK (3) -> 11f
            case 0x80000012: return 21; // BK (4) -> 21f
            case 0x80000023: return 6;  // D+1 -> 6f
            case 0x80000029: return 12; // D+4 -> 12f
            case 0x8000003A: return 10; // D+2 -> 10f
            default: break;
        }
        return 6;
    }

    void OnHitEvaluation(int attPlr, int defPlr, void* pDefender, void* pAttacker, bool isBlocked, int rxID, float damage, const AttackClassification* pAtk) {
        if (defPlr < 1 || defPlr > 2) return;

        // Check Target Scope
        if (g_TargetScope == 1 && defPlr != 1) return;
        if (g_TargetScope == 2 && defPlr != 2) return;

        uint32_t defAction = 0;
        uint32_t defStance = 0;
        uint32_t defCombat = 0;
        float defHeight = 0.0f;
        int defScriptFunc = -1;
        uint32_t defHitboxes = 0;

        if (pDefender && (uintptr_t)pDefender > 0x01000000) {
            __try {
                defAction = *(uint32_t*)((uintptr_t)pDefender + 0x1D4);
                defStance = *(uint32_t*)((uintptr_t)pDefender + 0x2E8);
                defCombat = *(uint32_t*)((uintptr_t)pDefender + 0x2EC);
                defHeight = *(float*)((uintptr_t)pDefender + 0x1F0);
                void* pScript = *(void**)((uintptr_t)pDefender + 0x25C);
                if (pScript && (uintptr_t)pScript > 0x01000000) {
                    defScriptFunc = *(int*)((uintptr_t)pScript + 0x18);
                }
                void* pColInfo = *(void**)((uintptr_t)pDefender + 0x2F10);
                if (pColInfo && (uintptr_t)pColInfo > 0x01000000) {
                    defHitboxes = *(uint32_t*)((uintptr_t)pColInfo + 0x10);
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
        }

        // 1. Authoritative Combo Chain Tracking Across Extended Stun Frames:
        // A hit ONLY counts as a true combo follow-up if:
        // - The defender was actively in true unblockable hitstun where they CANNOT act, block, or cancel to guard.
        // - False recovery frames, soft stumble (0x4600), actionable recovery (0x40000000), and blockstun are EXCLUDED.
        bool wasActionable = ((defCombat & 0x40000000) != 0) || 
                             (defAction == 0x0000 || defAction == 0x000F || 
                              defAction == 0x0100 || defAction == 0x0101 || defAction == 0x0102 || 
                              (defAction >= 0x0900 && defAction <= 0x0A0F));
        bool isSoftStumble = (defAction == 0x4600 || (defAction >= 0x4601 && defAction <= 0x4605));
        bool wasInNativeHitstun = (defAction >= 0x4000 && defAction <= 0x4FFF && 
                                  defAction != 0x4205 && defAction != 0x4206 && 
                                  defAction != 0x4F00 && !isSoftStumble && !wasActionable);
        bool inActiveExtraHitstun = (g_PlayerStun[defPlr].active && !g_PlayerStun[defPlr].isBlocked && 
                                     g_PlayerStun[defPlr].totalExtraFrames > 0 && 
                                     (g_PlayerStun[defPlr].releaseFrame == 0 || g_CurrentFrame < g_PlayerStun[defPlr].releaseFrame) &&
                                     !wasActionable);
        bool isComboFollowUp = inActiveExtraHitstun || (wasInNativeHitstun && !wasActionable && !isSoftStumble);

        if (isBlocked) {
            s_ComboTracker[attPlr].inCombo = false;
            s_ComboTracker[attPlr].hitCount = 0;
            s_ComboTracker[attPlr].totalDamage = 0.0f;
        } else {
            if (isComboFollowUp && s_ComboTracker[attPlr].inCombo && s_ComboTracker[attPlr].targetPlayer == defPlr) {
                s_ComboTracker[attPlr].hitCount++;
                s_ComboTracker[attPlr].totalDamage += damage;
                s_ComboTracker[attPlr].lastHitFrame = g_CurrentFrame;
                LogTrace("[COMBO TRACKER | F%llu] >>> TRUE COMBO EXTENDED! P%d -> P%d Hit #%u (Total Dmg: %.1f%%) <<<",
                         g_CurrentFrame, attPlr, defPlr, s_ComboTracker[attPlr].hitCount, s_ComboTracker[attPlr].totalDamage * 100.0f);
            } else {
                s_ComboTracker[attPlr].inCombo = true;
                s_ComboTracker[attPlr].hitCount = 1;
                s_ComboTracker[attPlr].totalDamage = damage;
                s_ComboTracker[attPlr].lastHitFrame = g_CurrentFrame;
                s_ComboTracker[attPlr].targetPlayer = defPlr;
                LogTrace("[COMBO TRACKER | F%llu] >>> %s P%d -> P%d Hit #1 (Dmg: %.1f%% | DefAct: 0x%04X%s) <<<",
                         g_CurrentFrame, isComboFollowUp ? "NEW COMBO INITIATED!" : "COMBO RESET / LINK HIT!", 
                         attPlr, defPlr, damage * 100.0f, defAction, isSoftStumble ? " [Soft Stagger - Blockable]" : (wasActionable ? " [Actionable/Recovered]" : ""));
            }
        }

        // 2. Classify Hit Outcome (Counter Hit vs Fatal Punish vs Clean Hit):
        int totalDelta = 0;
        bool counterhit = false;
        bool punish = false;

        // Check whether the hit leaves the defender standing on their feet vs airborne/knocked down
        bool isFloorOrWakeup = (defAction >= 0x6000 && defAction <= 0x62FF) || // Ground face up/down, roll, wakeup getup
                               (defAction == 0x4201) ||                        // Hard knockdown
                               (defAction >= 0x460A && defAction <= 0x460F) || // Crumple / knockdown
                               (defAction >= 0x4200 && defAction <= 0x45FF && defAction != 0x4205 && defAction != 0x4206);   // Air hurt / tumble / launch (0x4205/0x4206 are standing stun)

        bool isAirborne = (defHeight > 5.0f) || ((defStance & 0x01) != 0) || (defAction >= 0x0300 && defAction <= 0x030F);

        bool isKnockdownAttack = (
            rxID == 292 || // Sweep (B+4)
            rxID == 176 || rxID == 177 || rxID == 178 || // Uppercut (D+2)
            (pAtk && (pAtk->isUppercut || pAtk->moveClass == MOVE_CLASS_XRAY || pAtk->atkType == 10 || pAtk->atkType == 9))
        );

        bool leavesOpponentStanding = !isFloorOrWakeup && !isAirborne && !isKnockdownAttack;

        bool isCrouchBlock = false;
        if (isBlocked) {
            // Authentic Crouch Block check in NetherRealm engine:
            // rxID == 1 is Crouch Block reaction, rxID == 0 is Stand Block reaction.
            // Also verify action state: 0x4F00 is duck blockstun, 0x0901/0x0102 is duck block idle.
            isCrouchBlock = (rxID == 1 || defAction == 0x4F00 || defAction == 0x0901 || defAction == 0x0102 ||
                             defAction == 0x0100 || defAction == 0x0101 || (defAction >= 0x4700 && defAction <= 0x470F));
        }

        if (isBlocked) {
            totalDelta = g_GeneralBlockstunDelta;
        } else {
            // General Hitstun Adjustment only applies if the hit leaves the opponent standing:
            totalDelta = leavesOpponentStanding ? g_GeneralHitstunDelta : 0;

            if (isComboFollowUp) {
                // Combo follow-up hit! Ensure character is recognized by native engine as actively in hitstun
                // so the game's internal combo counter (0x008991E0) increments seamlessly (2 HITS, 3 HITS...)
                if (pDefender && (uintptr_t)pDefender > 0x01000000) {
                    __try {
                        // Mark Character + 0x2FCC = 1 so Combat_UpdateHUDComboCounter (0x008991E0)
                        // recognizes this as an active combo hit and does NOT call 0x00898F60 to reset the count!
                        *(uint32_t*)((uintptr_t)pDefender + 0x2FCC) = 1;

                        void* pDefInfo = *(void**)((uintptr_t)pDefender + 0x7638);
                        if (!pDefInfo || (uintptr_t)pDefInfo < 0x00400000) {
                            pDefInfo = (defPlr == 1) ? (void*)0x00E89D7C : (void*)0x00E8AA08;
                        }
                        if (pDefInfo && (uintptr_t)pDefInfo > 0x00400000) {
                            uint32_t curHits = *(uint32_t*)((uintptr_t)pDefInfo + 0xC54);
                            if (curHits < 1) {
                                *(uint32_t*)((uintptr_t)pDefInfo + 0xC54) = 1;
                            }
                        }
                    } __except (EXCEPTION_EXECUTE_HANDLER) {}
                }

                // Combo follow-up hit! Release the close-up zoom and immediately clear active stun hold
                // so the follow-up hit (e.g. launcher, juggle, sweep) takes full native control!
                FreeCamera::ReleaseCinematicZoom();
                ResetAnimRateScale(defPlr, pDefender);
                g_PlayerStun[defPlr].active = false;
                g_PlayerStun[defPlr].isHoldingExtraStun = false;
                g_PlayerStun[defPlr].totalExtraFrames = 0;
                g_PlayerStun[defPlr].animRateScale = 1.0f;
            } else if (g_FixEnabled) {
                const MoveLifecycleTracker& defLifecycle = (defPlr == 2) ? g_P2_Lifecycle : g_P1_Lifecycle;

                uint64_t defElapsed = 0;
                const char* hitDetail = nullptr;
                const char* hitClass = ClassifyHitOutcome(attPlr, defPlr, false, &defElapsed, &hitDetail);

                bool isDefInNeutralOrGuard = (defAction == 0x0000) || 
                                             (defAction == 0x0100 || defAction == 0x0101) || // Crouch
                                             (defAction >= 0x0200 && defAction <= 0x020F) || // Walk
                                             (defAction >= 0x0900 && defAction <= 0x0A01);   // Block

                uint64_t lastFinished = defLifecycle.lastFinishedEndFrame;
                uint64_t lastAttackEnd = (defPlr == 2) ? g_P2_LastAttackEndFrame : g_P1_LastAttackEndFrame;
                bool isRecentWhiffRecovery = !isDefInNeutralOrGuard &&
                                             ((lastFinished > 0 && g_CurrentFrame >= lastFinished && (g_CurrentFrame - lastFinished <= 16)) ||
                                              (lastAttackEnd > 0 && g_CurrentFrame >= lastAttackEnd && (g_CurrentFrame - lastAttackEnd <= 16)));

                if (hitClass && (strstr(hitClass, "STARTUP") || strstr(hitClass, "Startup") || strstr(hitClass, "ACTIVE") || strstr(hitClass, "Active"))) {
                    counterhit = true;
                } else if (!isDefInNeutralOrGuard && hitClass && (strstr(hitClass, "PUNISH") || strstr(hitClass, "Punish") || strstr(hitClass, "RECOVERY") || strstr(hitClass, "Recovery"))) {
                    punish = true;
                } else if (defLifecycle.inMove) {
                    if (defLifecycle.recoveryStartFrame > 0 && g_CurrentFrame >= defLifecycle.recoveryStartFrame) {
                        punish = true;
                    } else {
                        counterhit = true;
                    }
                } else if (defAction >= 0x5000 && defAction <= 0x5FFF) {
                    if (defHitboxes > 0 || (defCombat & 0x600) != 0) {
                        counterhit = true;
                    } else {
                        punish = true;
                    }
                } else if (defScriptFunc != -1 || (defCombat & 0x600) != 0) {
                    counterhit = true;
                } else if (isRecentWhiffRecovery) {
                    punish = true;
                }

                // Compute Attacker Move Startup Frames for threshold checks
                const MoveLifecycleTracker& attLifecycle = (attPlr == 1) ? g_P1_Lifecycle : g_P2_Lifecycle;
                int attStartup = ResolveMoveStartupFrames(attPlr, attLifecycle, pAttacker);

                // Classify Attacker Move: Normal vs Special
                bool isSpecial = (pAtk && (pAtk->moveClass == MOVE_CLASS_SPECIAL || pAtk->moveClass == MOVE_CLASS_EX_SPECIAL || pAtk->moveClass == MOVE_CLASS_XRAY));
                if (!isSpecial && pAttacker && (uintptr_t)pAttacker > 0x01000000) {
                    __try {
                        uint32_t combatFlags = *(uint32_t*)((uintptr_t)pAttacker + 0x2EC);
                        if ((combatFlags & 0x200) != 0) isSpecial = true;
                    } __except (EXCEPTION_EXECUTE_HANDLER) {}
                }
                bool isNormal = !isSpecial;

                if (punish) {
                    bool meetsType = true;
                    if (g_PunishMoveTypeFilter == 1 && !isNormal) meetsType = false;  // Normals Only
                    if (g_PunishMoveTypeFilter == 2 && !isSpecial) meetsType = false; // Specials Only

                    bool meetsThreshold = (g_PunishMinStartup <= 0 || attStartup >= g_PunishMinStartup);
                    if (meetsThreshold && meetsType) {
                        HitIndicationHUD::TriggerHitIndication(attPlr, HitIndicationHUD::HitIndicationType::FATAL_PUNISH);
                        if (g_PunishExtraHitstun != 0 && leavesOpponentStanding) {
                            totalDelta += g_PunishExtraHitstun;
                            LogTrace("[STUN MODIFIER | F%llu] >>> PUNISH (RECOVERY INTERRUPT) on P%d! (Atk Startup: %df >= Min: %df | Move: %s) Adding +%d frames hitstun (Total Delta: %+d) <<<",
                                     g_CurrentFrame, defPlr, attStartup, g_PunishMinStartup, isSpecial ? "Special" : "Normal", g_PunishExtraHitstun, totalDelta);
                        }
                        if (g_PunishCameraMode != 0) {
                            FreeCamera::TriggerCinematicZoom(g_PunishZoomAmount, 0, defPlr, attPlr, g_PunishCameraMode, g_PunishCamShiftY, g_PunishCamYawOffset);
                        }
                        if (g_EnablePunishMeterReward && g_PunishMeterReward > 0.0001f &&
                            MeterDrainFix::CheckMoveFilterMatch(g_PunishMeterMoveFilter, isSpecial)) {
                            int targetPlr = (g_PunishMeterRecipient == 0) ? defPlr : 
                                            ((g_PunishMeterRecipient == 1) ? attPlr : 0);
                            if (targetPlr != 0) {
                                MeterDrainFix::AwardPlayerMeter(targetPlr, g_PunishMeterReward);
                            } else {
                                MeterDrainFix::AwardPlayerMeter(1, g_PunishMeterReward);
                                MeterDrainFix::AwardPlayerMeter(2, g_PunishMeterReward);
                            }
                            LogTrace("[STUN MODIFIER | F%llu] Fatal Punish Meter Reward (+%.3f) awarded to %s (P%d)",
                                     g_CurrentFrame, g_PunishMeterReward,
                                     (g_PunishMeterRecipient == 0) ? "Hit Receiver (Defender)" : ((g_PunishMeterRecipient == 1) ? "Attacker" : "Both"),
                                     (g_PunishMeterRecipient == 0) ? defPlr : ((g_PunishMeterRecipient == 1) ? attPlr : 0));
                        }
                    } else {
                        // User Rule: If a fatal punish attempt does not meet the standard startup threshold,
                        // but meets at least the Counter Hit requirement frame/type, then it is a Counter Hit!
                        bool chMeetsType = true;
                        if (g_CounterhitMoveTypeFilter == 1 && !isNormal) chMeetsType = false;
                        if (g_CounterhitMoveTypeFilter == 2 && !isSpecial) chMeetsType = false;
                        bool chMeetsThreshold = (g_CounterhitMinStartup <= 0 || attStartup >= g_CounterhitMinStartup);

                        if (chMeetsThreshold && chMeetsType) {
                            punish = false;
                            counterhit = true;
                            LogTrace("[STUN MODIFIER | F%llu] Punish attempt startup (%df) < Punish Min (%df), but meets Counter Hit Min (%df). Converting to COUNTER HIT!",
                                     g_CurrentFrame, attStartup, g_PunishMinStartup, g_CounterhitMinStartup);
                        } else {
                            punish = false;
                            LogTrace("[STUN MODIFIER | F%llu] Punish detected on P%d, but filtered out (Startup: %df, Move: %s). Added frames skipped.",
                                     g_CurrentFrame, defPlr, attStartup, isSpecial ? "Special" : "Normal");
                        }
                    }
                }

                if (counterhit) {
                    bool meetsType = true;
                    if (g_CounterhitMoveTypeFilter == 1 && !isNormal) meetsType = false;  // Normals Only
                    if (g_CounterhitMoveTypeFilter == 2 && !isSpecial) meetsType = false; // Specials Only

                    bool meetsThreshold = (g_CounterhitMinStartup <= 0 || attStartup >= g_CounterhitMinStartup);
                    if (meetsThreshold && meetsType) {
                        HitIndicationHUD::TriggerHitIndication(attPlr, HitIndicationHUD::HitIndicationType::COUNTER_HIT);
                        if (g_CounterhitExtraHitstun != 0 && leavesOpponentStanding) {
                            totalDelta += g_CounterhitExtraHitstun;
                            LogTrace("[STUN MODIFIER | F%llu] >>> COUNTERHIT (STARTUP INTERRUPT) on P%d! (Atk Startup: %df >= Min: %df | Move: %s) Adding +%d frames hitstun (Total Delta: %+d) <<<",
                                     g_CurrentFrame, defPlr, attStartup, g_CounterhitMinStartup, isSpecial ? "Special" : "Normal", g_CounterhitExtraHitstun, totalDelta);
                        }
                        if (g_CounterhitCameraMode != 0) {
                            FreeCamera::TriggerCinematicZoom(g_CounterhitZoomAmount, 0, defPlr, attPlr, g_CounterhitCameraMode, g_CounterhitCamShiftY, g_CounterhitCamYawOffset);
                        }
                        if (g_EnableCounterhitMeterReward && g_CounterhitMeterReward > 0.0001f && 
                            MeterDrainFix::CheckMoveFilterMatch(g_CounterhitMeterMoveFilter, isSpecial)) {
                            int targetPlr = (g_CounterhitMeterRecipient == 0) ? defPlr : 
                                            ((g_CounterhitMeterRecipient == 1) ? attPlr : 0);
                            if (targetPlr != 0) {
                                MeterDrainFix::AwardPlayerMeter(targetPlr, g_CounterhitMeterReward);
                            } else {
                                MeterDrainFix::AwardPlayerMeter(1, g_CounterhitMeterReward);
                                MeterDrainFix::AwardPlayerMeter(2, g_CounterhitMeterReward);
                            }
                            LogTrace("[STUN MODIFIER | F%llu] Counter Hit Meter Reward (+%.3f) awarded to %s (P%d)",
                                     g_CurrentFrame, g_CounterhitMeterReward,
                                     (g_CounterhitMeterRecipient == 0) ? "Hit Receiver (Defender)" : ((g_CounterhitMeterRecipient == 1) ? "Attacker" : "Both"),
                                     (g_CounterhitMeterRecipient == 0) ? defPlr : ((g_CounterhitMeterRecipient == 1) ? attPlr : 0));
                        }
                    } else {
                        counterhit = false;
                        LogTrace("[STUN MODIFIER | F%llu] Counterhit detected on P%d, but filtered out (Startup: %df, Move: %s). Added frames skipped.",
                                 g_CurrentFrame, defPlr, attStartup, isSpecial ? "Special" : "Normal");
                    }
                } else if (totalDelta != 0) {
                    LogTrace("[STUN MODIFIER | F%llu] General Hitstun adjustment on P%d: %+d frames",
                             g_CurrentFrame, defPlr, totalDelta);
                }
            } else if (totalDelta != 0) {
                LogTrace("[STUN MODIFIER | F%llu] General Hitstun adjustment on P%d: %+d frames",
                         g_CurrentFrame, defPlr, totalDelta);
            }
        }

        if (isBlocked && totalDelta != 0) {
            LogTrace("[STUN MODIFIER | F%llu] General Blockstun adjustment on P%d (%s): %+d frames",
                     g_CurrentFrame, defPlr, isCrouchBlock ? "Crouch Guard" : "Stand Guard", totalDelta);
        }

        // 3. Strict Standing Hit Rule for Extra Hitstun:
        // Hitstun modifications (General Hitstun, Counter Hit, Fatal Punish) ONLY apply if hit leaves defender standing!
        // Knockdowns, sweeps, air hits/juggles, throws, and wakeups MUST NEVER receive added frames!
        if (isFloorOrWakeup || isAirborne || isKnockdownAttack || (!isBlocked && !leavesOpponentStanding)) {
            // Strictly reject extra frames for non-standing states
            g_PlayerStun[defPlr].active = false;
            g_PlayerStun[defPlr].isHoldingExtraStun = false;
            g_PlayerStun[defPlr].totalExtraFrames = 0;
            g_PlayerStun[defPlr].animRateScale = 1.0f;
            ResetAnimRateScale(defPlr, pDefender);
            return;
        }

        if (totalDelta != 0) {
            g_PlayerStun[defPlr].active = true;
            g_PlayerStun[defPlr].isBlocked = isBlocked;
            g_PlayerStun[defPlr].totalExtraFrames = totalDelta;
            g_PlayerStun[defPlr].hitFrame = g_CurrentFrame;
            if (isBlocked) {
                g_PlayerStun[defPlr].lastStunAction = isCrouchBlock ? 0x4F00 : 0x0A01;
                uint32_t baseStun = (defPlr == 1) ? ResolveAuthoredBlockstun(attPlr, g_P2_Lifecycle, pDefender)
                                                  : ResolveAuthoredBlockstun(attPlr, g_P1_Lifecycle, pDefender);
                g_PlayerStun[defPlr].baseAuthoredStun = baseStun;
                uint32_t targetFrames = (int)baseStun + totalDelta > 1 ? (uint32_t)((int)baseStun + totalDelta) : 1;
                g_PlayerStun[defPlr].releaseFrame = g_CurrentFrame + targetFrames;
                float rate = (float)baseStun / (float)targetFrames;
                if (rate < 0.2f) rate = 0.2f;
                if (rate > 3.0f) rate = 3.0f;
                g_PlayerStun[defPlr].animRateScale = rate;
                ApplyAnimRateScale(defPlr, pDefender, rate);
            } else {
                g_PlayerStun[defPlr].lastStunAction = (defAction == 0x4700 || defAction == 0x0100 || defAction == 0x0101) ? 0x4700 : 0x4600;
                g_PlayerStun[defPlr].releaseFrame = 0;
            }
            int attFunc = 0;
            if (pAttacker && (uintptr_t)pAttacker > 0x01000000) {
                void* pScript = *(void**)((uintptr_t)pAttacker + 0x25C);
                if (pScript && (uintptr_t)pScript > 0x01000000) {
                    attFunc = *(int*)((uintptr_t)pScript + 0x18);
                }
            }
            g_PlayerStun[defPlr].attackerScriptFunc = attFunc;
            g_PlayerStun[defPlr].isHoldingExtraStun = false;
            g_PlayerStun[defPlr].isCounterhit = counterhit;
            g_PlayerStun[defPlr].isPunish = punish;
            if (!isBlocked) {
                g_PlayerStun[defPlr].animRateScale = 1.0f;
            }

            if (!counterhit && !punish) {
                FreeCamera::ReleaseCinematicZoom();
            }
        } else {
            g_PlayerStun[defPlr].active = false;
            g_PlayerStun[defPlr].isHoldingExtraStun = false;
            g_PlayerStun[defPlr].totalExtraFrames = 0;
            uint32_t baseStun = (defPlr == 1) ? ResolveAuthoredBlockstun(attPlr, g_P2_Lifecycle, pDefender)
                                              : ResolveAuthoredBlockstun(attPlr, g_P1_Lifecycle, pDefender);
            g_PlayerStun[defPlr].baseAuthoredStun = baseStun;
            g_PlayerStun[defPlr].animRateScale = 1.0f;
            g_PlayerStun[defPlr].releaseFrame = 0;
            ResetAnimRateScale(defPlr, pDefender);
            FreeCamera::ReleaseCinematicZoom();
        }
    }

    void ResetBlockPushback() {
        memset(s_BlockPushback, 0, sizeof(s_BlockPushback));
    }

    void UpdateBlockPushbackTick(int defPlr, void* pDefender) {
        if (defPlr < 1 || defPlr > 2) return;
        BlockPushbackTracker& bp = s_BlockPushback[defPlr];
        if (!g_EnableCustomBlockPushback) {
            bp.active = false;
            return;
        }
        if (!pDefender || (uintptr_t)pDefender < 0x01000000 || !bp.active) {
            return;
        }

        __try {
            uint32_t act = *(uint32_t*)((uintptr_t)pDefender + 0x1D4);
            bool inBlockReaction = (act == 0x0A01 || act == 0x4F00 || (act >= 0x0A02 && act <= 0x0A0F));

            if (!inBlockReaction || bp.activeFrames > 40) {
                // Blockstun ended: deactivate tracker immediately - no persistent anchor in neutral!
                bp.active = false;
                return;
            }

            bp.activeFrames++;

            if (g_BlockPushbackMultiplier == 1.0f) {
                // Pure vanilla: just keep lastWrittenX tracking native position
                bp.lastWrittenX = *(float*)((uintptr_t)pDefender + 0x1E8);
                return;
            }

            float curX = *(float*)((uintptr_t)pDefender + 0x1E8);

            // Compute the single-frame displacement step applied by the native engine this frame
            float frameStep = curX - bp.lastWrittenX;

            // Safety guard: if sudden large jump occurs (e.g. training mode reset or teleport), deactivate immediately
            if (fabsf(frameStep) > 80.0f) {
                bp.active = false;
                return;
            }

            // Scale this single-frame step by the multiplier
            float scaledStep = frameStep * g_BlockPushbackMultiplier;
            bp.accumulatedPushback += scaledStep;

            float targetX = bp.startDefX + bp.accumulatedPushback;

            // Sanity bounds check & reflection
            float excess = fabsf(targetX) - 3500.0f;
            if (excess > 0.0f) {
                targetX = (targetX > 0.0f) ? 3500.0f : -3500.0f;
                if (g_CornerPushbackReflection && bp.pAttacker && (uintptr_t)bp.pAttacker > 0x01000000) {
                    float sign = (targetX > 0.0f) ? -1.0f : 1.0f;
                    float attX = *(float*)((uintptr_t)bp.pAttacker + 0x1E8);
                    ApplyLateralPosition(bp.pAttacker, attX + sign * excess);
                }
            }

            if (fabsf(targetX - curX) >= 0.001f) {
                ApplyLateralPosition(pDefender, targetX);
            }
            bp.lastWrittenX = targetX;

        } __except (EXCEPTION_EXECUTE_HANDLER) {
            bp.active = false;
        }
    }

    void OnPostDamageEvaluation(int defPlr, void* pDefender, bool isBlocked, int attPlr, void* pAttacker) {
        if (!pDefender || (uintptr_t)pDefender < 0x01000000 || defPlr < 1 || defPlr > 2) return;

        // Custom Block Pushback Modification
        if (isBlocked && g_EnableCustomBlockPushback) {
            __try {
                float* pDefVelX = (float*)((uintptr_t)pDefender + 0x2F74);
                if (pDefVelX && (uintptr_t)pDefVelX >= 0x00400000 && (uintptr_t)pDefVelX < 0x7FFE0000) {
                    *pDefVelX *= g_BlockPushbackMultiplier;
                    
                    // Corner Pushback Reflection
                    if (g_CornerPushbackReflection && pAttacker && (uintptr_t)pAttacker > 0x01000000) {
                        uintptr_t pActor = *(uintptr_t*)((uintptr_t)pDefender + 8);
                        if (pActor >= 0x00400000 && pActor < 0x7FFE0000) {
                            float posX = *(float*)(pActor + 0xE0);
                            if (fabsf(posX) >= 3500.0f) {
                                float* pAttVelX = (float*)((uintptr_t)pAttacker + 0x2F74);
                                if (pAttVelX && (uintptr_t)pAttVelX >= 0x00400000 && (uintptr_t)pAttVelX < 0x7FFE0000) {
                                    *pAttVelX = -(*pDefVelX);
                                }
                            }
                        }
                    }
                }

                // Initialize continuous block pushback tracker
                float curDefX = *(float*)((uintptr_t)pDefender + 0x1E8);
                s_BlockPushback[defPlr].active = true;
                s_BlockPushback[defPlr].defPlr = defPlr;
                s_BlockPushback[defPlr].attPlr = attPlr;
                s_BlockPushback[defPlr].pDefender = pDefender;
                s_BlockPushback[defPlr].pAttacker = pAttacker;
                s_BlockPushback[defPlr].startDefX = curDefX;
                s_BlockPushback[defPlr].lastWrittenX = curDefX;
                s_BlockPushback[defPlr].accumulatedPushback = 0.0f;
                s_BlockPushback[defPlr].startFrame = g_CurrentFrame;
                s_BlockPushback[defPlr].activeFrames = 0;
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
        }

        PlayerStunState& s = g_PlayerStun[defPlr];
        if (!s.active || s.totalExtraFrames == 0) return;

        __try {
            float currentTimer = *(float*)((uintptr_t)pDefender + 0x2F0);
            if (currentTimer > 0.05f && currentTimer < 5.0f) {
                float nativeFrames = isBlocked ? (float)s.baseAuthoredStun : (currentTimer * 60.0f);
                if (nativeFrames < 3.0f) nativeFrames = (currentTimer * 60.0f);
                if (nativeFrames < 3.0f && s.naturalAnimFrames > 3) nativeFrames = (float)s.naturalAnimFrames;
                if (nativeFrames < 3.0f) nativeFrames = 27.0f;

                s.naturalAnimFrames = (uint32_t)(nativeFrames + 0.5f);
                float targetFrames = nativeFrames + (float)s.totalExtraFrames;
                if (targetFrames < 3.0f) targetFrames = 3.0f;

                uint64_t startBase = (s.hitFrame > 0) ? s.hitFrame : g_CurrentFrame;
                s.releaseFrame = startBase + (uint64_t)(targetFrames + 0.5f);

                float mult = nativeFrames / targetFrames;
                if (mult < 0.15f) mult = 0.15f;
                if (mult > 3.0f) mult = 3.0f;
                s.animRateScale = mult;

                float newTimer = currentTimer / mult;
                if (newTimer < 0.016f) newTimer = 0.016f;
                *(float*)((uintptr_t)pDefender + 0x2F0) = newTimer;
                *(float*)((uintptr_t)pDefender + 0x2F4) = newTimer;

                // Mode 0 (Slow Motion / Speedup): Apply animation rate scale immediately to the defender's skeletal mesh
                if (g_StunVisualMode == STUN_VISUAL_SLOW_MO && !isBlocked) {
                    ApplyAnimRateScale(defPlr, pDefender, mult);
                }

                if (s.pLastAnimSeq && (uintptr_t)s.pLastAnimSeq > 0x01000000) {
                    __try { *(float*)((uintptr_t)s.pLastAnimSeq + 0x48) = mult; } __except (EXCEPTION_EXECUTE_HANDLER) {}
                }

                LogTrace("[STUN MODIFIER | F%llu] P%d %s: Synchronized native timer (+0x2F0/+0x2F4): %.3fs (%.1ff) -> %.3fs (%.1ff) (%+d frames, %.2fx speed, Release: #%llu)",
                         g_CurrentFrame, defPlr, isBlocked ? "BLOCKSTUN" : "HITSTUN", currentTimer, nativeFrames, newTimer, targetFrames, s.totalExtraFrames, mult, s.releaseFrame);
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    float CalculateHurtAnimRate(void* pChar, void* pAnimSeq, const char* animName) {
        if (!pChar) return 1.0f;
        int plr = (pChar == GetP2Char()) ? 2 : 1;
        PlayerStunState& s = g_PlayerStun[plr];

        if (!s.active || s.totalExtraFrames == 0) {
            return 1.0f;
        }

        // Return scaled playback rate for blockstun animations if active
        if (s.isBlocked) {
            s.pLastAnimSeq = pAnimSeq;
            return (s.animRateScale > 0.01f) ? s.animRateScale : 1.0f;
        }

        if (g_StunVisualMode != STUN_VISUAL_SLOW_MO) {
            return 1.0f;
        }

        uint32_t actionState = 0;
        __try { actionState = *(uint32_t*)((uintptr_t)pChar + 0x1D4); } __except (EXCEPTION_EXECUTE_HANDLER) {}

        bool isHurtAnim = false;
        if (animName && animName[0] != '\0') {
            if (strstr(animName, "Hit_") || strstr(animName, "hit_") ||
                strstr(animName, "React") || strstr(animName, "react") ||
                strstr(animName, "Flinch") || strstr(animName, "flinch") ||
                strstr(animName, "Hurt") || strstr(animName, "hurt") ||
                strstr(animName, "Block") || strstr(animName, "block") ||
                strstr(animName, "Guard") || strstr(animName, "guard") ||
                strstr(animName, "Stumble") || strstr(animName, "stumble") ||
                strstr(animName, "Impact") || strstr(animName, "impact")) {
                isHurtAnim = true;
            }
        }
        if (!isHurtAnim) {
            if (actionState == 0x4600 || actionState == 0x4700 || actionState == 0x0A01 || actionState == 0x4F00 ||
                (actionState >= 0x0A02 && actionState <= 0x0A0F) ||
                (actionState >= 0x4601 && actionState <= 0x4605)) {
                isHurtAnim = true;
            }
        }

        if (!isHurtAnim) {
            return 1.0f;
        }

        // If animRateScale is already configured from OnPostDamageEvaluation, preserve it
        if (s.animRateScale > 0.01f && fabsf(s.animRateScale - 1.0f) > 0.01f) {
            s.pLastAnimSeq = pAnimSeq;
            return s.animRateScale;
        }

        // Read actual base recovery duration: prioritize live reaction timer (+0x2F0)
        float baseHurtFrames = 0.0f;
        __try {
            float curTimer = *(float*)((uintptr_t)pChar + 0x2F0);
            if (curTimer > 0.05f && curTimer < 5.0f) {
                baseHurtFrames = curTimer * 60.0f;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}

        if (baseHurtFrames < 3.0f && s.naturalAnimFrames > 3) {
            baseHurtFrames = (float)s.naturalAnimFrames;
        }

        if (baseHurtFrames < 3.0f && pAnimSeq && (uintptr_t)pAnimSeq > 0x01000000) {
            __try {
                int numFrames = *(int*)((uintptr_t)pAnimSeq + 0x70); // MK9 UAnimSequence numFrames is at +0x70!
                if (numFrames > 3 && numFrames < 600) {
                    baseHurtFrames = (float)numFrames;
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
        }

        if (baseHurtFrames < 3.0f) {
            baseHurtFrames = s.isBlocked ? (float)s.baseAuthoredStun : 27.0f;
            if (baseHurtFrames < 3.0f) baseHurtFrames = 27.0f;
        }

        s.naturalAnimFrames = (uint32_t)(baseHurtFrames + 0.5f);

        // Calculate recovery frames with added frames
        float calculatedRecovery = baseHurtFrames + (float)s.totalExtraFrames;
        if (calculatedRecovery < 3.0f) calculatedRecovery = 3.0f;

        float rate = baseHurtFrames / calculatedRecovery;
        if (rate < 0.15f) rate = 0.15f;
        if (rate > 2.5f) rate = 2.5f;

        s.animRateScale = rate;
        uint64_t startBase = (s.hitFrame > 0) ? s.hitFrame : g_CurrentFrame;
        s.releaseFrame = startBase + (uint64_t)(calculatedRecovery + 0.5f);
        s.pLastAnimSeq = pAnimSeq;

        // Apply rate scale to skeletal mesh component as well
        ApplyAnimRateScale(plr, pChar, rate);

        LogTrace("[STUN MODIFIER | F%llu] P%d Hurt Anim '%s': Base Recovery: %.1ff, Delta: %+df -> Target Recovery: %.1ff frames (Rate: %.3fx, Release: #%llu)",
                 g_CurrentFrame, plr, (animName && animName[0]) ? animName : "Unknown", baseHurtFrames, s.totalExtraFrames, calculatedRecovery, rate, s.releaseFrame);

        return rate;
    }

    float GetAnimRateMultiplier(void* pChar, const char* animName, uint32_t actionState) {
        if (!pChar) return 1.0f;
        int plr = (pChar == GetP2Char()) ? 2 : 1;
        if (!g_PlayerStun[plr].active || g_PlayerStun[plr].totalExtraFrames == 0) {
            return 1.0f;
        }
        if (g_PlayerStun[plr].animRateScale > 0.01f && g_PlayerStun[plr].animRateScale < 10.0f) {
            return g_PlayerStun[plr].animRateScale;
        }
        return CalculateHurtAnimRate(pChar, nullptr, animName);
    }

    typedef void (__fastcall *tSetCharacterAction)(void* pChar, void* edxUnused, uint32_t action);
    static tSetCharacterAction s_fnSetCharacterAction = (tSetCharacterAction)0x00446B60;

    void EnforceStunHold(int defPlr, void* pChar) {
        if (!pChar || (uintptr_t)pChar < 0x01000000 || defPlr < 1 || defPlr > 2) return;
        PlayerStunState& s = g_PlayerStun[defPlr];

        if (s.isBlocked) return;

        __try {
            float charHeight = *(float*)((uintptr_t)pChar + 0x1F0);
            uint32_t charStance = *(uint32_t*)((uintptr_t)pChar + 0x2E8);
            uint32_t currAct = *(uint32_t*)((uintptr_t)pChar + 0x1D4);

            bool isAirOrJuggle = (charHeight > 5.0f) || ((charStance & 0x01) != 0) ||
                                 (currAct >= 0x4200 && currAct <= 0x45FF && currAct != 0x4205 && currAct != 0x4206) ||
                                 (currAct >= 0x6000 && currAct <= 0x62FF);

            if (isAirOrJuggle) {
                // Defender is launched into the air, juggled, or knocked down!
                // Abort stun hold immediately so air juggle executes cleanly with native physics!
                FreeCamera::ReleaseCinematicZoom();
                s.active = false;
                s.isHoldingExtraStun = false;
                s.animRateScale = 1.0f;
                if (s.pLastAnimSeq && (uintptr_t)s.pLastAnimSeq > 0x01000000) {
                    *(float*)((uintptr_t)s.pLastAnimSeq + 0x48) = 1.0f;
                    s.pLastAnimSeq = nullptr;
                }
                ResetAnimRateScale(defPlr, pChar);
                return;
            }

            // 1. Maintain character action state in stun recoil
            if (g_StunVisualMode == STUN_VISUAL_GENERIC_ANIM) {
                uint32_t stunAct = (uint32_t)g_GenericStunActionId;
                *(uint32_t*)((uintptr_t)pChar + 0x1D4) = stunAct;
            } else if (currAct == 0x0000 && !isAirOrJuggle) {
                // Prevent early return to neutral before added slow-motion hitstun completes
                uint32_t restoreAct = (s.lastStunAction != 0) ? s.lastStunAction : 0x4600;
                *(uint32_t*)((uintptr_t)pChar + 0x1D4) = restoreAct;
            }

            // Maintain continuous hit reaction timer so native combo counter links follow-up hits seamlessly
            float remainingSec = (s.releaseFrame > g_CurrentFrame) ? (float)(s.releaseFrame - g_CurrentFrame) / 60.0f : 0.05f;
            if (remainingSec > 0.0f) {
                *(float*)((uintptr_t)pChar + 0x2F0) = remainingSec;
                *(float*)((uintptr_t)pChar + 0x2F4) = remainingSec;
            }

            // 2. In slow-motion mode, ensure anim rate scale is maintained on mesh component
            if (g_StunVisualMode == STUN_VISUAL_SLOW_MO && s.animRateScale > 0.0f) {
                ApplyAnimRateScale(defPlr, pChar, s.animRateScale);
            }

            // 3. Lockout native player input gate ONLY during grounded hitstun (NOT during blockstun or in air)
            if (!isAirOrJuggle) {
                void* pInfo = *(void**)((uintptr_t)pChar + 0x7638);
                if (pInfo && (uintptr_t)pInfo > 0x01000000) {
                    *(uint32_t*)((uintptr_t)pInfo + 0x944) = 1; // 1 = LOCKED OUT
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    void ReleaseStunHold(int defPlr, void* pChar) {
        FreeCamera::ReleaseCinematicZoom();

        if (defPlr >= 1 && defPlr <= 2) {
            PlayerStunState& s = g_PlayerStun[defPlr];
            s.active = false;
            s.isHoldingExtraStun = false;
            s.animRateScale = 1.0f;
            if (s.pLastAnimSeq && (uintptr_t)s.pLastAnimSeq > 0x01000000) {
                __try {
                    *(float*)((uintptr_t)s.pLastAnimSeq + 0x48) = 1.0f;
                } __except (EXCEPTION_EXECUTE_HANDLER) {}
                s.pLastAnimSeq = nullptr;
            }
        }

        ResetAnimRateScale(defPlr, pChar);

        if (!pChar || (uintptr_t)pChar < 0x01000000) return;

        __try {
            float charHeight = *(float*)((uintptr_t)pChar + 0x1F0);
            uint32_t charStance = *(uint32_t*)((uintptr_t)pChar + 0x2E8);
            uint32_t currAct = *(uint32_t*)((uintptr_t)pChar + 0x1D4);

            bool isAirOrJuggle = (charHeight > 5.0f) || ((charStance & 0x01) != 0) ||
                                 (currAct >= 0x4200 && currAct <= 0x45FF && currAct != 0x4205 && currAct != 0x4206) ||
                                 (currAct >= 0x6000 && currAct <= 0x62FF);

            if (!isAirOrJuggle && g_StunVisualMode == STUN_VISUAL_GENERIC_ANIM) {
                uint32_t restoreAction = 0x0000;
                if (s_fnSetCharacterAction) {
                    s_fnSetCharacterAction(pChar, nullptr, restoreAction);
                }
                *(uint32_t*)((uintptr_t)pChar + 0x1D4) = restoreAction;
            }

            if (!isAirOrJuggle) {
                void* pInfo = *(void**)((uintptr_t)pChar + 0x7638);
                if (pInfo && (uintptr_t)pInfo > 0x01000000) {
                    bool negEdge = (defPlr == 1) ? g_P1_NegativeEdgeEnabled : g_P2_NegativeEdgeEnabled;
                    *(uint32_t*)((uintptr_t)pInfo + 0x944) = negEdge ? 0 : 1;
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    static void TriggerGenericStunAnim(int defPlr, void* pChar) {
        if (!pChar || (uintptr_t)pChar < 0x01000000) return;
        __try {
            uint32_t targetAction = (uint32_t)g_GenericStunActionId;
            if (s_fnSetCharacterAction) {
                s_fnSetCharacterAction(pChar, nullptr, targetAction);
            }
            *(uint32_t*)((uintptr_t)pChar + 0x1D4) = targetAction;
            g_PlayerStun[defPlr].lastStunAction = targetAction;
            LogTrace("[STUN MODIFIER | F%llu] P%d transitioned to Generic Stun Action 0x%04X (Dizzy/Stumble) for extended hitstun hold!",
                     g_CurrentFrame, defPlr, targetAction);
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    void ProcessStunLockTick(int defPlr, void* pChar, uint32_t action, bool isKnockdown, bool isBlockstun, uint32_t elapsed, bool* pStillLocked) {
        if (defPlr < 1 || defPlr > 2) return;
        PlayerStunState& s = g_PlayerStun[defPlr];
        if (!s.active) return;

        if (!pChar || (uintptr_t)pChar < 0x01000000) {
            s.active = false;
            s.isHoldingExtraStun = false;
            return;
        }

        // Safety timeout after 180 frames (3 seconds)
        if (g_CurrentFrame - s.hitFrame > 180) {
            ReleaseStunHold(defPlr, pChar);
            if (pStillLocked) *pStillLocked = false;
            return;
        }

        // Check for knockdown / air / ground / wakeup / roll / launcher during stun:
        float charHeight = 0.0f;
        uint32_t charStance = 0;
        __try {
            charHeight = *(float*)((uintptr_t)pChar + 0x1F0);
            charStance = *(uint32_t*)((uintptr_t)pChar + 0x2E8);
        } __except (EXCEPTION_EXECUTE_HANDLER) {}

        bool isAirborneOrLaunched = (charHeight > 5.0f) || ((charStance & 0x01) != 0);

        bool isFloorOrWakeup = (action >= 0x6000 && action <= 0x62FF) || 
                               (action == 0x4201) || 
                               (action >= 0x460A && action <= 0x460F) || 
                               (action >= 0x4200 && action <= 0x45FF && action != 0x4205 && action != 0x4206) ||
                               isAirborneOrLaunched;

        if (isKnockdown || isFloorOrWakeup) {
            // Knockdown, fall, tumble, launcher, or ground wakeup!
            // Cleanly abort stun modifier tracking WITHOUT touching action state or input gates!
            FreeCamera::ReleaseCinematicZoom();
            ResetAnimRateScale(defPlr, pChar);
            s.active = false;
            s.isHoldingExtraStun = false;
            s.animRateScale = 1.0f;
            if (s.pLastAnimSeq && (uintptr_t)s.pLastAnimSeq > 0x01000000) {
                __try { *(float*)((uintptr_t)s.pLastAnimSeq + 0x48) = 1.0f; } __except (EXCEPTION_EXECUTE_HANDLER) {}
                s.pLastAnimSeq = nullptr;
            }
            if (pStillLocked) *pStillLocked = false;
            return;
        }

        // Capture valid standing / crouching stun action while actively stunned
        bool isStandingOrCrouchingStun = (action == 0x4600 || action == 0x4700 || action == 0x0A01 || action == 0x4F00 ||
                                         (action >= 0x0A02 && action <= 0x0A0F) ||
                                         (action >= 0x4601 && action <= 0x4605) ||
                                         action == 0x4205 || action == 0x4206);
        if (isStandingOrCrouchingStun) {
            s.lastStunAction = action;
        }

        // -------------------------------------------------------------
        // Dedicated Blockstun Modifier (Preserves Move Frame Alignment)
        // -------------------------------------------------------------
        if (s.active && s.isBlocked && s.totalExtraFrames != 0) {
            uint32_t baseStun = (s.baseAuthoredStun > 0) ? s.baseAuthoredStun : 28;
            uint32_t targetFrames = (int)baseStun + s.totalExtraFrames > 1 ? (uint32_t)((int)baseStun + s.totalExtraFrames) : 1;

            bool hasExitedBlock = (action == 0x0000 || (action >= 0x2000 && action <= 0x2015));
            if (elapsed > targetFrames || (s.totalExtraFrames < 0 && hasExitedBlock)) {
                *(uint32_t*)((uintptr_t)pChar + 0x2EC) &= ~0x400;
                ResetAnimRateScale(defPlr, pChar);
                if (action == 0x0A01 || action == 0x4F00 || (action >= 0x0A02 && action <= 0x0A0F)) {
                    if (s_fnSetCharacterAction) {
                        s_fnSetCharacterAction(pChar, nullptr, 0x0000);
                    }
                    *(uint32_t*)((uintptr_t)pChar + 0x1D4) = 0x0000;
                }
                *(float*)((uintptr_t)pChar + 0x2F0) = 0.0f;
                *(float*)((uintptr_t)pChar + 0x2F4) = 0.0f;
                if (s.pLastAnimSeq && (uintptr_t)s.pLastAnimSeq > 0x01000000) {
                    __try { *(float*)((uintptr_t)s.pLastAnimSeq + 0x48) = 1.0f; } __except (EXCEPTION_EXECUTE_HANDLER) {}
                    s.pLastAnimSeq = nullptr;
                }
                *pStillLocked = false;
                s.active = false;
                s.isHoldingExtraStun = false;
                s.totalExtraFrames = 0;
            } else {
                *(uint32_t*)((uintptr_t)pChar + 0x2EC) |= 0x400;
                if (s.animRateScale > 0.01f) {
                    ApplyAnimRateScale(defPlr, pChar, s.animRateScale);
                }
                if (s.totalExtraFrames > 0 && (action == 0x0000 || action == 0x0100)) {
                    *(uint32_t*)((uintptr_t)pChar + 0x1D4) = (s.lastStunAction != 0) ? s.lastStunAction : 0x0A01;
                }
                *pStillLocked = true;
            }
            return;
        }

        // -------------------------------------------------------------
        // Hitstun Modifier (Standing Hits Only)
        // -------------------------------------------------------------
        if (s.totalExtraFrames > 0 && s.lastStunAction != 0) {
            if (g_StunVisualMode == STUN_VISUAL_SLOW_MO) {
                // Mode 0: Congruent Slow-Motion Hurt Animation
                if (s.releaseFrame > 0 && g_CurrentFrame < s.releaseFrame) {
                    *pStillLocked = true;
                    EnforceStunHold(defPlr, pChar);
                } else if (s.releaseFrame > 0 && g_CurrentFrame >= s.releaseFrame) {
                    ReleaseStunHold(defPlr, pChar);
                    *pStillLocked = false;
                    s.active = false;
                }
            } else {
                // Modes 1 & 2: Natural Stun followed by Stun Hold
                if (!*pStillLocked && !s.isHoldingExtraStun) {
                    // Natural stun ended on this frame! Arm the extended hold!
                    s.isHoldingExtraStun = true;
                    s.releaseFrame = g_CurrentFrame + (uint64_t)s.totalExtraFrames;

                    // Mode 1: Generic Dizzy / Stumble animation
                    if (g_StunVisualMode == STUN_VISUAL_GENERIC_ANIM) {
                        TriggerGenericStunAnim(defPlr, pChar);
                    }

                    *pStillLocked = true;
                    EnforceStunHold(defPlr, pChar);
                } else if (s.isHoldingExtraStun) {
                    if (g_CurrentFrame < s.releaseFrame) {
                        *pStillLocked = true;
                        EnforceStunHold(defPlr, pChar);
                    } else {
                        ReleaseStunHold(defPlr, pChar);
                        *pStillLocked = false;
                        s.active = false;
                        s.isHoldingExtraStun = false;
                    }
                }
            }
        } else if (s.totalExtraFrames < 0) {
            // Negative hitstun delta: early release when releaseFrame is reached or native timer elapsed
            uint32_t currAct = *(uint32_t*)((uintptr_t)pChar + 0x1D4);
            bool hasExitedHitstun = (currAct == 0x0000 || (currAct >= 0x2000 && currAct <= 0x2015));
            if ((s.releaseFrame > 0 && g_CurrentFrame >= s.releaseFrame) || hasExitedHitstun) {
                ReleaseStunHold(defPlr, pChar);
                if (currAct == 0x4600 || currAct == 0x4700 || (currAct >= 0x4601 && currAct <= 0x4605)) {
                    if (s_fnSetCharacterAction) {
                        s_fnSetCharacterAction(pChar, nullptr, 0x0000);
                    }
                    *(uint32_t*)((uintptr_t)pChar + 0x1D4) = 0x0000;
                }
                *(float*)((uintptr_t)pChar + 0x2F0) = 0.0f;
                *(float*)((uintptr_t)pChar + 0x2F4) = 0.0f;
                *pStillLocked = false;
                s.active = false;
                s.isHoldingExtraStun = false;
                s.totalExtraFrames = 0;
            } else if (*pStillLocked) {
                EnforceStunHold(defPlr, pChar);
            }
        }
    }

    bool IsInExtendedStun(int playerNum) {
        if (playerNum < 1 || playerNum > 2) return false;
        const PlayerStunState& s = g_PlayerStun[playerNum];
        return s.active && (s.isHoldingExtraStun || (s.totalExtraFrames > 0 && g_CurrentFrame < s.releaseFrame));
    }

    bool IsInExtendedBlockstun(int playerNum) {
        if (playerNum < 1 || playerNum > 2) return false;
        const PlayerStunState& s = g_PlayerStun[playerNum];
        return s.active && s.isBlocked && (s.isHoldingExtraStun || (s.totalExtraFrames > 0 && g_CurrentFrame < s.releaseFrame));
    }

    bool IsPlayerInBlockstun(int playerNum) {
        if (playerNum < 1 || playerNum > 2) return false;
        const PlayerStunState& s = g_PlayerStun[playerNum];
        return s.active && s.isBlocked;
    }

    uint32_t GetComboHitCount(int attPlr) {
        if (attPlr < 1 || attPlr > 2) return 0;
        return s_ComboTracker[attPlr].inCombo ? s_ComboTracker[attPlr].hitCount : 0;
    }

    bool IsInCombo(int attPlr) {
        if (attPlr < 1 || attPlr > 2) return false;
        return s_ComboTracker[attPlr].inCombo;
    }

} // namespace StunModifier
} // namespace MK9TradeFix
