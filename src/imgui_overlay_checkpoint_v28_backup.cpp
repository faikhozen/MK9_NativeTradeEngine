#include "imgui_overlay.hpp"
#include "trade_fix.hpp"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_dx9.h"
#include "imgui/imgui_impl_win32.h"
#include "minhook/include/MinHook.h"
#include <cstdio>
#include <deque>
#include <mutex>
#include <algorithm>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Forward declarations from trade_fix
namespace MK9TradeFix {
    extern bool IsFixEnabled();
    extern void ToggleFix();
    extern int GetDiagnosticMode();
    extern void SetDiagnosticMode(int mode);
    extern const char* GetDisplayModeName();
    extern void CycleDisplayMode();
    extern uint64_t GetCurrentSimulationFrame();
    extern bool IsCurrentMatchTag();
    extern void TriggerMacroBot(WORD p1Dir, WORD p1Key, int p1Delay, WORD p2Dir, WORD p2Key, int p2Delay, const char* desc);
    enum MacroSequenceType {
        MACRO_SEQ_STANDARD = 0,
        MACRO_SEQ_P1_WALK_THEN_ATTACK,
        MACRO_SEQ_P2_WALK_THEN_ATTACK,
        MACRO_SEQ_GENERIC_SPECIAL
    };
    extern void TriggerSequenceMacro(MacroSequenceType seq, const char* desc);
    extern void Trigger2StepMacro(WORD p1_d1, WORD p1_d2, WORD p1_b, WORD p2_d1, WORD p2_d2, WORD p2_b, const char* desc, int step1Hold = 2, int step1Delay = 2);
    extern void Trigger3StepMacro(WORD p1_d1, WORD p1_d2, WORD p1_d3, WORD p1_b, WORD p2_d1, WORD p2_d2, WORD p2_d3, WORD p2_b, const char* desc, int step1Hold = 2, int step1Delay = 2);
    extern void TriggerJumpInVsGround(bool isAntiAirD2, const char* desc);
    extern void TriggerP2SpecVsP1Norm(WORD p2_d1, WORD p2_d2, WORD p2_btn, WORD p1_dir, WORD p1_btn, int p1_delay, const char* desc);
    extern void TriggerP1SpecVsP2Norm(WORD p1_d1, WORD p1_d2, WORD p1_btn, WORD p2_dir, WORD p2_btn, int p2_delay, const char* desc);

    extern bool IsDrawDebugHitboxes();
    extern void SetDrawDebugHitboxes(bool enable);
    extern bool IsDrawDebugHurtboxes();
    extern void SetDrawDebugHurtboxes(bool enable);
    extern bool IsDrawDebugPushboxes();
    extern void SetDrawDebugPushboxes(bool enable);
    extern void* GetP1Character();
    extern void* GetP2Character();
    extern void RenderNativeCollisionPrimitives();
    extern bool IsGamePaused();
    extern void ToggleGamePause();
    extern void StepOneFrame();
    extern CollisionCalib& GetCalibration();
    extern void ResetCalibration();
}

namespace MK9Overlay {

    static bool g_ConsoleOpen = true; // Open by default on startup
    static bool g_ImGuiInitialized = false;
    static HWND g_hTargetWindow = NULL;
    static WNDPROC oWndProc = NULL;

    typedef HRESULT (WINAPI *tEndScene)(IDirect3DDevice9* pDevice);
    static tEndScene oEndScene = nullptr;

    typedef HRESULT (WINAPI *tReset)(IDirect3DDevice9* pDevice, D3DPRESENT_PARAMETERS* pPresentationParameters);
    static tReset oReset = nullptr;

