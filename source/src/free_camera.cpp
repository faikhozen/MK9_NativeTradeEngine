#include "free_camera.hpp"
#include "combat_state.hpp"
#include "stun_modifier.hpp"
#include "imgui/imgui.h"
#include "minhook/include/MinHook.h"
#include <xinput.h>
#include <cstdio>
#include <algorithm>

namespace MK9TradeFix {

    extern void LogTrace(const char* fmt, ...);

namespace FreeCamera {

    // Subsystem State
    bool g_FreeCamEnabled = false;
    bool g_FreeCamPausedUpdate = true;
    FVector g_FreeCamPos = { 0.0f, 0.0f, 0.0f };
    FRotator g_FreeCamRot = { 0, 0, 0 };
    float g_FreeCamFOV = 60.0f;
    bool g_bCustomFOV = false;

    // Cinematic Zoom & Angled Camera on Counter / Punish / Trade
    bool     g_CinematicZoomOnCounterHit = true;
    float    g_CinematicZoomAmount = -6.0f; // Negative = close-up, Positive = widen
    bool     g_CinematicZoomActive = false;
    float    g_CurrentCinematicFOV = 0.0f;
    uint64_t g_CinematicZoomExpireFrame = 0;
    int      g_CinematicZoomDefPlr = 0;
    int      g_CinematicZoomAttPlr = 0;
    int      g_CinematicCamMode = 1;
    bool     g_CinematicZoomSawKnockdown = false;
    uint64_t g_CinematicZoomTriggerFrame = 0;
    float    g_CurrentCinematicShiftY = 0.0f;
    float    g_CurrentCinematicYawOffset = 0.0f;
    float    g_TargetCinematicShiftY = 0.0f;
    float    g_TargetCinematicYawOffset = 0.0f;

    // Cinematic Camera Smoothing & General Slow Motion
    float    g_CinematicCameraSpeedIn = 0.23f;   // Camera Move-In Smoothing Speed (Default: 0.23f = Smooth Glide)
    float    g_CinematicCameraSpeedOut = 0.23f;  // Camera Move-Out Return Speed (Default: 0.23f = Smooth Return)
    bool     g_CinematicSlowMoEnabled = true;   // General slow motion during dynamic camera adjustment (Default: true)
    float    g_CinematicSlowMoRate = 0.80f;      // General slow motion playback speed (Default: 0.80f = 80% speed)

    void TriggerCinematicZoom(float zoomOffset, int holdFrames, int defPlr, int attPlr, int camMode, float shiftY, float yawOffsetDeg) {
        g_CinematicZoomAmount = zoomOffset;
        g_CinematicZoomActive = (camMode > 0);
        g_CinematicZoomDefPlr = defPlr;
        g_CinematicZoomAttPlr = attPlr;
        g_CinematicCamMode = camMode;
        g_CinematicZoomSawKnockdown = false;
        g_CinematicZoomTriggerFrame = g_CurrentFrame;
        if (holdFrames > 0) {
            g_CinematicZoomExpireFrame = g_CurrentFrame + (uint64_t)holdFrames;
        } else {
            g_CinematicZoomExpireFrame = 0;
        }

        if (camMode == 2 && (shiftY > 0.001f || yawOffsetDeg > 0.001f)) {
            // Option 2: Angled Cinematic Camera
            // Move Position Y towards Attacker, Yaw angle towards Defender
            void* pAtt = (attPlr == 2) ? GetP2Char() : GetP1Char();
            void* pDef = (defPlr == 2) ? GetP2Char() : GetP1Char();
            float dir = (attPlr == 2) ? 1.0f : -1.0f; // Default: P1 is left (-Y), P2 is right (+Y)
            if (pAtt && pDef && (uintptr_t)pAtt > 0x01000000 && (uintptr_t)pDef > 0x01000000) {
                __try {
                    float attLat = *(float*)((uintptr_t)pAtt + 0x1E8);
                    float defLat = *(float*)((uintptr_t)pDef + 0x1E8);
                    if (attLat < defLat) {
                        dir = -1.0f; // Attacker is on the Left (-Y)
                    } else {
                        dir = 1.0f;  // Attacker is on the Right (+Y)
                    }
                } __except (EXCEPTION_EXECUTE_HANDLER) {}
            }

            // Move Y towards attacker: dir * shiftY
            g_TargetCinematicShiftY = dir * shiftY;
            // Yaw towards defender: -dir * yawOffsetDeg * 182.0444f
            g_TargetCinematicYawOffset = -dir * (yawOffsetDeg * 182.0444f);
        } else {
            g_TargetCinematicShiftY = 0.0f;
            g_TargetCinematicYawOffset = 0.0f;
        }

        // Camera dynamic framing applied
        LogTrace("[FREE CAMERA] Dynamic Cinematic Camera Triggered (Mode: %d, FOV: %+.1f deg, ShiftY: %+.1f, Yaw: %+.1f, Def: P%d, Att: P%d, SlowMo: %s)",
                 camMode, zoomOffset, g_TargetCinematicShiftY, g_TargetCinematicYawOffset / 182.0444f, defPlr, attPlr,
                 g_CinematicSlowMoEnabled ? "ON" : "OFF");
    }

