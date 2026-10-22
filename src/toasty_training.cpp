#include "toasty_training.hpp"
#include "engine_common.hpp"
#include "combat_state.hpp"
#include "meter_drain_fix.hpp"
#include "minhook/include/MinHook.h"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <cmath>
#include <cstdlib>

namespace MK9TradeFix {
    namespace ToastyTraining {

        const ToastyBoostEntry g_ToastyBoostGuide[32] = {
            { "Baraka",        "U, U, D, 3", "Up, Up, Down + Front Kick" },
            { "CyberSub",      "D, D, D, 1", "Down, Down, Down + Front Punch" },
            { "Cyrax",         "U, U, D, 1", "Up, Up, Down + Front Punch" },
            { "Ermac",         "U, U, D, 2", "Up, Up, Down + Back Punch" },
            { "Freddy",        "D, B, D, 3", "Down, Back, Down + Front Kick" },
            { "Jade",          "D, B, D, 3", "Down, Back, Down + Front Kick" },
            { "Jax",           "D, D, D, 4", "Down, Down, Down + Back Kick" },
            { "Johnny Cage",   "D, D, U, 1", "Down, Down, Up + Front Punch" },
            { "Kabal",         "D, D, U, 2", "Down, Down, Up + Back Punch" },
            { "Kano",          "F, F, B, 3", "Forward, Forward, Back + Front Kick" },
            { "Kenshi",        "F, B, D, 3", "Forward, Back, Down + Front Kick" },
            { "Kitana",        "F, F, B, 4", "Forward, Forward, Back + Back Kick" },
            { "Kratos",        "B, B, D, 2", "Back, Back, Down + Back Punch" },
            { "Kung Lao",      "F, F, B, 1", "Forward, Forward, Back + Front Punch" },
            { "Liu Kang",      "F, F, B, 2", "Forward, Forward, Back + Back Punch" },
            { "Mileena",       "B, B, F, 3", "Back, Back, Forward + Front Kick" },
            { "Nightwolf",     "B, B, F, 4", "Back, Back, Forward + Back Kick" },
            { "Noob",          "B, B, F, 1", "Back, Back, Forward + Front Punch" },
            { "Quan Chi",      "B, B, F, 2", "Back, Back, Forward + Back Punch" },
            { "Raiden",        "D, B, D, 3", "Down, Back, Down + Front Kick" },
            { "Rain",          "B, B, D, 3", "Back, Back, Down + Front Kick" },
            { "Reptile",       "D, B, F, 3", "Down, Back, Forward + Front Kick" },
            { "Scorpion",      "D, B, F, 1", "Down, Back, Forward + Front Punch" },
            { "Sektor",        "D, B, D, 1", "Down, Back, Down + Front Punch" },
            { "Shang Tsung",   "D, D, F, 3", "Down, Down, Forward + Front Kick" },
            { "Sheeva",        "D, D, F, 4", "Down, Down, Forward + Back Kick" },
            { "Sindel",        "D, D, D, 1", "Down, Down, Down + Front Punch" },
            { "Skarlet",       "B, D, D, 3", "Back, Down, Down + Front Kick" },
            { "Smoke",         "D, D, F, 2", "Down, Down, Forward + Back Punch" },
            { "Sonya",         "B, B, D, 3", "Back, Back, Down + Front Kick" },
            { "Stryker",       "B, B, D, 4", "Back, Back, Down + Back Kick" },
            { "Sub Zero",      "B, B, D, 1", "Back, Back, Down + Front Punch" }
        };
        const size_t g_ToastyBoostGuideCount = 32;

        struct CharMatchRule {
            const char* displayName;
            const char* keywords[6];
        };

