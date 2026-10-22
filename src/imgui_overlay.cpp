#include "imgui_overlay.hpp"
#include "trade_fix.hpp"
#include "crouch_buffer_fix.hpp"
#include "cancel_detector.hpp"
#include "meter_drain_fix.hpp"
#include "jump_input_fix.hpp"
#include "toasty_training.hpp"
#include "move_priority.hpp"
#include "display_window.hpp"
#include "profiler.hpp"
#include "input_recording.hpp"
#include "reversal_engine.hpp"
#include "input_isolation.hpp"
#include "free_camera.hpp"
#include "anim_viewer.hpp"
#include "extra_gameplay.hpp"
#include "stun_modifier.hpp"
#include "juggle_deterioration.hpp"
#include "load_profiler.hpp"
#include "attack_frame_modifier.hpp"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_dx9.h"
#include "imgui/imgui_impl_win32.h"
#include "minhook/include/MinHook.h"
#include <cstdio>
#include <deque>
#include <mutex>
#include <algorithm>
#include <cmath>
#include <unordered_map>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);



namespace MK9Overlay {

    static bool g_ConsoleOpen = true; // Open by default on startup
    static bool g_ImGuiInitialized = false;
    static HWND g_hTargetWindow = NULL;
    static WNDPROC oWndProc = NULL;
    static bool g_ResetFrameBarPos = false;

    typedef HRESULT (WINAPI *tEndScene)(IDirect3DDevice9* pDevice);
    static tEndScene oEndScene = nullptr;

    typedef HRESULT (WINAPI *tReset)(IDirect3DDevice9* pDevice, D3DPRESENT_PARAMETERS* pPresentationParameters);
    static tReset oReset = nullptr;

    typedef HRESULT (WINAPI *tDrawIndexedPrimitive)(IDirect3DDevice9* pDevice, D3DPRIMITIVETYPE Type, INT BaseVertexIndex, UINT MinVertexIndex, UINT NumVertices, UINT StartIndex, UINT PrimCount);
    static tDrawIndexedPrimitive oDrawIndexedPrimitive = nullptr;

    typedef HRESULT (WINAPI *tClear)(IDirect3DDevice9* pDevice, DWORD Count, const D3DRECT* pRects, DWORD Flags, D3DCOLOR Color, float Z, DWORD Stencil);
    static tClear oClear = nullptr;

    static bool g_IsInsideImGuiRender = false;

    struct CachedDecl {
        IDirect3DVertexDeclaration9* pDecl;
        bool isStage;
    };
    static CachedDecl s_CachedDecls[32] = {};
    static int s_NumCachedDecls = 0;
    static CRITICAL_SECTION s_DeclLock;
    static bool s_DeclLockInit = false;
    static int s_MatchActiveFrames = 0;

    static std::deque<ConsoleLogEntry> g_LogEntries;
    static std::mutex g_LogMutex;
    static const size_t MAX_LOG_LINES = 500;
    static bool g_AutoScroll = true;
    static float g_ConsoleOpacity = 0.78f;
    static ImGuiTextFilter g_LogFilter;

    static ImVec4 ClassifyLogColor(const char* text) {
        if (!text) return ImVec4(0.85f, 0.85f, 0.85f, 1.0f);
        if (strstr(text, "[CRASH]")) return ImVec4(1.0f, 0.25f, 0.25f, 1.0f);
        if (strstr(text, "[MUTUAL TRADE]") || strstr(text, "[TRADE]")) return ImVec4(0.2f, 0.85f, 1.0f, 1.0f);
        if (strstr(text, "[HIT]") || strstr(text, "[DAMAGE]") || strstr(text, "[MOVE EVALUATION]")) return ImVec4(1.0f, 0.6f, 0.2f, 1.0f);
        if (strstr(text, "[DISPLAY]")) return ImVec4(0.3f, 1.0f, 0.4f, 1.0f);
        if (strstr(text, "[TEST BOT]") || strstr(text, "[MACRO]")) return ImVec4(1.0f, 1.0f, 0.3f, 1.0f);
        if (strstr(text, "[DIAGNOSTIC]")) return ImVec4(0.9f, 0.45f, 1.0f, 1.0f);
        if (strstr(text, "[+]")) return ImVec4(0.4f, 1.0f, 0.6f, 1.0f);
        if (strstr(text, "===")) return ImVec4(0.5f, 0.5f, 0.6f, 1.0f);
        return ImVec4(0.88f, 0.88f, 0.90f, 1.0f);
    }

    void AppendLog(const char* text, uint64_t frame) {
        if (!text || text[0] == '\0') return;
        std::lock_guard<std::mutex> lock(g_LogMutex);
        ConsoleLogEntry entry;
        entry.text = text;
        entry.color = ClassifyLogColor(text);
        entry.frame = frame;
        g_LogEntries.push_back(entry);
        while (g_LogEntries.size() > MAX_LOG_LINES) {
            g_LogEntries.pop_front();
        }
    }

    void ToggleConsole() {
        g_ConsoleOpen = !g_ConsoleOpen;
        ShowCursor(g_ConsoleOpen ? TRUE : FALSE);
    }

    bool IsConsoleOpen() {
        return g_ConsoleOpen;
    }

    void SetConsoleOpen(bool open) {
        g_ConsoleOpen = open;
        ShowCursor(g_ConsoleOpen ? TRUE : FALSE);
    }

    static LRESULT CALLBACK hkWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        if (!g_ImGuiInitialized) {
            return CallWindowProcA(oWndProc, hWnd, uMsg, wParam, lParam);
        }

        // Toggle ImGui Console overlay with ~ (Tilde / VK_OEM_3)
        if (uMsg == WM_KEYDOWN) {
            if (wParam == VK_OEM_3) {
                ToggleConsole();
                return 1;
            }
            // Toggle Free Camera with '-' (VK_OEM_MINUS). VK_SUBTRACT (Numpad -) is reserved for P2 Select!
            if (wParam == VK_OEM_MINUS) {
                if (!ImGui::GetIO().WantTextInput) {
                    MK9TradeFix::FreeCamera::ToggleFreeCamera();
                    return 1;
                }
            }
        }

        bool isOverlayInteractive = g_ConsoleOpen || 
                                    MK9TradeFix::g_FrameDataVisualizerEnabled || 
                                    MK9TradeFix::InputRecordingManager::Get().IsHUDActive() ||
                                    MK9TradeFix::ReversalEngine::Get().IsIdleStanceHUDActive();

        // Always show arrow cursor when mouse is over interactive ImGui windows or console is open
        if (uMsg == WM_SETCURSOR && isOverlayInteractive) {
            SetCursor(LoadCursor(NULL, IDC_ARROW));
            return TRUE;
        }

        // Mouse messages: process with ImGui and consume if over an ImGui window
        if (uMsg >= WM_MOUSEFIRST && uMsg <= WM_MOUSELAST) {
            if (isOverlayInteractive) {
                ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam);
                ImGuiIO& io = ImGui::GetIO();
                if (io.WantCaptureMouse) {
                    return true;
                }
            }
            return CallWindowProcA(oWndProc, hWnd, uMsg, wParam, lParam);
        }

        // Keyboard messages: NEVER allow ImGui navigation to hijack game directional keys (Up/Down/Left/Right/Crouch)
        // Only pass keyboard events to ImGui if the console is open AND a text input box is actively focused
        if (uMsg >= WM_KEYFIRST && uMsg <= WM_KEYLAST) {
            if (g_ConsoleOpen) {
                ImGuiIO& io = ImGui::GetIO();
                if (io.WantTextInput) {
                    if (ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam)) {
                        return true;
                    }
                }
            }
            // Direct all in-game gameplay keys (including crouch, movement, attacks) straight to the game
            return CallWindowProcA(oWndProc, hWnd, uMsg, wParam, lParam);
        }

        if (isOverlayInteractive) {
            if (ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam)) {
                return true;
            }
        }

        return CallWindowProcA(oWndProc, hWnd, uMsg, wParam, lParam);
    }

    static void RenderConsoleLogsUI() {
        ImGui::Spacing();
        g_LogFilter.Draw("Filter Logs (Search)", 240.0f);
        ImGui::SameLine();
        if (ImGui::Button("Clear")) {
            std::lock_guard<std::mutex> lock(g_LogMutex);
            g_LogEntries.clear();
        }
        ImGui::SameLine();
        if (ImGui::Button("Copy All")) {
            std::lock_guard<std::mutex> lock(g_LogMutex);
            std::string allText;
            for (const auto& entry : g_LogEntries) {
                allText += entry.text;
                allText += "\n";
            }
            ImGui::SetClipboardText(allText.c_str());
        }
        ImGui::SameLine();
        ImGui::Checkbox("Auto-Scroll", &g_AutoScroll);
        ImGui::SameLine();
        ImGui::Checkbox("Enable Logging", &MK9TradeFix::g_LoggingEnabled);

        ImGui::Separator();

        const float footerHeightToReserve = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
        ImGui::BeginChild("ScrollingLogRegion", ImVec2(0, -footerHeightToReserve), false, ImGuiWindowFlags_HorizontalScrollbar);

        {
            std::lock_guard<std::mutex> lock(g_LogMutex);
            for (size_t i = 0; i < g_LogEntries.size(); ++i) {
                const auto& entry = g_LogEntries[i];
                if (!g_LogFilter.PassFilter(entry.text.c_str()))
                    continue;

                ImGui::PushStyleColor(ImGuiCol_Text, entry.color);
                ImGui::TextUnformatted(entry.text.c_str());
                ImGui::PopStyleColor();
            }

            if (g_AutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
                ImGui::SetScrollHereY(1.0f);
            }
        }
        ImGui::EndChild();

        ImGui::Separator();
        ImGui::TextDisabled("Hotkey: Press [~ / Tilde] to toggle console | [F9] Frame Data Bar | [F10] Display Mode");
    }

    static void RenderAttackFrameModifierControls(const char* idSuffix);

    static void RenderTestBotDiagnosticsUI() {
        const WORD P1_1 = 'T', P1_2 = 'U', P1_3 = 'G', P1_4 = 'J';
        const WORD P2_1 = VK_NUMPAD7, P2_2 = VK_NUMPAD9, P2_3 = VK_NUMPAD4, P2_4 = VK_NUMPAD6;
        const WORD P1_U = 'W', P1_D = 'S', P1_B = 'A', P1_F = 'D';
        const WORD P2_U = VK_UP, P2_D = VK_DOWN, P2_B = VK_RIGHT, P2_F = VK_LEFT;

        int curDiagMode = MK9TradeFix::GetDiagnosticMode();
        static int s_SelectedDiagMode = -1;
        static int s_LastSyncedDiagMode = -1;
        if (s_SelectedDiagMode == -1 || curDiagMode != s_LastSyncedDiagMode) {
            s_SelectedDiagMode = curDiagMode;
            s_LastSyncedDiagMode = curDiagMode;
        }

        const char* diagModeNames[] = {
            "Off (Diagnostic Hotkeys Disabled)",
            "Mode 1: Normal Timing (0f, +1f, +2f FP)",
            "Mode 2: Weapons (b+FP, b+BP, f+FP, f+BP)",
            "Mode 3: Projectiles 1 (D, B, 1..4 / D, F, 1..3 / D, U, 2)",
            "Mode 4: Projectiles 2 (B, F, 1..4 / B, B, 1..4)",
            "Mode 5: Projectiles 3 (F, F, 3..4 / D, F, B, 2 / D, B, F, 1..3 / B, B, F, 1, 4)",
            "Mode 6: Projectiles 4 (U, D, 1..4 / D, U, 1, 3, 4)",
            "Mode 7: Scorpion vs Sub-Zero (Specials vs Normals / Jump-In vs D+2)",
            "Mode 8: Release Check & Buffer (Negative Edge vs Downstroke / Hit-Confirm)",
            "Mode 9: Dash & Projectiles (FP Recovery, Dash, Freeze, JC D+FP Buffers)",
            "Mode 10: EX Forceball -> F+3 vs P1 EX (Reversal Startup / Armor Priority)",
            "Mode 11: EX Forceball -> F+3 vs P1 D,F+1+BL (Unarmored EX Reversal)",
            "Mode 12: Multi-Device Input Bleed & Mashing Isolation (Stress Test)",
            "Mode 13: Quick Roll (Tech Roll Window Identification)",
            "Mode 14: Delayed Wakeup (Floor 0x460D Buffer Window Prober)",
            "Mode 15: Blockstun Diagnostic (Priority Pinpoint F1 - F4)",
            "Mode 16: Blockstun Distance & Pushback Measurement (F1 - F4)",
            "Mode 17: Attack Frame Data Measurement (F1: FP, F2: BP, F3: FK, F4: BK)",
            "Mode 18: Normal Cancel Diagnostic (F1: Spear, F2: EX Spear, F3: Dash Fwd, F4: Backdash, F5: X-Ray)",
            "Mode 19: Kabal Bug Diagnostic (F1: F+4 -> F+4 Infinite Loop, F2: 1,1,1 -> NDC -> 1 Glitch Link)",
            "Mode 20: Crouching Normal Diagnostics (F1: D+1, F2: D+2 Uppercut, F3: D+3, F4: D+4)"
        };

        ImGui::Spacing();
        ImGui::Text("Active Diagnostic Mode (Shift+F9 or Dropdown):");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(480.0f);
        if (ImGui::Combo("##DiagModeDropdown", &s_SelectedDiagMode, diagModeNames, IM_ARRAYSIZE(diagModeNames))) {
            MK9TradeFix::SetDiagnosticMode(s_SelectedDiagMode);
            s_LastSyncedDiagMode = s_SelectedDiagMode;
        }
        ImGui::SameLine();
        if (s_SelectedDiagMode > 0) {
            if (ImGui::Button("Turn Off##DiagModeOff")) {
                s_SelectedDiagMode = 0;
                MK9TradeFix::SetDiagnosticMode(0);
                s_LastSyncedDiagMode = 0;
            }
        }

        ImGui::Separator();
        ImGui::Spacing();

        switch (s_SelectedDiagMode) {
            case 0: {
                ImGui::TextDisabled("Diagnostic hotkeys are currently Off. Select a Mode above or press Shift+F9 in-game to enable.");
                break;
            }

            case 1: {
                if (ImGui::Button("Simultaneous FP (0f)")) {
                    MK9TradeFix::TriggerMacroBot(0, 'T', 0, 0, VK_NUMPAD7, 0, "Simultaneous FP (P1: 0f, P2: 0f)");
                }
                ImGui::SameLine();
                if (ImGui::Button("P1 Delayed (+1f)")) {
                    MK9TradeFix::TriggerMacroBot(0, 'T', 1, 0, VK_NUMPAD7, 0, "P1 Delayed FP (+1f P1)");
                }
                ImGui::SameLine();
                if (ImGui::Button("P2 Delayed (+1f)")) {
                    MK9TradeFix::TriggerMacroBot(0, 'T', 0, 0, VK_NUMPAD7, 1, "P2 Delayed FP (+1f P2)");
                }
                ImGui::SameLine();
                if (ImGui::Button("P1 Delayed (+2f)")) {
                    MK9TradeFix::TriggerMacroBot(0, 'T', 2, 0, VK_NUMPAD7, 0, "P1 Delayed FP (+2f P1)");
                }
                ImGui::SameLine();
                if (ImGui::Button("P2 Delayed (+2f)")) {
                    MK9TradeFix::TriggerMacroBot(0, 'T', 0, 0, VK_NUMPAD7, 2, "P2 Delayed FP (+2f P2)");
                }

                if (ImGui::Button("P1 FP vs P2 Walk Left")) {
                    MK9TradeFix::TriggerSequenceMacro(MK9TradeFix::MACRO_SEQ_P2_WALK_THEN_ATTACK, "P1 FP vs P2 Left(15f)+Wait(1f)+Num7");
                }
                ImGui::SameLine();
                if (ImGui::Button("P2 FP vs P1 Walk D")) {
                    MK9TradeFix::TriggerSequenceMacro(MK9TradeFix::MACRO_SEQ_P1_WALK_THEN_ATTACK, "P2 FP vs P1 D(15f)+Wait(1f)+T");
                }
                ImGui::SameLine();
                if (ImGui::Button("Simultaneous D+FK")) {
                    MK9TradeFix::TriggerMacroBot('S', 'G', 0, VK_DOWN, VK_NUMPAD4, 0, "Simultaneous D+FK (P1: S+G vs P2: Down+Num4)");
                }
                break;
            }

            case 2: {
                if (ImGui::Button("Simultaneous b+FP")) {
                    MK9TradeFix::TriggerMacroBot('A', 'T', 0, VK_RIGHT, VK_NUMPAD7, 0, "Simultaneous b+FP (P1: A+T, P2: Right+Num7)");
                }
                ImGui::SameLine();
                if (ImGui::Button("Simultaneous b+BP")) {
                    MK9TradeFix::TriggerMacroBot('A', 'U', 0, VK_RIGHT, VK_NUMPAD9, 0, "Simultaneous b+BP (P1: A+U, P2: Right+Num9)");
                }
                ImGui::SameLine();
                if (ImGui::Button("P1 b+BP vs P2 +1f FP")) {
                    MK9TradeFix::TriggerMacroBot('A', 'U', 0, 0, VK_NUMPAD7, 1, "P1 b+BP (A+U) vs P2 Delayed FP (+1f Num7)");
                }
                ImGui::SameLine();
                if (ImGui::Button("P1 b+FP vs P2 +1f FP")) {
                    MK9TradeFix::TriggerMacroBot('A', 'T', 0, 0, VK_NUMPAD7, 1, "P1 b+FP (A+T) vs P2 Delayed FP (+1f Num7)");
                }

                if (ImGui::Button("P1 b+BP vs P2 +2f FP")) {
                    MK9TradeFix::TriggerMacroBot('A', 'U', 0, 0, VK_NUMPAD7, 2, "P1 b+BP (A+U) vs P2 Delayed FP (+2f Num7)");
                }
                ImGui::SameLine();
                if (ImGui::Button("P1 b+FP vs P2 +2f FP")) {
                    MK9TradeFix::TriggerMacroBot('A', 'T', 0, 0, VK_NUMPAD7, 2, "P1 b+FP (A+T) vs P2 Delayed FP (+2f Num7)");
                }
                ImGui::SameLine();
                if (ImGui::Button("P1 f+FP vs P2 BP")) {
                    MK9TradeFix::TriggerMacroBot('D', 'T', 0, 0, VK_NUMPAD9, 0, "P1 f+FP (D+T) vs P2 BP (Num9) Simultaneous");
                }
                ImGui::SameLine();
                if (ImGui::Button("P1 f+BP vs P2 FP")) {
                    MK9TradeFix::TriggerMacroBot('D', 'U', 0, 0, VK_NUMPAD7, 0, "P1 f+BP (D+U) vs P2 FP (Num7) Simultaneous");
                }
                break;
            }

            case 3: {
                if (ImGui::Button("D, B, 1")) {
                    MK9TradeFix::Trigger2StepMacro(P1_D, P1_B, P1_1, P2_D, P2_B, P2_1, "Both do D, B, 1 (P1: S, A+T vs P2: Down, Right+Num7)");
                }
                ImGui::SameLine();
                if (ImGui::Button("D, B, 2")) {
                    MK9TradeFix::Trigger2StepMacro(P1_D, P1_B, P1_2, P2_D, P2_B, P2_2, "Both do D, B, 2 (P1: S, A+U vs P2: Down, Right+Num9)");
                }
                ImGui::SameLine();
                if (ImGui::Button("D, B, 3")) {
                    MK9TradeFix::Trigger2StepMacro(P1_D, P1_B, P1_3, P2_D, P2_B, P2_3, "Both do D, B, 3 (P1: S, A+G vs P2: Down, Right+Num4)");
                }
                ImGui::SameLine();
                if (ImGui::Button("D, B, 4")) {
                    MK9TradeFix::Trigger2StepMacro(P1_D, P1_B, P1_4, P2_D, P2_B, P2_4, "Both do D, B, 4 (P1: S, A+J vs P2: Down, Right+Num6)");
                }

                if (ImGui::Button("D, F, 1")) {
                    MK9TradeFix::Trigger2StepMacro(P1_D, P1_F, P1_1, P2_D, P2_F, P2_1, "Both do D, F, 1 (P1: S, D+T vs P2: Down, Left+Num7)");
                }
                ImGui::SameLine();
                if (ImGui::Button("D, F, 2")) {
                    MK9TradeFix::Trigger2StepMacro(P1_D, P1_F, P1_2, P2_D, P2_F, P2_2, "Both do D, F, 2 (P1: S, D+U vs P2: Down, Left+Num9)");
                }
                ImGui::SameLine();
                if (ImGui::Button("D, F, 3")) {
                    MK9TradeFix::Trigger2StepMacro(P1_D, P1_F, P1_3, P2_D, P2_F, P2_3, "Both do D, F, 3 (P1: S, D+G vs P2: Down, Left+Num4)");
                }
                ImGui::SameLine();
                if (ImGui::Button("D, U, 2")) {
                    MK9TradeFix::Trigger2StepMacro(P1_D, P1_U, P1_2, P2_D, P2_U, P2_2, "Both do D, U, 2 (P1: S, W+U vs P2: Down, Up+Num9)");
                }
                break;
            }

            case 4: {
                if (ImGui::Button("B, F, 1")) {
                    MK9TradeFix::Trigger2StepMacro(P1_B, P1_F, P1_1, P2_B, P2_F, P2_1, "Both do B, F, 1 (P1: A, D+T vs P2: Right, Left+Num7)");
                }
                ImGui::SameLine();
                if (ImGui::Button("B, F, 2")) {
                    MK9TradeFix::Trigger2StepMacro(P1_B, P1_F, P1_2, P2_B, P2_F, P2_2, "Both do B, F, 2 (P1: A, D+U vs P2: Right, Left+Num9)");
                }
                ImGui::SameLine();
                if (ImGui::Button("B, F, 3")) {
                    MK9TradeFix::Trigger2StepMacro(P1_B, P1_F, P1_3, P2_B, P2_F, P2_3, "Both do B, F, 3 (P1: A, D+G vs P2: Right, Left+Num4)");
                }
                ImGui::SameLine();
                if (ImGui::Button("B, F, 4")) {
                    MK9TradeFix::Trigger2StepMacro(P1_B, P1_F, P1_4, P2_B, P2_F, P2_4, "Both do B, F, 4 (P1: A, D+J vs P2: Right, Left+Num6)");
                }

                if (ImGui::Button("B, B, 1")) {
                    MK9TradeFix::Trigger2StepMacro(P1_B, P1_B, P1_1, P2_B, P2_B, P2_1, "Both do B, B, 1 (P1: A, A+T vs P2: Right, Right+Num7)");
                }
                ImGui::SameLine();
                if (ImGui::Button("B, B, 2")) {
                    MK9TradeFix::Trigger2StepMacro(P1_B, P1_B, P1_2, P2_B, P2_B, P2_2, "Both do B, B, 2 (P1: A, A+U vs P2: Right, Right+Num9)");
                }
                ImGui::SameLine();
                if (ImGui::Button("B, B, 3")) {
                    MK9TradeFix::Trigger2StepMacro(P1_B, P1_B, P1_3, P2_B, P2_B, P2_3, "Both do B, B, 3 (P1: A, A+G vs P2: Right, Right+Num4)");
                }
                ImGui::SameLine();
                if (ImGui::Button("B, B, 4")) {
                    MK9TradeFix::Trigger2StepMacro(P1_B, P1_B, P1_4, P2_B, P2_B, P2_4, "Both do B, B, 4 (P1: A, A+J vs P2: Right, Right+Num6)");
                }
                break;
            }

            case 5: {
                if (ImGui::Button("F, F, 3")) {
                    MK9TradeFix::Trigger2StepMacro(P1_F, P1_F, P1_3, P2_F, P2_F, P2_3, "Both do F, F, 3 (P1: D, D+G vs P2: Left, Left+Num4)");
                }
                ImGui::SameLine();
                if (ImGui::Button("F, F, 4")) {
                    MK9TradeFix::Trigger2StepMacro(P1_F, P1_F, P1_4, P2_F, P2_F, P2_4, "Both do F, F, 4 (P1: D, D+J vs P2: Left, Left+Num6)");
                }
                ImGui::SameLine();
                if (ImGui::Button("D, F, B, 2")) {
                    MK9TradeFix::Trigger3StepMacro(P1_D, P1_F, P1_B, P1_2, P2_D, P2_F, P2_B, P2_2, "Both do D, F, B, 2 (P1: S, D, A+U vs P2: Down, Left, Right+Num9)");
                }
                ImGui::SameLine();
                if (ImGui::Button("D, B, F, 1")) {
                    MK9TradeFix::Trigger3StepMacro(P1_D, P1_B, P1_F, P1_1, P2_D, P2_B, P2_F, P2_1, "Both do D, B, F, 1 (P1: S, A, D+T vs P2: Down, Right, Left+Num7)");
                }

                if (ImGui::Button("D, B, F, 2")) {
                    MK9TradeFix::Trigger3StepMacro(P1_D, P1_B, P1_F, P1_2, P2_D, P2_B, P2_F, P2_2, "Both do D, B, F, 2 (P1: S, A, D+U vs P2: Down, Right, Left+Num9)");
                }
                ImGui::SameLine();
                if (ImGui::Button("D, B, F, 3")) {
                    MK9TradeFix::Trigger3StepMacro(P1_D, P1_B, P1_F, P1_3, P2_D, P2_B, P2_F, P2_3, "Both do D, B, F, 3 (P1: S, A, D+G vs P2: Down, Right, Left+Num4)");
                }
                ImGui::SameLine();
                if (ImGui::Button("B, B, F, 1")) {
                    MK9TradeFix::Trigger3StepMacro(P1_B, P1_B, P1_F, P1_1, P2_B, P2_B, P2_F, P2_1, "Both do B, B, F, 1 (P1: A, A, D+T vs P2: Right, Right, Left+Num7)");
                }
                ImGui::SameLine();
                if (ImGui::Button("B, B, F, 4")) {
                    MK9TradeFix::Trigger3StepMacro(P1_B, P1_B, P1_F, P1_4, P2_B, P2_B, P2_F, P2_4, "Both do B, B, F, 4 (P1: A, A, D+J vs P2: Right, Right, Left+Num6)");
                }
                break;
            }

            case 6: {
                if (ImGui::Button("U, D, 1")) {
                    MK9TradeFix::Trigger2StepMacro(P1_U, P1_D, P1_1, P2_U, P2_D, P2_1, "Both do U, D, 1 (P1: W, S+T vs P2: Up, Down+Num7)");
                }
                ImGui::SameLine();
                if (ImGui::Button("U, D, 2")) {
                    MK9TradeFix::Trigger2StepMacro(P1_U, P1_D, P1_2, P2_U, P2_D, P2_2, "Both do U, D, 2 (P1: W, S+U vs P2: Up, Down+Num9)");
                }
                ImGui::SameLine();
                if (ImGui::Button("U, D, 3")) {
                    MK9TradeFix::Trigger2StepMacro(P1_U, P1_D, P1_3, P2_U, P2_D, P2_3, "Both do U, D, 3 (P1: W, S+G vs P2: Up, Down+Num4)");
                }
                ImGui::SameLine();
                if (ImGui::Button("U, D, 4")) {
                    MK9TradeFix::Trigger2StepMacro(P1_U, P1_D, P1_4, P2_U, P2_D, P2_4, "Both do U, D, 4 (P1: W, S+J vs P2: Up, Down+Num6)");
                }

                if (ImGui::Button("D, U, 1")) {
                    MK9TradeFix::Trigger2StepMacro(P1_D, P1_U, P1_1, P2_D, P2_U, P2_1, "Both do D, U, 1 (P1: S, W+T vs P2: Down, Up+Num7)");
                }
                ImGui::SameLine();
                if (ImGui::Button("D, U, 3")) {
                    MK9TradeFix::Trigger2StepMacro(P1_D, P1_U, P1_3, P2_D, P2_U, P2_3, "Both do D, U, 3 (P1: S, W+G vs P2: Down, Up+Num4)");
                }
                ImGui::SameLine();
                if (ImGui::Button("D, U, 4")) {
                    MK9TradeFix::Trigger2StepMacro(P1_D, P1_U, P1_4, P2_D, P2_U, P2_4, "Both do D, U, 4 (P1: S, W+J vs P2: Down, Up+Num6)");
                }
                break;
            }

            case 7: {
                if (ImGui::Button("F1: P2 D,F+3 vs P1 D+1 (Startup Int)")) {
                    MK9TradeFix::TriggerP2SpecVsP1Norm(P2_D, P2_F, P2_3, P1_D, P1_1, 0, "P2 Sub-Zero D,F+3 vs P1 D+1 on STARTUP (Scorpion Interrupts)");
                }
                ImGui::SameLine();
                if (ImGui::Button("F2: P2 D,F+3 vs P1 D+1 (Active Clash)")) {
                    MK9TradeFix::TriggerP2SpecVsP1Norm(P2_D, P2_F, P2_3, P1_D, P1_1, 5, "P2 Sub-Zero D,F+3 vs P1 D+1 on ACTIVE (Sub-Zero Wins)");
                }

                if (ImGui::Button("F3: P1 D,B+4 vs P2 D+1 (Startup Int)")) {
                    MK9TradeFix::TriggerP1SpecVsP2Norm(P1_D, P1_B, P1_4, P2_D, P2_1, 0, "P1 Scorpion D,B+4 vs P2 D+1 on STARTUP (Sub-Zero Interrupts)");
                }
                ImGui::SameLine();
                if (ImGui::Button("F4: P1 D,B+4 vs P2 D+1 (Active Clash)")) {
                    MK9TradeFix::TriggerP1SpecVsP2Norm(P1_D, P1_B, P1_4, P2_D, P2_1, 5, "P1 Scorpion D,B+4 vs P2 D+1 on ACTIVE (Scorpion Wins)");
                }

                if (ImGui::Button("F5: P2 D,F+3 vs P1 D+2 Uppercut")) {
                    MK9TradeFix::TriggerP2SpecVsP1Norm(P2_D, P2_F, P2_3, P1_D, P1_2, 5, "P2 Sub-Zero D,F+3 vs P1 D+2 Uppercut (D+2 Uppercut Wins)");
                }
                ImGui::SameLine();
                if (ImGui::Button("F6: Jump-In vs D+2 Anti-Air")) {
                    MK9TradeFix::TriggerJumpInVsGround(true, "P1 Jump-In vs P2 D+2 Anti-Air Uppercut (D+2 Wins)");
                }

                if (ImGui::Button("F7: Jump-In vs Standing 1 Ground")) {
                    MK9TradeFix::TriggerJumpInVsGround(false, "P1 Jump-In vs P2 Standing Normal 1 (Jump-In Wins)");
                }
                ImGui::SameLine();
                if (ImGui::Button("F8: D+2 vs D+2 (Later Inputted Wins)")) {
                    MK9TradeFix::TriggerMacroBot(P1_D, P1_2, 0, P2_D, P2_2, 0, "Both do D+2 Uppercut (Later Inputted D+2 Wins by Default)");
                }
                break;
            }

            case 8: {
                if (ImGui::Button("F1: P1 Raw Neg Edge Spear (B,B+Rel 1)")) {
                    MK9TradeFix::TriggerMode8F1();
                }
                ImGui::SameLine();
                if (ImGui::Button("F2: P1 Baseline Normal Special (B,B+Press 1)")) {
                    MK9TradeFix::TriggerMode8F2();
                }

                if (ImGui::Button("F3: P1 Close Jab -> Hit-Confirm Cancel")) {
                    MK9TradeFix::TriggerMode8F3();
                }
                ImGui::SameLine();
                if (ImGui::Button("F4: P1 Whiff Jab -> Neg Edge (Whiff Check)")) {
                    MK9TradeFix::TriggerMode8F4();
                }

                if (ImGui::Button("F5: P2 Raw Neg Edge Spear (Mirrored)")) {
                    MK9TradeFix::TriggerMode8F5();
                }
                ImGui::SameLine();
                if (ImGui::Button("F6: P2 Baseline Normal Special (Mirrored)")) {
                    MK9TradeFix::TriggerMode8F6();
                }

                if (ImGui::Button("F7: P2 Close Jab -> Hit-Confirm Cancel (Mirrored)")) {
                    MK9TradeFix::TriggerMode8F7();
                }
                ImGui::SameLine();
                if (ImGui::Button("F8: P1 1, 1 String -> Backdash Buffer Check")) {
                    MK9TradeFix::TriggerMode8F8();
                }

                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.4f, 0.9f, 1.0f, 1.0f), "Mode 8 Sequence Specifications (Native Injustice Release Check Architecture):");
                ImGui::BulletText("F1: P1 Raw Neg Edge | Hold [1] -> Tap [Back] -> Neutral -> Hold [Back] -> Release [1] (Spear if NegEdge ON, Suppressed if OFF)");
                ImGui::BulletText("F2: P1 Normal Special | Tap [Back] -> Neutral -> Hold [Back] -> Press [1] Downstroke (Spear ALWAYS on both ON and OFF)");
                ImGui::BulletText("F3: P1 Hit-Confirm Cancel | Auto close-in -> Jab connects -> Release [1] in Hitstop (Cancels to Spear if ON, Normal if OFF)");
                ImGui::BulletText("F4: P1 Whiff Neg Edge | Distance -> Jab whiffs -> Release [1] (Engine 0x8F1B20 prevents cancel on BOTH ON and OFF)");
                ImGui::BulletText("F5: P2 Raw Neg Edge | Mirrored P2 Raw Negative Edge Spear");
                ImGui::BulletText("F6: P2 Normal Special | Mirrored P2 Baseline Normal Special");
                ImGui::BulletText("F7: P2 Hit-Confirm Cancel | Mirrored P2 Hit-Confirmed Cancel");
                ImGui::BulletText("F8: P1 String Buffer | P1 1, 1 String followed by Backdash buffer (Rapid tap/release test)");
                break;
            }

            case 9: {
                if (ImGui::Button("F1: P1 FP Baseline (Screenshot 1)")) {
                    MK9TradeFix::TriggerMode9F1();
                }
                ImGui::SameLine();
                if (ImGui::Button("F2: P1 FP -> Immediate Dash (Screenshot 2)")) {
                    MK9TradeFix::TriggerMode9F2();
                }
                ImGui::SameLine();
                if (ImGui::Button("F3: P1 D, B + 3 Normal Projectile")) {
                    MK9TradeFix::TriggerMode9F3();
                }
                ImGui::SameLine();
                if (ImGui::Button("F4: P1 D, B + 3 + Block EX Projectile")) {
                    MK9TradeFix::TriggerMode9F4();
                }
                
                ImGui::Spacing();
                if (ImGui::Button("F5: JC D+FP (Frame 56)")) {
                    MK9TradeFix::TriggerMode9F5();
                }
                ImGui::SameLine();
                if (ImGui::Button("F6: JC D+FP (Frame 58)")) {
                    MK9TradeFix::TriggerMode9F6();
                }
                ImGui::SameLine();
                if (ImGui::Button("F7: JC D+FP (Frame 59)")) {
                    MK9TradeFix::TriggerMode9F7();
                }
                ImGui::SameLine();
                if (ImGui::Button("F8: JC D+FP (Frame 60)")) {
                    MK9TradeFix::TriggerMode9F8();
                }

                ImGui::Spacing();
                bool crouchFix = MK9TradeFix::CrouchBufferFix::IsEnabled();
                if (ImGui::Checkbox("Enable Crouch Normal Buffer Timeout Fix (Native Down Token 0x80000004)", &crouchFix)) {
                    MK9TradeFix::CrouchBufferFix::SetEnabled(crouchFix);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", "Prevents NetherRealm's 8-frame directional buffer expiration from forcing standing normals while ducking or holding Down.");
                }

                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.4f, 0.9f, 1.0f, 1.0f), "Mode 9 Diagnostic Specifications (Recovery, Dash & Projectiles):");
                ImGui::BulletText("F1: P1 FP Baseline | Stand alone Front Punch -> Natural 12F recovery -> Neutral (Total 24F, Advantage +1)");
                ImGui::BulletText("F2: P1 FP -> Immediate Dash | FP on hit -> Buffered F,F dash upon contact -> Dash marked Vibrant Orange");
                ImGui::BulletText("F3: P1 D, B + 3 Normal Projectile | Sub-Zero Ice Ball / Freeze (S, A+G) -> Startup, Launch & Recovery");
                ImGui::BulletText("F4: P1 D, B + 3 + Block EX Projectile | Sub-Zero Ice Beam (S, A+G+Space) -> EX Projectile Phase Analysis");
                ImGui::BulletText("F5: JC FP -> Dash -> FP -> D+FP (Frame 56) | Last FP input at Frame 56 (Buffer 7F)");
                ImGui::BulletText("F6: JC FP -> Dash -> FP -> D+FP (Frame 58) | Last FP input at Frame 58 (Buffer 9F, 1F expiration)");
                ImGui::BulletText("F7: JC FP -> Dash -> FP -> D+FP (Frame 59) | Last FP input at Frame 59 (Buffer 10F, 2F expiration)");
                ImGui::BulletText("F8: JC FP -> Dash -> FP -> D+FP (Frame 60) | Last FP input at Frame 60 (Buffer 11F, 3F expiration)");
                ImGui::BulletText("Visual Timeline | Pink = Startup, Red = Active, Blue = Recovery, Yellow = Hitstun, Cyan = Blockstun, Orange = Dash");
                break;
            }

            case 10: {
                if (ImGui::Button("F1: Solo Baseline (Frame 94)")) {
                    MK9TradeFix::TriggerMode10F1();
                }
                ImGui::SameLine();
                if (ImGui::Button("F2: Stand BL (+0f: Frame 94)")) {
                    MK9TradeFix::TriggerMode10F2();
                }
                ImGui::SameLine();
                if (ImGui::Button("F3: Stand BL (+5f: Frame 99)")) {
                    MK9TradeFix::TriggerMode10F3();
                }
                ImGui::SameLine();
                if (ImGui::Button("F4: Stand BL (+6f: Frame 100)")) {
                    MK9TradeFix::TriggerMode10F4();
                }
                
                ImGui::Spacing();
                if (ImGui::Button("F5: Crouch BL (+8f: Frame 102)")) {
                    MK9TradeFix::TriggerMode10F5();
                }
                ImGui::SameLine();
                if (ImGui::Button("F6: Crouch BL (+9f: Frame 103)")) {
                    MK9TradeFix::TriggerMode10F6();
                }
                ImGui::SameLine();
                if (ImGui::Button("F7: Stand BL (+8f: Frame 102)")) {
                    MK9TradeFix::TriggerMode10F7();
                }
                ImGui::SameLine();
                if (ImGui::Button("F8: Stand BL (+9f: Frame 103)")) {
                    MK9TradeFix::TriggerMode10F8();
                }

                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.4f, 0.9f, 1.0f, 1.0f), "Mode 10 Sequence Specifications (P2 EX Forceball -> F+3 vs P1 Reversal):");
                ImGui::BulletText("P2 Sequence: Hold Left (30f Walk) -> Neutral (5f) -> D,F+2+BL (EX Forceball) -> F+3 fixed at Frame 94");
                ImGui::BulletText("F1: Solo Baseline | P2 F+3 at Frame 94 uninterrupted (P1 idle)");
                ImGui::BulletText("F2: P1 Stand BL -> Reversal at Frame 94 (+0f delay)");
                ImGui::BulletText("F3: P1 Stand BL -> Reversal at Frame 99 (+5f delay)");
                ImGui::BulletText("F4: P1 Stand BL -> Reversal at Frame 100 (+6f delay)");
                ImGui::BulletText("F5: P1 Stand BL -> Crouch BL on P2 F+3 input -> Reversal at Frame 102 (+8f delay) [Former F8]");
                ImGui::BulletText("F6: P1 Stand BL -> Crouch BL on P2 F+3 input -> Reversal at Frame 103 (+9f delay)");
                ImGui::BulletText("F7: P1 Stand BL (No Crouch) -> Reversal at Frame 102 (+8f delay)");
                ImGui::BulletText("F8: P1 Stand BL (No Crouch) -> Reversal at Frame 103 (+9f delay)");
                break;
            }

            case 11: {
                if (ImGui::Button("F1: Solo Baseline (Frame 94)##m11")) {
                    MK9TradeFix::TriggerMode11F1();
                }
                ImGui::SameLine();
                if (ImGui::Button("F2: Stand BL (+0f: Frame 94)##m11")) {
                    MK9TradeFix::TriggerMode11F2();
                }
                ImGui::SameLine();
                if (ImGui::Button("F3: Stand BL (+5f: Frame 99)##m11")) {
                    MK9TradeFix::TriggerMode11F3();
                }
                ImGui::SameLine();
                if (ImGui::Button("F4: Stand BL (+6f: Frame 100)##m11")) {
                    MK9TradeFix::TriggerMode11F4();
                }
                
                ImGui::Spacing();
                if (ImGui::Button("F5: Crouch BL (+8f: Frame 102)##m11")) {
                    MK9TradeFix::TriggerMode11F5();
                }
                ImGui::SameLine();
                if (ImGui::Button("F6: Crouch BL (+9f: Frame 103)##m11")) {
                    MK9TradeFix::TriggerMode11F6();
                }
                ImGui::SameLine();
                if (ImGui::Button("F7: Stand BL (+8f: Frame 102)##m11")) {
                    MK9TradeFix::TriggerMode11F7();
                }
                ImGui::SameLine();
                if (ImGui::Button("F8: Stand BL (+9f: Frame 103)##m11")) {
                    MK9TradeFix::TriggerMode11F8();
                }

                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.4f, 0.9f, 1.0f, 1.0f), "Mode 11 Sequence Specifications (P2 EX Forceball -> F+3 vs P1 D,F+1+BL Unarmored EX):");
                ImGui::BulletText("P2 Sequence: Hold Left (30f Walk) -> Neutral (5f) -> D,F+2+BL (EX Forceball) -> F+3 fixed at Frame 94");
                ImGui::BulletText("F1: Solo Baseline | P2 F+3 at Frame 94 uninterrupted (P1 idle)");
                ImGui::BulletText("F2: P1 Stand BL -> D,F+1+BL Reversal at Frame 94 (+0f delay)");
                ImGui::BulletText("F3: P1 Stand BL -> D,F+1+BL Reversal at Frame 99 (+5f delay)");
                ImGui::BulletText("F4: P1 Stand BL -> D,F+1+BL Reversal at Frame 100 (+6f delay)");
                ImGui::BulletText("F5: P1 Stand BL -> Crouch BL on P2 F+3 input -> D,F+1+BL Reversal at Frame 102 (+8f delay)");
                ImGui::BulletText("F6: P1 Stand BL -> Crouch BL on P2 F+3 input -> D,F+1+BL Reversal at Frame 103 (+9f delay)");
                ImGui::BulletText("F7: P1 Stand BL (No Crouch) -> D,F+1+BL Reversal at Frame 102 (+8f delay)");
                ImGui::BulletText("F8: P1 Stand BL (No Crouch) -> D,F+1+BL Reversal at Frame 103 (+9f delay)");
                ImGui::BulletText("Direct Comparison: Compare Mode 10 (P1 D,B+3+BL) vs Mode 11 (P1 D,F+1+BL) to distinguish armored vs non-armored specials!");
                break;
            }

            case 12: {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Diagnostic Mode 12: Multi-Device Input Bleed & Mashing Isolation Bot");
                ImGui::TextDisabled("Simultaneously floods XInput gamepads (Port 0 & 1), DirectInput, and Keyboard with ultra-rapid mashing bursts to verify 0% bleed into the opponent.");
                ImGui::Spacing();

                if (ImGui::Button("F1: P2 Max Mashing (Extreme Multi-Device Burst)##m12")) {
                    MK9TradeFix::TriggerMode12F1();
                }
                ImGui::SameLine();
                if (ImGui::Button("F2: P2 Moderate Mashing (Rapid Inputs)##m12")) {
                    MK9TradeFix::TriggerMode12F2();
                }
                
                ImGui::Spacing();
                if (ImGui::Button("F3: P1 Max Mashing (Extreme Multi-Device Burst)##m12")) {
                    MK9TradeFix::TriggerMode12F3();
                }
                ImGui::SameLine();
                if (ImGui::Button("F4: P1 Moderate Mashing (Rapid Inputs)##m12")) {
                    MK9TradeFix::TriggerMode12F4();
                }

                ImGui::Spacing();
                if (ImGui::Button("F5: P2 Mashing (6s) + P1 Walk 4s Back -> 4s Fwd##m12")) {
                    MK9TradeFix::TriggerMode12F5();
                }
                ImGui::SameLine();
                if (ImGui::Button("F6: P1 Mashing (6s) + P2 Walk 4s Back -> 4s Fwd##m12")) {
                    MK9TradeFix::TriggerMode12F6();
                }

                ImGui::Spacing();
                bool isolState = MK9TradeFix::InputIsolation::IsEnabled();
                if (ImGui::Checkbox("Enable Input Device Isolation & Cross-Talk Quarantine", &isolState)) {
                    MK9TradeFix::InputIsolation::SetEnabled(isolState);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", "Quarantines 0x00425480, 0x00425510, 0x004255A0 and 0x004468D0 to isolate P1/P2 controller & keyboard input buffers.");
                }

                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.4f, 0.9f, 1.0f, 1.0f), "Mode 12 Multi-Device Stress Specifications (Actual Gamepads & Keyboard):");
                ImGui::BulletText("F1: P2 Max Mashing | Extreme burst: XInput P2 Gamepad + DirectInput + Keyboard (Num4..9) at 60Hz. P1 must remain in 100%% Neutral!");
                ImGui::BulletText("F2: P2 Moderate Mashing | Clean rapid mashing: P2 Controller & Numpad keys cycling at 30Hz. Verify 0 bleed.");
                ImGui::BulletText("F3: P1 Max Mashing | Extreme burst: XInput P1 Gamepad + DirectInput + Keyboard (T, U, G, J, W, S, A, D) at 60Hz. P2 must remain in 100%% Neutral!");
                ImGui::BulletText("F4: P1 Moderate Mashing | Clean rapid mashing: P1 Controller & keys cycling at 30Hz. Verify 0 bleed.");
                ImGui::BulletText("F5: P1 Walks (4s Back + 4s Fwd) | P2 mashes for 6s max then sits neutral while P1 finishes walking.");
                ImGui::BulletText("F6: P2 Walks (4s Back + 4s Fwd) | P1 mashes for 6s max then sits neutral while P2 finishes walking.");
                ImGui::BulletText("Live Telemetry: Real-time telemetry in console verifies opponent character stance and flags any detected bleed event.");
                break;
            }

            case 13: {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Diagnostic Mode 13: Tech Roll Window Identification (Fwd -> Back Rocker)");
                ImGui::TextDisabled("P1 executes knockdowns (Sweep / Uppercut). P2 executes Forward -> Back rocker at target frame offsets.");
                ImGui::Spacing();

                if (ImGui::Button("F1: Sweep -> Roll (+42f / +28f post-hit) [Confirmed]##m13_1")) {
                    MK9TradeFix::TriggerMode13F1();
                }
                ImGui::SameLine();
                if (ImGui::Button("F2: Sweep -> Roll (+44f / +30f post-hit) [Confirmed]##m13_2")) {
                    MK9TradeFix::TriggerMode13F2();
                }

                ImGui::Spacing();
                if (ImGui::Button("F3: Uppercut -> Roll (+65f / +56f post-hit) [Confirmed]##m13_3")) {
                    MK9TradeFix::TriggerMode13F3();
                }
                ImGui::SameLine();
                if (ImGui::Button("F4: Uppercut -> Roll (+66f / +57f post-hit) [Confirmed]##m13_4")) {
                    MK9TradeFix::TriggerMode13F4();
                }

                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.4f, 0.9f, 1.0f, 1.0f), "Mode 13 Quick Roll Window Identification:");
                ImGui::BulletText("F1 / F2 (Sweep B+4): Connects Frame 15 (elapsed +14f). P2 rolls at +42f / +44f (Displacement > 177 units confirmed).");
                ImGui::BulletText("F3 / F4 (Uppercut D+2): Connects Frame 10 (elapsed +9f). Floor contact is Frame 68. P2 rolls at +65f / +66f (Displacement up to 435 units confirmed).");
                ImGui::BulletText("Universal Trigger Commonality: All quick rolls buffer exactly within [-2f, +2f] of floor impact (Action State 0x460D)!");
                ImGui::BulletText("Live Logs: Check console / MK9_LIVE_RUNTIME.LOG for '[MODE 13 RESULT]'.");
                break;
            }

            case 14: {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Diagnostic Mode 14: Delayed Wakeup Buffer Window Identification");
                ImGui::TextDisabled("Probes the exact buffer window for holding DOWN relative to knockdown floor impact (Action State 0x460D).");
                ImGui::Spacing();

                // Knockdown Move Selector
                ImGui::Text("Knockdown Move:");
                ImGui::SameLine();
                bool isSweep = (MK9TradeFix::g_Mode14_KnockdownMove == 0);
                bool isUpper = (MK9TradeFix::g_Mode14_KnockdownMove == 1);
                if (isSweep) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.3f, 1.0f));
                if (ImGui::Button("P1 Sweep (Back + 4)##m14_mv0", ImVec2(180, 26))) {
                    MK9TradeFix::g_Mode14_KnockdownMove = 0;
                }
                if (isSweep) ImGui::PopStyleColor();
                ImGui::SameLine();
                if (isUpper) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.3f, 1.0f));
                if (ImGui::Button("P1 Uppercut (Down + 2)##m14_mv1", ImVec2(180, 26))) {
                    MK9TradeFix::g_Mode14_KnockdownMove = 1;
                }
                if (isUpper) ImGui::PopStyleColor();

                ImGui::Spacing();
                ImGui::PushItemWidth(260.0f);
                ImGui::SliderInt("Down Hold Duration##m14_hold", &MK9TradeFix::g_Mode14_HoldFrames, 30, 96, "%d Frames");
                ImGui::SameLine();
                ImGui::SliderInt("Custom Offset (rel to Floor 0x460D)##m14_cust_off", &MK9TradeFix::g_Mode14_FloorRelOffset, -25, 30, "%+d Frames");
                ImGui::SameLine();
                if (ImGui::Button("Test Custom Offset##m14_test_cust")) {
                    MK9TradeFix::TriggerMode14Custom(MK9TradeFix::g_Mode14_FloorRelOffset, MK9TradeFix::g_Mode14_HoldFrames);
                }
                ImGui::PopItemWidth();
                ImGui::Spacing();

                // Window Prober Grid: F1 through F8
                ImGui::TextColored(ImVec4(0.9f, 0.85f, 0.3f, 1.0f), "Buffer Window Milestones (F1 - F8 Relative to Floor Impact):");
                if (ImGui::Button("F1: Floor -15f (Airborne Fall)##m14_1", ImVec2(240, 26))) MK9TradeFix::TriggerMode14F1();
                ImGui::SameLine();
                if (ImGui::Button("F2: Floor -10f (Late Airborne)##m14_2", ImVec2(240, 26))) MK9TradeFix::TriggerMode14F2();
                ImGui::SameLine();
                if (ImGui::Button("F3: Floor -5f (Pre-Impact)##m14_3", ImVec2(240, 26))) MK9TradeFix::TriggerMode14F3();
                ImGui::SameLine();
                if (ImGui::Button("F4: Floor 0f (Impact 0x460D)##m14_4", ImVec2(240, 26))) MK9TradeFix::TriggerMode14F4();

                if (ImGui::Button("F5: Floor +5f (Ground Settle)##m14_5", ImVec2(240, 26))) MK9TradeFix::TriggerMode14F5();
                ImGui::SameLine();
                if (ImGui::Button("F6: Floor +10f (Mid Settle)##m14_6", ImVec2(240, 26))) MK9TradeFix::TriggerMode14F6();
                ImGui::SameLine();
                if (ImGui::Button("F7: Floor +15f (Prone 0x620A)##m14_7", ImVec2(240, 26))) MK9TradeFix::TriggerMode14F7();
                ImGui::SameLine();
                if (ImGui::Button("F8: Floor +20f (Prone Buffer)##m14_8", ImVec2(240, 26))) MK9TradeFix::TriggerMode14F8();

                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.4f, 0.9f, 1.0f, 1.0f), "Mode 14 Buffer Window Prober Instructions:");
                ImGui::BulletText("Each key triggers P1's attack, then commands P2 to hold DOWN for the configured duration (%df).", MK9TradeFix::g_Mode14_HoldFrames);
                ImGui::BulletText("Floor 0f corresponds to Action State 0x460D (Knockdown Contact). Negative = buffered before landing; Positive = buffered while grounded.");
                ImGui::BulletText("Live outcome: Console / live log displays whether the offset was ACCEPTED (Prone >= 48f) or REJECTED (Standard Getup < 48f).");
                break;
            }

            case 15: {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Diagnostic Mode 15: Blockstun & Priority Pinpoint Diagnostic");
                ImGui::TextDisabled("Probes the exact frame P1 and P2 become actionable after a blocked attack (Scorpion BK vs Block).");
                ImGui::Spacing();

                if (ImGui::Button("F1: P1 BK ('J') vs P2 Block (Adv: -5f)##m15_1", ImVec2(480, 28))) {
                    MK9TradeFix::TriggerMode15F1();
                }
                if (ImGui::Button("F2: P1 FP ('T') vs P2 Block (Adv:  0f | Screenshot 1)##m15_2", ImVec2(480, 28))) {
                    MK9TradeFix::TriggerMode15F2();
                }
                if (ImGui::Button("F3: P1 BP ('U') vs P2 Block (Adv: -4f | Screenshot 2)##m15_3", ImVec2(480, 28))) {
                    MK9TradeFix::TriggerMode15F3();
                }
                if (ImGui::Button("F4: P1 FK ('G') vs P2 Block (Adv: -14f | Screenshot 3)##m15_4", ImVec2(480, 28))) {
                    MK9TradeFix::TriggerMode15F4();
                }

                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.4f, 0.9f, 1.0f, 1.0f), "Mode 15 Diagnostic Instructions:");
                ImGui::BulletText("F1: BK (Back Kick - 'J') -> Expected physical priority: -5f");
                ImGui::BulletText("F2: FP (Front Punch - 'T') -> Expected physical priority: 0f (Screenshot 1)");
                ImGui::BulletText("F3: BP (Back Punch - 'U') -> Expected physical priority: -4f (Screenshot 2)");
                ImGui::BulletText("F4: FK (Front Kick - 'G') -> Expected physical priority: -14f (Screenshot 3)");
                ImGui::BulletText("P1 attacks for 2f, then buffers Up for 80 frames. P2 blocks 25f, then buffers Up for 50 frames.");
                ImGui::BulletText("Pinpoints exact block impact frame, stun duration, P1 jump takeoff, and P2 jump takeoff.");
                break;
            }

            case 16: {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Diagnostic Mode 16: Blockstun Distance & Pushback Measurement (F1 - F4)");
                ImGui::TextDisabled("Measures exact distance between characters at block impact, frame-by-frame pushback rate during blockstun, and net distance delta upon recovery.");
                ImGui::Spacing();

                if (ImGui::Button("F1: FP ('T') vs Block##m16_1", ImVec2(185, 26))) MK9TradeFix::TriggerMode16F1();
                ImGui::SameLine();
                if (ImGui::Button("F2: BP ('U') vs Block##m16_2", ImVec2(185, 26))) MK9TradeFix::TriggerMode16F2();
                ImGui::SameLine();
                if (ImGui::Button("F3: FK ('G') vs Block##m16_3", ImVec2(185, 26))) MK9TradeFix::TriggerMode16F3();
                ImGui::SameLine();
                if (ImGui::Button("F4: BK ('J') vs Block##m16_4", ImVec2(185, 26))) MK9TradeFix::TriggerMode16F4();

                ImGui::Spacing();
                ImGui::Checkbox("Enable Custom Block Pushback##m16_pb_enable", &MK9TradeFix::StunModifier::g_EnableCustomBlockPushback);
                ImGui::SameLine();
                ImGui::TextColored(MK9TradeFix::StunModifier::g_EnableCustomBlockPushback ? ImVec4(0.2f, 1.0f, 0.3f, 1.0f) : ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                                   MK9TradeFix::StunModifier::g_EnableCustomBlockPushback ? "[ACTIVE]" : "[VANILLA 1.00x]");

                if (MK9TradeFix::StunModifier::g_EnableCustomBlockPushback) {
                    ImGui::Indent(20.0f);
                    ImGui::SliderFloat("Pushback Distance Multiplier##m16_pb_mult",
                                       &MK9TradeFix::StunModifier::g_BlockPushbackMultiplier, 0.00f, 3.00f, "%.2fx");
                    ImGui::SameLine();
                    if (ImGui::Button("0.00x (Point-Blank)##m16_pb_0")) MK9TradeFix::StunModifier::g_BlockPushbackMultiplier = 0.00f;
                    ImGui::SameLine();
                    if (ImGui::Button("0.50x##m16_pb_05")) MK9TradeFix::StunModifier::g_BlockPushbackMultiplier = 0.50f;
                    ImGui::SameLine();
                    if (ImGui::Button("1.00x (Vanilla)##m16_pb_1")) MK9TradeFix::StunModifier::g_BlockPushbackMultiplier = 1.00f;
                    ImGui::SameLine();
                    if (ImGui::Button("2.00x (Heavy)##m16_pb_2")) MK9TradeFix::StunModifier::g_BlockPushbackMultiplier = 2.00f;

                    ImGui::Checkbox("Corner Pushback Reflection##m16_corner_pb", &MK9TradeFix::StunModifier::g_CornerPushbackReflection);
                    ImGui::Unindent(20.0f);
                }

                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.4f, 0.9f, 1.0f, 1.0f), "Mode 16 Diagnostic Instructions:");
                ImGui::BulletText("F1: FP (Front Punch - 'T') -> Fast 11f high attack vs P2 standing block");
                ImGui::BulletText("F2: BP (Back Punch - 'U') -> 12f mid attack vs P2 standing block");
                ImGui::BulletText("F3: FK (Front Kick - 'G') -> 9f low/mid attack vs P2 standing block");
                ImGui::BulletText("F4: BK (Back Kick - 'J') -> Heavy 20f attack vs P2 standing block");
                ImGui::BulletText("Captures contact distance, frame-by-frame sliding velocity, exit distance, and net pushback delta.");
                break;
            }
            case 17: {
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f), "Mode 17: Attack Frame Data Measurement (F1 - F4)");
                ImGui::TextWrapped("Measures and logs exact frame-by-frame Startup, Active, and Recovery windows for P1 standing attacks.");
                ImGui::Spacing();

                if (ImGui::Button("F1: Front Punch (FP / 'T')##m17_1", ImVec2(220, 28))) MK9TradeFix::TriggerMode17F1();
                ImGui::SameLine();
                if (ImGui::Button("F2: Back Punch (BP / 'U')##m17_2", ImVec2(220, 28))) MK9TradeFix::TriggerMode17F2();

                if (ImGui::Button("F3: Front Kick (FK / 'G')##m17_3", ImVec2(220, 28))) MK9TradeFix::TriggerMode17F3();
                ImGui::SameLine();
                if (ImGui::Button("F4: Back Kick (BK / 'J')##m17_4", ImVec2(220, 28))) MK9TradeFix::TriggerMode17F4();

                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.4f, 0.9f, 1.0f, 1.0f), "Mode 17 Diagnostic Instructions:");
                ImGui::BulletText("Position P1 in neutral stance (whiffing or standing free from hits).");
                ImGui::BulletText("Press F1, F2, F3, or F4 to measure FP ('T'), BP ('U'), FK ('G'), or BK ('J').");
                ImGui::BulletText("Telemetry logs every single frame with ActionState, Hitbox count, Volumes, and SubState.");
                ImGui::BulletText("A comprehensive summary box outputs exact Startup, Active, and Recovery counts.");

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
                RenderAttackFrameModifierControls("mode17");
                break;
            }
            case 18: {
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f), "Mode 18: Normal Cancel Diagnostic (F1 - F5)");
                ImGui::TextWrapped("Tests and detects whether P1's normal attack (FP) can cancel into special moves, EX specials, dashes, or X-Ray super.");
                ImGui::Spacing();

                ImGui::Checkbox("P2 Defender Blocks##m18_p2_block", &MK9TradeFix::g_Mode18_DefenderBlocks);
                ImGui::SameLine();
                ImGui::TextColored(MK9TradeFix::g_Mode18_DefenderBlocks ? ImVec4(1.0f, 0.8f, 0.2f, 1.0f) : ImVec4(0.5f, 1.0f, 0.5f, 1.0f),
                                   MK9TradeFix::g_Mode18_DefenderBlocks ? "(Testing Block Cancel Window)" : "(Testing Hit Cancel Window)");

                ImGui::Spacing();
                if (ImGui::Button("F1: FP into b,b,1 (Spear)##m18_1", ImVec2(230, 28))) MK9TradeFix::TriggerMode18F1();
                ImGui::SameLine();
                if (ImGui::Button("F2: FP into b,b,1+BL (EX Spear)##m18_2", ImVec2(230, 28))) MK9TradeFix::TriggerMode18F2();

                if (ImGui::Button("F3: FP into Front Dash (F, F)##m18_3", ImVec2(230, 28))) MK9TradeFix::TriggerMode18F3();
                ImGui::SameLine();
                if (ImGui::Button("F4: FP into Backdash (B, B)##m18_4", ImVec2(230, 28))) MK9TradeFix::TriggerMode18F4();

                if (ImGui::Button("F5: FP into X-Ray (3+4+BL)##m18_5", ImVec2(230, 28))) MK9TradeFix::TriggerMode18F5();

                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.4f, 0.9f, 1.0f, 1.0f), "Mode 18 Diagnostic Details:");
                ImGui::BulletText("F1: P1 FP ('T') -> Spear (B, B, 1) during hit/block cancel window.");
                ImGui::BulletText("F2: P1 FP ('T') -> EX Spear (B, B, 1 + Block) [Auto-supplies EX meter].");
                ImGui::BulletText("F3: P1 FP ('T') -> Front Dash (F, F).");
                ImGui::BulletText("F4: P1 FP ('T') -> Backdash (B, B).");
                ImGui::BulletText("F5: P1 FP ('T') -> X-Ray Super (Flip Stance + Block) [Auto-supplies 3 bars].");
                ImGui::BulletText("Toggle 'P2 Defender Blocks' to test cancels on Clean Hit vs Blocked Attack.");
                ImGui::BulletText("Logs show exact cancel transition frame, new script function, and cancelled recovery frames.");
                break;
            }
            case 19: {
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f), "Mode 19: Kabal Bug Diagnostics (F1 - F2)");
                ImGui::TextColored(ImVec4(0.4f, 0.9f, 1.0f, 1.0f), "Defender Reaction Setting:");
                ImGui::RadioButton("Clean Hit (Idle)##m19_blk0", &MK9TradeFix::g_Mode19_DefenderBlockMode, 0);
                ImGui::SameLine();
                ImGui::RadioButton("Auto Block (Earliest Recovery)##m19_blk1", &MK9TradeFix::g_Mode19_DefenderBlockMode, 1);
                ImGui::SameLine();
                ImGui::RadioButton("Constant Block##m19_blk2", &MK9TradeFix::g_Mode19_DefenderBlockMode, 2);
                ImGui::Spacing();

                if (ImGui::Button("F1: F+4 -> F+4 Infinite Loop (F+4 into F+4)##m19_1", ImVec2(340, 28))) MK9TradeFix::TriggerMode19F1();
                ImGui::SameLine();
                if (ImGui::Button("F2: 1,1,1 -> NDC -> 1 Glitch Link##m19_2", ImVec2(340, 28))) MK9TradeFix::TriggerMode19F2();

                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.4f, 0.9f, 1.0f, 1.0f), "Mode 19 Bug Diagnostic Details:");
                ImGui::BulletText("F1: F+4 (Forward + BK) -> Follow-up F+4 on recovery.");
                ImGui::Indent(20.0f);
                ImGui::TextDisabled("Root Cause: Kabal F+4 has 39F total duration (recovering in 39F) but inflicts 69F stagger hitstun.");
                ImGui::TextDisabled("Result: +30F frame advantage on hit allows follow-up F+4 (8F startup) to link with +22F surplus window!");
                ImGui::Unindent(20.0f);
                ImGui::Spacing();
                ImGui::BulletText("F2: 1,1,1 -> b,f+4 (Nomad Dash) -> b+BL (NDC) -> 1 (FP).");
                ImGui::Indent(20.0f);
                ImGui::TextDisabled("Root Cause: Nomad Dash Cancel (b+BL) cuts dash recovery down to ~4F, restoring 0x0000 neutral.");
                ImGui::TextDisabled("Result: Follow-up 1 (FP) connects during residual 1,1,1 hitstun, enabling an illegal unblockable loop.");
                ImGui::Unindent(20.0f);
                break;
            }
            case 20: {
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f), "Mode 20: Crouching Normal Attack Diagnostics (D+1, D+2, D+3, D+4)");
                ImGui::TextColored(ImVec4(0.4f, 0.9f, 1.0f, 1.0f), "Trigger Actions (Keyboard F1 - F4):");
                if (ImGui::Button("F1: D+1 (Crouching Front Punch: S + T)##m20_1", ImVec2(340, 28))) MK9TradeFix::TriggerMode20F1();
                ImGui::SameLine();
                if (ImGui::Button("F2: D+2 (Uppercut Back Punch: S + U)##m20_2", ImVec2(340, 28))) MK9TradeFix::TriggerMode20F2();

                if (ImGui::Button("F3: D+3 (Crouching Front Kick: S + G)##m20_3", ImVec2(340, 28))) MK9TradeFix::TriggerMode20F3();
                ImGui::SameLine();
                if (ImGui::Button("F4: D+4 (Crouching Back Kick: S + J)##m20_4", ImVec2(340, 28))) MK9TradeFix::TriggerMode20F4();

                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.4f, 0.9f, 1.0f, 1.0f), "Mode 20 Crouching Normal Diagnostic Details:");
                ImGui::BulletText("F1: D+1 (Down + Front Punch) -> Fast low jab poke (RxID 117 on Scorpion, Non-Uppercut).");
                ImGui::BulletText("F2: D+2 (Down + Back Punch)  -> True Uppercut Launcher (RxID 121 'attack2down', TOASTY-Eligible).");
                ImGui::BulletText("F3: D+3 (Down + Front Kick)   -> Low shin kick (RxID 125 on Scorpion, Non-Uppercut).");
                ImGui::BulletText("F4: D+4 (Down + Back Kick)    -> Long reach low sweep/poke (RxID 129 on Scorpion, Non-Uppercut).");
                ImGui::BulletText("Inspects exact reaction IDs, script functions, and validates Uppercut classification.");
                break;
            }
        }
    }

    static void RenderAttackFrameModifierControls(const char* idSuffix) {
        ImGui::BeginGroup();

        ImGui::TextColored(ImVec4(0.35f, 0.85f, 1.0f, 1.0f), "Attack Frame Data Modifier");
        ImGui::SameLine();
        ImGui::Checkbox("Enabled##afm_enable", &MK9TradeFix::AttackFrameModifier::g_EnableAttackFrameModifier);
        ImGui::SameLine();
        if (ImGui::Button("Reset All to Vanilla (0F)##afm_reset")) {
            MK9TradeFix::AttackFrameModifier::ResetToVanilla();
        }

        ImGui::Spacing();
        ImGui::Text("Target Scope:");
        ImGui::SameLine();
        ImGui::RadioButton("Both Players##afm_scope_both", &MK9TradeFix::AttackFrameModifier::g_TargetScope, 0);
        ImGui::SameLine();
        ImGui::RadioButton("P1 Only##afm_scope_p1", &MK9TradeFix::AttackFrameModifier::g_TargetScope, 1);
        ImGui::SameLine();
        ImGui::RadioButton("P2 Only##afm_scope_p2", &MK9TradeFix::AttackFrameModifier::g_TargetScope, 2);

        ImGui::Separator();
        ImGui::Spacing();

        // 1. Normal Attacks Tuning
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.5f, 1.0f), "[1] Normal Attacks Tuning:");
        ImGui::Indent(15.0f);
        ImGui::SliderInt("Normals Startup Delta##afm_n_start", &MK9TradeFix::AttackFrameModifier::g_NormalsStartupDelta, -12, 12, "%+d Frames");
        ImGui::SameLine();
        if (ImGui::Button("0F##afm_n_s0")) MK9TradeFix::AttackFrameModifier::g_NormalsStartupDelta = 0;
        
        ImGui::SliderInt("Normals Active Delta##afm_n_act", &MK9TradeFix::AttackFrameModifier::g_NormalsActiveDelta, -12, 12, "%+d Frames");
        ImGui::SameLine();
        if (ImGui::Button("0F##afm_n_a0")) MK9TradeFix::AttackFrameModifier::g_NormalsActiveDelta = 0;

        ImGui::SliderInt("Normals Recovery Delta##afm_n_rec", &MK9TradeFix::AttackFrameModifier::g_NormalsRecoveryDelta, -12, 12, "%+d Frames");
        ImGui::SameLine();
        if (ImGui::Button("0F##afm_n_r0")) MK9TradeFix::AttackFrameModifier::g_NormalsRecoveryDelta = 0;
        ImGui::Unindent(15.0f);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // 2. Special Moves Tuning
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "[2] Special Moves Tuning:");
        ImGui::Indent(15.0f);
        ImGui::SliderInt("Specials Startup Delta##afm_s_start", &MK9TradeFix::AttackFrameModifier::g_SpecialsStartupDelta, -12, 12, "%+d Frames");
        ImGui::SameLine();
        if (ImGui::Button("0F##afm_s_s0")) MK9TradeFix::AttackFrameModifier::g_SpecialsStartupDelta = 0;

        ImGui::SliderInt("Specials Active Delta##afm_s_act", &MK9TradeFix::AttackFrameModifier::g_SpecialsActiveDelta, -12, 12, "%+d Frames");
        ImGui::SameLine();
        if (ImGui::Button("0F##afm_s_a0")) MK9TradeFix::AttackFrameModifier::g_SpecialsActiveDelta = 0;

        ImGui::SliderInt("Specials Recovery Delta##afm_s_rec", &MK9TradeFix::AttackFrameModifier::g_SpecialsRecoveryDelta, -12, 12, "%+d Frames");
        ImGui::SameLine();
        if (ImGui::Button("0F##afm_s_r0")) MK9TradeFix::AttackFrameModifier::g_SpecialsRecoveryDelta = 0;
        ImGui::Unindent(15.0f);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // 3. Individual Attack Overrides (FP, BP, FK, BK)
        if (ImGui::TreeNode("Individual Standing Normal Overrides (FP, BP, FK, BK)")) {
            ImGui::Checkbox("Enable Individual Overrides##afm_indiv_enable", &MK9TradeFix::AttackFrameModifier::g_EnablePerAttackOverrides);
            if (MK9TradeFix::AttackFrameModifier::g_EnablePerAttackOverrides) {
                ImGui::Indent(15.0f);
                
                // FP
                ImGui::TextColored(ImVec4(0.5f, 0.85f, 1.0f, 1.0f), "FP (Front Punch - 1 / 'T') Overrides:");
                ImGui::SliderInt("FP Startup##afm_fp_s", &MK9TradeFix::AttackFrameModifier::g_FP_StartupDelta, -12, 12, "%+d Frames");
                ImGui::SliderInt("FP Active##afm_fp_a", &MK9TradeFix::AttackFrameModifier::g_FP_ActiveDelta, -12, 12, "%+d Frames");
                ImGui::SliderInt("FP Recovery##afm_fp_r", &MK9TradeFix::AttackFrameModifier::g_FP_RecoveryDelta, -12, 12, "%+d Frames");
                ImGui::Spacing();

                // BP
                ImGui::TextColored(ImVec4(0.5f, 0.85f, 1.0f, 1.0f), "BP (Back Punch - 2 / 'U') Overrides:");
                ImGui::SliderInt("BP Startup##afm_bp_s", &MK9TradeFix::AttackFrameModifier::g_BP_StartupDelta, -12, 12, "%+d Frames");
                ImGui::SliderInt("BP Active##afm_bp_a", &MK9TradeFix::AttackFrameModifier::g_BP_ActiveDelta, -12, 12, "%+d Frames");
                ImGui::SliderInt("BP Recovery##afm_bp_r", &MK9TradeFix::AttackFrameModifier::g_BP_RecoveryDelta, -12, 12, "%+d Frames");
                ImGui::Spacing();

                // FK
                ImGui::TextColored(ImVec4(0.5f, 0.85f, 1.0f, 1.0f), "FK (Front Kick - 3 / 'G') Overrides:");
                ImGui::SliderInt("FK Startup##afm_fk_s", &MK9TradeFix::AttackFrameModifier::g_FK_StartupDelta, -12, 12, "%+d Frames");
                ImGui::SliderInt("FK Active##afm_fk_a", &MK9TradeFix::AttackFrameModifier::g_FK_ActiveDelta, -12, 12, "%+d Frames");
                ImGui::SliderInt("FK Recovery##afm_fk_r", &MK9TradeFix::AttackFrameModifier::g_FK_RecoveryDelta, -12, 12, "%+d Frames");
                ImGui::Spacing();

                // BK
                ImGui::TextColored(ImVec4(0.5f, 0.85f, 1.0f, 1.0f), "BK (Back Kick - 4 / 'J') Overrides:");
                ImGui::SliderInt("BK Startup##afm_bk_s", &MK9TradeFix::AttackFrameModifier::g_BK_StartupDelta, -12, 12, "%+d Frames");
                ImGui::SliderInt("BK Active##afm_bk_a", &MK9TradeFix::AttackFrameModifier::g_BK_ActiveDelta, -12, 12, "%+d Frames");
                ImGui::SliderInt("BK Recovery##afm_bk_r", &MK9TradeFix::AttackFrameModifier::g_BK_RecoveryDelta, -12, 12, "%+d Frames");

                ImGui::Unindent(15.0f);
            }
            ImGui::TreePop();
        }

        ImGui::EndGroup();
    }

    static void RenderControllerPortConfigUI(const char* idSuffix) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.10f, 0.12f, 0.16f, 0.70f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.25f, 0.45f, 0.65f, 0.75f));
        
        char childId[32];
        snprintf(childId, sizeof(childId), "CtrlPortBox_%s", idSuffix);
        ImGui::BeginChild(childId, ImVec2(0, 84), true);

        // --- ROW 1: Gamepad Controllers ---
        // Detect P2 button
        char detP2Btn[64];
        bool isDetP2 = (MK9TradeFix::g_DetectingControllerForPlayer == 2);
        snprintf(detP2Btn, sizeof(detP2Btn), isDetP2 ? 
                 "[*] Press P2 Controller Button...##detP2%s" : "Detect P2 (Dummy) Controller##detP2%s", idSuffix);
        
        if (isDetP2) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.50f, 0.10f, 1.0f));
        if (ImGui::Button(detP2Btn, ImVec2(240, 24))) {
            if (isDetP2) MK9TradeFix::CancelControllerDetection();
            else MK9TradeFix::StartControllerDetection(2);
        }
        if (isDetP2) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Click then press any button/stick on your Dummy controller to automatically lock it as P2.");
        }

        ImGui::SameLine();

        // Detect P1 button
        char detP1Btn[64];
        bool isDetP1 = (MK9TradeFix::g_DetectingControllerForPlayer == 1);
        snprintf(detP1Btn, sizeof(detP1Btn), isDetP1 ? 
                 "[*] Press P1 Controller Button...##detP1%s" : "Detect P1 Controller##detP1%s", idSuffix);
        
        if (isDetP1) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.60f, 0.85f, 1.0f));
        if (ImGui::Button(detP1Btn, ImVec2(185, 24))) {
            if (isDetP1) MK9TradeFix::CancelControllerDetection();
            else MK9TradeFix::StartControllerDetection(1);
        }
        if (isDetP1) ImGui::PopStyleColor();

        ImGui::SameLine();

        // 1-Click Gamepad Swap Button
        char swapBtn[64];
        snprintf(swapBtn, sizeof(swapBtn), "Swap P1 <-> P2 Gamepad##swap%s", idSuffix);
        if (ImGui::Button(swapBtn, ImVec2(180, 24))) {
            MK9TradeFix::SwapControllerPorts();
        }

        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.40f, 0.90f, 1.0f, 1.0f), "Gamepad: P1 = Port %d (Index %d) | P2 Dummy = Port %d (Index %d)",
                           MK9TradeFix::g_P1_XInputPort + 1, MK9TradeFix::g_P1_XInputPort,
                           MK9TradeFix::g_P2_XInputPort + 1, MK9TradeFix::g_P2_XInputPort);

        // --- Keyboard Information Notice ---
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.80f, 0.80f, 0.80f, 1.0f), "Note: Keyboard input for Recording & Reversal uses Player 2 default controls (Numpad / Arrow keys).");

        ImGui::EndChild();
        ImGui::PopStyleColor(2);
    }

    static void RenderRecordingSettingsUI() {
        auto& rec = MK9TradeFix::InputRecordingManager::Get();
        static int s_ActiveSubMode = 0; // 0 = Record, 1 = Replay

        bool inMatch = MK9TradeFix::IsMatchActive();
        bool isPaused = MK9TradeFix::IsGameOrSimulationPaused();
        bool inTraining = inMatch && MK9TradeFix::IsTrainingMode();
        bool p2Human = inMatch && MK9TradeFix::IsP2HumanControl();
        bool canControl = (inMatch && inTraining && p2Human);
        bool shouldDisable = (!canControl || isPaused);

        ImGui::Spacing();

        // Game Mode & Dummy Status Banner (Matching Reversal Engine parity)
        if (!inMatch) {
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.9f, 0.25f, 0.25f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.25f, 0.08f, 0.08f, 0.70f));
            ImGui::BeginChild("RecMatchAlert", ImVec2(0, 48), true);
            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "[RESTRICTED] Active Match Required: Practice Dummy Recorder is only available during an active match.");
            ImGui::TextColored(ImVec4(0.75f, 0.75f, 0.75f, 1.0f), "Current State: Main Menu / Character Select / Loading Screen.");
            ImGui::EndChild();
            ImGui::PopStyleColor(2);
            ImGui::Spacing();
        } else if (!inTraining) {
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.9f, 0.25f, 0.25f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.25f, 0.08f, 0.08f, 0.70f));
            ImGui::BeginChild("RecGameModeAlert", ImVec2(0, 48), true);
            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "[RESTRICTED] Practice Dummy Recorder is only available in Practice / Training Mode.");
            ImGui::TextColored(ImVec4(0.75f, 0.75f, 0.75f, 1.0f), "Current Game Mode: Ladder / Arcade / Versus / Story (Recording and Playback are locked).");
            ImGui::EndChild();
            ImGui::PopStyleColor(2);
            ImGui::Spacing();
        } else if (!p2Human) {
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.95f, 0.75f, 0.20f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.25f, 0.20f, 0.05f, 0.70f));
            ImGui::BeginChild("RecDummyAlert", ImVec2(0, 48), true);
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.20f, 1.0f), "[ATTENTION] Training Mode Active: Player 2 Dummy is currently set to [ AI / Stand ].");
            ImGui::TextColored(ImVec4(0.90f, 0.90f, 0.90f, 1.0f), "Set Pause Menu -> Practice Options -> Dummy to [ Human ] to enable Recording & Playback.");
            ImGui::EndChild();
            ImGui::PopStyleColor(2);
            ImGui::Spacing();
        } else {
            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f), "[ACTIVE] Training Mode Active | Player 2 Dummy: [ Human ] (Ready for Recording & Playback)");
            ImGui::Spacing();
        }

        if (inMatch && isPaused) {
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.95f, 0.75f, 0.20f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.25f, 0.20f, 0.05f, 0.70f));
            ImGui::BeginChild("RecPauseAlert", ImVec2(0, 48), true);
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.20f, 1.0f), "[FROZEN] Game Simulation is Paused (Mod Pause or In-Game Menu)");
            ImGui::TextColored(ImVec4(0.90f, 0.90f, 0.90f, 1.0f), "Resume game simulation ( \\ or Unpause) to record or play back actions.");
            ImGui::EndChild();
            ImGui::PopStyleColor(2);
            ImGui::Spacing();
        }

        auto FormatRecordingDuration = [](uint32_t f, char* out, size_t outSize) {
            uint32_t totalSec = f / 60;
            uint32_t mins = totalSec / 60;
            uint32_t secs = totalSec % 60;
            if (mins > 0) {
                snprintf(out, outSize, "%uF Recorded (%dm %02ds)", f, mins, secs);
            } else {
                snprintf(out, outSize, "%uF Recorded (%.1fs)", f, (float)f / 60.0f);
            }
        };

        if (shouldDisable) ImGui::BeginDisabled();

        // Top Sub-Tabs [ Record Mode ] | [ Replay Mode ] + Super Meter Dropdown
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
        bool isRecModeActive = (s_ActiveSubMode == 0);
        if (isRecModeActive) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.70f, 0.20f, 0.25f, 1.0f));
        if (ImGui::Button("   Record Mode (Capture P2)   ", ImVec2(210, 30))) {
            s_ActiveSubMode = 0;
        }
        if (isRecModeActive) ImGui::PopStyleColor();

        ImGui::SameLine();
        bool isReplayModeActive = (s_ActiveSubMode == 1);
        if (isReplayModeActive) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.60f, 0.35f, 1.0f));
        if (ImGui::Button("   Replay Mode (Playback P2)   ", ImVec2(210, 30))) {
            s_ActiveSubMode = 1;
        }
        if (isReplayModeActive) ImGui::PopStyleColor();
        ImGui::SameLine(ImGui::GetWindowWidth() - 250.0f);
        {
            bool isNativeFull = MK9TradeFix::IsNativeSuperMeterFull();
            if (isNativeFull) ImGui::BeginDisabled();
            const char* meterModeItems[] = { "NORMAL", "FULL", "REFILL" };
            int curMeterMode = (int)MK9TradeFix::MeterDrainFix::g_SuperMeterPracticeMode;
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 1.0f, 1.0f), "Super Meter:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(120.0f);
            if (ImGui::Combo("##RecSuperMeterMode", &curMeterMode, meterModeItems, IM_ARRAYSIZE(meterModeItems))) {
                MK9TradeFix::MeterDrainFix::g_SuperMeterPracticeMode = (MK9TradeFix::MeterDrainFix::SuperMeterPracticeMode)curMeterMode;
            }
            if (isNativeFull) {
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    ImGui::SetTooltip("Disabled: In-game Practice Options Super Meter is currently set to FULL.");
                }
            } else {
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("NORMAL: Standard meter behavior.\nFULL: Continuously maintains 100%% Super Meter.\nREFILL: If P1/P2 does not attack for 4 seconds, automatically refills their meter to 100%%.");
                }
            }
        }
        ImGui::PopStyleVar();

        ImGui::Spacing();
        RenderControllerPortConfigUI("Rec");
        ImGui::Spacing();

        if (s_ActiveSubMode == 0) {
            // ==========================================
            // RECORD MODE
            // ==========================================
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "RECORD MODE: Record new actions for the practice dummy (Player 2).");
            ImGui::BulletText("Click 'Record' on any slot to start capturing inputs from Player 2 (Gamepad / Stick / Keyboard).");
            ImGui::BulletText("Extended capacity: up to 4 minutes (14,400 frames) continuous recording per slot.");
            ImGui::BulletText("When recording begins, this menu will collapse and display a floating countdown HUD.");
            ImGui::Spacing();

            // Recording Parameters
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "Recording Trigger Settings:");
            const char* triggerModes[] = { "Auto (On First Input)", "Manual (Instant)", "Countdown (Timer)" };
            int curTrigger = (int)rec.GetTriggerMode();
            ImGui::SetNextItemWidth(260.0f);
            if (ImGui::Combo("Trigger Method", &curTrigger, triggerModes, IM_ARRAYSIZE(triggerModes))) {
                rec.SetTriggerMode((MK9TradeFix::RecordingTriggerMode)curTrigger);
            }

            if (rec.GetTriggerMode() == MK9TradeFix::RecordingTriggerMode::COUNTDOWN) {
                int curCountdown = rec.GetCountdownSeconds();
                const char* countdownOptions[] = { "0 seconds", "1 second", "2 seconds", "3 seconds" };
                ImGui::SetNextItemWidth(260.0f);
                if (ImGui::Combo("Countdown Duration", &curCountdown, countdownOptions, IM_ARRAYSIZE(countdownOptions))) {
                    rec.SetCountdownSeconds(curCountdown);
                }
            }

            ImGui::Spacing();

            // Table of Slots for Recording
            if (ImGui::BeginTable("RecordSlotsTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("Slot", ImGuiTableColumnFlags_WidthFixed, 65.0f);
                ImGui::TableSetupColumn("Character", ImGuiTableColumnFlags_WidthFixed, 110.0f);
                ImGui::TableSetupColumn("Recorded Status", ImGuiTableColumnFlags_WidthStretch, 1.3f);
                ImGui::TableSetupColumn("Record Action", ImGuiTableColumnFlags_WidthStretch, 1.6f);
                ImGui::TableSetupColumn("Clear", ImGuiTableColumnFlags_WidthFixed, 65.0f);
                ImGui::TableHeadersRow();

                for (int i = 0; i < MK9TradeFix::InputRecordingManager::MAX_SLOTS; i++) {
                    ImGui::TableNextRow(ImGuiTableRowFlags_None, 24.0f);
                    auto& slot = rec.GetSlot(i);

                    // Col 0: Slot Label
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("Slot %d", i + 1);

                    // Col 1: Character Indication
                    ImGui::TableSetColumnIndex(1);
                    if (slot.isEmpty || slot.frames.empty()) {
                        ImGui::TextDisabled("-");
                    } else {
                        const char* charDisp = (slot.characterName[0] != '\0') ? slot.characterName : "Unknown";
                        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.25f, 1.0f), "%s", charDisp);
                    }

                    // Col 2: Status
                    ImGui::TableSetColumnIndex(2);
                    if (slot.isEmpty || slot.frames.empty()) {
                        ImGui::TextDisabled("Empty");
                    } else {
                        char durStr[64];
                        FormatRecordingDuration(slot.totalFrames, durStr, sizeof(durStr));
                        ImGui::TextColored(ImVec4(0.4f, 0.9f, 1.0f, 1.0f), "%s", durStr);
                    }

                    // Col 3: Action Control
                    ImGui::TableSetColumnIndex(3);
                    char btnRecLabel[32];
                    snprintf(btnRecLabel, sizeof(btnRecLabel), "Record Slot %d##rec%d", i + 1, i);
                    
                    bool isThisSlotRecording = (rec.IsRecording() && rec.GetActiveSlotIndex() == i);
                    if (isThisSlotRecording) {
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.2f, 0.2f, 1.0f));
                        if (ImGui::Button("Stop Recording")) {
                            rec.StopRecording();
                        }
                        ImGui::PopStyleColor();
                    } else {
                        if (ImGui::Button(btnRecLabel)) {
                            rec.StartRecording(i);
                        }
                    }

                    // Col 4: Clear
                    ImGui::TableSetColumnIndex(4);
                    char btnClearLabel[32];
                    snprintf(btnClearLabel, sizeof(btnClearLabel), "Clear##clr%d", i);
                    bool slotWasEmpty = slot.isEmpty;
                    if (slotWasEmpty) ImGui::BeginDisabled();
                    if (ImGui::Button(btnClearLabel)) {
                        rec.ClearSlot(i);
                    }
                    if (slotWasEmpty) ImGui::EndDisabled();
                }
                ImGui::EndTable();
            }
        } else {
            // ==========================================
            // REPLAY MODE
            // ==========================================
            ImGui::TextColored(ImVec4(0.45f, 1.0f, 0.55f, 1.0f), "REPLAY MODE: Have the practice dummy play back your recorded actions.");
            ImGui::BulletText("Click 'Play' on any recorded slot to execute it on the dummy immediately.");
            ImGui::BulletText("Set Playback Mode to 'Repeat' or 'Random' to practice defending or punishing specific sequences.");
            ImGui::Spacing();

            // Playback Parameters
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "Playback Settings:");
            const char* playbackModes[] = { "Play Once (Single Execution)", "Repeat / Loop (Continuous)", "Random Slot Selection" };
            int curPlayMode = (int)rec.GetPlaybackMode();
            ImGui::SetNextItemWidth(280.0f);
            if (ImGui::Combo("Playback Mode", &curPlayMode, playbackModes, IM_ARRAYSIZE(playbackModes))) {
                rec.SetPlaybackMode((MK9TradeFix::PlaybackMode)curPlayMode);
            }

            ImGui::Spacing();

            // Table of Slots for Playback
            if (ImGui::BeginTable("ReplaySlotsTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("Slot", ImGuiTableColumnFlags_WidthFixed, 65.0f);
                ImGui::TableSetupColumn("Character", ImGuiTableColumnFlags_WidthFixed, 110.0f);
                ImGui::TableSetupColumn("Recorded Status", ImGuiTableColumnFlags_WidthStretch, 1.3f);
                ImGui::TableSetupColumn("Playback Action", ImGuiTableColumnFlags_WidthStretch, 1.6f);
                ImGui::TableSetupColumn("Clear", ImGuiTableColumnFlags_WidthFixed, 65.0f);
                ImGui::TableHeadersRow();

                for (int i = 0; i < MK9TradeFix::InputRecordingManager::MAX_SLOTS; i++) {
                    ImGui::TableNextRow(ImGuiTableRowFlags_None, 24.0f);
                    auto& slot = rec.GetSlot(i);
                    bool slotWasEmpty = (slot.isEmpty || slot.frames.empty());

                    // Col 0: Slot Label
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("Slot %d", i + 1);

                    // Col 1: Character Indication
                    ImGui::TableSetColumnIndex(1);
                    if (slotWasEmpty) {
                        ImGui::TextDisabled("-");
                    } else {
                        const char* charDisp = (slot.characterName[0] != '\0') ? slot.characterName : "Unknown";
                        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.25f, 1.0f), "%s", charDisp);
                    }

                    // Col 2: Status
                    ImGui::TableSetColumnIndex(2);
                    if (slotWasEmpty) {
                        ImGui::TextDisabled("Empty");
                    } else {
                        char durStr[64];
                        FormatRecordingDuration(slot.totalFrames, durStr, sizeof(durStr));
                        ImGui::TextColored(ImVec4(0.4f, 0.9f, 1.0f, 1.0f), "%s", durStr);
                    }

                    // Col 3: Action Control
                    ImGui::TableSetColumnIndex(3);
                    char btnPlayLabel[32];
                    snprintf(btnPlayLabel, sizeof(btnPlayLabel), "Play Slot %d##play%d", i + 1, i);
                    bool isThisSlotPlaying = (rec.IsPlaying() && rec.GetActiveSlotIndex() == i);
                    
                    if (slotWasEmpty) ImGui::BeginDisabled();
                    if (isThisSlotPlaying) {
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.8f, 0.3f, 1.0f));
                        if (ImGui::Button("Stop Playback")) {
                            rec.StopPlayback();
                        }
                        ImGui::PopStyleColor();
                    } else {
                        if (ImGui::Button(btnPlayLabel)) {
                            rec.StartPlayback(i, 2, 0);
                        }
                    }
                    if (slotWasEmpty) ImGui::EndDisabled();

                    // Col 4: Clear
                    ImGui::TableSetColumnIndex(4);
                    char btnClearLabel[32];
                    snprintf(btnClearLabel, sizeof(btnClearLabel), "Clear##clr%d", i);
                    if (slotWasEmpty) ImGui::BeginDisabled();
                    if (ImGui::Button(btnClearLabel)) {
                        rec.ClearSlot(i);
                    }
                    if (slotWasEmpty) ImGui::EndDisabled();
                }
                ImGui::EndTable();
            }
        }

        // Live Status Footer
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        auto state = rec.GetState();
        if (state == MK9TradeFix::RecordingState::RECORDING) {
            uint32_t curF = rec.GetCurrentRecordProgress();
            uint32_t curSec = curF / 60;
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "[RECORDING ACTIVE] Slot %d - Frame %u / %d (%02u:%02u / 04:00)",
                               rec.GetActiveSlotIndex() + 1, curF, MK9TradeFix::InputRecordingManager::MAX_RECORD_FRAMES,
                               curSec / 60, curSec % 60);
        } else if (state == MK9TradeFix::RecordingState::WAITING_FOR_INPUT) {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "[ARMED - WAITING FOR FIRST INPUT] Slot %d", rec.GetActiveSlotIndex() + 1);
        } else if (state == MK9TradeFix::RecordingState::COUNTDOWN) {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "[COUNTDOWN IN PROGRESS] Starting in %d seconds...", rec.GetCountdownSeconds());
        } else if (state == MK9TradeFix::RecordingState::PLAYING) {
            uint32_t curF = rec.GetCurrentPlaybackProgress();
            uint32_t curSec = curF / 60;
            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f), "[PLAYING BACK] Slot %d - Frame %u (%02u:%02u)",
                               rec.GetActiveSlotIndex() + 1, curF, curSec / 60, curSec % 60);
        } else {
            ImGui::TextDisabled("[IDLE] Ready to record or play back practice dummy actions.");
        }

        ImGui::Spacing();
        bool isHudOn = rec.IsHUDActive();
        if (ImGui::Button(isHudOn ? "Hide Floating Recorder HUD" : "Show Floating Recorder HUD", ImVec2(240, 26))) {
            rec.SetHUDActive(!isHudOn);
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::CollapsingHeader("Import & Export Input Recordings (.mkr)")) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 1.0f, 1.0f), "Input Recording File Management:");
            ImGui::TextDisabled("Save recorded dummy sequences to disk (.mkr) or import them into practice slots.");
            ImGui::Spacing();

            static char s_RecPathBuf[260] = "MK9_Recordings/slot1.mkr";
            static char s_RecFolderBuf[260] = "MK9_Recordings/";
            static char s_RecFileMsg[128] = "";
            static ImVec4 s_RecMsgColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);

            int activeSlot = rec.GetActiveSlotIndex();
            ImGui::Text("Active Slot: Slot %d", activeSlot + 1);
            ImGui::InputText("Slot Filepath##rec_filepath", s_RecPathBuf, sizeof(s_RecPathBuf));
            
            if (ImGui::Button("Export Active Slot##btn_exp_slot")) {
                if (rec.ExportSlotToFile(activeSlot, s_RecPathBuf)) {
                    snprintf(s_RecFileMsg, sizeof(s_RecFileMsg), "Exported Slot %d to '%s' successfully.", activeSlot + 1, s_RecPathBuf);
                    s_RecMsgColor = ImVec4(0.3f, 1.0f, 0.4f, 1.0f);
                } else {
                    snprintf(s_RecFileMsg, sizeof(s_RecFileMsg), "Failed to export Slot %d (Slot is empty or invalid path).", activeSlot + 1);
                    s_RecMsgColor = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Import to Active Slot##btn_imp_slot")) {
                if (rec.ImportSlotFromFile(activeSlot, s_RecPathBuf)) {
                    snprintf(s_RecFileMsg, sizeof(s_RecFileMsg), "Imported '%s' into Slot %d successfully.", s_RecPathBuf, activeSlot + 1);
                    s_RecMsgColor = ImVec4(0.3f, 1.0f, 0.4f, 1.0f);
                } else {
                    snprintf(s_RecFileMsg, sizeof(s_RecFileMsg), "Failed to import '%s' (File not found or corrupt).", s_RecPathBuf);
                    s_RecMsgColor = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
                }
            }

            ImGui::Spacing();
            ImGui::InputText("Batch Folder##rec_batch_dir", s_RecFolderBuf, sizeof(s_RecFolderBuf));
            if (ImGui::Button("Export All Slots (slot1..5.mkr)##btn_exp_all_slots")) {
                if (rec.ExportAllSlots(s_RecFolderBuf)) {
                    snprintf(s_RecFileMsg, sizeof(s_RecFileMsg), "Exported all slots to folder '%s'.", s_RecFolderBuf);
                    s_RecMsgColor = ImVec4(0.3f, 1.0f, 0.4f, 1.0f);
                } else {
                    snprintf(s_RecFileMsg, sizeof(s_RecFileMsg), "No populated slots to export in '%s'.", s_RecFolderBuf);
                    s_RecMsgColor = ImVec4(1.0f, 0.7f, 0.2f, 1.0f);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Import All Slots##btn_imp_all_slots")) {
                if (rec.ImportAllSlots(s_RecFolderBuf)) {
                    snprintf(s_RecFileMsg, sizeof(s_RecFileMsg), "Imported all available slots from '%s'.", s_RecFolderBuf);
                    s_RecMsgColor = ImVec4(0.3f, 1.0f, 0.4f, 1.0f);
                } else {
                    snprintf(s_RecFileMsg, sizeof(s_RecFileMsg), "Failed to import from folder '%s' (No .mkr files found).", s_RecFolderBuf);
                    s_RecMsgColor = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
                }
            }

            if (s_RecFileMsg[0] != '\0') {
                ImGui::Spacing();
                ImGui::TextColored(s_RecMsgColor, "%s", s_RecFileMsg);
            }
        }

        if (shouldDisable) ImGui::EndDisabled();
    }

    static void RenderReversalSettingsUI() {
        auto& rev = MK9TradeFix::ReversalEngine::Get();
        int targetPlr = rev.GetTargetPlayer();
        bool isP2Target = (targetPlr == 2);

        bool inMatch = MK9TradeFix::IsMatchActive();
        bool isPaused = MK9TradeFix::IsGameOrSimulationPaused();
        bool inTraining = inMatch && MK9TradeFix::IsTrainingMode();
        bool p2Human = inMatch && MK9TradeFix::IsP2HumanControl();
        bool canControl = (inMatch && inTraining && (!isP2Target || p2Human));
        bool shouldDisable = (!canControl || isPaused);

        ImGui::Spacing();

        if (!inMatch) {
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.9f, 0.25f, 0.25f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.25f, 0.08f, 0.08f, 0.70f));
            ImGui::BeginChild("RevMatchAlert", ImVec2(0, 48), true);
            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "[RESTRICTED] Active Match Required: Practice Dummy Reversal Engine is only available during an active match.");
            ImGui::TextColored(ImVec4(0.75f, 0.75f, 0.75f, 1.0f), "Current State: Main Menu / Character Select / Loading Screen.");
            ImGui::EndChild();
            ImGui::PopStyleColor(2);
            ImGui::Spacing();
        } else if (!inTraining) {
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.9f, 0.25f, 0.25f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.25f, 0.08f, 0.08f, 0.70f));
            ImGui::BeginChild("RevGameModeAlert", ImVec2(0, 48), true);
            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "[RESTRICTED] Practice Dummy Reversal Engine is only available in Practice / Training Mode.");
            ImGui::TextColored(ImVec4(0.75f, 0.75f, 0.75f, 1.0f), "Current Game Mode: Ladder / Arcade / Versus / Story (Reversals are locked).");
            ImGui::EndChild();
            ImGui::PopStyleColor(2);
            ImGui::Spacing();
        } else if (isP2Target && !p2Human) {
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.95f, 0.75f, 0.20f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.25f, 0.20f, 0.05f, 0.70f));
            ImGui::BeginChild("RevDummyAlert", ImVec2(0, 48), true);
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.20f, 1.0f), "[ATTENTION] Training Mode Active: Player 2 Dummy is currently set to [ AI / Stand ].");
            ImGui::TextColored(ImVec4(0.90f, 0.90f, 0.90f, 1.0f), "Set Pause Menu -> Practice Options -> Dummy to [ Human ] to enable Wakeup / Block Reversals.");
            ImGui::EndChild();
            ImGui::PopStyleColor(2);
            ImGui::Spacing();
        } else {
            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f), "[ACTIVE] Training Mode Active | Reversal Target: [ Player %d%s ] (Reversal Engine Armed)",
                               targetPlr, isP2Target ? " (Dummy)" : "");
            ImGui::Spacing();
        }

        if (inMatch && isPaused) {
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.95f, 0.75f, 0.20f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.25f, 0.20f, 0.05f, 0.70f));
            ImGui::BeginChild("RevPauseAlert", ImVec2(0, 48), true);
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.20f, 1.0f), "[FROZEN] Game Simulation is Paused (Mod Pause or In-Game Menu)");
            ImGui::TextColored(ImVec4(0.90f, 0.90f, 0.90f, 1.0f), "Reversals held in buffer until simulation resumes.");
            ImGui::EndChild();
            ImGui::PopStyleColor(2);
            ImGui::Spacing();
        }

        if (shouldDisable) ImGui::BeginDisabled();

        bool globalEnabled = rev.IsEnabled();
        if (ImGui::Checkbox("Enable Practice Dummy Reversal Engine", &globalEnabled)) {
            rev.SetEnabled(globalEnabled);
        }
        ImGui::SameLine();
        ImGui::TextColored(globalEnabled ? ImVec4(0.2f, 1.0f, 0.3f, 1.0f) : ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
                           globalEnabled ? "[ACTIVE]" : "[DISABLED]");

        rev.SetTargetPlayer(2);

        ImGui::SameLine(ImGui::GetWindowWidth() - 250.0f);
        {
            bool isNativeFull = MK9TradeFix::IsNativeSuperMeterFull();
            if (isNativeFull) ImGui::BeginDisabled();
            const char* meterModeItems[] = { "NORMAL", "FULL", "REFILL" };
            int curMeterMode = (int)MK9TradeFix::MeterDrainFix::g_SuperMeterPracticeMode;
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 1.0f, 1.0f), "Super Meter:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(120.0f);
            if (ImGui::Combo("##RevSuperMeterMode", &curMeterMode, meterModeItems, IM_ARRAYSIZE(meterModeItems))) {
                MK9TradeFix::MeterDrainFix::g_SuperMeterPracticeMode = (MK9TradeFix::MeterDrainFix::SuperMeterPracticeMode)curMeterMode;
            }
            if (isNativeFull) {
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    ImGui::SetTooltip("Disabled: In-game Practice Options Super Meter is currently set to FULL.");
                }
            } else {
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("NORMAL: Standard meter behavior.\nFULL: Continuously maintains 100%% Super Meter.\nREFILL: If P1/P2 does not attack for 4 seconds, automatically refills their meter to 100%%.");
                }
            }
        }

        auto currentStance = rev.GetDummyIdleStance();
        auto RenderStanceButton = [&](const char* label, MK9TradeFix::DummyIdleStance stance, float width) {
            bool isActive = (currentStance == stance);
            if (isActive) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.65f, 0.28f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.78f, 0.35f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.25f, 0.90f, 0.40f, 1.0f));
            }
            if (ImGui::Button(label, ImVec2(width, 28))) {
                rev.SetDummyIdleStance(stance);
            }
            if (isActive) {
                ImGui::PopStyleColor(3);
            }
        };

        RenderStanceButton("Standing", MK9TradeFix::DummyIdleStance::STANDING, 110.0f);
        ImGui::SameLine();
        RenderStanceButton("Crouch", MK9TradeFix::DummyIdleStance::CROUCHING, 110.0f);
        ImGui::SameLine();
        RenderStanceButton("Stand Block", MK9TradeFix::DummyIdleStance::STAND_BLOCKING, 110.0f);
        ImGui::SameLine();
        RenderStanceButton("Crouch Block", MK9TradeFix::DummyIdleStance::CROUCH_BLOCKING, 110.0f);
        ImGui::SameLine();
        RenderStanceButton("Auto Block (Stand)", MK9TradeFix::DummyIdleStance::AUTO_BLOCK_STAND, 140.0f);
        ImGui::SameLine();
        RenderStanceButton("Auto Block (Crouch)", MK9TradeFix::DummyIdleStance::AUTO_BLOCK_CROUCH, 140.0f);

        ImGui::SameLine();
        bool isStanceHudOn = rev.IsIdleStanceHUDActive();
        if (ImGui::Button(isStanceHudOn ? "Hide Floating Stance HUD" : "Show Floating Stance HUD", ImVec2(185, 28))) {
            rev.SetIdleStanceHUDActive(!isStanceHudOn);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Toggles a separate compact floating window on screen for instant button-click posture switching during training.");
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        RenderControllerPortConfigUI("Rev");
        ImGui::Spacing();

        if (ImGui::BeginTabBar("ReversalCategoryTabBar", ImGuiTabBarFlags_None)) {
            const char* categoryTitles[] = { 
                "Invincible Wake-up", 
                "Regular Wake-up", 
                "Block Reversal", 
                "Reversal After Damage" 
            };

            auto RenderSlotTable = [&](MK9TradeFix::ReversalCategoryConfig& catConfig, int uniqueIdPrefix, bool isOnlySpecials, bool hasWakeupRecovery) {
                bool hasHoldColumn = (hasWakeupRecovery && !isOnlySpecials); // Only Regular Wakeup has the separate Hold column
                bool hasDelayColumn = !isOnlySpecials; // Regular Wakeup, Block, and After Damage have Delay

                int colCount = 3; // Status, Preset Reversal Action, Weight
                if (hasWakeupRecovery) colCount++;
                if (hasHoldColumn) colCount++;
                if (hasDelayColumn) colCount++;

                if (ImGui::BeginTable("ReversalSlotsTable", colCount, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
                    ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 55.0f);
                    if (hasWakeupRecovery) {
                        ImGui::TableSetupColumn("Knockdown Recovery", ImGuiTableColumnFlags_WidthFixed, isOnlySpecials ? 150.0f : 165.0f);
                    }
                    ImGui::TableSetupColumn("Preset Reversal Action", ImGuiTableColumnFlags_WidthStretch, 2.0f);
                    ImGui::TableSetupColumn("Weight", ImGuiTableColumnFlags_WidthFixed, 75.0f);
                    if (hasHoldColumn) {
                        ImGui::TableSetupColumn("Hold", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                    }
                    if (hasDelayColumn) {
                        ImGui::TableSetupColumn("Delay", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                    }
                    ImGui::TableHeadersRow();

                    for (int i = 0; i < MK9TradeFix::ReversalCategoryConfig::MAX_CONFIG_SLOTS; i++) {
                        ImGui::TableNextRow(ImGuiTableRowFlags_None, 24.0f);
                        auto& slotCfg = catConfig.slots[i];
                        int colIdx = 0;

                        // Col 0: On/Off Toggle
                        ImGui::TableSetColumnIndex(colIdx++);
                        char togLabel[32];
                        snprintf(togLabel, sizeof(togLabel), "%s##tog%d_%d", slotCfg.enabled ? " ON " : " OFF ", uniqueIdPrefix, i);
                        bool wasSlotEnabled = slotCfg.enabled;
                        if (wasSlotEnabled) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.8f, 0.3f, 1.0f));
                        if (ImGui::Button(togLabel)) {
                            slotCfg.enabled = !slotCfg.enabled;
                        }
                        if (wasSlotEnabled) ImGui::PopStyleColor();

                        // Col 1 (if present): Knockdown Recovery (Per-row options)
                        if (hasWakeupRecovery) {
                            ImGui::TableSetColumnIndex(colIdx++);
                            char recComboId[32];
                            snprintf(recComboId, sizeof(recComboId), "##rec%d_%d", uniqueIdPrefix, i);

                            // Invincible Wake-up can only have Normal Wakeup and Quick Roll
                            if (isOnlySpecials) {
                                if (slotCfg.wakeupOption != MK9TradeFix::WakeupRecoveryOption::NORMAL &&
                                    slotCfg.wakeupOption != MK9TradeFix::WakeupRecoveryOption::QUICK_ROLL) {
                                    slotCfg.wakeupOption = MK9TradeFix::WakeupRecoveryOption::NORMAL;
                                }
                            }
                            const char* previewRecText = MK9TradeFix::GetWakeupRecoveryOptionName(slotCfg.wakeupOption);
                            ImGui::SetNextItemWidth(-1.0f);
                            if (ImGui::BeginCombo(recComboId, previewRecText)) {
                                if (ImGui::Selectable("Normal Wakeup", slotCfg.wakeupOption == MK9TradeFix::WakeupRecoveryOption::NORMAL)) {
                                    slotCfg.wakeupOption = MK9TradeFix::WakeupRecoveryOption::NORMAL;
                                }
                                if (ImGui::Selectable("Quick Roll", slotCfg.wakeupOption == MK9TradeFix::WakeupRecoveryOption::QUICK_ROLL)) {
                                    slotCfg.wakeupOption = MK9TradeFix::WakeupRecoveryOption::QUICK_ROLL;
                                }
                                if (!isOnlySpecials) {
                                    if (ImGui::Selectable("Delayed Wakeup", slotCfg.wakeupOption == MK9TradeFix::WakeupRecoveryOption::DELAYED_WAKEUP)) {
                                        slotCfg.wakeupOption = MK9TradeFix::WakeupRecoveryOption::DELAYED_WAKEUP;
                                        if (slotCfg.holdFrames < 30 || slotCfg.holdFrames > 96) slotCfg.holdFrames = 60;
                                    }
                                    if (ImGui::Selectable("Delayed Wakeup Roll", slotCfg.wakeupOption == MK9TradeFix::WakeupRecoveryOption::DELAYED_WAKEUP_ROLL)) {
                                        slotCfg.wakeupOption = MK9TradeFix::WakeupRecoveryOption::DELAYED_WAKEUP_ROLL;
                                        if (slotCfg.holdFrames < 30 || slotCfg.holdFrames > 96) slotCfg.holdFrames = 60;
                                    }
                                }
                                ImGui::EndCombo();
                            }
                        }

                        // Col: Categorized Preset Action Dropdown
                        ImGui::TableSetColumnIndex(colIdx++);
                        char slotComboId[32];
                        snprintf(slotComboId, sizeof(slotComboId), "##combo%d_%d", uniqueIdPrefix, i);
                        const char* previewText = MK9TradeFix::GetReversalActionName(slotCfg.actionType);
                        ImGui::SetNextItemWidth(-1.0f);
                        if (ImGui::BeginCombo(slotComboId, previewText)) {
                            auto RenderCategoryHeader = [](const char* header) {
                                ImGui::Spacing();
                                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "--- %s ---", header);
                                ImGui::Separator();
                            };

                            auto RenderActionItem = [&](MK9TradeFix::ReversalActionType act) {
                                bool isSelected = (slotCfg.actionType == act);
                                const char* actName = MK9TradeFix::GetReversalActionName(act);
                                if (ImGui::Selectable(actName, isSelected)) {
                                    slotCfg.actionType = act;
                                }
                                if (isSelected) {
                                    ImGui::SetItemDefaultFocus();
                                }
                            };

                            RenderCategoryHeader("NEUTRAL (DO NOTHING / STAND)");
                            RenderActionItem(MK9TradeFix::ReversalActionType::ACTION_NEUTRAL);

                            if (isOnlySpecials) {
                                RenderCategoryHeader("EX SPECIAL MOVES (ARMOR & WAKEUP INVULNERABILITY)");
                                for (int a = (int)MK9TradeFix::ReversalActionType::EX_DF1; a <= (int)MK9TradeFix::ReversalActionType::EX_DBF3; a++) {
                                    RenderActionItem((MK9TradeFix::ReversalActionType)a);
                                }

                                RenderCategoryHeader("SPECIAL MOVES (NATIVE WAKEUP ATTACK WINDOW)");
                                for (int a = (int)MK9TradeFix::ReversalActionType::SPEC_DF1; a <= (int)MK9TradeFix::ReversalActionType::SPEC_DBF3; a++) {
                                    RenderActionItem((MK9TradeFix::ReversalActionType)a);
                                }
                            } else {
                                RenderCategoryHeader("EX SPECIAL MOVES (ARMOR & WAKEUP INVULNERABILITY)");
                                for (int a = (int)MK9TradeFix::ReversalActionType::EX_DF1; a <= (int)MK9TradeFix::ReversalActionType::EX_DBF3; a++) {
                                    RenderActionItem((MK9TradeFix::ReversalActionType)a);
                                }

                                RenderCategoryHeader("SPECIAL MOVES (NATIVE WAKEUP ATTACK WINDOW)");
                                for (int a = (int)MK9TradeFix::ReversalActionType::SPEC_DF1; a <= (int)MK9TradeFix::ReversalActionType::SPEC_DBF3; a++) {
                                    RenderActionItem((MK9TradeFix::ReversalActionType)a);
                                }

                                RenderCategoryHeader("NORMAL ATTACKS (FRAME 1 NEUTRAL RECOVERY)");
                                for (int a = (int)MK9TradeFix::ReversalActionType::NORM_S1; a <= (int)MK9TradeFix::ReversalActionType::NORM_THROW; a++) {
                                    RenderActionItem((MK9TradeFix::ReversalActionType)a);
                                }

                                RenderCategoryHeader("GENERAL MOVEMENT & DEFENSE (FRAME 1)");
                                for (int a = (int)MK9TradeFix::ReversalActionType::MOVE_WALK_FWD; a <= (int)MK9TradeFix::ReversalActionType::MOVE_CROUCH_BLOCK; a++) {
                                    RenderActionItem((MK9TradeFix::ReversalActionType)a);
                                }

                                RenderCategoryHeader("X-RAY / SUPER (FRAME 1)");
                                RenderActionItem(MK9TradeFix::ReversalActionType::SUPER_XRAY);
                            }

                            ImGui::EndCombo();
                        }

                        // Col: Weight (Selection Frequency)
                        ImGui::TableSetColumnIndex(colIdx++);
                        char weightInputId[32];
                        snprintf(weightInputId, sizeof(weightInputId), "##wt%d_%d", uniqueIdPrefix, i);
                        ImGui::SetNextItemWidth(-1.0f);
                        ImGui::SliderInt(weightInputId, &slotCfg.weight, 1, 10, "Weight: %d");
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Selection frequency / chance when multiple slots are ON.");
                        }

                        // Col: Hold (Delayed Wakeup ground hold duration: 30F to 96F)
                        if (hasHoldColumn) {
                            ImGui::TableSetColumnIndex(colIdx++);
                            char holdInputId[32];
                            snprintf(holdInputId, sizeof(holdInputId), "##hld%d_%d", uniqueIdPrefix, i);
                            ImGui::SetNextItemWidth(-1.0f);

                            bool isDelayed = (slotCfg.wakeupOption == MK9TradeFix::WakeupRecoveryOption::DELAYED_WAKEUP ||
                                              slotCfg.wakeupOption == MK9TradeFix::WakeupRecoveryOption::DELAYED_WAKEUP_ROLL);
                            if (isDelayed) {
                                if (slotCfg.holdFrames < 30 || slotCfg.holdFrames > 96) slotCfg.holdFrames = 60;
                                char hldFmt[32];
                                snprintf(hldFmt, sizeof(hldFmt), "%dF", slotCfg.holdFrames);
                                ImGui::SliderInt(holdInputId, &slotCfg.holdFrames, 30, 96, hldFmt);
                                if (ImGui::IsItemHovered()) {
                                    ImGui::SetTooltip("Duration to stay grounded on floor for Delayed Wakeup (30F to 96F max).");
                                }
                            } else {
                                ImGui::BeginDisabled();
                                int dummyHold = 0;
                                ImGui::SliderInt(holdInputId, &dummyHold, 0, 0, "  --  ");
                                ImGui::EndDisabled();
                                if (ImGui::IsItemHovered()) {
                                    ImGui::SetTooltip("Hold duration only applies to Delayed Wakeup and Delayed Wakeup Roll.");
                                }
                            }
                        }

                        // Col: Delay (Reversal action execution delay: 0F to 30F)
                        if (hasDelayColumn) {
                            ImGui::TableSetColumnIndex(colIdx++);
                            char delayInputId[32];
                            snprintf(delayInputId, sizeof(delayInputId), "##dly%d_%d", uniqueIdPrefix, i);
                            ImGui::SetNextItemWidth(-1.0f);
                            if (slotCfg.delayFrames < 0 || slotCfg.delayFrames > 30) slotCfg.delayFrames = 0;
                            char dlyFmt[32];
                            snprintf(dlyFmt, sizeof(dlyFmt), "%dF", slotCfg.delayFrames);
                            ImGui::SliderInt(delayInputId, &slotCfg.delayFrames, 0, 30, dlyFmt);
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("Reversal action execution delay upon getup recovery (0F = Frame 1 Reversal).");
                            }
                        } else {
                            slotCfg.delayFrames = 0; // Invincible Wakeup cannot be delayed
                        }
                    }
                    ImGui::EndTable();
                }
            };

            auto RenderTelemetryBox = [&](const char* modeDesc) {
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08f, 0.08f, 0.12f, 1.0f));
                ImGui::BeginChild("WakeupTelemetryChild", ImVec2(0, 54), true);
                
                auto phase = rev.GetWakeupPhase();
                const char* phaseText = "IDLE / READY";
                ImVec4 phaseCol = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
                if (phase == MK9TradeFix::WakeupTelemetryPhase::AIRBORNE_FALLING) {
                    phaseText = "AIRBORNE KNOCKDOWN (Falling)";
                    phaseCol = ImVec4(1.0f, 0.8f, 0.2f, 1.0f);
                } else if (phase == MK9TradeFix::WakeupTelemetryPhase::GROUND_KNOCKDOWN) {
                    phaseText = "GROUNDED KNOCKDOWN (Floor Settle)";
                    phaseCol = ImVec4(0.3f, 0.7f, 1.0f, 1.0f);
                } else if (phase == MK9TradeFix::WakeupTelemetryPhase::DELAYED_GROUND_HOLD) {
                    phaseText = "DELAYED WAKEUP (Holding Down on Floor)";
                    phaseCol = ImVec4(0.07f, 0.85f, 0.75f, 1.0f);
                } else if (phase == MK9TradeFix::WakeupTelemetryPhase::QUICK_ROLLING) {
                    phaseText = "FORCE ROLL MODE (Tech Roll across Floor)";
                    phaseCol = ImVec4(1.0f, 0.6f, 0.2f, 1.0f);
                } else if (phase == MK9TradeFix::WakeupTelemetryPhase::RISING_WAKEUP_WINDOW) {
                    phaseText = "RISING GET-UP (WAKEUP ATTACK WINDOW)";
                    phaseCol = ImVec4(0.2f, 1.0f, 0.4f, 1.0f);
                } else if (phase == MK9TradeFix::WakeupTelemetryPhase::REVERSAL_EXECUTING) {
                    phaseText = "REVERSAL ACTION EXECUTED";
                    phaseCol = ImVec4(0.2f, 1.0f, 0.8f, 1.0f);
                }

                ImGui::Text("Wakeup State:");
                ImGui::SameLine();
                ImGui::TextColored(phaseCol, "%s", phaseText);
                ImGui::TextColored(ImVec4(0.8f, 0.9f, 1.0f, 1.0f), "%s", modeDesc);

                ImGui::EndChild();
                ImGui::PopStyleColor();
                ImGui::Spacing();
            };

            // TAB 0: INVINCIBLE WAKE-UP
            if (ImGui::BeginTabItem(categoryTitles[0])) {
                rev.SetActiveWakeupCategory(MK9TradeFix::WakeupCategory::INVINCIBLE);
                ImGui::Spacing();
                RenderTelemetryBox("Mode: True Wakeup Attack (Specials / EX Specials -> Frame 1 Invulnerability + 'WAKE UP ATTACK' banner)");

                auto& catConfig = rev.GetConfig(MK9TradeFix::ReversalType::WAKEUP_INVINCIBLE);
                RenderSlotTable(catConfig, 0, true, true);
                ImGui::Spacing();
                ImGui::TextDisabled("Invincible Wake-up: Special move triggers with 3F pre-buffer on getup with native MK9 invulnerability armor & 'WAKE UP ATTACK' property.");
                ImGui::EndTabItem();
            }

#if 0 // Preserved for future use: Quick Roll & Delayed Wakeup UI Tabs
            // QUICK ROLL (Preserved for future use)
            if (ImGui::BeginTabItem("Quick Roll")) {
                ImGui::Spacing();
                RenderTelemetryBox("Mode: Quick Roll (Tech Roll triggered by F, B during knockdown getup window)");
                auto& qrCfg = rev.GetQuickRollSettings();
                
                ImGui::Checkbox("Enable Quick Roll (Tech Roll on Knockdown via F, B)", &qrCfg.enabled);
                ImGui::Spacing();

                ImGui::TextColored(ImVec4(0.4f, 0.9f, 1.0f, 1.0f), "Trigger Mechanic: Injects Forward -> Back (F, B) on knockdown contact.");
                ImGui::Spacing();

                ImGui::Text("Post-Roll Reversal Option:");
                ImGui::SameLine();
                int revModeIdx = (int)qrCfg.reversalMode;
                if (ImGui::RadioButton("None (Pure Roll)##qrMode", revModeIdx == 0)) qrCfg.reversalMode = MK9TradeFix::QuickRollReversalMode::NONE;
                ImGui::SameLine();
                if (ImGui::RadioButton("Regular Reversal##qrMode", revModeIdx == 1)) qrCfg.reversalMode = MK9TradeFix::QuickRollReversalMode::REGULAR_REVERSAL;
                ImGui::SameLine();
                if (ImGui::RadioButton("Invincible Special Move##qrMode", revModeIdx == 2)) qrCfg.reversalMode = MK9TradeFix::QuickRollReversalMode::INVINCIBLE_SPECIAL;
                ImGui::Spacing();

                if (qrCfg.reversalMode == MK9TradeFix::QuickRollReversalMode::INVINCIBLE_SPECIAL) {
                    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "Post-Roll Invincible Special Slots:");
                    auto& postRollInv = rev.GetConfig(MK9TradeFix::ReversalType::QUICK_ROLL_INVINCIBLE);
                    RenderSlotTable(postRollInv, 10, true, false);
                } else if (qrCfg.reversalMode == MK9TradeFix::QuickRollReversalMode::REGULAR_REVERSAL) {
                    ImGui::TextColored(ImVec4(0.8f, 0.9f, 1.0f, 1.0f), "Post-Roll Regular Reversal Slots:");
                    auto& postRollReg = rev.GetConfig(MK9TradeFix::ReversalType::QUICK_ROLL_REGULAR);
                    RenderSlotTable(postRollReg, 11, false, false);
                }

                ImGui::Spacing();
                ImGui::TextDisabled("Quick Roll: Feeds F, B during the knockdown getup window, then executes the selected post-roll reversal upon roll recovery.");
                ImGui::EndTabItem();
            }

            // DELAYED WAKEUP (Preserved for future use)
            if (ImGui::BeginTabItem("Delayed Wakeup")) {
                ImGui::Spacing();
                RenderTelemetryBox("Mode: Delayed Wakeup (Stay grounded on floor up to 2 seconds)");
                auto& delCfg = rev.GetDelayedWakeupSettings();

                ImGui::Checkbox("Enable Delayed Wakeup (Hold Down on Floor)", &delCfg.enabled);
                ImGui::Spacing();

                float holdSec = delCfg.holdFrames / 60.0f;
                char holdFmt[64];
                snprintf(holdFmt, sizeof(holdFmt), "%d Frames (%.2fs)", delCfg.holdFrames, holdSec);
                ImGui::SliderInt("Hold Down Duration", &delCfg.holdFrames, 1, 120, holdFmt);
                ImGui::SameLine();
                ImGui::TextDisabled("(Max: 2.0s / 120F)");
                ImGui::Spacing();

                ImGui::Checkbox("Hold Back on Release (Guard on rising)", &delCfg.holdBackOnRelease);
                ImGui::Spacing();

                ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "Delayed Regular Reversal Slots (Authentic MK9: Delayed Wakeup cannot have Invincible Armor):");
                auto& delSlots = rev.GetConfig(MK9TradeFix::ReversalType::DELAYED_WAKEUP);
                RenderSlotTable(delSlots, 20, false, false);

                ImGui::Spacing();
                ImGui::TextDisabled("Delayed Wakeup: Holds Down on the ground for up to 2 seconds. When released, executes regular reversal upon rising neutral recovery.");
                ImGui::EndTabItem();
            }
#endif

            // TAB 1: REGULAR WAKE-UP
            if (ImGui::BeginTabItem(categoryTitles[1])) {
                rev.SetActiveWakeupCategory(MK9TradeFix::WakeupCategory::REGULAR);
                ImGui::Spacing();
                RenderTelemetryBox("Mode: Regular Wakeup Reversal (Executes upon neutral recovery after standard getup)");

                auto& catConfig = rev.GetConfig(MK9TradeFix::ReversalType::WAKEUP_REGULAR);
                RenderSlotTable(catConfig, 3, false, true);
                ImGui::Spacing();
                ImGui::TextDisabled("Regular Wake-up: Reversal executes upon neutral recovery. 'Hold' sets floor hold duration (30F-96F) for Delayed options; 'Delay' sets attack execution delay (0F-30F).");
                ImGui::EndTabItem();
            }

            // TAB 2: BLOCK REVERSAL
            if (ImGui::BeginTabItem(categoryTitles[2])) {
                ImGui::Spacing();
                RenderTelemetryBox("Mode: Blockstun Reversal (Executes on Frame 1 out of blockstun)");
                auto& catConfig = rev.GetConfig(MK9TradeFix::ReversalType::BLOCK);
                RenderSlotTable(catConfig, 4, false, false);
                ImGui::Spacing();
                ImGui::TextDisabled("Block Reversal: Executes on the exact first frame out of blockstun. 'Delay' sets execution delay (0F-30F).");
                ImGui::EndTabItem();
            }

            // TAB 3: REVERSAL AFTER DAMAGE
            if (ImGui::BeginTabItem(categoryTitles[3])) {
                ImGui::Spacing();
                RenderTelemetryBox("Mode: Hitstun Reversal (Executes on Frame 1 out of hitstun)");
                auto& catConfig = rev.GetConfig(MK9TradeFix::ReversalType::AFTER_DAMAGE);
                RenderSlotTable(catConfig, 5, false, false);
                ImGui::Spacing();
                ImGui::TextDisabled("Reversal After Damage: Executes on the exact first frame out of hitstun. 'Delay' sets execution delay (0F-30F).");
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
        if (shouldDisable) ImGui::EndDisabled();
    }

    static void RenderConsoleUI() {
        if (!g_ConsoleOpen) return;

        ImGui::SetNextWindowSize(ImVec2(800, 560), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints(ImVec2(600.0f, 380.0f), ImVec2(1920.0f, 1080.0f));
        ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowBgAlpha(g_ConsoleOpacity);

        ImGuiWindowFlags windowFlags = ImGuiWindowFlags_MenuBar;

        char windowTitle[256];
        snprintf(windowTitle, sizeof(windowTitle),
                 "MK9 Native Physical Trade Engine v0.6709 - Live In-Game Console | Extra: [%s]###MK9MainConsoleWindow",
                 MK9TradeFix::StunModifier::GetCurrentPresetName());

        if (ImGui::Begin(windowTitle, &g_ConsoleOpen, windowFlags)) {
            // Top Menu Bar
            if (ImGui::BeginMenuBar()) {
                if (ImGui::BeginMenu("View")) {
                    ImGui::MenuItem("Frame Data Visualizer (F9)", nullptr, &MK9TradeFix::g_FrameDataVisualizerEnabled);
                    ImGui::MenuItem("Auto-Scroll", nullptr, &g_AutoScroll);
                    bool unlockRes = MK9TradeFix::g_UnlockAllResolutions;
                    if (ImGui::MenuItem("Unlock 4:3 & Non-16:9 Resolutions", nullptr, &unlockRes)) {
                        MK9TradeFix::SetUnlockAllResolutions(unlockRes);
                    }
                    if (ImGui::MenuItem("Clear Log")) {
                        std::lock_guard<std::mutex> lock(g_LogMutex);
                        g_LogEntries.clear();
                    }
                    if (ImGui::MenuItem("Close Console (~)")) {
                        g_ConsoleOpen = false;
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Gameplay Fixes")) {
                    bool duckFix = MK9TradeFix::CrouchBufferFix::IsEnabled();
                    if (ImGui::MenuItem("Ducking Bug Fix", nullptr, &duckFix)) {
                        MK9TradeFix::CrouchBufferFix::SetEnabled(duckFix);
                    }
                    bool meterFix = MK9TradeFix::MeterDrainFix::IsEnabled();
                    if (ImGui::MenuItem("Meter Drain Glitch Fix", nullptr, &meterFix)) {
                        MK9TradeFix::MeterDrainFix::SetEnabled(meterFix);
                    }
                    bool isolFix = MK9TradeFix::InputIsolation::IsEnabled();
                    if (ImGui::MenuItem("Input Bleed & Cross-Talk Quarantine", nullptr, &isolFix)) {
                        MK9TradeFix::InputIsolation::SetEnabled(isolFix);
                    }
                    bool tradeFix = MK9TradeFix::IsFixEnabled();
                    if (ImGui::MenuItem("Trade Priority Fix", nullptr, &tradeFix)) {
                        MK9TradeFix::ToggleFix();
                    }
                    bool jumpFix = MK9TradeFix::JumpInputFix::IsEnabled();
                    if (ImGui::MenuItem("Jump Input Threshold Fix", nullptr, &jumpFix)) {
                        MK9TradeFix::JumpInputFix::SetEnabled(jumpFix);
                    }
                    ImGui::EndMenu();
                }

                // Display Mode Dropdown in MenuBar
                ImGui::SameLine();
                const char* displayModes[] = { "BORDERLESS", "WINDOWED", "FULLSCREEN" };
                int currentDisplayMode = (int)MK9TradeFix::g_DisplayMode;
                ImGui::TextDisabled("| Display:");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(105.0f);
                if (ImGui::Combo("##MenuBarDisplayCombo", &currentDisplayMode, displayModes, IM_ARRAYSIZE(displayModes))) {
                    MK9TradeFix::SetDisplayMode((MK9TradeFix::DisplayMode)currentDisplayMode);
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", "Window Display Mode (Cycle with F10)");

                // Right-aligned Telemetry & Opacity in MenuBar
                bool inMatchMenuBar = MK9TradeFix::IsMatchActive();
                bool isNativePausedMenuBar = MK9TradeFix::IsNativeGamePaused();
                float totalTelemetryWidth = 370.0f;
                float rightMargin = 24.0f;
                float winWidth = ImGui::GetWindowWidth();
                float currentX = ImGui::GetCursorPosX();
                float targetX = winWidth - totalTelemetryWidth - rightMargin;

                if (targetX > currentX + 10.0f) {
                    ImGui::SameLine(targetX);
                } else {
                    ImGui::SameLine();
                }

                if (!inMatchMenuBar) {
                    ImGui::TextDisabled("Frame: In Menu");
                } else {
                    ImGui::TextColored(ImVec4(0.7f, 0.75f, 0.85f, 1.0f), "Frame: %-6llu", MK9TradeFix::GetCurrentSimulationFrame());
                }

                if (inMatchMenuBar && isNativePausedMenuBar) {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f), "| [PAUSED]");
                }

                ImGui::SameLine();
                int diagMode = MK9TradeFix::GetDiagnosticMode();
                if (diagMode > 0) {
                    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f), "| Mode: %d", diagMode);
                } else {
                    ImGui::TextDisabled("| Mode: Off");
                }

                ImGui::SameLine();
                ImGui::TextDisabled("| Opacity:");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(65.0f);
                ImGui::SliderFloat("##MenuBarOpacitySlider", &g_ConsoleOpacity, 0.05f, 1.0f, "%.2f");

                ImGui::EndMenuBar();
            }

            bool inMatch = MK9TradeFix::IsMatchActive();
            bool isSimPaused = MK9TradeFix::IsGamePaused();

            // -------------------------------------------------------------
            // Master Navigation Tabs
            // -------------------------------------------------------------
            if (ImGui::BeginTabBar("MK9ConsoleMasterTabBar", ImGuiTabBarFlags_None)) {
                
                // =========================================================
                // TAB 1: Gameplay & Engine Fixes
                // =========================================================
                if (ImGui::BeginTabItem("Gameplay Fixes")) {
                    ImGui::Spacing();
                    ImGui::TextColored(ImVec4(0.4f, 0.9f, 1.0f, 1.0f), "Core Engine Gameplay Fixes:");
                    
                    bool duckFixCheck = MK9TradeFix::CrouchBufferFix::IsEnabled();
                    if (ImGui::Checkbox("Ducking Bug Fix (Prevent Standing Normal While Ducking)", &duckFixCheck)) {
                        MK9TradeFix::CrouchBufferFix::SetEnabled(duckFixCheck);
                    }
                    ImGui::SameLine();
                    ImGui::TextColored(duckFixCheck ? ImVec4(0.2f, 1.0f, 0.3f, 1.0f) : ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
                                       duckFixCheck ? "[ACTIVE]" : "[DISABLED]");
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s", "Remaps expired neutral directional tokens (0x80000000) to native down token (0x80000004) when ducking or holding Down.");
                    }

                    bool meterFixCheck = MK9TradeFix::MeterDrainFix::IsEnabled();
                    if (ImGui::Checkbox("Meter Drain Glitch Fix (Super Meter Consumed on Move Launch)", &meterFixCheck)) {
                        MK9TradeFix::MeterDrainFix::SetEnabled(meterFixCheck);
                    }
                    ImGui::SameLine();
                    ImGui::TextColored(meterFixCheck ? ImVec4(0.2f, 1.0f, 0.3f, 1.0f) : ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
                                       meterFixCheck ? "[ACTIVE]" : "[DISABLED]");
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s", "Drains EX meter at move launch to prevent losing meter upon interrupt before active frames without executing move.");
                    }
                    bool tradeFixCheck = MK9TradeFix::IsFixEnabled();
                    if (ImGui::Checkbox("Physical Trade Priority Fix", &tradeFixCheck)) {
                        MK9TradeFix::ToggleFix();
                    }
                    ImGui::SameLine();
                    ImGui::TextColored(tradeFixCheck ? ImVec4(0.2f, 1.0f, 0.3f, 1.0f) : ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
                                       tradeFixCheck ? "[ACTIVE]" : "[DISABLED]");
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s", "Restores physical hitbox clashing and mutual hit execution on simultaneous strikes.");
                    }

                    bool isolFixCheck = MK9TradeFix::InputIsolation::IsEnabled();
                    if (ImGui::Checkbox("Input Bleed & Cross-Talk Quarantine (Isolate P1/P2 Gamepads & Keys)", &isolFixCheck)) {
                        MK9TradeFix::InputIsolation::SetEnabled(isolFixCheck);
                    }
                    ImGui::SameLine();
                    ImGui::TextColored(isolFixCheck ? ImVec4(0.2f, 1.0f, 0.3f, 1.0f) : ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
                                       isolFixCheck ? "[ACTIVE]" : "[DISABLED]");
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s", "Prevents rapid button mashing on Player 2 (or Player 1) from bleeding into the opponent by quarantining device queries and disabling global fallback scanning.");
                    }

                    bool jumpFixCheck = MK9TradeFix::JumpInputFix::IsEnabled();
                    if (ImGui::Checkbox("Jump Input Threshold Fix", &jumpFixCheck)) {
                        MK9TradeFix::JumpInputFix::SetEnabled(jumpFixCheck);
                    }
                    ImGui::SameLine();
                    ImGui::TextColored(jumpFixCheck ? ImVec4(0.2f, 1.0f, 0.3f, 1.0f) : ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
                                       jumpFixCheck ? "[ACTIVE]" : "[DISABLED]");
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s", "Fixes the engine quirk where 1f, 2f, and 3f taps of Up are ignored by eliminating the hardcoded 4-frame hold requirement, enabling immediate takeoff while preserving native 3-frame pre-jump squat (0x200E) and physics.");
                    }

                    ImGui::Checkbox("Runtime Console & Diagnostic Logging", &MK9TradeFix::g_LoggingEnabled);
                    ImGui::SameLine();
                    ImGui::TextColored(MK9TradeFix::g_LoggingEnabled ? ImVec4(0.2f, 1.0f, 0.3f, 1.0f) : ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
                                       MK9TradeFix::g_LoggingEnabled ? "[ACTIVE]" : "[MUTED]");
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s", "Enable or disable tick-by-tick combat tracing and console log ingestion. Turn OFF to maximize FPS during long sessions.");
                    }

                    ImGui::Separator();
                    ImGui::TextColored(ImVec4(0.4f, 0.9f, 1.0f, 1.0f), "Release Check / Negative Edge Subsystem:");
                    ImGui::Checkbox("Player 1 Negative Edge", &MK9TradeFix::g_P1_NegativeEdgeEnabled);
                    ImGui::SameLine(320);
                    ImGui::Checkbox("Player 2 Negative Edge", &MK9TradeFix::g_P2_NegativeEdgeEnabled);
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("When Negative Edge is OFF: Special moves require a true downstroke button press rather than triggering on release.");
                    }

                    ImGui::Separator();
                    ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.2f, 1.0f), "Dan Forden 'Toasty!' Meter Gain Training Subsystem:");
                    bool toastyCheck = MK9TradeFix::ToastyTraining::IsEnabled();
                    if (ImGui::Checkbox("100% Uppercut 'Toasty!' Cameo (Secret Input = 100% Super Meter)", &toastyCheck)) {
                        MK9TradeFix::ToastyTraining::SetEnabled(toastyCheck);
                    }
                    ImGui::SameLine();
                    ImGui::TextColored(toastyCheck ? ImVec4(0.2f, 1.0f, 0.3f, 1.0f) : ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
                                       toastyCheck ? "[ACTIVE]" : "[DISABLED]");
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s", "Guarantees Dan Forden's iconic 'Toasty!' cameo on every landed Uppercut (D+2).\nInput your character's secret TOASTY BOOST sequence (or Down) within the active cameo window (~40 frames) to receive a full 100% Super Meter Boost!");
                    }

                    if (!inMatch) ImGui::BeginDisabled();
                    if (ImGui::Button("Trigger 'Toasty!' Cameo Now")) {
                        MK9TradeFix::ToastyTraining::TriggerToastyCameo(true);
                    }
                    if (ImGui::IsItemHovered() && !inMatch) {
                        ImGui::SetTooltip("%s", "Triggering Toasty cameo requires an active match");
                    }
                    ImGui::SameLine();
                    ImGui::TextDisabled("(Invokes native 0x008D6180 dispatch directly)");
                    if (!inMatch) ImGui::EndDisabled();

                    // -------------------------------------------------------------
                    // Dan Forden Customization (Scale, Corners, Auto-Flip)
                    // -------------------------------------------------------------
                    ImGui::Spacing();
                    ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "Dan Forden Appearance & Screen Placement:");

                    bool extraToastyDisabled = MK9TradeFix::ToastyTraining::IsExtraToastySettingsDisabled();
                    if (extraToastyDisabled) {
                        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), 
                            "[!] Extra customization disabled in 720p or 4:3 resolutions (Locked to authentic vanilla)");
                        ImGui::BeginDisabled();
                    }

                    ImGui::SetNextItemWidth(260.0f);
                    if (MK9TradeFix::ToastyTraining::g_ToastyRandomScale) {
                        ImGui::BeginDisabled();
                        float dummyScale = MK9TradeFix::ToastyTraining::g_ToastyScale;
                        ImGui::SliderFloat("Dan Forden Size##ToastyScale", &dummyScale, 1.00f, 3.80f, "Random (1.0x-3.8x)");
                        ImGui::EndDisabled();
                    } else {
                        if (ImGui::SliderFloat("Dan Forden Size##ToastyScale", &MK9TradeFix::ToastyTraining::g_ToastyScale, 1.00f, 3.80f, "%.2fx")) {
                            if (fabs(MK9TradeFix::ToastyTraining::g_ToastyScale - 1.00f) > 0.01f) {
                                MK9TradeFix::ToastyTraining::g_ToastyAutoFlip = true;
                                MK9TradeFix::ToastyTraining::g_ToastyFlipTop = true;
                            }
                        }
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("%s", "Adjust the visual size of Dan Forden (1.00x = Vanilla default, up to 3.80x giant size).");
                        }
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Reset Size##ResetToastyScale")) {
                        MK9TradeFix::ToastyTraining::g_ToastyScale = 1.00f;
                        MK9TradeFix::ToastyTraining::g_ToastyRandomScale = false;
                    }
                    ImGui::SameLine();
                    if (ImGui::Checkbox("Random Size##ToastyRandomScale", &MK9TradeFix::ToastyTraining::g_ToastyRandomScale)) {
                        if (MK9TradeFix::ToastyTraining::g_ToastyRandomScale) {
                            MK9TradeFix::ToastyTraining::g_ToastyAutoFlip = true;
                            MK9TradeFix::ToastyTraining::g_ToastyFlipTop = true;
                        }
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s", "Picks a random size for Dan Forden (between 1.00x and 3.80x) dynamically whenever he appears.");
                    }

                    const char* cornerNames[] = {
                        "Bottom-Right (Vanilla)",
                        "Bottom-Left",
                        "Top-Right",
                        "Top-Left",
                        "Random Corners"
                    };
                    ImGui::SetNextItemWidth(260.0f);
                    if (ImGui::Combo("Cameo Corner##ToastyCorner", &MK9TradeFix::ToastyTraining::g_ToastyCorner, cornerNames, IM_ARRAYSIZE(cornerNames))) {
                        if (MK9TradeFix::ToastyTraining::g_ToastyCorner != MK9TradeFix::ToastyTraining::TOASTY_CORNER_BOTTOM_RIGHT) {
                            MK9TradeFix::ToastyTraining::g_ToastyAutoFlip = true;
                            MK9TradeFix::ToastyTraining::g_ToastyFlipTop = true;
                        }
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s", "Choose which screen corner Dan Forden appears from, or set to Random Corners.");
                    }

                    ImGui::Checkbox("Auto-Mirror (Face Inward Toward Center)##ToastyFlip", &MK9TradeFix::ToastyTraining::g_ToastyAutoFlip);
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s", "Automatically mirrors Dan Forden horizontally when appearing from left corners so he faces inward toward the fighters.");
                    }

                    ImGui::SameLine();
                    ImGui::Checkbox("Auto-Flip (Upside-Down in Top Corners)##ToastyFlipTop", &MK9TradeFix::ToastyTraining::g_ToastyFlipTop);
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s", "Automatically flips Dan Forden upside-down when appearing from top corners so he peeks down from the ceiling.");
                    }

                    if (extraToastyDisabled) {
                        ImGui::EndDisabled();
                    }

                    // -------------------------------------------------------------
                    // Dynamic Active Character Detection
                    // -------------------------------------------------------------
                    void* p1Char = MK9TradeFix::GetP1Char();
                    void* p2Char = MK9TradeFix::GetP2Char();
                    const auto* p1Boost = MK9TradeFix::ToastyTraining::GetCharacterToastyBoost(p1Char);
                    const auto* p2Boost = MK9TradeFix::ToastyTraining::GetCharacterToastyBoost(p2Char);
                    const char* p1Name = MK9TradeFix::ToastyTraining::DetectCharacterName(p1Char);
                    const char* p2Name = MK9TradeFix::ToastyTraining::DetectCharacterName(p2Char);

                    ImGui::Spacing();
                    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "Active Match Characters & Secret Toasty Boost Inputs:");
                    
                    ImGui::Columns(2, "ToastyActiveChars", true);
                    
                    // P1 Column
                    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "[P1 Fighter]");
                    if (p1Boost) {
                        ImGui::Text("Character: %s", p1Boost->name);
                        ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.2f, 1.0f), "Toasty Boost: %s", p1Boost->boostInput);
                        ImGui::TextDisabled("(%s)", p1Boost->inputDetails);
                    } else if (p1Name) {
                        ImGui::Text("Character: %s", p1Name);
                        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Input: Standard Down (D)");
                    } else {
                        ImGui::TextDisabled("P1: No active match fighter");
                    }
                    
                    ImGui::NextColumn();
                    
                    // P2 Column
                    ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "[P2 Fighter]");
                    if (p2Boost) {
                        ImGui::Text("Character: %s", p2Boost->name);
                        ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.2f, 1.0f), "Toasty Boost: %s", p2Boost->boostInput);
                        ImGui::TextDisabled("(%s)", p2Boost->inputDetails);
                    } else if (p2Name) {
                        ImGui::Text("Character: %s", p2Name);
                        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Input: Standard Down (D)");
                    } else {
                        ImGui::TextDisabled("P2: No active match fighter");
                    }
                    
                    ImGui::Columns(1);

                    // -------------------------------------------------------------
                    // Complete 32-Character Toasty Boost Reference Guide
                    // -------------------------------------------------------------
                    ImGui::Spacing();
                    if (ImGui::CollapsingHeader("TOASTY BOOST Roster Reference Guide (32 Characters)", ImGuiTreeNodeFlags_DefaultOpen)) {
                        static char s_FilterChar[32] = "";
                        ImGui::InputText("Filter Roster##toastyFilter", s_FilterChar, sizeof(s_FilterChar));
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Clear##toastyFilter")) {
                            s_FilterChar[0] = '\0';
                        }

                        ImGui::BeginChild("ToastyBoostList", ImVec2(0, 240), true);
                        ImGui::Columns(3, "ToastyGuideCols", true);
                        ImGui::SetColumnWidth(0, 140);
                        ImGui::SetColumnWidth(1, 130);
                        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Character");
                        ImGui::NextColumn();
                        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Secret Input");
                        ImGui::NextColumn();
                        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Input Details");
                        ImGui::NextColumn();
                        ImGui::Separator();

                        for (size_t i = 0; i < MK9TradeFix::ToastyTraining::g_ToastyBoostGuideCount; ++i) {
                            const auto& entry = MK9TradeFix::ToastyTraining::g_ToastyBoostGuide[i];
                            if (s_FilterChar[0] != '\0') {
                                bool matched = false;
                                for (const char* p = entry.name; *p; ++p) {
                                    if (tolower((unsigned char)*p) == tolower((unsigned char)s_FilterChar[0])) {
                                        bool subMatch = true;
                                        for (size_t f = 0; s_FilterChar[f]; ++f) {
                                            if (tolower((unsigned char)p[f]) != tolower((unsigned char)s_FilterChar[f])) {
                                                subMatch = false;
                                                break;
                                            }
                                        }
                                        if (subMatch) { matched = true; break; }
                                    }
                                }
                                if (!matched) continue;
                            }

                            bool isActiveP1 = (p1Boost && _stricmp(entry.name, p1Boost->name) == 0);
                            bool isActiveP2 = (p2Boost && _stricmp(entry.name, p2Boost->name) == 0);

                            if (isActiveP1) {
                                ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s [P1]", entry.name);
                            } else if (isActiveP2) {
                                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "%s [P2]", entry.name);
                            } else {
                                ImGui::Text("%s", entry.name);
                            }
                            ImGui::NextColumn();

                            if (isActiveP1 || isActiveP2) {
                                ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.2f, 1.0f), "%s", entry.boostInput);
                            } else {
                                ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.9f, 1.0f), "%s", entry.boostInput);
                            }
                            ImGui::NextColumn();

                            ImGui::TextDisabled("%s", entry.inputDetails);
                            ImGui::NextColumn();
                        }
                        ImGui::Columns(1);
                        ImGui::EndChild();
                    }
                    
                    ImGui::EndTabItem();
                }

                // =========================================================
                // TAB 2: Practice Dummy Recording Settings
                // =========================================================
                if (ImGui::BeginTabItem("Recording Settings")) {
                    RenderRecordingSettingsUI();
                    ImGui::EndTabItem();
                }

                // =========================================================
                // TAB 3: Practice Dummy Reversal Settings
                // =========================================================
                if (ImGui::BeginTabItem("Reversal Settings")) {
                    RenderReversalSettingsUI();
                    ImGui::EndTabItem();
                }

                // =========================================================
                // TAB 4: 3D Visualizer & Display
                // =========================================================
                if (ImGui::BeginTabItem("3D Visualizer & Display")) {
                    ImGui::Spacing();
                    ImGui::TextColored(ImVec4(0.4f, 0.9f, 1.0f, 1.0f), "UE3 Native 3D Collision Visualizer (Accurate):");
                    
                    bool drawHit = MK9TradeFix::IsDrawDebugHitboxes();
                    if (ImGui::Checkbox("Draw Attack Red Hitboxes (3D Spheres)", &drawHit)) {
                        MK9TradeFix::SetDrawDebugHitboxes(drawHit);
                    }
                    ImGui::SameLine();
                    bool drawHurt = MK9TradeFix::IsDrawDebugHurtboxes();
                    if (ImGui::Checkbox("Draw Body Green Hurtboxes (3D Spheres)", &drawHurt)) {
                        MK9TradeFix::SetDrawDebugHurtboxes(drawHurt);
                    }
                    ImGui::SameLine();
                    bool drawPush = MK9TradeFix::IsDrawDebugPushboxes();
                    if (ImGui::Checkbox("Draw Position Cyan Pushboxes (3D Cylinders)", &drawPush)) {
                        MK9TradeFix::SetDrawDebugPushboxes(drawPush);
                    }
                    ImGui::TextDisabled("Renders authentic native 3D collision wireframes directly inside Unreal Engine 3's viewport renderer.");

                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();

                    if (ImGui::CollapsingHeader("Stage Background & Floor Grid (Void Mode)", ImGuiTreeNodeFlags_DefaultOpen)) {
                        ImGui::Spacing();
                        ImGui::TextColored(ImVec4(0.4f, 0.9f, 1.0f, 1.0f), "Stage Background & Floor Grid (Void Mode):");
                        ImGui::TextDisabled("Customize stage backgrounds (Void Mode / Chroma Green Screen), 3D training floor grid, and stage corner boundaries.");
                        ImGui::Spacing();

                        // 1. Disable Stage Background (Void / Black Screen)
                        if (ImGui::Checkbox("Disable Stage Background (Void Mode)", &MK9TradeFix::ExtraGameplay::g_DisableBackground)) {
                            MK9TradeFix::ExtraGameplay::UpdateBackgroundVisibility(true);
                        }
                        ImGui::SameLine();
                        ImGui::TextColored(MK9TradeFix::ExtraGameplay::g_DisableBackground ? ImVec4(0.2f, 1.0f, 0.3f, 1.0f) : ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                                           MK9TradeFix::ExtraGameplay::g_DisableBackground ? "[ACTIVE]" : "[OFF]");
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Suppresses stage geometry, background meshes, decals, and skybox rendering, creating a clean void training arena (loads automatically when health bar appears in matches).");
                        }
                        ImGui::TextDisabled("   Hides stage meshes, background props, and scenery, leaving a clean void background (active only in 3D matches).");

                        // Void / Background Color & Colored Floor Grid
                        if (MK9TradeFix::ExtraGameplay::g_DisableBackground) {
                            ImGui::Indent(20.0f);
                            ImGui::Spacing();

                            ImGui::Text("Background Color (Green Screen / Void):");
                            ImGui::ColorEdit3("##void_bg_color", MK9TradeFix::ExtraGameplay::g_VoidBgColor,
                                              ImGuiColorEditFlags_PickerHueWheel | ImGuiColorEditFlags_DisplayRGB);
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("Choose the background color. Set to pure green (0, 1, 0) or blue for Chroma Key green/blue screen video editing!");
                            }

                            ImGui::SameLine();
                            if (ImGui::SmallButton("Pure Black##bg_black")) {
                                MK9TradeFix::ExtraGameplay::g_VoidBgColor[0] = 0.0f;
                                MK9TradeFix::ExtraGameplay::g_VoidBgColor[1] = 0.0f;
                                MK9TradeFix::ExtraGameplay::g_VoidBgColor[2] = 0.0f;
                            }
                            ImGui::SameLine();
                            if (ImGui::SmallButton("Green Screen##bg_green")) {
                                MK9TradeFix::ExtraGameplay::g_VoidBgColor[0] = 0.0f;
                                MK9TradeFix::ExtraGameplay::g_VoidBgColor[1] = 1.0f;
                                MK9TradeFix::ExtraGameplay::g_VoidBgColor[2] = 0.0f;
                            }
                            ImGui::SameLine();
                            if (ImGui::SmallButton("Blue Screen##bg_blue")) {
                                MK9TradeFix::ExtraGameplay::g_VoidBgColor[0] = 0.0f;
                                MK9TradeFix::ExtraGameplay::g_VoidBgColor[1] = 0.2f;
                                MK9TradeFix::ExtraGameplay::g_VoidBgColor[2] = 1.0f;
                            }
                            ImGui::SameLine();
                            if (ImGui::SmallButton("Magenta##bg_magenta")) {
                                MK9TradeFix::ExtraGameplay::g_VoidBgColor[0] = 1.0f;
                                MK9TradeFix::ExtraGameplay::g_VoidBgColor[1] = 0.0f;
                                MK9TradeFix::ExtraGameplay::g_VoidBgColor[2] = 1.0f;
                            }

                            ImGui::Spacing();
                            ImGui::Text("Lighting Reset & Morph Delay (Step 1 Warmup):");
                            ImGui::SliderInt("##lighting_warmup_frames", &MK9TradeFix::ExtraGameplay::g_LightingWarmupFrames, 15, 300, "%d frames");
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("Duration in frames that the stage remains visible during position resets and character form changes (morphs)\n"
                                                  "to allow Unreal Engine 3 to bake authentic ambient lighting before void mode re-engages (prevents pitch-black models).\n"
                                                  "Default: 15 frames (~0.25 sec).");
                            }
                            ImGui::SameLine();
                            if (ImGui::SmallButton("Default (15F)##warmup_default")) {
                                MK9TradeFix::ExtraGameplay::g_LightingWarmupFrames = 15;
                            }
                            ImGui::SameLine();
                            if (ImGui::SmallButton("30F##warmup_30")) {
                                MK9TradeFix::ExtraGameplay::g_LightingWarmupFrames = 30;
                            }
                            ImGui::SameLine();
                            if (ImGui::SmallButton("60F##warmup_60")) {
                                MK9TradeFix::ExtraGameplay::g_LightingWarmupFrames = 60;
                            }
                            ImGui::SameLine();
                            if (ImGui::SmallButton("90F##warmup_90")) {
                                MK9TradeFix::ExtraGameplay::g_LightingWarmupFrames = 90;
                            }

                            // Interactive manual trigger & real-time warmup stage status
                            if (ImGui::Button("Refresh Character Lighting (Warmup Now)##trigger_warmup")) {
                                MK9TradeFix::TriggerLightingProbeWarmup();
                            }
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("Manually triggers Step 1 warmup right now: briefly reveals stage to let character dynamic lighting probes re-bake.");
                            }
                            ImGui::SameLine();
                            if (MK9TradeFix::ExtraGameplay::g_DisableBackground) {
                                if (MK9TradeFix::ExtraGameplay::IsSoulStealOngoing()) {
                                    ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "[Soul Steal Move Active: Stage Force-Enabled]");
                                } else if (s_MatchActiveFrames < MK9TradeFix::ExtraGameplay::g_LightingWarmupFrames) {
                                    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f), "[Step 1: Revealing Stage (%d / %d F)]",
                                                       s_MatchActiveFrames, MK9TradeFix::ExtraGameplay::g_LightingWarmupFrames);
                                } else {
                                    ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "[Step 2: Void Mode Active (Lighting Baked)]");
                                }
                            } else {
                                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "[Normal Stage Active]");
                            }

                            ImGui::Spacing();

                            ImGui::Checkbox("Enable Colored Floor Grid", &MK9TradeFix::ExtraGameplay::g_EnableFloorGrid);
                            ImGui::SameLine();
                            ImGui::TextColored(MK9TradeFix::ExtraGameplay::g_EnableFloorGrid ? ImVec4(0.2f, 1.0f, 0.3f, 1.0f) : ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                                               MK9TradeFix::ExtraGameplay::g_EnableFloorGrid ? "[ACTIVE]" : "[OFF]");
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("Draws a 3D perspective floor grid with distance markers and center lines on the stage floor plane.");
                            }

                            if (MK9TradeFix::ExtraGameplay::g_EnableFloorGrid) {
                                ImGui::Spacing();
                                ImGui::Text("Grid Line Color (Wheel):");
                                ImGui::ColorEdit3("##floor_grid_color", MK9TradeFix::ExtraGameplay::g_FloorGridColor,
                                                  ImGuiColorEditFlags_PickerHueWheel | ImGuiColorEditFlags_DisplayRGB);
                                if (ImGui::IsItemHovered()) {
                                    ImGui::SetTooltip("Click to open the interactive color wheel / palette to customize the 3D floor grid color.");
                                }

                                ImGui::SameLine();
                                if (ImGui::SmallButton("Cyan Preset##grid_cyan")) {
                                    MK9TradeFix::ExtraGameplay::g_FloorGridColor[0] = 0.0f;
                                    MK9TradeFix::ExtraGameplay::g_FloorGridColor[1] = 0.85f;
                                    MK9TradeFix::ExtraGameplay::g_FloorGridColor[2] = 1.0f;
                                }
                                ImGui::SameLine();
                                if (ImGui::SmallButton("Green##grid_green")) {
                                    MK9TradeFix::ExtraGameplay::g_FloorGridColor[0] = 0.2f;
                                    MK9TradeFix::ExtraGameplay::g_FloorGridColor[1] = 1.0f;
                                    MK9TradeFix::ExtraGameplay::g_FloorGridColor[2] = 0.3f;
                                }
                                ImGui::SameLine();
                                if (ImGui::SmallButton("Gold##grid_gold")) {
                                    MK9TradeFix::ExtraGameplay::g_FloorGridColor[0] = 1.0f;
                                    MK9TradeFix::ExtraGameplay::g_FloorGridColor[1] = 0.8f;
                                    MK9TradeFix::ExtraGameplay::g_FloorGridColor[2] = 0.1f;
                                }
                                ImGui::SameLine();
                                if (ImGui::SmallButton("Purple##grid_purple")) {
                                    MK9TradeFix::ExtraGameplay::g_FloorGridColor[0] = 0.85f;
                                    MK9TradeFix::ExtraGameplay::g_FloorGridColor[1] = 0.25f;
                                    MK9TradeFix::ExtraGameplay::g_FloorGridColor[2] = 1.0f;
                                }

                                // Center Grid Lines Color
                                ImGui::Spacing();
                                ImGui::Text("Center Division Lines Color (u=0, v=0):");
                                ImGui::ColorEdit3("##floor_center_color", MK9TradeFix::ExtraGameplay::g_FloorGridCenterColor,
                                                  ImGuiColorEditFlags_PickerHueWheel | ImGuiColorEditFlags_DisplayRGB);
                                if (ImGui::IsItemHovered()) {
                                    ImGui::SetTooltip("Customizes the color of the stage center division line (u = 0) and the longitudinal combat axis line (v = 0).");
                                }

                                ImGui::SameLine();
                                if (ImGui::SmallButton("White (Default)##center_white")) {
                                    MK9TradeFix::ExtraGameplay::g_FloorGridCenterColor[0] = 1.0f;
                                    MK9TradeFix::ExtraGameplay::g_FloorGridCenterColor[1] = 1.0f;
                                    MK9TradeFix::ExtraGameplay::g_FloorGridCenterColor[2] = 1.0f;
                                }
                                ImGui::SameLine();
                                if (ImGui::SmallButton("Gold##center_gold")) {
                                    MK9TradeFix::ExtraGameplay::g_FloorGridCenterColor[0] = 1.0f;
                                    MK9TradeFix::ExtraGameplay::g_FloorGridCenterColor[1] = 0.85f;
                                    MK9TradeFix::ExtraGameplay::g_FloorGridCenterColor[2] = 0.1f;
                                }
                                ImGui::SameLine();
                                if (ImGui::SmallButton("Cyan##center_cyan")) {
                                    MK9TradeFix::ExtraGameplay::g_FloorGridCenterColor[0] = 0.0f;
                                    MK9TradeFix::ExtraGameplay::g_FloorGridCenterColor[1] = 1.0f;
                                    MK9TradeFix::ExtraGameplay::g_FloorGridCenterColor[2] = 1.0f;
                                }
                                ImGui::SameLine();
                                if (ImGui::SmallButton("Red##center_red")) {
                                    MK9TradeFix::ExtraGameplay::g_FloorGridCenterColor[0] = 1.0f;
                                    MK9TradeFix::ExtraGameplay::g_FloorGridCenterColor[1] = 0.2f;
                                    MK9TradeFix::ExtraGameplay::g_FloorGridCenterColor[2] = 0.2f;
                                }

                                // Corner Wall Boundaries
                                ImGui::Spacing();
                                ImGui::Checkbox("Show Corner Wall Boundaries (Dynamic Stage Bounds)", &MK9TradeFix::ExtraGameplay::g_ShowGridCornerWalls);
                                ImGui::SameLine();
                                ImGui::TextColored(MK9TradeFix::ExtraGameplay::g_ShowGridCornerWalls ? ImVec4(0.2f, 1.0f, 0.3f, 1.0f) : ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                                                   MK9TradeFix::ExtraGameplay::g_ShowGridCornerWalls ? "[ACTIVE]" : "[OFF]");
                                if (ImGui::IsItemHovered()) {
                                    ImGui::SetTooltip("Renders authentic 3D vertical barrier walls, horizontal rails, and prominent floor lines at the active stage corner boundaries.\n"
                                                      "Dynamically adapts to any arena width (e.g. +/-1300 on standard 2600 stages, +/-1600 on Bell Tower/Soul Chamber, etc.).\n"
                                                      "Dims out-of-bounds floor area so true corner limits are immediately visible in Void Mode.");
                                }

                                if (MK9TradeFix::ExtraGameplay::g_ShowGridCornerWalls) {
                                    ImGui::Indent(15.0f);
                                    ImGui::Text("Wall Boundary Color:");
                                    ImGui::ColorEdit3("##grid_wall_color", MK9TradeFix::ExtraGameplay::g_GridWallColor,
                                                      ImGuiColorEditFlags_PickerHueWheel | ImGuiColorEditFlags_DisplayRGB);
                                    ImGui::SameLine();
                                    if (ImGui::SmallButton("Crimson##wall_crimson")) {
                                        MK9TradeFix::ExtraGameplay::g_GridWallColor[0] = 1.0f;
                                        MK9TradeFix::ExtraGameplay::g_GridWallColor[1] = 0.2f;
                                        MK9TradeFix::ExtraGameplay::g_GridWallColor[2] = 0.2f;
                                    }
                                    ImGui::SameLine();
                                    if (ImGui::SmallButton("Orange##wall_orange")) {
                                        MK9TradeFix::ExtraGameplay::g_GridWallColor[0] = 1.0f;
                                        MK9TradeFix::ExtraGameplay::g_GridWallColor[1] = 0.55f;
                                        MK9TradeFix::ExtraGameplay::g_GridWallColor[2] = 0.05f;
                                    }
                                    ImGui::SameLine();
                                    if (ImGui::SmallButton("Gold##wall_gold")) {
                                        MK9TradeFix::ExtraGameplay::g_GridWallColor[0] = 1.0f;
                                        MK9TradeFix::ExtraGameplay::g_GridWallColor[1] = 0.85f;
                                        MK9TradeFix::ExtraGameplay::g_GridWallColor[2] = 0.1f;
                                    }
                                    ImGui::Unindent(15.0f);
                                }
                            }

                            ImGui::Unindent(20.0f);
                        }

                        ImGui::Spacing();
                        ImGui::Separator();
                        ImGui::Spacing();
                    }

                    ImGui::Separator();
                    ImGui::TextColored(ImVec4(0.4f, 0.9f, 1.0f, 1.0f), "Display & Window Management:");

                    bool is720pDetected = MK9TradeFix::Is720pMonitor();
                    bool is720pLocked = (MK9TradeFix::g_AutoFullscreenOn720p && is720pDetected);

                    if (ImGui::Checkbox("Lock to Fullscreen on 720p & Below Monitor (Disable Windowed)", &MK9TradeFix::g_AutoFullscreenOn720p)) {
                        if (MK9TradeFix::g_AutoFullscreenOn720p && is720pDetected) {
                            MK9TradeFix::SetDisplayMode(MK9TradeFix::DISPLAY_MODE_FULLSCREEN);
                        }
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("When enabled and a 720p or lower resolution display (<= 1280x720) is detected, windowed and borderless options are disabled and the game is locked to Fullscreen.");
                    }

                    if (is720pLocked) {
                        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f),
                                           "[720p & Below Monitor Detected (%dx%d) - Display Switching Disabled & Locked to Fullscreen]",
                                           MK9TradeFix::GetMonitorWidth(), MK9TradeFix::GetMonitorHeight());
                    }

                    if (is720pLocked) ImGui::BeginDisabled();
                    if (ImGui::Button("Borderless Windowed")) {
                        MK9TradeFix::SetDisplayMode(MK9TradeFix::DISPLAY_MODE_BORDERLESS);
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Windowed (Bordered)")) {
                        MK9TradeFix::SetDisplayMode(MK9TradeFix::DISPLAY_MODE_WINDOWED);
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Exclusive Fullscreen")) {
                        MK9TradeFix::SetDisplayMode(MK9TradeFix::DISPLAY_MODE_FULLSCREEN);
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cycle Display (F10)")) {
                        MK9TradeFix::CycleDisplayMode();
                    }
                    if (is720pLocked) ImGui::EndDisabled();

                    bool unlockResBox = MK9TradeFix::g_UnlockAllResolutions;
                    if (ImGui::Checkbox("Unlock 4:3 & Non-16:9 Resolutions in Video Options##unlock_res_box", &unlockResBox)) {
                        MK9TradeFix::SetUnlockAllResolutions(unlockResBox);
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Removes the game's hardcoded 0.15 aspect ratio filter (at 0x005692F7).\nAllows 4:3 (800x600, 1024x768, 1280x960, 1400x1050, 1600x1200), 5:4 (1280x1024), and 16:10 resolutions to appear in the in-game Video Options menu.");
                    }

                    ImGui::Separator();
                    ImGui::TextColored(ImVec4(0.4f, 0.9f, 1.0f, 1.0f), "Frame Data Timeline Bar Settings:");
                    ImGui::Checkbox("Enable Frame Data Visualizer (F9)", &MK9TradeFix::g_FrameDataVisualizerEnabled);
                    ImGui::SliderFloat("Frame Bar Opacity", &MK9TradeFix::g_FrameBarOpacity, 0.0f, 1.0f, "%.2f");
                    if (ImGui::Button("Reset Frame Bar Position (Center Bottom)")) {
                        g_ResetFrameBarPos = true;
                    }
                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "Frame Bar Color Reference & Glossary:");
                    ImGui::TextDisabled("Reference guide to identify which timeline color corresponds to each fighter state:");
                    ImGui::Spacing();

                    if (ImGui::BeginTable("FrameBarGlossaryTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
                        ImGui::TableSetupColumn("Color Swatch", ImGuiTableColumnFlags_WidthFixed, 140.0f);
                        ImGui::TableSetupColumn("State Name", ImGuiTableColumnFlags_WidthFixed, 175.0f);
                        ImGui::TableSetupColumn("Gameplay Meaning & Mechanics", ImGuiTableColumnFlags_WidthStretch);
                        ImGui::TableHeadersRow();

                        auto AddGlossaryRow = [](const ImVec4& color, const char* stateName, const char* desc) {
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            ImGui::TextColored(color, "[■■■■]");
                            ImGui::SameLine();
                            ImGui::TextColored(ImVec4(0.75f, 0.75f, 0.78f, 1.0f), "RGB(%d,%d,%d)",
                                               (int)(color.x * 255.0f + 0.5f),
                                               (int)(color.y * 255.0f + 0.5f),
                                               (int)(color.z * 255.0f + 0.5f));

                            ImGui::TableSetColumnIndex(1);
                            ImGui::TextColored(color, "%s", stateName);

                            ImGui::TableSetColumnIndex(2);
                            ImGui::TextUnformatted(desc);
                        };

                        AddGlossaryRow(ImVec4(0.92f, 0.22f, 0.49f, 1.0f), "Startup", "Move wind-up / startup frames before hitboxes become active.");
                        AddGlossaryRow(ImVec4(0.90f, 0.14f, 0.18f, 1.0f), "Active", "Active striking frames where hitboxes can connect or deal damage.");
                        AddGlossaryRow(ImVec4(0.10f, 0.53f, 0.88f, 1.0f), "Recovery", "Post-active cooldown frames where the attacker is vulnerable and cannot act.");
                        AddGlossaryRow(ImVec4(0.96f, 0.88f, 0.18f, 1.0f), "Hitstun / Knockdown", "Stunned hurt reaction, air juggle tumble, or floor knockdown stumble.");
                        AddGlossaryRow(ImVec4(0.00f, 0.84f, 0.94f, 1.0f), "Blockstun", "Defensive guard recoil frames while successfully blocking an attack.");
                        AddGlossaryRow(ImVec4(1.00f, 0.55f, 0.08f, 1.0f), "Dash / Cancel", "Forward dash, backdash, or dash-cancel mobility movement frames.");
                        AddGlossaryRow(ImVec4(0.16f, 0.84f, 0.41f, 1.0f), "Quick Roll / Tech Roll", "Fast ground recovery roll executed via Forward -> Back upon knockdown floor impact.");
                        AddGlossaryRow(ImVec4(0.71f, 0.27f, 1.00f, 1.0f), "Invincible Wake-up", "Wake-up reversal special attack executing with startup invulnerability frames.");

                        ImGui::EndTable();
                    }

                    ImGui::EndTabItem();
                }

                // =========================================================
                // TAB 3: Extras (Custom Meter, Knockdown, Move Priority)
                // =========================================================
                if (ImGui::BeginTabItem("Extras")) {
                    ImGui::Spacing();
                    RenderControllerPortConfigUI("Extras");
                    ImGui::Spacing();
                    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "Extras Subsystem & Extended Combat Controls:");
                    ImGui::Spacing();

                    // User Presets & Configuration Header
                    if (ImGui::CollapsingHeader("User Presets & Configuration (4 Slots, Save/Load, Import/Export)", ImGuiTreeNodeFlags_DefaultOpen)) {
                        ImGui::Spacing();
                        ImGui::TextColored(ImVec4(0.4f, 0.9f, 1.0f, 1.0f), "Extras User Presets (4 Slots):");
                        ImGui::TextDisabled("Save your customized combat tuning, dynamic camera settings, and meter options into 4 user presets.");
                        ImGui::Spacing();

                        static int s_SelectedPresetSlot = 0; // 0..3
                        static char s_PresetNameEdit[32] = "";
                        static int s_LastLoadedSlot = -1;

                        if (s_LastLoadedSlot != s_SelectedPresetSlot) {
                            strncpy_s(s_PresetNameEdit, sizeof(s_PresetNameEdit),
                                      MK9TradeFix::StunModifier::g_UserPresets[s_SelectedPresetSlot].name, _TRUNCATE);
                            s_LastLoadedSlot = s_SelectedPresetSlot;
                        }

                        // Slot Radio Selection
                        ImGui::Text("Active Slot:");
                        for (int s = 0; s < 4; s++) {
                            ImGui::SameLine();
                            char slotLabel[48];
                            snprintf(slotLabel, sizeof(slotLabel), "Slot %d: %s##slot_rad_%d", s + 1,
                                     MK9TradeFix::StunModifier::g_UserPresets[s].name, s);
                            if (ImGui::RadioButton(slotLabel, s_SelectedPresetSlot == s)) {
                                s_SelectedPresetSlot = s;
                                strncpy_s(s_PresetNameEdit, sizeof(s_PresetNameEdit),
                                          MK9TradeFix::StunModifier::g_UserPresets[s].name, _TRUNCATE);
                            }
                        }

                        ImGui::Spacing();
                        ImGui::InputText("Preset Name##preset_name_in", s_PresetNameEdit, sizeof(s_PresetNameEdit));

                        ImGui::Spacing();
                        char saveBtnLabel[64];
                        snprintf(saveBtnLabel, sizeof(saveBtnLabel), "Save to Slot %d##btn_save_slot", s_SelectedPresetSlot + 1);
                        if (ImGui::Button(saveBtnLabel, ImVec2(150, 26))) {
                            MK9TradeFix::StunModifier::SaveUserPreset(s_SelectedPresetSlot, s_PresetNameEdit);
                        }
                        ImGui::SameLine();
                        char loadBtnLabel[64];
                        snprintf(loadBtnLabel, sizeof(loadBtnLabel), "Load Slot %d##btn_load_slot", s_SelectedPresetSlot + 1);
                        if (ImGui::Button(loadBtnLabel, ImVec2(150, 26))) {
                            MK9TradeFix::StunModifier::LoadUserPreset(s_SelectedPresetSlot);
                        }

                        ImGui::SameLine();
                        if (ImGui::Button("Export Presets to INI##btn_export_presets", ImVec2(170, 26))) {
                            MK9TradeFix::StunModifier::ExportExtrasPresetsToFile("mk9_extras_presets.ini");
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Import Presets from INI##btn_import_presets", ImVec2(170, 26))) {
                            MK9TradeFix::StunModifier::ImportExtrasPresetsFromFile("mk9_extras_presets.ini");
                            strncpy_s(s_PresetNameEdit, sizeof(s_PresetNameEdit),
                                      MK9TradeFix::StunModifier::g_UserPresets[s_SelectedPresetSlot].name, _TRUNCATE);
                        }

                        ImGui::Spacing();
                        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "Quick Preset Actions:");
                        if (ImGui::Button("Apply Vanilla Preset (Default)##btn_quick_apply_vanilla", ImVec2(230, 26))) {
                            MK9TradeFix::StunModifier::ApplyVanillaPreset();
                        }
                        ImGui::SameLine();
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.45f, 0.10f, 0.90f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.55f, 0.15f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.70f, 0.35f, 0.05f, 1.0f));
                        if (ImGui::Button("Apply Dramatic Mode (Cam 1 FOV)##btn_quick_apply_dramatic1", ImVec2(260, 26))) {
                            MK9TradeFix::StunModifier::ApplyDramaticModePreset(1);
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Dramatic Mode (Cam 2 Angled)##btn_quick_apply_dramatic2", ImVec2(240, 26))) {
                            MK9TradeFix::StunModifier::ApplyDramaticModePreset(2);
                        }
                        ImGui::PopStyleColor(3);

                        if (MK9TradeFix::StunModifier::g_PresetStatusMsg[0] != '\0') {
                            ImGui::Spacing();
                            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f), "%s", MK9TradeFix::StunModifier::g_PresetStatusMsg);
                        }

                        ImGui::Spacing();
                        ImGui::Separator();
                        ImGui::Spacing();
                    }

                    if (ImGui::CollapsingHeader("Super Meter & Combat Meter System: Expenditure & Gains")) {
                        ImGui::Spacing();
                        ImGui::TextColored(ImVec4(0.4f, 0.9f, 1.0f, 1.0f), "Meter Expenditure Rules (Super Moves, Specials & Cancels):");
                        ImGui::TextDisabled("Customize meter costs for EX Specials, Breaker, X-Ray, raw Special Moves, and Normal Cancels (Specials & Dashes).");
                        ImGui::Spacing();

                        // 1. Super Meter Expenditure (EX, Breaker, X-Ray)
                        bool customMeter = MK9TradeFix::MeterDrainFix::IsCustomMeterCostsEnabled();
                        if (ImGui::Checkbox("Enable Custom Super Meter Expenditure (EX / Breaker / X-Ray)", &customMeter)) {
                            MK9TradeFix::MeterDrainFix::SetCustomMeterCostsEnabled(customMeter);
                        }
                        ImGui::SameLine();
                        ImGui::TextColored(customMeter ? ImVec4(0.2f, 1.0f, 0.3f, 1.0f) : ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
                                           customMeter ? "[ACTIVE]" : "[VANILLA]");
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("%s", "Enables custom thresholds and deductions for EX Specials, Kombo Breakers, and X-Ray super moves.");
                        }

                        if (customMeter) {
                            ImGui::Indent(20.0f);
                            float exCost = MK9TradeFix::MeterDrainFix::GetCustomEXCost();
                            if (ImGui::SliderFloat("EX Special Meter Cost", &exCost, 0.05f, 1.0f, "%.2f")) {
                                MK9TradeFix::MeterDrainFix::SetCustomEXCost(exCost);
                            }
                            ImGui::SameLine();
                            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f), "(%.1f%%)", exCost * 100.0f);
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("%s", "Vanilla: 0.33 (33.3% = 1 full bar). Example: 0.25 (25.0% = 4 EX moves per full meter).");
                            }

                            float brkCost = MK9TradeFix::MeterDrainFix::GetCustomBreakerCost();
                            if (ImGui::SliderFloat("Kombo Breaker Cost", &brkCost, 0.05f, 1.0f, "%.2f")) {
                                MK9TradeFix::MeterDrainFix::SetCustomBreakerCost(brkCost);
                            }
                            ImGui::SameLine();
                            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.5f, 1.0f), "(%.1f%%)", brkCost * 100.0f);
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("%s", "Vanilla: 0.67 (66.7% = 2 full bars). Example: 0.50 (50.0% = 2 Breakers per full meter).");
                            }

                            float xrayCost = MK9TradeFix::MeterDrainFix::GetCustomXRayCost();
                            if (ImGui::SliderFloat("X-Ray Super Move Cost", &xrayCost, 0.10f, 1.0f, "%.2f")) {
                                MK9TradeFix::MeterDrainFix::SetCustomXRayCost(xrayCost);
                            }
                            ImGui::SameLine();
                            ImGui::TextColored(ImVec4(0.2f, 0.9f, 1.0f, 1.0f), "(%.1f%%)", xrayCost * 100.0f);
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("%s", "Vanilla: 1.00 (100.0% = 3 full bars). Example: 0.75 (75.0%), 0.50 (50.0%).");
                            }
                            ImGui::Unindent(20.0f);
                        }

                        ImGui::Spacing();

                        // 2. Combat Move & Cancel Expenditure
                        // 2a. Raw Special Move Meter Cost
                        ImGui::Checkbox("Special Move Expenditure##special_meter_cost_enable", &MK9TradeFix::MeterDrainFix::g_EnableSpecialMoveMeterCost);
                        ImGui::SameLine();
                        ImGui::TextColored(MK9TradeFix::MeterDrainFix::g_EnableSpecialMoveMeterCost ? ImVec4(0.2f, 1.0f, 0.3f, 1.0f) : ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                                           MK9TradeFix::MeterDrainFix::g_EnableSpecialMoveMeterCost ? "[ACTIVE]" : "[OFF]");
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Deducts a flat amount of super meter whenever a neutral / raw special move is executed.");
                        }

                        if (MK9TradeFix::MeterDrainFix::g_EnableSpecialMoveMeterCost) {
                            ImGui::Indent(20.0f);
                            ImGui::SliderFloat("Special Move Cost##special_meter_cost",
                                               &MK9TradeFix::MeterDrainFix::g_SpecialMoveMeterCost, 0.000f, 0.500f, "-%.3f per special");
                            ImGui::SameLine();
                            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "(-%.1f%%)", MK9TradeFix::MeterDrainFix::g_SpecialMoveMeterCost * 100.0f);
                            ImGui::Unindent(20.0f);
                        }

                        ImGui::Spacing();

                        // 2b. Throw Attack Expenditure
                        ImGui::Checkbox("Throw Attack Expenditure##throw_meter_cost_enable", &MK9TradeFix::MeterDrainFix::g_EnableNormalToDashCancelCost);
                        ImGui::SameLine();
                        ImGui::TextColored(MK9TradeFix::MeterDrainFix::g_EnableNormalToDashCancelCost ? ImVec4(0.2f, 1.0f, 0.3f, 1.0f) : ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                                           MK9TradeFix::MeterDrainFix::g_EnableNormalToDashCancelCost ? "[ACTIVE]" : "[OFF]");
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Deducts super meter whenever a throw / grab attack is executed.");
                        }

                        if (MK9TradeFix::MeterDrainFix::g_EnableNormalToDashCancelCost) {
                            ImGui::Indent(20.0f);
                            ImGui::SliderFloat("Throw Meter Cost##throw_meter_cost",
                                               &MK9TradeFix::MeterDrainFix::g_NormalToDashCancelCost, 0.000f, 0.500f, "-%.3f per throw");
                            ImGui::SameLine();
                            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "(-%.1f%%)", MK9TradeFix::MeterDrainFix::g_NormalToDashCancelCost * 100.0f);
                            ImGui::Unindent(20.0f);
                        }

                        ImGui::Spacing();
                        ImGui::Separator();
                        ImGui::Spacing();

                        // Combat Meter Gains
                        ImGui::TextColored(ImVec4(0.4f, 0.9f, 1.0f, 1.0f), "Combat Meter Generation (Hit, Block & Damage Taken Gains):");
                        ImGui::TextDisabled("Customize base meter awarded to attacker on landed strikes, defender on blocked attacks, and defender on taking damage.");
                        ImGui::Spacing();

                        // 1. Attacker Hit Meter Gain
                        ImGui::Checkbox("Attacker Hit Meter Gain##att_hit_meter_enable", &MK9TradeFix::MeterDrainFix::g_EnableAttackerHitMeterGain);
                        ImGui::SameLine();
                        ImGui::TextColored(MK9TradeFix::MeterDrainFix::g_EnableAttackerHitMeterGain ? ImVec4(0.2f, 1.0f, 0.3f, 1.0f) : ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                                           MK9TradeFix::MeterDrainFix::g_EnableAttackerHitMeterGain ? "[ACTIVE]" : "[VANILLA]");
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Grants additional super meter directly to the attacking character upon connecting with clean attacks.");
                        }

                        if (MK9TradeFix::MeterDrainFix::g_EnableAttackerHitMeterGain) {
                            ImGui::Indent(20.0f);
                            ImGui::SliderFloat("Normal Attack Meter Reward##att_hit_norm",
                                               &MK9TradeFix::MeterDrainFix::g_AttackerHitMeterRewardNormals, 0.000f, 0.150f, "+%.3f per normal");
                            ImGui::SameLine();
                            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f), "(+%.1f%%)", MK9TradeFix::MeterDrainFix::g_AttackerHitMeterRewardNormals * 100.0f);

                            ImGui::SliderFloat("Special Attack Meter Reward##att_hit_spec",
                                               &MK9TradeFix::MeterDrainFix::g_AttackerHitMeterRewardSpecials, 0.000f, 0.150f, "+%.3f per special");
                            ImGui::SameLine();
                            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f), "(+%.1f%%)", MK9TradeFix::MeterDrainFix::g_AttackerHitMeterRewardSpecials * 100.0f);
                            ImGui::Unindent(20.0f);
                        }

                        ImGui::Spacing();

                        // 2. Defender Block Meter Gain (Meter on Block)
                        ImGui::Checkbox("Defender Block Meter Gain (Meter-on-Block)##def_block_meter_enable", &MK9TradeFix::MeterDrainFix::g_EnableDefenderBlockMeterGain);
                        ImGui::SameLine();
                        ImGui::TextColored(MK9TradeFix::MeterDrainFix::g_EnableDefenderBlockMeterGain ? ImVec4(0.2f, 1.0f, 0.3f, 1.0f) : ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                                           MK9TradeFix::MeterDrainFix::g_EnableDefenderBlockMeterGain ? "[ACTIVE]" : "[DEFAULT (0% on Block)]");
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("In vanilla MK9, defenders receive 0 meter when blocking strikes.\nWhen enabled, rewards defensive blocking with super meter.");
                        }

                        if (MK9TradeFix::MeterDrainFix::g_EnableDefenderBlockMeterGain) {
                            ImGui::Indent(20.0f);
                            ImGui::SliderFloat("Normal Block Meter Reward##def_blk_norm",
                                               &MK9TradeFix::MeterDrainFix::g_DefenderBlockMeterRewardNormals, 0.000f, 0.100f, "+%.3f per normal block");
                            ImGui::SameLine();
                            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.5f, 1.0f), "(+%.1f%%)", MK9TradeFix::MeterDrainFix::g_DefenderBlockMeterRewardNormals * 100.0f);

                            ImGui::SliderFloat("Special Block Meter Reward##def_blk_spec",
                                               &MK9TradeFix::MeterDrainFix::g_DefenderBlockMeterRewardSpecials, 0.000f, 0.100f, "+%.3f per special block");
                            ImGui::SameLine();
                            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.5f, 1.0f), "(+%.1f%%)", MK9TradeFix::MeterDrainFix::g_DefenderBlockMeterRewardSpecials * 100.0f);
                            ImGui::Unindent(20.0f);
                        }

                        ImGui::Spacing();

                        // 3. Defender Hit Meter Gain (Damage Taken / On Getting Hit)
                        ImGui::Checkbox("Defender Hit Meter Gain (Damage Taken)##def_hit_meter_enable", &MK9TradeFix::MeterDrainFix::g_EnableDefenderHitMeterGain);
                        ImGui::SameLine();
                        ImGui::TextColored(MK9TradeFix::MeterDrainFix::g_EnableDefenderHitMeterGain ? ImVec4(0.2f, 1.0f, 0.3f, 1.0f) : ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                                           MK9TradeFix::MeterDrainFix::g_EnableDefenderHitMeterGain ? "[ACTIVE]" : "[DEFAULT (Damage Ratio)]");
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Awards additional bonus super meter to the defender upon receiving damage from enemy attacks (on top of vanilla damage-taken meter).");
                        }

                        if (MK9TradeFix::MeterDrainFix::g_EnableDefenderHitMeterGain) {
                            ImGui::Indent(20.0f);
                            ImGui::SliderFloat("Normal Hit Taken Meter Reward##def_hit_norm",
                                               &MK9TradeFix::MeterDrainFix::g_DefenderHitMeterRewardNormals, 0.000f, 0.150f, "+%.3f per normal taken");
                            ImGui::SameLine();
                            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.5f, 1.0f), "(+%.1f%%)", MK9TradeFix::MeterDrainFix::g_DefenderHitMeterRewardNormals * 100.0f);

                            ImGui::SliderFloat("Special Hit Taken Meter Reward##def_hit_spec",
                                               &MK9TradeFix::MeterDrainFix::g_DefenderHitMeterRewardSpecials, 0.000f, 0.150f, "+%.3f per special taken");
                            ImGui::SameLine();
                            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.5f, 1.0f), "(+%.1f%%)", MK9TradeFix::MeterDrainFix::g_DefenderHitMeterRewardSpecials * 100.0f);
                            ImGui::Unindent(20.0f);
                        }

                        ImGui::Spacing();
                        ImGui::Separator();
                        ImGui::Spacing();

                        // 4. Normal Attack Cancel Meter Depletion Options
                        if (ImGui::TreeNode("Normal Attack Cancel Meter Expenditure Options")) {
                            ImGui::Spacing();
                            ImGui::TextDisabled("Configure added meter depletion when canceling out of normal attacks:");
                            ImGui::Spacing();

                            // 1. Normal -> Special Cancel
                            ImGui::Checkbox("Normal -> Special Move Cancel Cost##norm_spec_enable", &MK9TradeFix::MeterDrainFix::g_EnableNormalToSpecialCancelCost);
                            ImGui::SameLine();
                            ImGui::TextColored(MK9TradeFix::MeterDrainFix::g_EnableNormalToSpecialCancelCost ? ImVec4(0.2f, 1.0f, 0.3f, 1.0f) : ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                                               MK9TradeFix::MeterDrainFix::g_EnableNormalToSpecialCancelCost ? "[ACTIVE]" : "[OFF]");
                            if (MK9TradeFix::MeterDrainFix::g_EnableNormalToSpecialCancelCost) {
                                ImGui::Indent(20.0f);
                                ImGui::SliderFloat("Special Cancel Cost##spec_cost",
                                                   &MK9TradeFix::MeterDrainFix::g_NormalToSpecialCancelCost, 0.000f, 0.500f, "-%.3f per cancel");
                                ImGui::SameLine();
                                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "(-%.1f%% / %.2f bars)",
                                                   MK9TradeFix::MeterDrainFix::g_NormalToSpecialCancelCost * 100.0f,
                                                   MK9TradeFix::MeterDrainFix::g_NormalToSpecialCancelCost * 3.0f);
                                ImGui::Unindent(20.0f);
                            }
                            ImGui::Spacing();

                            // 2. Normal -> EX Special Cancel
                            ImGui::Checkbox("Normal -> EX Special Cancel Added Cost##norm_ex_enable", &MK9TradeFix::MeterDrainFix::g_EnableNormalToEXCancelCost);
                            ImGui::SameLine();
                            ImGui::TextColored(MK9TradeFix::MeterDrainFix::g_EnableNormalToEXCancelCost ? ImVec4(0.2f, 1.0f, 0.3f, 1.0f) : ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                                               MK9TradeFix::MeterDrainFix::g_EnableNormalToEXCancelCost ? "[ACTIVE]" : "[OFF]");
                            if (MK9TradeFix::MeterDrainFix::g_EnableNormalToEXCancelCost) {
                                ImGui::Indent(20.0f);
                                ImGui::SliderFloat("EX Cancel Extra Cost##ex_cancel_cost",
                                                   &MK9TradeFix::MeterDrainFix::g_NormalToEXCancelCost, 0.000f, 0.500f, "-%.3f added");
                                ImGui::SameLine();
                                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "(-%.1f%% / +%.2f bars extra)",
                                                   MK9TradeFix::MeterDrainFix::g_NormalToEXCancelCost * 100.0f,
                                                   MK9TradeFix::MeterDrainFix::g_NormalToEXCancelCost * 3.0f);
                                ImGui::Unindent(20.0f);
                            }
                            ImGui::Spacing();

                            // 3. Normal -> X-Ray Cancel
                            ImGui::Checkbox("Normal -> X-Ray Super Cancel Added Cost##norm_xray_enable", &MK9TradeFix::MeterDrainFix::g_EnableNormalToXRayCancelCost);
                            ImGui::SameLine();
                            ImGui::TextColored(MK9TradeFix::MeterDrainFix::g_EnableNormalToXRayCancelCost ? ImVec4(0.2f, 1.0f, 0.3f, 1.0f) : ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                                               MK9TradeFix::MeterDrainFix::g_EnableNormalToXRayCancelCost ? "[ACTIVE]" : "[OFF]");
                            if (MK9TradeFix::MeterDrainFix::g_EnableNormalToXRayCancelCost) {
                                ImGui::Indent(20.0f);
                                ImGui::SliderFloat("X-Ray Cancel Extra Cost##xray_cancel_cost",
                                                   &MK9TradeFix::MeterDrainFix::g_NormalToXRayCancelCost, 0.000f, 0.500f, "-%.3f added");
                                ImGui::SameLine();
                                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "(-%.1f%% / +%.2f bars extra)",
                                                   MK9TradeFix::MeterDrainFix::g_NormalToXRayCancelCost * 100.0f,
                                                   MK9TradeFix::MeterDrainFix::g_NormalToXRayCancelCost * 3.0f);
                                ImGui::Unindent(20.0f);
                            }
                            ImGui::Spacing();

                            // 4. Normal -> Dash Cancel
                            ImGui::Checkbox("Normal -> Dash Cancel Meter Cost##norm_dash_enable", &MK9TradeFix::MeterDrainFix::g_EnableNormalToDashCancelCost);
                            ImGui::SameLine();
                            ImGui::TextColored(MK9TradeFix::MeterDrainFix::g_EnableNormalToDashCancelCost ? ImVec4(0.2f, 1.0f, 0.3f, 1.0f) : ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                                               MK9TradeFix::MeterDrainFix::g_EnableNormalToDashCancelCost ? "[ACTIVE]" : "[OFF]");
                            if (MK9TradeFix::MeterDrainFix::g_EnableNormalToDashCancelCost) {
                                ImGui::Indent(20.0f);
                                ImGui::SliderFloat("Dash Cancel Cost##dash_cancel_cost",
                                                   &MK9TradeFix::MeterDrainFix::g_NormalToDashCancelCost, 0.000f, 0.500f, "-%.3f per cancel");
                                ImGui::SameLine();
                                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "(-%.1f%% / %.2f bars)",
                                                   MK9TradeFix::MeterDrainFix::g_NormalToDashCancelCost * 100.0f,
                                                   MK9TradeFix::MeterDrainFix::g_NormalToDashCancelCost * 3.0f);
                                ImGui::Unindent(20.0f);
                            }
                            ImGui::Spacing();
                            ImGui::TreePop();
                        }

                        ImGui::Spacing();
                        ImGui::Separator();
                        ImGui::Spacing();

                        ImGui::Checkbox("Show On-Screen Meter Percentage (1 Decimal)", &MK9TradeFix::MeterDrainFix::g_ShowMeterPercentHUD);
                        ImGui::SameLine();
                        ImGui::TextColored(MK9TradeFix::MeterDrainFix::g_ShowMeterPercentHUD ? ImVec4(0.2f, 1.0f, 0.3f, 1.0f) : ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                                           MK9TradeFix::MeterDrainFix::g_ShowMeterPercentHUD ? "[ON]" : "[OFF]");
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("%s", "Displays real-time meter percentage (e.g. 25.0%, 50.0%, 100.0%) beside each player's meter bar at the bottom of the screen.");
                        }

                        if (MK9TradeFix::MeterDrainFix::g_ShowMeterPercentHUD) {
                            ImGui::Indent(20.0f);
                            ImGui::SliderFloat("P1 Meter Indicator X Offset", &MK9TradeFix::MeterDrainFix::g_P1MeterHUDOffsetX, -300.0f, 300.0f, "%.0f px");
                            ImGui::SliderFloat("P1 Meter Indicator Y Offset", &MK9TradeFix::MeterDrainFix::g_P1MeterHUDOffsetY, -200.0f, 200.0f, "%.0f px");
                            ImGui::SliderFloat("P2 Meter Indicator X Offset", &MK9TradeFix::MeterDrainFix::g_P2MeterHUDOffsetX, -300.0f, 300.0f, "%.0f px");
                            ImGui::SliderFloat("P2 Meter Indicator Y Offset", &MK9TradeFix::MeterDrainFix::g_P2MeterHUDOffsetY, -200.0f, 200.0f, "%.0f px");
                            if (ImGui::Button("Reset Meter HUD Offsets")) {
                                MK9TradeFix::MeterDrainFix::g_P1MeterHUDOffsetX = 0.0f;
                                MK9TradeFix::MeterDrainFix::g_P1MeterHUDOffsetY = 0.0f;
                                MK9TradeFix::MeterDrainFix::g_P2MeterHUDOffsetX = 0.0f;
                                MK9TradeFix::MeterDrainFix::g_P2MeterHUDOffsetY = 0.0f;
                            }
                            ImGui::Unindent(20.0f);
                        }

                        ImGui::Spacing();
                        ImGui::Separator();
                        } // CollapsingHeader Super Meter & Combat Meter System

                    if (ImGui::CollapsingHeader("General Hitstun and Blockstun Modifiers")) {
                            ImGui::Spacing();
                            ImGui::TextColored(ImVec4(0.4f, 0.9f, 1.0f, 1.0f), "General Hitstun & Blockstun Modifiers:");
                            ImGui::TextDisabled("Customize defensive frame advantage, block recovery, and general hitstun.");
                            ImGui::Spacing();

                            // Target Scope
                            ImGui::Text("Target Scope:");
                            ImGui::SameLine();
                            if (ImGui::RadioButton("Both Players##stun_scope_both", MK9TradeFix::StunModifier::g_TargetScope == 0)) {
                                MK9TradeFix::StunModifier::g_TargetScope = 0;
                            }
                            ImGui::SameLine();
                            if (ImGui::RadioButton("Player 1 Only##stun_scope_p1", MK9TradeFix::StunModifier::g_TargetScope == 1)) {
                                MK9TradeFix::StunModifier::g_TargetScope = 1;
                            }
                            ImGui::SameLine();
                            if (ImGui::RadioButton("Player 2 Only##stun_scope_p2", MK9TradeFix::StunModifier::g_TargetScope == 2)) {
                                MK9TradeFix::StunModifier::g_TargetScope = 2;
                            }

                            ImGui::Spacing();

                            // 1. General Blockstun Slider
                            ImGui::SliderInt("General Blockstun Adjustment##g_blockstun_adj",
                                             &MK9TradeFix::StunModifier::g_GeneralBlockstunDelta, -12, 12, "%+d Frames");
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("Adjusts blockstun frames when defending player is blocking (- reduces guard recoil, + increases guard recoil).\nApplies to both standing and crouching blocks (-12 to +12 frames).");
                            }
                            ImGui::TextDisabled("   Modifies guard stun on blocked hits (- frames reduce blockstun, + frames give more advantage).");

                            ImGui::Spacing();

                            // 2. General Hitstun Slider (-15 to +60)
                            ImGui::SliderInt("General Hitstun Adjustment##g_hitstun_adj",
                                             &MK9TradeFix::StunModifier::g_GeneralHitstunDelta, -15, 60, "%+d Frames");
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("Adjusts hitstun frames across successful standing hits (-15 to +60 frames).\nApplies strictly to standing hits (crouch, airborne, and knockdown hits excluded).");
                            }
                            ImGui::TextDisabled("   Modifies stun on landed standing hits (- frames give defender early recovery, + frames allow easier combos).");

                            ImGui::Spacing();
                            ImGui::Separator();
                            ImGui::Spacing();

                            // 3. Blockstun Pushback Customization
                            ImGui::TextColored(ImVec4(0.4f, 0.9f, 1.0f, 1.0f), "Blockstun Pushback Customization:");
                            ImGui::Checkbox("Enable Custom Block Pushback##g_enable_block_pushback", &MK9TradeFix::StunModifier::g_EnableCustomBlockPushback);
                            ImGui::SameLine();
                            ImGui::TextColored(MK9TradeFix::StunModifier::g_EnableCustomBlockPushback ? ImVec4(0.2f, 1.0f, 0.3f, 1.0f) : ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                                               MK9TradeFix::StunModifier::g_EnableCustomBlockPushback ? "[ACTIVE]" : "[VANILLA 1.00x]");
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("Customizes horizontal sliding distance when defending against blocked attacks.");
                            }

                            if (MK9TradeFix::StunModifier::g_EnableCustomBlockPushback) {
                                ImGui::Indent(20.0f);
                                ImGui::SliderFloat("Pushback Distance Multiplier##g_slider_block_pushback",
                                                   &MK9TradeFix::StunModifier::g_BlockPushbackMultiplier, 0.00f, 3.00f, "%.2fx");
                                if (ImGui::IsItemHovered()) {
                                    ImGui::SetTooltip("0.00x = Zero Pushback (Point-blank pressure traps).\n1.00x = Vanilla standard.\n2.00x - 3.00x = Heavy spacing resets.");
                                }

                                ImGui::Checkbox("Corner Pushback Reflection##g_corner_pb_reflect", &MK9TradeFix::StunModifier::g_CornerPushbackReflection);
                                if (ImGui::IsItemHovered()) {
                                    ImGui::SetTooltip("When defender is backed against the corner stage boundary, pushback force is transferred into the attacker.");
                                }

                                if (ImGui::Button("Point-Blank (0.00x)##g_pb_btn_0")) {
                                    MK9TradeFix::StunModifier::g_BlockPushbackMultiplier = 0.00f;
                                }
                                ImGui::SameLine();
                                if (ImGui::Button("Tight (0.50x)##g_pb_btn_50")) {
                                    MK9TradeFix::StunModifier::g_BlockPushbackMultiplier = 0.50f;
                                }
                                ImGui::SameLine();
                                if (ImGui::Button("Vanilla (1.00x)##g_pb_btn_100")) {
                                    MK9TradeFix::StunModifier::g_BlockPushbackMultiplier = 1.00f;
                                }
                                ImGui::SameLine();
                                if (ImGui::Button("Heavy (2.00x)##g_pb_btn_200")) {
                                    MK9TradeFix::StunModifier::g_BlockPushbackMultiplier = 2.00f;
                                }
                                ImGui::Unindent(20.0f);
                            }

                            ImGui::Spacing();
                            ImGui::Separator();
                            ImGui::Spacing();

                            if (ImGui::Button("Reset General Hitstun & Blockstun to Default (0F)##reset_gen_stun")) {
                                MK9TradeFix::StunModifier::g_GeneralBlockstunDelta = 0;
                                MK9TradeFix::StunModifier::g_GeneralHitstunDelta = 0;
                                MK9TradeFix::StunModifier::g_BlockPushbackMultiplier = 1.00f;
                            }

                            ImGui::Spacing();
                            ImGui::Separator();
                        } // CollapsingHeader General Hitstun and Blockstun Modifiers

                    if (ImGui::CollapsingHeader("Attack Frame Data Modifier: Startup, Active & Recovery")) {
                        ImGui::Spacing();
                        RenderAttackFrameModifierControls("gameplay_tuning");
                        ImGui::Spacing();
                        ImGui::Separator();
                        ImGui::Spacing();
                    }

                    if (ImGui::CollapsingHeader("Trade Fix Hitstun Extensions & Dynamic Camera")) {
                            ImGui::Spacing();
                            if (MK9TradeFix::g_FixEnabled) {
                                static const char* s_moveTypeOptions[] = { "Both (Normals & Specials)", "Normals Only", "Specials Only" };
                                static const char* s_camModeOptions[] = { "Disabled (Default Game Camera)", "Option 1: Dynamic FOV Zoom Only", "Option 2: Angled Dynamic Camera (Shift Y + Yaw + Zoom)" };

                                // -------------------------------------------------------------
                                // SUBCATEGORY 1: COUNTER HIT SETTINGS & DYNAMIC CAMERA
                                // -------------------------------------------------------------
                                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.20f, 1.0f));
                                ImGui::SeparatorText("[ COUNTER HIT SETTINGS & DYNAMIC CAMERA ]");
                                ImGui::PopStyleColor();
                                ImGui::TextDisabled("Applied during opponent startup interrupt. Extra frames, minimum startup rule, and camera tracking.");
                                ImGui::Spacing();

                                // 1a. Counterhit Hitstun Slider (-15 to +60)
                                if (ImGui::SliderInt("Startup Interrupt Extra Hitstun (Counterhit)##g_ch_hitstun",
                                                     &MK9TradeFix::StunModifier::g_CounterhitExtraHitstun, -15, 60, "%+d Frames")) {
                                    MK9TradeFix::HitIndicationHUD::g_ShowHitIndicationHUD = 
                                        (MK9TradeFix::StunModifier::g_CounterhitExtraHitstun != 0 || MK9TradeFix::StunModifier::g_PunishExtraHitstun != 0);
                                }
                                if (ImGui::IsItemHovered()) {
                                    ImGui::SetTooltip("Additional hitstun frames applied when striking an opponent during move startup (-15 to +60 frames).");
                                }
                                ImGui::TextDisabled("   Reward for interrupting opponent's startup (+ frames allow counterhit combos).");

                                // 1b. Counterhit Min Startup Threshold Slider
                                ImGui::SliderInt("Counter Hit Min Startup Threshold##g_ch_min_startup",
                                                 &MK9TradeFix::StunModifier::g_CounterhitMinStartup, 0, 60,
                                                 (MK9TradeFix::StunModifier::g_CounterhitMinStartup == 0) ? "No Threshold (All Attacks)" : "%d Frames Startup");
                                if (ImGui::IsItemHovered()) {
                                    ImGui::SetTooltip("Minimum attacker move startup required to apply added Counter Hit frames.\nAttacks faster than this threshold will not receive extra frames.");
                                }
                                ImGui::TextDisabled("   Only attacks with startup >= threshold receive added counterhit frames.");

                                // 1c. Counterhit Move Type Filter
                                ImGui::Combo("Counter Hit Move Filter##g_ch_move_type", &MK9TradeFix::StunModifier::g_CounterhitMoveTypeFilter, s_moveTypeOptions, IM_ARRAYSIZE(s_moveTypeOptions));
                                if (ImGui::IsItemHovered()) {
                                    ImGui::SetTooltip("Choose whether Counter Hit stun extensions and camera zoom apply to Normals, Specials, or Both.");
                                }

                                // 1d. Counterhit Camera Mode & Configuration
                                int prevChCamMode = MK9TradeFix::StunModifier::g_CounterhitCameraMode;
                                if (ImGui::Combo("Counter Hit Camera Mode##g_ch_cam_mode", &MK9TradeFix::StunModifier::g_CounterhitCameraMode, s_camModeOptions, IM_ARRAYSIZE(s_camModeOptions))) {
                                    if (MK9TradeFix::StunModifier::g_CounterhitCameraMode == 2 && prevChCamMode != 2) {
                                        MK9TradeFix::StunModifier::g_CounterhitZoomAmount = 30.0f;
                                        MK9TradeFix::StunModifier::g_CounterhitCamShiftY = 302.0f;
                                        MK9TradeFix::StunModifier::g_CounterhitCamYawOffset = 45.0f;
                                    } else if (MK9TradeFix::StunModifier::g_CounterhitCameraMode == 1 && prevChCamMode != 1) {
                                        MK9TradeFix::StunModifier::g_CounterhitZoomAmount = -18.0f;
                                    }
                                }
                                if (ImGui::IsItemHovered()) {
                                    ImGui::SetTooltip("Option 1: Standard FOV Zoom close-up / pull back.\nOption 2: Moves Camera Y towards attacker and yaws angle towards defender with FOV Zoom.");
                                }
                                if (MK9TradeFix::StunModifier::g_CounterhitCameraMode > 0) {
                                    ImGui::Indent(20.0f);
                                    ImGui::SliderFloat("FOV Offset (CH)##g_ch_zoom_amt", &MK9TradeFix::StunModifier::g_CounterhitZoomAmount, -30.0f, 30.0f, "%+.1f deg");
                                    if (MK9TradeFix::StunModifier::g_CounterhitCameraMode == 2) {
                                        ImGui::SliderFloat("Camera Shift Y (Attacker)##g_ch_cam_shift", &MK9TradeFix::StunModifier::g_CounterhitCamShiftY, 0.0f, 600.0f, "%.1f units (±330)");
                                        if (ImGui::IsItemHovered()) {
                                            ImGui::SetTooltip("Lateral distance to shift camera position towards the attacking character (e.g. 302.0 = ±302 units).");
                                        }
                                        ImGui::SliderFloat("Camera Yaw Angle (Defender)##g_ch_cam_yaw", &MK9TradeFix::StunModifier::g_CounterhitCamYawOffset, 0.0f, 45.0f, "%.1f deg");
                                        ImGui::SameLine();
                                        ImGui::TextColored(ImVec4(0.4f, 0.9f, 1.0f, 1.0f), "(%+.0f units)", MK9TradeFix::StunModifier::g_CounterhitCamYawOffset * 182.0444f);
                                        if (ImGui::IsItemHovered()) {
                                            ImGui::SetTooltip("Degrees to yaw/angle the camera towards the defending character (45.0 deg = 8192 rotator units).");
                                        }
                                    }
                                    ImGui::Unindent(20.0f);
                                }

                                // 1e. Counterhit Meter Reward
                                ImGui::Checkbox("Counter Hit Meter Reward##g_ch_meter_enable", &MK9TradeFix::StunModifier::g_EnableCounterhitMeterReward);
                                if (MK9TradeFix::StunModifier::g_EnableCounterhitMeterReward) {
                                    ImGui::Indent(20.0f);
                                    ImGui::SliderFloat("Counter Hit Meter Amount##g_ch_meter_reward",
                                                       &MK9TradeFix::StunModifier::g_CounterhitMeterReward,
                                                       0.0f, 0.333f, "+%.3f Meter");
                                    ImGui::SameLine();
                                    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f), "(+%.1f%%)", MK9TradeFix::StunModifier::g_CounterhitMeterReward * 100.0f);

                                    ImGui::Combo("Counter Hit Meter Move Filter##g_ch_meter_filter",
                                                 &MK9TradeFix::StunModifier::g_CounterhitMeterMoveFilter, s_moveTypeOptions, IM_ARRAYSIZE(s_moveTypeOptions));

                                    static const char* s_meterRecipients[] = { "Hit Receiver (Defender)", "Attacker (Aggressor)", "Both Characters" };
                                    ImGui::Combo("Counter Hit Meter Recipient##g_ch_meter_recip",
                                                 &MK9TradeFix::StunModifier::g_CounterhitMeterRecipient,
                                                 s_meterRecipients, IM_ARRAYSIZE(s_meterRecipients));
                                    ImGui::Unindent(20.0f);
                                }

                                ImGui::Spacing();

                                // -------------------------------------------------------------
                                // SUBCATEGORY 2: FATAL PUNISH HIT SETTINGS & DYNAMIC CAMERA
                                // -------------------------------------------------------------
                                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.20f, 1.0f));
                                ImGui::SeparatorText("[ FATAL PUNISH HIT SETTINGS & DYNAMIC CAMERA ]");
                                ImGui::PopStyleColor();
                                ImGui::TextDisabled("Applied during opponent move recovery. Extra frames, minimum startup rule, and camera tracking.");
                                ImGui::Spacing();

                                // 2a. Punish Recovery Hitstun Slider (-15 to +60)
                                if (ImGui::SliderInt("Punish Recovery Extra Hitstun (Punish Counter)##g_punish_hitstun",
                                                     &MK9TradeFix::StunModifier::g_PunishExtraHitstun, -15, 60, "%+d Frames")) {
                                    MK9TradeFix::HitIndicationHUD::g_ShowHitIndicationHUD = 
                                        (MK9TradeFix::StunModifier::g_CounterhitExtraHitstun != 0 || MK9TradeFix::StunModifier::g_PunishExtraHitstun != 0);
                                }
                                if (ImGui::IsItemHovered()) {
                                    ImGui::SetTooltip("Additional hitstun frames applied when striking an opponent during move recovery (-15 to +60 frames).");
                                }
                                ImGui::TextDisabled("   Reward for punishing opponent's move recovery (+ frames reward clean punishes).");

                                // 2b. Punish Min Startup Threshold Slider
                                ImGui::SliderInt("Fatal Punish Min Startup Threshold##g_punish_min_startup",
                                                 &MK9TradeFix::StunModifier::g_PunishMinStartup, 0, 60,
                                                 (MK9TradeFix::StunModifier::g_PunishMinStartup == 0) ? "No Threshold (All Attacks)" : "%d Frames Startup");
                                if (ImGui::IsItemHovered()) {
                                    ImGui::SetTooltip("Minimum attacker move startup required to apply added Fatal Punish frames.\nAttacks faster than this threshold will not receive extra frames.");
                                }
                                ImGui::TextDisabled("   Only attacks with startup >= threshold receive added punish frames.");

                                // 2c. Fatal Punish Move Type Filter
                                ImGui::Combo("Fatal Punish Move Filter##g_punish_move_type", &MK9TradeFix::StunModifier::g_PunishMoveTypeFilter, s_moveTypeOptions, IM_ARRAYSIZE(s_moveTypeOptions));
                                if (ImGui::IsItemHovered()) {
                                    ImGui::SetTooltip("Choose whether Fatal Punish stun extensions and camera zoom apply to Normals, Specials, or Both.");
                                }

                                // 2d. Fatal Punish Camera Mode & Configuration
                                int prevPunishCamMode = MK9TradeFix::StunModifier::g_PunishCameraMode;
                                if (ImGui::Combo("Fatal Punish Camera Mode##g_punish_cam_mode", &MK9TradeFix::StunModifier::g_PunishCameraMode, s_camModeOptions, IM_ARRAYSIZE(s_camModeOptions))) {
                                    if (MK9TradeFix::StunModifier::g_PunishCameraMode == 2 && prevPunishCamMode != 2) {
                                        MK9TradeFix::StunModifier::g_PunishZoomAmount = 30.0f;
                                        MK9TradeFix::StunModifier::g_PunishCamShiftY = 302.0f;
                                        MK9TradeFix::StunModifier::g_PunishCamYawOffset = 45.0f;
                                    } else if (MK9TradeFix::StunModifier::g_PunishCameraMode == 1 && prevPunishCamMode != 1) {
                                        MK9TradeFix::StunModifier::g_PunishZoomAmount = -22.9f;
                                    }
                                }
                                if (ImGui::IsItemHovered()) {
                                    ImGui::SetTooltip("Option 1: Standard FOV Zoom close-up / pull back.\nOption 2: Moves Camera Y towards attacker and yaws angle towards defender with FOV Zoom.");
                                }
                                if (MK9TradeFix::StunModifier::g_PunishCameraMode > 0) {
                                    ImGui::Indent(20.0f);
                                    ImGui::SliderFloat("FOV Offset (Punish)##g_punish_zoom_amt", &MK9TradeFix::StunModifier::g_PunishZoomAmount, -30.0f, 30.0f, "%+.1f deg");
                                    if (MK9TradeFix::StunModifier::g_PunishCameraMode == 2) {
                                        ImGui::SliderFloat("Camera Shift Y (Attacker)##g_punish_cam_shift", &MK9TradeFix::StunModifier::g_PunishCamShiftY, 0.0f, 600.0f, "%.1f units (±330)");
                                        if (ImGui::IsItemHovered()) {
                                            ImGui::SetTooltip("Lateral distance to shift camera position towards the attacking character (e.g. 302.0 = ±302 units).");
                                        }
                                        ImGui::SliderFloat("Camera Yaw Angle (Defender)##g_punish_cam_yaw", &MK9TradeFix::StunModifier::g_PunishCamYawOffset, 0.0f, 45.0f, "%.1f deg");
                                        ImGui::SameLine();
                                        ImGui::TextColored(ImVec4(0.4f, 0.9f, 1.0f, 1.0f), "(%+.0f units)", MK9TradeFix::StunModifier::g_PunishCamYawOffset * 182.0444f);
                                        if (ImGui::IsItemHovered()) {
                                            ImGui::SetTooltip("Degrees to yaw/angle the camera towards the defending character (45.0 deg = 8192 rotator units).");
                                        }
                                    }
                                    ImGui::Unindent(20.0f);
                                }

                                // 2e. Fatal Punish Meter Reward
                                ImGui::Checkbox("Fatal Punish Meter Reward##g_punish_meter_enable", &MK9TradeFix::StunModifier::g_EnablePunishMeterReward);
                                if (MK9TradeFix::StunModifier::g_EnablePunishMeterReward) {
                                    ImGui::Indent(20.0f);
                                    ImGui::SliderFloat("Fatal Punish Meter Amount##g_punish_meter_reward",
                                                       &MK9TradeFix::StunModifier::g_PunishMeterReward,
                                                       0.0f, 0.333f, "+%.3f Meter");
                                    ImGui::SameLine();
                                    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f), "(+%.1f%%)", MK9TradeFix::StunModifier::g_PunishMeterReward * 100.0f);

                                    ImGui::Combo("Fatal Punish Meter Move Filter##g_punish_meter_filter",
                                                 &MK9TradeFix::StunModifier::g_PunishMeterMoveFilter, s_moveTypeOptions, IM_ARRAYSIZE(s_moveTypeOptions));

                                    static const char* s_meterRecipients[] = { "Hit Receiver (Defender)", "Attacker (Aggressor)", "Both Characters" };
                                    ImGui::Combo("Fatal Punish Meter Recipient##g_punish_meter_recip",
                                                 &MK9TradeFix::StunModifier::g_PunishMeterRecipient,
                                                 s_meterRecipients, IM_ARRAYSIZE(s_meterRecipients));
                                    ImGui::Unindent(20.0f);
                                }

                                ImGui::Spacing();

                                // -------------------------------------------------------------
                                // SUBCATEGORY 3: MUTUAL TRADE DYNAMIC CAMERA & REWARDS
                                // -------------------------------------------------------------
                                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.35f, 0.85f, 1.0f, 1.0f));
                                ImGui::SeparatorText("[ MUTUAL TRADE DYNAMIC CAMERA & REWARDS ]");
                                ImGui::PopStyleColor();
                                ImGui::TextDisabled("Dynamic camera pullback, hold frames, and super meter rewards on mutual trade hits.");
                                ImGui::Spacing();

                                int prevTradeCamMode = MK9TradeFix::StunModifier::g_TradeCameraMode;
                                if (ImGui::Combo("Trade Hit Camera Mode##g_trade_cam_mode", &MK9TradeFix::StunModifier::g_TradeCameraMode, s_camModeOptions, IM_ARRAYSIZE(s_camModeOptions))) {
                                    if (MK9TradeFix::StunModifier::g_TradeCameraMode == 2 && prevTradeCamMode != 2) {
                                        MK9TradeFix::StunModifier::g_TradeZoomAmount = 30.0f;
                                        MK9TradeFix::StunModifier::g_TradeCamShiftY = 302.0f;
                                        MK9TradeFix::StunModifier::g_TradeCamYawOffset = 45.0f;
                                    }
                                }
                                if (ImGui::IsItemHovered()) {
                                    ImGui::SetTooltip("Dynamic Camera on Mutual Trade Hit.\nOption 1: Standard FOV Zoom / Pullback.\nOption 2: Angled Dynamic View.");
                                }
                                if (MK9TradeFix::StunModifier::g_TradeCameraMode > 0) {
                                    ImGui::Indent(20.0f);
                                    ImGui::SliderFloat("FOV Offset (Trade)##g_trade_zoom_amt", &MK9TradeFix::StunModifier::g_TradeZoomAmount, -30.0f, 30.0f, "%+.1f deg");
                                    ImGui::SliderInt("Trade Hold Duration##g_trade_hold_frames", &MK9TradeFix::StunModifier::g_TradeZoomHoldFrames, 5, 120, "%d Frames");
                                    if (ImGui::IsItemHovered()) {
                                        ImGui::SetTooltip("Number of frames the camera pulls back/holds before smoothly returning to default FOV (Default: 30 Frames).");
                                    }
                                    ImGui::TextDisabled("   Holds Trade FOV for %d frames before smoothly restoring standard view.", MK9TradeFix::StunModifier::g_TradeZoomHoldFrames);
                                    if (MK9TradeFix::StunModifier::g_TradeCameraMode == 2) {
                                        ImGui::SliderFloat("Camera Shift Y##g_trade_cam_shift", &MK9TradeFix::StunModifier::g_TradeCamShiftY, 0.0f, 600.0f, "%.1f units");
                                        ImGui::SliderFloat("Camera Yaw Angle##g_trade_cam_yaw", &MK9TradeFix::StunModifier::g_TradeCamYawOffset, 0.0f, 45.0f, "%.1f deg");
                                        ImGui::SameLine();
                                        ImGui::TextColored(ImVec4(0.4f, 0.9f, 1.0f, 1.0f), "(%+.0f units)", MK9TradeFix::StunModifier::g_TradeCamYawOffset * 182.0444f);
                                    }
                                    ImGui::Unindent(20.0f);
                                }

                                // 3b. Trade Meter Reward Slider (Both Characters)
                                ImGui::Checkbox("Trade Hit Meter Reward (Both Fighters)##g_trade_meter_enable", &MK9TradeFix::StunModifier::g_EnableTradeMeterReward);
                                if (MK9TradeFix::StunModifier::g_EnableTradeMeterReward) {
                                    ImGui::Indent(20.0f);
                                    ImGui::SliderFloat("Trade Meter Amount##g_trade_meter_reward",
                                                       &MK9TradeFix::StunModifier::g_TradeMeterReward,
                                                       0.0f, 0.333f, "+%.3f Meter");
                                    ImGui::SameLine();
                                    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f), "(+%.1f%%)", MK9TradeFix::StunModifier::g_TradeMeterReward * 100.0f);

                                    ImGui::Combo("Trade Hit Meter Move Filter##g_trade_meter_filter",
                                                 &MK9TradeFix::StunModifier::g_TradeMeterMoveFilter, s_moveTypeOptions, IM_ARRAYSIZE(s_moveTypeOptions));
                                    ImGui::Unindent(20.0f);
                                }

                                ImGui::Spacing();

                                // -------------------------------------------------------------
                                // SUBCATEGORY 4: DYNAMIC CAMERA SLOW-MOTION & MOTION SMOOTHING
                                // -------------------------------------------------------------
                                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.40f, 1.0f, 0.50f, 1.0f));
                                ImGui::SeparatorText("[ DYNAMIC CAMERA SLOW-MOTION & MOTION SMOOTHING ]");
                                ImGui::PopStyleColor();
                                ImGui::TextDisabled("Slow-motion speed and interpolation smoothing while dynamic camera transitions are active.");
                                ImGui::Spacing();

                                ImGui::Checkbox("General Slow-Mo during Dynamic Camera Adjustment##freecam_slowmo_enable",
                                               &MK9TradeFix::FreeCamera::g_CinematicSlowMoEnabled);
                                if (ImGui::IsItemHovered()) {
                                    ImGui::SetTooltip("Applies authentic dramatic slow-motion to fighters while dynamic cinematic camera is actively panning/zooming on Counter Hit, Fatal Punish, or Mutual Trade.");
                                }

                                if (MK9TradeFix::FreeCamera::g_CinematicSlowMoEnabled) {
                                    ImGui::Indent(20.0f);
                                    ImGui::SliderFloat("Slow-Mo Playback Rate##freecam_slowmo_rate",
                                                       &MK9TradeFix::FreeCamera::g_CinematicSlowMoRate, 0.10f, 1.0f, "%.2fx speed");
                                    ImGui::SameLine();
                                    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f), "(%.0f%% Speed)", MK9TradeFix::FreeCamera::g_CinematicSlowMoRate * 100.0f);
                                    ImGui::Unindent(20.0f);
                                }

                                ImGui::SliderFloat("Camera Move-In Smoothing Speed##freecam_speed_in",
                                                   &MK9TradeFix::FreeCamera::g_CinematicCameraSpeedIn, 0.02f, 0.40f, "%.2f (Smooth Glide)");
                                if (ImGui::IsItemHovered()) {
                                    ImGui::SetTooltip("Interpolation speed when dynamic camera enters angled/zoom view.\nLower values (0.06 - 0.12) create a cinematic smooth glide.\nHigher values (> 0.25) snap quickly.");
                                }

                                ImGui::SliderFloat("Camera Move-Out Return Speed##freecam_speed_out",
                                                   &MK9TradeFix::FreeCamera::g_CinematicCameraSpeedOut, 0.02f, 0.40f, "%.2f (Smooth Return)");

                                ImGui::Spacing();
                                int maxExtra = (MK9TradeFix::StunModifier::g_CounterhitExtraHitstun > MK9TradeFix::StunModifier::g_PunishExtraHitstun) ? 
                                               MK9TradeFix::StunModifier::g_CounterhitExtraHitstun : MK9TradeFix::StunModifier::g_PunishExtraHitstun;
                                if (maxExtra > 0) {
                                    float rate = 16.0f / (16.0f + (float)maxExtra);
                                    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "   Slow-Motion Hurt Playback: %.2fx speed (16f base / %df total hitstun)", rate, 16 + maxExtra);
                                }
                            } else {
                                ImGui::TextDisabled("(Unavailable: Requires Trade Fix to be enabled above)");
                            }

                            ImGui::Spacing();
                            ImGui::Separator();
                            ImGui::Spacing();

                            // -------------------------------------------------------------
                            // SUBCATEGORY 5: HIT OUTCOME POP-UP INDICATORS (HUD)
                            // -------------------------------------------------------------
                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.65f, 0.20f, 1.0f));
                            ImGui::SeparatorText("[ HIT OUTCOME POP-UP INDICATORS (HUD) ]");
                            ImGui::PopStyleColor();
                            ImGui::TextDisabled("Displays stylized badges below combo counter on respective player's side when Trade Engine is active.");
                            ImGui::Spacing();

                        ImGui::Checkbox("Enable Hit Outcome Pop-up Badges##enable_hit_outcome_badges",
                                       &MK9TradeFix::HitIndicationHUD::g_ShowHitIndicationHUD);
                        ImGui::SameLine();
                        ImGui::TextColored(MK9TradeFix::HitIndicationHUD::g_ShowHitIndicationHUD ? ImVec4(0.2f, 1.0f, 0.3f, 1.0f) : ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                                           MK9TradeFix::HitIndicationHUD::g_ShowHitIndicationHUD ? "[ON]" : "[OFF]");

                        if (MK9TradeFix::HitIndicationHUD::g_ShowHitIndicationHUD) {
                            ImGui::Spacing();
                            ImGui::Text("Test Preview Badges:");
                            ImGui::SameLine();
                            if (ImGui::Button("Counter Hit (P1)##prev_ch_p1")) {
                                MK9TradeFix::HitIndicationHUD::TriggerHitIndication(1, MK9TradeFix::HitIndicationHUD::HitIndicationType::COUNTER_HIT);
                                if (MK9TradeFix::StunModifier::g_CounterhitCameraMode > 0) {
                                    MK9TradeFix::FreeCamera::TriggerCinematicZoom(
                                        MK9TradeFix::StunModifier::g_CounterhitZoomAmount, 0, 2, 1,
                                        MK9TradeFix::StunModifier::g_CounterhitCameraMode,
                                        MK9TradeFix::StunModifier::g_CounterhitCamShiftY,
                                        MK9TradeFix::StunModifier::g_CounterhitCamYawOffset);
                                }
                            }
                            ImGui::SameLine();
                            if (ImGui::Button("Trade Hit (Both)##prev_th")) {
                                MK9TradeFix::HitIndicationHUD::TriggerHitIndication(1, MK9TradeFix::HitIndicationHUD::HitIndicationType::TRADE_HIT);
                                MK9TradeFix::HitIndicationHUD::TriggerHitIndication(2, MK9TradeFix::HitIndicationHUD::HitIndicationType::TRADE_HIT);
                                if (MK9TradeFix::StunModifier::g_TradeCameraMode > 0) {
                                    MK9TradeFix::FreeCamera::TriggerCinematicZoom(
                                        MK9TradeFix::StunModifier::g_TradeZoomAmount,
                                        MK9TradeFix::StunModifier::g_TradeZoomHoldFrames, 1, 2,
                                        MK9TradeFix::StunModifier::g_TradeCameraMode,
                                        MK9TradeFix::StunModifier::g_TradeCamShiftY,
                                        MK9TradeFix::StunModifier::g_TradeCamYawOffset);
                                }
                            }
                            ImGui::SameLine();
                            if (ImGui::Button("Fatal Punish (P1)##prev_fp_p1")) {
                                MK9TradeFix::HitIndicationHUD::TriggerHitIndication(1, MK9TradeFix::HitIndicationHUD::HitIndicationType::FATAL_PUNISH);
                                if (MK9TradeFix::StunModifier::g_PunishCameraMode > 0) {
                                    MK9TradeFix::FreeCamera::TriggerCinematicZoom(
                                        MK9TradeFix::StunModifier::g_PunishZoomAmount, 0, 2, 1,
                                        MK9TradeFix::StunModifier::g_PunishCameraMode,
                                        MK9TradeFix::StunModifier::g_PunishCamShiftY,
                                        MK9TradeFix::StunModifier::g_PunishCamYawOffset);
                                }
                            }

                            ImGui::SameLine();
                            if (ImGui::Button("Counter Hit (P2)##prev_ch_p2")) {
                                MK9TradeFix::HitIndicationHUD::TriggerHitIndication(2, MK9TradeFix::HitIndicationHUD::HitIndicationType::COUNTER_HIT);
                                if (MK9TradeFix::StunModifier::g_CounterhitCameraMode > 0) {
                                    MK9TradeFix::FreeCamera::TriggerCinematicZoom(
                                        MK9TradeFix::StunModifier::g_CounterhitZoomAmount, 0, 1, 2,
                                        MK9TradeFix::StunModifier::g_CounterhitCameraMode,
                                        MK9TradeFix::StunModifier::g_CounterhitCamShiftY,
                                        MK9TradeFix::StunModifier::g_CounterhitCamYawOffset);
                                }
                            }
                            ImGui::SameLine();
                            if (ImGui::Button("Fatal Punish (P2)##prev_fp_p2")) {
                                MK9TradeFix::HitIndicationHUD::TriggerHitIndication(2, MK9TradeFix::HitIndicationHUD::HitIndicationType::FATAL_PUNISH);
                                if (MK9TradeFix::StunModifier::g_PunishCameraMode > 0) {
                                    MK9TradeFix::FreeCamera::TriggerCinematicZoom(
                                        MK9TradeFix::StunModifier::g_PunishZoomAmount, 0, 1, 2,
                                        MK9TradeFix::StunModifier::g_PunishCameraMode,
                                        MK9TradeFix::StunModifier::g_PunishCamShiftY,
                                        MK9TradeFix::StunModifier::g_PunishCamYawOffset);
                                }
                            }

                            ImGui::Spacing();
                            ImGui::SliderFloat("P1 Indicator X Offset##p1_ind_x", &MK9TradeFix::HitIndicationHUD::g_P1IndicationOffsetX, -300.0f, 300.0f, "%.0f px");
                            ImGui::SliderFloat("P1 Indicator Y Offset##p1_ind_y", &MK9TradeFix::HitIndicationHUD::g_P1IndicationOffsetY, -300.0f, 300.0f, "%.0f px");
                            ImGui::SliderFloat("P2 Indicator X Offset##p2_ind_x", &MK9TradeFix::HitIndicationHUD::g_P2IndicationOffsetX, -300.0f, 300.0f, "%.0f px");
                            ImGui::SliderFloat("P2 Indicator Y Offset##p2_ind_y", &MK9TradeFix::HitIndicationHUD::g_P2IndicationOffsetY, -300.0f, 300.0f, "%.0f px");

                            if (ImGui::Button("Reset Indicator Offsets to Default##reset_ind_offsets")) {
                                MK9TradeFix::HitIndicationHUD::g_P1IndicationOffsetX = -49.0f;
                                MK9TradeFix::HitIndicationHUD::g_P1IndicationOffsetY = 75.0f;
                                MK9TradeFix::HitIndicationHUD::g_P2IndicationOffsetX = 46.0f;
                                MK9TradeFix::HitIndicationHUD::g_P2IndicationOffsetY = 75.0f;
                            }
                        }

                        ImGui::Spacing();
                        ImGui::Separator();
                        ImGui::Spacing();
                    } // CollapsingHeader Trade Fix Hitstun Extensions & Dynamic Camera

                    ImGui::Spacing();

                    if (ImGui::CollapsingHeader("On-Screen Move ID & Move Name Telemetry (Above Meter)")) {
                        ImGui::Spacing();
                        ImGui::TextColored(ImVec4(0.4f, 0.9f, 1.0f, 1.0f), "On-Screen Move ID & Move Name Telemetry (Above Meter):");
                        ImGui::TextDisabled("Displays real-time move name and action state ID badge directly above each player's super meter dial.");
                        ImGui::Spacing();

                        ImGui::Checkbox("Show Move ID & Move Name HUD", &MK9TradeFix::MoveTelemetryHUD::g_ShowMoveHUD);
                        ImGui::SameLine();
                        ImGui::TextColored(MK9TradeFix::MoveTelemetryHUD::g_ShowMoveHUD ? ImVec4(0.2f, 1.0f, 0.3f, 1.0f) : ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                                           MK9TradeFix::MoveTelemetryHUD::g_ShowMoveHUD ? "[ON]" : "[OFF]");
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("%s", "Displays real-time move name and action state ID badge directly above each player's super meter dial.");
                        }

                        if (MK9TradeFix::MoveTelemetryHUD::g_ShowMoveHUD) {
                            ImGui::Indent(20.0f);
                            ImGui::Checkbox("Show Action State Hex ID (e.g. [0x5200])", &MK9TradeFix::MoveTelemetryHUD::g_ShowActionHex);

                            ImGui::Spacing();
                            ImGui::SliderFloat("P1 Move Badge X Offset", &MK9TradeFix::MoveTelemetryHUD::g_P1MoveHUDOffsetX, -300.0f, 300.0f, "%.0f px");
                            ImGui::SliderFloat("P1 Move Badge Y Offset", &MK9TradeFix::MoveTelemetryHUD::g_P1MoveHUDOffsetY, -200.0f, 200.0f, "%.0f px");
                            ImGui::SliderFloat("P2 Move Badge X Offset", &MK9TradeFix::MoveTelemetryHUD::g_P2MoveHUDOffsetX, -300.0f, 300.0f, "%.0f px");
                            ImGui::SliderFloat("P2 Move Badge Y Offset", &MK9TradeFix::MoveTelemetryHUD::g_P2MoveHUDOffsetY, -200.0f, 200.0f, "%.0f px");

                            if (ImGui::Button("Reset Move HUD Offsets")) {
                                MK9TradeFix::MoveTelemetryHUD::g_P1MoveHUDOffsetX = 100.0f;
                                MK9TradeFix::MoveTelemetryHUD::g_P1MoveHUDOffsetY = 50.0f;
                                MK9TradeFix::MoveTelemetryHUD::g_P2MoveHUDOffsetX = -100.0f;
                                MK9TradeFix::MoveTelemetryHUD::g_P2MoveHUDOffsetY = 50.0f;
                            }
                            ImGui::Unindent(20.0f);
                        }

                        ImGui::Spacing();
                        ImGui::Separator();
                        ImGui::Spacing();
                    }



                    if (ImGui::CollapsingHeader("Knockdown & Wakeup Options")) {
                        ImGui::Spacing();
                        ImGui::TextColored(ImVec4(0.4f, 0.9f, 1.0f, 1.0f), "Knockdown & Recovery Customization:");
                        ImGui::TextDisabled("Modify core engine knockdown recovery rules, tech rolls, and wakeup attack options.");
                        ImGui::Spacing();

                        // Target Scope
                        ImGui::Text("Target Scope:");
                        ImGui::SameLine();
                        if (ImGui::RadioButton("Both Players##eg_scope_both", MK9TradeFix::ExtraGameplay::g_TargetScope == 0)) {
                            MK9TradeFix::ExtraGameplay::g_TargetScope = 0;
                        }
                        ImGui::SameLine();
                        if (ImGui::RadioButton("Player 1 Only##eg_scope_p1", MK9TradeFix::ExtraGameplay::g_TargetScope == 1)) {
                            MK9TradeFix::ExtraGameplay::g_TargetScope = 1;
                        }
                        ImGui::SameLine();
                        if (ImGui::RadioButton("Player 2 Only##eg_scope_p2", MK9TradeFix::ExtraGameplay::g_TargetScope == 2)) {
                            MK9TradeFix::ExtraGameplay::g_TargetScope = 2;
                        }

                        ImGui::Spacing();

#if 0
                        // Stashed for further research:
                        // 1. Disable Quick Wakeup (Tech Roll)
                        ImGui::Checkbox("Disable Quick Wakeup (Tech Roll)", &MK9TradeFix::ExtraGameplay::g_DisableQuickWakeup);
                        ImGui::SameLine();
                        ImGui::TextColored(MK9TradeFix::ExtraGameplay::g_DisableQuickWakeup ? ImVec4(0.2f, 1.0f, 0.3f, 1.0f) : ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                                           MK9TradeFix::ExtraGameplay::g_DisableQuickWakeup ? "[ACTIVE]" : "[OFF]");
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Prevents horizontal tech rolls upon floor impact (0x460D). Characters always settle into standard prone knockdown.");
                        }
                        ImGui::TextDisabled("   Suppresses Forward/Back rolls upon knockdown floor impact. Characters always settle prone.");

                        ImGui::Spacing();

                        // 2. Disable Wakeup Attack from Quick Roll (Option B)
                        ImGui::Checkbox("Disable Wakeup Attack When Coming From Quick Roll", &MK9TradeFix::ExtraGameplay::g_DisableRollWakeupAttack);
                        ImGui::SameLine();
                        ImGui::TextColored(MK9TradeFix::ExtraGameplay::g_DisableRollWakeupAttack ? ImVec4(0.2f, 1.0f, 0.3f, 1.0f) : ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                                           MK9TradeFix::ExtraGameplay::g_DisableRollWakeupAttack ? "[ACTIVE]" : "[OFF]");
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Option B: Prohibits special moves directly out of roll recovery. Fighters must return to neutral standing before attacking.");
                        }
                        ImGui::TextDisabled("   Prohibits special moves directly out of roll recovery. Requires returning to neutral standing first.");

                        ImGui::Spacing();
#endif

                        // 3. Disable Delayed Wakeup
                        ImGui::Checkbox("Disable Delayed Wakeup", &MK9TradeFix::ExtraGameplay::g_DisableDelayedWakeup);
                        ImGui::SameLine();
                        ImGui::TextColored(MK9TradeFix::ExtraGameplay::g_DisableDelayedWakeup ? ImVec4(0.2f, 1.0f, 0.3f, 1.0f) : ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                                           MK9TradeFix::ExtraGameplay::g_DisableDelayedWakeup ? "[ACTIVE]" : "[OFF]");
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Suppresses holding Down on the floor. Characters always execute standard wakeup on immediate timing (~32-38F).");
                        }
                        ImGui::TextDisabled("   Enforces standard getup timing; ignores holding Down while grounded.");

                        ImGui::Spacing();

                        // 4. Delayed Wakeup Fixed Amount of Hold When Initiated
                        if (!MK9TradeFix::ExtraGameplay::g_DisableDelayedWakeup) {
                            ImGui::Checkbox("Delayed Wakeup: Fixed Amount of Hold (When Initiated)", &MK9TradeFix::ExtraGameplay::g_DelayedWakeupFixedHold);
                            ImGui::SameLine();
                            ImGui::TextColored(MK9TradeFix::ExtraGameplay::g_DelayedWakeupFixedHold ? ImVec4(0.2f, 1.0f, 0.3f, 1.0f) : ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                                               MK9TradeFix::ExtraGameplay::g_DelayedWakeupFixedHold ? "[ACTIVE]" : "[OFF]");
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("When Down is pressed upon floor contact, locks the player into a committed fixed ground duration instead of variable release.");
                            }
                            ImGui::TextDisabled("   Locks player into a predetermined ground duration once initiated, eliminating arbitrary release timing.");

                            if (MK9TradeFix::ExtraGameplay::g_DelayedWakeupFixedHold) {
                                ImGui::SliderInt("Fixed Hold Duration##eg_fixed_hold", &MK9TradeFix::ExtraGameplay::g_DelayedWakeupFixedFrames, 30, 96, "%d Frames");
                                if (ImGui::IsItemHovered()) {
                                    ImGui::SetTooltip("Committed duration to remain grounded before automatic rising (30 to 96 frames).");
                                }
                            }
                        } else {
                            ImGui::TextDisabled("Delayed Wakeup: Fixed Hold Duration (Unavailable: Delayed Wakeup is Disabled)");
                        }

                        ImGui::Spacing();

                        // 5. Disable Delayed Wakeup Roll
                        if (!MK9TradeFix::ExtraGameplay::g_DisableDelayedWakeup) {
                            ImGui::Checkbox("Disable Delayed Wakeup Roll", &MK9TradeFix::ExtraGameplay::g_DisableDelayedWakeupRoll);
                            ImGui::SameLine();
                            ImGui::TextColored(MK9TradeFix::ExtraGameplay::g_DisableDelayedWakeupRoll ? ImVec4(0.2f, 1.0f, 0.3f, 1.0f) : ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                                               MK9TradeFix::ExtraGameplay::g_DisableDelayedWakeupRoll ? "[ACTIVE]" : "[OFF]");
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("Suppresses the backward roll when buffering/holding Back upon releasing delayed wakeup. Characters always rise stationary in place.");
                            }
                            ImGui::TextDisabled("   Suppresses backward roll when exiting delayed wakeup; character rises stationary in place.");
                        } else {
                            ImGui::TextDisabled("Disable Delayed Wakeup Roll (Unavailable: Delayed Wakeup is Disabled)");
                        }

                        ImGui::Spacing();
                        ImGui::Separator();
                        ImGui::Spacing();
                    }



                    if (ImGui::CollapsingHeader("Move Priority Hierarchy")) {
                        ImGui::Spacing();
                        ImGui::TextColored(ImVec4(0.4f, 0.9f, 1.0f, 1.0f), "Move Priority Hierarchy (All OFF by Default):");
                        ImGui::TextDisabled("Select individual combat priority rules for simultaneous clashes and trade interactions.");
                        ImGui::Spacing();

                        // 1. Uppercut (D+2) Anti-Air Priority
                        ImGui::Checkbox("Uppercut (D+2) Anti-Air Priority", &MK9TradeFix::MovePriority::g_UppercutAntiAir);
                        ImGui::SameLine();
                        ImGui::TextColored(MK9TradeFix::MovePriority::g_UppercutAntiAir ? ImVec4(0.2f, 1.0f, 0.3f, 1.0f) : ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                                           MK9TradeFix::MovePriority::g_UppercutAntiAir ? "[ACTIVE]" : "[OFF]");
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("D+2 cleanly beats incoming Jump-In attacks during active anti-air frames.");
                        }
                        ImGui::TextDisabled("   D+2 cleanly beats incoming Jump-In attacks during active anti-air frames.");

                        ImGui::Spacing();

                        // 2. Uppercut vs Special
                        ImGui::Checkbox("Uppercut vs Special", &MK9TradeFix::MovePriority::g_UppercutVsSpecial);
                        ImGui::SameLine();
                        ImGui::TextColored(MK9TradeFix::MovePriority::g_UppercutVsSpecial ? ImVec4(0.2f, 1.0f, 0.3f, 1.0f) : ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                                           MK9TradeFix::MovePriority::g_UppercutVsSpecial ? "[ACTIVE]" : "[OFF]");
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("D+2 cleanly overpowers grounded special moves during active clash frames.");
                        }
                        ImGui::TextDisabled("   D+2 cleanly overpowers grounded special moves during active clash frames.");

                        ImGui::Spacing();

                        // 3. Uppercut vs Uppercut
                        ImGui::Checkbox("Uppercut vs Uppercut", &MK9TradeFix::MovePriority::g_UppercutVsUppercut);
                        ImGui::SameLine();
                        ImGui::TextColored(MK9TradeFix::MovePriority::g_UppercutVsUppercut ? ImVec4(0.2f, 1.0f, 0.3f, 1.0f) : ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                                           MK9TradeFix::MovePriority::g_UppercutVsUppercut ? "[ACTIVE]" : "[OFF]");
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Later inputted D+2 wins by default.");
                        }
                        ImGui::TextDisabled("   Later inputted D+2 wins by default.");

                        ImGui::Spacing();

                        // 4. Jump-In Attacks
                        ImGui::Checkbox("Jump-In Attacks", &MK9TradeFix::MovePriority::g_JumpInPriority);
                        ImGui::SameLine();
                        ImGui::TextColored(MK9TradeFix::MovePriority::g_JumpInPriority ? ImVec4(0.2f, 1.0f, 0.3f, 1.0f) : ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                                           MK9TradeFix::MovePriority::g_JumpInPriority ? "[ACTIVE]" : "[OFF]");
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Jump-ins trade or cleanly beat grounded standing normals during active falling frames.");
                        }
                        ImGui::TextDisabled("   Jump-ins trade or cleanly beat grounded standing normals during active falling frames.");

                        ImGui::Spacing();

                        // 5. X-Ray Absolute Priority
                        ImGui::Checkbox("X-Ray Absolute Priority", &MK9TradeFix::MovePriority::g_XRayAbsolutePriority);
                        ImGui::SameLine();
                        ImGui::TextColored(MK9TradeFix::MovePriority::g_XRayAbsolutePriority ? ImVec4(0.2f, 1.0f, 0.3f, 1.0f) : ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                                           MK9TradeFix::MovePriority::g_XRayAbsolutePriority ? "[ACTIVE]" : "[OFF]");
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Cinematic X-Ray super moves cleanly overpower all incoming normal and special strikes.");
                        }
                        ImGui::TextDisabled("   Cinematic X-Ray super moves cleanly overpower all incoming normal and special strikes.");

                        ImGui::Spacing();

                        // 6. Instant Projectile Impacts
                        ImGui::Checkbox("Instant Projectile Impacts", &MK9TradeFix::MovePriority::g_InstantProjectiles);
                        ImGui::SameLine();
                        ImGui::TextColored(MK9TradeFix::MovePriority::g_InstantProjectiles ? ImVec4(0.2f, 1.0f, 0.3f, 1.0f) : ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                                           MK9TradeFix::MovePriority::g_InstantProjectiles ? "[ACTIVE]" : "[OFF]");
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Projectiles deal immediate damage/reactions on contact without artificial holding or passthrough glitches.");
                        }
                        ImGui::TextDisabled("   Projectiles deal immediate damage/reactions on contact without artificial holding or passthrough glitches.");

                        ImGui::Spacing();
                        ImGui::Separator();
                        ImGui::Text("Quick Presets:");
                        if (ImGui::Button("All OFF (Default Standard)")) {
                            MK9TradeFix::MovePriority::SetAll(false);
                            MK9TradeFix::LogTrace("[MOVE PRIORITY] Preset Applied: All OFF (Default Standard)");
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("All ON (Full Priority Overrides)")) {
                            MK9TradeFix::MovePriority::SetAll(true);
                            MK9TradeFix::LogTrace("[MOVE PRIORITY] Preset Applied: All ON (Full Priority Overrides)");
                        }
                    }

                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();

                    MK9TradeFix::JuggleDeterioration::RenderUI();

                    if (ImGui::CollapsingHeader("Free Camera & Cinematic Controls (-)")) {
                        if (!inMatch) {
                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
                            ImGui::Text("[RESTRICTED: Free Camera requires an active match with spawned characters]");
                            ImGui::PopStyleColor();
                            ImGui::BeginDisabled();
                        }
                        MK9TradeFix::FreeCamera::RenderUI();
                        if (!inMatch) {
                            ImGui::EndDisabled();
                        }
                    }

                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();

                    if (ImGui::CollapsingHeader("Stashed / Experimental Gameplay Options")) {
                        ImGui::Spacing();
                        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.25f, 1.0f), "Experimental Gameplay Modifiers [CAUTION / UNSTABLE]:");
                        ImGui::TextDisabled("Archived gameplay options and experimental features undergoing active reverse engineering.");
                        ImGui::Spacing();

                        // 1. Disable Blood (Moved to Experimental due to post-match crash investigation)
                        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0f, 0.35f, 0.35f, 0.8f));
                        ImGui::Checkbox("Disable Blood (No Blood) [EXPERIMENTAL]", &MK9TradeFix::ExtraGameplay::g_DisableBlood);
                        ImGui::PopStyleColor();
                        ImGui::SameLine();
                        ImGui::TextColored(MK9TradeFix::ExtraGameplay::g_DisableBlood ? ImVec4(1.0f, 0.3f, 0.3f, 1.0f) : ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                                           MK9TradeFix::ExtraGameplay::g_DisableBlood ? "[ACTIVE - CAUTION]" : "[OFF]");
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("CAUTION: Caused post-match crash due to bypassed object initialization at 0x00830300 / 0x0084E1E0.\n"
                                              "Detour hooks have been safely neutralized; now uses native engine flag 0x00E04B04.\n"
                                              "Refer to docs/BUG_QUEUE_FIX.md for full technical details.");
                        }
                        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "   WARNING: Caused crash after match in earlier build.");
                        ImGui::TextDisabled("   Suppresses hit blood particle spawners (detours neutralized; fix queued in docs/BUG_QUEUE_FIX.md).");
                        ImGui::Spacing();
                    }

                    ImGui::EndTabItem();
                }

                // =========================================================
                // TAB 4: Hook Performance & Telemetry (w/ Test Bot Suites)
                // =========================================================
                if (ImGui::BeginTabItem("Performance & Telemetry")) {
                    ImGui::Spacing();

                    // Match Loading Landscape & I/O Telemetry
                    if (ImGui::CollapsingHeader("Match Loading Landscape & File I/O Diagnostics", ImGuiTreeNodeFlags_DefaultOpen)) {
                        ImGui::Checkbox("Fast Match Loading (Bypass Thread Sleep Throttles)", &MK9TradeFix::LoadProfiler::g_FastLoadingEnabled);
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Bypasses artificial 15ms worker thread sleep yields during loading screens,\nallowing asset deserialization to proceed at maximum PC hardware speed.");
                        }
                        ImGui::Spacing();

                        double loadSec  = MK9TradeFix::LoadProfiler::GetLastMatchLoadDuration();
                        double vsSec    = MK9TradeFix::LoadProfiler::GetLastVsScreenDuration();
                        double diskSec  = MK9TradeFix::LoadProfiler::GetLastMatchDiskReadDuration();
                        double sleepSec = MK9TradeFix::LoadProfiler::GetLastMatchSleepDuration();
                        double totalMB  = MK9TradeFix::LoadProfiler::GetLastMatchTotalBytesMB();
                        size_t totalPkgs = MK9TradeFix::LoadProfiler::GetTotalPackagesLoaded();

                        if (loadSec > 0.001) {
                            double diskSpeed = (diskSec > 0.001) ? (totalMB / diskSec) : 0.0;
                            double cpuSec = loadSec - diskSec - sleepSec;
                            if (cpuSec < 0.0) cpuSec = 0.0;

                            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f), "Actual VS Loading Screen: %.3f seconds", vsSec);
                            ImGui::Text("Total Select & Load:     %.3f seconds", loadSec);
                            ImGui::BulletText("Packages Loaded:  %zu files (%.1f MB)", totalPkgs, totalMB);
                            ImGui::BulletText("Disk Read Time:   %.3f s (%.1f%% of total) @ %.1f MB/s", diskSec, (diskSec / loadSec) * 100.0, diskSpeed);
                            ImGui::BulletText("Thread Sleeps:    %.3f s (%.1f%% of total)", sleepSec, (sleepSec / loadSec) * 100.0);
                            ImGui::BulletText("CPU Decompress:   %.3f s (%.1f%% of total)", cpuSec, (cpuSec / loadSec) * 100.0);
                        } else if (MK9TradeFix::LoadProfiler::IsMatchLoading()) {
                            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f), "Currently Loading Match... (Reading packages from disk)");
                        } else {
                            ImGui::TextDisabled("No match load completed yet. Load into any stage to view the full timing breakdown.");
                        }
                        ImGui::Spacing();
                        ImGui::Separator();
                        ImGui::Spacing();
                    }
                    
                    float fps = ImGui::GetIO().Framerate;
                    float ms = (fps > 0.0f) ? (1000.0f / fps) : 16.67f;
                    double totalAvgUs = ModProfiler::GetTotalAvgUs();
                    double frameBudgetPct = (totalAvgUs / 16666.67) * 100.0;

                    ImGui::TextColored(ImVec4(0.4f, 0.9f, 1.0f, 1.0f), "Live Game Loop & Frame Budget:");
                    ImGui::Text("Simulation FPS:   %.1f FPS  (%.2f ms / frame)", fps, ms);
                    
                    ImVec4 budgetColor = (totalAvgUs < 200.0) ? ImVec4(0.2f, 1.0f, 0.3f, 1.0f) :
                                         ((totalAvgUs < 800.0) ? ImVec4(1.0f, 0.85f, 0.2f, 1.0f) : ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
                    
                    ImGui::TextColored(budgetColor, "Total Mod Cost:   %.2f us / 16,666.7 us  (%.2f%% of 60 FPS frame budget)", totalAvgUs, frameBudgetPct);
                    ImGui::TextDisabled("Hardware Counter: QueryPerformanceCounter (Resolution: < 0.01 us, Zero Heap Allocations)");
                    
                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();

                    bool profEnabled = ModProfiler::IsEnabled();
                    if (ImGui::Checkbox("Enable Hardware Micro-Profiler", &profEnabled)) {
                        ModProfiler::SetEnabled(profEnabled);
                    }
                    ImGui::SameLine(320);
                    if (ImGui::Button("Reset Peak Spikes")) {
                        ModProfiler::ResetPeaks();
                    }

                    ImGui::Spacing();
                    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "Subsystem Microsecond Execution Breakdown:");
                    
                    if (ImGui::BeginTable("ProfilerMetricsTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
                        ImGui::TableSetupColumn("Subsystem", ImGuiTableColumnFlags_WidthStretch, 2.2f);
                        ImGui::TableSetupColumn("Avg Execution", ImGuiTableColumnFlags_WidthFixed, 105.0f);
                        ImGui::TableSetupColumn("Peak Spike", ImGuiTableColumnFlags_WidthFixed, 95.0f);
                        ImGui::TableSetupColumn("Frame Cost", ImGuiTableColumnFlags_WidthFixed, 85.0f);
                        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 85.0f);
                        ImGui::TableHeadersRow();

                        SubsystemMetric* metrics = ModProfiler::GetMetrics();
                        int count = ModProfiler::GetCount();
                        for (int i = 0; i < count; ++i) {
                            ImGui::TableNextRow();
                            
                            // Col 0: Name & Tooltip
                            ImGui::TableNextColumn();
                            ImGui::Text("%s", metrics[i].name);
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("%s", metrics[i].description);
                            }

                            // Col 1: Avg us
                            ImGui::TableNextColumn();
                            ImGui::Text("%.2f us", metrics[i].avgUs);

                            // Col 2: Peak us
                            ImGui::TableNextColumn();
                            ImGui::Text("%.2f us", metrics[i].peakUs);

                            // Col 3: Budget %
                            ImGui::TableNextColumn();
                            double costPct = (metrics[i].avgUs / 16666.67) * 100.0;
                            ImGui::Text("%.3f%%", costPct);

                            // Col 4: Status Badge
                            ImGui::TableNextColumn();
                            if (metrics[i].avgUs < 100.0) {
                                ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.3f, 1.0f), "[OPTIMAL]");
                            } else if (metrics[i].avgUs < 400.0) {
                                ImGui::TextColored(ImVec4(0.4f, 0.9f, 1.0f, 1.0f), "[GOOD]");
                            } else if (metrics[i].avgUs < 1000.0) {
                                ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f), "[MODERATE]");
                            } else {
                                ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "[HEAVY]");
                            }
                        }
                        ImGui::EndTable();
                    }

                    ImGui::Spacing();
                    ImGui::TextDisabled("Note: A 60 FPS fighting game frame lasts 16,666.7 microseconds (16.67 ms). Mod overhead < 500 us (< 3%) guarantees zero frame drops.");

                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();

                    if (ImGui::CollapsingHeader("Diagnostic Test Bot Suites (Modes 1 - 16)", ImGuiTreeNodeFlags_DefaultOpen)) {
                        bool isAnyPaused = MK9TradeFix::IsGameOrSimulationPaused();
                        bool testBotsAllowed = inMatch && !isAnyPaused;
                        if (!testBotsAllowed) {
                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
                            if (!inMatch) {
                                ImGui::Text("[RESTRICTED: Diagnostic test bots require an active match]");
                            } else {
                                ImGui::Text("[RESTRICTED: Diagnostic test bots require game simulation to be unpaused]");
                            }
                            ImGui::PopStyleColor();
                            ImGui::BeginDisabled();
                        }
                        RenderTestBotDiagnosticsUI();
                        if (!testBotsAllowed) {
                            ImGui::EndDisabled();
                        }
                    }

                    ImGui::EndTabItem();
                }

                // =========================================================
                // TAB 5: Live Console Logs
                // =========================================================
                if (ImGui::BeginTabItem("Console Logs")) {
                    RenderConsoleLogsUI();
                    ImGui::EndTabItem();
                }

                // Stashed & Hidden: In-Match Animation Viewer Tab
                /*
                if (ImGui::BeginTabItem("Anim Viewer")) {
                    MK9TradeFix::AnimViewer::RenderImGuiTab();
                    ImGui::EndTabItem();
                }
                */

                ImGui::EndTabBar();
            }
        }
        ImGui::End();
    }

    static void RenderFrameDataVisualizer() {
        if (!MK9TradeFix::g_FrameDataVisualizerEnabled || !MK9TradeFix::IsMatchActive()) return;

        MK9TradeFix::FighterFrameTimeline p1Data, p2Data;
        MK9TradeFix::GetLiveFrameDataTimelines(p1Data, p2Data);

        const float blockWidth = 10.0f;
        const float blockHeight = 18.0f;
        const int totalBlocks = MK9TradeFix::FighterFrameTimeline::CAPACITY; // 75 blocks
        const float timelineWidth = totalBlocks * blockWidth;

        ImGuiIO& io = ImGui::GetIO();
        io.ConfigWindowsMoveFromTitleBarOnly = false; // Allow dragging from anywhere on the window!

        float estWidth = timelineWidth + 24.0f;
        float estHeight = 85.0f;

        static bool s_hasInitializedPos = false;
        if ((g_ResetFrameBarPos || !s_hasInitializedPos) && io.DisplaySize.x > 0.0f && io.DisplaySize.y > 0.0f) {
            float posX = (io.DisplaySize.x - estWidth) * 0.5f;
            float posY = io.DisplaySize.y - estHeight - 25.0f;
            ImGui::SetNextWindowPos(ImVec2(posX, posY), ImGuiCond_Always);
            s_hasInitializedPos = true;
            g_ResetFrameBarPos = false;
        }

        ImGui::SetNextWindowBgAlpha(MK9TradeFix::g_FrameBarOpacity);

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | 
                                 ImGuiWindowFlags_AlwaysAutoResize | 
                                 ImGuiWindowFlags_NoSavedSettings;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 6.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.06f, 0.07f, 0.09f, MK9TradeFix::g_FrameBarOpacity));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.20f, 0.22f, 0.26f, (MK9TradeFix::g_FrameBarOpacity > 0.05f) ? (MK9TradeFix::g_FrameBarOpacity * 0.90f) : 0.0f));

        if (ImGui::Begin("MK9 Frame Data Timeline", nullptr, flags)) {

            // -------------------------------------------------------------
            // ROW 1: Player 1 Telemetry Header
            // -------------------------------------------------------------
            char p1LeftText[128];
            if (p1Data.startupFrames > 0 || p1Data.totalFrames > 0) {
                const char* advStr = "--";
                char advBuf[32];
                if (p1Data.hasAdvantage) {
                    if (p1Data.isInfiniteAdvantage) {
                        advStr = "oo";
                    } else if (p1Data.advantage > 0) {
                        snprintf(advBuf, sizeof(advBuf), "+%d", p1Data.advantage);
                        advStr = advBuf;
                    } else {
                        snprintf(advBuf, sizeof(advBuf), "%d", p1Data.advantage);
                        advStr = advBuf;
                    }
                }
                char startBuf[16] = "--";
                if (p1Data.startupFrames > 0) {
                    snprintf(startBuf, sizeof(startBuf), "%dF", p1Data.startupFrames);
                }
                char actBuf[16] = "--";
                if (p1Data.activeFrames > 0) {
                    snprintf(actBuf, sizeof(actBuf), "%dF", p1Data.activeFrames);
                }
                char totBuf[16] = "--";
                if (p1Data.totalFrames > 0) {
                    snprintf(totBuf, sizeof(totBuf), "%dF", p1Data.totalFrames);
                }
                snprintf(p1LeftText, sizeof(p1LeftText), "Startup %s / Active %s / Total %s / Advantage %s", 
                         startBuf, actBuf, totBuf, advStr);
            } else {
                snprintf(p1LeftText, sizeof(p1LeftText), "Startup -- / Active -- / Total -- / Advantage --");
            }

            ImGui::TextColored(ImVec4(0.92f, 0.92f, 0.95f, 1.0f), "%s", p1LeftText);
            if (p1Data.isQuickRolling) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.16f, 0.84f, 0.41f, 1.0f), "[QUICK ROLL / TECH ROLL]");
            } else if (p1Data.isWakeupInvincible) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.71f, 0.27f, 1.00f, 1.0f), "[INVINCIBLE WAKE-UP]");
            } else if (p1Data.isWakeupAttack) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.85f, 0.40f, 1.00f, 1.0f), "[WAKE-UP ATTACK]");
            } else if (p1Data.isDelayedWakeup) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.07f, 0.85f, 0.75f, 1.0f), "[DELAYED WAKEUP]");
            }
            ImGui::SameLine();
            float p1RightTextWidth = ImGui::CalcTextSize("Player 1").x;
            ImGui::SetCursorPosX(timelineWidth - p1RightTextWidth);
            ImGui::TextColored(ImVec4(0.70f, 0.72f, 0.78f, 1.0f), "Player 1");

            // -------------------------------------------------------------
            // ROW 2: Player 1 Timeline Bar
            // -------------------------------------------------------------
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            ImVec2 p1BarPos = ImGui::GetCursorScreenPos();

            int lastDrawnP1 = -10;
            for (int i = 0; i < totalBlocks; i++) {
                ImVec2 cellMin = ImVec2(p1BarPos.x + i * blockWidth, p1BarPos.y);
                ImVec2 cellMax = ImVec2(cellMin.x + blockWidth, cellMin.y + blockHeight);

                uint8_t state = p1Data.blocks[i].state;
                uint8_t phaseNum = p1Data.blocks[i].frameNumberInPhase;
                uint8_t prevState = (i > 0) ? p1Data.blocks[i - 1].state : 0;
                bool isPhaseStart = (state != 0 && (state != prevState || phaseNum == 1));

                ImU32 fillColor = IM_COL32(20, 22, 28, 255); // Default dark neutral
                if (state == 1) {
                    // Startup: Pink
                    fillColor = IM_COL32(235, 55, 125, 255);
                } else if (state == 2) {
                    // Active: Red
                    fillColor = IM_COL32(230, 35, 45, 255);
                } else if (state == 3) {
                    // Recovery: Blue
                    fillColor = IM_COL32(25, 135, 225, 255);
                } else if (state == 4) {
                    // Hitstun / Stumble: Yellow
                    fillColor = IM_COL32(245, 225, 45, 255);
                } else if (state == 5) {
                    // Blockstun / Stun Block: Vibrant Cyan
                    fillColor = IM_COL32(0, 215, 240, 255);
                } else if (state == 6) {
                    // Dash / Mobility Cancel: Vibrant Orange
                    fillColor = IM_COL32(255, 140, 20, 255);
                } else if (state == 7) {
                    // Quick Roll / Tech Roll: Emerald Green
                    fillColor = IM_COL32(40, 215, 105, 255);
                } else if (state == 8) {
                    // Invincible Wake-up Attack: Royal Electric Violet
                    fillColor = IM_COL32(180, 70, 255, 255);
                } else if (state == 9) {
                    // Delayed Wakeup: Deep Sea Teal
                    fillColor = IM_COL32(18, 185, 165, 255);
                }

                drawList->AddRectFilled(cellMin, cellMax, fillColor);
                // Inner 1px divider
                drawList->AddRect(cellMin, cellMax, IM_COL32(10, 10, 14, 255), 0.0f, 0, 1.0f);

                // Smart Milestone Number Labeling (prevent digit collision / overlapping)
                bool shouldDrawNumber = false;
                if (state != 0 && phaseNum > 0) {
                    if (isPhaseStart) {
                        if (i - lastDrawnP1 >= 2) {
                            shouldDrawNumber = true;
                        }
                    } else if (phaseNum % 5 == 0) {
                        if (i - lastDrawnP1 >= 3) {
                            shouldDrawNumber = true;
                        }
                    }
                }

                if (shouldDrawNumber) {
                    char numBuf[8];
                    snprintf(numBuf, sizeof(numBuf), "%u", phaseNum);
                    ImVec2 numSize = ImGui::CalcTextSize(numBuf);
                    ImVec2 textPos = ImVec2(cellMin.x + (blockWidth - numSize.x) * 0.5f,
                                            cellMin.y + (blockHeight - numSize.y) * 0.5f);
                    // High-contrast text shadow for readability
                    drawList->AddText(ImVec2(textPos.x + 1.0f, textPos.y + 1.0f), IM_COL32(0, 0, 0, 220), numBuf);
                    drawList->AddText(textPos, IM_COL32(255, 255, 255, 255), numBuf);
                    lastDrawnP1 = i;
                }

                // Interactive Cell Hover
                if (ImGui::IsMouseHoveringRect(cellMin, cellMax)) {
                    drawList->AddRect(cellMin, cellMax, IM_COL32(255, 255, 255, 240), 0.0f, 0, 1.5f);
                    const char* stateNames[] = {
                        "Neutral / Idle",
                        "Startup",
                        "Active",
                        "Recovery",
                        "Hitstun / Stumble",
                        "Blockstun",
                        "Dash / Cancel",
                        "Quick Roll / Tech Roll",
                        "Invincible Wake-up Attack",
                        "Delayed Wakeup"
                    };
                    const char* sName = (state <= 9) ? stateNames[state] : "Unknown";
                    ImGui::BeginTooltip();
                    ImGui::TextColored(ImVec4(0.4f, 0.9f, 1.0f, 1.0f), "[Player 1] Frame %d / %d", i + 1, totalBlocks);
                    ImGui::Separator();
                    ImGui::Text("State: %s", sName);
                    if (phaseNum > 0) {
                        ImGui::Text("Frame in Phase: %u", phaseNum);
                    }
                    ImGui::EndTooltip();
                }
            }

            // Outer border around Player 1 bar
            drawList->AddRect(p1BarPos, ImVec2(p1BarPos.x + timelineWidth, p1BarPos.y + blockHeight), 
                              IM_COL32(0, 0, 0, 255), 0.0f, 0, 2.0f);

            ImGui::Dummy(ImVec2(timelineWidth, blockHeight));
            ImGui::Spacing();

            // -------------------------------------------------------------
            // ROW 3: Player 2 Timeline Bar
            // -------------------------------------------------------------
            ImVec2 p2BarPos = ImGui::GetCursorScreenPos();
            int lastDrawnP2 = -10;

            for (int i = 0; i < totalBlocks; i++) {
                ImVec2 cellMin = ImVec2(p2BarPos.x + i * blockWidth, p2BarPos.y);
                ImVec2 cellMax = ImVec2(cellMin.x + blockWidth, cellMin.y + blockHeight);

                uint8_t state = p2Data.blocks[i].state;
                uint8_t phaseNum = p2Data.blocks[i].frameNumberInPhase;
                uint8_t prevState = (i > 0) ? p2Data.blocks[i - 1].state : 0;
                bool isPhaseStart = (state != 0 && (state != prevState || phaseNum == 1));

                ImU32 fillColor = IM_COL32(20, 22, 28, 255); // Default dark neutral
                if (state == 1) {
                    // Startup: Pink
                    fillColor = IM_COL32(235, 55, 125, 255);
                } else if (state == 2) {
                    // Active: Red
                    fillColor = IM_COL32(230, 35, 45, 255);
                } else if (state == 3) {
                    // Recovery: Blue
                    fillColor = IM_COL32(25, 135, 225, 255);
                } else if (state == 4) {
                    // Hitstun / Stumble: Yellow
                    fillColor = IM_COL32(245, 225, 45, 255);
                } else if (state == 5) {
                    // Blockstun / Stun Block: Vibrant Cyan
                    fillColor = IM_COL32(0, 215, 240, 255);
                } else if (state == 6) {
                    // Dash / Mobility Cancel: Vibrant Orange
                    fillColor = IM_COL32(255, 140, 20, 255);
                } else if (state == 7) {
                    // Quick Roll / Tech Roll: Emerald Green
                    fillColor = IM_COL32(40, 215, 105, 255);
                } else if (state == 8) {
                    // Invincible Wake-up Attack: Royal Electric Violet
                    fillColor = IM_COL32(180, 70, 255, 255);
                } else if (state == 9) {
                    // Delayed Wakeup: Deep Sea Teal
                    fillColor = IM_COL32(18, 185, 165, 255);
                }

                drawList->AddRectFilled(cellMin, cellMax, fillColor);
                // Inner 1px divider
                drawList->AddRect(cellMin, cellMax, IM_COL32(10, 10, 14, 255), 0.0f, 0, 1.0f);

                // Smart Milestone Number Labeling (prevent digit collision / overlapping)
                bool shouldDrawNumber = false;
                if (state != 0 && phaseNum > 0) {
                    if (isPhaseStart) {
                        if (i - lastDrawnP2 >= 2) {
                            shouldDrawNumber = true;
                        }
                    } else if (phaseNum % 5 == 0) {
                        if (i - lastDrawnP2 >= 3) {
                            shouldDrawNumber = true;
                        }
                    }
                }

                if (shouldDrawNumber) {
                    char numBuf[8];
                    snprintf(numBuf, sizeof(numBuf), "%u", phaseNum);
                    ImVec2 numSize = ImGui::CalcTextSize(numBuf);
                    ImVec2 textPos = ImVec2(cellMin.x + (blockWidth - numSize.x) * 0.5f,
                                            cellMin.y + (blockHeight - numSize.y) * 0.5f);
                    // High-contrast text shadow for readability
                    drawList->AddText(ImVec2(textPos.x + 1.0f, textPos.y + 1.0f), IM_COL32(0, 0, 0, 220), numBuf);
                    drawList->AddText(textPos, IM_COL32(255, 255, 255, 255), numBuf);
                    lastDrawnP2 = i;
                }

                // Interactive Cell Hover
                if (ImGui::IsMouseHoveringRect(cellMin, cellMax)) {
                    drawList->AddRect(cellMin, cellMax, IM_COL32(255, 255, 255, 240), 0.0f, 0, 1.5f);
                    const char* stateNames[] = {
                        "Neutral / Idle",
                        "Startup",
                        "Active",
                        "Recovery",
                        "Hitstun / Stumble",
                        "Blockstun",
                        "Dash / Cancel",
                        "Quick Roll / Tech Roll",
                        "Invincible Wake-up Attack",
                        "Delayed Wakeup"
                    };
                    const char* sName = (state <= 9) ? stateNames[state] : "Unknown";
                    ImGui::BeginTooltip();
                    ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "[Player 2] Frame %d / %d", i + 1, totalBlocks);
                    ImGui::Separator();
                    ImGui::Text("State: %s", sName);
                    if (phaseNum > 0) {
                        ImGui::Text("Frame in Phase: %u", phaseNum);
                    }
                    ImGui::EndTooltip();
                }
            }

            // Outer border around Player 2 bar
            drawList->AddRect(p2BarPos, ImVec2(p2BarPos.x + timelineWidth, p2BarPos.y + blockHeight), 
                              IM_COL32(0, 0, 0, 255), 0.0f, 0, 2.0f);

            ImGui::Dummy(ImVec2(timelineWidth, blockHeight));

            // -------------------------------------------------------------
            // ROW 4: Player 2 Telemetry Footer
            // -------------------------------------------------------------
            char p2LeftText[128];
            if (p2Data.startupFrames > 0 || p2Data.totalFrames > 0) {
                const char* advStr = "--";
                char advBuf[32];
                if (p2Data.hasAdvantage) {
                    if (p2Data.isInfiniteAdvantage) {
                        advStr = "oo";
                    } else if (p2Data.advantage > 0) {
                        snprintf(advBuf, sizeof(advBuf), "+%d", p2Data.advantage);
                        advStr = advBuf;
                    } else {
                        snprintf(advBuf, sizeof(advBuf), "%d", p2Data.advantage);
                        advStr = advBuf;
                    }
                }
                char startBuf[16] = "--";
                if (p2Data.startupFrames > 0) {
                    snprintf(startBuf, sizeof(startBuf), "%dF", p2Data.startupFrames);
                }
                char actBuf[16] = "--";
                if (p2Data.activeFrames > 0) {
                    snprintf(actBuf, sizeof(actBuf), "%dF", p2Data.activeFrames);
                }
                char totBuf[16] = "--";
                if (p2Data.totalFrames > 0) {
                    snprintf(totBuf, sizeof(totBuf), "%dF", p2Data.totalFrames);
                }
                snprintf(p2LeftText, sizeof(p2LeftText), "Startup %s / Active %s / Total %s / Advantage %s", 
                         startBuf, actBuf, totBuf, advStr);
            } else {
                snprintf(p2LeftText, sizeof(p2LeftText), "Startup -- / Active -- / Total -- / Advantage --");
            }

            ImGui::TextColored(ImVec4(0.92f, 0.92f, 0.95f, 1.0f), "%s", p2LeftText);
            if (p2Data.isQuickRolling) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.16f, 0.84f, 0.41f, 1.0f), "[QUICK ROLL / TECH ROLL]");
            } else if (p2Data.isWakeupInvincible) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.71f, 0.27f, 1.00f, 1.0f), "[INVINCIBLE WAKE-UP]");
            } else if (p2Data.isWakeupAttack) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.85f, 0.40f, 1.00f, 1.0f), "[WAKE-UP ATTACK]");
            } else if (p2Data.isDelayedWakeup) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.07f, 0.85f, 0.75f, 1.0f), "[DELAYED WAKEUP]");
            }
            ImGui::SameLine();
            float p2RightTextWidth = ImGui::CalcTextSize("Player 2").x;
            ImGui::SetCursorPosX(timelineWidth - p2RightTextWidth);
            ImGui::TextColored(ImVec4(0.70f, 0.72f, 0.78f, 1.0f), "Player 2");

            // Live Normal Cancel Notice
            char p1CancelNotice[128] = "";
            MK9TradeFix::CancelDetector::FormatCancelNotice(1, p1CancelNotice, sizeof(p1CancelNotice));
            char p2CancelNotice[128] = "";
            MK9TradeFix::CancelDetector::FormatCancelNotice(2, p2CancelNotice, sizeof(p2CancelNotice));

            if (p1CancelNotice[0] != '\0' || p2CancelNotice[0] != '\0') {
                ImGui::Spacing();
                ImGui::Separator();
                if (p1CancelNotice[0] != '\0') {
                    ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f), "[P1] %s", p1CancelNotice);
                }
                if (p2CancelNotice[0] != '\0') {
                    if (p1CancelNotice[0] != '\0') ImGui::SameLine(timelineWidth * 0.5f);
                    ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f), "[P2] %s", p2CancelNotice);
                }
            }

            // Right-click context popup on the Frame Bar for live opacity & position controls
            if (ImGui::BeginPopupContextWindow("FrameBarContextMenu")) {
                ImGui::Text("Frame Bar Settings");
                ImGui::Separator();
                ImGui::SliderFloat("Opacity", &MK9TradeFix::g_FrameBarOpacity, 0.0f, 1.0f, "%.2f");
                if (ImGui::MenuItem("Fully Transparent (0.00)")) {
                    MK9TradeFix::g_FrameBarOpacity = 0.0f;
                }
                if (ImGui::MenuItem("Default Glass (0.25)")) {
                    MK9TradeFix::g_FrameBarOpacity = 0.25f;
                }
                if (ImGui::MenuItem("Medium Tint (0.50)")) {
                    MK9TradeFix::g_FrameBarOpacity = 0.50f;
                }
                if (ImGui::MenuItem("Default (0.88)")) {
                    MK9TradeFix::g_FrameBarOpacity = 0.88f;
                }
                if (ImGui::MenuItem("Solid Background (1.00)")) {
                    MK9TradeFix::g_FrameBarOpacity = 1.00f;
                }
                ImGui::Separator();
                if (ImGui::BeginMenu("Color Glossary & Legend")) {
                    ImGui::TextColored(ImVec4(0.92f, 0.22f, 0.49f, 1.0f), "[■] Startup (Pink)");
                    ImGui::SameLine(); ImGui::TextDisabled("- Move wind-up before hitboxes activate");

                    ImGui::TextColored(ImVec4(0.90f, 0.14f, 0.18f, 1.0f), "[■] Active (Red)");
                    ImGui::SameLine(); ImGui::TextDisabled("- Active strike frames dealing damage");

                    ImGui::TextColored(ImVec4(0.10f, 0.53f, 0.88f, 1.0f), "[■] Recovery (Blue)");
                    ImGui::SameLine(); ImGui::TextDisabled("- Post-attack cooldown frames");

                    ImGui::TextColored(ImVec4(0.96f, 0.88f, 0.18f, 1.0f), "[■] Hitstun (Yellow)");
                    ImGui::SameLine(); ImGui::TextDisabled("- Hurt reaction or floor knockdown");

                    ImGui::TextColored(ImVec4(0.00f, 0.84f, 0.94f, 1.0f), "[■] Blockstun (Cyan)");
                    ImGui::SameLine(); ImGui::TextDisabled("- Guard recoil while blocking");

                    ImGui::TextColored(ImVec4(1.00f, 0.55f, 0.08f, 1.0f), "[■] Dash (Orange)");
                    ImGui::SameLine(); ImGui::TextDisabled("- Forward/back dash mobility frames");

                    ImGui::TextColored(ImVec4(0.16f, 0.84f, 0.41f, 1.0f), "[■] Quick Roll (Emerald Green)");
                    ImGui::SameLine(); ImGui::TextDisabled("- Tech roll on knockdown floor impact (F, B)");

                    ImGui::TextColored(ImVec4(0.71f, 0.27f, 1.00f, 1.0f), "[■] Invincible Wakeup (Royal Violet)");
                    ImGui::SameLine(); ImGui::TextDisabled("- Wake-up reversal with invulnerability");

                    ImGui::EndMenu();
                } else {
                    ImGui::TextDisabled("Color Legend (Hover for details):");
                    auto LegendItem = [](const ImVec4& color, const char* label, const char* tip) {
                        ImGui::TextColored(color, "[■] %s", label);
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
                    };
                    LegendItem(ImVec4(0.92f, 0.22f, 0.49f, 1.0f), "Startup", "Move wind-up frames before hitboxes activate");
                    ImGui::SameLine();
                    LegendItem(ImVec4(0.90f, 0.14f, 0.18f, 1.0f), "Active", "Active striking frames where hitboxes can connect");
                    ImGui::SameLine();
                    LegendItem(ImVec4(0.10f, 0.53f, 0.88f, 1.0f), "Recovery", "Post-attack cooldown frames before attacker can act");
                    LegendItem(ImVec4(0.96f, 0.88f, 0.18f, 1.0f), "Hitstun", "Hurt reaction, air juggle tumble, or floor knockdown");
                    ImGui::SameLine();
                    LegendItem(ImVec4(0.00f, 0.84f, 0.94f, 1.0f), "Blockstun", "Defensive guard recoil frames while blocking");
                    ImGui::SameLine();
                    LegendItem(ImVec4(1.00f, 0.55f, 0.08f, 1.0f), "Dash", "Forward/back dash or dash-cancel mobility frames");
                    LegendItem(ImVec4(0.16f, 0.84f, 0.41f, 1.0f), "Quick Roll", "Tech roll recovery executed via Forward -> Back upon floor impact");
                    ImGui::SameLine();
                    LegendItem(ImVec4(0.71f, 0.27f, 1.00f, 1.0f), "Invincible Wakeup", "Wake-up reversal special attack with startup invulnerability");
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Reset Position (Center Bottom)")) {
                    g_ResetFrameBarPos = true;
                }
                if (ImGui::MenuItem("Hide Visualizer (F9)")) {
                    MK9TradeFix::g_FrameDataVisualizerEnabled = false;
                }
                ImGui::EndPopup();
            }
        }
        ImGui::End();

        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(2);
    }

    static void RenderRecordingHUD() {
        auto& rec = MK9TradeFix::InputRecordingManager::Get();
        if (!rec.IsHUDActive()) return;
        bool targetNeedsHuman = rec.IsRecording() ? (rec.GetRecordedTargetPlayer() == 2) : (rec.GetPlaybackTargetPlayer() == 2);
        if (!MK9TradeFix::IsTrainingMode() || (targetNeedsHuman && !MK9TradeFix::IsP2HumanControl())) {
            rec.StopRecording();
            rec.StopPlayback();
            rec.SetHUDActive(false);
            return;
        }

        ImGuiIO& io = ImGui::GetIO();
        float screenW = io.DisplaySize.x;
        float windowW = 380.0f;

        static bool s_hasInitPos = false;
        if (!s_hasInitPos && screenW > 0.0f) {
            ImGui::SetNextWindowPos(ImVec2((screenW - windowW) * 0.5f, 25.0f), ImGuiCond_FirstUseEver);
            s_hasInitPos = true;
        }

        ImGui::SetNextWindowSize(ImVec2(windowW, 0.0f), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.88f);

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | 
                                 ImGuiWindowFlags_AlwaysAutoResize | 
                                 ImGuiWindowFlags_NoSavedSettings;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 10.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.07f, 0.07f, 0.10f, 0.92f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.55f, 0.20f, 0.75f, 0.90f));

        if (ImGui::Begin("PracticeDummyRecordingHUD", nullptr, flags)) {
            int slotNum = rec.GetActiveSlotIndex() + 1;
            auto state = rec.GetState();

            // Header Row: Slot badge & Mode title + [ X ] close button
            ImGui::TextColored(ImVec4(0.75f, 0.40f, 1.0f, 1.0f), "[ PRACTICE DUMMY RECORDER ]");
            ImGui::SameLine(ImGui::GetWindowWidth() - 110.0f);
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f), "SLOT %d", slotNum);
            ImGui::SameLine();
            if (ImGui::SmallButton(" X ")) {
                rec.StopRecording();
                rec.StopPlayback();
                rec.SetHUDActive(false);
            }

            ImGui::Separator();
            ImGui::Spacing();

            // 1. COUNTDOWN STATE
            if (state == MK9TradeFix::RecordingState::COUNTDOWN) {
                int countNum = rec.GetCountdownDisplayNumber();
                ImVec4 countColor = (countNum == 1) ? ImVec4(1.0f, 0.2f, 0.2f, 1.0f) :
                                    ((countNum == 2) ? ImVec4(1.0f, 0.8f, 0.2f, 1.0f) : ImVec4(0.3f, 1.0f, 0.4f, 1.0f));
                
                ImGui::TextColored(countColor, "COUNTDOWN: %d...", countNum);
                ImGui::ProgressBar(rec.GetCountdownProgressFraction(), ImVec2(-1, 14), "");
                ImGui::TextDisabled("Get ready! Recording from P2 Gamepad / Controller starts at 0.");
            }
            // 2. WAITING FOR INPUT (AUTO TRIGGER)
            else if (state == MK9TradeFix::RecordingState::WAITING_FOR_INPUT) {
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "[ARMED - WAITING FOR INPUT]");
                ImGui::Text("Move or press any button on Player 2 to start recording!");
            }
            // 3. ACTIVE RECORDING
            else if (state == MK9TradeFix::RecordingState::RECORDING) {
                static float s_BlinkTimer = 0.0f;
                s_BlinkTimer += io.DeltaTime;
                bool blink = ((int)(s_BlinkTimer * 2.5f) % 2) == 0;

                auto& slot = rec.GetSlot(rec.GetActiveSlotIndex());
                const char* curChar = (slot.characterName[0] != '\0') ? slot.characterName : "P2 Dummy";
                ImGui::TextColored(blink ? ImVec4(1.0f, 0.2f, 0.2f, 1.0f) : ImVec4(0.6f, 0.1f, 0.1f, 1.0f),
                                   "● RECORDING ACTIVE (Slot %d: %s)", slotNum, curChar);
                
                uint32_t cur = rec.GetCurrentRecordProgress();
                uint32_t maxF = MK9TradeFix::InputRecordingManager::MAX_RECORD_FRAMES;
                float progress = (float)cur / (float)maxF;
                uint32_t curSec = cur / 60;
                uint32_t maxSec = maxF / 60;
                
                char overlayTxt[64];
                snprintf(overlayTxt, sizeof(overlayTxt), "%02u:%02u / %02u:%02u (%uF)",
                         curSec / 60, curSec % 60, maxSec / 60, maxSec % 60, cur);
                ImGui::ProgressBar(progress, ImVec2(-1, 14), overlayTxt);
            }
            // 4. ACTIVE PLAYBACK
            else if (state == MK9TradeFix::RecordingState::PLAYING) {
                auto& slot = rec.GetSlot(rec.GetActiveSlotIndex());
                const char* curChar = (slot.characterName[0] != '\0') ? slot.characterName : "Dummy";
                ImGui::TextColored(ImVec4(0.3f, 0.9f, 1.0f, 1.0f), "▶ PLAYING BACK (Slot %d: %s on P2)", slotNum, curChar);
                
                uint32_t cur = rec.GetCurrentPlaybackProgress();
                uint32_t total = slot.totalFrames;
                float progress = (total > 0) ? ((float)cur / (float)total) : 0.0f;
                uint32_t curSec = cur / 60;
                uint32_t totSec = total / 60;

                char overlayTxt[64];
                snprintf(overlayTxt, sizeof(overlayTxt), "%02u:%02u / %02u:%02u (%uF / %uF)",
                         curSec / 60, curSec % 60, totSec / 60, totSec % 60, cur, total);
                ImGui::ProgressBar(progress, ImVec2(-1, 14), overlayTxt);
            }
            // 5. IDLE / COMPLETED
            else {
                auto& slot = rec.GetSlot(rec.GetActiveSlotIndex());
                if (!slot.isEmpty) {
                    const char* curChar = (slot.characterName[0] != '\0') ? slot.characterName : "Unknown";
                    ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f), "[READY] Slot %d [%s] has %uF recorded.", slotNum, curChar, slot.totalFrames);
                } else {
                    ImGui::TextDisabled("[IDLE] Slot %d is currently empty.", slotNum);
                }
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Bottom Buttons
            if (state == MK9TradeFix::RecordingState::RECORDING || 
                state == MK9TradeFix::RecordingState::WAITING_FOR_INPUT || 
                state == MK9TradeFix::RecordingState::COUNTDOWN) {
                if (ImGui::Button("Stop Recording", ImVec2(140, 24))) {
                    rec.StopRecording();
                }
            } else if (state == MK9TradeFix::RecordingState::PLAYING) {
                if (ImGui::Button("Stop Playback", ImVec2(140, 24))) {
                    rec.StopPlayback();
                }
            } else {
                if (ImGui::Button("Record Slot##hudRec", ImVec2(90, 24))) {
                    rec.StartRecording(rec.GetActiveSlotIndex());
                }
                ImGui::SameLine();
                if (ImGui::Button("Play Slot##hudPlay", ImVec2(90, 24))) {
                    rec.StartPlayback(rec.GetActiveSlotIndex(), 2, 0);
                }
            }

            ImGui::SameLine();
            if (ImGui::Button("Menu (~)", ImVec2(90, 24))) {
                SetConsoleOpen(true);
            }
        }
        ImGui::End();

        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(2);
    }

    static void RenderDummyIdleStanceHUD() {
        auto& rev = MK9TradeFix::ReversalEngine::Get();

        ImGui::SetNextWindowSize(ImVec2(380, 200), ImGuiCond_FirstUseEver);
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.08f, 0.12f, 0.94f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.3f, 0.65f, 0.95f, 0.85f));

        if (ImGui::Begin("Practice Dummy Stance Controls##DummyStanceHUD", nullptr, flags)) {
            auto currentStance = rev.GetDummyIdleStance();
            const char* stanceName = (currentStance == MK9TradeFix::DummyIdleStance::STANDING) ? "Standing (Default)" :
                                     (currentStance == MK9TradeFix::DummyIdleStance::CROUCHING) ? "Crouching (Duck)" :
                                     (currentStance == MK9TradeFix::DummyIdleStance::STAND_BLOCKING) ? "Stand Blocking" :
                                     (currentStance == MK9TradeFix::DummyIdleStance::CROUCH_BLOCKING) ? "Crouch Blocking" :
                                     (currentStance == MK9TradeFix::DummyIdleStance::AUTO_BLOCK_STAND) ? "Auto Block (Stand)" : "Auto Block (Crouch)";

            ImGui::TextColored(ImVec4(0.4f, 0.9f, 1.0f, 1.0f), "Dummy Posture: ");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "%s", stanceName);
            ImGui::SameLine(ImGui::GetWindowWidth() - 36.0f);
            if (ImGui::SmallButton(" X ##closeStanceHUD")) {
                rev.SetIdleStanceHUDActive(false);
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            auto RenderStanceButton = [&](const char* label, MK9TradeFix::DummyIdleStance stance, float width) {
                bool isActive = (currentStance == stance);
                if (isActive) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.65f, 0.28f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.78f, 0.35f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.25f, 0.90f, 0.40f, 1.0f));
                }
                if (ImGui::Button(label, ImVec2(width, 28))) {
                    rev.SetDummyIdleStance(stance);
                }
                if (isActive) {
                    ImGui::PopStyleColor(3);
                }
            };

            RenderStanceButton("Standing", MK9TradeFix::DummyIdleStance::STANDING, 170.0f);
            ImGui::SameLine();
            RenderStanceButton("Crouch (Duck)", MK9TradeFix::DummyIdleStance::CROUCHING, 170.0f);

            RenderStanceButton("Stand Block", MK9TradeFix::DummyIdleStance::STAND_BLOCKING, 170.0f);
            ImGui::SameLine();
            RenderStanceButton("Crouch Block", MK9TradeFix::DummyIdleStance::CROUCH_BLOCKING, 170.0f);

            RenderStanceButton("Auto Block (Stand)", MK9TradeFix::DummyIdleStance::AUTO_BLOCK_STAND, 170.0f);
            ImGui::SameLine();
            RenderStanceButton("Auto Block (Crouch)", MK9TradeFix::DummyIdleStance::AUTO_BLOCK_CROUCH, 170.0f);

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (ImGui::Button("Menu (~)", ImVec2(90, 24))) {
                SetConsoleOpen(true);
            }
        }
        ImGui::End();

        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar();
    }

    static ImFont* g_PopupFont = nullptr;
    static ImFont* g_MeterFont = nullptr;

    static void RenderSuperMeterPercentHUD() {
        bool showMeter = MK9TradeFix::MeterDrainFix::g_ShowMeterPercentHUD;
        bool showMove = MK9TradeFix::MoveTelemetryHUD::g_ShowMoveHUD;
        if ((!showMeter && !showMove) || !MK9TradeFix::IsMatchActive()) {
            return;
        }

        ImGuiIO& io = ImGui::GetIO();
        float screenW = io.DisplaySize.x;
        float screenH = io.DisplaySize.y;
        if (screenW <= 100.0f || screenH <= 100.0f) return;

        // Use BackgroundDrawList so in-game HUD pills stay under the ImGui Console / Tool windows
        ImDrawList* drawList = ImGui::GetBackgroundDrawList();
        if (!drawList) return;

        // 1. Super Meter Percentage Badges
        if (showMeter) {
            float p1Meter = MK9TradeFix::MeterDrainFix::GetPlayerMeter(1);
            float p2Meter = MK9TradeFix::MeterDrainFix::GetPlayerMeter(2);

            if (p1Meter >= 0.0f || p2Meter >= 0.0f) {
                float exCost = MK9TradeFix::MeterDrainFix::g_CustomMeterCostsEnabled ? 
                               MK9TradeFix::MeterDrainFix::g_CustomEXCost : 0.333333f;
                float brkCost = MK9TradeFix::MeterDrainFix::g_CustomMeterCostsEnabled ? 
                                MK9TradeFix::MeterDrainFix::g_CustomBreakerCost : 0.666667f;
                float xrayCost = MK9TradeFix::MeterDrainFix::g_CustomMeterCostsEnabled ?
                                 MK9TradeFix::MeterDrainFix::g_CustomXRayCost : 1.0f;

                auto DrawMeterPill = [&](float meter, float posX, float posY, bool isP1) {
                    if (meter < 0.0f) return;
                    float pct = meter * 100.0f;
                    char buf[32];
                    snprintf(buf, sizeof(buf), "%.1f%%", pct);

                    // Dynamic color thresholding
                    ImU32 textColor, borderColor, glowColor;
                    if (meter >= (xrayCost - 0.001f)) {
                        textColor = IM_COL32(0, 240, 255, 255);    // Cyan for Max / X-Ray ready
                        borderColor = IM_COL32(0, 180, 255, 220);
                        glowColor = IM_COL32(0, 100, 200, 70);
                    } else if (meter >= brkCost) {
                        textColor = IM_COL32(50, 255, 120, 255);   // Emerald Green for Breaker ready
                        borderColor = IM_COL32(40, 200, 90, 220);
                        glowColor = IM_COL32(20, 140, 50, 60);
                    } else if (meter >= exCost) {
                        textColor = IM_COL32(255, 205, 50, 255);   // Amber Gold for EX ready
                        borderColor = IM_COL32(220, 160, 30, 220);
                        glowColor = IM_COL32(160, 110, 10, 50);
                    } else {
                        textColor = IM_COL32(190, 195, 205, 220);  // Clean Muted Gray
                        borderColor = IM_COL32(75, 80, 95, 180);
                        glowColor = IM_COL32(0, 0, 0, 0);
                    }

                    ImFont* font = g_MeterFont ? g_MeterFont : (g_PopupFont ? g_PopupFont : ImGui::GetFont());
                    float meterFontSize = g_MeterFont ? 20.0f : (g_PopupFont ? 20.0f : (ImGui::GetFontSize() * 1.35f));
                    ImVec2 textSize = font ? font->CalcTextSizeA(meterFontSize, FLT_MAX, 0.0f, buf) : ImGui::CalcTextSize(buf);
                    ImVec2 pad(10.0f, 5.0f);
                    ImVec2 boxMin(posX - pad.x, posY - pad.y);
                    ImVec2 boxMax(posX + textSize.x + pad.x, posY + textSize.y + pad.y);

                    // Subtle outer glow
                    if (glowColor != 0) {
                        drawList->AddRectFilled(ImVec2(boxMin.x - 3.0f, boxMin.y - 3.0f), ImVec2(boxMax.x + 3.0f, boxMax.y + 3.0f), glowColor, 6.0f);
                    }

                    // Dark semi-transparent background pill
                    drawList->AddRectFilled(boxMin, boxMax, IM_COL32(10, 10, 15, 235), 6.0f);
                    // Sleek border
                    drawList->AddRect(boxMin, boxMax, borderColor, 6.0f, 0, 1.8f);

                    // Faux bold drop shadow + crisp bold text
                    drawList->AddText(font, meterFontSize, ImVec2(posX + 1.0f, posY + 1.0f), IM_COL32(0, 0, 0, 220), buf);
                    drawList->AddText(font, meterFontSize, ImVec2(posX, posY), textColor, buf);
                };

                // P1 Meter Badge (Bottom-Left, adjacent to P1 meter dial)
                float p1X = (screenW * 0.155f) + MK9TradeFix::MeterDrainFix::g_P1MeterHUDOffsetX;
                float p1Y = (screenH * 0.952f) + MK9TradeFix::MeterDrainFix::g_P1MeterHUDOffsetY;
                DrawMeterPill(p1Meter, p1X, p1Y, true);

                // P2 Meter Badge (Bottom-Right, adjacent to P2 meter dial)
                float p2X = (screenW * 0.810f) + MK9TradeFix::MeterDrainFix::g_P2MeterHUDOffsetX;
                float p2Y = (screenH * 0.952f) + MK9TradeFix::MeterDrainFix::g_P2MeterHUDOffsetY;
                DrawMeterPill(p2Meter, p2X, p2Y, false);
            }
        }

        // 2. Move ID & Move Name Badges (Directly above each meter dial)
        if (showMove && MK9TradeFix::IsMatchActive()) {
            auto DrawMoveBadge = [&](int playerIdx, float posX, float posY, bool alignRight) {
                char text[96];
                uint32_t colorRGBA = 0;
                if (!MK9TradeFix::MoveTelemetryHUD::GetPlayerMoveTelemetry(playerIdx, text, sizeof(text), &colorRGBA)) {
                    return;
                }

                ImVec2 textSize = ImGui::CalcTextSize(text);
                ImVec2 pad(8.0f, 4.0f);

                float actualX = alignRight ? (posX - textSize.x) : posX;
                ImVec2 boxMin(actualX - pad.x, posY - pad.y);
                ImVec2 boxMax(actualX + textSize.x + pad.x, posY + textSize.y + pad.y);

                ImU32 textColor = (ImU32)colorRGBA;
                ImU32 borderColor = (textColor & 0x00FFFFFF) | 0xB0000000;
                ImU32 glowColor   = (textColor & 0x00FFFFFF) | 0x30000000;

                // Subtle outer glow
                drawList->AddRectFilled(ImVec2(boxMin.x - 2.0f, boxMin.y - 2.0f), ImVec2(boxMax.x + 2.0f, boxMax.y + 2.0f), glowColor, 6.0f);
                // Dark background pill
                drawList->AddRectFilled(boxMin, boxMax, IM_COL32(12, 14, 18, 235), 5.0f);
                // Sleek border
                drawList->AddRect(boxMin, boxMax, borderColor, 5.0f, 0, 1.2f);
                // Crisp text
                drawList->AddText(ImVec2(actualX, posY), textColor, text);
            };

            // P1 Move Badge (Above P1 meter gauge, left-aligned)
            float p1MoveX = (screenW * 0.155f) + MK9TradeFix::MoveTelemetryHUD::g_P1MoveHUDOffsetX;
            float p1MoveY = (screenH * 0.916f) + MK9TradeFix::MoveTelemetryHUD::g_P1MoveHUDOffsetY;
            DrawMoveBadge(1, p1MoveX, p1MoveY, false);

            // P2 Move Badge (Above P2 meter gauge, right-aligned to match dial boundary)
            float p2MoveX = (screenW * 0.845f) + MK9TradeFix::MoveTelemetryHUD::g_P2MoveHUDOffsetX;
            float p2MoveY = (screenH * 0.916f) + MK9TradeFix::MoveTelemetryHUD::g_P2MoveHUDOffsetY;
            DrawMoveBadge(2, p2MoveX, p2MoveY, true);
        }
    }

    static void RenderHitIndicationHUD() {
        if (!MK9TradeFix::g_FixEnabled || !MK9TradeFix::HitIndicationHUD::g_ShowHitIndicationHUD || !MK9TradeFix::IsMatchActive()) {
            return;
        }

        ImGuiIO& io = ImGui::GetIO();
        float screenW = io.DisplaySize.x;
        float screenH = io.DisplaySize.y;
        if (screenW <= 100.0f || screenH <= 100.0f) return;

        // Use BackgroundDrawList so in-game notification badges stay under the ImGui Console / Tool windows
        ImDrawList* drawList = ImGui::GetBackgroundDrawList();
        if (!drawList) return;

        auto DrawPopup = [&](int playerIdx, float baseX, float baseY, bool alignRight) {
            MK9TradeFix::HitIndicationHUD::HitIndicationType type;
            float alpha = 0.0f;
            float slide = 1.0f;
            if (!MK9TradeFix::HitIndicationHUD::GetPlayerPopup(playerIdx, &type, &alpha, &slide) || alpha <= 0.001f) {
                return;
            }

            const char* title = "";
            ImU32 textCol = 0;
            ImU32 borderCol = 0;
            ImU32 glowCol = 0;
            ImU32 bgCol = 0;

            uint8_t aByte = (uint8_t)(alpha * 255.0f);
            uint8_t bgA   = (uint8_t)(alpha * 235.0f);
            uint8_t glowA = (uint8_t)(alpha * 95.0f);

            switch (type) {
                case MK9TradeFix::HitIndicationHUD::HitIndicationType::COUNTER_HIT:
                    title = "COUNTER HIT";
                    textCol   = IM_COL32(255, 195, 30, aByte);   // Electric Amber / Gold
                    borderCol = IM_COL32(255, 160, 10, aByte);   // Deep Gold
                    glowCol   = IM_COL32(255, 140, 0, glowA);    // Amber Glow
                    bgCol     = IM_COL32(18, 14, 6, bgA);        // Deep Dark Amber/Black
                    break;
                case MK9TradeFix::HitIndicationHUD::HitIndicationType::TRADE_HIT:
                    title = "TRADE HIT";
                    textCol   = IM_COL32(0, 235, 255, aByte);    // Electric Cyan
                    borderCol = IM_COL32(0, 190, 240, aByte);    // Vivid Cyan
                    glowCol   = IM_COL32(0, 160, 255, glowA);    // Cyan Bloom
                    bgCol     = IM_COL32(6, 16, 22, bgA);        // Deep Dark Cyan/Black
                    break;
                case MK9TradeFix::HitIndicationHUD::HitIndicationType::FATAL_PUNISH:
                    title = "FATAL PUNISH";
                    textCol   = IM_COL32(255, 50, 75, aByte);    // Menacing Crimson
                    borderCol = IM_COL32(230, 30, 50, aByte);    // Blood Red
                    glowCol   = IM_COL32(255, 20, 40, glowA);    // Crimson Pulse
                    bgCol     = IM_COL32(22, 6, 10, bgA);        // Deep Dark Ruby/Black
                    break;
                default:
                    return;
            }

            // Calculate entrance slide offset (slides in horizontally by up to 35px for prominent pop)
            float slideOffset = (1.0f - slide) * (alignRight ? 35.0f : -35.0f);
            float posX = baseX + slideOffset;
            float posY = baseY;

            ImFont* font = g_PopupFont ? g_PopupFont : ImGui::GetFont();
            float popupFontSize = g_PopupFont ? 26.0f : (ImGui::GetFontSize() * 1.85f);

            ImVec2 textSize = font ? font->CalcTextSizeA(popupFontSize, FLT_MAX, 0.0f, title) : ImGui::CalcTextSize(title);
            ImVec2 pad(22.0f, 10.0f);

            float actualX = alignRight ? (posX - textSize.x) : posX;
            ImVec2 boxMin(actualX - pad.x, posY - pad.y);
            ImVec2 boxMax(actualX + textSize.x + pad.x, posY + textSize.y + pad.y);

            // 1. Soft layered bloom / outer glow
            drawList->AddRectFilled(ImVec2(boxMin.x - 6.0f, boxMin.y - 6.0f),
                                    ImVec2(boxMax.x + 6.0f, boxMax.y + 6.0f),
                                    glowCol, 8.0f);

            // 2. Deep dark background pill
            drawList->AddRectFilled(boxMin, boxMax, bgCol, 6.0f);

            // 3. Colored accent border
            drawList->AddRect(boxMin, boxMax, borderCol, 6.0f, 0, 2.5f);

            // 4. Stylish colored vertical accent tab
            float tabWidth = 6.0f;
            if (alignRight) {
                drawList->AddRectFilled(ImVec2(boxMax.x - tabWidth, boxMin.y + 3.0f),
                                        ImVec2(boxMax.x - 1.0f, boxMax.y - 3.0f),
                                        textCol, 2.5f);
            } else {
                drawList->AddRectFilled(ImVec2(boxMin.x + 1.0f, boxMin.y + 3.0f),
                                        ImVec2(boxMin.x + tabWidth, boxMax.y - 3.0f),
                                        textCol, 2.5f);
            }

            // 5. Large crisp text
            drawList->AddText(font, popupFontSize, ImVec2(actualX, posY), textCol, title);
        };

        // P1 Hit Outcome Pop-up (Below P1 combo counter, left-aligned)
        float p1X = (screenW * 0.14f) + MK9TradeFix::HitIndicationHUD::g_P1IndicationOffsetX;
        float p1Y = (screenH * 0.33f) + MK9TradeFix::HitIndicationHUD::g_P1IndicationOffsetY;
        DrawPopup(1, p1X, p1Y, false);

        // P2 Hit Outcome Pop-up (Below P2 combo counter, right-aligned)
        float p2X = (screenW * 0.86f) + MK9TradeFix::HitIndicationHUD::g_P2IndicationOffsetX;
        float p2Y = (screenH * 0.33f) + MK9TradeFix::HitIndicationHUD::g_P2IndicationOffsetY;
        DrawPopup(2, p2X, p2Y, true);
    }

    static HRESULT WINAPI hkEndScene(IDirect3DDevice9* pDevice) {
        if (!pDevice) return oEndScene ? oEndScene(pDevice) : D3D_OK;

        HRESULT coop = pDevice->TestCooperativeLevel();
        if (coop != D3D_OK) {
            return oEndScene(pDevice);
        }

        MK9TradeFix::LoadProfiler::RecordFirstFrame();
        MK9TradeFix::LoadProfiler::OnFrameTick();

        if (!g_ImGuiInitialized && pDevice) {
            HWND hWnd = *(HWND*)0x00E53BF4;
            if (hWnd && IsWindow(hWnd)) {
                g_hTargetWindow = hWnd;
                oWndProc = (WNDPROC)SetWindowLongPtrA(hWnd, GWLP_WNDPROC, (LONG_PTR)hkWndProc);

                IMGUI_CHECKVERSION();
                ImGui::CreateContext();
                ImGuiIO& io = ImGui::GetIO();
                io.ConfigFlags = ImGuiConfigFlags_None; // Explicitly disable ImGui keyboard/gamepad navigation hijacking
                ImGui::StyleColorsDark();

                // Customize ImGui Theme to Sleek Dark / Crimson Style
                ImGuiStyle& style = ImGui::GetStyle();
                style.WindowRounding = 6.0f;
                style.FrameRounding = 4.0f;
                style.PopupRounding = 4.0f;
                style.ScrollbarRounding = 4.0f;
                style.GrabRounding = 4.0f;
                style.WindowBorderSize = 1.0f;
                style.FrameBorderSize = 0.0f;
                style.Colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.08f, 0.10f, 0.94f);
                style.Colors[ImGuiCol_Header] = ImVec4(0.55f, 0.12f, 0.15f, 0.80f);
                style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.70f, 0.18f, 0.20f, 0.90f);
                style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.85f, 0.20f, 0.22f, 1.00f);
                style.Colors[ImGuiCol_Button] = ImVec4(0.45f, 0.12f, 0.15f, 0.70f);
                style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.65f, 0.16f, 0.20f, 0.90f);
                style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.80f, 0.20f, 0.25f, 1.00f);
                style.Colors[ImGuiCol_FrameBg] = ImVec4(0.14f, 0.14f, 0.18f, 0.80f);
                style.Colors[ImGuiCol_TitleBg] = ImVec4(0.35f, 0.08f, 0.10f, 1.00f);
                style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.55f, 0.12f, 0.15f, 1.00f);

                // Configure fonts: default font first for general menus
                io.Fonts->AddFontDefault();

                // Load crisp bold font for large fighting game popups and meter percentage badges
                if (GetFileAttributesA("C:\\Windows\\Fonts\\segoeuib.ttf") != INVALID_FILE_ATTRIBUTES) {
                    g_PopupFont = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeuib.ttf", 26.0f);
                    g_MeterFont = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeuib.ttf", 20.0f);
                } else if (GetFileAttributesA("C:\\Windows\\Fonts\\arialbd.ttf") != INVALID_FILE_ATTRIBUTES) {
                    g_PopupFont = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\arialbd.ttf", 26.0f);
                    g_MeterFont = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\arialbd.ttf", 20.0f);
                }

                ImGui_ImplWin32_Init(hWnd);
                ImGui_ImplDX9_Init(pDevice);
                g_ImGuiInitialized = true;
            }
        }

        if (g_ImGuiInitialized) {
            ScopedProfiler prof(PROF_IMGUI_RENDER);
            ImGuiIO& io = ImGui::GetIO();
            
            bool isHoldingRMB = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
            bool hideCursorForFreeCam = MK9TradeFix::FreeCamera::IsEnabled() && 
                                        (MK9TradeFix::FreeCamera::g_bAlwaysLockMouse || isHoldingRMB);

            io.MouseDrawCursor = !hideCursorForFreeCam && 
                                 (g_ConsoleOpen || 
                                  MK9TradeFix::InputRecordingManager::Get().IsHUDActive() ||
                                  MK9TradeFix::ReversalEngine::Get().IsIdleStanceHUDActive());

            ImGui_ImplDX9_NewFrame();
            ImGui_ImplWin32_NewFrame();
            if (io.DeltaTime <= 0.0f) {
                io.DeltaTime = 1.0f / 60.0f;
            }
            ImGui::NewFrame();

            MK9TradeFix::FreeCamera::UpdateInput(io.DeltaTime);

            // 1. In-Game Background DrawList HUDs (Meter Percent & Hit Indication) - strictly during active match
            if (MK9TradeFix::IsMatchActive()) {
                if (MK9TradeFix::MeterDrainFix::g_ShowMeterPercentHUD ||
                    MK9TradeFix::MoveTelemetryHUD::g_ShowMoveHUD) {
                    RenderSuperMeterPercentHUD();
                }

                if (MK9TradeFix::g_FixEnabled && MK9TradeFix::HitIndicationHUD::g_ShowHitIndicationHUD) {
                    RenderHitIndicationHUD();
                }

                // 2. In-Game Frame Data Visualizer Window (Rendered before Main Console so Console has top Z-order)
                if (MK9TradeFix::g_FrameDataVisualizerEnabled) {
                    RenderFrameDataVisualizer();
                }
            }

            // 3. Training Helper Floating HUD Windows
            if (MK9TradeFix::InputRecordingManager::Get().IsHUDActive()) {
                RenderRecordingHUD();
            }

            if (MK9TradeFix::ReversalEngine::Get().IsIdleStanceHUDActive()) {
                RenderDummyIdleStanceHUD();
            }

            // 4. Main Live Console (Rendered last so it always remains on top of all in-game HUDs)
            if (g_ConsoleOpen) {
                RenderConsoleUI();
            }

            ImGui::EndFrame();
            ImGui::Render();
            ImDrawData* drawData = ImGui::GetDrawData();
            if (drawData && drawData->TotalVtxCount > 0) {
                g_IsInsideImGuiRender = true;
                ImGui_ImplDX9_RenderDrawData(drawData);
                g_IsInsideImGuiRender = false;
            }
        }

        // Track active match frames when Void Mode or Floor Grid is enabled to allow UE3 to bake character lighting probes
        if (MK9TradeFix::IsMatchLoadedUp()) {
            if (MK9TradeFix::ExtraGameplay::g_DisableBackground || MK9TradeFix::ExtraGameplay::g_EnableFloorGrid) {
                // Keep counter at 0 while Soul Steal is actively playing, so warmup delay starts fresh when move completes
                if (MK9TradeFix::ExtraGameplay::IsSoulStealOngoing()) {
                    s_MatchActiveFrames = 0;
                } else if (!MK9TradeFix::IsGameOrSimulationPaused()) {
                    if (s_MatchActiveFrames < 2000) s_MatchActiveFrames++;
                }
            } else {
                s_MatchActiveFrames = 0;
            }
        } else {
            // Outside of match, during intro cinematics, or after match ends:
            // Effect remains disabled internally. The user's ImGui checkboxes remain persistent.
            s_MatchActiveFrames = 0;
            if (s_DeclLockInit) {
                EnterCriticalSection(&s_DeclLock);
                s_NumCachedDecls = 0;
                LeaveCriticalSection(&s_DeclLock);
            }
        }

        return oEndScene(pDevice);
    }

    static HRESULT WINAPI hkReset(IDirect3DDevice9* pDevice, D3DPRESENT_PARAMETERS* pPresentationParameters) {
        if (!pDevice || !pPresentationParameters) return D3DERR_INVALIDCALL;

        if (g_ImGuiInitialized) {
            ImGui_ImplDX9_InvalidateDeviceObjects();
        }

        s_MatchActiveFrames = 0;

        if (s_DeclLockInit) {
            EnterCriticalSection(&s_DeclLock);
            s_NumCachedDecls = 0;
            LeaveCriticalSection(&s_DeclLock);
        }

        HRESULT hr = D3DERR_INVALIDCALL;
        __try {
            hr = oReset(pDevice, pPresentationParameters);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            // Guard against crash in nvd3dum.dll on display mode transitions
        }

        if (SUCCEEDED(hr) && g_ImGuiInitialized) {
            ImGui_ImplDX9_CreateDeviceObjects();
        }

        return hr;
    }

    static HRESULT WINAPI hkDrawIndexedPrimitive(IDirect3DDevice9* pDevice, D3DPRIMITIVETYPE Type, INT BaseVertexIndex, UINT MinVertexIndex, UINT NumVertices, UINT StartIndex, UINT PrimCount) {
        // Fast path: if Void Mode (Disable Background) is disabled or match is not loaded up, immediately bypass with zero overhead
        int warmupThreshold = MK9TradeFix::ExtraGameplay::g_LightingWarmupFrames;
        if (warmupThreshold < 1) warmupThreshold = 1;
        if (!MK9TradeFix::ExtraGameplay::g_DisableBackground || !MK9TradeFix::IsMatchLoadedUp() || s_MatchActiveFrames < warmupThreshold || MK9TradeFix::ExtraGameplay::IsSoulStealOngoing() || g_IsInsideImGuiRender || !pDevice) {
            return oDrawIndexedPrimitive(pDevice, Type, BaseVertexIndex, MinVertexIndex, NumVertices, StartIndex, PrimCount);
        }

        // Line rendering (floor grid, debug lines, collision visualizers) is always preserved
        if (Type == D3DPT_LINELIST) {
            return oDrawIndexedPrimitive(pDevice, Type, BaseVertexIndex, MinVertexIndex, NumVertices, StartIndex, PrimCount);
        }

        // Dynamic VFX preservation:
        // Alpha-blended & additive materials: All fireballs (Reptile Forceball, Liu Kang Fireball, Sub-Zero Ice Ball,
        // Ermac Forceball, etc.), energy blasts, magic VFX, slime trails, and particle meshes.
        // In Unreal Engine 3, true translucent/additive materials blend into the destination buffer (destBlend > D3DBLEND_ZERO).
        // Opaque stage environment geometry uses destBlend == D3DBLEND_ZERO (or blending disabled), overwriting the background.
        DWORD alphaBlend = 0;
        DWORD destBlend = 0;
        if (SUCCEEDED(pDevice->GetRenderState(D3DRS_ALPHABLENDENABLE, &alphaBlend)) && alphaBlend) {
            if (SUCCEEDED(pDevice->GetRenderState(D3DRS_DESTBLEND, &destBlend)) && destBlend > D3DBLEND_ZERO) {
                return oDrawIndexedPrimitive(pDevice, Type, BaseVertexIndex, MinVertexIndex, NumVertices, StartIndex, PrimCount);
            }
        }

        IDirect3DVertexDeclaration9* pDecl = nullptr;
        if (SUCCEEDED(pDevice->GetVertexDeclaration(&pDecl)) && pDecl) {
            if (!s_DeclLockInit) {
                InitializeCriticalSection(&s_DeclLock);
                s_DeclLockInit = true;
            }

            bool isStage = false;
            bool found = false;

            EnterCriticalSection(&s_DeclLock);
            for (int i = 0; i < s_NumCachedDecls; ++i) {
                if (s_CachedDecls[i].pDecl == pDecl) {
                    isStage = s_CachedDecls[i].isStage;
                    found = true;
                    break;
                }
            }

            if (!found) {
                D3DVERTEXELEMENT9 declElements[64];
                UINT numElem = 0;
                bool hasSkinning = false;
                bool is2D = false;
                if (SUCCEEDED(pDecl->GetDeclaration(declElements, &numElem))) {
                    for (UINT i = 0; i < numElem && declElements[i].Type != D3DDECLTYPE_UNUSED; ++i) {
                        if (declElements[i].Usage == D3DDECLUSAGE_BLENDWEIGHT || declElements[i].Usage == D3DDECLUSAGE_BLENDINDICES) {
                            hasSkinning = true;
                            break;
                        }
                        if (declElements[i].Usage == D3DDECLUSAGE_POSITIONT) {
                            is2D = true;
                            break;
                        }
                    }
                }
                // Suppress if it has no bone skinning and is not a 2D UI element
                isStage = (!hasSkinning && !is2D);
                if (s_NumCachedDecls < 32) {
                    s_CachedDecls[s_NumCachedDecls].pDecl = pDecl;
                    s_CachedDecls[s_NumCachedDecls].isStage = isStage;
                    s_NumCachedDecls++;
                }
            }
            LeaveCriticalSection(&s_DeclLock);

            // Release after using declaration
            pDecl->Release();

            // Suppress all static/environment stage geometry
            if (isStage) {
                return D3D_OK;
            }
        }
        return oDrawIndexedPrimitive(pDevice, Type, BaseVertexIndex, MinVertexIndex, NumVertices, StartIndex, PrimCount);
    }

    static HRESULT WINAPI hkClear(IDirect3DDevice9* pDevice, DWORD Count, const D3DRECT* pRects, DWORD Flags, D3DCOLOR Color, float Z, DWORD Stencil) {
        int warmupThreshold = MK9TradeFix::ExtraGameplay::g_LightingWarmupFrames;
        if (warmupThreshold < 1) warmupThreshold = 1;
        if (MK9TradeFix::ExtraGameplay::g_DisableBackground && MK9TradeFix::IsMatchLoadedUp() && s_MatchActiveFrames >= warmupThreshold && !MK9TradeFix::ExtraGameplay::IsSoulStealOngoing() && (Flags & D3DCLEAR_TARGET) && pDevice) {
            IDirect3DSurface9* pRenderTarget = nullptr;
            if (SUCCEEDED(pDevice->GetRenderTarget(0, &pRenderTarget)) && pRenderTarget) {
                D3DSURFACE_DESC rtDesc;
                bool isMainTarget = true;
                if (SUCCEEDED(pRenderTarget->GetDesc(&rtDesc))) {
                    // Filter out tiny shadow maps, post-process downsamples, and bloom buffers (< 400x300)
                    isMainTarget = (rtDesc.Width >= 400 && rtDesc.Height >= 300);
                }
                if (isMainTarget) {
                    int r = (int)(MK9TradeFix::ExtraGameplay::g_VoidBgColor[0] * 255.0f);
                    int g = (int)(MK9TradeFix::ExtraGameplay::g_VoidBgColor[1] * 255.0f);
                    int b = (int)(MK9TradeFix::ExtraGameplay::g_VoidBgColor[2] * 255.0f);
                    r = (r < 0) ? 0 : (r > 255 ? 255 : r);
                    g = (g < 0) ? 0 : (g > 255 ? 255 : g);
                    b = (b < 0) ? 0 : (b > 255 ? 255 : b);
                    Color = D3DCOLOR_XRGB(r, g, b);
                }
                pRenderTarget->Release();
            }
        }
        return oClear(pDevice, Count, pRects, Flags, Color, Z, Stencil);
    }

    void CheckAndHookD3D9() {
        if (oEndScene != nullptr) return; // Already hooked

        IDirect3DDevice9* pDevice = *(IDirect3DDevice9**)0x00E84534;
        if (!pDevice) return;

        void** pVTable = *(void***)pDevice;
        if (!pVTable) return;

        // Hook EndScene (Index 42)
        void* pEndScene = pVTable[42];
        if (pEndScene && MH_CreateHook(pEndScene, (void*)hkEndScene, (void**)&oEndScene) == MH_OK) {
            MH_EnableHook(pEndScene);
            MK9TradeFix::LoadProfiler::RecordD3D9Init();
        }

        // Hook Clear (Index 43)
        void* pClear = pVTable[43];
        if (pClear && MH_CreateHook(pClear, (void*)hkClear, (void**)&oClear) == MH_OK) {
            MH_EnableHook(pClear);
        }

        // Hook Reset (Index 16)
        void* pReset = pVTable[16];
        if (pReset && MH_CreateHook(pReset, (void*)hkReset, (void**)&oReset) == MH_OK) {
            MH_EnableHook(pReset);
        }

        // Hook DrawIndexedPrimitive (Index 82)
        void* pDIP = pVTable[82];
        if (pDIP && MH_CreateHook(pDIP, (void*)hkDrawIndexedPrimitive, (void**)&oDrawIndexedPrimitive) == MH_OK) {
            MH_EnableHook(pDIP);
        }
    }

    void Initialize() {
        // Ready for D3D9 hook
    }

    void Shutdown() {
        if (g_ImGuiInitialized) {
            ImGui_ImplDX9_Shutdown();
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
            g_ImGuiInitialized = false;
        }

        if (g_hTargetWindow && oWndProc) {
            SetWindowLongPtrA(g_hTargetWindow, GWLP_WNDPROC, (LONG_PTR)oWndProc);
            oWndProc = NULL;
        }
    }

} // namespace MK9Overlay

namespace MK9TradeFix {
    void TriggerLightingProbeWarmup() {
        MK9Overlay::s_MatchActiveFrames = 0;
    }
}