    void ReleaseCinematicZoom(bool force) {
        if (!force && g_CinematicZoomExpireFrame > 0 && g_CurrentFrame < g_CinematicZoomExpireFrame) {
            // Still in timed hold period (e.g. 30 frames for Trade)
            return;
        }
        if (g_CinematicZoomActive) {
            g_CinematicZoomActive = false;
            g_CinematicZoomExpireFrame = 0;
            g_CinematicZoomDefPlr = 0;
            g_CinematicZoomAttPlr = 0;
            g_CinematicZoomSawKnockdown = false;
            g_TargetCinematicShiftY = 0.0f;
            g_TargetCinematicYawOffset = 0.0f;

            LogTrace("[FREE CAMERA] Dynamic Cinematic Camera Released back to standard");
        }
    }

    // Movement & Sensitivity Settings
    float g_FreeCamMoveSpeed = 15.0f;
    float g_FreeCamFastMultiplier = 3.0f;
    float g_FreeCamSlowMultiplier = 0.25f;
    int g_FreeCamRotSpeed = 400;
    float g_MouseSensitivity = 18.0f;
    bool g_bHoldRMBToLook = true;
    bool g_bAlwaysLockMouse = false;
    bool g_bInvertPitch = false;
    bool g_bInvertYaw = false;
    bool g_bGamepadEnabled = true;

    // Native Camera Object Pointer
    void* g_pMKCamera = nullptr;

    // Native Game Baseline Coordinates (continuously tracked every frame from game engine)
    static FVector g_NativeCamPos = { -550.0f, 0.0f, 160.0f };
    static FRotator g_NativeCamRot = { 0, 16384, 0 };
    static float g_NativeCamFOV = 60.0f;
    static int g_NativeCamFOVUnk = 1;
    static bool g_HasNativeCoords = false;

    // Native Function Types & Trampolines
    typedef void (__thiscall *tMKCamera_SetPosition)(void* pCamera, FVector* pPos);
    static tMKCamera_SetPosition oMKCamera_SetPosition = nullptr;

    typedef void (__thiscall *tMKCamera_SetRotation)(void* pCamera, FRotator* pRot);
    static tMKCamera_SetRotation oMKCamera_SetRotation = nullptr;

    typedef void (__thiscall *tMKCamera_SetFOV)(void* pCamera, float fov, int unk);
    static tMKCamera_SetFOV oMKCamera_SetFOV = nullptr;

    static float s_LastAppliedShiftY = 0.0f;
    static int   s_LastAppliedYaw = 0;

    // Hook Implementations
    static void __fastcall hkMKCamera_SetPosition(void* pCamera, void* edx, FVector* pPos) {
        g_pMKCamera = pCamera;

        if (pPos) {
            float rawY = pPos->Y - s_LastAppliedShiftY;
            g_NativeCamPos.X = pPos->X;
            g_NativeCamPos.Y = rawY;
            g_NativeCamPos.Z = pPos->Z;
            g_HasNativeCoords = true;
        }

        if (!g_FreeCamEnabled) {
            static uint64_t s_LastShiftFrame = 0;
            if (s_LastShiftFrame != g_CurrentFrame) {
                s_LastShiftFrame = g_CurrentFrame;
                // Interpolate dynamic cinematic shift Y (Option 2)
                float targetShift = (g_CinematicZoomActive && g_CinematicCamMode == 2) ? g_TargetCinematicShiftY : 0.0f;
                float speed = g_CinematicZoomActive ? g_CinematicCameraSpeedIn : g_CinematicCameraSpeedOut;
                g_CurrentCinematicShiftY += (targetShift - g_CurrentCinematicShiftY) * speed;
                if (!g_CinematicZoomActive && fabsf(g_CurrentCinematicShiftY) < 0.2f) {
                    g_CurrentCinematicShiftY = 0.0f;
                }
            }

            if (pPos) {
                g_FreeCamPos = g_NativeCamPos;
                if (fabsf(g_CurrentCinematicShiftY) > 0.05f) {
                    FVector modifiedPos = g_NativeCamPos;
                    modifiedPos.Y += g_CurrentCinematicShiftY;
                    s_LastAppliedShiftY = g_CurrentCinematicShiftY;
                    if (oMKCamera_SetPosition) {
                        oMKCamera_SetPosition(pCamera, &modifiedPos);
                    }
                } else {
                    s_LastAppliedShiftY = 0.0f;
                    if (oMKCamera_SetPosition) {
                        oMKCamera_SetPosition(pCamera, &g_NativeCamPos);
                    }
                }
            }
        } else {
            s_LastAppliedShiftY = 0.0f;
            if (oMKCamera_SetPosition) {
                oMKCamera_SetPosition(pCamera, &g_FreeCamPos);
            }
        }
    }