        static const CharMatchRule s_MatchRules[] = {
            { "Baraka",        { "baraka", nullptr } },
            { "CyberSub",      { "cybersub", "cyberzero", "cyber_sub", "cyber_zero", nullptr } },
            { "Cyrax",         { "cyrax", nullptr } },
            { "Ermac",         { "ermac", nullptr } },
            { "Freddy",        { "freddy", "krueger", nullptr } },
            { "Jade",          { "jade", nullptr } },
            { "Jax",           { "jax", nullptr } },
            { "Johnny Cage",   { "johnnycage", "johnny_cage", "johnny", "cage", nullptr } },
            { "Kabal",         { "kabal", nullptr } },
            { "Kano",          { "kano", nullptr } },
            { "Kenshi",        { "kenshi", nullptr } },
            { "Kitana",        { "kitana", nullptr } },
            { "Kratos",        { "kratos", nullptr } },
            { "Kung Lao",      { "kunglao", "kung_lao", "kung", nullptr } },
            { "Liu Kang",      { "liukang", "liu_kang", nullptr } },
            { "Mileena",       { "mileena", nullptr } },
            { "Nightwolf",     { "nightwolf", "night_wolf", nullptr } },
            { "Noob",          { "noob", "saibot", nullptr } },
            { "Quan Chi",      { "quanchi", "quan_chi", nullptr } },
            { "Raiden",        { "raiden", "rayden", nullptr } },
            { "Rain",          { "rain", nullptr } },
            { "Reptile",       { "reptile", nullptr } },
            { "Scorpion",      { "scorpion", nullptr } },
            { "Sektor",        { "sektor", nullptr } },
            { "Shang Tsung",   { "shangtsung", "shang_tsung", "shang", nullptr } },
            { "Sheeva",        { "sheeva", nullptr } },
            { "Sindel",        { "sindel", nullptr } },
            { "Skarlet",       { "skarlet", nullptr } },
            { "Smoke",         { "smoke", nullptr } },
            { "Sonya",         { "sonya", "blade", nullptr } },
            { "Stryker",       { "stryker", nullptr } },
            { "Sub Zero",      { "subzero", "sub_zero", nullptr } }
        };

        static bool MatchKeyword(const char* haystack, const char* needle) {
            if (!haystack || !needle || !*needle) return false;
            size_t nlen = strlen(needle);
            for (const char* h = haystack; *h; ++h) {
                bool match = true;
                for (size_t i = 0; i < nlen; ++i) {
                    if (tolower((unsigned char)h[i]) != tolower((unsigned char)needle[i])) {
                        match = false;
                        break;
                    }
                }
                if (match) return true;
            }
            return false;
        }