    static std::deque<ConsoleLogEntry> g_LogEntries;
    static std::mutex g_LogMutex;
    static const size_t MAX_LOG_LINES = 2500;
    static bool g_AutoScroll = true;
    static float g_ConsoleOpacity = 1.0f;
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
    }

    bool IsConsoleOpen() {
        return g_ConsoleOpen;
    }

    static LRESULT CALLBACK hkWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        // If ImGui is not yet initialized or has no valid context, pass directly to original WndProc
        if (!g_ImGuiInitialized || ImGui::GetCurrentContext() == nullptr) {
            if (oWndProc) {
                return CallWindowProcA(oWndProc, hWnd, uMsg, wParam, lParam);
            }
            return DefWindowProcA(hWnd, uMsg, wParam, lParam);
        }

        // Toggle ImGui Console overlay with ~ (Tilde / VK_OEM_3) or F12
        if (uMsg == WM_KEYDOWN) {
            if (wParam == VK_OEM_3 || wParam == VK_F12) {
                g_ConsoleOpen = !g_ConsoleOpen;
                return 1;
            }
        }

        if (g_ConsoleOpen) {
            if (ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam)) {
                return true;
            }

            // If mouse is interacting with ImGui window, block game input
            ImGuiIO& io = ImGui::GetIO();
            if (io.WantCaptureMouse && (uMsg >= WM_MOUSEFIRST && uMsg <= WM_MOUSELAST)) {
                return true;
            }
        }

        if (oWndProc) {
            return CallWindowProcA(oWndProc, hWnd, uMsg, wParam, lParam);
        }
        return DefWindowProcA(hWnd, uMsg, wParam, lParam);
    }

    static void RenderConsoleUI() {
        if (!g_ConsoleOpen) return;

        ImGui::SetNextWindowSize(ImVec2(780, 480), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowBgAlpha(g_ConsoleOpacity);

        ImGuiWindowFlags windowFlags = ImGuiWindowFlags_MenuBar;

        if (ImGui::Begin("MK9 Native Physical Trade Engine v0.77 - Live In-Game Console", &g_ConsoleOpen, windowFlags)) {
            // Top Menu Bar
            if (ImGui::BeginMenuBar()) {
                if (ImGui::BeginMenu("View")) {
                    ImGui::MenuItem("Auto-Scroll", nullptr, &g_AutoScroll);
                    ImGui::SliderFloat("Opacity", &g_ConsoleOpacity, 0.20f, 1.0f, "%.2f");
                    if (ImGui::MenuItem("Clear Log")) {
                        std::lock_guard<std::mutex> lock(g_LogMutex);
                        g_LogEntries.clear();
                    }
                    if (ImGui::MenuItem("Close Console (~ / F12)")) {
                        g_ConsoleOpen = false;
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Display Mode")) {
                    if (ImGui::MenuItem("Cycle Mode (F10)")) {
                        MK9TradeFix::CycleDisplayMode();
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndMenuBar();
            }

            // Status Banner
            bool fixEnabled = MK9TradeFix::IsFixEnabled();
            ImGui::Text("Trade Fix:");
            ImGui::SameLine();
            if (fixEnabled) {
                ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.3f, 1.0f), "[ENABLED]");
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "[DISABLED]");
            }

            ImGui::SameLine();
            if (ImGui::SmallButton(fixEnabled ? "Disable (F11)" : "Enable (F11)")) {
                MK9TradeFix::ToggleFix();
            }

            ImGui::SameLine();
            ImGui::Text("| Display: %s", MK9TradeFix::GetDisplayModeName());

            ImGui::SameLine();
            if (ImGui::SmallButton("Cycle (F10)")) {
                MK9TradeFix::CycleDisplayMode();
            }

            ImGui::SameLine();
            ImGui::Text("| Frame: %llu", MK9TradeFix::GetCurrentSimulationFrame());

            ImGui::SameLine();
            ImGui::Text("| Mode: %d", MK9TradeFix::GetDiagnosticMode());

            bool isPaused = MK9TradeFix::IsGamePaused();
            ImGui::SameLine();
            ImGui::Text("| Sim:");
            ImGui::SameLine();
            if (isPaused) {
                ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f), "[PAUSED]");
            } else {
                ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.3f, 1.0f), "[RUNNING]");
            }

            ImGui::SameLine();
            if (ImGui::SmallButton(isPaused ? "Resume (\\)" : "Pause (\\)")) {
                MK9TradeFix::ToggleGamePause();
            }

            if (isPaused) {
                ImGui::SameLine();
                if (ImGui::SmallButton("Step 1 Frame (=)")) {
                    MK9TradeFix::StepOneFrame();
                }
            }

            ImGui::Separator();

            // UE3 Native 3D Collision Visualizer Subsystem
            if (ImGui::CollapsingHeader("UE3 Native 3D Collision Visualizer", ImGuiTreeNodeFlags_DefaultOpen)) {
                bool drawHit = MK9TradeFix::IsDrawDebugHitboxes();
                if (ImGui::Checkbox("Draw Attack Hitboxes (3D Red Boxes)", &drawHit)) {
                    MK9TradeFix::SetDrawDebugHitboxes(drawHit);
                }
                ImGui::SameLine();
                bool drawHurt = MK9TradeFix::IsDrawDebugHurtboxes();
                if (ImGui::Checkbox("Draw Body Hurtboxes (3D Green Boxes)", &drawHurt)) {
                    MK9TradeFix::SetDrawDebugHurtboxes(drawHurt);
                }
                ImGui::SameLine();
                bool drawPush = MK9TradeFix::IsDrawDebugPushboxes();
                if (ImGui::Checkbox("Draw Pushbox Volumes (3D Cyan Boxes)", &drawPush)) {
                    MK9TradeFix::SetDrawDebugPushboxes(drawPush);
                }

                ImGui::TextDisabled("Renders authentic native 3D collision wireframes directly inside Unreal Engine 3's viewport renderer.");

                // Live Calibration Sub-Panel
                if (ImGui::TreeNodeEx("Live Calibration", ImGuiTreeNodeFlags_DefaultOpen)) {
                    MK9TradeFix::CollisionCalib& calib = MK9TradeFix::GetCalibration();

                    if (ImGui::Button("Reset All to Defaults")) {
                        MK9TradeFix::ResetCalibration();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Copy Current Values to Clipboard")) {
                        char buf[512];
                        snprintf(buf, sizeof(buf),
                            "// CollisionCalib Settings\n"
                            "pushRadiusScale = %.2ff;\n"
                            "pushHeightScale = %.2ff;\n"
                            "pushYOffset     = %.2ff;\n"
                            "pushZOffset     = %.2ff;\n"
                            "hurtSizeScale   = %.2ff;\n"
                            "hurtForwardBias = %.2ff;\n"
                            "hurtHeightBias  = %.2ff;\n"
                            "limbPadding     = %.2ff;\n"
                            "limbForward     = %.2ff;\n"
                            "limbScaleX      = %.2ff;\n"
                            "limbScaleY      = %.2ff;\n"
                            "limbScaleZ      = %.2ff;\n"
                            "propLenScale    = %.2ff;\n"
                            "propRadScale    = %.2ff;\n"
                            "propCenterBias  = %.2ff;\n",
                            calib.pushRadiusScale, calib.pushHeightScale, calib.pushYOffset, calib.pushZOffset,
                            calib.hurtSizeScale, calib.hurtForwardBias, calib.hurtHeightBias,
                            calib.limbPadding, calib.limbForward, calib.limbScaleX, calib.limbScaleY, calib.limbScaleZ,
                            calib.propLenScale, calib.propRadScale, calib.propCenterBias
                        );
                        ImGui::SetClipboardText(buf);
                    }

                    if (ImGui::TreeNodeEx("Pushbox", ImGuiTreeNodeFlags_DefaultOpen)) {
                        ImGui::DragFloat("Radius Scale##PB_Rad", &calib.pushRadiusScale, 0.01f, 0.50f, 2.00f, "%.2f");
                        ImGui::DragFloat("Height Scale##PB_Height", &calib.pushHeightScale, 0.01f, 0.50f, 2.00f, "%.2f");
                        ImGui::DragFloat("Y Offset##PB_Y", &calib.pushYOffset, 0.5f, -50.0f, 50.0f, "%.1f");
                        ImGui::DragFloat("Z Offset##PB_Z", &calib.pushZOffset, 0.5f, -50.0f, 50.0f, "%.1f");
                        ImGui::TreePop();
                    }

                    if (ImGui::TreeNodeEx("Hurtbox", ImGuiTreeNodeFlags_DefaultOpen)) {
                        ImGui::DragFloat("Size Scale##HB_Size", &calib.hurtSizeScale, 0.01f, 0.50f, 2.00f, "%.2f");
                        ImGui::DragFloat("Forward Bias##HB_Fwd", &calib.hurtForwardBias, 0.5f, -30.0f, 30.0f, "%.1f");
                        ImGui::DragFloat("Height Bias##HB_Height", &calib.hurtHeightBias, 0.5f, -30.0f, 30.0f, "%.1f");
                        ImGui::TreePop();
                    }

                    if (ImGui::TreeNodeEx("Limb Hitbox", ImGuiTreeNodeFlags_DefaultOpen)) {
                        ImGui::DragFloat("Padding##LB_Pad", &calib.limbPadding, 0.5f, 5.0f, 40.0f, "%.1f");
                        ImGui::DragFloat("Forward Offset##LB_Fwd", &calib.limbForward, 0.5f, 0.0f, 50.0f, "%.1f");
                        ImGui::DragFloat("Scale X##LB_ScaleX", &calib.limbScaleX, 0.01f, 0.50f, 2.00f, "%.2f");
                        ImGui::DragFloat("Scale Y##LB_ScaleY", &calib.limbScaleY, 0.01f, 0.50f, 2.00f, "%.2f");
                        ImGui::DragFloat("Scale Z##LB_ScaleZ", &calib.limbScaleZ, 0.01f, 0.50f, 2.00f, "%.2f");
                        ImGui::TreePop();
                    }

                    if (ImGui::TreeNodeEx("Prop / Weapon", ImGuiTreeNodeFlags_DefaultOpen)) {
                        ImGui::DragFloat("Length Scale##Prop_Len", &calib.propLenScale, 0.01f, 0.50f, 2.00f, "%.2f");
                        ImGui::DragFloat("Radius Scale##Prop_Rad", &calib.propRadScale, 0.01f, 0.50f, 2.00f, "%.2f");
                        ImGui::DragFloat("Center Bias##Prop_Center", &calib.propCenterBias, 0.5f, -30.0f, 30.0f, "%.1f");
                        ImGui::TreePop();
                    }

                    ImGui::TreePop();
                }
            }

            ImGui::Separator();
            if (ImGui::CollapsingHeader("Diagnostic Test Bot Triggers (Modes 1 - 7)")) {
                const WORD P1_1 = 'T', P1_2 = 'U', P1_3 = 'G', P1_4 = 'J';
                const WORD P2_1 = VK_NUMPAD7, P2_2 = VK_NUMPAD9, P2_3 = VK_NUMPAD4, P2_4 = VK_NUMPAD6;
                const WORD P1_U = 'W', P1_D = 'S', P1_B = 'A', P1_F = 'D';
                const WORD P2_U = VK_UP, P2_D = VK_DOWN, P2_B = VK_RIGHT, P2_F = VK_LEFT;

                int curDiagMode = MK9TradeFix::GetDiagnosticMode();
                ImGui::Text("Active Diagnostic Mode (ImGui Only):");
                for (int m = 0; m <= 8; m++) {
                    ImGui::SameLine();
                    char mLabel[32];
                    if (m == 0) snprintf(mLabel, sizeof(mLabel), "Off##m%d", m);
                    else snprintf(mLabel, sizeof(mLabel), "Mode %d##m%d", m, m);
                    bool isSelected = (curDiagMode == m);
                    if (isSelected) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
                    if (ImGui::Button(mLabel)) {
                        MK9TradeFix::SetDiagnosticMode(m);
                    }
                    if (isSelected) ImGui::PopStyleColor();
                }

                if (ImGui::BeginTabBar("DiagnosticModesTabs")) {
                    if (ImGui::BeginTabItem("Mode 1: Normal Timing")) {
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
                        ImGui::EndTabItem();
                    }

                    if (ImGui::BeginTabItem("Mode 2: Weapons")) {
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
                        ImGui::EndTabItem();
                    }

                    if (ImGui::BeginTabItem("Mode 3: Projectiles 1")) {
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
                        ImGui::EndTabItem();
                    }

                    if (ImGui::BeginTabItem("Mode 4: Projectiles 2")) {
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
                        ImGui::EndTabItem();
                    }

                    if (ImGui::BeginTabItem("Mode 5: Projectiles 3")) {
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
                        ImGui::EndTabItem();
                    }

                    if (ImGui::BeginTabItem("Mode 6: Projectiles 4")) {
                        if (ImGui::Button("U, D, 4")) {
                            MK9TradeFix::Trigger2StepMacro(P1_U, P1_D, P1_4, P2_U, P2_D, P2_4, "Both do U, D, 4", 7, 3);
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("U, D, F, 1")) {
                            MK9TradeFix::Trigger3StepMacro(P1_U, P1_D, P1_F, P1_1, P2_U, P2_D, P2_F, P2_1, "Both do U, D, F, 1", 7, 3);
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("U, D, B, 1")) {
                            MK9TradeFix::Trigger3StepMacro(P1_U, P1_D, P1_B, P1_1, P2_U, P2_D, P2_B, P2_1, "Both do U, D, B, 1", 7, 3);
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("U, B, F, 1")) {
                            MK9TradeFix::Trigger3StepMacro(P1_U, P1_B, P1_F, P1_1, P2_U, P2_B, P2_F, P2_1, "Both do U, B, F, 1", 7, 3);
                        }

                        if (ImGui::Button("P1 bb1 vs P2 df3")) {
                            MK9TradeFix::Trigger2StepMacro(P1_B, P1_B, P1_1, P2_D, P2_F, P2_3, "P1 b, b, 1 (A, A+T) vs P2 d, f, 3 (Down, Left+Num4)");
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("P1 df3 vs P2 df2")) {
                            MK9TradeFix::Trigger2StepMacro(P1_D, P1_F, P1_3, P2_D, P2_F, P2_2, "P1 d, f, 3 (S, D+G) vs P2 d, f, 2 (Down, Left+Num9)");
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("P1 bf3 vs P2 bf1")) {
                            MK9TradeFix::Trigger2StepMacro(P1_B, P1_F, P1_3, P2_B, P2_F, P2_1, "P1 b, f, 3 (A, D+G) vs P2 b, f, 1 (Right, Left+Num7)");
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("P1 bf3 vs P2 bb1")) {
                            MK9TradeFix::Trigger2StepMacro(P1_B, P1_F, P1_3, P2_B, P2_B, P2_1, "P1 b, f, 3 (A, D+G) vs P2 b, b, 1 (Right, Right+Num7)");
                        }
                        ImGui::EndTabItem();
                    }

                    if (ImGui::BeginTabItem("Mode 7: Scorpion vs Sub-Zero")) {
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
                        ImGui::EndTabItem();
                    }

                    if (ImGui::BeginTabItem("Mode 8: State & Frame Dumper")) {
                        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f), "Raw Attack State Profiler (Saves to MK9_ATTACK_LIFECYCLE_DUMP.LOG):");
                        if (ImGui::Button("F1: P1 Standing 1 (FP)")) {
                            MK9TradeFix::TriggerMacroBot(0, 'T', 0, 0, 0, 0, "P1 Standing 1 (FP: T) - Frame Data Profiler");
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("F2: P1 Standing 2 (BP)")) {
                            MK9TradeFix::TriggerMacroBot(0, 'U', 0, 0, 0, 0, "P1 Standing 2 (BP: U) - Frame Data Profiler");
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("F3: P1 Crouching 1 (D+1)")) {
                            MK9TradeFix::TriggerMacroBot('S', 'T', 0, 0, 0, 0, "P1 Crouching 1 (D+1: S+T) - Frame Data Profiler");
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("F4: P1 Uppercut (D+2)")) {
                            MK9TradeFix::TriggerMacroBot('S', 'U', 0, 0, 0, 0, "P1 Uppercut (D+2: S+U) - Frame Data Profiler");
                        }

                        if (ImGui::Button("F5: P1 Back Weapon (b+1)")) {
                            MK9TradeFix::TriggerMacroBot('A', 'T', 0, 0, 0, 0, "P1 Back Normal/Weapon (b+1: A+T) - Frame Data Profiler");
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("F6: P1 Fwd Heavy (f+2)")) {
                            MK9TradeFix::TriggerMacroBot('D', 'U', 0, 0, 0, 0, "P1 Forward Heavy (f+2: D+U) - Frame Data Profiler");
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("F7: P1 Special 1 (D, B, 1)")) {
                            MK9TradeFix::Trigger2StepMacro('S', 'A', 'T', 0, 0, 0, "P1 Special Move 1 (D, B, 1) - Frame Data Profiler");
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("F8: P1 Special 2 (D, F, 1)")) {
                            MK9TradeFix::Trigger2StepMacro('S', 'D', 'T', 0, 0, 0, "P1 Special Move 2 (D, F, 1) - Frame Data Profiler");
                        }
                        ImGui::TextDisabled("* Real-time state transitions and exact frame duration are dumped on every attack execution.");
                        ImGui::EndTabItem();
                    }

                    ImGui::EndTabBar();
                }
                ImGui::Separator();
            }

            // Filter Bar
            g_LogFilter.Draw("Filter Logs (Search)", 250.0f);
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

            ImGui::Separator();

            // Scrolling Log Content Area
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
            ImGui::TextDisabled("Hotkey: Press [~ / Tilde] or [F12] to toggle this in-game console.");
        }
        ImGui::End();
    }

    static HRESULT WINAPI hkEndScene(IDirect3DDevice9* pDevice) {
        if (!g_ImGuiInitialized && pDevice) {
            HWND hWnd = *(HWND*)0x00E53BF4;
            if (hWnd && IsWindow(hWnd)) {
                IMGUI_CHECKVERSION();
                ImGui::CreateContext();
                ImGuiIO& io = ImGui::GetIO();
                io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
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

                ImGui_ImplWin32_Init(hWnd);
                ImGui_ImplDX9_Init(pDevice);
                g_ImGuiInitialized = true;

                g_hTargetWindow = hWnd;
                oWndProc = (WNDPROC)SetWindowLongPtrA(hWnd, GWLP_WNDPROC, (LONG_PTR)hkWndProc);
            }
        }

        if (g_ImGuiInitialized) {
            ImGui_ImplDX9_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();

            RenderConsoleUI();

            ImGui::EndFrame();
            ImGui::Render();
            ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
        }

        return oEndScene(pDevice);
    }

    static HRESULT WINAPI hkReset(IDirect3DDevice9* pDevice, D3DPRESENT_PARAMETERS* pPresentationParameters) {
        if (g_ImGuiInitialized) {
            ImGui_ImplDX9_InvalidateDeviceObjects();
        }

        HRESULT hr = oReset(pDevice, pPresentationParameters);

        if (SUCCEEDED(hr) && g_ImGuiInitialized) {
            ImGui_ImplDX9_CreateDeviceObjects();
        }

        return hr;
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
        }

        // Hook Reset (Index 16)
        void* pReset = pVTable[16];
        if (pReset && MH_CreateHook(pReset, (void*)hkReset, (void**)&oReset) == MH_OK) {
            MH_EnableHook(pReset);
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

}