    static void __fastcall hkMKCamera_SetRotation(void* pCamera, void* edx, FRotator* pRot) {
        g_pMKCamera = pCamera;

        if (pRot) {
            int rawYaw = pRot->Yaw - s_LastAppliedYaw;
            g_NativeCamRot.Pitch = pRot->Pitch;
            g_NativeCamRot.Yaw = rawYaw;
            g_NativeCamRot.Roll = pRot->Roll;
        }

        if (!g_FreeCamEnabled) {
            static uint64_t s_LastYawFrame = 0;
            if (s_LastYawFrame != g_CurrentFrame) {
                s_LastYawFrame = g_CurrentFrame;
                // Interpolate dynamic cinematic yaw offset (Option 2)
                float targetYaw = (g_CinematicZoomActive && g_CinematicCamMode == 2) ? g_TargetCinematicYawOffset : 0.0f;
                float speed = g_CinematicZoomActive ? g_CinematicCameraSpeedIn : g_CinematicCameraSpeedOut;
                g_CurrentCinematicYawOffset += (targetYaw - g_CurrentCinematicYawOffset) * speed;
                if (!g_CinematicZoomActive && fabsf(g_CurrentCinematicYawOffset) < 2.0f) {
                    g_CurrentCinematicYawOffset = 0.0f;
                }
            }

            if (pRot) {
                g_FreeCamRot = g_NativeCamRot;
                if (fabsf(g_CurrentCinematicYawOffset) > 0.5f) {
                    FRotator modifiedRot = g_NativeCamRot;
                    modifiedRot.Yaw += (int)g_CurrentCinematicYawOffset;
                    s_LastAppliedYaw = (int)g_CurrentCinematicYawOffset;
                    if (oMKCamera_SetRotation) {
                        oMKCamera_SetRotation(pCamera, &modifiedRot);
                    }
                } else {
                    s_LastAppliedYaw = 0;
                    if (oMKCamera_SetRotation) {
                        oMKCamera_SetRotation(pCamera, &g_NativeCamRot);
                    }
                }
            }
        } else {
            s_LastAppliedYaw = 0;
            if (oMKCamera_SetRotation) {
                oMKCamera_SetRotation(pCamera, &g_FreeCamRot);
            }
        }
    }

    static void __fastcall hkMKCamera_SetFOV(void* pCamera, void* edx, float fov, int unk) {
        g_pMKCamera = pCamera;
        g_NativeCamFOV = fov;
        g_NativeCamFOVUnk = unk;

        // Check timed hold expiration (e.g. 30f on Trade)
        if (g_CinematicZoomActive && g_CinematicZoomExpireFrame > 0 && g_CurrentFrame >= g_CinematicZoomExpireFrame) {
            ReleaseCinematicZoom(true);
            LogTrace("[FREE CAMERA] Dynamic FOV timed hold expired, returning to standard FOV");
        }

        // Check dynamic camera defender state (e.g. knockdown getup or neutral recovery)
        if (g_CinematicZoomActive && g_CinematicZoomExpireFrame == 0 && g_CinematicZoomDefPlr >= 1 && g_CinematicZoomDefPlr <= 2) {
            void* pDef = (g_CinematicZoomDefPlr == 2) ? GetP2Char() : GetP1Char();
            if (pDef && (uintptr_t)pDef > 0x01000000) {
                __try {
                    uint32_t action = *(uint32_t*)((uintptr_t)pDef + 0x1D4);
                    float height = *(float*)((uintptr_t)pDef + 0x1F0);

                    // Air hurt, tumble, launch, floor KD, roll, or getup
                    bool isKnockedDownOrGetup = (action >= 0x4200 && action <= 0x45FF && action != 0x4205 && action != 0x4206) ||
                                                (action >= 0x460A && action <= 0x460F) ||
                                                (action == 0x4201) ||
                                                (action >= 0x6000 && action <= 0x62FF) ||
                                                (height > 15.0f);

                    if (isKnockedDownOrGetup) {
                        g_CinematicZoomSawKnockdown = true;
                    }

                    if (g_CinematicZoomSawKnockdown) {
                        // Defender was knocked down! Did they finish getup?
                        bool isGrounded = (height <= 5.0f);
                        bool hasGottenUp = isGrounded && !isKnockedDownOrGetup && (
                            action == 0x0000 ||                              // Neutral stand
                            action == 0x0100 || action == 0x0101 ||          // Neutral crouch
                            (action >= 0x0200 && action <= 0x020F) ||        // Walk
                            (action >= 0x0900 && action <= 0x0A01) ||        // Guard
                            (action >= 0x2010 && action <= 0x201F) ||        // Dash
                            (action >= 0x5000 && action <= 0x5FFF)           // Started move after getup
                        );

                        if (hasGottenUp) {
                            LogTrace("[FREE CAMERA | F%llu] Defender P%d got up from knockdown (Action=0x%04X)! Releasing dynamic camera.",
                                     g_CurrentFrame, g_CinematicZoomDefPlr, action);
                            ReleaseCinematicZoom(true);
                        }
                    } else if (g_CurrentFrame > g_CinematicZoomTriggerFrame + 15) {
                        // Standard standing hit: check if defender returned to neutral
                        bool inNeutral = (action == 0x0000 || action == 0x0100 || action == 0x0101 ||
                                         (action >= 0x0200 && action <= 0x020F) ||
                                         (action >= 0x0900 && action <= 0x0A01) ||
                                         (action >= 0x5000 && action <= 0x5FFF));
                        if (inNeutral && !StunModifier::IsInExtendedStun(g_CinematicZoomDefPlr)) {
                            LogTrace("[FREE CAMERA | F%llu] Defender P%d recovered to neutral (Action=0x%04X)! Releasing dynamic camera.",
                                     g_CurrentFrame, g_CinematicZoomDefPlr, action);
                            ReleaseCinematicZoom(true);
                        }
                    }

                    // Safety timeout: 180 frames (3 seconds)
                    if (g_CurrentFrame > g_CinematicZoomTriggerFrame + 180) {
                        ReleaseCinematicZoom(true);
                    }
                } __except (EXCEPTION_EXECUTE_HANDLER) {}
            }
        }

        // 1. Dynamic Cinematic Close-Up / Widen FOV on Counterhit / Fatal Punish / Trade
        if (!g_FreeCamEnabled && (g_CinematicZoomActive || (g_CurrentCinematicFOV > 0.0f && fabsf(g_CurrentCinematicFOV - fov) > 0.05f))) {
            if (g_CurrentCinematicFOV <= 0.0f) {
                g_CurrentCinematicFOV = fov;
            }

            float targetFOV = g_CinematicZoomActive ? (fov + g_CinematicZoomAmount) : fov;
            if (targetFOV < 20.0f) targetFOV = 20.0f;
            if (targetFOV > 120.0f) targetFOV = 120.0f;

            // Smooth cinematic FOV glide
            float speed = g_CinematicZoomActive ? g_CinematicCameraSpeedIn : g_CinematicCameraSpeedOut;
            g_CurrentCinematicFOV += (targetFOV - g_CurrentCinematicFOV) * speed;

            if (!g_CinematicZoomActive && fabsf(g_CurrentCinematicFOV - fov) < 0.15f) {
                g_CurrentCinematicFOV = fov;
            }

            if (g_CinematicZoomActive || fabsf(g_CurrentCinematicFOV - fov) > 0.05f) {
                if (oMKCamera_SetFOV) {
                    oMKCamera_SetFOV(pCamera, g_CurrentCinematicFOV, unk);
                }
                return;
            } else {
                g_CurrentCinematicFOV = fov;
            }
        }

        // 2. Manual User FOV Override
        if (g_bCustomFOV) {
            if (oMKCamera_SetFOV) {
                oMKCamera_SetFOV(pCamera, g_FreeCamFOV, unk);
            }
        } else {
            g_FreeCamFOV = fov;
            if (oMKCamera_SetFOV) {
                oMKCamera_SetFOV(pCamera, fov, unk);
            }
        }
    }