        const char* DetectCharacterName(void* pChar) {
            if (!pChar || (uintptr_t)pChar < 0x01000000) return nullptr;

            __try {
                uintptr_t pkg = *(uintptr_t*)((uintptr_t)pChar + 0x02A8);
                if (!pkg || pkg < 0x00400000) {
                    pkg = *(uintptr_t*)((uintptr_t)pChar + 0x7624);
                }
                if (!pkg || pkg < 0x00400000) {
                    pkg = *(uintptr_t*)((uintptr_t)pChar + 0x2F84);
                }

                if (pkg && pkg > 0x00400000) {
                    // 1. Check Function Table Symbols
                    uintptr_t pFnTable = *(uintptr_t*)(pkg + 0x8C);
                    uint32_t  fnCount  = *(uint32_t*)(pkg + 0x20);

                    if (pFnTable && fnCount > 0 && fnCount < 3000) {
                        uint32_t checkLimit = (fnCount < 20) ? fnCount : 20;
                        for (uint32_t i = 1; i <= checkLimit; ++i) {
                            uintptr_t pFn = *(uintptr_t*)(pFnTable - 4 + i * 4);
                            if (pFn && pFn > 0x00400000) {
                                const char* pSym = *(const char**)(pFn + 0x00);
                                if (pSym && (uintptr_t)pSym > 0x00400000) {
                                    for (const auto& rule : s_MatchRules) {
                                        for (int k = 0; rule.keywords[k] != nullptr; ++k) {
                                            if (MatchKeyword(pSym, rule.keywords[k])) {
                                                return rule.displayName;
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // 2. Check String Pool (First 2KB)
                    const char* pStrPool = *(const char**)(pkg + 0xA8);
                    if (pStrPool && (uintptr_t)pStrPool > 0x00400000) {
                        char poolSample[2048];
                        memcpy(poolSample, pStrPool, sizeof(poolSample) - 1);
                        poolSample[sizeof(poolSample) - 1] = '\0';
                        for (size_t s = 0; s < sizeof(poolSample); ++s) {
                            if (poolSample[s] == '\0') poolSample[s] = ' ';
                        }
                        for (const auto& rule : s_MatchRules) {
                            for (int k = 0; rule.keywords[k] != nullptr; ++k) {
                                if (MatchKeyword(poolSample, rule.keywords[k])) {
                                    return rule.displayName;
                                }
                            }
                        }
                    }
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {}

            return nullptr;
        }

        const ToastyBoostEntry* FindToastyBoostByName(const char* name) {
            if (!name || !*name) return nullptr;
            for (size_t i = 0; i < g_ToastyBoostGuideCount; ++i) {
                if (_stricmp(g_ToastyBoostGuide[i].name, name) == 0) {
                    return &g_ToastyBoostGuide[i];
                }
            }
            return nullptr;
        }

        const ToastyBoostEntry* GetCharacterToastyBoost(void* pChar) {
            const char* name = DetectCharacterName(pChar);
            if (!name) return nullptr;
            return FindToastyBoostByName(name);
        }

        static bool g_ToastyTrainingEnabled = false;

        float g_ToastyScale = 1.00f;
        int   g_ToastyCorner = TOASTY_CORNER_BOTTOM_RIGHT;
        bool  g_ToastyAutoFlip = false;
        bool  g_ToastyFlipTop = false;
        bool  g_ToastyRandomScale = false;

        static int   s_EffectiveToastyCorner = -1;
        static float s_EffectiveToastyScale  = 1.00f;
        static float s_CurrentAnimX = 1600.0f;
        static float s_CurrentAnimY = 900.0f;

        // -------------------------------------------------------------
        // Native Dan Forden 2D Canvas Tile Hooks
        // 0x008472D0: Sets Widget 1 (0x00E91CBC) PosX, PosY
        // 0x00847300: Sets Widget 2 (0x00E91CC0) PosX, PosY
        // 0x008433E0: Renders Widget using FCanvas::DrawTile (0x00758340)
        // -------------------------------------------------------------
        typedef void (__cdecl *tSetToastyPos)(float x, float y);
        static tSetToastyPos oSetToastyPos = nullptr;
        static tSetToastyPos oSetToastyPos2 = nullptr;

        typedef void (__thiscall *tRenderToastyWidget)(void* pWidget, void* pCanvas);
        static tRenderToastyWidget oRenderToastyWidget = nullptr;

        typedef void (__cdecl *tDrawTile)(
            void* pCanvas,
            float x,
            float y,
            float sizeX,
            float sizeY,
            float u,
            float v,
            float sizeU,
            float sizeV,
            void* pColor,
            void* pTexture,
            int alphaBlend,
            float depth
        );
        static tDrawTile fnDrawTile = (tDrawTile)0x00758340;

        typedef void* (__cdecl *tResolveTex)(void*);
        static tResolveTex fnResolveTex = (tResolveTex)0x00B8FFD0;

        static uint64_t s_LastToastyTriggerFrame = 0;

        bool IsExtraToastySettingsDisabled() {
            uint32_t resX = *(uint32_t*)0x00DE9B78;
            uint32_t resY = *(uint32_t*)0x00DE9B7C;
            if (resX == 0 || resY == 0) {
                resX = (uint32_t)*(float*)0x00DE992C;
                resY = (uint32_t)*(float*)0x00DE9930;
            }
            if (resX == 0 || resY == 0) {
                HWND hGame = *(HWND*)0x00E53BF4;
                if (hGame && IsWindow(hGame)) {
                    RECT rc = { 0 };
                    if (GetClientRect(hGame, &rc) && rc.right > 0 && rc.bottom > 0) {
                        resX = (uint32_t)rc.right;
                        resY = (uint32_t)rc.bottom;
                    }
                }
            }
            if (resX == 0 || resY == 0) {
                resX = 1280;
                resY = 720;
            }

            bool is720p = (resY <= 720 || resX <= 1280);
            float aspect = (resY > 0) ? ((float)resX / (float)resY) : 1.7777f;
            bool is4by3 = (aspect <= 1.40f);

            return is720p || is4by3;
        }

        static void ComputeToastyPlacement(float origX, float origY, float& outX, float& outY, float& outW, float& outH, bool& outFlipH, bool& outFlipV) {
            // If extra settings are disabled (720p or 4:3), enforce strict vanilla placement
            bool extraDisabled = IsExtraToastySettingsDisabled();

            // Normalized slide progress (0.0 = fully hidden, 1.0 = fully shown)
            // In vanilla Toasty.mko:
            // X animates between 1600.0f (hidden) and 1375.0f (shown)
            // Y animates between 900.0f (hidden) and 675.0f (shown)
            float progX = (1600.0f - origX) / 225.0f;
            float progY = (900.0f - origY) / 225.0f;
            if (progX < 0.0f) progX = 0.0f;
            if (progX > 1.0f) progX = 1.0f;
            if (progY < 0.0f) progY = 0.0f;
            if (progY > 1.0f) progY = 1.0f;

            if (extraDisabled) {
                s_EffectiveToastyCorner = TOASTY_CORNER_BOTTOM_RIGHT;
                s_EffectiveToastyScale = 1.00f;
            } else {
                if (s_EffectiveToastyCorner < 0 || s_EffectiveToastyCorner > 3) {
                    if (g_ToastyCorner == TOASTY_CORNER_RANDOM) {
                        s_EffectiveToastyCorner = rand() % 4;
                    } else {
                        s_EffectiveToastyCorner = g_ToastyCorner;
                    }
                }

                if (g_ToastyRandomScale) {
                    if (s_EffectiveToastyScale < 0.5f || s_EffectiveToastyScale > 4.5f) {
                        float r = (float)(rand() % 29) * 0.10f; // 0.00 to 2.80
                        s_EffectiveToastyScale = 1.00f + r;     // 1.00f to 3.80f
                    }
                } else {
                    s_EffectiveToastyScale = g_ToastyScale;
                }
            }

            float scale = extraDisabled ? 1.00f : (g_ToastyRandomScale ? s_EffectiveToastyScale : g_ToastyScale);
            if (scale < 0.5f) scale = 0.5f;
            if (scale > 4.0f) scale = 4.0f;

            // MK9 UI Canvas virtual coordinate space is always 1600x900 regardless of monitor or render target
            const float canvasW = 1600.0f;
            const float canvasH = 900.0f;

            outW = 230.0f * scale;
            outH = 230.0f * scale;

            bool isLeft = (s_EffectiveToastyCorner == TOASTY_CORNER_BOTTOM_LEFT || s_EffectiveToastyCorner == TOASTY_CORNER_TOP_LEFT);
            bool isTop  = (s_EffectiveToastyCorner == TOASTY_CORNER_TOP_RIGHT    || s_EffectiveToastyCorner == TOASTY_CORNER_TOP_LEFT);

            outFlipH = (!extraDisabled && isLeft && g_ToastyAutoFlip);
            outFlipV = (!extraDisabled && isTop  && g_ToastyFlipTop);

            if (isLeft) {
                outX = -outW + progX * outW;
            } else {
                outX = canvasW - progX * outW;
            }

            if (isTop) {
                outY = -outH + progY * outH;
            } else {
                outY = canvasH - progY * outH;
            }
        }

        static void __cdecl hkSetToastyPos(float x, float y) {
            s_CurrentAnimX = x;
            s_CurrentAnimY = y;

            if (IsExtraToastySettingsDisabled() || ((s_EffectiveToastyCorner == TOASTY_CORNER_BOTTOM_RIGHT || s_EffectiveToastyCorner < 0) && fabs(s_EffectiveToastyScale - 1.00f) < 0.01f)) {
                oSetToastyPos(x, y);
                return;
            }

            float newX = x;
            float newY = y;
            float newW = 230.0f;
            float newH = 230.0f;
            bool flipH = false;
            bool flipV = false;
            ComputeToastyPlacement(x, y, newX, newY, newW, newH, flipH, flipV);

            void* pWidget = *(void**)0x00E91CBC;
            if (pWidget && (uintptr_t)pWidget > 0x00400000) {
                __try {
                    *(float*)((uintptr_t)pWidget + 0x00) = newX;
                    *(float*)((uintptr_t)pWidget + 0x04) = newY;
                    *(float*)((uintptr_t)pWidget + 0x08) = 1.0f;
                    *(float*)((uintptr_t)pWidget + 0x0C) = 1.0f;
                    *(float*)((uintptr_t)pWidget + 0x10) = 0.0f;
                    *(float*)((uintptr_t)pWidget + 0x14) = newW;
                    *(float*)((uintptr_t)pWidget + 0x18) = newH;
                } __except (EXCEPTION_EXECUTE_HANDLER) {}
            }

            oSetToastyPos(newX, newY);
        }

        static void __cdecl hkSetToastyPos2(float x, float y) {
            s_CurrentAnimX = x;
            s_CurrentAnimY = y;

            if (IsExtraToastySettingsDisabled() || ((s_EffectiveToastyCorner == TOASTY_CORNER_BOTTOM_RIGHT || s_EffectiveToastyCorner < 0) && fabs(s_EffectiveToastyScale - 1.00f) < 0.01f)) {
                oSetToastyPos2(x, y);
                return;
            }

            float newX = x;
            float newY = y;
            float newW = 230.0f;
            float newH = 230.0f;
            bool flipH = false;
            bool flipV = false;
            ComputeToastyPlacement(x, y, newX, newY, newW, newH, flipH, flipV);

            void* pWidget = *(void**)0x00E91CC0;
            if (pWidget && (uintptr_t)pWidget > 0x00400000) {
                __try {
                    *(float*)((uintptr_t)pWidget + 0x00) = newX;
                    *(float*)((uintptr_t)pWidget + 0x04) = newY;
                    *(float*)((uintptr_t)pWidget + 0x08) = 1.0f;
                    *(float*)((uintptr_t)pWidget + 0x0C) = 1.0f;
                    *(float*)((uintptr_t)pWidget + 0x10) = 0.0f;
                    *(float*)((uintptr_t)pWidget + 0x14) = newW;
                    *(float*)((uintptr_t)pWidget + 0x18) = newH;
                } __except (EXCEPTION_EXECUTE_HANDLER) {}
            }

            oSetToastyPos2(newX, newY);
        }

        static void __fastcall hkRenderToastyWidget(void* pWidget, void* edx, void* pCanvas) {
            if (!pWidget || (uintptr_t)pWidget < 0x00400000 || !pCanvas) {
                if (oRenderToastyWidget) oRenderToastyWidget(pWidget, pCanvas);
                return;
            }

            // 1. If extra settings disabled (720p or 4:3) or authentic vanilla placement, use original engine renderer directly
            if (IsExtraToastySettingsDisabled() || ((s_EffectiveToastyCorner == TOASTY_CORNER_BOTTOM_RIGHT || s_EffectiveToastyCorner < 0) && fabs(s_EffectiveToastyScale - 1.00f) < 0.01f)) {
                if (oRenderToastyWidget) oRenderToastyWidget(pWidget, pCanvas);
                return;
            }

            // 2. Debounce and timeout guard: cameo animation lasts ~65 frames. If expired, do not draw and reset to hidden.
            uint64_t curFrame = g_CurrentFrame;
            if (s_LastToastyTriggerFrame > 0 && curFrame > (s_LastToastyTriggerFrame + 75)) {
                s_CurrentAnimX = 1600.0f;
                s_CurrentAnimY = 900.0f;
                return;
            }

            // 3. Progress check: if Dan Forden is offscreen, do not draw
            float progX = (1600.0f - s_CurrentAnimX) / 225.0f;
            float progY = (900.0f - s_CurrentAnimY) / 225.0f;
            if (progX <= 0.005f && progY <= 0.005f) {
                return;
            }

            __try {
                void* pTextureRaw = *(void**)((uintptr_t)pWidget + 0x1C);
                if (!pTextureRaw) {
                    return;
                }

                void* pTexture = fnResolveTex(pTextureRaw);
                if (!pTexture) {
                    return;
                }

                float posX = 0.0f;
                float posY = 0.0f;
                float width = 230.0f;
                float height = 230.0f;
                bool flipH = false;
                bool flipV = false;

                ComputeToastyPlacement(s_CurrentAnimX, s_CurrentAnimY, posX, posY, width, height, flipH, flipV);

                // Vanilla orientation (Bottom-Right) faces Left toward center: U = 1.0f, SizeU = -1.0f.
                // Left corners with Auto-Mirror face Right toward center: U = 0.0f, SizeU = 1.0f.
                float u = flipH ? 0.0f : 1.0f;
                float sizeU = flipH ? 1.0f : -1.0f;
                float v = flipV ? 1.0f : 0.0f;
                float sizeV = flipV ? -1.0f : 1.0f;

                fnDrawTile(
                    pCanvas,
                    posX,
                    posY,
                    width,
                    height,
                    u,
                    v,
                    sizeU,
                    sizeV,
                    (void*)0x00DEBF8C,
                    pTexture,
                    1,
                    1.0f
                );
                return;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                LogTrace("[!] Exception caught in hkRenderToastyWidget.");
            }

            if (oRenderToastyWidget) oRenderToastyWidget(pWidget, pCanvas);
        }


        void UnlockTrainingMode() {
            __try {
                DWORD oldProt = 0;
                
                // 1. Simulation tick caller for Toasty polling loop (0x0080F709: je -> 2x NOP)
                if (VirtualProtect((void*)0x0080F709, 2, PAGE_EXECUTE_READWRITE, &oldProt)) {
                    *(uint8_t*)0x0080F709 = 0x90;
                    *(uint8_t*)0x0080F70A = 0x90;
                    VirtualProtect((void*)0x0080F709, 2, oldProt, &oldProt);
                }

                // 2. FUN_008acd90: Remove Mode 0xB guard (0x008ACDB9: jne -> 6x NOP)
                if (VirtualProtect((void*)0x008ACDB9, 6, PAGE_EXECUTE_READWRITE, &oldProt)) {
                    memset((void*)0x008ACDB9, 0x90, 6);
                    VirtualProtect((void*)0x008ACDB9, 6, oldProt, &oldProt);
                }

                // 3. FUN_008acd90: Remove Practice Mode 0x400 guard (0x008ACDC9: jne -> 6x NOP)
                if (VirtualProtect((void*)0x008ACDC9, 6, PAGE_EXECUTE_READWRITE, &oldProt)) {
                    memset((void*)0x008ACDC9, 0x90, 6);
                    VirtualProtect((void*)0x008ACDC9, 6, oldProt, &oldProt);
                }

                // 4. FUN_008acd90: Remove Tag assist 0x10 guard (0x008ACDD6: jne -> 6x NOP)
                if (VirtualProtect((void*)0x008ACDD6, 6, PAGE_EXECUTE_READWRITE, &oldProt)) {
                    memset((void*)0x008ACDD6, 0x90, 6);
                    VirtualProtect((void*)0x008ACDD6, 6, oldProt, &oldProt);
                }

                LogTrace("[TOASTY] Successfully unlocked Training / Practice Mode cameo and reward polling!");
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                LogTrace("[!] Exception during Toasty Training Mode unlock patch.");
            }
        }

        void InstallHooks() {
            UnlockTrainingMode();

            if (MH_CreateHook((void*)0x008472D0, (void*)hkSetToastyPos, (void**)&oSetToastyPos) == MH_OK) {
                MH_EnableHook((void*)0x008472D0);
                LogTrace("[+] Dan Forden Set Pos Hook Installed (0x008472D0)");
            }
            if (MH_CreateHook((void*)0x00847300, (void*)hkSetToastyPos2, (void**)&oSetToastyPos2) == MH_OK) {
                MH_EnableHook((void*)0x00847300);
                LogTrace("[+] Dan Forden Set Pos 2 Hook Installed (0x00847300)");
            }
            if (MH_CreateHook((void*)0x008433E0, (void*)hkRenderToastyWidget, (void**)&oRenderToastyWidget) == MH_OK) {
                MH_EnableHook((void*)0x008433E0);
                LogTrace("[+] Dan Forden Render Widget Hook Installed (0x008433E0)");
            }
        }

        void SetEnabled(bool enabled) {
            g_ToastyTrainingEnabled = enabled;
            if (enabled) {
                UnlockTrainingMode();
            }
            LogTrace(">>> [TOASTY TRAINING] Subsystem: %s <<<", enabled ? "ENABLED" : "DISABLED");
        }

        bool IsEnabled() {
            return g_ToastyTrainingEnabled;
        }

        void Toggle() {
            SetEnabled(!g_ToastyTrainingEnabled);
        }

        typedef void (__thiscall *tToastyInit)(void* pThis);
        static tToastyInit fnToastyInit = (tToastyInit)0x00897830;

        typedef void (__cdecl *tTriggerToastyNative)();
        static tTriggerToastyNative fnTriggerToastyNative = (tTriggerToastyNative)0x008D6180;

        typedef void* (__thiscall *tGetToastyPkgContext)(void* pMgr);
        static tGetToastyPkgContext fnGetToastyPkgContext = (tGetToastyPkgContext)0x00407800;

        typedef int (__thiscall *tResolveMkoSymbol)(void* pPkg, const char* symName, int type);
        static tResolveMkoSymbol fnResolveMkoSymbol = (tResolveMkoSymbol)0x00458680;

        typedef void (__cdecl *tExecuteScriptThread)(
            uint32_t priority,
            uint32_t functionId,
            void* targetObject,
            void* packageContext,
            uint32_t numArgs,
            uint32_t arg1
        );
        static tExecuteScriptThread fnExecuteScriptThread = (tExecuteScriptThread)0x00492590;

        void TriggerToastyCameo(bool force) {
            TriggerToastyCameo(1, force);
        }

        void TriggerToastyCameo(int attPlr, bool force) {
            if (!force && !g_ToastyTrainingEnabled) return;

            __try {
                // 1. Ensure Training Mode patches are active
                UnlockTrainingMode();

                const uintptr_t kToastyMgr = 0x00E91BB8;
                void* pMgr = (void*)kToastyMgr;

                // 2. Debounce: if Dan Forden is actively animating (60 frame window), do not re-trigger overlapping threads
                uint64_t curFrame = g_CurrentFrame;
                if (!force && curFrame > 0 && s_LastToastyTriggerFrame > 0 && (curFrame - s_LastToastyTriggerFrame) < 60) {
                    return;
                }
                s_LastToastyTriggerFrame = curFrame;

                // 3. Ensure Toasty.mko package is preloaded into ToastyManager
                if (*(uintptr_t*)(kToastyMgr + 4) == 0 || *(uintptr_t*)(kToastyMgr + 8) == 0) {
                    if (fnToastyInit) {
                        fnToastyInit(pMgr);
                        LogTrace("[TOASTY] Preloaded MKScript/Toasty.mko via ToastyInit @ 0x00897830");
                    }
                }

                // 4. Set active expiration window for secret input meter reward (85 frames)
                uint32_t matchFrame = *(uint32_t*)0x00E89BE0;
                *(uint32_t*)(kToastyMgr + 0x0C) = matchFrame + 85;

                // 5. Reset per-cameo customization tracking
                if (IsExtraToastySettingsDisabled()) {
                    s_EffectiveToastyCorner = TOASTY_CORNER_BOTTOM_RIGHT;
                    s_EffectiveToastyScale = 1.00f;
                } else {
                    if (g_ToastyCorner == TOASTY_CORNER_RANDOM) {
                        s_EffectiveToastyCorner = rand() % 4;
                    } else {
                        s_EffectiveToastyCorner = g_ToastyCorner;
                    }

                    if (g_ToastyRandomScale) {
                        float r = (float)(rand() % 29) * 0.10f; // 0.00 to 2.80
                        s_EffectiveToastyScale = 1.00f + r;     // 1.00f to 3.80f
                    } else {
                        s_EffectiveToastyScale = g_ToastyScale;
                    }
                }

                s_CurrentAnimX = 1600.0f;
                s_CurrentAnimY = 900.0f;

                // 6. Execute Dan Forden visual slide script thread via 0x00492590 (with nullptr targetObject to prevent Actor vtable corruption)
                void* pPkg = nullptr;
                if (fnGetToastyPkgContext) {
                    pPkg = fnGetToastyPkgContext(pMgr);
                }
                if (!pPkg && *(uintptr_t*)(kToastyMgr + 4) != 0) {
                    pPkg = *(void**)(kToastyMgr + 4);
                }
                if (pPkg && fnExecuteScriptThread) {
                    int fnStart = 0;
                    if (fnResolveMkoSymbol) {
                        fnStart = fnResolveMkoSymbol(pPkg, "start_toasty", 1);
                    }
                    if (fnStart <= 0) fnStart = 2; // Index 2 in Toasty.mko is start_toasty

                    // Pass targetObject = nullptr so 0x00492590 skips Actor attachment (0x4466b0) on pMgr
                    fnExecuteScriptThread(0xFE, fnStart, nullptr, pPkg, 0, 0);
                    LogTrace("[TOASTY!] >>> Executed Dan Forden 'start_toasty' for P%d (fnStart: %d, pkg: %p, Corner: %d, Scale: %.2fx%s) <<<", 
                             attPlr, fnStart, pPkg, s_EffectiveToastyCorner, s_EffectiveToastyScale, g_ToastyRandomScale ? " [RANDOM]" : "");
                } else if (fnTriggerToastyNative) {
                    fnTriggerToastyNative();
                    LogTrace("[TOASTY!] >>> Executed Dan Forden via native 0x008D6180 for P%d (Corner: %d, Scale: %.2fx%s) <<<", 
                             attPlr, s_EffectiveToastyCorner, s_EffectiveToastyScale, g_ToastyRandomScale ? " [RANDOM]" : "");
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                LogTrace("[!] Exception caught during Toasty Cameo trigger.");
            }
        }

        void OnAttackHit(int attPlr, int defPlr, bool isUppercut, bool isBlocked, float damage, int rxID) {
            if (!g_ToastyTrainingEnabled) return;
            if (isBlocked) return;

            // STRICT D+2 ONLY POLICY:
            // Only trust the isUppercut flag from AnalyzeAttack(), which already verifies:
            //   1. ClassifySpecialOrSuper() returned false (NOT a special/EX/X-Ray)
            //   2. actionID == 0x20 OR D2 script/anim symbol match
            //   3. moveClass is NORMAL_LIMB or NORMAL_WEAPON (not special/EX)
            // DO NOT independently re-check IsFighterExecutingUppercut() here — that
            // bypasses the special move filter and causes false positives on moves like
            // Sektor's Telepunch, Cyrax's EX Bomb, and other specials with "uppercut"
            // in their animation symbols.
            if (!isUppercut) return;

            // Secondary safety net: verify the attacker is genuinely in a d+2 normal,
            // not a special move that was somehow misclassified upstream
            void* pAttacker = (attPlr == 1) ? GetP1Char() : GetP2Char();
            if (!pAttacker) {
                pAttacker = (attPlr == 1) ? *(void**)0x00E89D98 : *(void**)0x00E8AA24;
            }

            if (pAttacker && (uintptr_t)pAttacker > 0x01000000) {
                __try {
                    // Reject if the engine's special move flag (+0x2EC bit 0x200) is set
                    // and the normal attack flag (+0x2EC bit 0x400) is NOT set
                    uint32_t flags2EC = *(uint32_t*)((uintptr_t)pAttacker + 0x2EC);
                    bool isEngineSpecialBit = (flags2EC & 0x200) != 0;
                    bool isEngineNormalBit  = (flags2EC & 0x400) != 0;
                    if (isEngineSpecialBit && !isEngineNormalBit) {
                        LogTrace("[TOASTY TRAINING] P%d hit classified as uppercut but engine special flag is set — REJECTED (flags2EC: 0x%08X). Not a genuine D+2.",
                                 attPlr, flags2EC);
                        return;
                    }

                    // Reject if action ID is a known special move range (0x01-0x1F excluding 0x20)
                    int actionID = *(int*)((uintptr_t)pAttacker + 0x2E0);
                    if (actionID >= 0x01 && actionID <= 0x1F) {
                        LogTrace("[TOASTY TRAINING] P%d hit classified as uppercut but actionID is 0x%02X (special move range) — REJECTED. Not a genuine D+2.",
                                 attPlr, actionID);
                        return;
                    }
                } __except (EXCEPTION_EXECUTE_HANDLER) {}
            }

            LogTrace("[TOASTY TRAINING] P%d landed authentic Uppercut (D+2) on P%d! (rxID: %d, dmg: %.1f%%). Dispatching 100%% Toasty Cameo...",
                     attPlr, defPlr, rxID, damage * 100.0f);
            TriggerToastyCameo(attPlr, false);
        }

    }
}
