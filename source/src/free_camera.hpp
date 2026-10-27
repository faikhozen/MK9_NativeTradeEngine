#pragma once
#include <windows.h>
#include <cstdint>
#include <cmath>

namespace MK9TradeFix {
namespace FreeCamera {

    // Native Unreal Engine 3 / MK9 Structures
    struct FVector {
        float X; // Depth / Forward axis (into screen)
        float Y; // Lateral / Fighting plane axis
        float Z; // Vertical / Height axis

        FVector() : X(0.0f), Y(0.0f), Z(0.0f) {}
        FVector(float x, float y, float z) : X(x), Y(y), Z(z) {}

        FVector operator+(const FVector& o) const { return FVector(X + o.X, Y + o.Y, Z + o.Z); }
        FVector operator-(const FVector& o) const { return FVector(X - o.X, Y - o.Y, Z - o.Z); }
        FVector operator*(float s) const { return FVector(X * s, Y * s, Z * s); }
        FVector& operator+=(const FVector& o) { X += o.X; Y += o.Y; Z += o.Z; return *this; }
        FVector& operator-=(const FVector& o) { X -= o.X; Y -= o.Y; Z -= o.Z; return *this; }
    };

    struct FRotator {
        int Pitch; // Elevation angle (-16384 .. 16384)
        int Yaw;   // Azimuth angle (0 .. 65535, 65536 = 360 degrees)
        int Roll;  // Tilt angle

        FRotator() : Pitch(0), Yaw(0), Roll(0) {}
        FRotator(int p, int y, int r) : Pitch(p), Yaw(y), Roll(r) {}
    };

    // Subsystem State
    extern bool g_FreeCamEnabled;
    extern bool g_FreeCamPausedUpdate;
    extern FVector g_FreeCamPos;
    extern FRotator g_FreeCamRot;
    extern float g_FreeCamFOV;
    extern bool g_bCustomFOV;

    // Movement & Sensitivity Settings
    extern float g_FreeCamMoveSpeed;
    extern float g_FreeCamFastMultiplier;
    extern float g_FreeCamSlowMultiplier;
    extern int g_FreeCamRotSpeed;
    extern float g_MouseSensitivity;
    extern bool g_bHoldRMBToLook;
    extern bool g_bAlwaysLockMouse;
    extern bool g_bInvertPitch;
    extern bool g_bInvertYaw;
    extern bool g_bGamepadEnabled;

    // Native Camera Object Pointer
    extern void* g_pMKCamera;

    // Cinematic Zoom & Angled Camera on Counter / Punish / Trade
    extern bool     g_CinematicZoomOnCounterHit;
    extern float    g_CinematicZoomAmount;
    extern bool     g_CinematicZoomActive;
    extern float    g_CurrentCinematicFOV;
    extern uint64_t g_CinematicZoomExpireFrame;
    extern int      g_CinematicZoomDefPlr;
    extern int      g_CinematicZoomAttPlr;
    extern int      g_CinematicCamMode;
    extern bool     g_CinematicZoomSawKnockdown;
    extern float    g_CurrentCinematicShiftY;
    extern float    g_CurrentCinematicYawOffset;
    extern float    g_TargetCinematicShiftY;
    extern float    g_TargetCinematicYawOffset;

    // Cinematic Camera Smoothing & General Slow Motion
    extern float    g_CinematicCameraSpeedIn;   // Speed entering dynamic camera (Default: 0.09f = smooth glide)
    extern float    g_CinematicCameraSpeedOut;  // Speed returning to standard camera (Default: 0.08f)
    extern bool     g_CinematicSlowMoEnabled;   // General slow motion during dynamic camera adjustment (Default: true)
    extern float    g_CinematicSlowMoRate;      // General slow motion playback speed (Default: 0.35f = 35% speed)

    void TriggerCinematicZoom(float zoomOffset = -6.0f, int holdFrames = 0, int defPlr = 0, int attPlr = 0, int camMode = 1, float shiftY = 0.0f, float yawOffsetDeg = 0.0f);
    void ReleaseCinematicZoom(bool force = false);

    // Lifecycle & API
    void InitHooks();
    void ToggleFreeCamera();
    void SetEnabled(bool enabled);
    bool IsEnabled();
    void ResetCameraSettingsForMatchLoad();

    // Direction Vectors & Calculation
    void GetDirectionVectors(const FRotator& rot, FVector& outForward, FVector& outRight, FVector& outUp);

    // Frame Execution
    void UpdateInput(float deltaTime);
    void ForceCameraUpdate();

    // Presets
    void ResetToGameCamera();
    void PresetPlayer1CloseUp();
    void PresetPlayer2CloseUp();
    void PresetTopDown();
    void PresetCinematicSide();

    // ImGui Interface
    void RenderUI();

} // namespace FreeCamera
} // namespace MK9TradeFix