    void InitHooks() {
        // Hook root camera functions in MKKE.exe
        // 0x0045E1C0 : SetPosition(FVector* pos)
        if (MH_CreateHook((void*)0x0045E1C0, (void*)hkMKCamera_SetPosition, (void**)&oMKCamera_SetPosition) == MH_OK) {
            MH_EnableHook((void*)0x0045E1C0);
            LogTrace("[FREE CAMERA] Successfully hooked MKCamera::SetPosition (0x0045E1C0)");
        } else {
            LogTrace("[FREE CAMERA] [ERROR] Failed to hook MKCamera::SetPosition");
        }

        // 0x0045DB30 : SetRotation(FRotator* rot)
        if (MH_CreateHook((void*)0x0045DB30, (void*)hkMKCamera_SetRotation, (void**)&oMKCamera_SetRotation) == MH_OK) {
            MH_EnableHook((void*)0x0045DB30);
            LogTrace("[FREE CAMERA] Successfully hooked MKCamera::SetRotation (0x0045DB30)");
        } else {
            LogTrace("[FREE CAMERA] [ERROR] Failed to hook MKCamera::SetRotation");
        }

        // 0x0045DAE0 : SetFOV(float fov, int unk)
        if (MH_CreateHook((void*)0x0045DAE0, (void*)hkMKCamera_SetFOV, (void**)&oMKCamera_SetFOV) == MH_OK) {
            MH_EnableHook((void*)0x0045DAE0);
            LogTrace("[FREE CAMERA] Successfully hooked MKCamera::SetFOV (0x0045DAE0)");
        } else {
            LogTrace("[FREE CAMERA] [ERROR] Failed to hook MKCamera::SetFOV");
        }
    }

    void ToggleFreeCamera() {
        SetEnabled(!g_FreeCamEnabled);
    }

    void SetEnabled(bool enabled) {
        if (g_FreeCamEnabled == enabled) return;
        g_FreeCamEnabled = enabled;

        if (g_FreeCamEnabled) {
            // When detaching, sync from latest native coordinates so there's zero jump
            if (g_HasNativeCoords) {
                g_FreeCamPos = g_NativeCamPos;
                g_FreeCamRot = g_NativeCamRot;
                if (!g_bCustomFOV) {
                    g_FreeCamFOV = g_NativeCamFOV;
                }
            } else if (g_pMKCamera) {
                g_FreeCamPos = *(FVector*)((uintptr_t)g_pMKCamera + 0x10C);
                g_FreeCamRot = *(FRotator*)((uintptr_t)g_pMKCamera + 0x118);
                if (!g_bCustomFOV) {
                    g_FreeCamFOV = *(float*)((uintptr_t)g_pMKCamera + 0x140);
                }
            }
            LogTrace("[FREE CAMERA] Free Camera Activated. (Pos: %.1f, %.1f, %.1f | Rot: %d, %d, %d)",
                     g_FreeCamPos.X, g_FreeCamPos.Y, g_FreeCamPos.Z,
                     g_FreeCamRot.Pitch, g_FreeCamRot.Yaw, g_FreeCamRot.Roll);
        } else {
            // Disable custom FOV
            g_bCustomFOV = false;

            // Immediately force camera back to native game coordinates!
            if (g_pMKCamera) {
                if (g_HasNativeCoords) {
                    if (oMKCamera_SetPosition) oMKCamera_SetPosition(g_pMKCamera, &g_NativeCamPos);
                    if (oMKCamera_SetRotation) oMKCamera_SetRotation(g_pMKCamera, &g_NativeCamRot);
                    if (oMKCamera_SetFOV) oMKCamera_SetFOV(g_pMKCamera, g_NativeCamFOV, g_NativeCamFOVUnk);
                } else {
                    FVector defPos(-550.0f, 0.0f, 160.0f);
                    FRotator defRot(0, 16384, 0);
                    if (oMKCamera_SetPosition) oMKCamera_SetPosition(g_pMKCamera, &defPos);
                    if (oMKCamera_SetRotation) oMKCamera_SetRotation(g_pMKCamera, &defRot);
                    if (oMKCamera_SetFOV) oMKCamera_SetFOV(g_pMKCamera, 60.0f, 1);
                }
            }
            LogTrace("[FREE CAMERA] Free Camera Deactivated. Native game camera restored.");
        }
    }

    bool IsEnabled() {
        return g_FreeCamEnabled;
    }

    void ResetCameraSettingsForMatchLoad() {
        g_CinematicZoomActive = false;
        g_CinematicZoomExpireFrame = 0;
        g_CurrentCinematicFOV = 0.0f;
        g_CinematicZoomDefPlr = 0;
        g_CinematicZoomSawKnockdown = false;
        g_CinematicZoomTriggerFrame = 0;
        g_TargetCinematicShiftY = 0.0f;
        g_TargetCinematicYawOffset = 0.0f;
        g_CurrentCinematicShiftY = 0.0f;
        g_CurrentCinematicYawOffset = 0.0f;
        s_LastAppliedShiftY = 0.0f;
        s_LastAppliedYaw = 0;
        if (!g_FreeCamEnabled && !g_bCustomFOV) {
            return;
        }
        LogTrace("[FREE CAMERA] Match load detected. Automatically resetting camera overrides to default.");
        g_FreeCamEnabled = false;
        g_bCustomFOV = false;
        g_bAlwaysLockMouse = false;
        g_FreeCamMoveSpeed = 15.0f;
        g_FreeCamRotSpeed = 400;
        g_FreeCamFastMultiplier = 3.0f;
        g_FreeCamSlowMultiplier = 0.25f;
    }

    void GetDirectionVectors(const FRotator& rot, FVector& outForward, FVector& outRight, FVector& outUp) {
        // Unreal Engine 3 angle conversion: 65536 units = 2*PI radians
        const float radFactor = 3.14159265358979323846f / 32768.0f;
        float radP = (float)rot.Pitch * radFactor;
        float radY = (float)rot.Yaw   * radFactor;
        float radR = (float)rot.Roll  * radFactor;

        float sp = sinf(radP), cp = cosf(radP);
        float sy = sinf(radY), cy = cosf(radY);
        float sr = sinf(radR), cr = cosf(radR);

        // Native UE3 FMatrix formulas:
        outForward = FVector(cp * cy, cp * sy, sp);
        outRight   = FVector(sr * sp * cy - cr * sy, sr * sp * sy + cr * cy, -sr * cp);
        outUp      = FVector(-(cr * sp * cy + sr * sy), cy * sr - cr * sp * sy, cr * cp);
    }

    void ForceCameraUpdate() {
        if (!g_pMKCamera) return;

        if (g_FreeCamEnabled) {
            if (oMKCamera_SetPosition) {
                oMKCamera_SetPosition(g_pMKCamera, &g_FreeCamPos);
            }
            if (oMKCamera_SetRotation) {
                oMKCamera_SetRotation(g_pMKCamera, &g_FreeCamRot);
            }
        }
        if (g_bCustomFOV && oMKCamera_SetFOV) {
            oMKCamera_SetFOV(g_pMKCamera, g_FreeCamFOV, g_NativeCamFOVUnk);
        }
    }

    void UpdateInput(float deltaTime) {
        if (!g_FreeCamEnabled) return;

        ImGuiIO& io = ImGui::GetIO();

        // If user is currently typing in an ImGui text input, don't hijack keyboard
        if (io.WantTextInput) return;

        // Compute current speed with modifiers
        float currentSpeed = g_FreeCamMoveSpeed;
        if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
            currentSpeed *= g_FreeCamFastMultiplier;
        } else if ((GetAsyncKeyState(VK_MENU) & 0x8000) || (GetAsyncKeyState(VK_CONTROL) & 0x8000)) {
            currentSpeed *= g_FreeCamSlowMultiplier;
        }

        // Compute direction vectors
        FVector forward, right, up;
        GetDirectionVectors(g_FreeCamRot, forward, right, up);

        bool moved = false;

        // 1. Keyboard Navigation (WASD / Space / Q / E / C)
        // Forward / Backward
        if (GetAsyncKeyState('W') & 0x8000) {
            g_FreeCamPos += forward * currentSpeed;
            moved = true;
        }
        if (GetAsyncKeyState('S') & 0x8000) {
            g_FreeCamPos -= forward * currentSpeed;
            moved = true;
        }

        // Strafe Left / Right
        if (GetAsyncKeyState('A') & 0x8000) {
            g_FreeCamPos -= right * currentSpeed;
            moved = true;
        }
        if (GetAsyncKeyState('D') & 0x8000) {
            g_FreeCamPos += right * currentSpeed;
            moved = true;
        }

        // Ascend / Descend
        if ((GetAsyncKeyState(VK_SPACE) & 0x8000) || (GetAsyncKeyState('Q') & 0x8000)) {
            g_FreeCamPos += up * currentSpeed;
            moved = true;
        }
        if ((GetAsyncKeyState('E') & 0x8000) || (GetAsyncKeyState('C') & 0x8000)) {
            g_FreeCamPos -= up * currentSpeed;
            moved = true;
        }

        // Arrow Keys / Numpad alternative movement
        if (GetAsyncKeyState(VK_NUMPAD8) & 0x8000) { g_FreeCamPos += forward * currentSpeed; moved = true; }
        if (GetAsyncKeyState(VK_NUMPAD5) & 0x8000) { g_FreeCamPos -= forward * currentSpeed; moved = true; }
        if (GetAsyncKeyState(VK_NUMPAD4) & 0x8000) { g_FreeCamPos -= right * currentSpeed;   moved = true; }
        if (GetAsyncKeyState(VK_NUMPAD6) & 0x8000) { g_FreeCamPos += right * currentSpeed;   moved = true; }
        if (GetAsyncKeyState(VK_NUMPAD7) & 0x8000) { g_FreeCamPos += up * currentSpeed;      moved = true; }
        if (GetAsyncKeyState(VK_NUMPAD1) & 0x8000) { g_FreeCamPos -= up * currentSpeed;      moved = true; }

        // Keyboard Rotation (Numpad or J/L/I/K/U/O)
        if (GetAsyncKeyState(VK_LEFT) & 0x8000)  { g_FreeCamRot.Yaw -= g_FreeCamRotSpeed; moved = true; }
        if (GetAsyncKeyState(VK_RIGHT) & 0x8000) { g_FreeCamRot.Yaw += g_FreeCamRotSpeed; moved = true; }
        if (GetAsyncKeyState(VK_UP) & 0x8000)    { g_FreeCamRot.Pitch += g_FreeCamRotSpeed; moved = true; }
        if (GetAsyncKeyState(VK_DOWN) & 0x8000)  { g_FreeCamRot.Pitch -= g_FreeCamRotSpeed; moved = true; }

        // Roll (Tilt) keys (Z / X)
        if (GetAsyncKeyState('Z') & 0x8000) { g_FreeCamRot.Roll -= g_FreeCamRotSpeed; moved = true; }
        if (GetAsyncKeyState('X') & 0x8000) { g_FreeCamRot.Roll += g_FreeCamRotSpeed; moved = true; }

        // 2. Mouse Look (Pitch & Yaw)
        bool rmbDown = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
        if (g_bAlwaysLockMouse || (g_bHoldRMBToLook && rmbDown)) {
            float dx = io.MouseDelta.x;
            float dy = io.MouseDelta.y;

            if (fabs(dx) > 0.001f || fabs(dy) > 0.001f) {
                float yawMult = g_bInvertYaw ? -1.0f : 1.0f;
                float pitchMult = g_bInvertPitch ? 1.0f : -1.0f;

                g_FreeCamRot.Yaw += (int)(dx * g_MouseSensitivity * yawMult);
                g_FreeCamRot.Pitch += (int)(dy * g_MouseSensitivity * pitchMult);
                moved = true;
            }
        }

        // Mouse Wheel shortcuts:
        // Wheel + Ctrl = FOV zoom
        // Wheel + Shift = Speed scale
        if (io.MouseWheel != 0.0f) {
            if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {
                g_bCustomFOV = true;
                g_FreeCamFOV -= io.MouseWheel * 2.0f;
                if (g_FreeCamFOV < 15.0f) g_FreeCamFOV = 15.0f;
                if (g_FreeCamFOV > 150.0f) g_FreeCamFOV = 150.0f;
                moved = true;
            } else if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
                g_FreeCamMoveSpeed += io.MouseWheel * 2.0f;
                if (g_FreeCamMoveSpeed < 1.0f) g_FreeCamMoveSpeed = 1.0f;
                if (g_FreeCamMoveSpeed > 100.0f) g_FreeCamMoveSpeed = 100.0f;
            }
        }

        // 3. Gamepad Navigation (XInput controller 0)
        if (g_bGamepadEnabled) {
            typedef DWORD (WINAPI *tXInputGetState)(DWORD dwUserIndex, XINPUT_STATE* pState);
            static tXInputGetState fnXInputGetState = nullptr;
            if (!fnXInputGetState) {
                const char* dlls[] = { "xinput1_3.dll", "xinput1_4.dll", "xinput9_1_0.dll" };
                for (int i = 0; i < 3; ++i) {
                    HMODULE h = GetModuleHandleA(dlls[i]);
                    if (!h) h = LoadLibraryA(dlls[i]);
                    if (h) {
                        fnXInputGetState = (tXInputGetState)GetProcAddress(h, "XInputGetState");
                        if (!fnXInputGetState) fnXInputGetState = (tXInputGetState)GetProcAddress(h, (LPCSTR)3);
                        if (fnXInputGetState) break;
                    }
                }
            }

            XINPUT_STATE xstate;
            ZeroMemory(&xstate, sizeof(XINPUT_STATE));
            if (fnXInputGetState && fnXInputGetState(0, &xstate) == ERROR_SUCCESS) {
                // Left Stick: Movement (Forward/Back & Strafe)
                SHORT lx = xstate.Gamepad.sThumbLX;
                SHORT ly = xstate.Gamepad.sThumbLY;
                const SHORT deadzone = 7849;

                if (abs(lx) > deadzone || abs(ly) > deadzone) {
                    float normX = (abs(lx) > deadzone) ? ((float)lx / 32767.0f) : 0.0f;
                    float normY = (abs(ly) > deadzone) ? ((float)ly / 32767.0f) : 0.0f;

                    g_FreeCamPos += forward * (normY * currentSpeed);
                    g_FreeCamPos += right * (normX * currentSpeed);
                    moved = true;
                }

                // Right Stick: Look (Pitch & Yaw)
                SHORT rx = xstate.Gamepad.sThumbRX;
                SHORT ry = xstate.Gamepad.sThumbRY;
                const SHORT deadzoneR = 8689;

                if (abs(rx) > deadzoneR || abs(ry) > deadzoneR) {
                    float normRX = (abs(rx) > deadzoneR) ? ((float)rx / 32767.0f) : 0.0f;
                    float normRY = (abs(ry) > deadzoneR) ? ((float)ry / 32767.0f) : 0.0f;

                    float yawMult = g_bInvertYaw ? -1.0f : 1.0f;
                    float pitchMult = g_bInvertPitch ? 1.0f : -1.0f;

                    g_FreeCamRot.Yaw += (int)(normRX * (float)g_FreeCamRotSpeed * 0.08f * yawMult);
                    g_FreeCamRot.Pitch += (int)(normRY * (float)g_FreeCamRotSpeed * 0.08f * pitchMult);
                    moved = true;
                }

                // Triggers: Ascend (RT) & Descend (LT)
                if (xstate.Gamepad.bRightTrigger > 30) {
                    float t = (float)xstate.Gamepad.bRightTrigger / 255.0f;
                    g_FreeCamPos += up * (t * currentSpeed);
                    moved = true;
                }
                if (xstate.Gamepad.bLeftTrigger > 30) {
                    float t = (float)xstate.Gamepad.bLeftTrigger / 255.0f;
                    g_FreeCamPos -= up * (t * currentSpeed);
                    moved = true;
                }
            }
        }

        // Clamp Pitch to prevent camera flipping upside down (-16000 to 16000)
        if (g_FreeCamRot.Pitch > 16000) g_FreeCamRot.Pitch = 16000;
        if (g_FreeCamRot.Pitch < -16000) g_FreeCamRot.Pitch = -16000;

        // Normalize Yaw into 0 .. 65535
        g_FreeCamRot.Yaw = (int)((unsigned int)g_FreeCamRot.Yaw & 0xFFFF);

        // Always ensure camera updates in rendering loop
        if (moved || g_FreeCamPausedUpdate) {
            ForceCameraUpdate();
        }
    }

    // Camera Presets
    void ResetToGameCamera() {
        g_bCustomFOV = false;
        if (g_HasNativeCoords) {
            g_FreeCamPos = g_NativeCamPos;
            g_FreeCamRot = g_NativeCamRot;
            g_FreeCamFOV = g_NativeCamFOV;
        } else if (g_pMKCamera) {
            g_FreeCamPos = FVector(-550.0f, 0.0f, 160.0f);
            g_FreeCamRot = FRotator(0, 16384, 0);
            g_FreeCamFOV = 60.0f;
        }
        if (g_pMKCamera) {
            if (oMKCamera_SetPosition) oMKCamera_SetPosition(g_pMKCamera, &g_FreeCamPos);
            if (oMKCamera_SetRotation) oMKCamera_SetRotation(g_pMKCamera, &g_FreeCamRot);
            if (oMKCamera_SetFOV) oMKCamera_SetFOV(g_pMKCamera, g_FreeCamFOV, g_NativeCamFOVUnk);
        }
        LogTrace("[FREE CAMERA] Reset camera to default game vantage point (Pos: %.1f, %.1f, %.1f | FOV: %.1f).",
                 g_FreeCamPos.X, g_FreeCamPos.Y, g_FreeCamPos.Z, g_FreeCamFOV);
    }

    void PresetPlayer1CloseUp() {
        g_FreeCamPos = FVector(-350.0f, -120.0f, 130.0f);
        g_FreeCamRot = FRotator(-600, 2500, 0);
        g_bCustomFOV = true;
        g_FreeCamFOV = 55.0f;
        ForceCameraUpdate();
        LogTrace("[FREE CAMERA] Applied Preset: Player 1 Close-Up.");
    }

    void PresetPlayer2CloseUp() {
        g_FreeCamPos = FVector(-350.0f, 120.0f, 130.0f);
        g_FreeCamRot = FRotator(-600, 30200, 0);
        g_bCustomFOV = true;
        g_FreeCamFOV = 55.0f;
        ForceCameraUpdate();
        LogTrace("[FREE CAMERA] Applied Preset: Player 2 Close-Up.");
    }

    void PresetTopDown() {
        g_FreeCamPos = FVector(0.0f, 0.0f, 850.0f);
        g_FreeCamRot = FRotator(-16000, 16384, 0);
        g_bCustomFOV = true;
        g_FreeCamFOV = 70.0f;
        ForceCameraUpdate();
        LogTrace("[FREE CAMERA] Applied Preset: Top-Down Arena View.");
    }

    void PresetCinematicSide() {
        g_FreeCamPos = FVector(-600.0f, 0.0f, 160.0f);
        g_FreeCamRot = FRotator(-300, 16384, 0);
        g_bCustomFOV = true;
        g_FreeCamFOV = 65.0f;
        ForceCameraUpdate();
        LogTrace("[FREE CAMERA] Applied Preset: Cinematic Side Profile.");
    }

    void RenderUI() {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.4f, 0.9f, 1.0f, 1.0f), "Free Camera & Cinematic Controls (Hotkey: -):");
        ImGui::TextDisabled("Detaches camera from 2.5D fighting plane to fly freely in 3D space.");
        ImGui::Spacing();

        // Main Activation Toggle
        bool active = g_FreeCamEnabled;
        if (ImGui::Checkbox("Enable Free Camera Mode (Key: -)##FreeCamToggle", &active)) {
            SetEnabled(active);
        }
        ImGui::SameLine();
        ImGui::TextColored(g_FreeCamEnabled ? ImVec4(0.2f, 1.0f, 0.3f, 1.0f) : ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                           g_FreeCamEnabled ? "[CAMERA DETACHED / ACTIVE]" : "[LOCKED TO GAME]");

        ImGui::Spacing();

        if (ImGui::Button("Reset to Game View")) {
            ResetToGameCamera();
        }
        ImGui::SameLine();
        if (ImGui::Button("P1 Close-Up")) {
            PresetPlayer1CloseUp();
        }
        ImGui::SameLine();
        if (ImGui::Button("P2 Close-Up")) {
            PresetPlayer2CloseUp();
        }
        ImGui::SameLine();
        if (ImGui::Button("Top-Down View")) {
            PresetTopDown();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cinematic Profile")) {
            PresetCinematicSide();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Camera Coordinates & Sliders
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "Live Camera Telemetry & Manual Adjustments:");
        
        float posArr[3] = { g_FreeCamPos.X, g_FreeCamPos.Y, g_FreeCamPos.Z };
        if (ImGui::DragFloat3("Position (X | Y | Z)", posArr, 2.0f, -5000.0f, 5000.0f, "%.1f")) {
            g_FreeCamPos.X = posArr[0];
            g_FreeCamPos.Y = posArr[1];
            g_FreeCamPos.Z = posArr[2];
            ForceCameraUpdate();
        }

        int rotArr[3] = { g_FreeCamRot.Pitch, g_FreeCamRot.Yaw, g_FreeCamRot.Roll };
        if (ImGui::DragInt3("Rotation (Pitch | Yaw | Roll)", rotArr, 20.0f, -65536, 65536)) {
            g_FreeCamRot.Pitch = rotArr[0];
            g_FreeCamRot.Yaw = rotArr[1];
            g_FreeCamRot.Roll = rotArr[2];
            ForceCameraUpdate();
        }

        // Degrees Display helper
        float pitchDeg = (float)g_FreeCamRot.Pitch * (360.0f / 65536.0f);
        float yawDeg   = (float)g_FreeCamRot.Yaw   * (360.0f / 65536.0f);
        float rollDeg  = (float)g_FreeCamRot.Roll  * (360.0f / 65536.0f);
        ImGui::TextDisabled("   Angles: Pitch: %.1f deg | Yaw: %.1f deg | Roll: %.1f deg", pitchDeg, yawDeg, rollDeg);

        ImGui::Spacing();

        // Field of View
        ImGui::Checkbox("Override Field of View (FOV)##CustomFOV", &g_bCustomFOV);
        ImGui::SameLine();
        if (ImGui::SliderFloat("FOV##SliderFOV", &g_FreeCamFOV, 20.0f, 140.0f, "%.1f deg")) {
            g_bCustomFOV = true;
            ForceCameraUpdate();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Speed & Controls Configuration
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "Controls & Sensitivity Settings:");
        ImGui::SliderFloat("Fly Speed", &g_FreeCamMoveSpeed, 1.0f, 60.0f, "%.1f");
        ImGui::SliderFloat("Speed Boost (Shift)", &g_FreeCamFastMultiplier, 1.5f, 8.0f, "%.1fx");
        ImGui::SliderFloat("Slow Crawl (Alt/Ctrl)", &g_FreeCamSlowMultiplier, 0.05f, 0.5f, "%.2fx");
        ImGui::SliderFloat("Mouse Sensitivity", &g_MouseSensitivity, 2.0f, 50.0f, "%.1f");

        ImGui::Spacing();
        ImGui::Checkbox("Hold Right Mouse Button (RMB) to Look", &g_bHoldRMBToLook);
        ImGui::SameLine(320);
        ImGui::Checkbox("Always Lock Mouse", &g_bAlwaysLockMouse);

        ImGui::Checkbox("Invert Pitch (Up/Down)", &g_bInvertPitch);
        ImGui::SameLine(320);
        ImGui::Checkbox("Invert Yaw (Left/Right)", &g_bInvertYaw);

        ImGui::Checkbox("Enable Gamepad Flight (XInput Dual Sticks)", &g_bGamepadEnabled);
        ImGui::SameLine(320);
        ImGui::Checkbox("Update While Paused / Frozen", &g_FreeCamPausedUpdate);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextDisabled("Quick Flight Guide:");
        ImGui::TextDisabled(" * Move: W/A/S/D (Fly/Strafe) | Space / Q: Ascend | Ctrl / E / C: Descend");
        ImGui::TextDisabled(" * Look: Hold RMB & Move Mouse, or Arrow Keys / Numpad 4,8,6,2");
        ImGui::TextDisabled(" * Modifiers: Hold Shift (Speed Boost 3x) | Hold Alt (Precision Crawl 0.25x)");
        ImGui::TextDisabled(" * Shortcuts: Ctrl + Mouse Wheel (Live FOV Zoom) | Shift + Mouse Wheel (Fly Speed)");
        ImGui::TextDisabled(" * Gamepad: Left Stick (Move) | Right Stick (Look) | RT/LT (Ascend/Descend)");
    }

} // namespace FreeCamera
} // namespace MK9TradeFix
