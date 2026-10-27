#include "simulation.hpp"
#include "collision_visualizer.hpp"
#include "combat_state.hpp"
#include "frame_data.hpp"
#include "trade_engine.hpp"
#include "display_window.hpp"
#include "negative_edge.hpp"
#include "crouch_buffer_fix.hpp"
#include "meter_drain_fix.hpp"
#include "input_recording.hpp"
#include "reversal_engine.hpp"
#include "input_isolation.hpp"
#include "extra_gameplay.hpp"
#include "juggle_deterioration.hpp"
#include "jump_input_fix.hpp"
#include "profiler.hpp"
#include "anim_viewer.hpp"
#include "stun_modifier.hpp"
#include "free_camera.hpp"
#include "attack_frame_modifier.hpp"
#include "minhook/include/MinHook.h"
#include <cmath>

namespace MK9TradeFix {

    void ToggleFix();

    std::atomic<bool> g_GamePaused{ false };
    std::atomic<bool> g_StepOneFrame{ false };

    void SetEnginePaused(bool pause) {
        __try {
            *(int*)0x00E43054 = pause ? 1 : 0;
            *(int*)0x00E43058 = pause ? 1 : 0;
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    void ToggleGamePause() {
        bool newState = !g_GamePaused.load();
        g_GamePaused.store(newState);
        SetEnginePaused(newState);
        LogTrace(">>> Game Simulation %s <<<", newState ? "PAUSED (\\ to Resume, = to Step 1 Frame)" : "RESUMED");
    }

    bool IsGamePaused() {
        return g_GamePaused.load();
    }

    void StepOneFrame() {
        if (!g_GamePaused.load()) {
            g_GamePaused.store(true);
        }
        g_StepOneFrame.store(true);
        *(int*)0x00E43054 = 0;
        *(int*)0x00E43058 = 0;
        LogTrace(">>> Stepped 1 Frame Forward (Current Frame: %llu) <<<", g_CurrentFrame);
    }

    MacroSequenceType g_SimSeqType = MACRO_SEQ_STANDARD;
    int               g_DiagnosticMode = 0;

    struct SpecialSequenceDef {
        int  stepCount;
        WORD p1_dirs[4];
        WORD p1_btn;
        WORD p2_dirs[4];
        WORD p2_btn;
        int  step1HoldFrames;
        int  step1NeutralDelay;
    };
    static SpecialSequenceDef g_SimSpecialSeq = { 0 };

    struct MatchupMacroDef {
        WORD spec_d1;
        WORD spec_d2;
        WORD spec_btn;
        WORD norm_dir;
        WORD norm_btn;
        int  norm_delay;
        bool isJumpIn;
        bool isAntiAirD2;
    };
    static MatchupMacroDef g_SimMatchup = { 0 };

    uint64_t           g_SimTriggerFrame = 0;
    static int         g_SimP1_DelayFrames = -1;
    static int         g_SimP2_DelayFrames = -1;
    static WORD        g_SimP1_Key = 'T';
    static WORD        g_SimP1_DirKey = 0;
    static WORD        g_SimP2_Key = VK_NUMPAD7;
    static WORD        g_SimP2_DirKey = 0;
    static const char* g_SimDesc = "";

    uint64_t    g_ActiveMacroTriggerFrame = 0;
    int         g_ActiveMacroP1Delay = 0;
    int         g_ActiveMacroP2Delay = 0;
    const char* g_ActiveMacroDesc = "";

    int GetDiagnosticMode() { return g_DiagnosticMode; }

    void SetDiagnosticMode(int mode) { 
        g_DiagnosticMode = mode; 
        PrintDiagnosticModeMenu();
    }

    void PrintDiagnosticModeMenu() {
        LogTrace("====================================================================");
        if (g_DiagnosticMode == 0) {
            LogTrace(" [DIAGNOSTIC MODE 0] Disabled Diagnostics");
            LogTrace("====================================================================");
            LogTrace("   [F1-F8] Inactive (Diagnostics Disabled)");
        } else {
            LogTrace(" Enabled Diagnostics (Keyboard Only)");
            switch (g_DiagnosticMode) {
                case 1:
                    LogTrace(" Mode 1: Normal Timing Assessment");
                    LogTrace("====================================================================");
                    LogTrace("   [F1]  Simultaneous FP   (P1: 0f,  P2: 0f)");
                    LogTrace("   [F2]  P1 Delayed FP     (+1 frame delay on P1)");
                    LogTrace("   [F3]  P2 Delayed FP     (+1 frame delay on P2)");
                    LogTrace("   [F4]  P1 Delayed FP     (+2 frame delay on P1)");
                    LogTrace("   [F5]  P2 Delayed FP     (+2 frame delay on P2)");
                    LogTrace("   [F6]  P1 FP vs P2 Left(15f) + Wait(1f) + Num7");
                    LogTrace("   [F7]  P2 FP vs P1 D(15f) + Wait(1f) + T");
                    LogTrace("   [F8]  Simultaneous D+FK (P1: S+G vs P2: Down+Num4)");
                    break;
                case 2:
                    LogTrace(" Mode 2: Weapon Check");
                    LogTrace("====================================================================");
                    LogTrace("   [F1]  Simultaneous b+FP (P1: A+T, P2: Right+Num7)");
                    LogTrace("   [F2]  Simultaneous b+BP (P1: A+U, P2: Right+Num9)");
                    LogTrace("   [F3]  P1 b+BP (A+U) vs P2 Delayed FP (+1f Num7)");
                    LogTrace("   [F4]  P1 b+FP (A+T) vs P2 Delayed FP (+1f Num7)");
                    LogTrace("   [F5]  P1 b+BP (A+U) vs P2 Delayed FP (+2f Num7)");
                    LogTrace("   [F6]  P1 b+FP (A+T) vs P2 Delayed FP (+2f Num7)");
                    LogTrace("   [F7]  P1 f+FP (D+T) vs P2 BP (Num9) Simultaneous");
                    LogTrace("   [F8]  P1 f+BP (D+U) vs P2 FP (Num7) Simultaneous");
                    break;
                case 3:
                    LogTrace(" Mode 3: Simultaneous Projectile Diagnostic Test 1");
                    LogTrace("====================================================================");
                    LogTrace("   [F1]  D, B, 1 (P1: S, A+T vs P2: Down, Right+Num7)");
                    LogTrace("   [F2]  D, B, 2 (P1: S, A+U vs P2: Down, Right+Num9)");
                    LogTrace("   [F3]  D, B, 3 (P1: S, A+G vs P2: Down, Right+Num4)");
                    LogTrace("   [F4]  D, B, 4 (P1: S, A+J vs P2: Down, Right+Num6)");
                    LogTrace("   [F5]  D, F, 1 (P1: S, D+T vs P2: Down, Left+Num7)");
                    LogTrace("   [F6]  D, F, 2 (P1: S, D+U vs P2: Down, Left+Num9)");
                    LogTrace("   [F7]  D, F, 3 (P1: S, D+G vs P2: Down, Left+Num4)");
                    LogTrace("   [F8]  D, U, 2 (P1: S, W+U vs P2: Down, Up+Num9)");
                    break;
                case 4:
                    LogTrace(" Mode 4: Simultaneous Projectile Diagnostic Test 2");
                    LogTrace("====================================================================");
                    LogTrace("   [F1]  B, F, 1 (P1: A, D+T vs P2: Right, Left+Num7)");
                    LogTrace("   [F2]  B, F, 2 (P1: A, D+U vs P2: Right, Left+Num9)");
                    LogTrace("   [F3]  B, F, 3 (P1: A, D+G vs P2: Right, Left+Num4)");
                    LogTrace("   [F4]  B, F, 4 (P1: A, D+J vs P2: Right, Left+Num6)");
                    LogTrace("   [F5]  B, B, 1 (P1: A, A+T vs P2: Right, Right+Num7)");
                    LogTrace("   [F6]  B, B, 2 (P1: A, A+U vs P2: Right, Right+Num9)");
                    LogTrace("   [F7]  B, B, 3 (P1: A, A+G vs P2: Right, Right+Num4)");
                    LogTrace("   [F8]  B, B, 4 (P1: A, A+J vs P2: Right, Right+Num6)");
                    break;
                case 5:
                    LogTrace(" Mode 5: Simultaneous Projectile Diagnostic Test 3");
                    LogTrace("====================================================================");
                    LogTrace("   [F1]  F, F, 3    (P1: D, D+G vs P2: Left, Left+Num4)");
                    LogTrace("   [F2]  F, F, 4    (P1: D, D+J vs P2: Left, Left+Num6)");
                    LogTrace("   [F3]  D, F, B, 2 (P1: S, D, A+U vs P2: Down, Left, Right+Num9)");
                    LogTrace("   [F4]  D, B, F, 1 (P1: S, A, D+T vs P2: Down, Right, Left+Num7)");
                    LogTrace("   [F5]  D, B, F, 2 (P1: S, A, D+U vs P2: Down, Right, Left+Num9)");
                    LogTrace("   [F6]  D, B, F, 3 (P1: S, A, D+G vs P2: Down, Right, Left+Num4)");
                    LogTrace("   [F7]  B, B, F, 1 (P1: A, A, D+T vs P2: Right, Right, Left+Num7)");
                    LogTrace("   [F8]  B, B, F, 4 (P1: A, A, D+J vs P2: Right, Right, Left+Num6)");
                    break;
                case 6:
                    LogTrace(" Mode 6: Simultaneous Projectile Diagnostic Test 4 (U held 7f + 3f delay)");
                    LogTrace("====================================================================");
                    LogTrace("   [F1]  U, D, 4    (P1: W(7f)+Wait(3f)+S+J vs P2: Up(7f)+Wait(3f)+Down+Num6)");
                    LogTrace("   [F2]  U, D, F, 1 (P1: W(7f)+Wait(3f)+S, D+T vs P2: Up(7f)+Wait(3f)+Down, Left+Num7)");
                    LogTrace("   [F3]  U, D, B, 1 (P1: W(7f)+Wait(3f)+S, A+T vs P2: Up(7f)+Wait(3f)+Down, Right+Num7)");
                    LogTrace("   [F4]  U, B, F, 1 (P1: W(7f)+Wait(3f)+A, D+T vs P2: Up(7f)+Wait(3f)+Right, Left+Num7)");
                    LogTrace("   [F5]  P1 b, b, 1 vs P2 d, f, 3 (P1: A, A+T vs P2: Down, Left+Num4)");
                    LogTrace("   [F6]  P1 d, f, 3 vs P2 d, f, 2 (P1: S, D+G vs P2: Down, Left+Num9)");
                    LogTrace("   [F7]  P1 b, f, 3 vs P2 b, f, 1 (P1: A, D+G vs P2: Right, Left+Num7)");
                    LogTrace("   [F8]  P1 b, f, 3 vs P2 b, b, 1 (P1: A, D+G vs P2: Right, Right+Num7)");
                    break;
                case 7:
                    LogTrace(" Mode 7: Matchup Priority Diagnostic (P1 Scorpion vs P2 Sub-Zero)");
                    LogTrace("====================================================================");
                    LogTrace("   [F1]  Sub-Zero D,F,3 vs P1 D+1 on STARTUP -> P1 D+1 WINS (Interrupt)");
                    LogTrace("   [F2]  Sub-Zero D,F,3 vs P1 D+1 on ACTIVE  -> Sub-Zero D,F,3 WINS (Special)");
                    LogTrace("   [F3]  Scorpion D,B,4 vs P2 D+1 on STARTUP -> P2 D+1 WINS (Interrupt)");
                    LogTrace("   [F4]  Scorpion D,B,4 vs P2 D+1 on ACTIVE  -> Scorpion D,B,4 WINS (Special)");
                    LogTrace("   [F5]  Special Move vs D+2 Uppercut        -> D+2 Uppercut WINS");
                    LogTrace("   [F6]  Jump-In vs D+2 Uppercut (Anti-Air)  -> D+2 Uppercut WINS");
                    LogTrace("   [F7]  Jump-In vs Standing Normal 1        -> Jump-In WINS");
                    LogTrace("   [F8]  Uppercut vs Uppercut (D+2 vs D+2)   -> Later Inputted Uppercut WINS");
                    break;
                case 8:
                    LogTrace(" Mode 8: Release Check & Input Buffer Diagnostics");
                    LogTrace("====================================================================");
                    LogTrace("   [F1]  P1 Raw Neutral Neg Edge    (Hold 1 -> B, B -> Release 1 [Spear])");
                    LogTrace("   [F2]  P1 Normal Special Baseline (B, B -> Press 1 Downstroke)");
                    LogTrace("   [F3]  P1 Hit-Confirm Jab Cancel  (Point-blank Jab -> Cancel on Hit)");
                    LogTrace("   [F4]  P1 Whiff Jab Neg Edge      (Whiffed Jab -> Verify Whiff Blocked)");
                    LogTrace("   [F5]  P2 Raw Neutral Neg Edge    (P2 Mirrored Hold 1 -> B, B -> Release 1)");
                    LogTrace("   [F6]  P2 Normal Special Baseline (P2 Mirrored B, B -> Press 1)");
                    LogTrace("   [F7]  P2 Hit-Confirm Jab Cancel  (P2 Mirrored Point-blank Jab -> Cancel)");
                    LogTrace("   [F8]  P1 String Buffer Check     (1, 1 -> Post-recovery B, B Backdash)");
                    break;
                case 9:
                    LogTrace(" Mode 9: Live Frame Data & Projectile Analysis");
                    LogTrace("====================================================================");
                    LogTrace("   [F1]  P1 Front Punch Baseline    (11f Startup, 1f Active, 12f Recovery)");
                    LogTrace("   [F2]  P1 Front Punch -> Immediate Dash Forward (Verify Dash Separation)");
                    LogTrace("   [F3]  P1 D, B + 3 Normal Projectile (S, A+G)");
                    LogTrace("   [F4]  P1 D, B + 3 + Block EX Projectile (S, A+G+Space)");
                    LogTrace("   [F5]  JC FP -> Dash -> FP -> Tight D+FP (Frame 56)");
                    LogTrace("   [F6]  JC FP -> Dash -> FP -> Tight D+FP (Frame 58)");
                    LogTrace("   [F7]  JC FP -> Dash -> FP -> Tight D+FP (Frame 59)");
                    LogTrace("   [F8]  JC FP -> Dash -> FP -> Tight D+FP (Frame 60)");
                    break;
                case 10:
                    LogTrace(" Mode 10: Johnny Cage EX Forceball -> F+3 Frame Trap vs P1 EX Reversal");
                    LogTrace("====================================================================");
                    LogTrace("   [F1]  P2 EX Forceball -> F+3 (Solo Baseline Frame 94)");
                    LogTrace("   [F2]  P2 F+3 (Frame 94) vs P1 Stand BL -> D,B+3+BL (+0f Delay: Frame 94)");
                    LogTrace("   [F3]  P2 F+3 (Frame 94) vs P1 Stand BL -> D,B+3+BL (+5f Delay: Frame 99)");
                    LogTrace("   [F4]  P2 F+3 (Frame 94) vs P1 Stand BL -> D,B+3+BL (+6f Delay: Frame 100)");
                    LogTrace("   [F5]  P2 F+3 (Frame 94) vs P1 Crouch BL (on P2 F+3) -> EX (+8f Delay: Frame 102) [Former F8]");
                    LogTrace("   [F6]  P2 F+3 (Frame 94) vs P1 Crouch BL (on P2 F+3) -> EX (+9f Delay: Frame 103)");
                    LogTrace("   [F7]  P2 F+3 (Frame 94) vs P1 Stand BL (No Crouch) -> EX (+8f Delay: Frame 102)");
                    LogTrace("   [F8]  P2 F+3 (Frame 94) vs P1 Stand BL (No Crouch) -> EX (+9f Delay: Frame 103)");
                    break;
                case 11:
                    LogTrace(" Mode 11: Johnny Cage EX Forceball -> F+3 Frame Trap vs P1 D,F+1+BL (EX Forceball)");
                    LogTrace("====================================================================");
                    LogTrace("   [F1]  P2 EX Forceball -> F+3 (Solo Baseline Frame 94)");
                    LogTrace("   [F2]  P2 F+3 (Frame 94) vs P1 Stand BL -> D,F+1+BL (+0f Delay: Frame 94)");
                    LogTrace("   [F3]  P2 F+3 (Frame 94) vs P1 Stand BL -> D,F+1+BL (+5f Delay: Frame 99)");
                    LogTrace("   [F4]  P2 F+3 (Frame 94) vs P1 Stand BL -> D,F+1+BL (+6f Delay: Frame 100)");
                    LogTrace("   [F5]  P2 F+3 (Frame 94) vs P1 Crouch BL (on P2 F+3) -> D,F+1+BL (+8f Delay: Frame 102)");
                    LogTrace("   [F6]  P2 F+3 (Frame 94) vs P1 Crouch BL (on P2 F+3) -> D,F+1+BL (+9f Delay: Frame 103)");
                    LogTrace("   [F7]  P2 F+3 (Frame 94) vs P1 Stand BL (No Crouch) -> D,F+1+BL (+8f Delay: Frame 102)");
                    LogTrace("   [F8]  P2 F+3 (Frame 94) vs P1 Stand BL (No Crouch) -> D,F+1+BL (+9f Delay: Frame 103)");
                    break;
                case 12:
                    LogTrace(" Mode 12: Multi-Device Input Isolation & Mashing Test Bot");
                    LogTrace("====================================================================");
                    LogTrace("   [F1]  P2 Max Mashing      (Extreme burst across P2 controller & keys -> Check P1)");
                    LogTrace("   [F2]  P2 Moderate Mashing (Clean rapid mashing across P2 controller & keys -> Check P1)");
                    LogTrace("   [F3]  P1 Max Mashing      (Extreme burst across P1 controller & keys -> Check P2)");
                    LogTrace("   [F4]  P1 Moderate Mashing (Clean rapid mashing across P1 controller & keys -> Check P2)");
                    break;
                case 13:
                    LogTrace(" Mode 13: Quick Roll Window Identification (Fwd -> Back Rocker)");
                    LogTrace("====================================================================");
                    LogTrace("   [F1]  P1 Sweep (B+4) vs P2 Tech Roll (+42f / +28f post-hit) [CONFIRMED]");
                    LogTrace("   [F2]  P1 Sweep (B+4) vs P2 Tech Roll (+44f / +30f post-hit) [CONFIRMED]");
                    LogTrace("   [F3]  P1 Uppercut (D+2) vs P2 Tech Roll (+65f / +56f post-hit) [CONFIRMED]");
                    LogTrace("   [F4]  P1 Uppercut (D+2) vs P2 Tech Roll (+66f / +57f post-hit) [CONFIRMED]");
                    break;
                case 14:
                    LogTrace(" Mode 14: Delayed Wakeup Buffer Window Identification (F1 - F8)");
                    LogTrace("====================================================================");
                    LogTrace("   Knockdown Move: %s | Down Hold Duration: %df",
                             (g_Mode14_KnockdownMove == 1) ? "P1 Uppercut (D+2)" : "P1 Sweep (B+4)", g_Mode14_HoldFrames);
                    LogTrace("   [F1]  Floor -15f (Airborne Fall / Late Hitstun Buffer)");
                    LogTrace("   [F2]  Floor -10f (Late Airborne Fall Buffer)");
                    LogTrace("   [F3]  Floor  -5f (Pre-Impact Buffer Window)");
                    LogTrace("   [F4]  Floor   0f (Exact Floor Contact 0x460D)");
                    LogTrace("   [F5]  Floor  +5f (Ground Impact Settle)");
                    LogTrace("   [F6]  Floor +10f (Mid Ground Settle)");
                    LogTrace("   [F7]  Floor +15f (Prone Ground Entry 0x620A)");
                    LogTrace("   [F8]  Floor +20f (Prone Ground Buffer)");
                    break;
                case 15:
                    LogTrace(" Mode 15: Blockstun & Priority Pinpoint Diagnostic (F1 - F4)");
                    LogTrace("====================================================================");
                    LogTrace("   [F1]  P1 BK ('J')  -> Hold Up 80f vs P2 Block 25f -> Hold Up 50f (Adv: -5f)");
                    LogTrace("   [F2]  P1 FP ('T')  -> Hold Up 80f vs P2 Block 25f -> Hold Up 50f (Adv:  0f)");
                    LogTrace("   [F3]  P1 BP ('U')  -> Hold Up 80f vs P2 Block 25f -> Hold Up 50f (Adv: -4f)");
                    LogTrace("   [F4]  P1 FK ('G')  -> Hold Up 80f vs P2 Block 25f -> Hold Up 50f (Adv: -14f)");
                    LogTrace("         - Pinpoints exact frame P1 & P2 become actionable / jump");
                    LogTrace("         - Verifies frame advantage & physical move priority on block");
                    break;
                case 16:
                    LogTrace(" Mode 16: Blockstun Distance & Pushback Measurement (F1 - F4)");
                    LogTrace("====================================================================");
                    LogTrace("   [F1]  P1 FP ('T') vs P2 Block -> Measures contact dist & pushback delta");
                    LogTrace("   [F2]  P1 BP ('U') vs P2 Block -> Measures contact dist & pushback delta");
                    LogTrace("   [F3]  P1 FK ('G') vs P2 Block -> Measures contact dist & pushback delta");
                    LogTrace("   [F4]  P1 BK ('J') vs P2 Block -> Measures contact dist & pushback delta");
                    LogTrace("         - Frame-by-frame distance, velocity, and net pushback measurement");
                    LogTrace("         - Verifies custom block pushback distance multipliers in real-time");
                    break;
                case 17:
                    LogTrace(" Mode 17: Attack Frame Data Measurement (F1 - F4)");
                    LogTrace("====================================================================");
                    LogTrace("   [F1]  P1 FP (Front Punch - 'T') Frame Data Measurement");
                    LogTrace("   [F2]  P1 BP (Back Punch  - 'U') Frame Data Measurement");
                    LogTrace("   [F3]  P1 FK (Front Kick  - 'G') Frame Data Measurement");
                    LogTrace("   [F4]  P1 BK (Back Kick   - 'J') Frame Data Measurement");
                    LogTrace("         - Measures exact Startup, Active, and Recovery frames");
                    LogTrace("         - Frame-by-frame log with ActionState, Hitboxes, Vols, SubState");
                    break;
                case 18:
                    LogTrace(" Mode 18: Normal Cancel Diagnostic (F1 - F5)");
                    LogTrace("====================================================================");
                    LogTrace("   [F1]  P1 FP ('T') into b,b,1 (Spear)");
                    LogTrace("   [F2]  P1 FP ('T') into b,b,1+BL (EX Spear)");
                    LogTrace("   [F3]  P1 FP ('T') into Front Dash (F, F)");
                    LogTrace("   [F4]  P1 FP ('T') into Backdash (B, B)");
                    LogTrace("   [F5]  P1 FP ('T') into X-Ray (3+4+BL)");
                    LogTrace("         - Tests hit/block cancel windows & logs cancel destination");
                    LogTrace("         - Verifies whether normal can cancel into target move");
                    break;
                case 19:
                    LogTrace(" Mode 19: Kabal Bug Diagnostics (F1 - F2)");
                    LogTrace("====================================================================");
                    LogTrace("   [F1]  P1 Kabal F+4 -> F+4 Infinite Loop Diagnostic");
                    LogTrace("   [F2]  P1 Kabal 1,1,1 -> b,f+4 (Nomad Dash) -> b+BL (NDC) -> 1 Glitch Link");
                    LogTrace("         - Analyzes massive frame advantage (+30F on F+4) enabling infinite loop");
                    LogTrace("         - Measures Nomad Dash Cancel recovery reduction and illegal combo extension");
                    break;
                case 20:
                    LogTrace(" Mode 20: Crouching Normal Attack Diagnostics (D+1, D+2, D+3, D+4)");
                    LogTrace("====================================================================");
                    LogTrace("   [F1]  D+1 (Down + Front Punch: S + T)      -> Crouching Jab");
                    LogTrace("   [F2]  D+2 (Down + Back Punch:  S + U)      -> Uppercut (Toasty Eligible)");
                    LogTrace("   [F3]  D+3 (Down + Front Kick:  S + G)      -> Crouching Shin Kick");
                    LogTrace("   [F4]  D+4 (Down + Back Kick:   S + J)      -> Crouching Poke");
                    LogTrace("         - Distinguishes exact reaction IDs (rx_id) and script functions");
                    LogTrace("         - Validates Uppercut vs Crouching Normal classification");
                    LogTrace("         - Measures Startup, Active, and Recovery frame telemetry");
                    break;
            }
        }
        LogTrace("   [F9]  Toggle Frame Data Visualizer Timeline");
        LogTrace("   [F10] Cycle Display Mode (Borderless -> Windowed -> Fullscreen)");
        LogTrace("====================================================================\n");
    }

    void TriggerSequenceMacro(MacroSequenceType seq, const char* desc) {
        g_SimSeqType = seq;
        g_SimP1_DirKey = 0;
        g_SimP1_Key = 0;
        g_SimP1_DelayFrames = 0;
        g_SimP2_DirKey = 0;
        g_SimP2_Key = 0;
        g_SimP2_DelayFrames = 0;
        g_SimTriggerFrame = g_CurrentFrame + 15;
        g_SimDesc = desc;

        g_ActiveMacroTriggerFrame = g_SimTriggerFrame;
        g_ActiveMacroDesc = desc;
        g_ActiveMacroP1Delay = 0;
        g_ActiveMacroP2Delay = 0;

        LogTrace("[TEST BOT] >>> Triggering (Sequence): %s in 15 frames <<<", desc);
    }

    static void TriggerSpecialMoveMacro(int steps, const WORD* p1Dirs, WORD p1Btn, const WORD* p2Dirs, WORD p2Btn, const char* desc, int step1Hold = 2, int step1Delay = 2) {
        g_SimSeqType = MACRO_SEQ_GENERIC_SPECIAL;
        g_SimSpecialSeq.stepCount = steps;
        g_SimSpecialSeq.step1HoldFrames = step1Hold;
        g_SimSpecialSeq.step1NeutralDelay = step1Delay;
        for (int i = 0; i < steps; i++) {
            g_SimSpecialSeq.p1_dirs[i] = p1Dirs[i];
            g_SimSpecialSeq.p2_dirs[i] = p2Dirs[i];
        }
        g_SimSpecialSeq.p1_btn = p1Btn;
        g_SimSpecialSeq.p2_btn = p2Btn;

        g_SimP1_DirKey = 0;
        g_SimP1_Key = 0;
        g_SimP1_DelayFrames = 0;
        g_SimP2_DirKey = 0;
        g_SimP2_Key = 0;
        g_SimP2_DelayFrames = 0;
        g_SimTriggerFrame = g_CurrentFrame + 15;
        g_SimDesc = desc;

        g_ActiveMacroTriggerFrame = g_SimTriggerFrame;
        g_ActiveMacroDesc = desc;
        g_ActiveMacroP1Delay = 0;
        g_ActiveMacroP2Delay = 0;

        LogTrace("[TEST BOT] >>> Triggering (Special Move): %s in 15 frames <<<", desc);
    }

    void Trigger2StepMacro(WORD p1_d1, WORD p1_d2, WORD p1_b, WORD p2_d1, WORD p2_d2, WORD p2_b, const char* desc, int step1Hold, int step1Delay) {
        WORD p1[2] = { p1_d1, p1_d2 };
        WORD p2[2] = { p2_d1, p2_d2 };
        TriggerSpecialMoveMacro(2, p1, p1_b, p2, p2_b, desc, step1Hold, step1Delay);
    }

    void Trigger3StepMacro(WORD p1_d1, WORD p1_d2, WORD p1_d3, WORD p1_b, WORD p2_d1, WORD p2_d2, WORD p2_d3, WORD p2_b, const char* desc, int step1Hold, int step1Delay) {
        WORD p1[3] = { p1_d1, p1_d2, p1_d3 };
        WORD p2[3] = { p2_d1, p2_d2, p2_d3 };
        TriggerSpecialMoveMacro(3, p1, p1_b, p2, p2_b, desc, step1Hold, step1Delay);
    }

    void TriggerMacroBot(WORD p1Dir, WORD p1Key, int p1Delay, WORD p2Dir, WORD p2Key, int p2Delay, const char* desc) {
        g_SimSeqType = MACRO_SEQ_STANDARD;
        g_SimP1_DirKey = p1Dir;
        g_SimP1_Key = p1Key;
        g_SimP1_DelayFrames = p1Delay;
        g_SimP2_DirKey = p2Dir;
        g_SimP2_Key = p2Key;
        g_SimP2_DelayFrames = p2Delay;
        g_SimTriggerFrame = g_CurrentFrame + 15;
        g_SimDesc = desc;

        g_ActiveMacroTriggerFrame = g_SimTriggerFrame;
        g_ActiveMacroDesc = desc;
        g_ActiveMacroP1Delay = p1Delay;
        g_ActiveMacroP2Delay = p2Delay;

        LogTrace("[TEST BOT] >>> Triggering: %s in 15 frames <<<", desc);
    }

    void TriggerP2SpecVsP1Norm(WORD p2_d1, WORD p2_d2, WORD p2_btn, WORD p1_dir, WORD p1_btn, int p1_delay, const char* desc) {
        g_SimSeqType = MACRO_SEQ_P2_SPEC_VS_P1_NORM;
        g_SimMatchup.spec_d1 = p2_d1;
        g_SimMatchup.spec_d2 = p2_d2;
        g_SimMatchup.spec_btn = p2_btn;
        g_SimMatchup.norm_dir = p1_dir;
        g_SimMatchup.norm_btn = p1_btn;
        g_SimMatchup.norm_delay = p1_delay;
        g_SimMatchup.isJumpIn = false;
        g_SimMatchup.isAntiAirD2 = false;

        g_SimTriggerFrame = g_CurrentFrame + 15;
        g_SimDesc = desc;
        g_ActiveMacroTriggerFrame = g_SimTriggerFrame;
        g_ActiveMacroDesc = desc;
        g_ActiveMacroP1Delay = p1_delay;
        g_ActiveMacroP2Delay = 0;

        LogTrace("[TEST BOT] >>> Triggering: %s in 15 frames <<<", desc);
    }

    void TriggerP1SpecVsP2Norm(WORD p1_d1, WORD p1_d2, WORD p1_btn, WORD p2_dir, WORD p2_btn, int p2_delay, const char* desc) {
        g_SimSeqType = MACRO_SEQ_P1_SPEC_VS_P2_NORM;
        g_SimMatchup.spec_d1 = p1_d1;
        g_SimMatchup.spec_d2 = p1_d2;
        g_SimMatchup.spec_btn = p1_btn;
        g_SimMatchup.norm_dir = p2_dir;
        g_SimMatchup.norm_btn = p2_btn;
        g_SimMatchup.norm_delay = p2_delay;
        g_SimMatchup.isJumpIn = false;
        g_SimMatchup.isAntiAirD2 = false;

        g_SimTriggerFrame = g_CurrentFrame + 15;
        g_SimDesc = desc;
        g_ActiveMacroTriggerFrame = g_SimTriggerFrame;
        g_ActiveMacroDesc = desc;
        g_ActiveMacroP1Delay = 0;
        g_ActiveMacroP2Delay = p2_delay;

        LogTrace("[TEST BOT] >>> Triggering: %s in 15 frames <<<", desc);
    }

    void TriggerJumpInVsGround(bool isAntiAirD2, const char* desc) {
        g_SimSeqType = MACRO_SEQ_JUMP_IN_VS_GROUND;
        g_SimMatchup.isJumpIn = true;
        g_SimMatchup.isAntiAirD2 = isAntiAirD2;

        g_SimTriggerFrame = g_CurrentFrame + 15;
        g_SimDesc = desc;
        g_ActiveMacroTriggerFrame = g_SimTriggerFrame;
        g_ActiveMacroDesc = desc;
        g_ActiveMacroP1Delay = 0;
        g_ActiveMacroP2Delay = 0;

        LogTrace("[TEST BOT] >>> Triggering: %s in 15 frames <<<", desc);
    }

    void TriggerMode8F1() {
        g_SimP1_DirKey = 0;
        g_SimP1_Key = 0;
        g_SimP1_DelayFrames = 0;
        g_SimP2_DirKey = 0;
        g_SimP2_Key = 0;
        g_SimP2_DelayFrames = 0;
        g_SimTriggerFrame = g_CurrentFrame + 15;
        g_SimDesc = "Mode 8 F1: P1 Hold [1] -> B, B -> Release [1] (Raw Neutral Neg Edge)";
        g_SimSeqType = MACRO_SEQ_MODE8_F1_RAW_NEG_EDGE;
        g_ActiveMacroTriggerFrame = g_SimTriggerFrame;
        g_ActiveMacroDesc = g_SimDesc;
        g_ActiveMacroP1Delay = 0;
        g_ActiveMacroP2Delay = 0;
        LogTrace("[TEST BOT Mode 8] >>> Triggering F1 (P1 Raw Neutral Neg Edge) in 15 frames <<< [P1 NegEdge: %s]",
                 g_P1_NegativeEdgeEnabled ? "ON (Allow)" : "OFF (Disabled)");
    }

    void TriggerMode8F2() {
        g_SimSeqType = MACRO_SEQ_MODE8_F2_NORMAL_SPECIAL;
        g_SimP1_DirKey = 0;
        g_SimP1_Key = 0;
        g_SimP1_DelayFrames = 0;
        g_SimP2_DirKey = 0;
        g_SimP2_Key = 0;
        g_SimP2_DelayFrames = 0;
        g_SimTriggerFrame = g_CurrentFrame + 15;
        g_SimDesc = "Mode 8 F2: P1 B, B -> Press [1] (Normal Special Baseline)";
        g_ActiveMacroTriggerFrame = g_SimTriggerFrame;
        g_ActiveMacroDesc = g_SimDesc;
        g_ActiveMacroP1Delay = 0;
        g_ActiveMacroP2Delay = 0;
        LogTrace("[TEST BOT Mode 8] >>> Triggering F2 (Normal Special Baseline) in 15 frames <<<");
    }

    void TriggerMode8F3() {
        g_SimSeqType = MACRO_SEQ_MODE8_F3_HIT_CONFIRM_CANCEL;
        g_SimP1_DirKey = 0;
        g_SimP1_Key = 0;
        g_SimP1_DelayFrames = 0;
        g_SimP2_DirKey = 0;
        g_SimP2_Key = 0;
        g_SimP2_DelayFrames = 0;
        g_SimTriggerFrame = g_CurrentFrame + 15;
        g_SimDesc = "Mode 8 F3: P1 Close Jab -> Hitstop Cancel into Spear (Hit Neg Edge)";
        g_ActiveMacroTriggerFrame = g_SimTriggerFrame;
        g_ActiveMacroDesc = g_SimDesc;
        g_ActiveMacroP1Delay = 0;
        g_ActiveMacroP2Delay = 0;
        LogTrace("[TEST BOT Mode 8] >>> Triggering F3 (P1 Hit-Confirm Jab Cancel) in 15 frames <<< [P1 NegEdge: %s]",
                 g_P1_NegativeEdgeEnabled ? "ON (Allow)" : "OFF (Disabled)");
    }

    void TriggerMode8F4() {
        g_SimSeqType = MACRO_SEQ_MODE8_F4_WHIFF_NEG_EDGE;
        g_SimP1_DirKey = 0;
        g_SimP1_Key = 0;
        g_SimP1_DelayFrames = 0;
        g_SimP2_DirKey = 0;
        g_SimP2_Key = 0;
        g_SimP2_DelayFrames = 0;
        g_SimTriggerFrame = g_CurrentFrame + 15;
        g_SimDesc = "Mode 8 F4: P1 Whiff Jab -> Neg Edge (Verify Whiff Cancel Blocked)";
        g_ActiveMacroTriggerFrame = g_SimTriggerFrame;
        g_ActiveMacroDesc = g_SimDesc;
        g_ActiveMacroP1Delay = 0;
        g_ActiveMacroP2Delay = 0;
        LogTrace("[TEST BOT Mode 8] >>> Triggering F4 (P1 Whiff Jab Neg Edge) in 15 frames <<< [P1 NegEdge: %s]",
                 g_P1_NegativeEdgeEnabled ? "ON (Allow)" : "OFF (Disabled)");
    }

    void TriggerMode8F5() {
        g_SimSeqType = MACRO_SEQ_MODE8_F5_P2_RAW_NEG_EDGE;
        g_SimP1_DirKey = 0;
        g_SimP1_Key = 0;
        g_SimP1_DelayFrames = 0;
        g_SimP2_DirKey = 0;
        g_SimP2_Key = 0;
        g_SimP2_DelayFrames = 0;
        g_SimTriggerFrame = g_CurrentFrame + 15;
        g_SimDesc = "Mode 8 F5: P2 Hold [1] -> B, B -> Release [1] (P2 Mirrored Raw Neg Edge)";
        g_ActiveMacroTriggerFrame = g_SimTriggerFrame;
        g_ActiveMacroDesc = g_SimDesc;
        g_ActiveMacroP1Delay = 0;
        g_ActiveMacroP2Delay = 0;
        LogTrace("[TEST BOT Mode 8] >>> Triggering F5 (P2 Mirrored Raw Neg Edge) in 15 frames <<< [P2 NegEdge: %s]",
                 g_P2_NegativeEdgeEnabled ? "ON (Allow)" : "OFF (Disabled)");
    }

    void TriggerMode8F6() {
        g_SimSeqType = MACRO_SEQ_MODE8_F6_P2_NORMAL_SPECIAL;
        g_SimP1_DirKey = 0;
        g_SimP1_Key = 0;
        g_SimP1_DelayFrames = 0;
        g_SimP2_DirKey = 0;
        g_SimP2_Key = 0;
        g_SimP2_DelayFrames = 0;
        g_SimTriggerFrame = g_CurrentFrame + 15;
        g_SimDesc = "Mode 8 F6: P2 B, B -> Press [1] (P2 Mirrored Normal Special Baseline)";
        g_ActiveMacroTriggerFrame = g_SimTriggerFrame;
        g_ActiveMacroDesc = g_SimDesc;
        g_ActiveMacroP1Delay = 0;
        g_ActiveMacroP2Delay = 0;
        LogTrace("[TEST BOT Mode 8] >>> Triggering F6 (P2 Normal Special Baseline) in 15 frames <<<");
    }

    void TriggerMode8F7() {
        g_SimSeqType = MACRO_SEQ_MODE8_F7_P2_HIT_CONFIRM_CANCEL;
        g_SimP1_DirKey = 0;
        g_SimP1_Key = 0;
        g_SimP1_DelayFrames = 0;
        g_SimP2_DirKey = 0;
        g_SimP2_Key = 0;
        g_SimP2_DelayFrames = 0;
        g_SimTriggerFrame = g_CurrentFrame + 15;
        g_SimDesc = "Mode 8 F7: P2 Close Jab -> Hitstop Cancel into Spear (P2 Hit Neg Edge)";
        g_ActiveMacroTriggerFrame = g_SimTriggerFrame;
        g_ActiveMacroDesc = g_SimDesc;
        g_ActiveMacroP1Delay = 0;
        g_ActiveMacroP2Delay = 0;
        LogTrace("[TEST BOT Mode 8] >>> Triggering F7 (P2 Hit-Confirm Jab Cancel) in 15 frames <<< [P2 NegEdge: %s]",
                 g_P2_NegativeEdgeEnabled ? "ON (Allow)" : "OFF (Disabled)");
    }

    void TriggerMode8F8() {
        g_SimSeqType = MACRO_SEQ_MODE8_F8_STRING_BUFFER;
        g_SimP1_DirKey = 0;
        g_SimP1_Key = 0;
        g_SimP1_DelayFrames = 0;
        g_SimP2_DirKey = 0;
        g_SimP2_Key = 0;
        g_SimP2_DelayFrames = 0;
        g_SimTriggerFrame = g_CurrentFrame + 15;
        g_SimDesc = "Mode 8 F8: P1 String 1, 1 into Post-Recovery B, B Backdash (Buffer Check)";
        g_ActiveMacroTriggerFrame = g_SimTriggerFrame;
        g_ActiveMacroDesc = g_SimDesc;
        g_ActiveMacroP1Delay = 0;
        g_ActiveMacroP2Delay = 0;
        LogTrace("[TEST BOT Mode 8] >>> Triggering F8 (String Buffer to Backdash) in 15 frames <<<");
    }

    void TriggerMode9F1() {
        g_SimSeqType = MACRO_SEQ_MODE9_F1_FP;
        g_SimP1_DirKey = 0;
        g_SimP1_Key = 0;
        g_SimP1_DelayFrames = 0;
        g_SimP2_DirKey = 0;
        g_SimP2_Key = 0;
        g_SimP2_DelayFrames = 0;
        g_SimTriggerFrame = g_CurrentFrame + 15;
        g_SimDesc = "Mode 9 F1: P1 Front Punch Baseline (11f Startup, 1f Active, 12f Recovery)";
        g_ActiveMacroTriggerFrame = g_SimTriggerFrame;
        g_ActiveMacroDesc = g_SimDesc;
        g_ActiveMacroP1Delay = 0;
        g_ActiveMacroP2Delay = 0;
        LogTrace("[TEST BOT Mode 9] >>> Triggering F1 (FP Baseline) in 15 frames <<<");
    }

    void TriggerMode9F2() {
        g_SimSeqType = MACRO_SEQ_MODE9_F2_FP_DASH;
        g_SimP1_DirKey = 0;
        g_SimP1_Key = 0;
        g_SimP1_DelayFrames = 0;
        g_SimP2_DirKey = 0;
        g_SimP2_Key = 0;
        g_SimP2_DelayFrames = 0;
        g_SimTriggerFrame = g_CurrentFrame + 15;
        g_SimDesc = "Mode 9 F2: P1 Front Punch -> Immediate Dash Forward";
        g_ActiveMacroTriggerFrame = g_SimTriggerFrame;
        g_ActiveMacroDesc = g_SimDesc;
        g_ActiveMacroP1Delay = 0;
        g_ActiveMacroP2Delay = 0;
        LogTrace("[TEST BOT Mode 9] >>> Triggering F2 (FP -> Immediate Dash) in 15 frames <<<");
    }

    void TriggerMode9F3() {
        g_SimSeqType = MACRO_SEQ_MODE9_F3_PROJECTILE_NORMAL;
        g_SimP1_DirKey = 0;
        g_SimP1_Key = 0;
        g_SimP1_DelayFrames = 0;
        g_SimP2_DirKey = 0;
        g_SimP2_Key = 0;
        g_SimP2_DelayFrames = 0;
        g_SimTriggerFrame = g_CurrentFrame + 15;
        g_SimDesc = "Mode 9 F3: P1 D, B + 3 Normal Projectile (S, A+G)";
        g_ActiveMacroTriggerFrame = g_SimTriggerFrame;
        g_ActiveMacroDesc = g_SimDesc;
        g_ActiveMacroP1Delay = 0;
        g_ActiveMacroP2Delay = 0;
        LogTrace("[TEST BOT Mode 9] >>> Triggering F3 (D,B+3 Normal Projectile) in 15 frames <<<");
    }

    void TriggerMode9F4() {
        g_SimSeqType = MACRO_SEQ_MODE9_F4_PROJECTILE_EX;
        g_SimP1_DirKey = 0;
        g_SimP1_Key = 0;
        g_SimP1_DelayFrames = 0;
        g_SimP2_DirKey = 0;
        g_SimP2_Key = 0;
        g_SimP2_DelayFrames = 0;
        g_SimTriggerFrame = g_CurrentFrame + 15;
        g_SimDesc = "Mode 9 F4: P1 D, B + 3 + Block EX Projectile (S, A+G+Space)";
        g_ActiveMacroTriggerFrame = g_SimTriggerFrame;
        g_ActiveMacroDesc = g_SimDesc;
        g_ActiveMacroP1Delay = 0;
        g_ActiveMacroP2Delay = 0;
        LogTrace("[TEST BOT Mode 9] >>> Triggering F4 (D,B+3+Block EX Projectile) in 15 frames <<<");
    }

    void TriggerMode9F5() {
        g_SimSeqType = MACRO_SEQ_MODE9_F5_JC_FP_HOLD_DOWN_FP;
        g_SimP1_DirKey = 0;
        g_SimP1_Key = 0;
        g_SimP1_DelayFrames = 0;
        g_SimP2_DirKey = 0;
        g_SimP2_Key = 0;
        g_SimP2_DelayFrames = 0;
        g_SimTriggerFrame = g_CurrentFrame + 15;
        g_SimDesc = "Mode 9 F5: JC FP -> Dash -> FP -> Tight D+FP (Frame 56)";
        g_ActiveMacroTriggerFrame = g_SimTriggerFrame;
        g_ActiveMacroDesc = g_SimDesc;
        g_ActiveMacroP1Delay = 0;
        g_ActiveMacroP2Delay = 0;
        LogTrace("[TEST BOT Mode 9] >>> Triggering F5 (JC FP -> Dash -> FP -> D+FP Frame 56) in 15 frames <<<");
    }

    void TriggerMode9F6() {
        g_SimSeqType = MACRO_SEQ_MODE9_F6_JC_FP_HOLD_DOWN_FP;
        g_SimP1_DirKey = 0;
        g_SimP1_Key = 0;
        g_SimP1_DelayFrames = 0;
        g_SimP2_DirKey = 0;
        g_SimP2_Key = 0;
        g_SimP2_DelayFrames = 0;
        g_SimTriggerFrame = g_CurrentFrame + 15;
        g_SimDesc = "Mode 9 F6: JC FP -> Dash -> FP -> Tight D+FP (Frame 58)";
        g_ActiveMacroTriggerFrame = g_SimTriggerFrame;
        g_ActiveMacroDesc = g_SimDesc;
        g_ActiveMacroP1Delay = 0;
        g_ActiveMacroP2Delay = 0;
        LogTrace("[TEST BOT Mode 9] >>> Triggering F6 (JC FP -> Dash -> FP -> D+FP Frame 58) in 15 frames <<<");
    }

    void TriggerMode9F7() {
        g_SimSeqType = MACRO_SEQ_MODE9_F7_JC_FP_HOLD_DOWN_FP;
        g_SimP1_DirKey = 0;
        g_SimP1_Key = 0;
        g_SimP1_DelayFrames = 0;
        g_SimP2_DirKey = 0;
        g_SimP2_Key = 0;
        g_SimP2_DelayFrames = 0;
        g_SimTriggerFrame = g_CurrentFrame + 15;
        g_SimDesc = "Mode 9 F7: JC FP -> Dash -> FP -> Tight D+FP (Frame 59)";
        g_ActiveMacroTriggerFrame = g_SimTriggerFrame;
        g_ActiveMacroDesc = g_SimDesc;
        g_ActiveMacroP1Delay = 0;
        g_ActiveMacroP2Delay = 0;
        LogTrace("[TEST BOT Mode 9] >>> Triggering F7 (JC FP -> Dash -> FP -> D+FP Frame 59) in 15 frames <<<");
    }

    void TriggerMode9F8() {
        g_SimSeqType = MACRO_SEQ_MODE9_F8_JC_FP_HOLD_DOWN_FP;
        g_SimP1_DirKey = 0;
        g_SimP1_Key = 0;
        g_SimP1_DelayFrames = 0;
        g_SimP2_DirKey = 0;
        g_SimP2_Key = 0;
        g_SimP2_DelayFrames = 0;
        g_SimTriggerFrame = g_CurrentFrame + 15;
        g_SimDesc = "Mode 9 F8: JC FP -> Dash -> FP -> Tight D+FP (Frame 60)";
        g_ActiveMacroTriggerFrame = g_SimTriggerFrame;
        g_ActiveMacroDesc = g_SimDesc;
        g_ActiveMacroP1Delay = 0;
        g_ActiveMacroP2Delay = 0;
        LogTrace("[TEST BOT Mode 9] >>> Triggering F8 (JC FP -> Dash -> FP -> D+FP Frame 60) in 15 frames <<<");
    }

    void TriggerMode10F1() {
        g_SimSeqType = MACRO_SEQ_MODE10_F1_JC_EX_FORCEBALL_SOLO;
        g_SimP1_DirKey = 0; g_SimP1_Key = 0; g_SimP1_DelayFrames = 0;
        g_SimP2_DirKey = 0; g_SimP2_Key = 0; g_SimP2_DelayFrames = 0;
        g_SimTriggerFrame = g_CurrentFrame + 15;
        g_SimDesc = "Mode 10 F1: P2 Left(30f) -> Neutral(5f) -> D,F+2+BL -> F+3 (Solo Baseline Frame 94)";
        g_ActiveMacroTriggerFrame = g_SimTriggerFrame;
        g_ActiveMacroDesc = g_SimDesc;
        g_ActiveMacroP1Delay = 0; g_ActiveMacroP2Delay = 0;
        LogTrace("[TEST BOT Mode 10] >>> Triggering F1 (P2 EX Forceball -> F+3 Solo Baseline Frame 94) in 15 frames <<<");
    }

    void TriggerMode10F2() {
        g_SimSeqType = MACRO_SEQ_MODE10_F2_JC_EX_FORCEBALL_VS_P1_EX;
        g_SimP1_DirKey = 0; g_SimP1_Key = 0; g_SimP1_DelayFrames = 0;
        g_SimP2_DirKey = 0; g_SimP2_Key = 0; g_SimP2_DelayFrames = 0;
        g_SimTriggerFrame = g_CurrentFrame + 15;
        g_SimDesc = "Mode 10 F2: P2 F+3 (Frame 94) vs P1 Stand BL -> D,B+3+BL (+0f Delay: Frame 94)";
        g_ActiveMacroTriggerFrame = g_SimTriggerFrame;
        g_ActiveMacroDesc = g_SimDesc;
        g_ActiveMacroP1Delay = 0; g_ActiveMacroP2Delay = 0;
        LogTrace("[TEST BOT Mode 10] >>> Triggering F2 (P2 F+3 Frame 94 vs P1 Stand BL +0f Delay Frame 94) in 15 frames <<<");
    }

    void TriggerMode10F3() {
        g_SimSeqType = MACRO_SEQ_MODE10_F3_JC_EX_FORCEBALL_VS_P1_EX;
        g_SimP1_DirKey = 0; g_SimP1_Key = 0; g_SimP1_DelayFrames = 0;
        g_SimP2_DirKey = 0; g_SimP2_Key = 0; g_SimP2_DelayFrames = 0;
        g_SimTriggerFrame = g_CurrentFrame + 15;
        g_SimDesc = "Mode 10 F3: P2 F+3 (Frame 94) vs P1 Stand BL -> D,B+3+BL (+5f Delay: Frame 99) [Formerly F7]";
        g_ActiveMacroTriggerFrame = g_SimTriggerFrame;
        g_ActiveMacroDesc = g_SimDesc;
        g_ActiveMacroP1Delay = 0; g_ActiveMacroP2Delay = 0;
        LogTrace("[TEST BOT Mode 10] >>> Triggering F3 (P2 F+3 Frame 94 vs P1 Stand BL +5f Delay Frame 99) in 15 frames <<<");
    }

    void TriggerMode10F4() {
        g_SimSeqType = MACRO_SEQ_MODE10_F4_JC_EX_FORCEBALL_VS_P1_EX;
        g_SimP1_DirKey = 0; g_SimP1_Key = 0; g_SimP1_DelayFrames = 0;
        g_SimP2_DirKey = 0; g_SimP2_Key = 0; g_SimP2_DelayFrames = 0;
        g_SimTriggerFrame = g_CurrentFrame + 15;
        g_SimDesc = "Mode 10 F4: P2 F+3 (Frame 94) vs P1 Stand BL -> D,B+3+BL (+6f Delay: Frame 100) [Formerly F8]";
        g_ActiveMacroTriggerFrame = g_SimTriggerFrame;
        g_ActiveMacroDesc = g_SimDesc;
        g_ActiveMacroP1Delay = 0; g_ActiveMacroP2Delay = 0;
        LogTrace("[TEST BOT Mode 10] >>> Triggering F4 (P2 F+3 Frame 94 vs P1 Stand BL +6f Delay Frame 100) in 15 frames <<<");
    }

    void TriggerMode10F5() {
        g_SimSeqType = MACRO_SEQ_MODE10_F5_JC_EX_FORCEBALL_VS_P1_CROUCH;
        g_SimP1_DirKey = 0; g_SimP1_Key = 0; g_SimP1_DelayFrames = 0;
        g_SimP2_DirKey = 0; g_SimP2_Key = 0; g_SimP2_DelayFrames = 0;
        g_SimTriggerFrame = g_CurrentFrame + 15;
        g_SimDesc = "Mode 10 F5: P2 F+3 (Frame 94) vs P1 Crouch BL (on P2 F+3) -> EX (+8f Delay: Frame 102) [Former F8]";
        g_ActiveMacroTriggerFrame = g_SimTriggerFrame;
        g_ActiveMacroDesc = g_SimDesc;
        g_ActiveMacroP1Delay = 0; g_ActiveMacroP2Delay = 0;
        LogTrace("[TEST BOT Mode 10] >>> Triggering F5 (P2 F+3 Frame 94 vs P1 Crouch BL +8f Delay Frame 102) in 15 frames <<<");
    }

    void TriggerMode10F6() {
        g_SimSeqType = MACRO_SEQ_MODE10_F6_JC_EX_FORCEBALL_VS_P1_CROUCH;
        g_SimP1_DirKey = 0; g_SimP1_Key = 0; g_SimP1_DelayFrames = 0;
        g_SimP2_DirKey = 0; g_SimP2_Key = 0; g_SimP2_DelayFrames = 0;
        g_SimTriggerFrame = g_CurrentFrame + 15;
        g_SimDesc = "Mode 10 F6: P2 F+3 (Frame 94) vs P1 Crouch BL (on P2 F+3) -> EX (+9f Delay: Frame 103)";
        g_ActiveMacroTriggerFrame = g_SimTriggerFrame;
        g_ActiveMacroDesc = g_SimDesc;
        g_ActiveMacroP1Delay = 0; g_ActiveMacroP2Delay = 0;
        LogTrace("[TEST BOT Mode 10] >>> Triggering F6 (P2 F+3 Frame 94 vs P1 Crouch BL +9f Delay Frame 103) in 15 frames <<<");
    }

    void TriggerMode10F7() {
        g_SimSeqType = MACRO_SEQ_MODE10_F7_JC_EX_FORCEBALL_VS_P1_STAND;
        g_SimP1_DirKey = 0; g_SimP1_Key = 0; g_SimP1_DelayFrames = 0;
        g_SimP2_DirKey = 0; g_SimP2_Key = 0; g_SimP2_DelayFrames = 0;
        g_SimTriggerFrame = g_CurrentFrame + 15;
        g_SimDesc = "Mode 10 F7: P2 F+3 (Frame 94) vs P1 Stand BL (No Crouch) -> EX (+8f Delay: Frame 102)";
        g_ActiveMacroTriggerFrame = g_SimTriggerFrame;
        g_ActiveMacroDesc = g_SimDesc;
        g_ActiveMacroP1Delay = 0; g_ActiveMacroP2Delay = 0;
        LogTrace("[TEST BOT Mode 10] >>> Triggering F7 (P2 F+3 Frame 94 vs P1 Stand BL +8f Delay Frame 102) in 15 frames <<<");
    }

    void TriggerMode10F8() {
        g_SimSeqType = MACRO_SEQ_MODE10_F8_JC_EX_FORCEBALL_VS_P1_STAND;
        g_SimP1_DirKey = 0; g_SimP1_Key = 0; g_SimP1_DelayFrames = 0;
        g_SimP2_DirKey = 0; g_SimP2_Key = 0; g_SimP2_DelayFrames = 0;
        g_SimTriggerFrame = g_CurrentFrame + 15;
        g_SimDesc = "Mode 10 F8: P2 F+3 (Frame 94) vs P1 Stand BL (No Crouch) -> EX (+9f Delay: Frame 103)";
        g_ActiveMacroTriggerFrame = g_SimTriggerFrame;
        g_ActiveMacroDesc = g_SimDesc;
        g_ActiveMacroP1Delay = 0; g_ActiveMacroP2Delay = 0;
        LogTrace("[TEST BOT Mode 10] >>> Triggering F8 (P2 F+3 Frame 94 vs P1 Stand BL +9f Delay Frame 103) in 15 frames <<<");
    }

    void TriggerMode11F1() {
        g_SimSeqType = MACRO_SEQ_MODE11_F1_JC_EX_FORCEBALL_SOLO;
        g_SimP1_DirKey = 0; g_SimP1_Key = 0; g_SimP1_DelayFrames = 0;
        g_SimP2_DirKey = 0; g_SimP2_Key = 0; g_SimP2_DelayFrames = 0;
        g_SimTriggerFrame = g_CurrentFrame + 15;
        g_SimDesc = "Mode 11 F1: P2 Left(30f) -> Neutral(5f) -> D,F+2+BL -> F+3 (Solo Baseline Frame 94)";
        g_ActiveMacroTriggerFrame = g_SimTriggerFrame;
        g_ActiveMacroDesc = g_SimDesc;
        g_ActiveMacroP1Delay = 0; g_ActiveMacroP2Delay = 0;
        LogTrace("[TEST BOT Mode 11] >>> Triggering F1 (P2 EX Forceball -> F+3 Solo Baseline Frame 94) in 15 frames <<<");
    }

    void TriggerMode11F2() {
        g_SimSeqType = MACRO_SEQ_MODE11_F2_JC_EX_FORCEBALL_VS_P1_DF1_EX;
        g_SimP1_DirKey = 0; g_SimP1_Key = 0; g_SimP1_DelayFrames = 0;
        g_SimP2_DirKey = 0; g_SimP2_Key = 0; g_SimP2_DelayFrames = 0;
        g_SimTriggerFrame = g_CurrentFrame + 15;
        g_SimDesc = "Mode 11 F2: P2 F+3 (Frame 94) vs P1 Stand BL -> D,F+1+BL (+0f Delay: Frame 94)";
        g_ActiveMacroTriggerFrame = g_SimTriggerFrame;
        g_ActiveMacroDesc = g_SimDesc;
        g_ActiveMacroP1Delay = 0; g_ActiveMacroP2Delay = 0;
        LogTrace("[TEST BOT Mode 11] >>> Triggering F2 (P2 F+3 Frame 94 vs P1 Stand BL +0f Delay Frame 94 [D,F+1+BL]) in 15 frames <<<");
    }

    void TriggerMode11F3() {
        g_SimSeqType = MACRO_SEQ_MODE11_F3_JC_EX_FORCEBALL_VS_P1_DF1_EX;
        g_SimP1_DirKey = 0; g_SimP1_Key = 0; g_SimP1_DelayFrames = 0;
        g_SimP2_DirKey = 0; g_SimP2_Key = 0; g_SimP2_DelayFrames = 0;
        g_SimTriggerFrame = g_CurrentFrame + 15;
        g_SimDesc = "Mode 11 F3: P2 F+3 (Frame 94) vs P1 Stand BL -> D,F+1+BL (+5f Delay: Frame 99)";
        g_ActiveMacroTriggerFrame = g_SimTriggerFrame;
        g_ActiveMacroDesc = g_SimDesc;
        g_ActiveMacroP1Delay = 0; g_ActiveMacroP2Delay = 0;
        LogTrace("[TEST BOT Mode 11] >>> Triggering F3 (P2 F+3 Frame 94 vs P1 Stand BL +5f Delay Frame 99 [D,F+1+BL]) in 15 frames <<<");
    }

    void TriggerMode11F4() {
        g_SimSeqType = MACRO_SEQ_MODE11_F4_JC_EX_FORCEBALL_VS_P1_DF1_EX;
        g_SimP1_DirKey = 0; g_SimP1_Key = 0; g_SimP1_DelayFrames = 0;
        g_SimP2_DirKey = 0; g_SimP2_Key = 0; g_SimP2_DelayFrames = 0;
        g_SimTriggerFrame = g_CurrentFrame + 15;
        g_SimDesc = "Mode 11 F4: P2 F+3 (Frame 94) vs P1 Stand BL -> D,F+1+BL (+6f Delay: Frame 100)";
        g_ActiveMacroTriggerFrame = g_SimTriggerFrame;
        g_ActiveMacroDesc = g_SimDesc;
        g_ActiveMacroP1Delay = 0; g_ActiveMacroP2Delay = 0;
        LogTrace("[TEST BOT Mode 11] >>> Triggering F4 (P2 F+3 Frame 94 vs P1 Stand BL +6f Delay Frame 100 [D,F+1+BL]) in 15 frames <<<");
    }

    void TriggerMode11F5() {
        g_SimSeqType = MACRO_SEQ_MODE11_F5_JC_EX_FORCEBALL_VS_P1_DF1_CROUCH;
        g_SimP1_DirKey = 0; g_SimP1_Key = 0; g_SimP1_DelayFrames = 0;
        g_SimP2_DirKey = 0; g_SimP2_Key = 0; g_SimP2_DelayFrames = 0;
        g_SimTriggerFrame = g_CurrentFrame + 15;
        g_SimDesc = "Mode 11 F5: P2 F+3 (Frame 94) vs P1 Crouch BL (on P2 F+3) -> D,F+1+BL (+8f Delay: Frame 102)";
        g_ActiveMacroTriggerFrame = g_SimTriggerFrame;
        g_ActiveMacroDesc = g_SimDesc;
        g_ActiveMacroP1Delay = 0; g_ActiveMacroP2Delay = 0;
        LogTrace("[TEST BOT Mode 11] >>> Triggering F5 (P2 F+3 Frame 94 vs P1 Crouch BL +8f Delay Frame 102 [D,F+1+BL]) in 15 frames <<<");
    }

    void TriggerMode11F6() {
        g_SimSeqType = MACRO_SEQ_MODE11_F6_JC_EX_FORCEBALL_VS_P1_DF1_CROUCH;
        g_SimP1_DirKey = 0; g_SimP1_Key = 0; g_SimP1_DelayFrames = 0;
        g_SimP2_DirKey = 0; g_SimP2_Key = 0; g_SimP2_DelayFrames = 0;
        g_SimTriggerFrame = g_CurrentFrame + 15;
        g_SimDesc = "Mode 11 F6: P2 F+3 (Frame 94) vs P1 Crouch BL (on P2 F+3) -> D,F+1+BL (+9f Delay: Frame 103)";
        g_ActiveMacroTriggerFrame = g_SimTriggerFrame;
        g_ActiveMacroDesc = g_SimDesc;
        g_ActiveMacroP1Delay = 0; g_ActiveMacroP2Delay = 0;
        LogTrace("[TEST BOT Mode 11] >>> Triggering F6 (P2 F+3 Frame 94 vs P1 Crouch BL +9f Delay Frame 103 [D,F+1+BL]) in 15 frames <<<");
    }

    void TriggerMode11F7() {
        g_SimSeqType = MACRO_SEQ_MODE11_F7_JC_EX_FORCEBALL_VS_P1_DF1_STAND;
        g_SimP1_DirKey = 0; g_SimP1_Key = 0; g_SimP1_DelayFrames = 0;
        g_SimP2_DirKey = 0; g_SimP2_Key = 0; g_SimP2_DelayFrames = 0;
        g_SimTriggerFrame = g_CurrentFrame + 15;
        g_SimDesc = "Mode 11 F7: P2 F+3 (Frame 94) vs P1 Stand BL (No Crouch) -> D,F+1+BL (+8f Delay: Frame 102)";
        g_ActiveMacroTriggerFrame = g_SimTriggerFrame;
        g_ActiveMacroDesc = g_SimDesc;
        g_ActiveMacroP1Delay = 0; g_ActiveMacroP2Delay = 0;
        LogTrace("[TEST BOT Mode 11] >>> Triggering F7 (P2 F+3 Frame 94 vs P1 Stand BL +8f Delay Frame 102 [D,F+1+BL]) in 15 frames <<<");
    }

    void TriggerMode11F8() {
        g_SimSeqType = MACRO_SEQ_MODE11_F8_JC_EX_FORCEBALL_VS_P1_DF1_STAND;
        g_SimP1_DirKey = 0; g_SimP1_Key = 0; g_SimP1_DelayFrames = 0;
        g_SimP2_DirKey = 0; g_SimP2_Key = 0; g_SimP2_DelayFrames = 0;
        g_SimTriggerFrame = g_CurrentFrame + 15;
        g_SimDesc = "Mode 11 F8: P2 F+3 (Frame 94) vs P1 Stand BL (No Crouch) -> D,F+1+BL (+9f Delay: Frame 103)";
        g_ActiveMacroTriggerFrame = g_SimTriggerFrame;
        g_ActiveMacroDesc = g_SimDesc;
        g_ActiveMacroP1Delay = 0; g_ActiveMacroP2Delay = 0;
        LogTrace("[TEST BOT Mode 11] >>> Triggering F8 (P2 F+3 Frame 94 vs P1 Stand BL +9f Delay Frame 103 [D,F+1+BL]) in 15 frames <<<");
    }

    void TriggerMode12F1() {
        g_SimSeqType = MACRO_SEQ_MODE12_F1_P2_MAX_MASH;
        g_SimP1_DirKey = 0; g_SimP1_Key = 0; g_SimP1_DelayFrames = 0;
        g_SimP2_DirKey = 0; g_SimP2_Key = 0; g_SimP2_DelayFrames = 0;
        g_SimTriggerFrame = g_CurrentFrame + 15;
        g_SimDesc = "Mode 12 F1: P2 Max Wild Mashing (All buttons + directions on P2 devices -> Check P1 for bleed)";
        g_ActiveMacroTriggerFrame = g_SimTriggerFrame;
        g_ActiveMacroDesc = g_SimDesc;
        g_ActiveMacroP1Delay = 0; g_ActiveMacroP2Delay = 0;
        LogTrace("[TEST BOT Mode 12] >>> Triggering F1 (P2 Max Wild Mashing across Gamepad & Keyboard) in 15 frames <<<");
    }

    void TriggerMode12F2() {
        g_SimSeqType = MACRO_SEQ_MODE12_F2_P2_MODERATE_MASH;
        g_SimP1_DirKey = 0; g_SimP1_Key = 0; g_SimP1_DelayFrames = 0;
        g_SimP2_DirKey = 0; g_SimP2_Key = 0; g_SimP2_DelayFrames = 0;
        g_SimTriggerFrame = g_CurrentFrame + 15;
        g_SimDesc = "Mode 12 F2: P2 Moderate Mashing (P2 devices -> Check P1 for bleed)";
        g_ActiveMacroTriggerFrame = g_SimTriggerFrame;
        g_ActiveMacroDesc = g_SimDesc;
        g_ActiveMacroP1Delay = 0; g_ActiveMacroP2Delay = 0;
        LogTrace("[TEST BOT Mode 12] >>> Triggering F2 (P2 Moderate Mashing across Gamepad & Keyboard) in 15 frames <<<");
    }

    void TriggerMode12F3() {
        g_SimSeqType = MACRO_SEQ_MODE12_F3_P1_MAX_MASH;
        g_SimP1_DirKey = 0; g_SimP1_Key = 0; g_SimP1_DelayFrames = 0;
        g_SimP2_DirKey = 0; g_SimP2_Key = 0; g_SimP2_DelayFrames = 0;
        g_SimTriggerFrame = g_CurrentFrame + 15;
        g_SimDesc = "Mode 12 F3: P1 Max Wild Mashing (All buttons + directions on P1 devices -> Check P2 for bleed)";
        g_ActiveMacroTriggerFrame = g_SimTriggerFrame;
        g_ActiveMacroDesc = g_SimDesc;
        g_ActiveMacroP1Delay = 0; g_ActiveMacroP2Delay = 0;
        LogTrace("[TEST BOT Mode 12] >>> Triggering F3 (P1 Max Wild Mashing across Gamepad & Keyboard) in 15 frames <<<");
    }

    void TriggerMode12F4() {
        g_SimSeqType = MACRO_SEQ_MODE12_F4_P1_MODERATE_MASH;
        g_SimP1_DirKey = 0; g_SimP1_Key = 0; g_SimP1_DelayFrames = 0;
        g_SimP2_DirKey = 0; g_SimP2_Key = 0; g_SimP2_DelayFrames = 0;
        g_SimTriggerFrame = g_CurrentFrame + 15;
        g_SimDesc = "Mode 12 F4: P1 Moderate Mashing (P1 devices -> Check P2 for bleed)";
        g_ActiveMacroTriggerFrame = g_SimTriggerFrame;
        g_ActiveMacroDesc = g_SimDesc;
        g_ActiveMacroP1Delay = 0; g_ActiveMacroP2Delay = 0;
        LogTrace("[TEST BOT Mode 12] >>> Triggering F4 (P1 Moderate Mashing across Gamepad & Keyboard) in 15 frames <<<");
    }

    void TriggerMode12F5() {
        g_SimSeqType = MACRO_SEQ_MODE12_F5_P2_MASH_P1_WALK_4S_BACK_4S_FWD;
        g_SimP1_DirKey = 0; g_SimP1_Key = 0; g_SimP1_DelayFrames = 0;
        g_SimP2_DirKey = 0; g_SimP2_Key = 0; g_SimP2_DelayFrames = 0;
        g_SimTriggerFrame = g_CurrentFrame + 15;
        g_SimDesc = "Mode 12 F5: P2 Mashing (6s) + P1 Walk 4s Back -> 4s Fwd (Movement & Bleed Observation)";
        g_ActiveMacroTriggerFrame = g_SimTriggerFrame;
        g_ActiveMacroDesc = g_SimDesc;
        g_ActiveMacroP1Delay = 0; g_ActiveMacroP2Delay = 0;
        LogTrace("[TEST BOT Mode 12] >>> Triggering F5 (P2 Mashing 6s + P1 Walk 4s Back / 4s Fwd) in 15 frames <<<");
    }

    void TriggerMode12F6() {
        g_SimSeqType = MACRO_SEQ_MODE12_F6_P1_MASH_P2_WALK_4S_BACK_4S_FWD;
        g_SimP1_DirKey = 0; g_SimP1_Key = 0; g_SimP1_DelayFrames = 0;
        g_SimP2_DirKey = 0; g_SimP2_Key = 0; g_SimP2_DelayFrames = 0;
        g_SimTriggerFrame = g_CurrentFrame + 15;
        g_SimDesc = "Mode 12 F6: P1 Mashing (6s) + P2 Walk 4s Back -> 4s Fwd (Movement & Bleed Observation)";
        g_ActiveMacroTriggerFrame = g_SimTriggerFrame;
        g_ActiveMacroDesc = g_SimDesc;
        g_ActiveMacroP1Delay = 0; g_ActiveMacroP2Delay = 0;
        LogTrace("[TEST BOT Mode 12] >>> Triggering F6 (P1 Mashing 6s + P2 Walk 4s Back / 4s Fwd) in 15 frames <<<");
    }

    void TriggerMode13FKey(int fNum) {
        int index = fNum - 1;
        MacroSequenceType seq = (MacroSequenceType)((int)MACRO_SEQ_MODE13_F1_QUICK_ROLL + index);
        g_SimSeqType = seq;
        g_SimP1_DirKey = 0; g_SimP1_Key = 0; g_SimP1_DelayFrames = 0;
        g_SimP2_DirKey = 0; g_SimP2_Key = 0; g_SimP2_DelayFrames = 0;
        g_SimTriggerFrame = g_CurrentFrame + 15;

        int p2Offset = (fNum == 1) ? 42 : (fNum == 2) ? 44 : (fNum == 3) ? 65 : 66;
        bool isUppercut = (fNum >= 3);

        static char s_Mode13Desc[128];
        if (isUppercut) {
            snprintf(s_Mode13Desc, sizeof(s_Mode13Desc),
                     "Mode 13 F%d: P1 Uppercut (D+2) vs P2 Tech Roll (+%df)", fNum, p2Offset);
        } else {
            snprintf(s_Mode13Desc, sizeof(s_Mode13Desc),
                     "Mode 13 F%d: P1 Sweep (B+4) vs P2 Tech Roll (+%df)", fNum, p2Offset);
        }
        g_SimDesc = s_Mode13Desc;
        g_ActiveMacroTriggerFrame = g_SimTriggerFrame;
        g_ActiveMacroDesc = g_SimDesc;
        g_ActiveMacroP1Delay = 0; g_ActiveMacroP2Delay = p2Offset;

        LogTrace("====================================================================");
        if (isUppercut) {
            LogTrace("[MODE 13 F%d QUEUED | Frame #%llu] >>> P1 UPPERCUT vs P2 QUICK ROLL WINDOW TEST <<<", fNum, g_CurrentFrame);
            LogTrace("  P1 Action: Down + 2 (Uppercut - S + U)");
            LogTrace("  P2 Action: Forward -> Back Rocker");
            LogTrace("  P2 Roll Target Offset: +%d frames from D+2 Start (+%df post-hit)",
                     p2Offset, p2Offset - 9);
        } else {
            LogTrace("[MODE 13 F%d QUEUED | Frame #%llu] >>> P1 SWEEP vs P2 QUICK ROLL WINDOW TEST <<<", fNum, g_CurrentFrame);
            LogTrace("  P1 Action: Back + 4 (Sweep - Back + J)");
            LogTrace("  P2 Action: Forward -> Back Rocker");
            LogTrace("  P2 Roll Target Offset: +%d frames from Sweep Start (+%df post-hit)",
                     p2Offset, p2Offset - 14);
        }
        LogTrace("  Macro starts in 15 frames at Frame #%llu", g_SimTriggerFrame);
        LogTrace("====================================================================");
    }

    void TriggerMode13F1() { TriggerMode13FKey(1); }
    void TriggerMode13F2() { TriggerMode13FKey(2); }
    void TriggerMode13F3() { TriggerMode13FKey(3); }
    void TriggerMode13F4() { TriggerMode13FKey(4); }

    void TriggerMode14FKey(int fNum) {
        if (fNum < 1) fNum = 1;
        if (fNum > 8) fNum = 8;
        g_Mode14_ActiveFNum = fNum;
        g_SimSeqType = MACRO_SEQ_MODE14_DELAYED_WAKEUP;
        g_SimP1_DirKey = 0; g_SimP1_Key = 0; g_SimP1_DelayFrames = 0;
        g_SimP2_DirKey = 0; g_SimP2_Key = 0; g_SimP2_DelayFrames = 0;
        g_SimTriggerFrame = g_CurrentFrame + 15;

        bool isUppercut = (g_Mode14_KnockdownMove == 1);
        int baseFloor = isUppercut ? 67 : 42;
        int relOffset = GetMode14RelativeOffset(fNum);
        int p2Offset = baseFloor + relOffset;
        if (p2Offset < 15) p2Offset = 15;
        int holdDuration = g_Mode14_HoldFrames;
        int hitStartup = isUppercut ? 9 : 14;

        static char s_Mode14Desc[128];
        snprintf(s_Mode14Desc, sizeof(s_Mode14Desc),
                 "Mode 14 F%d: %s vs P2 Hold Down %df (Offset: %+df rel to floor)",
                 fNum, isUppercut ? "Uppercut (D+2)" : "Sweep (B+4)", holdDuration, relOffset);

        g_SimDesc = s_Mode14Desc;
        g_ActiveMacroTriggerFrame = g_SimTriggerFrame;
        g_ActiveMacroDesc = g_SimDesc;
        g_ActiveMacroP1Delay = 0;
        g_ActiveMacroP2Delay = p2Offset;

        LogTrace("====================================================================");
        LogTrace("[MODE 14 F%d QUEUED | Frame #%llu] >>> %s vs P2 DELAYED WAKEUP WINDOW TEST <<<",
                 fNum, g_CurrentFrame, isUppercut ? "P1 UPPERCUT" : "P1 SWEEP");
        LogTrace("  P1 Action: %s", isUppercut ? "Down + 2 (Uppercut - S + U)" : "Back + 4 (Sweep - Back + J)");
        LogTrace("  P2 Action: Hold Down (VK_DOWN) for %d frames (F%d Preset)", holdDuration, fNum);
        LogTrace("  P2 Injection Timing: %+df relative to Floor Impact (0x460D at +%df) -> Target: +%df",
                 relOffset, baseFloor, p2Offset);
        LogTrace("  P2 Release Timing:   +%df from Start (+%df post-hit)",
                 p2Offset + holdDuration, p2Offset + holdDuration - hitStartup);
        LogTrace("  Macro starts in 15 frames at Frame #%llu", g_SimTriggerFrame);
        LogTrace("====================================================================");
    }

    void TriggerMode14Custom(int relOffset, int holdDuration) {
        g_Mode14_ActiveFNum = 0;
        g_SimSeqType = MACRO_SEQ_MODE14_DELAYED_WAKEUP;
        g_SimP1_DirKey = 0; g_SimP1_Key = 0; g_SimP1_DelayFrames = 0;
        g_SimP2_DirKey = 0; g_SimP2_Key = 0; g_SimP2_DelayFrames = 0;
        g_SimTriggerFrame = g_CurrentFrame + 15;

        bool isUppercut = (g_Mode14_KnockdownMove == 1);
        int baseFloor = isUppercut ? 67 : 42;
        int p2Offset = baseFloor + relOffset;
        if (p2Offset < 15) p2Offset = 15;
        int hitStartup = isUppercut ? 9 : 14;

        static char s_Mode14CustomDesc[128];
        snprintf(s_Mode14CustomDesc, sizeof(s_Mode14CustomDesc),
                 "Mode 14 Custom: %s vs P2 Hold Down %df (Offset: %+df rel to floor)",
                 isUppercut ? "Uppercut (D+2)" : "Sweep (B+4)", holdDuration, relOffset);

        g_SimDesc = s_Mode14CustomDesc;
        g_ActiveMacroTriggerFrame = g_SimTriggerFrame;
        g_ActiveMacroDesc = g_SimDesc;
        g_ActiveMacroP1Delay = 0;
        g_ActiveMacroP2Delay = p2Offset;

        LogTrace("====================================================================");
        LogTrace("[MODE 14 CUSTOM QUEUED | Frame #%llu] >>> %s vs P2 DELAYED WAKEUP WINDOW TEST <<<",
                 g_CurrentFrame, isUppercut ? "P1 UPPERCUT" : "P1 SWEEP");
        LogTrace("  P1 Action: %s", isUppercut ? "Down + 2 (Uppercut - S + U)" : "Back + 4 (Sweep - Back + J)");
        LogTrace("  P2 Action: Hold Down (VK_DOWN) for %d frames (Custom Offset)", holdDuration);
        LogTrace("  P2 Injection Timing: %+df relative to Floor Impact (0x460D at +%df) -> Target: +%df",
                 relOffset, baseFloor, p2Offset);
        LogTrace("  P2 Release Timing:   +%df from Start (+%df post-hit)",
                 p2Offset + holdDuration, p2Offset + holdDuration - hitStartup);
        LogTrace("  Macro starts in 15 frames at Frame #%llu", g_SimTriggerFrame);
        LogTrace("====================================================================");
    }

    void TriggerMode14F1() { TriggerMode14FKey(1); }
    void TriggerMode14F2() { TriggerMode14FKey(2); }
    void TriggerMode14F3() { TriggerMode14FKey(3); }
    void TriggerMode14F4() { TriggerMode14FKey(4); }
    void TriggerMode14F5() { TriggerMode14FKey(5); }
    void TriggerMode14F6() { TriggerMode14FKey(6); }
    void TriggerMode14F7() { TriggerMode14FKey(7); }
    void TriggerMode14F8() { TriggerMode14FKey(8); }

    void TriggerMode15FKey(int fNum) {
        if (fNum < 1) fNum = 1;
        if (fNum > 4) fNum = 4;
        g_Mode15_ActiveFNum = fNum;

        const char* moveName = (fNum == 2) ? "FP ('T')" :
                               (fNum == 3) ? "BP ('U')" :
                               (fNum == 4) ? "FK ('G')" : "BK ('J')";

        static char s_Mode15Desc[128];
        snprintf(s_Mode15Desc, sizeof(s_Mode15Desc),
                 "Mode 15 F%d: P1 %s (2f) -> Hold Up 80f vs P2 Block 25f -> Hold Up 50f",
                 fNum, moveName);

        TriggerSequenceMacro(MACRO_SEQ_MODE15_BLOCKSTUN_DIAGNOSTIC, s_Mode15Desc);
    }

    void TriggerMode15F1() { TriggerMode15FKey(1); }
    void TriggerMode15F2() { TriggerMode15FKey(2); }
    void TriggerMode15F3() { TriggerMode15FKey(3); }
    void TriggerMode15F4() { TriggerMode15FKey(4); }

    void TriggerMode16FKey(int fNum) {
        if (fNum < 1) fNum = 1;
        if (fNum > 4) fNum = 4;
        g_Mode16_ActiveFNum = fNum;

        const char* p1AttackName = (fNum == 1) ? "FP ('T')" :
                                   (fNum == 2) ? "BP ('U')" :
                                   (fNum == 3) ? "FK ('G')" : "BK ('J')";
        static char s_Mode16Desc[128];
        snprintf(s_Mode16Desc, sizeof(s_Mode16Desc),
                 "Mode 16 F%d: P1 %s vs P2 Block (Distance & Pushback Measurement)",
                 fNum, p1AttackName);

        TriggerSequenceMacro(MACRO_SEQ_MODE16_UP_TAP_DIAGNOSTIC, s_Mode16Desc);
    }

    void TriggerMode16F1() { TriggerMode16FKey(1); }
    void TriggerMode16F2() { TriggerMode16FKey(2); }
    void TriggerMode16F3() { TriggerMode16FKey(3); }
    void TriggerMode16F4() { TriggerMode16FKey(4); }
    void TriggerMode16F5() { TriggerMode16FKey(5); }
    void TriggerMode16F6() { TriggerMode16FKey(6); }
    void TriggerMode16F7() { TriggerMode16FKey(7); }
    void TriggerMode16F8() { TriggerMode16FKey(8); }

    void TriggerMode17FKey(int fNum) {
        if (fNum < 1) fNum = 1;
        if (fNum > 4) fNum = 4;
        g_Mode17_ActiveFNum = fNum;

        const char* atkName = (fNum == 1) ? "FP ('T')" :
                              (fNum == 2) ? "BP ('U')" :
                              (fNum == 3) ? "FK ('G')" : "BK ('J')";

        static char s_Mode17Desc[128];
        snprintf(s_Mode17Desc, sizeof(s_Mode17Desc),
                 "Mode 17 F%d: P1 %s Attack Frame Data Measurement",
                 fNum, atkName);

        TriggerSequenceMacro(MACRO_SEQ_MODE17_ATTACK_MEASUREMENT, s_Mode17Desc);
    }

    void TriggerMode17F1() { TriggerMode17FKey(1); }
    void TriggerMode17F2() { TriggerMode17FKey(2); }
    void TriggerMode17F3() { TriggerMode17FKey(3); }
    void TriggerMode17F4() { TriggerMode17FKey(4); }

    void TriggerMode18FKey(int fNum) {
        if (fNum < 1) fNum = 1;
        if (fNum > 5) fNum = 5;
        g_Mode18_ActiveFNum = fNum;

        const char* cancelName = (fNum == 1) ? "b,b,1 (Spear)" :
                                 (fNum == 2) ? "b,b,1+BL (EX Spear)" :
                                 (fNum == 3) ? "Front Dash (F, F)" :
                                 (fNum == 4) ? "Backdash (B, B)" : "X-Ray (3+4+BL)";

        static char s_Mode18Desc[128];
        snprintf(s_Mode18Desc, sizeof(s_Mode18Desc),
                 "Mode 18 F%d: P1 FP ('T') into %s (%s)",
                 fNum, cancelName, g_Mode18_DefenderBlocks ? "Block" : "Hit");

        TriggerSequenceMacro(MACRO_SEQ_MODE18_NORMAL_CANCEL, s_Mode18Desc);
    }

    void TriggerMode18F1() { TriggerMode18FKey(1); }
    void TriggerMode18F2() { TriggerMode18FKey(2); }
    void TriggerMode18F3() { TriggerMode18FKey(3); }
    void TriggerMode18F4() { TriggerMode18FKey(4); }
    void TriggerMode18F5() { TriggerMode18FKey(5); }

    void TriggerMode19FKey(int fNum) {
        if (fNum < 1) fNum = 1;
        if (fNum > 2) fNum = 2;
        g_Mode19_ActiveFNum = fNum;
        g_Mode19_HitCount = 0;
        g_Mode19_Hit1Frame = 0;
        g_Mode19_Hit2Frame = 0;
        g_Mode19_Hit3Frame = 0;
        g_Mode19_Hit4Frame = 0;
        g_Mode19_TotalDamage = 0.0f;
        g_Mode19_NDCCancelDetected = false;
        g_Mode19_NDCCancelFrame = 0;

        const char* blockDesc = (g_Mode19_DefenderBlockMode == 0) ? "Clean Hit" :
                                (g_Mode19_DefenderBlockMode == 1) ? "Auto Block" : "Constant Block";
        const char* desc = (fNum == 1) ? "Mode 19 F1: Kabal F+4 -> F+4 Infinite Loop" :
                                         "Mode 19 F2: Kabal 1,1,1 -> NDC -> 1 Glitch Link";

        static char s_Mode19Desc[128];
        snprintf(s_Mode19Desc, sizeof(s_Mode19Desc), "%s (%s)", desc, blockDesc);
        TriggerSequenceMacro(MACRO_SEQ_MODE19_KABAL_BUG, s_Mode19Desc);
    }

    void TriggerMode19F1() { TriggerMode19FKey(1); }
    void TriggerMode19F2() { TriggerMode19FKey(2); }

    void TriggerMode20FKey(int fNum) {
        if (fNum < 1) fNum = 1;
        if (fNum > 4) fNum = 4;
        g_Mode20_ActiveFNum = fNum;

        const char* moveName = (fNum == 1) ? "D+1 (Down + FP / Crouching Jab: 'S + T')" :
                               (fNum == 2) ? "D+2 (Down + BP / Uppercut: 'S + U')" :
                               (fNum == 3) ? "D+3 (Down + FK / Crouching Kick: 'S + G')" :
                                             "D+4 (Down + BK / Crouching Poke: 'S + J')";

        static char s_Mode20Desc[128];
        snprintf(s_Mode20Desc, sizeof(s_Mode20Desc), "Mode 20 F%d: P1 %s", fNum, moveName);
        TriggerSequenceMacro(MACRO_SEQ_MODE20_CROUCHING_NORMAL, s_Mode20Desc);
    }

    void TriggerMode20F1() { TriggerMode20FKey(1); }
    void TriggerMode20F2() { TriggerMode20FKey(2); }
    void TriggerMode20F3() { TriggerMode20FKey(3); }
    void TriggerMode20F4() { TriggerMode20FKey(4); }

    // ========================================================================
    // Root-Level Button Press & Release State Query Hook (0x00446910)
    // Prototype: int __thiscall IsButtonPressed(void* pPlayerInfo, uint32_t tag)
    // Tags:
    //   0x80000001: 1 (FP) Press       0x8000001B: 1 (FP) Release (Negative Edge)
    //   0x80000002: 2 (BP) Press       0x8000001C: 2 (BP) Release (Negative Edge)
    //   0x80000003: 3 (FK) Press       0x8000001D: 3 (FK) Release (Negative Edge)
    //   0x80000004: 4 (BK) Press       0x8000001E: 4 (BK) Release (Negative Edge)
    //
    // When Negative Edge is disabled for a player, any query for release tags
    // (0x8000001B..0x8000001E) returns 0 (FALSE).
    // This suppresses release checks at the root of NetherRealm's command engine!
    // Normal downstroke attacks, strings, dial-a-combos, and specials are 100% unaffected.
    // ========================================================================
    // Match Buffered Move Hook (0x008A06F0)
    // Prototype: int __thiscall MatchBufferedMove(void* pChar, uint32_t p1, uint32_t p2, uint32_t p3, uint32_t p4, uint32_t p5)
    // ========================================================================
    typedef int (__fastcall *tMatchBufferedMove)(void* pChar, void* edxUnused, uint32_t p1, uint32_t p2, uint32_t p3, uint32_t p4);
    static tMatchBufferedMove oMatchBufferedMove = nullptr;

    static int __fastcall hkMatchBufferedMove(void* pChar, void* edxUnused, uint32_t p1, uint32_t p2, uint32_t p3, uint32_t p4) {
        if (!oMatchBufferedMove) return 0;
        int res = 0;
        __asm {
            push p4
            push p3
            push p2
            push p1
            mov ecx, pChar
            call oMatchBufferedMove
            mov res, eax
        }
        if (g_SimSeqType != MACRO_SEQ_STANDARD && res != 0) {
            if ((g_SimSeqType >= MACRO_SEQ_MODE8_F1_RAW_NEG_EDGE && g_SimSeqType <= MACRO_SEQ_MODE8_F8_STRING_BUFFER) ||
                (g_SimSeqType >= MACRO_SEQ_MODE9_F5_JC_FP_HOLD_DOWN_FP && g_SimSeqType <= MACRO_SEQ_MODE9_F8_JC_FP_HOLD_DOWN_FP)) {
                LogTrace("[MATCH_BUFFERED_MOVE | #%llu] P%d matched buffered move! p1=0x%08X, res=%d",
                         g_CurrentFrame, GetPlayerNumber(pChar), p1, res);
            }
        }
        return res;
    }

    // ========================================================================
    // Match Action / Buffered Motion Hook (0x008A1D10)
    // Prototype: int __thiscall MatchAction(void* pChar, uint32_t arg1, uint32_t* pOut1, uint32_t* pOut2, uint32_t* pOut3, uint32_t* pOut4)
    // ========================================================================
    typedef int (__fastcall *tMatchAction)(void* pChar, void* edxUnused, uint32_t arg1, uint32_t* pOut1, uint32_t* pOut2, uint32_t* pOut3, uint32_t* pOut4);
    static tMatchAction oMatchAction = nullptr;

    static int __fastcall hkMatchAction(void* pChar, void* edxUnused, uint32_t arg1, uint32_t* pOut1, uint32_t* pOut2, uint32_t* pOut3, uint32_t* pOut4) {
        if (!oMatchAction) return 0;
        int res = 0;
        __asm {
            push pOut4
            push pOut3
            push pOut2
            push pOut1
            push arg1
            mov ecx, pChar
            call oMatchAction
            mov res, eax
        }
        if (g_SimSeqType != MACRO_SEQ_STANDARD && res != 0) {
            if ((g_SimSeqType >= MACRO_SEQ_MODE8_F1_RAW_NEG_EDGE && g_SimSeqType <= MACRO_SEQ_MODE8_F8_STRING_BUFFER) ||
                (g_SimSeqType >= MACRO_SEQ_MODE9_F5_JC_FP_HOLD_DOWN_FP && g_SimSeqType <= MACRO_SEQ_MODE9_F8_JC_FP_HOLD_DOWN_FP)) {
                LogTrace("[MATCH_ACTION | #%llu] P%d matched action! res=%d (arg1=0x%08X)", g_CurrentFrame, GetPlayerNumber(pChar), res, arg1);
            }
        }
        return res;
    }



    // Hook: Combat_TickFightFrame (0x0080F850)
    typedef void(__cdecl* tTickFightFrame)(int* param_1);
    static tTickFightFrame oTickFightFrame = nullptr;
    static float s_CinematicSlowMoAccumulator = 0.0f;

    static void __cdecl hkTickFightFrame(int* param_1) {
        if (g_GamePaused.load()) {
            if (!g_StepOneFrame.exchange(false)) {
                if (IsDrawDebugPushboxes() || IsDrawDebugHurtboxes() || IsDrawDebugHitboxes() || ExtraGameplay::g_EnableFloorGrid) {
                    SafeFlushDebugPrimitives();
                    void* p1 = GetP1Char();
                    void* p2 = GetP2Char();
                    if (p1 && (IsDrawDebugPushboxes() || IsDrawDebugHurtboxes() || IsDrawDebugHitboxes())) RenderDebugCollisionPrimitives(p1, 1);
                    if (p2 && (IsDrawDebugPushboxes() || IsDrawDebugHurtboxes() || IsDrawDebugHitboxes())) RenderDebugCollisionPrimitives(p2, 2);
                    if (ExtraGameplay::g_EnableFloorGrid) ExtraGameplay::RenderFloorGrid();
                }
                return;
            }
        }

        // True Fight Simulation Slow-Motion during Dynamic Cinematic Camera
        if (FreeCamera::g_CinematicZoomActive && FreeCamera::g_CinematicSlowMoEnabled) {
            s_CinematicSlowMoAccumulator += FreeCamera::g_CinematicSlowMoRate;
            if (s_CinematicSlowMoAccumulator < 1.0f) {
                // Rendering loop & camera run at full 60 FPS while fight simulation runs in slow-mo!
                return;
            }
            s_CinematicSlowMoAccumulator -= 1.0f;
        } else {
            s_CinematicSlowMoAccumulator = 0.0f;
        }

        g_CurrentFrame++;

        if (IsCurrentMatchTag()) {
            if (oTickFightFrame) oTickFightFrame(param_1);
            return;
        }

        void* p1_char = GetP1Char();
        void* p2_char = GetP2Char();

        if (p1_char && (uintptr_t)p1_char > 0x01000000) {
            uint32_t act1 = *(uint32_t*)((uintptr_t)p1_char + 0x1D4);
            int f1 = *(int*)((uintptr_t)p1_char + 0x2F88);
            bool isIdleOrStun1 = (act1 == 0x0000 || act1 == 0x4600 || act1 == 0x4201 || (act1 >= 0x6200 && act1 <= 0x620B));
            if (isIdleOrStun1) {
                f1 = -1;
            }
            if (f1 != -1 && (g_P1_LastScriptFunc == -1 || f1 != g_P1_LastScriptFunc)) {
                g_P1_AttackStartFrame = g_CurrentFrame;
                g_P1_LastAttackStartFrame = g_CurrentFrame;
            } else if (f1 == -1) {
                if (g_P1_LastScriptFunc != -1) {
                    g_P1_LastAttackEndFrame = g_CurrentFrame;
                }
                g_P1_AttackStartFrame = 0;
            }
            g_P1_LastScriptFunc = f1;
        }

        if (p2_char && (uintptr_t)p2_char > 0x01000000) {
            uint32_t act2 = *(uint32_t*)((uintptr_t)p2_char + 0x1D4);
            int f2 = *(int*)((uintptr_t)p2_char + 0x2F88);
            bool isIdleOrStun2 = (act2 == 0x0000 || act2 == 0x4600 || act2 == 0x4201 || (act2 >= 0x6200 && act2 <= 0x620B));
            if (isIdleOrStun2) {
                f2 = -1;
            }
            if (f2 != -1 && (g_P2_LastScriptFunc == -1 || f2 != g_P2_LastScriptFunc)) {
                g_P2_AttackStartFrame = g_CurrentFrame;
                g_P2_LastAttackStartFrame = g_CurrentFrame;
            } else if (f2 == -1) {
                if (g_P2_LastScriptFunc != -1) {
                    g_P2_LastAttackEndFrame = g_CurrentFrame;
                }
                g_P2_AttackStartFrame = 0;
            }
            g_P2_LastScriptFunc = f2;
        }

        ProcessFighterLifecycleTick(p1_char, 1, g_P1_Lifecycle);
        ProcessFighterLifecycleTick(p2_char, 2, g_P2_Lifecycle);
        RecordFrameDataTick(p1_char, p2_char);

        JumpInputFix::OnFighterTick(p1_char);
        JumpInputFix::OnFighterTick(p2_char);

        CacheActiveFighterState(p1_char, 1);
        CacheActiveFighterState(p2_char, 2);

        // Enforce NetherRealm Native Release Gate [+0x944] every fight frame tick
        void* p1Info = nullptr;
        void* p2Info = nullptr;
        if (p1_char && (uintptr_t)p1_char > 0x01000000) {
            p1Info = *(void**)((uintptr_t)p1_char + 0x7638);
            if (p1Info && (uintptr_t)p1Info > 0x01000000) {
                if (StunModifier::IsInExtendedStun(1) && !StunModifier::IsPlayerInBlockstun(1)) {
                    *(uint32_t*)((uintptr_t)p1Info + 0x944) = 1;
                } else {
                    *(uint32_t*)((uintptr_t)p1Info + 0x944) = g_P1_NegativeEdgeEnabled ? 0 : 1;
                }
            }
        }
        if (p2_char && (uintptr_t)p2_char > 0x01000000) {
            p2Info = *(void**)((uintptr_t)p2_char + 0x7638);
            if (p2Info && (uintptr_t)p2Info > 0x01000000) {
                if (StunModifier::IsInExtendedStun(2) && !StunModifier::IsPlayerInBlockstun(2)) {
                    *(uint32_t*)((uintptr_t)p2Info + 0x944) = 1;
                } else {
                    *(uint32_t*)((uintptr_t)p2Info + 0x944) = g_P2_NegativeEdgeEnabled ? 0 : 1;
                }
            }
        }
        NegativeEdge::UpdatePlayerCache(p1_char, p2_char, p1Info, p2Info);

        {
            PROFILE_START();
            MeterDrainFix::OnTick(p1_char, p2_char, g_CurrentFrame);
            PROFILE_END(PROF_METER_DRAIN_FIX);
        }
        InputRecordingManager::Get().OnFrameTick(p1_char, p2_char);
        ReversalEngine::Get().OnFrameTick(p1_char, p2_char);
        InputIsolation::OnFrameTick(p1_char, p2_char);
        ExtraGameplay::OnFrameTick(p1_char, p2_char);
        JuggleDeterioration::OnFrameTick(p1_char, p2_char);
        AttackFrameModifier::OnFrameTick(p1_char, p2_char);
        StunModifier::UpdateBlockPushbackTick(1, p1_char);
        StunModifier::UpdateBlockPushbackTick(2, p2_char);

        // High-Resolution Macro Live Telemetry & Inspector
        uint64_t maxMacroWindow = (g_SimSeqType == MACRO_SEQ_MODE9_F1_FP || g_SimSeqType == MACRO_SEQ_MODE9_F2_FP_DASH || 
                                   g_SimSeqType == MACRO_SEQ_MODE9_F3_PROJECTILE_NORMAL || g_SimSeqType == MACRO_SEQ_MODE9_F4_PROJECTILE_EX || 
                                   (g_SimSeqType >= MACRO_SEQ_MODE9_F5_JC_FP_HOLD_DOWN_FP && g_SimSeqType <= MACRO_SEQ_MODE9_F8_JC_FP_HOLD_DOWN_FP) ||
                                   g_DiagnosticMode == 9) ? 100 : 
                                  ((g_SimSeqType == MACRO_SEQ_MODE18_NORMAL_CANCEL || g_DiagnosticMode == 18 || g_SimSeqType == MACRO_SEQ_MODE19_KABAL_BUG || g_DiagnosticMode == 19) ? 85 : 30);
        if (g_ActiveMacroTriggerFrame > 0 && 
            g_CurrentFrame >= g_ActiveMacroTriggerFrame - 1 && 
            g_CurrentFrame <= g_ActiveMacroTriggerFrame + maxMacroWindow) {
            
            void* p1 = GetP1Char();
            void* p2 = GetP2Char();
            
            int p1_f = (p1 && (uintptr_t)p1 > 0x01000000) ? *(int*)((uintptr_t)p1 + 0x2F88) : -1;
            uint32_t p1_atk = (p1 && (uintptr_t)p1 > 0x01000000) ? *(uint32_t*)((uintptr_t)p1 + 0x7A3C) : 0;
            float p1_x = (p1 && (uintptr_t)p1 > 0x01000000) ? *(float*)((uintptr_t)p1 + 0x1E8) : 0.0f;
            uint64_t p1_el = (g_P1_AttackStartFrame > 0 && g_CurrentFrame >= g_P1_AttackStartFrame) ? (g_CurrentFrame - g_P1_AttackStartFrame) : 0;
            
            int p2_f = (p2 && (uintptr_t)p2 > 0x01000000) ? *(int*)((uintptr_t)p2 + 0x2F88) : -1;
            uint32_t p2_atk = (p2 && (uintptr_t)p2 > 0x01000000) ? *(uint32_t*)((uintptr_t)p2 + 0x7A3C) : 0;
            float p2_x = (p2 && (uintptr_t)p2 > 0x01000000) ? *(float*)((uintptr_t)p2 + 0x1E8) : 0.0f;
            uint64_t p2_el = (g_P2_AttackStartFrame > 0 && g_CurrentFrame >= g_P2_AttackStartFrame) ? (g_CurrentFrame - g_P2_AttackStartFrame) : 0;

            float dist = fabsf(p1_x - p2_x);
            int relFrame = (int)(g_CurrentFrame - g_ActiveMacroTriggerFrame);

            uint32_t p1_sub = 0, p2_sub = 0;
            int32_t  p1_res = 0, p2_res = 0;
            void* p1_info = (p1 && (uintptr_t)p1 > 0x01000000) ? *(void**)((uintptr_t)p1 + 0x7638) : nullptr;
            void* p2_info = (p2 && (uintptr_t)p2 > 0x01000000) ? *(void**)((uintptr_t)p2 + 0x7638) : nullptr;
            if (p1_info && (uintptr_t)p1_info >= 0x00400000) {
                uint32_t curr = *(uint32_t*)((uintptr_t)p1_info + 0x574);
                if (curr == 0) curr = *(uint32_t*)((uintptr_t)p1_info + 0x570);
                if (curr > 0) {
                    uintptr_t pRec = (uintptr_t)p1_info + 0x48 + (curr - 1) * 0x2C;
                    p1_res = *(int32_t*)(pRec + 0x1C);
                    p1_sub = *(uint32_t*)(pRec + 0x24);
                }
            }
            if (p2_info && (uintptr_t)p2_info >= 0x00400000) {
                uint32_t curr = *(uint32_t*)((uintptr_t)p2_info + 0x574);
                if (curr == 0) curr = *(uint32_t*)((uintptr_t)p2_info + 0x570);
                if (curr > 0) {
                    uintptr_t pRec = (uintptr_t)p2_info + 0x48 + (curr - 1) * 0x2C;
                    p2_res = *(int32_t*)(pRec + 0x1C);
                    p2_sub = *(uint32_t*)(pRec + 0x24);
                }
            }

            LogTrace("[F%+03d | #%llu] P1: el=%02lluf func=%d atk=%u sub=%u res=%d | P2: el=%02lluf func=%d atk=%u sub=%u res=%d | dist=%.1f",
                     relFrame, g_CurrentFrame, p1_el, p1_f, p1_atk, p1_sub, p1_res,
                     p2_el, p2_f, p2_atk, p2_sub, p2_res, dist);

            // Detailed Frame-by-Frame Telemetry Inspector for Mode 9 (Recovery & Dash Diagnostics)
            if (g_SimSeqType == MACRO_SEQ_MODE9_F1_FP || g_SimSeqType == MACRO_SEQ_MODE9_F2_FP_DASH || 
                g_SimSeqType == MACRO_SEQ_MODE9_F3_PROJECTILE_NORMAL || g_SimSeqType == MACRO_SEQ_MODE9_F4_PROJECTILE_EX || 
                (g_SimSeqType >= MACRO_SEQ_MODE9_F5_JC_FP_HOLD_DOWN_FP && g_SimSeqType <= MACRO_SEQ_MODE9_F8_JC_FP_HOLD_DOWN_FP) ||
                g_DiagnosticMode == 9) {
                uint32_t p1_act = (p1 && (uintptr_t)p1 > 0x01000000) ? *(uint32_t*)((uintptr_t)p1 + 0x1D4) : 0;
                uint32_t p1_st = (p1 && (uintptr_t)p1 > 0x01000000) ? *(uint32_t*)((uintptr_t)p1 + 0x2E8) : 0;
                uint32_t p1_cmb = (p1 && (uintptr_t)p1 > 0x01000000) ? *(uint32_t*)((uintptr_t)p1 + 0x2EC) : 0;
                uint32_t p1_recAtk = 0;
                if (p1_info && (uintptr_t)p1_info >= 0x00400000) {
                    uint32_t curr = *(uint32_t*)((uintptr_t)p1_info + 0x574);
                    if (curr == 0) curr = *(uint32_t*)((uintptr_t)p1_info + 0x570);
                    if (curr > 0) {
                        uintptr_t pRec = (uintptr_t)p1_info + 0x48 + (curr - 1) * 0x2C;
                        p1_recAtk = *(uint32_t*)(pRec + 0x20);
                    }
                }
                uint32_t p2_act = (p2 && (uintptr_t)p2 > 0x01000000) ? *(uint32_t*)((uintptr_t)p2 + 0x1D4) : 0;
                uint32_t p2_lock = (p2 && (uintptr_t)p2 > 0x01000000) ? *(uint32_t*)((uintptr_t)p2 + 0xAAB0) : 0;
                int p2_rx = (p2 && (uintptr_t)p2 > 0x01000000) ? *(int*)((uintptr_t)p2 + 0x2FB4) : -1;

                uint32_t p1_colVols = 0;
                void* pColInfo = (p1 && (uintptr_t)p1 > 0x01000000) ? *(void**)((uintptr_t)p1 + 0x2F10) : nullptr;
                if (pColInfo && (uintptr_t)pColInfo > 0x01000000) {
                    __try {
                        uint32_t* pVolList = *(uint32_t**)((uintptr_t)pColInfo + 0x14);
                        if (pVolList && (uintptr_t)pVolList > 0x01000000) p1_colVols = pVolList[0];
                    } __except (EXCEPTION_EXECUTE_HANDLER) {}
                }
                int p1_child = (p1 && (uintptr_t)p1 > 0x01000000) ? *(int*)((uintptr_t)p1 + 0x68) : 0;
                void* p1_prop = (p1 && (uintptr_t)p1 > 0x01000000) ? *(void**)((uintptr_t)p1 + 0x2F1C) : nullptr;

                LogTrace("[MODE 9 TRACE | F%+03d | #%llu] P1: act=0x%04X, func=%d, sub=%u, res=%d, recAtk=%u, vol=%u, child=%d, prop=%p | P2: act=0x%04X, func=%d, lock=%u, rx=%d",
                         relFrame, g_CurrentFrame, p1_act, p1_f, p1_sub, p1_res, p1_recAtk, p1_colVols, p1_child, p1_prop,
                         p2_act, p2_f, p2_lock, p2_rx);
            }

            // Detailed Input Structure & Release Probe for Mode 8
            if (g_SimSeqType >= MACRO_SEQ_MODE8_F1_RAW_NEG_EDGE && g_SimSeqType <= MACRO_SEQ_MODE8_F8_STRING_BUFFER && relFrame >= 14 && relFrame <= 30) {
                void* pProbeActor = (g_SimSeqType >= MACRO_SEQ_MODE8_F5_P2_RAW_NEG_EDGE && g_SimSeqType <= MACRO_SEQ_MODE8_F7_P2_HIT_CONFIRM_CANCEL) ? p2 : p1;
                int pNum = (pProbeActor == p2) ? 2 : 1;
                if (pProbeActor && (uintptr_t)pProbeActor > 0x01000000) {
                    uintptr_t base = (uintptr_t)pProbeActor;
                    uint32_t b78B0 = *(uint32_t*)(base + 0x78B0);
                    uint32_t b78B4 = *(uint32_t*)(base + 0x78B4);
                    uint32_t b78B8 = *(uint32_t*)(base + 0x78B8);
                    uint32_t b78BC = *(uint32_t*)(base + 0x78BC);
                    uint32_t b78C0 = *(uint32_t*)(base + 0x78C0);
                    uint32_t b78C4 = *(uint32_t*)(base + 0x78C4); // Current btnMask
                    uint32_t b78C8 = *(uint32_t*)(base + 0x78C8);
                    uint32_t b78CC = *(uint32_t*)(base + 0x78CC);
                    uint32_t b78D0 = *(uint32_t*)(base + 0x78D0);
                    uint32_t b78D4 = *(uint32_t*)(base + 0x78D4);
                    uint32_t b78D8 = *(uint32_t*)(base + 0x78D8);
                    uint32_t b78DC = *(uint32_t*)(base + 0x78DC);
                    uint32_t b78E0 = *(uint32_t*)(base + 0x78E0);
                    uint32_t b78E4 = *(uint32_t*)(base + 0x78E4);
                    uint32_t b78E8 = *(uint32_t*)(base + 0x78E8);
                    uint32_t b78EC = *(uint32_t*)(base + 0x78EC);
                    uint32_t b78F0 = *(uint32_t*)(base + 0x78F0);
                    uint32_t b78F4 = *(uint32_t*)(base + 0x78F4);
                    uint32_t b78F8 = *(uint32_t*)(base + 0x78F8);
                    uint32_t b78FC = *(uint32_t*)(base + 0x78FC);

                    LogTrace("[INPUT PROBE P%d | F%+02d | #%llu] 78B0: %08X %08X %08X %08X | 78C0: %08X [Mask:%08X] %08X %08X",
                             pNum, relFrame, g_CurrentFrame, b78B0, b78B4, b78B8, b78BC, b78C0, b78C4, b78C8, b78CC);
                    LogTrace("   -> 78D0: %08X %08X %08X %08X | 78E0: %08X %08X %08X %08X | 78F0: %08X %08X %08X %08X",
                             b78D0, b78D4, b78D8, b78DC, b78E0, b78E4, b78E8, b78EC, b78F0, b78F4, b78F8, b78FC);
                }
            }
        }

        // Dynamically resolve horizontal directions based on live player positions (Arena Side Swap Fix)
        auto ResolvePlayerDir = [](int player, WORD dir) -> WORD {
            if (dir == 0 || dir == 'W' || dir == 'S' || dir == VK_UP || dir == VK_DOWN) {
                return dir;
            }
            void* p1 = GetP1Char();
            void* p2 = GetP2Char();
            if (p1 && p2 && (uintptr_t)p1 > 0x01000000 && (uintptr_t)p2 > 0x01000000) {
                __try {
                    float p1_x = *(float*)((uintptr_t)p1 + 0x1E8);
                    float p2_x = *(float*)((uintptr_t)p2 + 0x1E8);
                    bool swapped = (p1_x > p2_x); // Standard is P1 on left (p1_x < p2_x)
                    if (player == 1) {
                        if (swapped) {
                            if (dir == 'A') return 'D';
                            if (dir == 'D') return 'A';
                        }
                    } else if (player == 2) {
                        if (swapped) {
                            if (dir == VK_RIGHT) return VK_LEFT;
                            if (dir == VK_LEFT)  return VK_RIGHT;
                        }
                    }
                } __except (EXCEPTION_EXECUTE_HANDLER) {}
            }
            return dir;
        };

        // Automated Input Macro Injection (Available both with Trade Fix ON or OFF)
        if (g_SimTriggerFrame > 0) {

            if (g_SimSeqType == MACRO_SEQ_P2_WALK_THEN_ATTACK) {
                WORD p2_walk = ResolvePlayerDir(2, VK_LEFT);
                if (g_CurrentFrame == g_SimTriggerFrame) {
                    SimulateKeyDown('T');
                    SimulateKeyDown(p2_walk);
                    LogTrace("[TEST BOT] P1 FP (T) & P2 Left (15f walk) pressed at frame %llu", g_CurrentFrame);
                }
                if (g_CurrentFrame == g_SimTriggerFrame + 2) {
                    SimulateKeyUp('T');
                    LogTrace("[TEST BOT] P1 T released at frame %llu (P2 still walking Left...)", g_CurrentFrame);
                }
                if (g_CurrentFrame == g_SimTriggerFrame + 15) {
                    SimulateKeyUp(p2_walk);
                    SimulateKeyUp(VK_LEFT);
                    SimulateKeyUp(VK_RIGHT);
                    LogTrace("[TEST BOT] P2 Left released after 15f at frame %llu (1 frame delay...)", g_CurrentFrame);
                }
                if (g_CurrentFrame == g_SimTriggerFrame + 16) {
                    SimulateKeyDown(VK_NUMPAD7);
                    LogTrace("[TEST BOT] P2 FP (NumPad 7) pressed at frame %llu", g_CurrentFrame);
                }
                if (g_CurrentFrame == g_SimTriggerFrame + 18) {
                    ReleaseAllSimulationKeys();
                    g_SimTriggerFrame = 0;
                    g_SimSeqType = MACRO_SEQ_STANDARD;
                    LogTrace("[TEST BOT] P2 FP (NumPad 7) released at frame %llu", g_CurrentFrame);
                }
            } else if (g_SimSeqType == MACRO_SEQ_P1_WALK_THEN_ATTACK) {
                WORD p1_walk = ResolvePlayerDir(1, 'D');
                if (g_CurrentFrame == g_SimTriggerFrame) {
                    SimulateKeyDown(VK_NUMPAD7);
                    SimulateKeyDown(p1_walk);
                    LogTrace("[TEST BOT] P2 FP (Num7) & P1 D (15f walk) pressed at frame %llu", g_CurrentFrame);
                }
                if (g_CurrentFrame == g_SimTriggerFrame + 2) {
                    SimulateKeyUp(VK_NUMPAD7);
                    LogTrace("[TEST BOT] P2 Num7 released at frame %llu (P1 still walking D...)", g_CurrentFrame);
                }
                if (g_CurrentFrame == g_SimTriggerFrame + 15) {
                    SimulateKeyUp(p1_walk);
                    SimulateKeyUp('A');
                    SimulateKeyUp('D');
                    LogTrace("[TEST BOT] P1 D released after 15f at frame %llu (1 frame delay...)", g_CurrentFrame);
                }
                if (g_CurrentFrame == g_SimTriggerFrame + 16) {
                    SimulateKeyDown('T');
                    LogTrace("[TEST BOT] P1 FP (T) pressed at frame %llu", g_CurrentFrame);
                }
                if (g_CurrentFrame == g_SimTriggerFrame + 18) {
                    ReleaseAllSimulationKeys();
                    g_SimTriggerFrame = 0;
                    g_SimSeqType = MACRO_SEQ_STANDARD;
                    LogTrace("[TEST BOT] P1 FP (T) released at frame %llu", g_CurrentFrame);
                }
            } else if (g_SimSeqType == MACRO_SEQ_GENERIC_SPECIAL) {
                int s1Hold = (g_SimSpecialSeq.step1HoldFrames > 0) ? g_SimSpecialSeq.step1HoldFrames : 2;
                int s1Delay = (g_SimSpecialSeq.step1NeutralDelay > 0) ? g_SimSpecialSeq.step1NeutralDelay : 2;
                int s2Start = s1Hold + s1Delay;

                if (g_CurrentFrame == g_SimTriggerFrame) {
                    if (g_SimSpecialSeq.p1_dirs[0]) SimulateKeyDown(ResolvePlayerDir(1, g_SimSpecialSeq.p1_dirs[0]));
                    if (g_SimSpecialSeq.p2_dirs[0]) SimulateKeyDown(ResolvePlayerDir(2, g_SimSpecialSeq.p2_dirs[0]));
                    LogTrace("[TEST BOT] Special step 1 (tap) pressed at frame %llu (hold: %df)", g_CurrentFrame, s1Hold);
                }
                if (g_CurrentFrame == g_SimTriggerFrame + s1Hold) {
                    if (g_SimSpecialSeq.p1_dirs[0]) {
                        SimulateKeyUp(g_SimSpecialSeq.p1_dirs[0]);
                        SimulateKeyUp('A');
                        SimulateKeyUp('D');
                    }
                    if (g_SimSpecialSeq.p2_dirs[0]) {
                        SimulateKeyUp(g_SimSpecialSeq.p2_dirs[0]);
                        SimulateKeyUp(VK_LEFT);
                        SimulateKeyUp(VK_RIGHT);
                    }
                    LogTrace("[TEST BOT] Special step 1 released at frame %llu (neutral delay: %df)", g_CurrentFrame, s1Delay);
                }

                if (g_SimSpecialSeq.stepCount == 2) {
                    if (g_CurrentFrame == g_SimTriggerFrame + s2Start) {
                        if (g_SimSpecialSeq.p1_dirs[1]) SimulateKeyDown(ResolvePlayerDir(1, g_SimSpecialSeq.p1_dirs[1]));
                        if (g_SimSpecialSeq.p1_btn)     SimulateKeyDown(g_SimSpecialSeq.p1_btn);
                        if (g_SimSpecialSeq.p2_dirs[1]) SimulateKeyDown(ResolvePlayerDir(2, g_SimSpecialSeq.p2_dirs[1]));
                        if (g_SimSpecialSeq.p2_btn)     SimulateKeyDown(g_SimSpecialSeq.p2_btn);
                        LogTrace("[TEST BOT] Special step 2 (chord) pressed at frame %llu", g_CurrentFrame);
                    }
                    if (g_CurrentFrame == g_SimTriggerFrame + s2Start + 2) {
                        ReleaseAllSimulationKeys();
                        g_SimTriggerFrame = 0;
                        g_SimSeqType = MACRO_SEQ_STANDARD;
                    }
                } else if (g_SimSpecialSeq.stepCount == 3) {
                    if (g_CurrentFrame == g_SimTriggerFrame + s2Start) {
                        if (g_SimSpecialSeq.p1_dirs[1]) SimulateKeyDown(ResolvePlayerDir(1, g_SimSpecialSeq.p1_dirs[1]));
                        if (g_SimSpecialSeq.p2_dirs[1]) SimulateKeyDown(ResolvePlayerDir(2, g_SimSpecialSeq.p2_dirs[1]));
                        LogTrace("[TEST BOT] Special step 2 (tap) pressed at frame %llu", g_CurrentFrame);
                    }
                    if (g_CurrentFrame == g_SimTriggerFrame + s2Start + 2) {
                        if (g_SimSpecialSeq.p1_dirs[1]) {
                            SimulateKeyUp(g_SimSpecialSeq.p1_dirs[1]);
                            SimulateKeyUp('A');
                            SimulateKeyUp('D');
                        }
                        if (g_SimSpecialSeq.p2_dirs[1]) {
                            SimulateKeyUp(g_SimSpecialSeq.p2_dirs[1]);
                            SimulateKeyUp(VK_LEFT);
                            SimulateKeyUp(VK_RIGHT);
                        }
                    }
                    if (g_CurrentFrame == g_SimTriggerFrame + s2Start + 4) {
                        if (g_SimSpecialSeq.p1_dirs[2]) SimulateKeyDown(ResolvePlayerDir(1, g_SimSpecialSeq.p1_dirs[2]));
                        if (g_SimSpecialSeq.p1_btn)     SimulateKeyDown(g_SimSpecialSeq.p1_btn);
                        if (g_SimSpecialSeq.p2_dirs[2]) SimulateKeyDown(ResolvePlayerDir(2, g_SimSpecialSeq.p2_dirs[2]));
                        if (g_SimSpecialSeq.p2_btn)     SimulateKeyDown(g_SimSpecialSeq.p2_btn);
                        LogTrace("[TEST BOT] Special step 3 (chord) pressed at frame %llu", g_CurrentFrame);
                    }
                    if (g_CurrentFrame == g_SimTriggerFrame + s2Start + 6) {
                        ReleaseAllSimulationKeys();
                        g_SimTriggerFrame = 0;
                        g_SimSeqType = MACRO_SEQ_STANDARD;
                    }
                }
            } else if (g_SimSeqType == MACRO_SEQ_P2_SPEC_VS_P1_NORM) {
                if (g_CurrentFrame == g_SimTriggerFrame) {
                    if (g_SimMatchup.spec_d1) SimulateKeyDown(ResolvePlayerDir(2, g_SimMatchup.spec_d1));
                    if (g_SimMatchup.norm_delay == 0) {
                        if (g_SimMatchup.norm_dir) SimulateKeyDown(ResolvePlayerDir(1, g_SimMatchup.norm_dir));
                        if (g_SimMatchup.norm_btn) SimulateKeyDown(g_SimMatchup.norm_btn);
                    }
                    LogTrace("[TEST BOT] P2 Special step 1 & P1 Normal (delay %df) triggered", g_SimMatchup.norm_delay);
                }
                if (g_CurrentFrame == g_SimTriggerFrame + 2) {
                    if (g_SimMatchup.spec_d1) {
                        SimulateKeyUp(g_SimMatchup.spec_d1);
                        SimulateKeyUp(VK_LEFT);
                        SimulateKeyUp(VK_RIGHT);
                    }
                }
                if (g_CurrentFrame == g_SimTriggerFrame + 4) {
                    if (g_SimMatchup.spec_d2)  SimulateKeyDown(ResolvePlayerDir(2, g_SimMatchup.spec_d2));
                    if (g_SimMatchup.spec_btn) SimulateKeyDown(g_SimMatchup.spec_btn);
                }
                if (g_SimMatchup.norm_delay > 0) {
                    if (g_CurrentFrame == g_SimTriggerFrame + g_SimMatchup.norm_delay - 2) {
                        if (g_SimMatchup.norm_dir) SimulateKeyDown(ResolvePlayerDir(1, g_SimMatchup.norm_dir));
                    }
                    if (g_CurrentFrame == g_SimTriggerFrame + g_SimMatchup.norm_delay) {
                        if (g_SimMatchup.norm_dir) SimulateKeyDown(ResolvePlayerDir(1, g_SimMatchup.norm_dir));
                        if (g_SimMatchup.norm_btn) SimulateKeyDown(g_SimMatchup.norm_btn);
                    }
                    if (g_CurrentFrame == g_SimTriggerFrame + g_SimMatchup.norm_delay + 4) {
                        if (g_SimMatchup.norm_dir) {
                            SimulateKeyUp(g_SimMatchup.norm_dir);
                            SimulateKeyUp('A');
                            SimulateKeyUp('D');
                        }
                        if (g_SimMatchup.norm_btn) SimulateKeyUp(g_SimMatchup.norm_btn);
                    }
                } else {
                    if (g_CurrentFrame == g_SimTriggerFrame + 4) {
                        if (g_SimMatchup.norm_dir) {
                            SimulateKeyUp(g_SimMatchup.norm_dir);
                            SimulateKeyUp('A');
                            SimulateKeyUp('D');
                        }
                        if (g_SimMatchup.norm_btn) SimulateKeyUp(g_SimMatchup.norm_btn);
                    }
                }
                if (g_CurrentFrame == g_SimTriggerFrame + 10) {
                    ReleaseAllSimulationKeys();
                    g_SimTriggerFrame = 0;
                    g_SimSeqType = MACRO_SEQ_STANDARD;
                }
            } else if (g_SimSeqType == MACRO_SEQ_P1_SPEC_VS_P2_NORM) {
                if (g_CurrentFrame == g_SimTriggerFrame) {
                    if (g_SimMatchup.spec_d1) SimulateKeyDown(ResolvePlayerDir(1, g_SimMatchup.spec_d1));
                    if (g_SimMatchup.norm_delay == 0) {
                        if (g_SimMatchup.norm_dir) SimulateKeyDown(ResolvePlayerDir(2, g_SimMatchup.norm_dir));
                        if (g_SimMatchup.norm_btn) SimulateKeyDown(g_SimMatchup.norm_btn);
                    }
                    LogTrace("[TEST BOT] P1 Special step 1 & P2 Normal (delay %df) triggered", g_SimMatchup.norm_delay);
                }
                if (g_CurrentFrame == g_SimTriggerFrame + 2) {
                    if (g_SimMatchup.spec_d1) {
                        SimulateKeyUp(g_SimMatchup.spec_d1);
                        SimulateKeyUp('A');
                        SimulateKeyUp('D');
                    }
                }
                if (g_CurrentFrame == g_SimTriggerFrame + 4) {
                    if (g_SimMatchup.spec_d2)  SimulateKeyDown(ResolvePlayerDir(1, g_SimMatchup.spec_d2));
                    if (g_SimMatchup.spec_btn) SimulateKeyDown(g_SimMatchup.spec_btn);
                }
                if (g_SimMatchup.norm_delay > 0) {
                    if (g_CurrentFrame == g_SimTriggerFrame + g_SimMatchup.norm_delay - 2) {
                        if (g_SimMatchup.norm_dir) SimulateKeyDown(ResolvePlayerDir(2, g_SimMatchup.norm_dir));
                    }
                    if (g_CurrentFrame == g_SimTriggerFrame + g_SimMatchup.norm_delay) {
                        if (g_SimMatchup.norm_dir) SimulateKeyDown(ResolvePlayerDir(2, g_SimMatchup.norm_dir));
                        if (g_SimMatchup.norm_btn) SimulateKeyDown(g_SimMatchup.norm_btn);
                    }
                    if (g_CurrentFrame == g_SimTriggerFrame + g_SimMatchup.norm_delay + 4) {
                        if (g_SimMatchup.norm_dir) {
                            SimulateKeyUp(g_SimMatchup.norm_dir);
                            SimulateKeyUp(VK_LEFT);
                            SimulateKeyUp(VK_RIGHT);
                        }
                        if (g_SimMatchup.norm_btn) SimulateKeyUp(g_SimMatchup.norm_btn);
                    }
                } else {
                    if (g_CurrentFrame == g_SimTriggerFrame + 4) {
                        if (g_SimMatchup.norm_dir) {
                            SimulateKeyUp(g_SimMatchup.norm_dir);
                            SimulateKeyUp(VK_LEFT);
                            SimulateKeyUp(VK_RIGHT);
                        }
                        if (g_SimMatchup.norm_btn) SimulateKeyUp(g_SimMatchup.norm_btn);
                    }
                }
                if (g_CurrentFrame == g_SimTriggerFrame + 10) {
                    ReleaseAllSimulationKeys();
                    g_SimTriggerFrame = 0;
                    g_SimSeqType = MACRO_SEQ_STANDARD;
                }
            } else if (g_SimSeqType == MACRO_SEQ_JUMP_IN_VS_GROUND) {
                if (g_CurrentFrame == g_SimTriggerFrame) {
                    SimulateKeyDown('W');
                    SimulateKeyDown('D');
                    LogTrace("[TEST BOT] P1 forward jump triggered");
                }
                if (g_CurrentFrame == g_SimTriggerFrame + 6) {
                    SimulateKeyUp('W');
                    SimulateKeyUp('D');
                }
                if (g_CurrentFrame == g_SimTriggerFrame + 22) {
                    SimulateKeyDown('T');
                    LogTrace("[TEST BOT] P1 Jump-In punch pressed (descent)");
                }
                if (g_CurrentFrame == g_SimTriggerFrame + 24) {
                    if (g_SimMatchup.isAntiAirD2) {
                        SimulateKeyDown(VK_DOWN);
                    }
                }
                if (g_CurrentFrame == g_SimTriggerFrame + 26) {
                    if (g_SimMatchup.isAntiAirD2) {
                        SimulateKeyDown(VK_DOWN);
                        SimulateKeyDown(VK_NUMPAD9);
                        LogTrace("[TEST BOT] P2 D+2 anti-air Uppercut pressed on incoming jump");
                    } else {
                        SimulateKeyDown(VK_NUMPAD7);
                        LogTrace("[TEST BOT] P2 Standing Normal 1 pressed");
                    }
                }
                if (g_CurrentFrame == g_SimTriggerFrame + 32) {
                    SimulateKeyUp('T');
                    if (g_SimMatchup.isAntiAirD2) {
                        SimulateKeyUp(VK_DOWN);
                        SimulateKeyUp(VK_NUMPAD9);
                    } else {
                        SimulateKeyUp(VK_NUMPAD7);
                    }
                    g_SimTriggerFrame = 0;
                    g_SimSeqType = MACRO_SEQ_STANDARD;
                }
            } else if (g_SimSeqType == MACRO_SEQ_MODE8_F1_RAW_NEG_EDGE) {
                WORD p1_back = 'A';
                void* p1 = GetP1Char();
                void* p2 = GetP2Char();
                if (p1 && p2 && (uintptr_t)p1 > 0x01000000 && (uintptr_t)p2 > 0x01000000) {
                    __try {
                        float p1_x = *(float*)((uintptr_t)p1 + 0x1E8);
                        float p2_x = *(float*)((uintptr_t)p2 + 0x1E8);
                        if (p1_x > p2_x) { p1_back = 'D'; }
                    } __except (EXCEPTION_EXECUTE_HANDLER) {}
                }

                // Frame 001: Press & Hold [1] ('T')
                if (g_CurrentFrame == g_SimTriggerFrame) {
                    SimulateKeyDown('T');
                    LogTrace("[MODE 8 F1 | #%llu] Frame 01: Hold [1] ('T')", g_CurrentFrame);
                }
                // Frames 001 - 026: Standing 1 Jab (11f startup, 3f active, 10f recovery = 24f total) finishes
                // Frame 028 - 029: First [Back] tap (2f) while 1 remains held in neutral
                if (g_CurrentFrame == g_SimTriggerFrame + 27) {
                    SimulateKeyDown(p1_back);
                    LogTrace("[MODE 8 F1 | #%llu] Frame 28: Tap [Back] from Neutral (2f hold)", g_CurrentFrame);
                }
                // Frame 030 - 031: Stick neutral (2f)
                if (g_CurrentFrame == g_SimTriggerFrame + 29) {
                    SimulateKeyUp(p1_back);
                    LogTrace("[MODE 8 F1 | #%llu] Frame 30: Stick Neutral (2f)", g_CurrentFrame);
                }
                // Frame 032 - 033: Second [Back] press + RELEASE [1] ('T') SIMULTANEOUSLY on the exact same tick!
                // Synchronizing 2nd Back with Release 1 matches Trigger2StepMacro and gives Spear priority over backdash!
                if (g_CurrentFrame == g_SimTriggerFrame + 31) {
                    SimulateKeyDown(p1_back);
                    SimulateKeyUp('T');
                    LogTrace("[MODE 8 F1 | #%llu] Frame 32: Press [Back] + RELEASE [1] ('T') (Negative Edge Trigger)", g_CurrentFrame);
                }
                // Frame 034: Release [Back]
                if (g_CurrentFrame == g_SimTriggerFrame + 33) {
                    SimulateKeyUp(p1_back);
                    LogTrace("[MODE 8 F1 | #%llu] Frame 34: Release [Back]", g_CurrentFrame);
                }
                // Frame 055: End of observation
                if (g_CurrentFrame >= g_SimTriggerFrame + 54) {
                    SimulateKeyUp('T');
                    SimulateKeyUp('A');
                    SimulateKeyUp('D');
                    g_SimTriggerFrame = 0;
                    g_SimSeqType = MACRO_SEQ_STANDARD;
                    LogTrace("[MODE 8 F1 | #%llu] Frame 55: End of observation window", g_CurrentFrame);
                }
            } else if (g_SimSeqType == MACRO_SEQ_MODE8_F2_NORMAL_SPECIAL) {
                WORD p1_back = 'A';
                void* p1 = GetP1Char();
                void* p2 = GetP2Char();
                if (p1 && p2 && (uintptr_t)p1 > 0x01000000 && (uintptr_t)p2 > 0x01000000) {
                    __try {
                        float p1_x = *(float*)((uintptr_t)p1 + 0x1E8);
                        float p2_x = *(float*)((uintptr_t)p2 + 0x1E8);
                        if (p1_x > p2_x) { p1_back = 'D'; }
                    } __except (EXCEPTION_EXECUTE_HANDLER) {}
                }

                // Frame 001 - 002: First [Back] tap (2f)
                if (g_CurrentFrame == g_SimTriggerFrame) {
                    SimulateKeyDown(p1_back);
                    LogTrace("[MODE 8 F2 | #%llu] Frame 01: Press [Back] (2f)", g_CurrentFrame);
                }
                // Frame 003 - 004: Neutral (2f)
                if (g_CurrentFrame == g_SimTriggerFrame + 2) {
                    SimulateKeyUp(p1_back);
                    LogTrace("[MODE 8 F2 | #%llu] Frame 03: Stick Neutral (2f)", g_CurrentFrame);
                }
                // Frame 005 - 006: Second [Back] press + Press [1] ('T') simultaneously (Downstroke special execution)
                if (g_CurrentFrame == g_SimTriggerFrame + 4) {
                    SimulateKeyDown(p1_back);
                    SimulateKeyDown('T');
                    LogTrace("[MODE 8 F2 | #%llu] Frame 05: Press [Back] + Press [1] (Downstroke Attack)", g_CurrentFrame);
                }
                // Frame 007: Release both
                if (g_CurrentFrame == g_SimTriggerFrame + 6) {
                    SimulateKeyUp('T');
                    SimulateKeyUp(p1_back);
                    LogTrace("[MODE 8 F2 | #%llu] Frame 07: Release [Back] & [1]", g_CurrentFrame);
                }
                // Frame 030: End of observation
                if (g_CurrentFrame >= g_SimTriggerFrame + 29) {
                    SimulateKeyUp('T');
                    SimulateKeyUp('A');
                    SimulateKeyUp('D');
                    g_SimTriggerFrame = 0;
                    g_SimSeqType = MACRO_SEQ_STANDARD;
                    LogTrace("[MODE 8 F2 | #%llu] Frame 30: End of observation window", g_CurrentFrame);
                }
            } else if (g_SimSeqType == MACRO_SEQ_MODE8_F3_HIT_CONFIRM_CANCEL) {
                WORD p1_back = 'A';
                WORD p1_fwd  = 'D';
                void* p1 = GetP1Char();
                void* p2 = GetP2Char();
                if (p1 && p2 && (uintptr_t)p1 > 0x01000000 && (uintptr_t)p2 > 0x01000000) {
                    __try {
                        float p1_x = *(float*)((uintptr_t)p1 + 0x1E8);
                        float p2_x = *(float*)((uintptr_t)p2 + 0x1E8);
                        if (p1_x > p2_x) { p1_back = 'D'; p1_fwd = 'A'; }
                    } __except (EXCEPTION_EXECUTE_HANDLER) {}
                }

                // Frame 001 - 012: Ensure close contact range (Walk forward if needed)
                if (g_CurrentFrame == g_SimTriggerFrame) {
                    void* p1_cur = GetP1Char();
                    void* p2_cur = GetP2Char();
                    if (p1_cur && p2_cur) {
                        __try {
                            float d = fabsf(*(float*)((uintptr_t)p1_cur + 0x1E8) - *(float*)((uintptr_t)p2_cur + 0x1E8));
                            if (d > 95.0f) {
                                SimulateKeyDown(p1_fwd);
                                LogTrace("[MODE 8 F3 | #%llu] Frame 01: Closing distance (dist=%.1f)...", g_CurrentFrame, d);
                            }
                        } __except (EXCEPTION_EXECUTE_HANDLER) {}
                    }
                }
                if (g_CurrentFrame == g_SimTriggerFrame + 12) {
                    SimulateKeyUp(p1_fwd);
                }

                // Frame 015: Press [1] ('T') to start Standing 1 Jab (11f startup)
                if (g_CurrentFrame == g_SimTriggerFrame + 14) {
                    SimulateKeyDown('T');
                    LogTrace("[MODE 8 F3 | #%llu] Frame 15: Standing 1 Jab START (Hold [1])", g_CurrentFrame);
                }
                // Frame 019: First [Back] tap during Standing 1 startup
                if (g_CurrentFrame == g_SimTriggerFrame + 18) {
                    SimulateKeyDown(p1_back);
                    LogTrace("[MODE 8 F3 | #%llu] Frame 19: Buffer [Back] during startup", g_CurrentFrame);
                }
                // Frame 022: Stick neutral
                if (g_CurrentFrame == g_SimTriggerFrame + 21) {
                    SimulateKeyUp(p1_back);
                    LogTrace("[MODE 8 F3 | #%llu] Frame 22: Neutral during startup", g_CurrentFrame);
                }
                // Frame 024: Second [Back] press & hold
                if (g_CurrentFrame == g_SimTriggerFrame + 23) {
                    SimulateKeyDown(p1_back);
                    LogTrace("[MODE 8 F3 | #%llu] Frame 24: Buffer 2nd [Back] (Hold)", g_CurrentFrame);
                }
                // Frame 027: (Frame 12 after Jab start -> Hit contact & hitstop active!) RELEASE [1] while [Back] held!
                if (g_CurrentFrame == g_SimTriggerFrame + 26) {
                    SimulateKeyUp('T');
                    LogTrace("[MODE 8 F3 | #%llu] Frame 27: RELEASE [1] in Hitstop / Cancel Window! (Negative Edge Cancel)", g_CurrentFrame);
                }
                // Frame 032: Release [Back]
                if (g_CurrentFrame == g_SimTriggerFrame + 31) {
                    SimulateKeyUp(p1_back);
                    LogTrace("[MODE 8 F3 | #%llu] Frame 32: Release [Back]", g_CurrentFrame);
                }
                // Frame 050: End
                if (g_CurrentFrame >= g_SimTriggerFrame + 49) {
                    SimulateKeyUp('T');
                    SimulateKeyUp('A');
                    SimulateKeyUp('D');
                    g_SimTriggerFrame = 0;
                    g_SimSeqType = MACRO_SEQ_STANDARD;
                    LogTrace("[MODE 8 F3 | #%llu] Frame 50: End of observation window", g_CurrentFrame);
                }
            } else if (g_SimSeqType == MACRO_SEQ_MODE8_F4_WHIFF_NEG_EDGE) {
                WORD p1_back = 'A';
                WORD p1_fwd  = 'D';
                void* p1 = GetP1Char();
                void* p2 = GetP2Char();
                if (p1 && p2 && (uintptr_t)p1 > 0x01000000 && (uintptr_t)p2 > 0x01000000) {
                    __try {
                        float p1_x = *(float*)((uintptr_t)p1 + 0x1E8);
                        float p2_x = *(float*)((uintptr_t)p2 + 0x1E8);
                        if (p1_x > p2_x) { p1_back = 'D'; p1_fwd = 'A'; }
                    } __except (EXCEPTION_EXECUTE_HANDLER) {}
                }

                // Frame 001 - 012: Ensure distance (Whiff range)
                if (g_CurrentFrame == g_SimTriggerFrame) {
                    void* p1_cur = GetP1Char();
                    void* p2_cur = GetP2Char();
                    if (p1_cur && p2_cur) {
                        __try {
                            float d = fabsf(*(float*)((uintptr_t)p1_cur + 0x1E8) - *(float*)((uintptr_t)p2_cur + 0x1E8));
                            if (d < 180.0f) {
                                SimulateKeyDown(p1_back);
                                LogTrace("[MODE 8 F4 | #%llu] Frame 01: Creating distance for Whiff (dist=%.1f)...", g_CurrentFrame, d);
                            }
                        } __except (EXCEPTION_EXECUTE_HANDLER) {}
                    }
                }
                if (g_CurrentFrame == g_SimTriggerFrame + 12) {
                    SimulateKeyUp(p1_back);
                }

                // Frame 015: Press [1] ('T') into thin air (Whiff)
                if (g_CurrentFrame == g_SimTriggerFrame + 14) {
                    SimulateKeyDown('T');
                    LogTrace("[MODE 8 F4 | #%llu] Frame 15: Standing 1 Jab WHIFF (Hold [1])", g_CurrentFrame);
                }
                // Frame 019: First [Back] tap during whiff startup
                if (g_CurrentFrame == g_SimTriggerFrame + 18) {
                    SimulateKeyDown(p1_back);
                }
                // Frame 022: Stick neutral
                if (g_CurrentFrame == g_SimTriggerFrame + 21) {
                    SimulateKeyUp(p1_back);
                }
                // Frame 024: Second [Back] press & hold
                if (g_CurrentFrame == g_SimTriggerFrame + 23) {
                    SimulateKeyDown(p1_back);
                }
                // Frame 027: Release [1] during whiff active frames (Engine 0x8F1B20 should return 1 -> cancel prohibited)
                if (g_CurrentFrame == g_SimTriggerFrame + 26) {
                    SimulateKeyUp('T');
                    LogTrace("[MODE 8 F4 | #%llu] Frame 27: RELEASE [1] on Whiff (MK9 Native Whiff Rule: Must NOT cancel)", g_CurrentFrame);
                }
                // Frame 032: Release [Back]
                if (g_CurrentFrame == g_SimTriggerFrame + 31) {
                    SimulateKeyUp(p1_back);
                }
                // Frame 050: End
                if (g_CurrentFrame >= g_SimTriggerFrame + 49) {
                    SimulateKeyUp('T');
                    SimulateKeyUp('A');
                    SimulateKeyUp('D');
                    g_SimTriggerFrame = 0;
                    g_SimSeqType = MACRO_SEQ_STANDARD;
                    LogTrace("[MODE 8 F4 | #%llu] Frame 50: End of observation window", g_CurrentFrame);
                }
            } else if (g_SimSeqType == MACRO_SEQ_MODE8_F5_P2_RAW_NEG_EDGE) {
                WORD p2_back = VK_RIGHT;
                void* p1 = GetP1Char();
                void* p2 = GetP2Char();
                if (p1 && p2 && (uintptr_t)p1 > 0x01000000 && (uintptr_t)p2 > 0x01000000) {
                    __try {
                        float p1_x = *(float*)((uintptr_t)p1 + 0x1E8);
                        float p2_x = *(float*)((uintptr_t)p2 + 0x1E8);
                        if (p2_x < p1_x) { p2_back = VK_LEFT; }
                    } __except (EXCEPTION_EXECUTE_HANDLER) {}
                }

                // Frame 001: Hold P2 [1] (VK_NUMPAD7)
                if (g_CurrentFrame == g_SimTriggerFrame) {
                    SimulateKeyDown(VK_NUMPAD7);
                    LogTrace("[MODE 8 F5 | #%llu] Frame 01: P2 Hold [1]", g_CurrentFrame);
                }
                // Frames 001 - 026: P2 Standing 1 recovers to neutral
                // Frame 028 - 029: First [Back] tap (2f)
                if (g_CurrentFrame == g_SimTriggerFrame + 27) {
                    SimulateKeyDown(p2_back);
                    LogTrace("[MODE 8 F5 | #%llu] Frame 28: P2 Tap [Back] from Neutral (2f hold)", g_CurrentFrame);
                }
                // Frame 030 - 031: Neutral (2f)
                if (g_CurrentFrame == g_SimTriggerFrame + 29) {
                    SimulateKeyUp(p2_back);
                    LogTrace("[MODE 8 F5 | #%llu] Frame 30: P2 Stick Neutral (2f)", g_CurrentFrame);
                }
                // Frame 032 - 033: Second [Back] press + RELEASE P2 [1] SIMULTANEOUSLY on the exact same tick!
                if (g_CurrentFrame == g_SimTriggerFrame + 31) {
                    SimulateKeyDown(p2_back);
                    SimulateKeyUp(VK_NUMPAD7);
                    LogTrace("[MODE 8 F5 | #%llu] Frame 32: P2 Press [Back] + RELEASE [1] (Negative Edge Trigger)", g_CurrentFrame);
                }
                // Frame 034: Release [Back]
                if (g_CurrentFrame == g_SimTriggerFrame + 33) {
                    SimulateKeyUp(p2_back);
                    LogTrace("[MODE 8 F5 | #%llu] Frame 34: P2 Release [Back]", g_CurrentFrame);
                }
                // Frame 055: End
                if (g_CurrentFrame >= g_SimTriggerFrame + 54) {
                    SimulateKeyUp(VK_NUMPAD7);
                    SimulateKeyUp(VK_LEFT);
                    SimulateKeyUp(VK_RIGHT);
                    g_SimTriggerFrame = 0;
                    g_SimSeqType = MACRO_SEQ_STANDARD;
                    LogTrace("[MODE 8 F5 | #%llu] Frame 55: End of observation window", g_CurrentFrame);
                }
            } else if (g_SimSeqType == MACRO_SEQ_MODE8_F6_P2_NORMAL_SPECIAL) {
                WORD p2_back = VK_RIGHT;
                void* p1 = GetP1Char();
                void* p2 = GetP2Char();
                if (p1 && p2 && (uintptr_t)p1 > 0x01000000 && (uintptr_t)p2 > 0x01000000) {
                    __try {
                        float p1_x = *(float*)((uintptr_t)p1 + 0x1E8);
                        float p2_x = *(float*)((uintptr_t)p2 + 0x1E8);
                        if (p2_x < p1_x) { p2_back = VK_LEFT; }
                    } __except (EXCEPTION_EXECUTE_HANDLER) {}
                }

                // Frame 001 - 002: First [Back] tap (2f)
                if (g_CurrentFrame == g_SimTriggerFrame) {
                    SimulateKeyDown(p2_back);
                    LogTrace("[MODE 8 F6 | #%llu] Frame 01: P2 Press [Back] (2f)", g_CurrentFrame);
                }
                // Frame 003 - 004: Neutral (2f)
                if (g_CurrentFrame == g_SimTriggerFrame + 2) {
                    SimulateKeyUp(p2_back);
                    LogTrace("[MODE 8 F6 | #%llu] Frame 03: P2 Stick Neutral (2f)", g_CurrentFrame);
                }
                // Frame 005 - 006: Second [Back] press + Press P2 [1] simultaneously (Downstroke Attack)
                if (g_CurrentFrame == g_SimTriggerFrame + 4) {
                    SimulateKeyDown(p2_back);
                    SimulateKeyDown(VK_NUMPAD7);
                    LogTrace("[MODE 8 F6 | #%llu] Frame 05: P2 Press [Back] + Press [1] (Downstroke Attack)", g_CurrentFrame);
                }
                // Frame 007: Release both
                if (g_CurrentFrame == g_SimTriggerFrame + 6) {
                    SimulateKeyUp(VK_NUMPAD7);
                    SimulateKeyUp(p2_back);
                    LogTrace("[MODE 8 F6 | #%llu] Frame 07: P2 Release [Back] & [1]", g_CurrentFrame);
                }
                // Frame 030: End
                if (g_CurrentFrame >= g_SimTriggerFrame + 29) {
                    SimulateKeyUp(VK_NUMPAD7);
                    SimulateKeyUp(VK_LEFT);
                    SimulateKeyUp(VK_RIGHT);
                    g_SimTriggerFrame = 0;
                    g_SimSeqType = MACRO_SEQ_STANDARD;
                    LogTrace("[MODE 8 F6 | #%llu] Frame 30: End of observation window", g_CurrentFrame);
                }
            } else if (g_SimSeqType == MACRO_SEQ_MODE8_F7_P2_HIT_CONFIRM_CANCEL) {
                WORD p2_back = VK_RIGHT;
                WORD p2_fwd  = VK_LEFT;
                void* p1 = GetP1Char();
                void* p2 = GetP2Char();
                if (p1 && p2 && (uintptr_t)p1 > 0x01000000 && (uintptr_t)p2 > 0x01000000) {
                    __try {
                        float p1_x = *(float*)((uintptr_t)p1 + 0x1E8);
                        float p2_x = *(float*)((uintptr_t)p2 + 0x1E8);
                        if (p2_x < p1_x) { p2_back = VK_LEFT; p2_fwd = VK_RIGHT; }
                    } __except (EXCEPTION_EXECUTE_HANDLER) {}
                }

                // Frame 001 - 012: Ensure close contact range
                if (g_CurrentFrame == g_SimTriggerFrame) {
                    void* p1_cur = GetP1Char();
                    void* p2_cur = GetP2Char();
                    if (p1_cur && p2_cur) {
                        __try {
                            float d = fabsf(*(float*)((uintptr_t)p1_cur + 0x1E8) - *(float*)((uintptr_t)p2_cur + 0x1E8));
                            if (d > 95.0f) {
                                SimulateKeyDown(p2_fwd);
                                LogTrace("[MODE 8 F7 | #%llu] Frame 01: P2 Closing distance (dist=%.1f)...", g_CurrentFrame, d);
                            }
                        } __except (EXCEPTION_EXECUTE_HANDLER) {}
                    }
                }
                if (g_CurrentFrame == g_SimTriggerFrame + 12) {
                    SimulateKeyUp(p2_fwd);
                }

                // Frame 015: P2 Press [1] (VK_NUMPAD7)
                if (g_CurrentFrame == g_SimTriggerFrame + 14) {
                    SimulateKeyDown(VK_NUMPAD7);
                    LogTrace("[MODE 8 F7 | #%llu] Frame 15: P2 Standing 1 Jab START (Hold [1])", g_CurrentFrame);
                }
                // Frame 019: First [Back] tap during startup
                if (g_CurrentFrame == g_SimTriggerFrame + 18) {
                    SimulateKeyDown(p2_back);
                }
                // Frame 022: Stick neutral
                if (g_CurrentFrame == g_SimTriggerFrame + 21) {
                    SimulateKeyUp(p2_back);
                }
                // Frame 024: Second [Back] hold
                if (g_CurrentFrame == g_SimTriggerFrame + 23) {
                    SimulateKeyDown(p2_back);
                }
                // Frame 027: Release P2 [1] in Hitstop / Cancel Window!
                if (g_CurrentFrame == g_SimTriggerFrame + 26) {
                    SimulateKeyUp(VK_NUMPAD7);
                    LogTrace("[MODE 8 F7 | #%llu] Frame 27: P2 RELEASE [1] in Hitstop / Cancel Window! (Negative Edge Cancel)", g_CurrentFrame);
                }
                // Frame 032: Release [Back]
                if (g_CurrentFrame == g_SimTriggerFrame + 31) {
                    SimulateKeyUp(p2_back);
                }
                // Frame 050: End
                if (g_CurrentFrame >= g_SimTriggerFrame + 49) {
                    SimulateKeyUp(VK_NUMPAD7);
                    SimulateKeyUp(VK_LEFT);
                    SimulateKeyUp(VK_RIGHT);
                    g_SimTriggerFrame = 0;
                    g_SimSeqType = MACRO_SEQ_STANDARD;
                    LogTrace("[MODE 8 F7 | #%llu] Frame 50: End of observation window", g_CurrentFrame);
                }
            } else if (g_SimSeqType == MACRO_SEQ_MODE8_F8_STRING_BUFFER) {
                WORD p1_back = 'A';
                void* p1 = GetP1Char();
                void* p2 = GetP2Char();
                if (p1 && p2 && (uintptr_t)p1 > 0x01000000 && (uintptr_t)p2 > 0x01000000) {
                    __try {
                        float p1_x = *(float*)((uintptr_t)p1 + 0x1E8);
                        float p2_x = *(float*)((uintptr_t)p2 + 0x1E8);
                        if (p1_x > p2_x) { p1_back = 'D'; }
                    } __except (EXCEPTION_EXECUTE_HANDLER) {}
                }

                // Frame 001 - 003: Press [1] (Hit 1 of string)
                if (g_CurrentFrame == g_SimTriggerFrame) {
                    SimulateKeyDown('T');
                    LogTrace("[MODE 8 F8 | #%llu] Frame 01: Press [1] (Hit 1 of string)", g_CurrentFrame);
                }
                // Frame 004: Release [1]
                if (g_CurrentFrame == g_SimTriggerFrame + 3) {
                    SimulateKeyUp('T');
                    LogTrace("[MODE 8 F8 | #%llu] Frame 04: Release [1]", g_CurrentFrame);
                }
                // Frame 008 - 010: Press [1] (Hit 2 of string)
                if (g_CurrentFrame == g_SimTriggerFrame + 7) {
                    SimulateKeyDown('T');
                    LogTrace("[MODE 8 F8 | #%llu] Frame 08: Press [1] (Hit 2 of string)", g_CurrentFrame);
                }
                // Frame 011 - 014: Tap [Back]
                if (g_CurrentFrame == g_SimTriggerFrame + 10) {
                    SimulateKeyDown(p1_back);
                    LogTrace("[MODE 8 F8 | #%llu] Frame 11: Tap [Back]", g_CurrentFrame);
                }
                // Frame 015: Neutral
                if (g_CurrentFrame == g_SimTriggerFrame + 14) {
                    SimulateKeyUp(p1_back);
                }
                // Frame 018 - 021: Tap [Back] (Second Back)
                if (g_CurrentFrame == g_SimTriggerFrame + 17) {
                    SimulateKeyDown(p1_back);
                    LogTrace("[MODE 8 F8 | #%llu] Frame 18: Tap 2nd [Back]", g_CurrentFrame);
                }
                // Frame 022: Release [Back]
                if (g_CurrentFrame == g_SimTriggerFrame + 21) {
                    SimulateKeyUp(p1_back);
                    SimulateKeyUp('T');
                    LogTrace("[MODE 8 F8 | #%llu] Frame 22: Release [Back] + Neutral", g_CurrentFrame);
                }
                // Frame 040: End
                if (g_CurrentFrame >= g_SimTriggerFrame + 39) {
                    SimulateKeyUp('T');
                    SimulateKeyUp('A');
                    SimulateKeyUp('D');
                    g_SimTriggerFrame = 0;
                    g_SimSeqType = MACRO_SEQ_STANDARD;
                    LogTrace("[MODE 8 F8 | #%llu] Frame 40: End of observation window", g_CurrentFrame);
                }
            } else if (g_SimSeqType == MACRO_SEQ_MODE9_F1_FP) {
                // Frame 001 - 002: Press [1] (FP)
                if (g_CurrentFrame == g_SimTriggerFrame) {
                    SimulateKeyDown('T');
                    LogTrace("[MODE 9 F1 | #%llu] Frame 01: Press [1] (Front Punch)", g_CurrentFrame);
                }
                if (g_CurrentFrame == g_SimTriggerFrame + 2) {
                    SimulateKeyUp('T');
                    LogTrace("[MODE 9 F1 | #%llu] Frame 03: Release [1] (Neutral)", g_CurrentFrame);
                }
                if (g_CurrentFrame >= g_SimTriggerFrame + 55) {
                    SimulateKeyUp('T');
                    g_SimTriggerFrame = 0;
                    g_SimSeqType = MACRO_SEQ_STANDARD;
                    LogTrace("[MODE 9 F1 | #%llu] Frame 55: End of observation window", g_CurrentFrame);
                }
            } else if (g_SimSeqType == MACRO_SEQ_MODE9_F2_FP_DASH) {
                WORD p1_fwd = 'D';
                void* p1 = GetP1Char();
                void* p2 = GetP2Char();
                if (p1 && p2 && (uintptr_t)p1 > 0x01000000 && (uintptr_t)p2 > 0x01000000) {
                    __try {
                        float p1_x = *(float*)((uintptr_t)p1 + 0x1E8);
                        float p2_x = *(float*)((uintptr_t)p2 + 0x1E8);
                        if (p1_x > p2_x) { p1_fwd = 'A'; }
                    } __except (EXCEPTION_EXECUTE_HANDLER) {}
                }

                // Frame 001 - 002: Press [1] (FP)
                if (g_CurrentFrame == g_SimTriggerFrame) {
                    SimulateKeyDown('T');
                    LogTrace("[MODE 9 F2 | #%llu] Frame 01: Press [1] (Front Punch)", g_CurrentFrame);
                }
                if (g_CurrentFrame == g_SimTriggerFrame + 2) {
                    SimulateKeyUp('T');
                    LogTrace("[MODE 9 F2 | #%llu] Frame 03: Release [1] (Neutral)", g_CurrentFrame);
                }
                // Hit connects on frame ~11/12. Tap Forward for dash immediately upon impact/early recovery:
                // Frame 013 - 015: Tap 1st [Forward]
                if (g_CurrentFrame == g_SimTriggerFrame + 12) {
                    SimulateKeyDown(p1_fwd);
                    LogTrace("[MODE 9 F2 | #%llu] Frame 13: Tap 1st [Forward]", g_CurrentFrame);
                }
                if (g_CurrentFrame == g_SimTriggerFrame + 14) {
                    SimulateKeyUp(p1_fwd);
                    LogTrace("[MODE 9 F2 | #%llu] Frame 15: Release 1st [Forward]", g_CurrentFrame);
                }
                // Frame 016 - 020: Tap 2nd [Forward] (Completes F, F Dash)
                if (g_CurrentFrame == g_SimTriggerFrame + 16) {
                    SimulateKeyDown(p1_fwd);
                    LogTrace("[MODE 9 F2 | #%llu] Frame 17: Tap 2nd [Forward] (F,F Dash Complete)", g_CurrentFrame);
                }
                if (g_CurrentFrame == g_SimTriggerFrame + 20) {
                    SimulateKeyUp(p1_fwd);
                    LogTrace("[MODE 9 F2 | #%llu] Frame 21: Release 2nd [Forward]", g_CurrentFrame);
                }
                if (g_CurrentFrame >= g_SimTriggerFrame + 60) {
                    SimulateKeyUp('T');
                    SimulateKeyUp('A');
                    SimulateKeyUp('D');
                    g_SimTriggerFrame = 0;
                    g_SimSeqType = MACRO_SEQ_STANDARD;
                    LogTrace("[MODE 9 F2 | #%llu] Frame 60: End of observation window", g_CurrentFrame);
                }
            } else if (g_SimSeqType == MACRO_SEQ_MODE9_F3_PROJECTILE_NORMAL) {
                WORD p1_back = 'A';
                void* p1 = GetP1Char();
                void* p2 = GetP2Char();
                if (p1 && p2 && (uintptr_t)p1 > 0x01000000 && (uintptr_t)p2 > 0x01000000) {
                    __try {
                        float p1_x = *(float*)((uintptr_t)p1 + 0x1E8);
                        float p2_x = *(float*)((uintptr_t)p2 + 0x1E8);
                        if (p1_x > p2_x) { p1_back = 'D'; }
                    } __except (EXCEPTION_EXECUTE_HANDLER) {}
                }

                // Frame 01: Buffer flush
                if (g_CurrentFrame == g_SimTriggerFrame) {
                    LogTrace("[MODE 9 F3 | #%llu] Frame 01: Buffer Flush", g_CurrentFrame);
                }
                // Frame 02 - 04: Press [Down] (S)
                if (g_CurrentFrame == g_SimTriggerFrame + 1) {
                    SimulateKeyDown('S');
                    LogTrace("[MODE 9 F3 | #%llu] Frame 02: Press [Down] (S)", g_CurrentFrame);
                }
                if (g_CurrentFrame == g_SimTriggerFrame + 4) {
                    SimulateKeyUp('S');
                    LogTrace("[MODE 9 F3 | #%llu] Frame 05: Release [Down]", g_CurrentFrame);
                }
                // Frame 06 - 08: Press [Back] + [3] (A + G)
                if (g_CurrentFrame == g_SimTriggerFrame + 5) {
                    SimulateKeyDown(p1_back);
                    SimulateKeyDown('G');
                    LogTrace("[MODE 9 F3 | #%llu] Frame 06: Press [Back + 3] (Normal Projectile D,B+3)", g_CurrentFrame);
                }
                if (g_CurrentFrame == g_SimTriggerFrame + 8) {
                    SimulateKeyUp(p1_back);
                    SimulateKeyUp('G');
                    LogTrace("[MODE 9 F3 | #%llu] Frame 09: Release [Back + 3]", g_CurrentFrame);
                }
                if (g_CurrentFrame >= g_SimTriggerFrame + 70) {
                    SimulateKeyUp('S');
                    SimulateKeyUp('A');
                    SimulateKeyUp('D');
                    SimulateKeyUp('G');
                    g_SimTriggerFrame = 0;
                    g_SimSeqType = MACRO_SEQ_STANDARD;
                    LogTrace("[MODE 9 F3 | #%llu] Frame 70: End of observation window", g_CurrentFrame);
                }
            } else if (g_SimSeqType == MACRO_SEQ_MODE9_F4_PROJECTILE_EX) {
                WORD p1_back = 'A';
                void* p1 = GetP1Char();
                void* p2 = GetP2Char();
                if (p1 && p2 && (uintptr_t)p1 > 0x01000000 && (uintptr_t)p2 > 0x01000000) {
                    __try {
                        float p1_x = *(float*)((uintptr_t)p1 + 0x1E8);
                        float p2_x = *(float*)((uintptr_t)p2 + 0x1E8);
                        if (p1_x > p2_x) { p1_back = 'D'; }
                    } __except (EXCEPTION_EXECUTE_HANDLER) {}
                }

                // Frame 01: Buffer flush
                if (g_CurrentFrame == g_SimTriggerFrame) {
                    LogTrace("[MODE 9 F4 | #%llu] Frame 01: Buffer Flush", g_CurrentFrame);
                }
                // Frame 02 - 04: Press [Down] (S)
                if (g_CurrentFrame == g_SimTriggerFrame + 1) {
                    SimulateKeyDown('S');
                    LogTrace("[MODE 9 F4 | #%llu] Frame 02: Press [Down] (S)", g_CurrentFrame);
                }
                if (g_CurrentFrame == g_SimTriggerFrame + 4) {
                    SimulateKeyUp('S');
                    LogTrace("[MODE 9 F4 | #%llu] Frame 05: Release [Down]", g_CurrentFrame);
                }
                // Frame 06 - 08: Press [Back] + [3] + [Block] (A + G + Space)
                if (g_CurrentFrame == g_SimTriggerFrame + 5) {
                    SimulateKeyDown(p1_back);
                    SimulateKeyDown('G');
                    SimulateKeyDown(VK_SPACE);
                    LogTrace("[MODE 9 F4 | #%llu] Frame 06: Press [Back + 3 + Block] (EX Projectile D,B+3+Block)", g_CurrentFrame);
                }
                if (g_CurrentFrame == g_SimTriggerFrame + 8) {
                    SimulateKeyUp(p1_back);
                    SimulateKeyUp('G');
                    SimulateKeyUp(VK_SPACE);
                    LogTrace("[MODE 9 F4 | #%llu] Frame 09: Release [Back + 3 + Block]", g_CurrentFrame);
                }
                if (g_CurrentFrame >= g_SimTriggerFrame + 70) {
                    SimulateKeyUp('S');
                    SimulateKeyUp('A');
                    SimulateKeyUp('D');
                    SimulateKeyUp('G');
                    SimulateKeyUp(VK_SPACE);
                    g_SimTriggerFrame = 0;
                    g_SimSeqType = MACRO_SEQ_STANDARD;
                    LogTrace("[MODE 9 F4 | #%llu] Frame 70: End of observation window", g_CurrentFrame);
                }
            } else if (g_SimSeqType >= MACRO_SEQ_MODE9_F5_JC_FP_HOLD_DOWN_FP && g_SimSeqType <= MACRO_SEQ_MODE9_F8_JC_FP_HOLD_DOWN_FP) {
                // Exact replication: FP 1 -> Dash Forward (11f) -> FP 2 (cancels dash) -> Tight D+FP on recovery
                // Target: Observe why Johnny Cage executes Standing FP instead of Crouching FP (D+1)
                // F5: Last FP at Frame 56 (was F7)
                // F6: Last FP at Frame 58 (was F8)
                // F7: Last FP at Frame 59
                // F8: Last FP at Frame 60
                uint64_t targetFpFrame = 56;
                const char* fKeyName = "F5";
                if (g_SimSeqType == MACRO_SEQ_MODE9_F6_JC_FP_HOLD_DOWN_FP) {
                    targetFpFrame = 58;
                    fKeyName = "F6";
                } else if (g_SimSeqType == MACRO_SEQ_MODE9_F7_JC_FP_HOLD_DOWN_FP) {
                    targetFpFrame = 59;
                    fKeyName = "F7";
                } else if (g_SimSeqType == MACRO_SEQ_MODE9_F8_JC_FP_HOLD_DOWN_FP) {
                    targetFpFrame = 60;
                    fKeyName = "F8";
                }
                uint64_t fpOffset = targetFpFrame - 1; // 0-indexed frame offset from g_SimTriggerFrame

                WORD p1_fwd = 'D';
                void* p1 = GetP1Char();
                void* p2 = GetP2Char();
                if (p1 && p2 && (uintptr_t)p1 > 0x01000000 && (uintptr_t)p2 > 0x01000000) {
                    __try {
                        float p1_x = *(float*)((uintptr_t)p1 + 0x1E8);
                        float p2_x = *(float*)((uintptr_t)p2 + 0x1E8);
                        if (p1_x > p2_x) { p1_fwd = 'A'; }
                    } __except (EXCEPTION_EXECUTE_HANDLER) {}
                }

                // Phase 1: Attack 1 - Standing Front Punch (FP 1)
                if (g_CurrentFrame == g_SimTriggerFrame) {
                    SimulateKeyDown('T');
                    LogTrace("[MODE 9 %s | #%llu] Frame 01: Press FP 1 (Standing Front Punch)", fKeyName, g_CurrentFrame);
                }
                if (g_CurrentFrame == g_SimTriggerFrame + 2) {
                    SimulateKeyUp('T');
                    LogTrace("[MODE 9 %s | #%llu] Frame 03: Release FP 1", fKeyName, g_CurrentFrame);
                }

                // Phase 2: Dash Forward (F, F) - Cancel recovery into Dash (matches Orange 1..11 on timeline)
                if (g_CurrentFrame == g_SimTriggerFrame + 11) {
                    SimulateKeyDown(p1_fwd);
                    LogTrace("[MODE 9 %s | #%llu] Frame 12: Tap 1st [Forward]", fKeyName, g_CurrentFrame);
                }
                if (g_CurrentFrame == g_SimTriggerFrame + 13) {
                    SimulateKeyUp(p1_fwd);
                    LogTrace("[MODE 9 %s | #%llu] Frame 14: Release 1st [Forward]", fKeyName, g_CurrentFrame);
                }
                if (g_CurrentFrame == g_SimTriggerFrame + 15) {
                    SimulateKeyDown(p1_fwd);
                    LogTrace("[MODE 9 %s | #%llu] Frame 16: Tap 2nd [Forward] (Dash Initiated)", fKeyName, g_CurrentFrame);
                }
                if (g_CurrentFrame == g_SimTriggerFrame + 19) {
                    SimulateKeyUp(p1_fwd);
                    LogTrace("[MODE 9 %s | #%llu] Frame 20: Release 2nd [Forward] (Dashing ~11f...)", fKeyName, g_CurrentFrame);
                }

                // Phase 3: Attack 2 - FP 2 cancelling Dash (after exactly 11 frames of dash, on Frame 31)
                // Dash runs from Frame 20 to Frame 30 = 11 Orange frames (matches Screenshot 2)
                if (g_CurrentFrame == g_SimTriggerFrame + 30) {
                    SimulateKeyDown('T');
                    LogTrace("[MODE 9 %s | #%llu] Frame 31: Press FP 2 (Cancelling Dash after 11f)", fKeyName, g_CurrentFrame);
                }
                if (g_CurrentFrame == g_SimTriggerFrame + 32) {
                    SimulateKeyUp('T');
                    LogTrace("[MODE 9 %s | #%llu] Frame 33: Release FP 2", fKeyName, g_CurrentFrame);
                }

                // Phase 4: Attack 3 - Tight D+FP buffer inside recovery of FP 2!
                // FP 2 runs: Startup 7f (31..37), Active 1f (38), Recovery 12f (39..50). Recovery ends at Frame 51.
                // Input [Down] ('S') at Frame 49 (2 frames before recovery ends, while character is locked in standing recovery)
                if (g_CurrentFrame == g_SimTriggerFrame + 48) {
                    SimulateKeyDown('S');
                    void* p1_cur = GetP1Char();
                    uint32_t stance = (p1_cur && (uintptr_t)p1_cur > 0x01000000) ? *(uint32_t*)((uintptr_t)p1_cur + 0x2E8) : 0;
                    uint32_t action = (p1_cur && (uintptr_t)p1_cur > 0x01000000) ? *(uint32_t*)((uintptr_t)p1_cur + 0x1D4) : 0;
                    LogTrace("[MODE 9 %s | #%llu] Frame 49: Input [Down] ('S') tightly inside FP 2 recovery! (Stance=0x%04X, Action=0x%04X)",
                             fKeyName, g_CurrentFrame, stance, action);
                }
                // Input [FP] ('T') at targetFpFrame (with Down held!)
                if (g_CurrentFrame == g_SimTriggerFrame + fpOffset) {
                    SimulateKeyDown('T');
                    void* p1_cur = GetP1Char();
                    uint32_t stance = (p1_cur && (uintptr_t)p1_cur > 0x01000000) ? *(uint32_t*)((uintptr_t)p1_cur + 0x2E8) : 0;
                    uint32_t action = (p1_cur && (uintptr_t)p1_cur > 0x01000000) ? *(uint32_t*)((uintptr_t)p1_cur + 0x1D4) : 0;
                    int func = (p1_cur && (uintptr_t)p1_cur > 0x01000000) ? *(int*)((uintptr_t)p1_cur + 0x2F88) : -1;
                    LogTrace("[MODE 9 %s | #%llu] Frame %02llu: Press [FP] ('T') with [Down] HELD! (Stance=0x%04X, Action=0x%04X, ScriptFunc=%d)",
                             fKeyName, g_CurrentFrame, targetFpFrame, stance, action, func);
                }
                // Maintain [Down] HELD across Frames 49-85
                if (g_CurrentFrame >= g_SimTriggerFrame + 48 && g_CurrentFrame <= g_SimTriggerFrame + 85) {
                    SimulateKeyDown('S');
                }
                // Maintain [FP] HELD for 16 frames
                if (g_CurrentFrame >= g_SimTriggerFrame + fpOffset && g_CurrentFrame <= g_SimTriggerFrame + fpOffset + 15) {
                    SimulateKeyDown('T'); // Long press on FP (16 frames)
                }
                // Release FP ('T') after 16 frames
                if (g_CurrentFrame == g_SimTriggerFrame + fpOffset + 16) {
                    SimulateKeyUp('T');
                    LogTrace("[MODE 9 %s | #%llu] Frame %02llu: Release [FP] after 16f press (Down 'S' still held)",
                             fKeyName, g_CurrentFrame, targetFpFrame + 16);
                }

                // Phase 5: Result Inspection - Check what attack NetherRealm dispatched!
                if (g_CurrentFrame == g_SimTriggerFrame + fpOffset + 16) {
                    void* p1_cur = GetP1Char();
                    if (p1_cur && (uintptr_t)p1_cur > 0x01000000) {
                        uint32_t act = *(uint32_t*)((uintptr_t)p1_cur + 0x1D4);
                        uint32_t stance = *(uint32_t*)((uintptr_t)p1_cur + 0x2E8);
                        int func = *(int*)((uintptr_t)p1_cur + 0x2F88);
                        uint32_t hitboxes = *(uint32_t*)((uintptr_t)p1_cur + 0x7A3C);
                        
                        bool isCrouchAttack = (act >= 0x6200 && act <= 0x620B) || (act == 0x5300);
                        const char* moveType = isCrouchAttack ? "CROUCHING FP (D+1)" : "STANDING FP (1)";
                        LogTrace("====================================================================");
                        LogTrace("[MODE 9 %s RESULT | #%llu] DISPATCHED ATTACK 3: %s", fKeyName, g_CurrentFrame, moveType);
                        LogTrace("   Action State: 0x%04X | Stance Flags: 0x%04X | Script Function: %d | Hitboxes: %u",
                                 act, stance, func, hitboxes);
                        LogTrace("   Down Held: YES ('S' held) | FP Input: Frame %llu", targetFpFrame);
                        if (!isCrouchAttack) {
                            LogTrace("   >>> ANOMALY: Action state is 0x%04X (Standing Attack) / Script Func %d!", act, func);
                            LogTrace("   >>> NetherRealm matched Standing Normal 1 instead of Crouching D+1!");
                        } else {
                            LogTrace("   >>> SUCCESS: Character executed Crouching Normal (D+1) cleanly while ducking!");
                        }
                        LogTrace("====================================================================");
                    }
                }

                // Frame 95: Cleanup all keys
                if (g_CurrentFrame >= g_SimTriggerFrame + 95) {
                    SimulateKeyUp('T');
                    SimulateKeyUp('S');
                    SimulateKeyUp(p1_fwd);
                    g_SimTriggerFrame = 0;
                    g_SimSeqType = MACRO_SEQ_STANDARD;
                    LogTrace("[MODE 9 %s | #%llu] Frame 95: End of observation window (All keys released)", fKeyName, g_CurrentFrame);
                }
            } else if (g_SimSeqType >= MACRO_SEQ_MODE10_F1_JC_EX_FORCEBALL_SOLO && g_SimSeqType <= MACRO_SEQ_MODE10_F8_JC_EX_FORCEBALL_VS_P1_STAND) {
                bool isSolo = (g_SimSeqType == MACRO_SEQ_MODE10_F1_JC_EX_FORCEBALL_SOLO);
                bool isCrouchBL = (g_SimSeqType == MACRO_SEQ_MODE10_F5_JC_EX_FORCEBALL_VS_P1_CROUCH || g_SimSeqType == MACRO_SEQ_MODE10_F6_JC_EX_FORCEBALL_VS_P1_CROUCH);

                int delayF = 0;
                const char* fKeyName = "F1 (Solo Baseline)";
                if (g_SimSeqType == MACRO_SEQ_MODE10_F2_JC_EX_FORCEBALL_VS_P1_EX) {
                    delayF = 0;
                    fKeyName = "F2 (P1 Stand BL -> EX +0f Delay Frame 94)";
                } else if (g_SimSeqType == MACRO_SEQ_MODE10_F3_JC_EX_FORCEBALL_VS_P1_EX) {
                    delayF = 5;
                    fKeyName = "F3 (P1 Stand BL -> EX +5f Delay Frame 99)";
                } else if (g_SimSeqType == MACRO_SEQ_MODE10_F4_JC_EX_FORCEBALL_VS_P1_EX) {
                    delayF = 6;
                    fKeyName = "F4 (P1 Stand BL -> EX +6f Delay Frame 100)";
                } else if (g_SimSeqType == MACRO_SEQ_MODE10_F5_JC_EX_FORCEBALL_VS_P1_CROUCH) {
                    delayF = 8;
                    fKeyName = "F5 (P1 Crouch BL -> EX +8f Delay Frame 102 [Former F8])";
                } else if (g_SimSeqType == MACRO_SEQ_MODE10_F6_JC_EX_FORCEBALL_VS_P1_CROUCH) {
                    delayF = 9;
                    fKeyName = "F6 (P1 Crouch BL -> EX +9f Delay Frame 103)";
                } else if (g_SimSeqType == MACRO_SEQ_MODE10_F7_JC_EX_FORCEBALL_VS_P1_STAND) {
                    delayF = 8;
                    fKeyName = "F7 (P1 Stand BL -> EX +8f Delay Frame 102 [No Crouch])";
                } else if (g_SimSeqType == MACRO_SEQ_MODE10_F8_JC_EX_FORCEBALL_VS_P1_STAND) {
                    delayF = 9;
                    fKeyName = "F8 (P1 Stand BL -> EX +9f Delay Frame 103 [No Crouch])";
                }

                WORD p2_fwd  = VK_LEFT;
                WORD p2_back = VK_RIGHT;
                WORD p1_back = 'A';
                WORD p1_fwd  = 'D';

                void* p1 = GetP1Char();
                void* p2 = GetP2Char();
                if (p1 && p2 && (uintptr_t)p1 > 0x01000000 && (uintptr_t)p2 > 0x01000000) {
                    __try {
                        float p1_x = *(float*)((uintptr_t)p1 + 0x1E8);
                        float p2_x = *(float*)((uintptr_t)p2 + 0x1E8);
                        if (p2_x < p1_x) { p2_back = VK_LEFT; p2_fwd = VK_RIGHT; }
                        else { p2_back = VK_RIGHT; p2_fwd = VK_LEFT; }

                        if (p1_x < p2_x) { p1_back = 'A'; p1_fwd = 'D'; }
                        else { p1_back = 'D'; p1_fwd = 'A'; }
                    } __except (EXCEPTION_EXECUTE_HANDLER) {}
                }

                // Phase 1: Frame 01 - 30 (30 frames): P2 Hold Left (Forward walk), P1 Hold Block (for F2-F8)
                if (g_CurrentFrame == g_SimTriggerFrame) {
                    SimulateKeyDown(p2_fwd);
                    if (!isSolo) {
                        SimulateKeyDown(VK_SPACE);
                        LogTrace("[MODE 10 %s | #%llu] Frame 01: P1 Hold [Block] (Blocking EX Forceball)", fKeyName, g_CurrentFrame);
                    }
                    LogTrace("[MODE 10 %s | #%llu] Frame 01: P2 Hold [Left/Forward] (30f Walk)", fKeyName, g_CurrentFrame);
                }
                // Phase 2: Frame 31 - 35 (5 frames): Release Left (Neutral)
                if (g_CurrentFrame == g_SimTriggerFrame + 30) {
                    SimulateKeyUp(p2_fwd);
                    LogTrace("[MODE 10 %s | #%llu] Frame 31: P2 Release [Left/Forward] (5f Neutral)", fKeyName, g_CurrentFrame);
                }
                // Phase 3: Frame 36 - 38 (3 frames): Press Down (VK_DOWN)
                if (g_CurrentFrame == g_SimTriggerFrame + 35) {
                    SimulateKeyDown(VK_DOWN);
                    LogTrace("[MODE 10 %s | #%llu] Frame 36: P2 Press [Down] (D,F+2+BL Start)", fKeyName, g_CurrentFrame);
                }
                if (g_CurrentFrame == g_SimTriggerFrame + 38) {
                    SimulateKeyUp(VK_DOWN);
                }
                // Phase 4: Frame 40 - 43 (4 frames): Press Forward + Back Punch [2] + Block (VK_LEFT + VK_NUMPAD9 + VK_NUMPAD0)
                if (g_CurrentFrame == g_SimTriggerFrame + 39) {
                    SimulateKeyDown(p2_fwd);
                    SimulateKeyDown(VK_NUMPAD9); // Back Punch (2)
                    SimulateKeyDown(VK_NUMPAD0); // Block
                    LogTrace("[MODE 10 %s | #%llu] Frame 40: P2 Press [Forward + 2 + Block] (EX Forceball D,F+2+BL)", fKeyName, g_CurrentFrame);
                }
                if (g_CurrentFrame == g_SimTriggerFrame + 43) {
                    SimulateKeyUp(p2_fwd);
                    SimulateKeyUp(VK_NUMPAD9);
                    SimulateKeyUp(VK_NUMPAD0);
                    LogTrace("[MODE 10 %s | #%llu] Frame 44: P2 Release EX Forceball Inputs (Startup 18f, Total 53f)", fKeyName, g_CurrentFrame);
                }

                // Phase 5: Frame 94: P2 Press Forward + 3 (VK_LEFT + VK_NUMPAD4 [FK])
                // EX Forceball executes at Frame 40: baseline F+3 set to Frame 94 fixed across all macros.
                uint64_t targetP2F3Frame = 94;
                if (g_CurrentFrame == g_SimTriggerFrame + (targetP2F3Frame - 1)) {
                    SimulateKeyDown(p2_fwd);
                    SimulateKeyDown(VK_NUMPAD4); // Front Kick (3)
                    LogTrace("[MODE 10 %s | #%llu] Frame %02llu: P2 Input [Forward + 3] (Left + Num4)",
                             fKeyName, g_CurrentFrame, targetP2F3Frame);

                    // For F5 - F8 (Crouch BL): P1 transitions to Crouch Block as soon as P2 inputs F+3!
                    if (isCrouchBL) {
                        SimulateKeyDown('S'); // Hold Down while Block (VK_SPACE) is held -> Crouch Block!
                        LogTrace("[MODE 10 %s | #%llu] Frame %02llu: P1 Transition to [Down + Block] (Crouch Block on P2 F+3 input)",
                                 fKeyName, g_CurrentFrame, targetP2F3Frame);
                    }
                }
                if (g_CurrentFrame == g_SimTriggerFrame + (targetP2F3Frame + 5)) {
                    SimulateKeyUp(p2_fwd);
                    SimulateKeyUp(VK_NUMPAD4);
                    LogTrace("[MODE 10 %s | #%llu] Frame %02llu: P2 Release [Forward + 3]",
                             fKeyName, g_CurrentFrame, targetP2F3Frame + 6);
                }

                // Phase 6 (For F2 - F8): P1 Responds with D, B + 3 + Block (EX Special Reversal)
                // P1 holds Block (or Down+Block for F5-F8) -> releases Block for 1 frame -> buffers Down (2f) -> Back+3+Block (held 15f)
                // Delays are applied to P1: targetP1ReversalFrame = 94 + delayF
                if (!isSolo) {
                    uint64_t targetP1ReversalFrame = 94 + delayF;

                    // Release Block for 1 frame (3 frames before final input)
                    if (g_CurrentFrame == g_SimTriggerFrame + (targetP1ReversalFrame - 4)) {
                        SimulateKeyUp(VK_SPACE);
                        LogTrace("[MODE 10 %s | #%llu] Frame %02llu: P1 Release [Block] (1 frame release while buffering Down)",
                                 fKeyName, g_CurrentFrame, targetP1ReversalFrame - 3);
                    }
                    // Start buffering Down (S) (2 frames before final input) for Stand BL macros
                    if (!isCrouchBL) {
                        if (g_CurrentFrame == g_SimTriggerFrame + (targetP1ReversalFrame - 3)) {
                            SimulateKeyDown('S');
                            LogTrace("[MODE 10 %s | #%llu] Frame %02llu: P1 Press & Buffer [Down] (S)",
                                     fKeyName, g_CurrentFrame, targetP1ReversalFrame - 2);
                        }
                        if (g_CurrentFrame == g_SimTriggerFrame + (targetP1ReversalFrame - 1)) {
                            SimulateKeyUp('S');
                        }
                    } else {
                        // For Crouch BL macros (F5-F8), Down (S) was already held from Frame 94 (P2 F+3 input)
                        if (g_CurrentFrame == g_SimTriggerFrame + (targetP1ReversalFrame - 1)) {
                            SimulateKeyUp('S');
                        }
                    }

                    // Reversal point: Press and hold Back + 3 + Block for 15 frames
                    if (g_CurrentFrame == g_SimTriggerFrame + (targetP1ReversalFrame - 1)) {
                        SimulateKeyDown(p1_back);
                        SimulateKeyDown('G');
                        SimulateKeyDown(VK_SPACE);
                        LogTrace("[MODE 10 %s | #%llu] Frame %02llu: P1 Press & HOLD [Back + 3 + Block] (D,B+3+BL EX Reversal held 15 frames) [P1 Delay +%df]",
                                 fKeyName, g_CurrentFrame, targetP1ReversalFrame, delayF);
                    }
                    if (g_CurrentFrame == g_SimTriggerFrame + (targetP1ReversalFrame + 14)) {
                        SimulateKeyUp(p1_back);
                        SimulateKeyUp('G');
                        SimulateKeyUp(VK_SPACE);
                        if (isCrouchBL) {
                            SimulateKeyUp('S');
                        }
                        LogTrace("[MODE 10 %s | #%llu] Frame %02llu: P1 Release [Back + 3 + Block] Reversal",
                                 fKeyName, g_CurrentFrame, targetP1ReversalFrame + 15);
                    }
                }

                // Detailed Frame-by-Frame Diagnostic Logging for clash window (Frames 92 to 115)
                if (g_CurrentFrame >= g_SimTriggerFrame + 92 && g_CurrentFrame <= g_SimTriggerFrame + 115) {
                    uint64_t relFrame = (g_CurrentFrame - g_SimTriggerFrame) + 1;
                    if (p1 && p2 && (uintptr_t)p1 > 0x01000000 && (uintptr_t)p2 > 0x01000000) {
                        __try {
                            uint32_t p1_act      = *(uint32_t*)((uintptr_t)p1 + 0x1D4);
                            uint32_t p1_flags2EC = *(uint32_t*)((uintptr_t)p1 + 0x2EC);
                            int      p1_func     = *(int*)((uintptr_t)p1 + 0x2F88);
                            uint32_t p1_hitboxes = *(uint32_t*)((uintptr_t)p1 + 0x7A3C);
                            bool     p1_hasArmor = (p1_flags2EC & 0x800000) != 0;
                            bool     p1_isSpecial= (p1_flags2EC & 0x200) != 0;

                            uint32_t p2_act      = *(uint32_t*)((uintptr_t)p2 + 0x1D4);
                            uint32_t p2_flags2EC = *(uint32_t*)((uintptr_t)p2 + 0x2EC);
                            int      p2_func     = *(int*)((uintptr_t)p2 + 0x2F88);
                            uint32_t p2_hitboxes = *(uint32_t*)((uintptr_t)p2 + 0x7A3C);

                            LogTrace("[MODE 10 %s | Frame %03llu | #%llu] STATE:", fKeyName, relFrame, g_CurrentFrame);
                            LogTrace("   P2 (Attacker): Act=0x%04X | Func=%d | Hitboxes=%u | Flags=0x%08X",
                                     p2_act, p2_func, p2_hitboxes, p2_flags2EC);
                            LogTrace("   P1 (Defender): Act=0x%04X | Func=%d | Hitboxes=%u | Flags=0x%08X | Armor=%s | Spec=%s",
                                     p1_act, p1_func, p1_hitboxes, p1_flags2EC,
                                     p1_hasArmor ? "YES (0x800000)" : "NO",
                                     p1_isSpecial ? "YES (0x200)" : "NO");
                        } __except (EXCEPTION_EXECUTE_HANDLER) {}
                    }
                }

                // Phase 7: Frame 150: Cleanup and end observation
                if (g_CurrentFrame >= g_SimTriggerFrame + 150) {
                    SimulateKeyUp(p2_fwd);
                    SimulateKeyUp(p2_back);
                    SimulateKeyUp(VK_DOWN);
                    SimulateKeyUp(VK_UP);
                    SimulateKeyUp(VK_NUMPAD9);
                    SimulateKeyUp(VK_NUMPAD0);
                    SimulateKeyUp(VK_NUMPAD4);
                    SimulateKeyUp('S');
                    SimulateKeyUp('A');
                    SimulateKeyUp('D');
                    SimulateKeyUp('G');
                    SimulateKeyUp(VK_SPACE);
                    g_SimTriggerFrame = 0;
                    g_SimSeqType = MACRO_SEQ_STANDARD;
                    LogTrace("[MODE 10 %s | #%llu] Frame 150: End of observation window (All keys released)", fKeyName, g_CurrentFrame);
                }
            } else if (g_SimSeqType >= MACRO_SEQ_MODE11_F1_JC_EX_FORCEBALL_SOLO && g_SimSeqType <= MACRO_SEQ_MODE11_F8_JC_EX_FORCEBALL_VS_P1_DF1_STAND) {
                bool isSolo = (g_SimSeqType == MACRO_SEQ_MODE11_F1_JC_EX_FORCEBALL_SOLO);
                bool isCrouchBL = (g_SimSeqType == MACRO_SEQ_MODE11_F5_JC_EX_FORCEBALL_VS_P1_DF1_CROUCH || g_SimSeqType == MACRO_SEQ_MODE11_F6_JC_EX_FORCEBALL_VS_P1_DF1_CROUCH);

                int delayF = 0;
                const char* fKeyName = "F1 (Solo Baseline)";
                if (g_SimSeqType == MACRO_SEQ_MODE11_F2_JC_EX_FORCEBALL_VS_P1_DF1_EX) {
                    delayF = 0;
                    fKeyName = "F2 (P1 Stand BL -> D,F+1+BL +0f Delay Frame 94)";
                } else if (g_SimSeqType == MACRO_SEQ_MODE11_F3_JC_EX_FORCEBALL_VS_P1_DF1_EX) {
                    delayF = 5;
                    fKeyName = "F3 (P1 Stand BL -> D,F+1+BL +5f Delay Frame 99)";
                } else if (g_SimSeqType == MACRO_SEQ_MODE11_F4_JC_EX_FORCEBALL_VS_P1_DF1_EX) {
                    delayF = 6;
                    fKeyName = "F4 (P1 Stand BL -> D,F+1+BL +6f Delay Frame 100)";
                } else if (g_SimSeqType == MACRO_SEQ_MODE11_F5_JC_EX_FORCEBALL_VS_P1_DF1_CROUCH) {
                    delayF = 8;
                    fKeyName = "F5 (P1 Crouch BL -> D,F+1+BL +8f Delay Frame 102)";
                } else if (g_SimSeqType == MACRO_SEQ_MODE11_F6_JC_EX_FORCEBALL_VS_P1_DF1_CROUCH) {
                    delayF = 9;
                    fKeyName = "F6 (P1 Crouch BL -> D,F+1+BL +9f Delay Frame 103)";
                } else if (g_SimSeqType == MACRO_SEQ_MODE11_F7_JC_EX_FORCEBALL_VS_P1_DF1_STAND) {
                    delayF = 8;
                    fKeyName = "F7 (P1 Stand BL -> D,F+1+BL +8f Delay Frame 102 [No Crouch])";
                } else if (g_SimSeqType == MACRO_SEQ_MODE11_F8_JC_EX_FORCEBALL_VS_P1_DF1_STAND) {
                    delayF = 9;
                    fKeyName = "F8 (P1 Stand BL -> D,F+1+BL +9f Delay Frame 103 [No Crouch])";
                }

                WORD p2_fwd  = VK_LEFT;
                WORD p2_back = VK_RIGHT;
                WORD p1_back = 'A';
                WORD p1_fwd  = 'D';

                void* p1 = GetP1Char();
                void* p2 = GetP2Char();
                if (p1 && p2 && (uintptr_t)p1 > 0x01000000 && (uintptr_t)p2 > 0x01000000) {
                    __try {
                        float p1_x = *(float*)((uintptr_t)p1 + 0x1E8);
                        float p2_x = *(float*)((uintptr_t)p2 + 0x1E8);
                        if (p2_x < p1_x) { p2_back = VK_LEFT; p2_fwd = VK_RIGHT; }
                        else { p2_back = VK_RIGHT; p2_fwd = VK_LEFT; }

                        if (p1_x < p2_x) { p1_back = 'A'; p1_fwd = 'D'; }
                        else { p1_back = 'D'; p1_fwd = 'A'; }
                    } __except (EXCEPTION_EXECUTE_HANDLER) {}
                }

                // Phase 1: Frame 01 - 30 (30 frames): P2 Hold Left (Forward walk), P1 Hold Block (for F2-F8)
                if (g_CurrentFrame == g_SimTriggerFrame) {
                    SimulateKeyDown(p2_fwd);
                    if (!isSolo) {
                        SimulateKeyDown(VK_SPACE);
                        LogTrace("[MODE 11 %s | #%llu] Frame 01: P1 Hold [Block] (Blocking EX Forceball)", fKeyName, g_CurrentFrame);
                    }
                    LogTrace("[MODE 11 %s | #%llu] Frame 01: P2 Hold [Left/Forward] (30f Walk)", fKeyName, g_CurrentFrame);
                }
                // Phase 2: Frame 31 - 35 (5 frames): Release Left (Neutral)
                if (g_CurrentFrame == g_SimTriggerFrame + 30) {
                    SimulateKeyUp(p2_fwd);
                    LogTrace("[MODE 11 %s | #%llu] Frame 31: P2 Release [Left/Forward] (5f Neutral)", fKeyName, g_CurrentFrame);
                }
                // Phase 3: Frame 36 - 38 (3 frames): Press Down (VK_DOWN)
                if (g_CurrentFrame == g_SimTriggerFrame + 35) {
                    SimulateKeyDown(VK_DOWN);
                    LogTrace("[MODE 11 %s | #%llu] Frame 36: P2 Press [Down] (D,F+2+BL Start)", fKeyName, g_CurrentFrame);
                }
                if (g_CurrentFrame == g_SimTriggerFrame + 38) {
                    SimulateKeyUp(VK_DOWN);
                }
                // Phase 4: Frame 40 - 43 (4 frames): Press Forward + Back Punch [2] + Block (VK_LEFT + VK_NUMPAD9 + VK_NUMPAD0)
                if (g_CurrentFrame == g_SimTriggerFrame + 39) {
                    SimulateKeyDown(p2_fwd);
                    SimulateKeyDown(VK_NUMPAD9); // Back Punch (2)
                    SimulateKeyDown(VK_NUMPAD0); // Block
                    LogTrace("[MODE 11 %s | #%llu] Frame 40: P2 Press [Forward + 2 + Block] (EX Forceball D,F+2+BL)", fKeyName, g_CurrentFrame);
                }
                if (g_CurrentFrame == g_SimTriggerFrame + 43) {
                    SimulateKeyUp(p2_fwd);
                    SimulateKeyUp(VK_NUMPAD9);
                    SimulateKeyUp(VK_NUMPAD0);
                    LogTrace("[MODE 11 %s | #%llu] Frame 44: P2 Release EX Forceball Inputs (Startup 18f, Total 53f)", fKeyName, g_CurrentFrame);
                }

                // Phase 5: Frame 94: P2 Press Forward + 3 (VK_LEFT + VK_NUMPAD4 [FK])
                uint64_t targetP2F3Frame = 94;
                if (g_CurrentFrame == g_SimTriggerFrame + (targetP2F3Frame - 1)) {
                    SimulateKeyDown(p2_fwd);
                    SimulateKeyDown(VK_NUMPAD4); // Front Kick (3)
                    LogTrace("[MODE 11 %s | #%llu] Frame %02llu: P2 Input [Forward + 3] (Left + Num4)",
                             fKeyName, g_CurrentFrame, targetP2F3Frame);

                    // For F5 - F8 (Crouch BL): P1 transitions to Crouch Block as soon as P2 inputs F+3!
                    if (isCrouchBL) {
                        SimulateKeyDown('S'); // Hold Down while Block (VK_SPACE) is held -> Crouch Block!
                        LogTrace("[MODE 11 %s | #%llu] Frame %02llu: P1 Transition to [Down + Block] (Crouch Block on P2 F+3 input)",
                                 fKeyName, g_CurrentFrame, targetP2F3Frame);
                    }
                }
                if (g_CurrentFrame == g_SimTriggerFrame + (targetP2F3Frame + 5)) {
                    SimulateKeyUp(p2_fwd);
                    SimulateKeyUp(VK_NUMPAD4);
                    LogTrace("[MODE 11 %s | #%llu] Frame %02llu: P2 Release [Forward + 3]",
                             fKeyName, g_CurrentFrame, targetP2F3Frame + 6);
                }

                // Phase 6 (For F2 - F8): P1 Responds with D, F + 1 + Block (EX Low Forceball Reversal)
                if (!isSolo) {
                    uint64_t targetP1ReversalFrame = 94 + delayF;

                    // Release Block for 1 frame (3 frames before final input)
                    if (g_CurrentFrame == g_SimTriggerFrame + (targetP1ReversalFrame - 4)) {
                        SimulateKeyUp(VK_SPACE);
                        LogTrace("[MODE 11 %s | #%llu] Frame %02llu: P1 Release [Block] (1 frame release while buffering Down)",
                                 fKeyName, g_CurrentFrame, targetP1ReversalFrame - 3);
                    }
                    // Start buffering Down (S) (2 frames before final input) for Stand BL macros
                    if (!isCrouchBL) {
                        if (g_CurrentFrame == g_SimTriggerFrame + (targetP1ReversalFrame - 3)) {
                            SimulateKeyDown('S');
                            LogTrace("[MODE 11 %s | #%llu] Frame %02llu: P1 Press & Buffer [Down] (S)",
                                     fKeyName, g_CurrentFrame, targetP1ReversalFrame - 2);
                        }
                        if (g_CurrentFrame == g_SimTriggerFrame + (targetP1ReversalFrame - 1)) {
                            SimulateKeyUp('S');
                        }
                    } else {
                        if (g_CurrentFrame == g_SimTriggerFrame + (targetP1ReversalFrame - 1)) {
                            SimulateKeyUp('S');
                        }
                    }

                    // Reversal point: Press and hold Forward + 1 + Block for 15 frames (D,F+1+BL)
                    if (g_CurrentFrame == g_SimTriggerFrame + (targetP1ReversalFrame - 1)) {
                        SimulateKeyDown(p1_fwd);
                        SimulateKeyDown('T'); // Front Punch (1)
                        SimulateKeyDown(VK_SPACE); // Block
                        LogTrace("[MODE 11 %s | #%llu] Frame %02llu: P1 Press & HOLD [Forward + 1 + Block] (D,F+1+BL EX Reversal held 15 frames) [P1 Delay +%df]",
                                 fKeyName, g_CurrentFrame, targetP1ReversalFrame, delayF);
                    }
                    if (g_CurrentFrame == g_SimTriggerFrame + (targetP1ReversalFrame + 14)) {
                        SimulateKeyUp(p1_fwd);
                        SimulateKeyUp('T');
                        SimulateKeyUp(VK_SPACE);
                        if (isCrouchBL) {
                            SimulateKeyUp('S');
                        }
                        LogTrace("[MODE 11 %s | #%llu] Frame %02llu: P1 Release [Forward + 1 + Block] Reversal",
                                 fKeyName, g_CurrentFrame, targetP1ReversalFrame + 15);
                    }
                }

                // Detailed Frame-by-Frame Diagnostic Logging for clash window (Frames 92 to 115)
                if (g_CurrentFrame >= g_SimTriggerFrame + 92 && g_CurrentFrame <= g_SimTriggerFrame + 115) {
                    uint64_t relFrame = (g_CurrentFrame - g_SimTriggerFrame) + 1;
                    if (p1 && p2 && (uintptr_t)p1 > 0x01000000 && (uintptr_t)p2 > 0x01000000) {
                        __try {
                            uint32_t p1_act      = *(uint32_t*)((uintptr_t)p1 + 0x1D4);
                            uint32_t p1_flags2EC = *(uint32_t*)((uintptr_t)p1 + 0x2EC);
                            int      p1_func     = *(int*)((uintptr_t)p1 + 0x2F88);
                            uint32_t p1_hitboxes = *(uint32_t*)((uintptr_t)p1 + 0x7A3C);
                            bool     p1_hasArmor = (p1_flags2EC & 0x800000) != 0;
                            bool     p1_isSpecial= (p1_flags2EC & 0x200) != 0;

                            uint32_t p2_act      = *(uint32_t*)((uintptr_t)p2 + 0x1D4);
                            uint32_t p2_flags2EC = *(uint32_t*)((uintptr_t)p2 + 0x2EC);
                            int      p2_func     = *(int*)((uintptr_t)p2 + 0x2F88);
                            uint32_t p2_hitboxes = *(uint32_t*)((uintptr_t)p2 + 0x7A3C);

                            float p1_meter = 0.0f;
                            float p2_meter = 0.0f;
                            void* pInfo1 = *(void**)((uintptr_t)p1 + 0x7638);
                            void* pInfo2 = *(void**)((uintptr_t)p2 + 0x7638);
                            if (pInfo1 && (uintptr_t)pInfo1 > 0x00400000) p1_meter = *(float*)((uintptr_t)pInfo1 + 0xC14);
                            if (pInfo2 && (uintptr_t)pInfo2 > 0x00400000) p2_meter = *(float*)((uintptr_t)pInfo2 + 0xC14);

                            LogTrace("[MODE 11 %s | Frame %03llu | #%llu] STATE:", fKeyName, relFrame, g_CurrentFrame);
                            LogTrace("   P2 (Attacker): Act=0x%04X | Func=%d | Hitboxes=%u | Flags=0x%08X | Meter=%.2f (%.1f bars)",
                                     p2_act, p2_func, p2_hitboxes, p2_flags2EC, p2_meter, p2_meter * 3.0f);
                            LogTrace("   P1 (Defender): Act=0x%04X | Func=%d | Hitboxes=%u | Flags=0x%08X | Meter=%.2f (%.1f bars) | Armor=%s | Spec=%s",
                                     p1_act, p1_func, p1_hitboxes, p1_flags2EC, p1_meter, p1_meter * 3.0f,
                                     p1_hasArmor ? "YES (0x800000)" : "NO",
                                     p1_isSpecial ? "YES (0x200)" : "NO");
                        } __except (EXCEPTION_EXECUTE_HANDLER) {}
                    }
                }

                // Phase 7: Frame 150: Cleanup and end observation
                if (g_CurrentFrame >= g_SimTriggerFrame + 150) {
                    SimulateKeyUp(p2_fwd);
                    SimulateKeyUp(p2_back);
                    SimulateKeyUp(VK_DOWN);
                    SimulateKeyUp(VK_UP);
                    SimulateKeyUp(VK_NUMPAD9);
                    SimulateKeyUp(VK_NUMPAD0);
                    SimulateKeyUp(VK_NUMPAD4);
                    SimulateKeyUp('S');
                    SimulateKeyUp('A');
                    SimulateKeyUp('D');
                    SimulateKeyUp('T');
                    SimulateKeyUp('G');
                    SimulateKeyUp('J');
                    SimulateKeyUp(VK_SPACE);
                    g_SimTriggerFrame = 0;
                    g_SimSeqType = MACRO_SEQ_STANDARD;
                }
            } else if (g_SimSeqType >= MACRO_SEQ_MODE12_F1_P2_MAX_MASH && g_SimSeqType <= MACRO_SEQ_MODE12_F6_P1_MASH_P2_WALK_4S_BACK_4S_FWD) {
                // Mode 12 is handled exclusively by InputIsolation::OnFrameTick and InputIsolation::GetActiveTestModeGamepad
                // (Runs for full duration followed by neutral settle, then cleans itself up)
            } else if (g_SimSeqType >= MACRO_SEQ_MODE13_F1_QUICK_ROLL && g_SimSeqType <= MACRO_SEQ_MODE13_F4_QUICK_ROLL) {
                if (g_CurrentFrame < g_SimTriggerFrame) {
                    return; // Lead-in wait, do not evaluate elapsed yet to avoid unsigned underflow
                }

                int fIndex = (int)g_SimSeqType - (int)MACRO_SEQ_MODE13_F1_QUICK_ROLL;
                int fNum = fIndex + 1;
                bool isUppercut = (fNum >= 3);
                uint64_t p2Offset = (fNum == 1) ? 42 : (fNum == 2) ? 44 : (fNum == 3) ? 65 : 66;
                uint64_t elapsed = g_CurrentFrame - g_SimTriggerFrame;

                void* p1 = GetP1Char();
                void* p2 = GetP2Char();
                float p1_x = 0.0f, p2_x = 0.0f;
                uint32_t p1_act = 0, p2_act = 0;
                float p2_h = 0.0f;
                uint32_t p1_stance = 0, p2_stance = 0;

                if (p1 && (uintptr_t)p1 > 0x01000000) {
                    __try {
                        p1_x = *(float*)((uintptr_t)p1 + 0x1E8);
                        p1_act = *(uint32_t*)((uintptr_t)p1 + 0x1D4);
                        p1_stance = *(uint32_t*)((uintptr_t)p1 + 0x2E8);
                    } __except (EXCEPTION_EXECUTE_HANDLER) {}
                }
                if (p2 && (uintptr_t)p2 > 0x01000000) {
                    __try {
                        p2_x = *(float*)((uintptr_t)p2 + 0x1E8);
                        p2_act = *(uint32_t*)((uintptr_t)p2 + 0x1D4);
                        p2_h = *(float*)((uintptr_t)p2 + 0x1F0);
                        p2_stance = *(uint32_t*)((uintptr_t)p2 + 0x2E8);
                    } __except (EXCEPTION_EXECUTE_HANDLER) {}
                }

                WORD p1_back = (p1_x < p2_x) ? 'A' : 'D';
                WORD p2_back = (p2_x < p1_x) ? VK_LEFT : VK_RIGHT;
                WORD p2_fwd  = (p2_x < p1_x) ? VK_RIGHT : VK_LEFT;

                static uint32_t s_P2LastAct = 0;
                static float s_P2InitialX = 0.0f;

                // Phase 0: Frame 01 (elapsed == 0) -> Trigger P1 Attack
                if (elapsed == 0) {
                    s_P2LastAct = p2_act;
                    s_P2InitialX = p2_x;

                    if (isUppercut) {
                        SimulateKeyDown('S'); // Down
                        SimulateKeyDown('U'); // BP (2)
                    } else {
                        SimulateKeyDown(p1_back);
                        SimulateKeyDown('J'); // BK (4) for P1 (do NOT press H/K to prevent P2 bleed)
                    }

                    LogTrace("====================================================================");
                    if (isUppercut) {
                        LogTrace("[MODE 13 F%d | #%llu] >>> P1 UPPERCUT (D+2) vs P2 TECH ROLL TEST <<<", fNum, g_CurrentFrame);
                        LogTrace("  Target P2 Roll Offset: +%llu frames from D+2 Start (+%lluf post-hit / Step +%df)",
                                 p2Offset, p2Offset - 9, fNum - 3);
                        LogTrace("  P2 Action: Rocker (Forward -> Back)");
                        LogTrace("  P1 Stance: 0x%05X (%s) | Pos: %.1f",
                                 p1_stance, (p1_stance & 0x80000) ? "SWITCHED STANCE (SS)" : "NORMAL STANCE", p1_x);
                        LogTrace("  P2 Stance: 0x%05X | Pos: %.1f | Facing: %s",
                                 p2_stance, p2_x, (p1_x >= p2_x) ? "RIGHT" : "LEFT");
                        LogTrace("====================================================================");
                        LogTrace("[MODE 13 TIMELINE | #%llu] Frame 01: P1 Press [Down + 2] (Uppercut Start)", g_CurrentFrame);
                    } else {
                        LogTrace("[MODE 13 F%d | #%llu] >>> P1 SWEEP (B+4) vs P2 TECH ROLL TEST <<<", fNum, g_CurrentFrame);
                        LogTrace("  Target P2 Roll Offset: +%llu frames from Sweep Start (+%lluf post-hit)", p2Offset, p2Offset - 14);
                        LogTrace("  P2 Action: Rocker (Forward -> Back)");
                        LogTrace("  P1 Stance: 0x%05X (%s) | Pos: %.1f",
                                 p1_stance, (p1_stance & 0x80000) ? "SWITCHED STANCE (SS)" : "NORMAL STANCE", p1_x);
                        LogTrace("  P2 Stance: 0x%05X | Pos: %.1f | Facing: %s",
                                 p2_stance, p2_x, (p1_x >= p2_x) ? "RIGHT" : "LEFT");
                        LogTrace("====================================================================");
                        LogTrace("[MODE 13 TIMELINE | #%llu] Frame 01: P1 Press [Back + 4] (Sweep Start)", g_CurrentFrame);
                    }
                }

                // Phase 1: Frame 05 (elapsed == 4) -> Release P1 inputs
                if (elapsed == 4) {
                    if (isUppercut) {
                        SimulateKeyUp('S');
                        SimulateKeyUp('U');
                    } else {
                        SimulateKeyUp(p1_back);
                        SimulateKeyUp('J');
                    }
                }

                // Phase 2: Expected Hit Window
                if (isUppercut) {
                    if (elapsed == 9) { // Frame 10 (Startup 9F, Active on Frame 10)
                        LogTrace("[MODE 13 TIMELINE | #%llu] Frame 10: P1 Uppercut Active Window (Hit) | P2 Act: 0x%04X, H: %.2f",
                                 g_CurrentFrame, p2_act, p2_h);
                    }
                } else {
                    if (elapsed == 14) { // Frame 15 (Startup 14F, Active on Frame 15)
                        LogTrace("[MODE 13 TIMELINE | #%llu] Frame 15: P1 Sweep Hit Active Window | P2 Act: 0x%04X, H: %.2f",
                                 g_CurrentFrame, p2_act, p2_h);
                    }
                }

                // Track P2 state changes in real time
                if (elapsed > 0 && p2_act != s_P2LastAct) {
                    LogTrace("[MODE 13 P2 STATE | #%llu (+%02lluf)] 0x%04X -> 0x%04X | Height: %.2f | Pos: %.1f",
                             g_CurrentFrame, elapsed + 1, s_P2LastAct, p2_act, p2_h, p2_x);
                    s_P2LastAct = p2_act;
                }

                // Phase 3: P2 Injects Quick Roll: Forward then Back rocker at offset
                if (elapsed == p2Offset) {
                    SimulateKeyDown(p2_fwd);
                    LogTrace("[MODE 13 TIMELINE | #%llu (+%02lluf)] >>> P2 INJECTS [FORWARD] (Rocker Step 1) | Act: 0x%04X, H: %.2f <<<",
                             g_CurrentFrame, elapsed + 1, p2_act, p2_h);
                }
                if (elapsed == p2Offset + 2) {
                    SimulateKeyUp(p2_fwd);
                    SimulateKeyDown(p2_back);
                    LogTrace("[MODE 13 TIMELINE | #%llu (+%02lluf)] >>> P2 INJECTS [BACK] (Rocker Step 2) | Act: 0x%04X, H: %.2f <<<",
                             g_CurrentFrame, elapsed + 1, p2_act, p2_h);
                }
                if (elapsed == p2Offset + 5) {
                    SimulateKeyUp(p2_back);
                    LogTrace("[MODE 13 TIMELINE | #%llu (+%02lluf)] >>> P2 RELEASES [BACK] (Rocker Complete) <<<",
                             g_CurrentFrame, elapsed + 1);
                }

                // Phase 4: End of observation window (elapsed == 120 for Uppercut, 90 for Sweep)
                uint64_t obsDuration = isUppercut ? 120 : 90;
                if (elapsed >= obsDuration) {
                    ReleaseAllSimulationKeys();
                    float xTravel = fabsf(p2_x - s_P2InitialX);
                    bool movedSignificantly = (xTravel > 80.0f);

                    LogTrace("====================================================================");
                    LogTrace("[MODE 13 F%d RESULT | #%llu] OBSERVATION COMPLETE (Offset: +%lluf)",
                             fNum, g_CurrentFrame, p2Offset);
                    LogTrace("  P2 Final Action: 0x%04X | P2 Travel: %.1f units (Start: %.1f -> End: %.1f)",
                             p2_act, xTravel, s_P2InitialX, p2_x);
                    if (movedSignificantly) {
                        LogTrace("  >>> RESULT: TECH ROLL / QUICK ROLL DETECTED! (Displacement %.1f > 80) <<<", xTravel);
                    } else {
                        LogTrace("  >>> RESULT: NO ROLL / STANDARD GETUP (Displacement %.1f <= 80) <<<", xTravel);
                    }
                    LogTrace("====================================================================\n");

                    g_SimTriggerFrame = 0;
                    g_SimSeqType = MACRO_SEQ_STANDARD;
                }
            } else if (g_SimSeqType == MACRO_SEQ_MODE14_DELAYED_WAKEUP) {
                if (g_CurrentFrame < g_SimTriggerFrame) {
                    return; // Lead-in wait
                }

                int fNum = g_Mode14_ActiveFNum;
                bool isUppercut = (g_Mode14_KnockdownMove == 1);
                int baseFloor = isUppercut ? 67 : 42;
                int relOffset = (fNum > 0) ? GetMode14RelativeOffset(fNum) : g_Mode14_FloorRelOffset;
                uint64_t p2Offset = (uint64_t)(baseFloor + relOffset);
                if (p2Offset < 15) p2Offset = 15;
                uint64_t holdDuration = (uint64_t)g_Mode14_HoldFrames;
                uint64_t elapsed = g_CurrentFrame - g_SimTriggerFrame;

                void* p1 = GetP1Char();
                void* p2 = GetP2Char();
                float p1_x = 0.0f, p2_x = 0.0f;
                uint32_t p1_act = 0, p2_act = 0;
                float p2_h = 0.0f;
                uint32_t p1_stance = 0, p2_stance = 0;

                if (p1 && (uintptr_t)p1 > 0x01000000) {
                    __try {
                        p1_x = *(float*)((uintptr_t)p1 + 0x1E8);
                        p1_act = *(uint32_t*)((uintptr_t)p1 + 0x1D4);
                        p1_stance = *(uint32_t*)((uintptr_t)p1 + 0x2E8);
                    } __except (EXCEPTION_EXECUTE_HANDLER) {}
                }
                if (p2 && (uintptr_t)p2 > 0x01000000) {
                    __try {
                        p2_x = *(float*)((uintptr_t)p2 + 0x1E8);
                        p2_act = *(uint32_t*)((uintptr_t)p2 + 0x1D4);
                        p2_h = *(float*)((uintptr_t)p2 + 0x1F0);
                        p2_stance = *(uint32_t*)((uintptr_t)p2 + 0x2E8);
                    } __except (EXCEPTION_EXECUTE_HANDLER) {}
                }

                WORD p1_back = (p1_x < p2_x) ? 'A' : 'D';

                static uint32_t s_M14P2LastAct = 0;
                static uint32_t s_M14ProneFrames = 0;
                static bool s_M14GroundedReached = false;
                static bool s_M14DownInjected = false;
                static bool s_M14DownReleased = false;

                // Phase 0: Frame 01 (elapsed == 0) -> Trigger P1 Attack
                if (elapsed == 0) {
                    s_M14P2LastAct = p2_act;
                    s_M14ProneFrames = 0;
                    s_M14GroundedReached = false;
                    s_M14DownInjected = false;
                    s_M14DownReleased = false;

                    if (isUppercut) {
                        SimulateKeyDown('S'); // Down
                        SimulateKeyDown('U'); // BP (2) for P1
                    } else {
                        SimulateKeyDown(p1_back);
                        SimulateKeyDown('J'); // BK (4) for P1
                    }

                    char fLabel[16];
                    if (fNum > 0) snprintf(fLabel, sizeof(fLabel), "F%d", fNum);
                    else strcpy_s(fLabel, "CUSTOM");

                    LogTrace("====================================================================");
                    LogTrace("[MODE 14 %s | #%llu] >>> %s vs P2 DELAYED WAKEUP WINDOW TEST <<<",
                             fLabel, g_CurrentFrame, isUppercut ? "P1 UPPERCUT (D+2)" : "P1 SWEEP (B+4)");
                    LogTrace("  Target P2 Down Offset: +%llu frames from Start (+%lluf post-hit) [Timing: %+df rel to Floor Impact]",
                             p2Offset, (p2Offset >= (isUppercut ? 9 : 14)) ? (p2Offset - (isUppercut ? 9 : 14)) : 0, relOffset);
                    LogTrace("  P2 Down Hold Duration: %llu frames (Release at +%lluf)",
                             holdDuration, p2Offset + holdDuration);
                    LogTrace("  P1 Stance: 0x%05X (%s) | Pos: %.1f",
                             p1_stance, (p1_stance & 0x80000) ? "SWITCHED STANCE (SS)" : "NORMAL STANCE", p1_x);
                    LogTrace("  P2 Stance: 0x%05X | Pos: %.1f | Facing: %s",
                             p2_stance, p2_x, (p1_x >= p2_x) ? "RIGHT" : "LEFT");
                    LogTrace("====================================================================");
                    if (isUppercut) {
                        LogTrace("[MODE 14 TIMELINE | #%llu] Frame 01: P1 Press [Down + 2] (Uppercut Start)", g_CurrentFrame);
                    } else {
                        LogTrace("[MODE 14 TIMELINE | #%llu] Frame 01: P1 Press [Back + 4] (Sweep Start)", g_CurrentFrame);
                    }
                }

                // Phase 1: Frame 05 (elapsed == 4) -> Release P1 inputs
                if (elapsed == 4) {
                    if (isUppercut) {
                        SimulateKeyUp('S');
                        SimulateKeyUp('U');
                    } else {
                        SimulateKeyUp(p1_back);
                        SimulateKeyUp('J');
                    }
                }

                // Phase 2: Expected Hit Window (Frame 10 for Uppercut, Frame 15 for Sweep)
                if (isUppercut) {
                    if (elapsed == 9) {
                        LogTrace("[MODE 14 TIMELINE | #%llu] Frame 10: P1 Uppercut Hit Active Window | P2 Act: 0x%04X, H: %.2f",
                                 g_CurrentFrame, p2_act, p2_h);
                    }
                } else {
                    if (elapsed == 14) {
                        LogTrace("[MODE 14 TIMELINE | #%llu] Frame 15: P1 Sweep Hit Active Window | P2 Act: 0x%04X, H: %.2f",
                                 g_CurrentFrame, p2_act, p2_h);
                    }
                }

                // Track P2 state changes in real time
                if (elapsed > 0 && p2_act != s_M14P2LastAct) {
                    const char* stateNote = "";
                    if (p2_act == 0x460D) {
                        stateNote = " >>> [P2 HIT FLOOR / KNOCKDOWN CONTACT] <<<";
                        s_M14GroundedReached = true;
                    } else if (p2_act >= 0x6000 && p2_act <= 0x62FF) {
                        stateNote = " (Prone / Ground State)";
                        s_M14GroundedReached = true;
                    }
                    LogTrace("[MODE 14 P2 STATE | #%llu (+%02lluf)] 0x%04X -> 0x%04X | H: %.2f | Pos: %.1f%s",
                             g_CurrentFrame, elapsed + 1, s_M14P2LastAct, p2_act, p2_h, p2_x, stateNote);
                    s_M14P2LastAct = p2_act;
                }

                // Accumulate grounded prone frames
                if (s_M14GroundedReached && (p2_act == 0x460D || (p2_act >= 0x6000 && p2_act <= 0x62FF) || (p2_act >= 0x4600 && p2_act <= 0x4620))) {
                    s_M14ProneFrames++;
                }

                // Phase 3: P2 Injects Down (VK_DOWN) at target offset
                if (elapsed == p2Offset && !s_M14DownInjected) {
                    SimulateKeyDown(VK_DOWN);
                    s_M14DownInjected = true;
                    LogTrace("[MODE 14 TIMELINE | #%llu (+%02lluf)] >>> P2 INJECTS [HOLD DOWN] (Start %lluf Hold) | Act: 0x%04X, H: %.2f <<<",
                             g_CurrentFrame, elapsed + 1, holdDuration, p2_act, p2_h);
                }

                // Phase 3b: P2 Releases Down after holdDuration
                if (elapsed == p2Offset + holdDuration && !s_M14DownReleased) {
                    SimulateKeyUp(VK_DOWN);
                    s_M14DownReleased = true;
                    LogTrace("[MODE 14 TIMELINE | #%llu (+%02lluf)] >>> P2 RELEASES [HOLD DOWN] (Completed %lluf Hold) | Act: 0x%04X, H: %.2f <<<",
                             g_CurrentFrame, elapsed + 1, holdDuration, p2_act, p2_h);
                }

                // Phase 4: End of observation window (Dynamically extends past Down release)
                uint64_t obsDuration = p2Offset + holdDuration + 30;
                if (elapsed >= obsDuration) {
                    ReleaseAllSimulationKeys();
                    bool isDelayed = (s_M14ProneFrames >= 48); // Normal getup prone duration is ~32-38f; delayed is ~55-65f (up to ~113-118f)

                    char fLabelRes[16];
                    if (fNum > 0) snprintf(fLabelRes, sizeof(fLabelRes), "F%d", fNum);
                    else strcpy_s(fLabelRes, "CUSTOM");

                    LogTrace("====================================================================");
                    LogTrace("[MODE 14 %s RESULT | #%llu] WINDOW PROBE COMPLETE",
                             fLabelRes, g_CurrentFrame);
                    LogTrace("  Knockdown: %s | Relative Offset: %+df (Target: +%lluf) | Hold: %lluf frames",
                             isUppercut ? "P1 Uppercut (D+2)" : "P1 Sweep (B+4)",
                             relOffset, p2Offset, holdDuration);
                    LogTrace("  P2 Final Action: 0x%04X | Height: %.2f | Total Prone Time: %u frames",
                             p2_act, p2_h, s_M14ProneFrames);
                    if (isDelayed) {
                        LogTrace("  >>> RESULT: DELAYED WAKEUP ACCEPTED! [IN BUFFER WINDOW] (Prone Time: %u >= 48 frames) <<<",
                                 s_M14ProneFrames);
                    } else {
                        LogTrace("  >>> RESULT: STANDARD GETUP / BUFFER MISSED [OUTSIDE WINDOW] (Prone Time: %u < 48 frames) <<<",
                                 s_M14ProneFrames);
                    }
                    LogTrace("====================================================================\n");

                    g_SimTriggerFrame = 0;
                    g_SimSeqType = MACRO_SEQ_STANDARD;
                }
            } else if (g_SimSeqType == MACRO_SEQ_MODE15_BLOCKSTUN_DIAGNOSTIC) {
                if (g_CurrentFrame < g_SimTriggerFrame) {
                    return; // Lead-in wait
                }

                uint64_t elapsed = g_CurrentFrame - g_SimTriggerFrame;

                int fNum = g_Mode15_ActiveFNum;
                WORD p1AttackKey = (fNum == 2) ? 'T' :
                                   (fNum == 3) ? 'U' :
                                   (fNum == 4) ? 'G' : 'J';
                const char* p1AttackName = (fNum == 2) ? "FP (Front Punch - 'T')" :
                                           (fNum == 3) ? "BP (Back Punch - 'U')" :
                                           (fNum == 4) ? "FK (Front Kick - 'G')" : "BK (Back Kick - 'J')";

                void* p1 = GetP1Char();
                void* p2 = GetP2Char();
                uint32_t p1_act = 0, p2_act = 0;
                float p1_h = 0.0f, p2_h = 0.0f;
                uint32_t p1_combat = 0, p2_combat = 0;
                float p1_x = 0.0f, p2_x = 0.0f;

                if (p1 && (uintptr_t)p1 > 0x01000000) {
                    __try {
                        p1_act = *(uint32_t*)((uintptr_t)p1 + 0x1D4);
                        p1_h = *(float*)((uintptr_t)p1 + 0x1F0);
                        p1_combat = *(uint32_t*)((uintptr_t)p1 + 0x2EC);
                        p1_x = *(float*)((uintptr_t)p1 + 0x1E8);
                    } __except (EXCEPTION_EXECUTE_HANDLER) {}
                }
                if (p2 && (uintptr_t)p2 > 0x01000000) {
                    __try {
                        p2_act = *(uint32_t*)((uintptr_t)p2 + 0x1D4);
                        p2_h = *(float*)((uintptr_t)p2 + 0x1F0);
                        p2_combat = *(uint32_t*)((uintptr_t)p2 + 0x2EC);
                        p2_x = *(float*)((uintptr_t)p2 + 0x1E8);
                    } __except (EXCEPTION_EXECUTE_HANDLER) {}
                }

                float dist = fabsf(p1_x - p2_x);

                // Check action lockout flag (0x400) and jump states
                bool p1_canAct = ((p1_combat & 0x400) == 0);
                bool p2_canAct = ((p2_combat & 0x400) == 0);
                bool p1_jumping = (p1_act >= 0x2000 && p1_act <= 0x2015) || (p1_h > 10.0f);
                bool p2_jumping = (p2_act >= 0x2000 && p2_act <= 0x2015) || (p2_h > 10.0f);

                static int s_M15P1JumpRel = -1;
                static int s_M15P2JumpRel = -1;
                static uint64_t s_M15BlockImpactFrame = 0;
                static uint32_t s_M15P2LastAct = 0;
                static bool s_M15WasInBlockstun = false;
                static bool s_M15P2BlockReleased = false;
                static float s_M15ImpactDist = 0.0f;
                static float s_M15ExitDist = 0.0f;

                // Move-specific expected hit & block release timing (aligned with Screenshots 1-3)
                uint64_t p2BlockReleaseFallback = (fNum == 2) ? 14 : // FP (11f startup -> hit at ~11-12f -> release at 14f)
                                                  (fNum == 3) ? 15 : // BP (12f startup -> hit at ~12-13f -> release at 15f)
                                                  (fNum == 4) ? 12 : // FK ( 9f startup -> hit at ~9-10f  -> release at 12f)
                                                                24;  // BK (20f startup -> hit at ~21-22f -> release at 24f)

                // Frame 0: P1 Attack pressed + P2 Block (VK_NUMPAD0) pressed
                if (elapsed == 0) {
                    s_M15P1JumpRel = -1;
                    s_M15P2JumpRel = -1;
                    s_M15BlockImpactFrame = 0;
                    s_M15P2LastAct = p2_act;
                    s_M15WasInBlockstun = false;
                    s_M15P2BlockReleased = false;
                    s_M15ImpactDist = dist;
                    s_M15ExitDist = dist;

                    if (fNum == 1) g_P1_LastAttackToken = 0x80000012;      // BK
                    else if (fNum == 2) g_P1_LastAttackToken = 0x8000000F; // FP
                    else if (fNum == 3) g_P1_LastAttackToken = 0x80000010; // BP
                    else if (fNum == 4) g_P1_LastAttackToken = 0x80000011; // FK

                    SimulateKeyDown(p1AttackKey);   // P1 Selected Attack
                    SimulateKeyDown(VK_NUMPAD0);    // P2 Block (Numpad 0)

                    LogTrace("====================================================================");
                    LogTrace("[MODE 15 F%d | #%llu (+00f)] >>> STARTING BLOCKSTUN & PRIORITY PINPOINT DIAGNOSTIC <<<", fNum, g_CurrentFrame);
                    LogTrace("  P1 Action: %s (held 2f) -> Hold Up ('W') for 80f (Extended Hold)", p1AttackName);
                    LogTrace("  P2 Action: Block (VK_NUMPAD0, release at impact+2f) -> Hold Up (VK_UP) for 50f");
                    LogTrace("  P1 Initial Act: 0x%04X | P2 Initial Act: 0x%04X | Dist: %.2f", p1_act, p2_act, dist);
                    LogTrace("====================================================================");
                }

                // Frame 2: P1 releases Attack (held for 2 frames) and immediately holds Up ('W') for 80 frames
                if (elapsed == 2) {
                    SimulateKeyUp(p1AttackKey);
                    SimulateKeyDown('W');           // P1 Jump (Up)
                    LogTrace("[MODE 15 F%d | #%llu (+02f)] >>> P1 RELEASES %s (2F HELD) -> HOLDS UP ('W') FOR 80 FRAMES <<<", fNum, g_CurrentFrame, p1AttackName);
                }

                // Continuously keep Up asserted until jump takeoff is established + 4f
                if (s_M15P1JumpRel != -1 && elapsed >= s_M15P1JumpRel + 4) {
                    SimulateKeyUp('W');
                } else if (elapsed >= 2 && elapsed <= 82) {
                    SimulateKeyDown('W');
                }

                // P2 releases Block 2 frames after attack impact (or at tailored fallback elapsed frame)
                bool shouldReleaseBlock = !s_M15P2BlockReleased && 
                                          ((s_M15BlockImpactFrame > 0 && g_CurrentFrame >= s_M15BlockImpactFrame + 2) ||
                                           (elapsed >= p2BlockReleaseFallback && s_M15BlockImpactFrame > 0));

                if (shouldReleaseBlock) {
                    s_M15P2BlockReleased = true;
                    SimulateKeyUp(VK_NUMPAD0);
                    SimulateKeyDown(VK_UP);         // P2 Jump (Up)
                    LogTrace("[MODE 15 F%d | #%llu (+%02lluf)] >>> P2 RELEASES BLOCK (Impact + 2f) -> HOLDS UP (VK_UP) FOR 50 FRAMES <<<", fNum, g_CurrentFrame, elapsed);
                }

                // Continuously keep P2 Up asserted until jump takeoff is established + 4f (prevents repeat jump upon landing)
                if (s_M15P2JumpRel != -1 && elapsed >= s_M15P2JumpRel + 4) {
                    SimulateKeyUp(VK_UP);
                } else if (s_M15P2BlockReleased && elapsed <= 80) {
                    SimulateKeyDown(VK_UP);
                }

                // Real-time P2 Action State change tracking
                if (elapsed > 0 && p2_act != s_M15P2LastAct) {
                    const char* actName = "";
                    if (p2_act == 0x0A01) actName = " [GUARD / BLOCK STANCE]";
                    else if (p2_act >= 0x0A02 && p2_act <= 0x0A0F) actName = " [BLOCK REACTION RECOIL]";
                    else if (p2_act >= 0x2000 && p2_act <= 0x2015) actName = " [JUMP TAKEOFF / AIRBORNE]";
                    else if (p2_act == 0x0000) actName = " [NEUTRAL / IDLE]";

                    LogTrace("[MODE 15 P2 STATE | #%llu (+%02lluf)] 0x%04X -> 0x%04X%s | Stun: %d | CanAct: %d | H: %.2f | Dist: %.2f",
                             g_CurrentFrame, elapsed, s_M15P2LastAct, p2_act, actName,
                             g_P2_StunTracker.inStun ? 1 : 0, p2_canAct ? 1 : 0, p2_h, dist);
                    s_M15P2LastAct = p2_act;
                }

                // Track block impact & blockstun transitions
                if (g_P2_StunTracker.inStun && g_P2_StunTracker.isBlockstun) {
                    if (s_M15BlockImpactFrame == 0) {
                        s_M15BlockImpactFrame = g_CurrentFrame;
                        s_M15ImpactDist = dist;
                        LogTrace("[MODE 15 BLOCK IMPACT | #%llu (+%02lluf)] >>> P2 ENTERED BLOCKSTUN <<< (Authored: %u frames, Contact Dist: %.2f units)",
                                 g_CurrentFrame, elapsed, g_P2_StunTracker.authoredFrames, dist);
                    }
                    s_M15WasInBlockstun = true;
                } else if (s_M15WasInBlockstun && (!g_P2_StunTracker.inStun || !g_P2_StunTracker.isBlockstun)) {
                    s_M15ExitDist = dist;
                    uint64_t actualDuration = (s_M15BlockImpactFrame > 0 && g_CurrentFrame >= s_M15BlockImpactFrame) ?
                                              (g_CurrentFrame - s_M15BlockImpactFrame) : 0;
                    LogTrace("[MODE 15 BLOCKSTUN END | #%llu (+%02lluf)] >>> P2 EXITED BLOCKSTUN <<< (Actual Stun Duration: %lluf, Exit Dist: %.2f units, Net Pushback: %+.2f units)",
                             g_CurrentFrame, elapsed, actualDuration, dist, s_M15ExitDist - s_M15ImpactDist);
                    s_M15WasInBlockstun = false;
                }

                // Track first actionable jump frame for P1 and P2
                if (elapsed >= 2 && p1_jumping && s_M15P1JumpRel == -1) {
                    s_M15P1JumpRel = (int)elapsed;
                    LogTrace("[MODE 15 P1 ACTIONABLE | #%llu (+%02lluf)] P1 Jump Takeoff Detected (Act: 0x%04X, H: %.2f)",
                             g_CurrentFrame, elapsed, p1_act, p1_h);
                }
                if (s_M15P2BlockReleased && p2_jumping && s_M15P2JumpRel == -1) {
                    s_M15P2JumpRel = (int)elapsed;
                    LogTrace("[MODE 15 P2 ACTIONABLE | #%llu (+%02lluf)] P2 Jump Takeoff Detected (Act: 0x%04X, H: %.2f)",
                             g_CurrentFrame, elapsed, p2_act, p2_h);
                }

                // Frame-by-frame diagnostic logging
                if (elapsed <= 85) {
                    LogTrace("[DIAG M15 | F%+03lld | #%llu] P1: Act=0x%04X CanAct=%d H=%.1f Jump=%s | P2: Act=0x%04X CanAct=%d H=%.1f Jump=%s | Dist=%.1f | Stun=%d Auth=%d",
                             elapsed, g_CurrentFrame,
                             p1_act, p1_canAct ? 1 : 0, p1_h, p1_jumping ? "YES" : "no",
                             p2_act, p2_canAct ? 1 : 0, p2_h, p2_jumping ? "YES" : "no",
                             dist, g_P2_StunTracker.inStun ? 1 : 0, g_P2_StunTracker.authoredFrames);
                }

                // Frame 75: P2 finishes 50f Up hold
                if (elapsed == 75) {
                    SimulateKeyUp(VK_UP);
                }

                // Frame 82: P1 finishes 80f Up hold
                if (elapsed == 82) {
                    SimulateKeyUp('W');
                }

                // Frame 85: Finish diagnostic sequence
                if (elapsed >= 85) {
                    SimulateKeyUp('W');
                    SimulateKeyUp(VK_UP);
                    ReleaseAllSimulationKeys();

                    LogTrace("====================================================================");
                    LogTrace("[MODE 15 F%d RESULT | #%llu] BLOCKSTUN PINPOINT COMPLETE (%s)", fNum, g_CurrentFrame, p1AttackName);
                    LogTrace("  Block Impact Frame:   #%llu", s_M15BlockImpactFrame);
                    LogTrace("  P1 Jump Takeoff Rel:  F%+d (Elapsed: %df)", s_M15P1JumpRel, s_M15P1JumpRel);
                    LogTrace("  P2 Jump Takeoff Rel:  F%+d (Elapsed: %df)", s_M15P2JumpRel, s_M15P2JumpRel);
                    if (s_M15P1JumpRel != -1 && s_M15P2JumpRel != -1) {
                        int diff = s_M15P1JumpRel - s_M15P2JumpRel;
                        LogTrace("  Physical Priority:    %s jumped first by %d frames (Advantage: %+df)",
                                 (diff < 0) ? "P1" : ((diff > 0) ? "P2" : "TIED"),
                                 abs(diff), -diff);
                    }
                    LogTrace("  Block Spacing:        Contact: %.2f units -> Exit: %.2f units (Net Pushback: %+.2f units)",
                             s_M15ImpactDist, s_M15ExitDist, s_M15ExitDist - s_M15ImpactDist);
                    LogTrace("  Custom Pushback:      %s (Multiplier: %.2fx)",
                             MK9TradeFix::StunModifier::g_EnableCustomBlockPushback ? "ACTIVE" : "VANILLA (1.00x)",
                             MK9TradeFix::StunModifier::g_BlockPushbackMultiplier);
                    LogTrace("====================================================================\n");

                    g_SimTriggerFrame = 0;
                    g_SimSeqType = MACRO_SEQ_STANDARD;
                }
            } else if (g_SimSeqType == MACRO_SEQ_MODE16_UP_TAP_DIAGNOSTIC) {
                if (g_CurrentFrame < g_SimTriggerFrame) {
                    return; // Lead-in wait
                }

                uint64_t elapsed = g_CurrentFrame - g_SimTriggerFrame;
                int fNum = g_Mode16_ActiveFNum;
                if (fNum < 1) fNum = 1;
                if (fNum > 4) fNum = 4;

                uint8_t p1AttackKey = (fNum == 1) ? 'T' :
                                      (fNum == 2) ? 'U' :
                                      (fNum == 3) ? 'G' : 'J';
                const char* p1AttackName = (fNum == 1) ? "FP (Front Punch - 'T')" :
                                           (fNum == 2) ? "BP (Back Punch - 'U')" :
                                           (fNum == 3) ? "FK (Front Kick - 'G')" : "BK (Back Kick - 'J')";

                void* p1 = GetP1Char();
                void* p2 = GetP2Char();
                uint32_t p1_act = 0, p2_act = 0;
                float p1_x = 0.0f, p2_x = 0.0f;
                uint32_t p1_combat = 0, p2_combat = 0;
                float p2_velX = 0.0f;

                if (p1 && (uintptr_t)p1 > 0x01000000) {
                    __try {
                        p1_act = *(uint32_t*)((uintptr_t)p1 + 0x1D4);
                        p1_x = *(float*)((uintptr_t)p1 + 0x1E8);
                        p1_combat = *(uint32_t*)((uintptr_t)p1 + 0x2EC);
                    } __except (EXCEPTION_EXECUTE_HANDLER) {}
                }
                if (p2 && (uintptr_t)p2 > 0x01000000) {
                    __try {
                        p2_act = *(uint32_t*)((uintptr_t)p2 + 0x1D4);
                        p2_x = *(float*)((uintptr_t)p2 + 0x1E8);
                        p2_combat = *(uint32_t*)((uintptr_t)p2 + 0x2EC);
                        p2_velX = *(float*)((uintptr_t)p2 + 0x2F74);
                    } __except (EXCEPTION_EXECUTE_HANDLER) {}
                }

                bool p1_canAct = ((p1_combat & 0x400) == 0);
                bool p2_canAct = ((p2_combat & 0x400) == 0);
                float dist = fabsf(p1_x - p2_x);

                static float s_M16InitialDist = 0.0f;
                static float s_M16P1InitialX = 0.0f;
                static float s_M16P2InitialX = 0.0f;
                static float s_M16ImpactDist = 0.0f;
                static uint64_t s_M16ImpactFrame = 0;
                static uint64_t s_M16ImpactRel = 0;
                static float s_M16ExitDist = 0.0f;
                static uint64_t s_M16ExitFrame = 0;
                static uint64_t s_M16ExitRel = 0;
                static float s_M16PrevDist = 0.0f;
                static bool s_M16WasInBlockstun = false;
                static bool s_M16P2BlockReleased = false;
                static float s_M16P1ImpactX = 0.0f;
                static float s_M16P2ImpactX = 0.0f;
                static uint32_t s_M16P2LastAct = 0;

                // Frame 0: P1 Attack pressed + P2 Block (VK_NUMPAD0) pressed
                if (elapsed == 0) {
                    MK9TradeFix::StunModifier::ResetBlockPushback();
                    s_M16InitialDist = dist;
                    s_M16P1InitialX = p1_x;
                    s_M16P2InitialX = p2_x;
                    s_M16ImpactDist = 0.0f;
                    s_M16ImpactFrame = 0;
                    s_M16ImpactRel = 0;
                    s_M16ExitDist = 0.0f;
                    s_M16ExitFrame = 0;
                    s_M16ExitRel = 0;
                    s_M16PrevDist = dist;
                    s_M16WasInBlockstun = false;
                    s_M16P2BlockReleased = false;
                    s_M16P1ImpactX = p1_x;
                    s_M16P2ImpactX = p2_x;
                    s_M16P2LastAct = p2_act;

                    if (fNum == 1) g_P1_LastAttackToken = 0x8000000F;      // FP
                    else if (fNum == 2) g_P1_LastAttackToken = 0x80000010; // BP
                    else if (fNum == 3) g_P1_LastAttackToken = 0x80000011; // FK
                    else if (fNum == 4) g_P1_LastAttackToken = 0x80000012; // BK

                    SimulateKeyDown(p1AttackKey);   // P1 Attack
                    SimulateKeyDown(VK_NUMPAD0);    // P2 Block

                    LogTrace("====================================================================");
                    LogTrace("[MODE 16 F%d | #%llu (+00f)] >>> STARTING BLOCKSTUN DISTANCE & PUSHBACK MEASUREMENT <<<", fNum, g_CurrentFrame);
                    LogTrace("  P1 Action: %s (Key: '%c', Held 2f)", p1AttackName, (char)p1AttackKey);
                    LogTrace("  P2 Action: Block (VK_NUMPAD0, Held through blockstun)");
                    LogTrace("  Initial Spacing: Dist=%.2f units | P1 X=%.2f | P2 X=%.2f", dist, p1_x, p2_x);
                    LogTrace("  Custom Pushback: %s | Multiplier: %.2fx | Corner Reflect: %s",
                             MK9TradeFix::StunModifier::g_EnableCustomBlockPushback ? "ENABLED" : "VANILLA (OFF)",
                             MK9TradeFix::StunModifier::g_BlockPushbackMultiplier,
                             MK9TradeFix::StunModifier::g_CornerPushbackReflection ? "ON" : "OFF");
                    LogTrace("====================================================================");
                }

                // Frame 2: P1 releases Attack key
                if (elapsed == 2) {
                    SimulateKeyUp(p1AttackKey);
                }

                // Action change logging
                if (elapsed > 0 && p2_act != s_M16P2LastAct) {
                    const char* actDesc = "";
                    if (p2_act == 0x0A01) actDesc = " [GUARD STANCE]";
                    else if (p2_act >= 0x0A02 && p2_act <= 0x0A0F) actDesc = " [BLOCK REACTION RECOIL]";
                    else if (p2_act == 0x4F00) actDesc = " [CROUCH BLOCK]";
                    else if (p2_act == 0x0000) actDesc = " [NEUTRAL / IDLE]";

                    LogTrace("[MODE 16 P2 STATE | #%llu (+%02lluf)] 0x%04X -> 0x%04X%s | Dist=%.2f | VelX=%+.2f",
                             g_CurrentFrame, elapsed, s_M16P2LastAct, p2_act, actDesc, dist, p2_velX);
                    s_M16P2LastAct = p2_act;
                }

                // Detect Block Impact
                bool inBlockstunNow = (g_P2_StunTracker.inStun && g_P2_StunTracker.isBlockstun) ||
                                      (p2_act >= 0x0A02 && p2_act <= 0x0A0F);
                if (inBlockstunNow) {
                    if (s_M16ImpactFrame == 0) {
                        s_M16ImpactFrame = g_CurrentFrame;
                        s_M16ImpactRel = elapsed;
                        s_M16ImpactDist = dist;
                        s_M16P1ImpactX = p1_x;
                        s_M16P2ImpactX = p2_x;
                        LogTrace("[MODE 16 BLOCK IMPACT | #%llu (+%02lluf)] >>> P2 ENTERED BLOCKSTUN <<< Dist=%.2f units | P1 X=%.2f, P2 X=%.2f | P2 Act=0x%04X",
                                 g_CurrentFrame, elapsed, dist, p1_x, p2_x, p2_act);
                    }
                    s_M16WasInBlockstun = true;
                } else if (s_M16WasInBlockstun && !inBlockstunNow) {
                    // Blockstun Exit
                    s_M16ExitFrame = g_CurrentFrame;
                    s_M16ExitRel = elapsed;
                    s_M16ExitDist = dist;
                    float stunPushback = s_M16ExitDist - s_M16ImpactDist;
                    uint64_t stunDuration = (s_M16ExitFrame >= s_M16ImpactFrame) ? (s_M16ExitFrame - s_M16ImpactFrame) : 0;
                    LogTrace("[MODE 16 BLOCKSTUN END | #%llu (+%02lluf)] >>> P2 EXITED BLOCKSTUN <<< (Duration: %lluf, Exit Dist: %.2f units, Net Pushback: %+.2f units)",
                             g_CurrentFrame, elapsed, stunDuration, dist, stunPushback);
                    s_M16WasInBlockstun = false;
                    s_M16P2BlockReleased = true;
                    SimulateKeyUp(VK_NUMPAD0); // Release block
                }

                // Frame-by-frame distance telemetry
                float stepDelta = dist - s_M16PrevDist;
                if (elapsed <= 75) {
                    LogTrace("[DIAG M16 | F%+03lld | #%llu] Dist=%6.2f (Δ=%+6.2f) | P1 X=%7.2f, P2 X=%7.2f | P2: Act=0x%04X CanAct=%d VelX=%+6.2f | Stun=%d (%s)",
                             elapsed, g_CurrentFrame,
                             dist, stepDelta,
                             p1_x, p2_x,
                             p2_act, p2_canAct ? 1 : 0, p2_velX,
                             g_P2_StunTracker.inStun ? 1 : 0,
                             inBlockstunNow ? "BLOCKSTUN" : (s_M16P2BlockReleased ? "RECOVERED" : "NEUTRAL/STARTUP"));
                }
                s_M16PrevDist = dist;

                // Safety: release block at elapsed 50 if still held
                if (elapsed == 50 && !s_M16P2BlockReleased) {
                    SimulateKeyUp(VK_NUMPAD0);
                    s_M16P2BlockReleased = true;
                }

                // Finish diagnostic sequence
                bool isDone = (s_M16ExitFrame > 0 && elapsed >= s_M16ExitRel + 10) || (elapsed >= 80);
                if (isDone) {
                    SimulateKeyUp(p1AttackKey);
                    SimulateKeyUp(VK_NUMPAD0);
                    ReleaseAllSimulationKeys();

                    float finalDist = dist;
                    float blockstunPushback = (s_M16ImpactFrame > 0 && s_M16ExitFrame > 0) ? (s_M16ExitDist - s_M16ImpactDist) : 0.0f;
                    float totalPushback = finalDist - s_M16InitialDist;
                    uint64_t stunDuration = (s_M16ExitFrame >= s_M16ImpactFrame && s_M16ImpactFrame > 0) ? (s_M16ExitFrame - s_M16ImpactFrame) : 0;
                    float ratePerFrame = (stunDuration > 0) ? (blockstunPushback / (float)stunDuration) : 0.0f;

                    LogTrace("====================================================================");
                    LogTrace("[MODE 16 F%d RESULT | #%llu] BLOCKSTUN DISTANCE & PUSHBACK COMPLETE (%s)", fNum, g_CurrentFrame, p1AttackName);
                    LogTrace("--------------------------------------------------------------------");
                    LogTrace("  Initial Distance (At Move Start):    %6.2f units (P1 X: %.2f | P2 X: %.2f)", s_M16InitialDist, s_M16P1InitialX, s_M16P2InitialX);
                    LogTrace("  Contact Distance (At Block Impact):  %6.2f units (F%+02llu, Frame #%llu, P1 X: %.2f | P2 X: %.2f)", s_M16ImpactDist, s_M16ImpactRel, s_M16ImpactFrame, s_M16P1ImpactX, s_M16P2ImpactX);
                    LogTrace("  Exit Distance (At Blockstun End):    %6.2f units (F%+02llu, Frame #%llu)", s_M16ExitDist, s_M16ExitRel, s_M16ExitFrame);
                    LogTrace("  Final Distance (At Test End):        %6.2f units (P1 X: %.2f | P2 X: %.2f)", finalDist, p1_x, p2_x);
                    LogTrace("--------------------------------------------------------------------");
                    LogTrace("  Net Blockstun Pushback Delta:        %+6.2f units (Over %llu frames)", blockstunPushback, stunDuration);
                    LogTrace("  Total Exchange Pushback Delta:       %+6.2f units", totalPushback);
                    LogTrace("  Average Pushback Rate:               %6.2f units/frame", ratePerFrame);
                    LogTrace("  Custom Pushback Setting:             %s (Multiplier: %.2fx)",
                             MK9TradeFix::StunModifier::g_EnableCustomBlockPushback ? "ACTIVE" : "VANILLA (1.00x)",
                             MK9TradeFix::StunModifier::g_BlockPushbackMultiplier);
                    LogTrace("====================================================================\n");

                    g_SimTriggerFrame = 0;
                    g_SimSeqType = MACRO_SEQ_STANDARD;
                }
            } else if (g_SimSeqType == MACRO_SEQ_MODE17_ATTACK_MEASUREMENT) {
                if (g_CurrentFrame < g_SimTriggerFrame) {
                    return; // Lead-in wait
                }

                uint64_t elapsed = g_CurrentFrame - g_SimTriggerFrame;
                int fNum = g_Mode17_ActiveFNum;
                if (fNum < 1) fNum = 1;
                if (fNum > 4) fNum = 4;

                WORD p1AttackKey = (fNum == 1) ? 'T' :
                                   (fNum == 2) ? 'U' :
                                   (fNum == 3) ? 'G' : 'J';
                const char* p1AttackName = (fNum == 1) ? "FP (Front Punch - 'T')" :
                                           (fNum == 2) ? "BP (Back Punch - 'U')" :
                                           (fNum == 3) ? "FK (Front Kick - 'G')" : "BK (Back Kick - 'J')";

                void* p1 = GetP1Char();
                uint32_t p1_act = 0;
                float p1_h = 0.0f;
                uint32_t p1_combat = 0;
                uint32_t hitboxes = 0;
                int scriptFunc = -1;
                uintptr_t scriptPkg = 0;
                uint32_t stance = 0;
                uint32_t subState = 0;
                int32_t moveRes = 0;
                uint32_t recAtkType = 0;
                uint32_t animFlags = 0;
                uint32_t activeVols = 0;

                if (p1 && (uintptr_t)p1 > 0x01000000) {
                    __try {
                        p1_act = *(uint32_t*)((uintptr_t)p1 + 0x1D4);
                        p1_h = *(float*)((uintptr_t)p1 + 0x1F0);
                        p1_combat = *(uint32_t*)((uintptr_t)p1 + 0x2EC);
                        hitboxes = *(uint32_t*)((uintptr_t)p1 + 0x7A3C);
                        scriptPkg = *(uintptr_t*)((uintptr_t)p1 + 0x2F84);
                        scriptFunc = *(int*)((uintptr_t)p1 + 0x2F88);
                        stance = *(uint32_t*)((uintptr_t)p1 + 0x2E8);

                        void* pInfo = *(void**)((uintptr_t)p1 + 0x7638);
                        if (pInfo && (uintptr_t)pInfo >= 0x00400000) {
                            uint32_t curr = *(uint32_t*)((uintptr_t)pInfo + 0x574);
                            if (curr == 0) curr = *(uint32_t*)((uintptr_t)pInfo + 0x570);
                            if (curr > 0) {
                                uintptr_t pRec = (uintptr_t)pInfo + 0x48 + (curr - 1) * 0x2C;
                                moveRes = *(int32_t*)(pRec + 0x1C);
                                recAtkType = *(uint32_t*)(pRec + 0x20);
                                subState = *(uint32_t*)(pRec + 0x24);
                            }
                            void* pAnimPlayer = *(void**)((uintptr_t)pInfo + 0xB40);
                            if (pAnimPlayer && (uintptr_t)pAnimPlayer > 0x01000000) {
                                animFlags = *(uint32_t*)((uintptr_t)pAnimPlayer + 0xC);
                            }
                        }

                        void* pColInfo = *(void**)((uintptr_t)p1 + 0x2F10);
                        if (pColInfo && (uintptr_t)pColInfo > 0x01000000) {
                            uint32_t* pVolList = *(uint32_t**)((uintptr_t)pColInfo + 0x14);
                            if (pVolList && (uintptr_t)pVolList > 0x01000000) {
                                activeVols = pVolList[0];
                            }
                        }
                    } __except (EXCEPTION_EXECUTE_HANDLER) {}
                }

                bool p1_canAct = ((p1_combat & 0x400) == 0);
                bool isHitboxActive = (hitboxes > 0) || (activeVols > 0) || ((animFlags & 0x02) != 0);

                static int s_M17AttackStartRel = -1;
                static int s_M17FirstActiveRel = -1;
                static int s_M17LastActiveRel = -1;
                static int s_M17RecoveryStartRel = -1;
                static int s_M17MoveEndRel = -1;
                static uint32_t s_M17InitialAct = 0;
                static uint32_t s_M17LastAct = 0;
                static uint32_t s_M17MaxHitboxes = 0;
                static uint32_t s_M17PeakVols = 0;
                static int s_M17ScriptFunc = -1;
                static float s_M17BaseDamage = 0.0f;
                static int s_M17ReactionID = 0;

                // Frame 0: P1 Attack Key Pressed
                if (elapsed == 0) {
                    s_M17AttackStartRel = -1;
                    s_M17FirstActiveRel = -1;
                    s_M17LastActiveRel = -1;
                    s_M17RecoveryStartRel = -1;
                    s_M17MoveEndRel = -1;
                    s_M17InitialAct = p1_act;
                    s_M17LastAct = p1_act;
                    s_M17MaxHitboxes = 0;
                    s_M17PeakVols = 0;
                    s_M17ScriptFunc = -1;
                    s_M17BaseDamage = 0.0f;
                    s_M17ReactionID = 0;

                    if (fNum == 1)      g_P1_LastAttackToken = 0x8000000F; // FP
                    else if (fNum == 2) g_P1_LastAttackToken = 0x80000010; // BP
                    else if (fNum == 3) g_P1_LastAttackToken = 0x80000011; // FK
                    else if (fNum == 4) g_P1_LastAttackToken = 0x80000012; // BK

                    SimulateKeyDown(p1AttackKey);

                    LogTrace("====================================================================");
                    LogTrace("[MODE 17 F%d | #%llu (+00f)] >>> STARTING ATTACK FRAME DATA MEASUREMENT <<<", fNum, g_CurrentFrame);
                    LogTrace("  P1 Attack: %s (Key: '%c', Held 2f)", p1AttackName, (char)p1AttackKey);
                    LogTrace("  P1 Initial Act: 0x%04X | CanAct: %d | ScriptFunc: %d | Token: 0x%08X",
                             p1_act, p1_canAct ? 1 : 0, scriptFunc, g_P1_LastAttackToken);
                    LogTrace("====================================================================");
                }

                // Frame 2: Release attack key
                if (elapsed == 2) {
                    SimulateKeyUp(p1AttackKey);
                }

                // Detect Attack Initiation
                if (s_M17AttackStartRel == -1 && (p1_act != s_M17InitialAct || !p1_canAct || scriptFunc != -1 || isHitboxActive)) {
                    s_M17AttackStartRel = (int)elapsed;
                    s_M17ScriptFunc = scriptFunc;
                    QueryCharacterAttackInfo(p1, &s_M17BaseDamage, &s_M17ReactionID);
                }
                if (s_M17ScriptFunc == -1 && scriptFunc != -1) {
                    s_M17ScriptFunc = scriptFunc;
                    QueryCharacterAttackInfo(p1, &s_M17BaseDamage, &s_M17ReactionID);
                }

                // Detect Active Phase (Hitboxes or Physical Collision Volumes deployed)
                if (isHitboxActive) {
                    if (s_M17FirstActiveRel == -1) {
                        s_M17FirstActiveRel = (int)elapsed;
                    }
                    s_M17LastActiveRel = (int)elapsed;
                    if (hitboxes > s_M17MaxHitboxes) s_M17MaxHitboxes = hitboxes;
                    if (activeVols > s_M17PeakVols) s_M17PeakVols = activeVols;
                } else if (s_M17FirstActiveRel != -1 && s_M17RecoveryStartRel == -1) {
                    // First frame after hitboxes despawn = start of recovery
                    s_M17RecoveryStartRel = (int)elapsed;
                }

                // Detect End of Move (Return to neutral/idle stance and actionable)
                if (s_M17FirstActiveRel != -1 && elapsed > (uint64_t)s_M17FirstActiveRel) {
                    bool returnedToNeutral = (p1_act == 0x0000 || p1_act == 0x0100 || p1_act == 0x0101) && p1_canAct;
                    if (returnedToNeutral && s_M17MoveEndRel == -1) {
                        s_M17MoveEndRel = (int)elapsed;
                    }
                }

                // Classify current frame phase
                const char* phaseStr = "STARTUP";
                if (s_M17MoveEndRel != -1) {
                    phaseStr = "NEUTRAL";
                } else if (s_M17RecoveryStartRel != -1) {
                    phaseStr = "RECOVERY";
                } else if (isHitboxActive) {
                    phaseStr = "ACTIVE";
                } else {
                    phaseStr = "STARTUP";
                }

                // Frame-by-frame diagnostic telemetry (frames 0 to 110)
                if (elapsed <= 110) {
                    LogTrace("[DIAG M17 | F%+03lld | #%llu] Phase: %-8s | Act=0x%04X CanAct=%d | Hitbox=%u Vols=%u AnimFlg=0x%02X | SubSt=%u Func=%d",
                             elapsed, g_CurrentFrame,
                             phaseStr,
                             p1_act, p1_canAct ? 1 : 0,
                             hitboxes, activeVols, animFlags,
                             subState, scriptFunc);
                }

                // Finish diagnostic sequence: when return to neutral confirmed for 3 frames, or ceiling at 120 frames
                bool isComplete = (s_M17MoveEndRel != -1 && elapsed >= (uint64_t)(s_M17MoveEndRel + 3)) || (elapsed >= 120);
                if (isComplete) {
                    SimulateKeyUp(p1AttackKey);
                    ReleaseAllSimulationKeys();

                    int startupFrames = (s_M17FirstActiveRel != -1) ? s_M17FirstActiveRel : 0;
                    int activeFrames = (s_M17FirstActiveRel != -1 && s_M17LastActiveRel >= s_M17FirstActiveRel) ?
                                       (s_M17LastActiveRel - s_M17FirstActiveRel + 1) : 0;
                    int recoveryFrames = (s_M17MoveEndRel != -1 && s_M17LastActiveRel != -1 && s_M17MoveEndRel > s_M17LastActiveRel) ?
                                         (s_M17MoveEndRel - s_M17LastActiveRel - 1) : 0;
                    int totalDuration = (s_M17MoveEndRel != -1) ? s_M17MoveEndRel : (int)elapsed;

                    LogTrace("====================================================================");
                    LogTrace("[MODE 17 F%d RESULT | #%llu] ATTACK FRAME DATA MEASUREMENT COMPLETE", fNum, g_CurrentFrame);
                    LogTrace("--------------------------------------------------------------------");
                    LogTrace("  Move Name:             %s", p1AttackName);
                    LogTrace("  Input Key:             '%c' (Held 2f)", (char)p1AttackKey);
                    LogTrace("  Script Function:       %d", (s_M17ScriptFunc != -1) ? s_M17ScriptFunc : scriptFunc);
                    LogTrace("  Authored Damage:       %.1f%% (Reaction ID: %d)", s_M17BaseDamage * 100.0f, s_M17ReactionID);
                    LogTrace("  Peak Hitbox Volumes:   %u (Physical Volumes: %u)", s_M17MaxHitboxes, s_M17PeakVols);
                    LogTrace("--------------------------------------------------------------------");
                    LogTrace("  Timeline (Relative to Input Button Press at F+00):");
                    LogTrace("    Startup Phase:       F+00 .. F%+02d (%d frame%s until first active frame)",
                             (startupFrames > 0) ? (startupFrames - 1) : 0, startupFrames, (startupFrames == 1) ? "" : "s");
                    if (s_M17FirstActiveRel != -1) {
                        LogTrace("    Active Phase:        F%+02d .. F%+02d (%d active frame%s)",
                                 s_M17FirstActiveRel, s_M17LastActiveRel, activeFrames, (activeFrames == 1) ? "" : "s");
                    } else {
                        LogTrace("    Active Phase:        NO HITBOXES DETECTED");
                    }
                    if (s_M17RecoveryStartRel != -1 && s_M17MoveEndRel != -1) {
                        LogTrace("    Recovery Phase:      F%+02d .. F%+02d (%d recovery frames)",
                                 s_M17RecoveryStartRel, s_M17MoveEndRel - 1, recoveryFrames);
                        LogTrace("    Actionable / Idle:   F%+02d (Character returned to neutral)", s_M17MoveEndRel);
                    } else {
                        LogTrace("    Recovery Phase:      Unfinished / Interrupted");
                    }
                    LogTrace("--------------------------------------------------------------------");
                    LogTrace("  >>> SUMMARY:  STARTUP: %df | ACTIVE: %df | RECOVERY: %df | TOTAL: %df <<<",
                             startupFrames, activeFrames, recoveryFrames, totalDuration);
                    LogTrace("====================================================================\n");

                    g_SimTriggerFrame = 0;
                    g_SimSeqType = MACRO_SEQ_STANDARD;
                }
            } else if (g_SimSeqType == MACRO_SEQ_MODE18_NORMAL_CANCEL) {
                if (g_CurrentFrame < g_SimTriggerFrame) {
                    return; // Lead-in wait (15 frames countdown)
                }

                uint64_t elapsed = g_CurrentFrame - g_SimTriggerFrame;
                int fNum = g_Mode18_ActiveFNum;
                if (fNum < 1) fNum = 1;
                if (fNum > 5) fNum = 5;

                const char* cancelTargetName = (fNum == 1) ? "b,b,1 (Spear)" :
                                               (fNum == 2) ? "b,b,1+BL (EX Spear)" :
                                               (fNum == 3) ? "Front Dash (F, F)" :
                                               (fNum == 4) ? "Backdash (B, B)" : "X-Ray (3+4+BL)";

                void* p1 = GetP1Char();
                void* p2 = GetP2Char();
                float p1_x = 0.0f, p2_x = 0.0f;
                uint32_t p1_act = 0, p2_act = 0;
                int p1_func = -1;
                uint32_t p1_combat = 0;
                uint32_t hitboxes = 0;
                uint32_t activeVols = 0;
                uint32_t subState = 0;

                if (p1 && (uintptr_t)p1 > 0x01000000) {
                    __try {
                        p1_x = *(float*)((uintptr_t)p1 + 0x1E8);
                        p1_act = *(uint32_t*)((uintptr_t)p1 + 0x1D4);
                        p1_combat = *(uint32_t*)((uintptr_t)p1 + 0x2EC);
                        p1_func = *(int*)((uintptr_t)p1 + 0x2F88);
                        hitboxes = *(uint32_t*)((uintptr_t)p1 + 0x7A3C);

                        void* pInfo = *(void**)((uintptr_t)p1 + 0x7638);
                        if (pInfo && (uintptr_t)pInfo >= 0x00400000) {
                            uint32_t curr = *(uint32_t*)((uintptr_t)pInfo + 0x574);
                            if (curr == 0) curr = *(uint32_t*)((uintptr_t)pInfo + 0x570);
                            if (curr > 0) {
                                uintptr_t pRec = (uintptr_t)pInfo + 0x48 + (curr - 1) * 0x2C;
                                subState = *(uint32_t*)(pRec + 0x24);
                            }
                        }

                        void* pColInfo = *(void**)((uintptr_t)p1 + 0x2F10);
                        if (pColInfo && (uintptr_t)pColInfo > 0x01000000) {
                            uint32_t* pVolList = *(uint32_t**)((uintptr_t)pColInfo + 0x14);
                            if (pVolList && (uintptr_t)pVolList > 0x01000000) {
                                activeVols = pVolList[0];
                            }
                        }
                    } __except (EXCEPTION_EXECUTE_HANDLER) {}
                }

                if (p2 && (uintptr_t)p2 > 0x01000000) {
                    __try {
                        p2_x = *(float*)((uintptr_t)p2 + 0x1E8);
                        p2_act = *(uint32_t*)((uintptr_t)p2 + 0x1D4);
                    } __except (EXCEPTION_EXECUTE_HANDLER) {}
                }

                float dist = fabsf(p1_x - p2_x);
                bool p1_canAct = ((p1_combat & 0x400) == 0);

                WORD p1_back = (p1_x > p2_x) ? 'D' : 'A';
                WORD p1_fwd  = (p1_x > p2_x) ? 'A' : 'D';

                static float    s_M18InitialDist = 0.0f;
                static int      s_M18InitialFunc = -1;
                static uint32_t s_M18InitialAct = 0;
                static bool     s_M18ImpactDetected = false;
                static int      s_M18ImpactRel = -1;
                static uint64_t s_M18ImpactFrame = 0;
                static bool     s_M18IsBlocked = false;
                static float    s_M18ImpactDist = 0.0f;
                static bool     s_M18CancelDetected = false;
                static int      s_M18CancelRel = -1;
                static uint64_t s_M18CancelFrame = 0;
                static int      s_M18NewFunc = -1;
                static uint32_t s_M18NewAct = 0;
                static const char* s_M18CancelTypeName = "";

                // Frame 0: P1 Attack (FP / 'T') pressed, Defender setup
                if (elapsed == 0) {
                    s_M18InitialDist = dist;
                    s_M18InitialFunc = -1; // Wait until FP executes to lock in initial function
                    s_M18InitialAct = p1_act;
                    s_M18ImpactDetected = false;
                    s_M18ImpactRel = -1;
                    s_M18ImpactFrame = 0;
                    s_M18IsBlocked = false;
                    s_M18ImpactDist = 0.0f;
                    s_M18CancelDetected = false;
                    s_M18CancelRel = -1;
                    s_M18CancelFrame = 0;
                    s_M18NewFunc = -1;
                    s_M18NewAct = 0;
                    s_M18CancelTypeName = "";

                    g_Mode18_SpecialTriggered = false;
                    g_Mode18_SpecialTriggerFrame = 0;
                    g_Mode18_SpecialMoveInfo = nullptr;
                    g_Mode18_SpecialIsEX = false;
                    g_Mode18_SpecialIsXRay = false;
                    g_Mode18_FollowUpHitDetected = false;
                    g_Mode18_FollowUpHitFrame = 0;
                    g_Mode18_FollowUpDamage = 0.0f;
                    g_Mode18_FollowUpRxID = 0;
                    g_Mode18_FollowUpMoveClass = "";

                    g_P1_LastAttackToken = 0x8000000F; // FP

                    // Meter setup for cancel testing
                    if (fNum == 1) {
                        float curMeter = MeterDrainFix::GetPlayerMeter(1);
                        if (curMeter < 0.30f) {
                            MeterDrainFix::AwardPlayerMeter(1, 0.40f);
                        }
                    } else if (fNum == 2) {
                        float curMeter = MeterDrainFix::GetPlayerMeter(1);
                        if (curMeter < 0.60f) {
                            MeterDrainFix::AwardPlayerMeter(1, 0.70f);
                        }
                    } else if (fNum == 5) {
                        MeterDrainFix::AwardPlayerMeter(1, 1.00f);
                    }

                    SimulateKeyDown('T'); // Press FP

                    if (g_Mode18_DefenderBlocks) {
                        SimulateKeyDown(VK_NUMPAD0); // P2 Block
                    }

                    LogTrace("====================================================================");
                    LogTrace("[MODE 18 F%d | #%llu (+00f)] >>> STARTING NORMAL CANCEL DIAGNOSTIC <<<", fNum, g_CurrentFrame);
                    LogTrace("  P1 Normal Attack:   FP (Front Punch - 'T')");
                    LogTrace("  Target Cancel:      %s", cancelTargetName);
                    LogTrace("  Defender Condition: %s", g_Mode18_DefenderBlocks ? "BLOCKING (VK_NUMPAD0)" : "CLEAN HIT (Neutral / Idle)");
                    LogTrace("  Initial Spacing:    Dist=%.2f units | P1 X=%.2f | P2 X=%.2f", dist, p1_x, p2_x);
                    LogTrace("  P1 Initial State:   Act=0x%04X | Func=%d | CanAct=%d", p1_act, p1_func, p1_canAct ? 1 : 0);
                    LogTrace("====================================================================");
                }

                // Track initial function once FP starts executing (typically on frame 1 or 2)
                if (s_M18InitialFunc == -1 && p1_func != -1) {
                    s_M18InitialFunc = p1_func;
                }

                // Frame 2: Release FP key
                if (elapsed == 2) {
                    SimulateKeyUp('T');
                }

                // Input timing for each cancel sequence:
                if (fNum == 1) { // FP into b,b,1 (Spear)
                    if (elapsed == 6)  SimulateKeyDown(p1_back);
                    if (elapsed == 8)  SimulateKeyUp(p1_back);
                    if (elapsed == 9)  SimulateKeyDown(p1_back);
                    if (elapsed == 11) SimulateKeyDown('T');
                    if (elapsed == 13) {
                        SimulateKeyUp(p1_back);
                        SimulateKeyUp('T');
                    }
                } else if (fNum == 2) { // FP into b,b,1+BL (EX Spear)
                    if (elapsed == 6)  SimulateKeyDown(p1_back);
                    if (elapsed == 8)  SimulateKeyUp(p1_back);
                    if (elapsed == 9)  SimulateKeyDown(p1_back);
                    if (elapsed == 11) {
                        SimulateKeyDown(p1_back);
                        SimulateKeyDown('T');
                        SimulateKeyDown(VK_SPACE);
                    }
                    if (elapsed == 14) {
                        SimulateKeyUp(p1_back);
                        SimulateKeyUp('T');
                        SimulateKeyUp(VK_SPACE);
                    }
                } else if (fNum == 3) { // FP into Front Dash (F, F)
                    if (elapsed == 10) SimulateKeyDown(p1_fwd);
                    if (elapsed == 12) SimulateKeyUp(p1_fwd);
                    if (elapsed == 14) SimulateKeyDown(p1_fwd);
                    if (elapsed == 28) SimulateKeyUp(p1_fwd);
                } else if (fNum == 4) { // FP into Backdash (B, B)
                    if (elapsed == 10) SimulateKeyDown(p1_back);
                    if (elapsed == 12) SimulateKeyUp(p1_back);
                    if (elapsed == 14) SimulateKeyDown(p1_back);
                    if (elapsed == 28) SimulateKeyUp(p1_back);
                } else if (fNum == 5) { // FP into X-Ray (3+4+BL / Flip Stance + Block)
                    if (elapsed == 10) {
                        SimulateKeyDown('G');
                        SimulateKeyDown('J');
                        SimulateKeyDown(VK_SPACE);
                        SimulateKeyDown(VK_LCONTROL);
                    }
                    if (elapsed == 15) {
                        SimulateKeyUp('G');
                        SimulateKeyUp('J');
                        SimulateKeyUp(VK_SPACE);
                        SimulateKeyUp(VK_LCONTROL);
                    }
                }

                // Detect Impact
                if (!s_M18ImpactDetected && elapsed > 0) {
                    bool inStun = g_P2_StunTracker.inStun ||
                                  (p2_act >= 0x4000 && p2_act <= 0x4FFF) || 
                                  (p2_act >= 0x0A01 && p2_act <= 0x0A0F) || (p2_act == 0x4F00);
                    if (inStun) {
                        s_M18ImpactDetected = true;
                        s_M18ImpactRel = (int)elapsed;
                        s_M18ImpactFrame = g_CurrentFrame;
                        s_M18IsBlocked = g_P2_StunTracker.isBlockstun ||
                                         (p2_act >= 0x0A01 && p2_act <= 0x0A0F) || (p2_act == 0x4F00);
                        s_M18ImpactDist = dist;
                        LogTrace("[MODE 18 IMPACT | F%+02lld | #%llu] FP CONNECTED (%s)! Dist=%.2f | P1 Act=0x%04X Func=%d | P2 Act=0x%04X",
                                 elapsed, g_CurrentFrame, s_M18IsBlocked ? "BLOCKED" : "CLEAN HIT", s_M18ImpactDist, p1_act, p1_func, p2_act);
                    }
                }
                // Detect Cancel Execution
                if (!s_M18CancelDetected && elapsed >= 10) {
                    if (fNum == 1 || fNum == 2 || fNum == 5) {
                        // Multi-signal Special Move / EX Special / X-Ray Super cancel detection:
                        // 1. Native engine transfer: g_Mode18_SpecialTriggered (fired by 0x008A75F0 / 0x008A76D0)
                        // 2. Action state change: X-Ray uses 0x5203 / 0x5208 / 0x4203
                        // 3. Projectile volume deployment: Spear deploys 353 volumes (hitboxes >= 50)
                        // 4. Native Combat Special Bit: (p1_combat & 0x200) != 0
                        // 5. Follow-up Hit: g_Mode18_FollowUpHitDetected
                        bool nativeHookFired = g_Mode18_SpecialTriggered && (g_Mode18_SpecialTriggerFrame >= g_SimTriggerFrame);
                        bool isXRayAction = (fNum == 5) && (p1_act == 0x5203 || p1_act == 0x5208 || p1_act == 0x4203);
                        bool isSpearDeployed = (fNum == 1 || fNum == 2) && (hitboxes >= 50);
                        bool isSpecialCombatBit = ((p1_combat & 0x200) != 0);
                        bool followUpHit = g_Mode18_FollowUpHitDetected;

                        if (nativeHookFired || isXRayAction || isSpearDeployed || isSpecialCombatBit || followUpHit) {
                            s_M18CancelDetected = true;
                            if (nativeHookFired) {
                                s_M18CancelFrame = g_Mode18_SpecialTriggerFrame;
                                s_M18CancelRel = (int)(g_Mode18_SpecialTriggerFrame - g_SimTriggerFrame);
                            } else {
                                s_M18CancelFrame = g_CurrentFrame;
                                s_M18CancelRel = (int)elapsed;
                            }
                            s_M18NewFunc = p1_func;
                            s_M18NewAct = p1_act;
                            s_M18CancelTypeName = (fNum == 1) ? "SPECIAL CANCEL: b,b,1 (Spear)" :
                                                  (fNum == 2) ? "EX SPECIAL CANCEL: b,b,1+BL (EX Spear)" :
                                                                "SUPER CANCEL: X-Ray (3+4+Block)";
                            LogTrace("====================================================================");
                            LogTrace("[CANCEL DETECTED | F%+02d | #%llu] >>> P1 NORMAL CANCEL SUCCESSFUL! <<<", s_M18CancelRel, s_M18CancelFrame);
                            LogTrace("  Move Cancelled:      FP (Front Punch - 'T')");
                            LogTrace("  Cancel Target:       %s", s_M18CancelTypeName);
                            LogTrace("  Initiation Method:   %s", nativeHookFired ? "Native Engine Transfer (0x008A75F0 / 0x008A76D0)" :
                                                               (isXRayAction ? "Cinematic Action State (0x5203)" :
                                                               (isSpearDeployed ? "Projectile Volume Deployment (353 volumes)" : "Combat Special Bit (+0x2EC:0x200)")));
                            LogTrace("  Action State:        0x%04X | Script Function: %d", s_M18NewAct, s_M18NewFunc);
                            LogTrace("  Cancel Timing:       Frame F%+02d (+%df post-impact)",
                                     s_M18CancelRel, s_M18ImpactDetected ? (s_M18CancelRel - s_M18ImpactRel) : 0);
                            LogTrace("  Recovery Stolen:     %d frames of normal recovery cancelled early!",
                                     (24 - s_M18CancelRel > 0) ? (24 - s_M18CancelRel) : 0);
                            LogTrace("====================================================================");
                        }
                    } else if (fNum == 3 || fNum == 4) {
                        // Dash or Backdash transition: action state changes to Dash
                        bool isFwdDash = (p1_act == 0x0300 || (p1_act >= 0x2010 && p1_act <= 0x2017));
                        bool isBackdash = (p1_act == 0x0301 || p1_act == 0x2011 || (p1_act >= 0x2018 && p1_act <= 0x201F));
                        if ((fNum == 3 && isFwdDash) || (fNum == 4 && isBackdash)) {
                            s_M18CancelDetected = true;
                            s_M18CancelRel = (int)elapsed;
                            s_M18CancelFrame = g_CurrentFrame;
                            s_M18NewFunc = p1_func;
                            s_M18NewAct = p1_act;
                            s_M18CancelTypeName = (fNum == 3) ? "Front Dash (F, F)" : "Backdash (B, B)";

                            int recoveryStolen = (24 - s_M18CancelRel > 0) ? (24 - s_M18CancelRel) : 0;
                            LogTrace("====================================================================");
                            if (recoveryStolen > 0) {
                                LogTrace("[CANCEL DETECTED | F%+02lld | #%llu] >>> P1 NORMAL DASH CANCEL SUCCESSFUL! <<<", elapsed, g_CurrentFrame);
                                LogTrace("  Move Cancelled:      FP (Front Punch - 'T')");
                                LogTrace("  Cancel Target:       %s", s_M18CancelTypeName);
                                LogTrace("  New Action State:    0x%04X", s_M18NewAct);
                                LogTrace("  Cancel Timing:       Frame F%+02d (+%df post-impact)",
                                         s_M18CancelRel, s_M18ImpactDetected ? (s_M18CancelRel - s_M18ImpactRel) : 0);
                                LogTrace("  Recovery Stolen:     %d frames of normal recovery cancelled early", recoveryStolen);
                            } else {
                                LogTrace("[POST-RECOVERY DASH | F%+02lld | #%llu] >>> P1 DASH EXECUTED (NATURAL TRANSITION) <<<", elapsed, g_CurrentFrame);
                                LogTrace("  Move Lifecycle:      FP completed full 24f duration (11f startup + 1f active + 12f recovery)");
                                LogTrace("  Dash Action:         %s (Action State: 0x%04X)", s_M18CancelTypeName, s_M18NewAct);
                                LogTrace("  Dash Timing:         Frame F%+02d (+%df post-impact)",
                                         s_M18CancelRel, s_M18ImpactDetected ? (s_M18CancelRel - s_M18ImpactRel) : 0);
                                LogTrace("  Cancel Mechanism:    CANNOT DASH-CANCEL NORMALS (Input buffered and released upon neutral return)");
                            }
                            LogTrace("====================================================================");
                        }
                    }
                }

                // Frame-by-frame diagnostic logging (frames 0 to 55, or up to 60 for X-Ray)
                if (elapsed <= ((fNum == 5) ? 60 : 55)) {
                    const char* phaseStr = s_M18CancelDetected ? "CANCEL_ACTIVE" :
                                           (s_M18ImpactDetected ? "RECOVERY/CANCEL_WIN" :
                                           (elapsed >= 10 ? "ACTIVE" : "STARTUP"));

                    LogTrace("[DIAG M18 | F%+02lld | #%llu] P1: Act=0x%04X Func=%d SubSt=%u CanAct=%d | P2: Act=0x%04X Stun=%d | Dist=%.1f | %s",
                             elapsed, g_CurrentFrame,
                             p1_act, p1_func, subState, p1_canAct ? 1 : 0,
                             p2_act, (p2_act >= 0x0A01 && p2_act <= 0x0A0F || p2_act >= 0x4000 && p2_act <= 0x4FFF) ? 1 : 0,
                             dist, phaseStr);
                }

                // Complete test sequence:
                // Dashes (fNum 3, 4): finish after dash is well established (~25 frames post-dash)
                // Specials (fNum 1, 2): finish 5 frames after follow-up hit connects, or timeout at 55 frames
                // X-Ray (fNum 5): finish 5 frames after X-Ray hit connects (around F+51), or timeout at 75 frames
                bool isDone = false;
                if (fNum == 3 || fNum == 4) {
                    isDone = (s_M18CancelDetected && elapsed >= (uint64_t)(s_M18CancelRel + 25)) || (elapsed >= 55);
                } else if (fNum == 5) {
                    if (g_Mode18_FollowUpHitDetected && elapsed >= (uint64_t)(g_Mode18_FollowUpHitFrame - g_SimTriggerFrame + 5)) {
                        isDone = true;
                    } else if (elapsed >= 75) {
                        isDone = true;
                    }
                } else {
                    if (g_Mode18_FollowUpHitDetected && elapsed >= (uint64_t)(g_Mode18_FollowUpHitFrame - g_SimTriggerFrame + 5)) {
                        isDone = true;
                    } else if (elapsed >= 55) {
                        isDone = true;
                    }
                }
                if (isDone) {
                    SimulateKeyUp('T');
                    SimulateKeyUp('G');
                    SimulateKeyUp('J');
                    SimulateKeyUp(VK_SPACE);
                    SimulateKeyUp(VK_LCONTROL);
                    SimulateKeyUp(p1_back);
                    SimulateKeyUp(p1_fwd);
                    if (g_Mode18_DefenderBlocks) {
                        SimulateKeyUp(VK_NUMPAD0);
                    }
                    ReleaseAllSimulationKeys();

                    LogTrace("====================================================================");
                    LogTrace("[MODE 18 F%d RESULT | #%llu] NORMAL CANCEL DIAGNOSTIC COMPLETE", fNum, g_CurrentFrame);
                    LogTrace("--------------------------------------------------------------------");
                    LogTrace("  Normal Move Tested:   FP (Front Punch - 'T')");
                    LogTrace("  Target Cancel:        %s", cancelTargetName);
                    LogTrace("  Defender Condition:   %s", g_Mode18_DefenderBlocks ? "BLOCKING (P2 Blockstun 14f)" : "CLEAN HIT (P2 Hitstun 14f)");
                    LogTrace("  Initial Spacing:      Dist=%.2f units", s_M18InitialDist);
                    LogTrace("  Impact Connected:     %s (F%+02d, Dist=%.2f)",
                             s_M18ImpactDetected ? (s_M18IsBlocked ? "BLOCKED" : "CLEAN HIT") : "WHIFF / NO IMPACT",
                             s_M18ImpactRel, s_M18ImpactDist);
                    if (s_M18CancelDetected) {
                        int recoveryStolen = (24 - s_M18CancelRel > 0) ? (24 - s_M18CancelRel) : 0;
                        if (fNum == 3 || fNum == 4) {
                            LogTrace("  Cancel Result:        SUCCESSFULLY DETECTED (%s)", s_M18CancelTypeName);
                            LogTrace("  Dash Execution:       F%+02d (Frame #%llu, %d frame%s post-impact)",
                                     s_M18CancelRel, s_M18CancelFrame,
                                     s_M18ImpactDetected ? (s_M18CancelRel - s_M18ImpactRel) : 0,
                                     (s_M18ImpactDetected && (s_M18CancelRel - s_M18ImpactRel) == 1) ? "" : "s");
                            LogTrace("  New Destination:      Action State: 0x%04X", s_M18NewAct);
                            if (recoveryStolen > 0) {
                                LogTrace("  Recovery Stolen:      %d frames of normal recovery cancelled early!", recoveryStolen);
                                LogTrace("--------------------------------------------------------------------");
                                LogTrace("  >>> STATUS: [DASH CANCEL CONFIRMED & VERIFIED] <<<");
                            } else {
                                LogTrace("  Transition Timing:    Buffered Dash executed on earliest actionable frame (+0f link delay)");
                                LogTrace("--------------------------------------------------------------------");
                                LogTrace("  >>> STATUS: [DASH TRANSITION / LINK CONFIRMED & VERIFIED] <<<");
                            }
                        } else {
                            LogTrace("  Cancel Result:        SUCCESSFULLY CANCELLED (%s)", s_M18CancelTypeName);
                            LogTrace("  Cancel Execution:     F%+02d (Frame #%llu, %d frame%s post-impact)",
                                     s_M18CancelRel, s_M18CancelFrame,
                                     s_M18ImpactDetected ? (s_M18CancelRel - s_M18ImpactRel) : 0,
                                     (s_M18ImpactDetected && (s_M18CancelRel - s_M18ImpactRel) == 1) ? "" : "s");
                            LogTrace("  New Destination:      Action State: 0x%04X", s_M18NewAct);
                            LogTrace("  Recovery Stolen:      %d frames of normal recovery cancelled early!", recoveryStolen);
                            if (g_Mode18_FollowUpHitDetected) {
                                LogTrace("  Follow-up Impact:     CONNECTED! (Frame F%+02d, Dmg: %.1f%%, Class: %s, RxID: %d)",
                                         (int)(g_Mode18_FollowUpHitFrame - g_SimTriggerFrame), g_Mode18_FollowUpDamage,
                                         g_Mode18_FollowUpMoveClass, g_Mode18_FollowUpRxID);
                            } else {
                                LogTrace("  Follow-up Impact:     WHIFF / NO IMPACT DETECTED");
                            }
                            LogTrace("--------------------------------------------------------------------");
                            LogTrace("  >>> STATUS: [NORMAL CANCEL CONFIRMED & VERIFIED] <<<");
                        }
                    } else {
                        LogTrace("  Cancel Result:        CANCEL NOT EXECUTED / REJECTED");
                        LogTrace("  Normal Outcome:       FP completed full natural lifecycle (Total 24f)");
                        LogTrace("--------------------------------------------------------------------");
                        LogTrace("  >>> STATUS: [CANCEL NOT ACCEPTED - MOVE FINISHED NATURALLY] <<<");
                    }
                    LogTrace("====================================================================\n");

                    g_SimTriggerFrame = 0;
                    g_SimSeqType = MACRO_SEQ_STANDARD;
                }
            } else if (g_SimSeqType == MACRO_SEQ_MODE19_KABAL_BUG) {
                if (g_CurrentFrame < g_SimTriggerFrame) {
                    return; // Lead-in wait (15 frames countdown)
                }

                uint64_t elapsed = g_CurrentFrame - g_SimTriggerFrame;
                int fNum = g_Mode19_ActiveFNum;
                if (fNum < 1) fNum = 1;
                if (fNum > 2) fNum = 2;

                const char* bugSeqName = (fNum == 1) ? "F+4 -> F+4 Infinite Loop (+30F Advantage Bug)" :
                                                       "1,1,1 -> b,f+4 (Nomad Dash) -> b+BL (NDC) -> 1 (Glitch Link)";

                void* p1 = GetP1Char();
                void* p2 = GetP2Char();
                float p1_x = 0.0f, p2_x = 0.0f;
                uint32_t p1_act = 0, p2_act = 0;
                int p1_func = -1;
                uint32_t p1_combat = 0;
                uint32_t p2_combat = 0;
                uint32_t hitboxes = 0;
                uint32_t subState = 0;

                if (p1 && (uintptr_t)p1 > 0x01000000) {
                    __try {
                        p1_x = *(float*)((uintptr_t)p1 + 0x1E8);
                        p1_act = *(uint32_t*)((uintptr_t)p1 + 0x1D4);
                        p1_combat = *(uint32_t*)((uintptr_t)p1 + 0x2EC);
                        p1_func = *(int*)((uintptr_t)p1 + 0x2F88);
                        hitboxes = *(uint32_t*)((uintptr_t)p1 + 0x7A3C);

                        void* pInfo = *(void**)((uintptr_t)p1 + 0x7638);
                        if (pInfo && (uintptr_t)pInfo >= 0x00400000) {
                            uint32_t curr = *(uint32_t*)((uintptr_t)pInfo + 0x574);
                            if (curr == 0) curr = *(uint32_t*)((uintptr_t)pInfo + 0x570);
                            if (curr > 0) {
                                uintptr_t pRec = (uintptr_t)pInfo + 0x48 + (curr - 1) * 0x2C;
                                subState = *(uint32_t*)(pRec + 0x24);
                            }
                        }
                    } __except (EXCEPTION_EXECUTE_HANDLER) {}
                }

                if (p2 && (uintptr_t)p2 > 0x01000000) {
                    __try {
                        p2_x = *(float*)((uintptr_t)p2 + 0x1E8);
                        p2_act = *(uint32_t*)((uintptr_t)p2 + 0x1D4);
                        p2_combat = *(uint32_t*)((uintptr_t)p2 + 0x2EC);
                    } __except (EXCEPTION_EXECUTE_HANDLER) {}
                }

                float dist = fabsf(p1_x - p2_x);
                bool p1_canAct = (p1_act == 0x0000 || p1_act == 0x000F || (p1_combat & 0x40000000) != 0);

                WORD p1_fwd = ResolvePlayerDir(1, 'D');
                WORD p1_back = ResolvePlayerDir(1, 'A');

                static bool s_M19Hit1Detected = false;
                static bool s_M19Hit2Detected = false;
                static bool s_M19Hit2Blocked = false;
                static bool s_M19FollowUpHitDetected = false;
                static bool s_M19FollowUpBlocked = false;
                static uint64_t s_M19Hit1Rel = 0;
                static uint64_t s_M19Hit2Rel = 0;
                static uint64_t s_M19NDCRel = 0;
                static float s_M19InitialDist = 0.0f;
                static bool s_M19NDCKeySent = false;
                static uint64_t s_M19NDCSendFrame = 0;
                static bool s_M19FollowUp1Sent = false;
                static uint64_t s_M19FollowUp1SendFrame = 0;

                const char* defenderCond = (g_Mode19_DefenderBlockMode == 0) ? "CLEAN HIT (Neutral / Idle)" :
                                           (g_Mode19_DefenderBlockMode == 1) ? "AUTO BLOCK (Block on Earliest Recovery / Gap)" :
                                           "CONSTANT BLOCK (Holding VK_NUMPAD0)";

                if (elapsed == 0) {
                    s_M19Hit1Detected = false;
                    s_M19Hit2Detected = false;
                    s_M19Hit2Blocked = false;
                    s_M19FollowUpHitDetected = false;
                    s_M19FollowUpBlocked = false;
                    s_M19Hit1Rel = 0;
                    s_M19Hit2Rel = 0;
                    s_M19NDCRel = 0;
                    s_M19InitialDist = dist;
                    s_M19NDCKeySent = false;
                    s_M19NDCSendFrame = 0;
                    s_M19FollowUp1Sent = false;
                    s_M19FollowUp1SendFrame = 0;

                    LogTrace("====================================================================");
                    LogTrace("[MODE 19 F%d | #%llu (+00f)] >>> STARTING KABAL BUG DIAGNOSTIC <<<", fNum, g_CurrentFrame);
                    LogTrace("  P1 Character:       KABAL (Player 1)");
                    LogTrace("  Diagnostic Mode:    %s", bugSeqName);
                    LogTrace("  Defender Condition: %s", defenderCond);
                    LogTrace("  Initial Spacing:    Dist=%.2f units | P1 X=%.2f | P2 X=%.2f", dist, p1_x, p2_x);
                    LogTrace("  P1 Initial State:   Act=0x%04X | Func=%d | CanAct=%d", p1_act, p1_func, p1_canAct ? 1 : 0);
                    LogTrace("====================================================================");

                    if (g_Mode19_DefenderBlockMode == 2) {
                        SimulateKeyDown(VK_NUMPAD0); // Constant Block
                    }

                    if (fNum == 1) {
                        // F1: Press F+4 (Forward + BK / 'J')
                        SimulateKeyDown(p1_fwd);
                        SimulateKeyDown('J');
                    } else if (fNum == 2) {
                        // F2: 1, 1, 1 -> NDC -> 1 (Start with 1st '1')
                        SimulateKeyDown('T');
                    }
                }

                if (fNum == 1) { // Kabal F+4 -> F+4 Infinite Loop
                    if (elapsed == 2) {
                        SimulateKeyUp(p1_fwd);
                        SimulateKeyUp('J');
                    }
                    // Frame 39: Kabal recovers from F+4 (Total 39F duration) and inputs follow-up F+4
                    if (elapsed == 39) {
                        SimulateKeyDown(p1_fwd);
                        SimulateKeyDown('J');
                    }
                    if (elapsed == 42) {
                        SimulateKeyUp(p1_fwd);
                        SimulateKeyUp('J');
                    }

                    // Track Hit 1
                    if (!s_M19Hit1Detected && elapsed >= 6 && elapsed <= 20) {
                        bool inStun = g_P2_StunTracker.inStun || (p2_act >= 0x4000 && p2_act <= 0x4FFF) || (g_Mode19_HitCount >= 1);
                        if (inStun) {
                            s_M19Hit1Detected = true;
                            s_M19Hit1Rel = elapsed;
                            LogTrace("[MODE 19 IMPACT 1 | F%+02lld | #%llu] FIRST F+4 HIT CONNECTED! Dist=%.2f | P2 Act=0x%04X (Stagger Hitstun 69F)",
                                     elapsed, g_CurrentFrame, dist, p2_act);

                            // If Auto Block is enabled, defender starts holding block immediately upon taking the first hit!
                            if (g_Mode19_DefenderBlockMode == 1) {
                                SimulateKeyDown(VK_NUMPAD0);
                                LogTrace("[MODE 19 AUTO BLOCK | F%+02lld | #%llu] DEFENDER AUTO BLOCK ENGAGED (Holding Block during 0x4600 stagger recovery)",
                                         elapsed, g_CurrentFrame);
                            }
                        }
                    }

                    // Track Hit 2 or Block of 2nd F+4
                    if (s_M19Hit1Detected && !s_M19Hit2Detected && !s_M19Hit2Blocked && elapsed >= 44) {
                        bool isBlockstun = (p2_act >= 0x0A01 && p2_act <= 0x0A0F) || g_P2_StunTracker.isBlockstun || ((p2_combat & 0x08) != 0);
                        bool inHitstun = (g_Mode19_HitCount >= 2) || (p2_act >= 0x4000 && p2_act <= 0x4FFF);

                        if (isBlockstun) {
                            s_M19Hit2Blocked = true;
                            s_M19Hit2Rel = elapsed;
                            LogTrace("====================================================================");
                            LogTrace("[MODE 19 AUTO BLOCK | F%+02lld | #%llu] >>> SECOND F+4 WAS BLOCKED! (FALSE INFINITE) <<<", elapsed, g_CurrentFrame);
                            LogTrace("  Defender State:     Blockstun (Action 0x%04X)", p2_act);
                            LogTrace("  Block Timing:       Frame F%+02d (%lld frames after 1st F+4 recovered)", (int)elapsed, (long long)elapsed - 39);
                            LogTrace("  Stun Interaction:   Although defender is in 0x4600 stagger, MK9 allows active GUARD");
                            LogTrace("  Conclusion:         F+4 -> F+4 is NOT an unblockable true combo. Defender can AUTO-BLOCK!");
                            LogTrace("====================================================================");
                        } else if (inHitstun) {
                            s_M19Hit2Detected = true;
                            s_M19Hit2Rel = elapsed;
                            LogTrace("====================================================================");
                            LogTrace("[MODE 19 COMBO LINK | F%+02lld | #%llu] >>> SECOND F+4 CONNECTED (COMBO LINK)! <<<", elapsed, g_CurrentFrame);
                            LogTrace("  Hit Count:          2 Hits (Continuous Stagger Chain)");
                            LogTrace("  Link Timing:        Frame F%+02d (%lld frames after 1st F+4 recovered)", (int)elapsed, (long long)elapsed - 39);
                            LogTrace("  Remaining Stun:     Defender was STILL in 69F hitstun (+22F margin remaining)!");
                            LogTrace("  Bug Status:         Hit connected (Opponent did not block)");
                            LogTrace("====================================================================");
                        }
                    }
                } else if (fNum == 2) { // 1,1,1 -> b,f+4 (Nomad Dash) -> b+BL (NDC) -> 1
                    // 1. Rapid 1, 1, 1 dial
                    if (elapsed == 2)  SimulateKeyUp('T'); // release 1st '1'
                    if (elapsed == 5)  SimulateKeyDown('T'); // 2nd '1'
                    if (elapsed == 7)  SimulateKeyUp('T');
                    if (elapsed == 10) SimulateKeyDown('T'); // 3rd '1'
                    if (elapsed == 12) SimulateKeyUp('T');

                    // If Auto Block is enabled, engage block when 1,1,1 connects
                    if (g_Mode19_DefenderBlockMode == 1 && !s_M19Hit1Detected && (g_Mode19_HitCount >= 1 || p2_act >= 0x4000)) {
                        s_M19Hit1Detected = true;
                        SimulateKeyDown(VK_NUMPAD0);
                        LogTrace("[MODE 19 AUTO BLOCK | F%+02lld | #%llu] DEFENDER AUTO BLOCK ENGAGED on 1,1,1 impact", elapsed, g_CurrentFrame);
                    }

                    // 2. Special Cancel input into Nomad Dash (Back, Forward + 4)
                    if (elapsed == 20) SimulateKeyDown(p1_back);
                    if (elapsed == 23) SimulateKeyUp(p1_back);
                    if (elapsed == 25) SimulateKeyDown(p1_fwd);
                    if (elapsed == 27) SimulateKeyDown('J'); // 4 (BK)
                    if (elapsed == 40) {
                        SimulateKeyUp(p1_fwd);
                        SimulateKeyUp('J');
                    }

                    // 3. Dynamic Nomad Dash Detection & NDC Trigger (Back + Block)
                    bool isDashActive = ((p1_combat & 0x200) != 0) || (p1_act >= 0x2010 && p1_act <= 0x201F) || (p1_act == 0x0300);
                    if (elapsed >= 36 && (isDashActive || elapsed == 42) && !s_M19NDCKeySent) {
                        s_M19NDCKeySent = true;
                        s_M19NDCSendFrame = elapsed;
                        SimulateKeyDown(p1_back);
                        SimulateKeyDown(VK_SPACE); // NDC: Back + Block
                        LogTrace("====================================================================");
                        LogTrace("[MODE 19 NOMAD DASH CANCEL | F%+02lld | #%llu] >>> NOMAD DASH CANCEL (NDC) TRIGGERED! <<<", elapsed, g_CurrentFrame);
                        LogTrace("  Input Sent:         Back + Block (Halting Nomad Dash)");
                        LogTrace("  P1 Action State:    0x%04X | Combat Flags: 0x%08X", p1_act, p1_combat);
                        LogTrace("====================================================================");
                    }
                    if (s_M19NDCKeySent && elapsed == s_M19NDCSendFrame + 4) {
                        SimulateKeyUp(p1_back);
                        SimulateKeyUp(VK_SPACE);
                    }

                    // 4. Follow-up FP ('1') when neutral is restored after NDC
                    if (s_M19NDCKeySent && elapsed >= s_M19NDCSendFrame + 5 && !s_M19FollowUp1Sent) {
                        if (p1_canAct || p1_act == 0x0000 || p1_act == 0x000F || elapsed >= s_M19NDCSendFrame + 7) {
                            s_M19FollowUp1Sent = true;
                            s_M19FollowUp1SendFrame = elapsed;
                            SimulateKeyDown('T'); // Follow-up FP ('1')
                            LogTrace("[MODE 19 FOLLOW-UP 1 | F%+02lld | #%llu] >>> PRESSING FOLLOW-UP '1' (FP) OUT OF NDC NEUTRAL! <<<", elapsed, g_CurrentFrame);
                        }
                    }
                    if (s_M19FollowUp1Sent && elapsed == s_M19FollowUp1SendFrame + 3) {
                        SimulateKeyUp('T');
                    }

                    // Detect NDC neutral return
                    if (!g_Mode19_NDCCancelDetected && s_M19NDCKeySent && elapsed >= s_M19NDCSendFrame + 2) {
                        if (p1_act == 0x0000 || p1_act == 0x000F || p1_canAct) {
                            g_Mode19_NDCCancelDetected = true;
                            s_M19NDCRel = elapsed;
                            LogTrace("[MODE 19 NDC RESTORED | F%+02lld | #%llu] NEUTRAL 0x0000 RESTORED IN %lld FRAMES POST-NDC!",
                                     elapsed, g_CurrentFrame, (long long)elapsed - s_M19NDCSendFrame);
                        }
                    }

                    // Track follow-up FP hit connecting in hitstun or being blocked
                    if (s_M19FollowUp1Sent && !s_M19FollowUpHitDetected && !s_M19FollowUpBlocked && elapsed >= s_M19FollowUp1SendFrame + 6) {
                        bool isBlockstun = (p2_act >= 0x0A01 && p2_act <= 0x0A0F) || g_P2_StunTracker.isBlockstun || ((p2_combat & 0x08) != 0);
                        bool inHitstun = (g_Mode19_HitCount >= 4) || (p2_act >= 0x4000 && p2_act <= 0x4FFF);

                        if (isBlockstun) {
                            s_M19FollowUpBlocked = true;
                            LogTrace("====================================================================");
                            LogTrace("[MODE 19 AUTO BLOCK | F%+02lld | #%llu] >>> FOLLOW-UP '1' WAS BLOCKED BY DEFENDER! <<<", elapsed, g_CurrentFrame);
                            LogTrace("  Sequence:           1,1,1 -> b,f+4 (Nomad Dash) -> b+BL (NDC) -> 1");
                            LogTrace("  Defender State:     Blockstun (Action 0x%04X)", p2_act);
                            LogTrace("  Conclusion:         Defender recovered and blocked follow-up '1'.");
                            LogTrace("====================================================================");
                        } else if (inHitstun) {
                            s_M19FollowUpHitDetected = true;
                            LogTrace("====================================================================");
                            LogTrace("[MODE 19 GLITCH LINK | F%+02lld | #%llu] >>> FOLLOW-UP '1' CONNECTED (GLITCH COMBO LINK)! <<<", elapsed, g_CurrentFrame);
                            LogTrace("  Sequence:           1,1,1 -> b,f+4 (Nomad Dash) -> b+BL (NDC) -> 1");
                            LogTrace("  NDC Execution:      Halted dash in ~4F and restored actionable neutral (0x0000)");
                            LogTrace("  Glitch Mechanism:   Follow-up '1' connected before 1,1,1 hitstun expired!");
                            LogTrace("  Bug Status:         CONFIRMED ILLEGAL NDC COMBO RESET");
                            LogTrace("====================================================================");
                        }
                    }
                }

                // Frame-by-frame diagnostic trace
                if (elapsed <= 70) {
                    const char* phaseStr = (fNum == 1) ? 
                        (elapsed < 8 ? "F+4_STARTUP" : (elapsed < 39 ? "F+4_1_RECOVERY(+30F ADV)" : (elapsed < 47 ? "F+4_2_STARTUP" : "F+4_2_ACTIVE/CHAIN"))) :
                        (elapsed < 20 ? "1,1,1_STRING" : (elapsed < 30 ? "NOMAD_DASH_CANCEL_BUFFER" : (elapsed < 37 ? "NDC_NEUTRAL_RECOVERY" : "FOLLOWUP_1_LINK")));

                    LogTrace("[DIAG M19 | F%+02lld | #%llu] P1: Act=0x%04X Func=%d SubSt=%u CanAct=%d | P2: Act=0x%04X Stun=%d | Dist=%.1f | %s",
                             elapsed, g_CurrentFrame,
                             p1_act, p1_func, subState, p1_canAct ? 1 : 0,
                             p2_act, (p2_act >= 0x0A01 && p2_act <= 0x0A0F || p2_act >= 0x4000 && p2_act <= 0x4FFF) ? 1 : 0,
                             dist, phaseStr);
                }

                // Termination
                bool isDone = (elapsed >= 75);
                if (isDone) {
                    SimulateKeyUp('T');
                    SimulateKeyUp('J');
                    SimulateKeyUp(VK_SPACE);
                    SimulateKeyUp(p1_back);
                    SimulateKeyUp(p1_fwd);
                    ReleaseAllSimulationKeys();

                    LogTrace("====================================================================");
                    LogTrace("[MODE 19 F%d RESULT | #%llu] KABAL BUG DIAGNOSTIC COMPLETE", fNum, g_CurrentFrame);
                    LogTrace("--------------------------------------------------------------------");
                    LogTrace("  Bug Name:             %s", bugSeqName);
                    LogTrace("  Defender Condition:   %s", defenderCond);
                    LogTrace("  Initial Spacing:      Dist=%.2f units", s_M19InitialDist);
                    if (fNum == 1) {
                        LogTrace("  1st F+4 Hit:          %s (F%+02lld)", s_M19Hit1Detected ? "CONNECTED (Clean Hit)" : "WHIFF / NO HIT", s_M19Hit1Rel);
                        LogTrace("  1st F+4 Duration:     39 Frames Total (8F Startup + 1F Active + 30F Recovery)");
                        LogTrace("  Defender Stun State:  69 Frames Inflicted Stagger Hitstun (Action 0x4600)");
                        LogTrace("  Calculated Advantage: +30 Frames on Hit!");
                        LogTrace("  2nd F+4 Input Frame:  Frame F+39 (Earliest actionable frame)");
                        if (s_M19Hit2Blocked) {
                            LogTrace("  2nd F+4 Hit Result:   BLOCKED (Action 0x%04X Blockstun at F%+02lld)", p2_act, s_M19Hit2Rel);
                            LogTrace("  ------------------------------------------------------------------");
                            LogTrace("  >>> ROOT CAUSE: Kabal's F+4 inflicts 69F stumble hitstun (0x4600).     <<<");
                            LogTrace("  >>> While +30F advantage exists, MK9's stumble state allows active      <<<");
                            LogTrace("  >>> GUARD. When AUTO-BLOCK is enabled, defender immediately blocks!    <<<");
                            LogTrace("  >>> STATUS: [FALSE INFINITE - CAN BE AUTO-BLOCKED BY DEFENDER]        <<<");
                        } else {
                            LogTrace("  2nd F+4 Hit Result:   %s (F%+02lld)", s_M19Hit2Detected ? "CONNECTED (Combo Link)" : "WHIFF", s_M19Hit2Rel);
                            LogTrace("  Link Margin:          +22 Frames (+30F advantage - 8F startup = +22F surplus)");
                            LogTrace("  ------------------------------------------------------------------");
                            LogTrace("  >>> ROOT CAUSE: Kabal's F+4 inflicts massive 69F stumble hitstun,     <<<");
                            LogTrace("  >>> but Kabal recovers in only 39F (+30F frame advantage).             <<<");
                            LogTrace("  >>> Since F+4 starts up in 8F, Kabal loops F+4 if opponent doesn't block!<<<");
                            LogTrace("  >>> STATUS: [ENDLESS COMBO LOOP IF UNBLOCKED]                         <<<");
                        }
                    } else if (fNum == 2) {
                        LogTrace("  1,1,1 String:         FP -> FP -> FP executed cleanly");
                        LogTrace("  Special Cancel:       Cancelled into Nomad Dash (b,f+4)");
                        LogTrace("  Nomad Dash Cancel:    %s (F%+02lld)", g_Mode19_NDCCancelDetected ? "NDC TRIGGERED (Back+Block halted dash into 0x0000 neutral in ~4F)" : "NOT DETECTED", s_M19NDCRel);
                        LogTrace("  Follow-up 1 (FP):     %s", s_M19FollowUpHitDetected ? "CONNECTED (Glitch Combo Extension)" : (s_M19FollowUpBlocked ? "BLOCKED by Defender" : "EXECUTED"));
                        LogTrace("  ------------------------------------------------------------------");
                        LogTrace("  >>> ROOT CAUSE: Nomad Dash Cancel (NDC) cancels dash momentum/recovery <<<");
                        LogTrace("  >>> instantly into standing neutral (0x0000) within ~4 frames.         <<<");
                        LogTrace("  >>> This bypasses recovery, allowing follow-up '1' to combo during    <<<");
                        LogTrace("  >>> residual hitstun from 1,1,1, creating an illegal combo loop!      <<<");
                        LogTrace("  >>> STATUS: %s <<<", s_M19FollowUpHitDetected ? "[NDC GLITCH COMBO LINK CONFIRMED & VERIFIED]" : "[BLOCKED]");
                    }
                    LogTrace("====================================================================\n");

                    g_SimTriggerFrame = 0;
                    g_SimSeqType = MACRO_SEQ_STANDARD;
                }
            } else if (g_SimSeqType == MACRO_SEQ_MODE20_CROUCHING_NORMAL) {
                if (g_CurrentFrame < g_SimTriggerFrame) {
                    return; // Lead-in wait
                }

                uint64_t elapsed = g_CurrentFrame - g_SimTriggerFrame;
                int fNum = g_Mode20_ActiveFNum;
                if (fNum < 1) fNum = 1;
                if (fNum > 4) fNum = 4;

                WORD p1AttackKey = (fNum == 1) ? 'T' :
                                   (fNum == 2) ? 'U' :
                                   (fNum == 3) ? 'G' : 'J';
                const char* p1AttackName = (fNum == 1) ? "D+1 (Down + Front Punch / Crouching Jab)" :
                                           (fNum == 2) ? "D+2 (Down + Back Punch / Uppercut)" :
                                           (fNum == 3) ? "D+3 (Down + Front Kick / Crouching Shin Kick)" :
                                                         "D+4 (Down + Back Kick / Crouching Poke)";

                void* p1 = GetP1Char();
                uint32_t p1_act = 0;
                float p1_h = 0.0f;
                uint32_t p1_combat = 0;
                uint32_t hitboxes = 0;
                int scriptFunc = -1;
                uintptr_t scriptPkg = 0;
                uint32_t stance = 0;
                uint32_t subState = 0;
                int32_t moveRes = 0;
                uint32_t recAtkType = 0;
                uint32_t animFlags = 0;
                uint32_t activeVols = 0;
                void* activeRx = nullptr;
                int activeRxID = -1;

                if (p1 && (uintptr_t)p1 > 0x01000000) {
                    __try {
                        p1_act = *(uint32_t*)((uintptr_t)p1 + 0x1D4);
                        p1_h = *(float*)((uintptr_t)p1 + 0x1F0);
                        p1_combat = *(uint32_t*)((uintptr_t)p1 + 0x2EC);
                        hitboxes = *(uint32_t*)((uintptr_t)p1 + 0x7A3C);
                        scriptPkg = *(uintptr_t*)((uintptr_t)p1 + 0x2F84);
                        scriptFunc = *(int*)((uintptr_t)p1 + 0x2F88);
                        activeRx = *(void**)((uintptr_t)p1 + 0x2F8C);
                        if (activeRx && (uintptr_t)activeRx >= 0x00400000 && (uintptr_t)activeRx < 0x7FFE0000) {
                            activeRxID = *(int*)((uintptr_t)activeRx + 0x00);
                        }
                        stance = *(uint32_t*)((uintptr_t)p1 + 0x2E8);

                        void* pInfo = *(void**)((uintptr_t)p1 + 0x7638);
                        if (pInfo && (uintptr_t)pInfo >= 0x00400000) {
                            uint32_t curr = *(uint32_t*)((uintptr_t)pInfo + 0x574);
                            if (curr == 0) curr = *(uint32_t*)((uintptr_t)pInfo + 0x570);
                            if (curr > 0) {
                                uintptr_t pRec = (uintptr_t)pInfo + 0x48 + (curr - 1) * 0x2C;
                                moveRes = *(int32_t*)(pRec + 0x1C);
                                recAtkType = *(uint32_t*)(pRec + 0x20);
                                subState = *(uint32_t*)(pRec + 0x24);
                            }
                            void* pAnimPlayer = *(void**)((uintptr_t)pInfo + 0xB40);
                            if (pAnimPlayer && (uintptr_t)pAnimPlayer > 0x01000000) {
                                animFlags = *(uint32_t*)((uintptr_t)pAnimPlayer + 0xC);
                            }
                        }

                        void* pColInfo = *(void**)((uintptr_t)p1 + 0x2F10);
                        if (pColInfo && (uintptr_t)pColInfo > 0x01000000) {
                            uint32_t* pVolList = *(uint32_t**)((uintptr_t)pColInfo + 0x14);
                            if (pVolList && (uintptr_t)pVolList > 0x01000000) {
                                activeVols = pVolList[0];
                            }
                        }
                    } __except (EXCEPTION_EXECUTE_HANDLER) {}
                }

                bool p1_canAct = ((p1_combat & 0x400) == 0);
                bool isHitboxActive = (hitboxes > 0) || (activeVols > 0) || ((animFlags & 0x02) != 0);
                bool isNativeUppercut = IsFighterExecutingUppercut(p1);

                static int s_M20AttackStartRel = -1;
                static int s_M20FirstActiveRel = -1;
                static int s_M20LastActiveRel = -1;
                static int s_M20RecoveryStartRel = -1;
                static int s_M20MoveEndRel = -1;
                static uint32_t s_M20InitialAct = 0;
                static uint32_t s_M20MaxHitboxes = 0;
                static uint32_t s_M20PeakVols = 0;
                static int s_M20ScriptFunc = -1;
                static float s_M20BaseDamage = 0.0f;
                static int s_M20ReactionID = 0;
                static bool s_M20WasUppercut = false;

                // Frame 0: P1 Holds 'S' to crouch
                if (elapsed == 0) {
                    s_M20AttackStartRel = -1;
                    s_M20FirstActiveRel = -1;
                    s_M20LastActiveRel = -1;
                    s_M20RecoveryStartRel = -1;
                    s_M20MoveEndRel = -1;
                    s_M20InitialAct = p1_act;
                    s_M20MaxHitboxes = 0;
                    s_M20PeakVols = 0;
                    s_M20ScriptFunc = -1;
                    s_M20BaseDamage = 0.0f;
                    s_M20ReactionID = 0;
                    s_M20WasUppercut = false;

                    SimulateKeyDown('S');

                    LogTrace("====================================================================");
                    LogTrace("[MODE 20 F%d | #%llu (+00f)] >>> STARTING CROUCHING NORMAL DIAGNOSTIC <<<", fNum, g_CurrentFrame);
                    LogTrace("  Move Name:       %s", p1AttackName);
                    LogTrace("  Input Sequence:  Hold 'S' (Crouch) -> Frame +06: Press '%c'", (char)p1AttackKey);
                    LogTrace("  P1 Initial Act:  0x%04X | CanAct: %d | Stance: %u", p1_act, p1_canAct ? 1 : 0, stance);
                    LogTrace("====================================================================");
                }

                // Frame 6: Press Attack Button while continuing to hold 'S'
                if (elapsed == 6) {
                    if (fNum == 1)      g_P1_LastAttackToken = 0x8000000F; // FP
                    else if (fNum == 2) g_P1_LastAttackToken = 0x80000010; // BP
                    else if (fNum == 3) g_P1_LastAttackToken = 0x80000011; // FK
                    else if (fNum == 4) g_P1_LastAttackToken = 0x80000012; // BK

                    SimulateKeyDown(p1AttackKey);
                    LogTrace("[DIAG M20 | F%+03lld | #%llu] >>> BUTTON PRESS: '%c' (Token: 0x%08X) <<<",
                             elapsed, g_CurrentFrame, (char)p1AttackKey, g_P1_LastAttackToken);
                }

                // Frame 9: Release Attack Button (Hold 'S' to remain low)
                if (elapsed == 9) {
                    SimulateKeyUp(p1AttackKey);
                }

                if (isNativeUppercut) {
                    s_M20WasUppercut = true;
                }

                // Detect Attack Initiation
                if (s_M20AttackStartRel == -1 && elapsed >= 6 && (p1_act != s_M20InitialAct || !p1_canAct || scriptFunc != -1 || isHitboxActive)) {
                    s_M20AttackStartRel = (int)elapsed;
                    s_M20ScriptFunc = scriptFunc;
                    QueryCharacterAttackInfo(p1, &s_M20BaseDamage, &s_M20ReactionID);
                }
                if (s_M20ScriptFunc == -1 && scriptFunc != -1 && elapsed >= 6) {
                    s_M20ScriptFunc = scriptFunc;
                    QueryCharacterAttackInfo(p1, &s_M20BaseDamage, &s_M20ReactionID);
                }
                if (activeRxID > 0 && s_M20ReactionID <= 0) {
                    s_M20ReactionID = activeRxID;
                }

                // Detect Active Phase (Hitboxes or Physical Collision Volumes deployed)
                if (isHitboxActive && elapsed >= 6) {
                    if (s_M20FirstActiveRel == -1) {
                        s_M20FirstActiveRel = (int)elapsed;
                    }
                    s_M20LastActiveRel = (int)elapsed;
                    if (hitboxes > s_M20MaxHitboxes) s_M20MaxHitboxes = hitboxes;
                    if (activeVols > s_M20PeakVols) s_M20PeakVols = activeVols;
                } else if (s_M20FirstActiveRel != -1 && s_M20RecoveryStartRel == -1 && elapsed > (uint64_t)s_M20LastActiveRel) {
                    s_M20RecoveryStartRel = (int)elapsed;
                }

                // Detect End of Move (Return to crouch idle / neutral stance)
                if (s_M20FirstActiveRel != -1 && elapsed > (uint64_t)s_M20FirstActiveRel) {
                    bool returnedToCrouchOrNeutral = (p1_act == 0x0000 || p1_act == 0x0100 || p1_act == 0x0101 || p1_act == 0x0102 || p1_act == 0x0103) && p1_canAct;
                    if (returnedToCrouchOrNeutral && s_M20MoveEndRel == -1 && elapsed > (uint64_t)(s_M20LastActiveRel + 2)) {
                        s_M20MoveEndRel = (int)elapsed;
                    }
                }

                // Classify current frame phase
                const char* phaseStr = "CROUCH";
                if (elapsed < 6) {
                    phaseStr = "CROUCH_IDLE";
                } else if (s_M20MoveEndRel != -1) {
                    phaseStr = "SETTLED";
                } else if (s_M20RecoveryStartRel != -1) {
                    phaseStr = "RECOVERY";
                } else if (isHitboxActive) {
                    phaseStr = "ACTIVE";
                } else {
                    phaseStr = "STARTUP";
                }

                // Frame-by-frame diagnostic telemetry (frames 0 to 90)
                if (elapsed <= 90) {
                    LogTrace("[DIAG M20 | F%+03lld | #%llu] Phase: %-11s | Act=0x%04X CanAct=%d | RxID=%-3d Upper=%-3s | Hitbox=%u Vols=%u | Func=%d",
                             elapsed, g_CurrentFrame,
                             phaseStr,
                             p1_act, p1_canAct ? 1 : 0,
                             (activeRxID > 0 ? activeRxID : s_M20ReactionID),
                             isNativeUppercut ? "YES" : "NO ",
                             hitboxes, activeVols,
                             scriptFunc);
                }

                // Finish diagnostic sequence
                bool isComplete = (s_M20MoveEndRel != -1 && elapsed >= (uint64_t)(s_M20MoveEndRel + 3)) || (elapsed >= 100);
                if (isComplete) {
                    SimulateKeyUp(p1AttackKey);
                    SimulateKeyUp('S');
                    ReleaseAllSimulationKeys();

                    int startupFrames = (s_M20FirstActiveRel != -1 && s_M20FirstActiveRel >= 6) ? (s_M20FirstActiveRel - 6) : 0;
                    int activeFrames = (s_M20FirstActiveRel != -1 && s_M20LastActiveRel >= s_M20FirstActiveRel) ?
                                       (s_M20LastActiveRel - s_M20FirstActiveRel + 1) : 0;
                    int recoveryFrames = (s_M20MoveEndRel != -1 && s_M20LastActiveRel != -1 && s_M20MoveEndRel > s_M20LastActiveRel) ?
                                         (s_M20MoveEndRel - s_M20LastActiveRel - 1) : 0;
                    int totalDuration = startupFrames + activeFrames + recoveryFrames;

                    const char* distFactor = (fNum == 1) ? "D+1: Crouching Front Punch (RxID 117 on Scorpion, fast low-profile poke, non-uppercut)" :
                                             (fNum == 2) ? "D+2: Crouching Back Punch (RxID 121 on Scorpion, true Uppercut launcher, TOASTY-eligible)" :
                                             (fNum == 3) ? "D+3: Crouching Front Kick (RxID 125 on Scorpion, low shin kick, non-uppercut)" :
                                                           "D+4: Crouching Back Kick (RxID 129 on Scorpion, longer reach low poke, non-uppercut)";

                    LogTrace("====================================================================");
                    LogTrace("[MODE 20 RESULT | F%d: %s | #%llu]", fNum, p1AttackName, g_CurrentFrame);
                    LogTrace("--------------------------------------------------------------------");
                    LogTrace("  Attack Type:            %s", p1AttackName);
                    LogTrace("  Input Keys:             'S' + '%c' (Token: 0x%08X)", (char)p1AttackKey, g_P1_LastAttackToken);
                    LogTrace("  Script Function:        ID %d (Package: 0x%08X)", s_M20ScriptFunc, (uint32_t)scriptPkg);
                    LogTrace("  Detected Reaction ID:   RxID %d", (activeRxID > 0 ? activeRxID : s_M20ReactionID));
                    LogTrace("  Base / Authored Damage: %.1f", s_M20BaseDamage);
                    LogTrace("  Is Native Uppercut:     %s", s_M20WasUppercut ? "YES (True D+2 Uppercut)" : "NO (Crouching Normal)");
                    LogTrace("  Toasty Eligible:        %s", s_M20WasUppercut ? "YES (D+2 Uppercut triggers Toasty on qualifying hit)" : "NO (Only D+2 qualifies)");
                    LogTrace("  Measured Frame Data:    %dF Startup | %dF Active | %dF Recovery (Total: %dF)",
                             startupFrames, activeFrames, recoveryFrames, totalDuration);
                    LogTrace("  Distinguishing Factor:  %s", distFactor);
                    LogTrace("====================================================================\n");

                    g_SimTriggerFrame = 0;
                    g_SimSeqType = MACRO_SEQ_STANDARD;
                }
            } else {
                if (g_SimP1_DirKey && (g_CurrentFrame == g_SimTriggerFrame + g_SimP1_DelayFrames - 2 || g_CurrentFrame == g_SimTriggerFrame + g_SimP1_DelayFrames - 1)) {
                    WORD p1Dir = ResolvePlayerDir(1, g_SimP1_DirKey);
                    SimulateKeyDown(p1Dir);
                }

                if (g_CurrentFrame == g_SimTriggerFrame + g_SimP1_DelayFrames) {
                    WORD p1Dir = ResolvePlayerDir(1, g_SimP1_DirKey);
                    if (p1Dir)          SimulateKeyDown(p1Dir);
                    if (g_SimP1_Key)    SimulateKeyDown(g_SimP1_Key);
                    LogTrace("[TEST BOT] P1 attack triggered at frame %llu", g_CurrentFrame);
                }
                if (g_CurrentFrame == g_SimTriggerFrame + g_SimP1_DelayFrames + 5) {
                    if (g_SimP1_Key)    SimulateKeyUp(g_SimP1_Key);
                    if (g_SimP1_DirKey) {
                        SimulateKeyUp(g_SimP1_DirKey);
                        SimulateKeyUp('D');
                        SimulateKeyUp('A');
                    }
                }

                if (g_SimP2_DirKey && (g_CurrentFrame == g_SimTriggerFrame + g_SimP2_DelayFrames - 2 || g_CurrentFrame == g_SimTriggerFrame + g_SimP2_DelayFrames - 1)) {
                    WORD p2Dir = ResolvePlayerDir(2, g_SimP2_DirKey);
                    SimulateKeyDown(p2Dir);
                }

                if (g_CurrentFrame == g_SimTriggerFrame + g_SimP2_DelayFrames) {
                    WORD p2Dir = ResolvePlayerDir(2, g_SimP2_DirKey);
                    if (p2Dir)          SimulateKeyDown(p2Dir);
                    if (g_SimP2_Key)    SimulateKeyDown(g_SimP2_Key);
                    LogTrace("[TEST BOT] P2 attack triggered at frame %llu", g_CurrentFrame);
                }
                if (g_CurrentFrame == g_SimTriggerFrame + g_SimP2_DelayFrames + 5) {
                    if (g_SimP2_Key)    SimulateKeyUp(g_SimP2_Key);
                    if (g_SimP2_DirKey) {
                        SimulateKeyUp(g_SimP2_DirKey);
                        SimulateKeyUp(VK_LEFT);
                        SimulateKeyUp(VK_RIGHT);
                    }
                }

                if (g_CurrentFrame > g_SimTriggerFrame + 25) {
                    ReleaseAllSimulationKeys();
                    g_SimTriggerFrame = 0;
                    g_SimSeqType = MACRO_SEQ_STANDARD;
                    g_SimP1_DelayFrames = -1;
                    g_SimP2_DelayFrames = -1;
                    g_SimP1_DirKey = 0;
                    g_SimP2_DirKey = 0;
                }
            }
        }

        AnimViewer::OnFrameTick();

        if (oTickFightFrame) {
            oTickFightFrame(param_1);
        }

        FinalizeFrameDataActivity();
        FinalizeJadeStaffRecovery();
        ExtraGameplay::FinalizeCleanFighters();

        if (g_GamePaused.load()) {
            *(int*)0x00E43054 = 1;
            *(int*)0x00E43058 = 1;
        }

        if (IsDrawDebugPushboxes() || IsDrawDebugHurtboxes() || IsDrawDebugHitboxes() || ExtraGameplay::g_EnableFloorGrid) {
            SafeFlushDebugPrimitives();
            void* p1 = GetP1Char();
            void* p2 = GetP2Char();
            if (p1 && (IsDrawDebugPushboxes() || IsDrawDebugHurtboxes() || IsDrawDebugHitboxes())) RenderDebugCollisionPrimitives(p1, 1);
            if (p2 && (IsDrawDebugPushboxes() || IsDrawDebugHurtboxes() || IsDrawDebugHitboxes())) RenderDebugCollisionPrimitives(p2, 2);
            if (ExtraGameplay::g_EnableFloorGrid) ExtraGameplay::RenderFloorGrid();
        }

        // Clean up / flush deferred hit if defender did not connect within timeout window (+2 frames)
        if (g_DeferredHit.isPending && (g_CurrentFrame > g_DeferredHit.frameCaptured + 2)) {
            FlushDeferredHit("TIMEOUT (+2f)");
        }
    }

    static bool s_KeyStates[256] = { false };
    static inline bool CheckHotkeyDown(int vk) {
        bool isDown = (GetAsyncKeyState(vk) & 0x8000) != 0;
        bool pressed = isDown && !s_KeyStates[vk & 0xFF];
        s_KeyStates[vk & 0xFF] = isDown;
        return pressed;
    }

    static DWORD WINAPI HotkeyThread(LPVOID lpParam) {
        const WORD P1_1 = 'T', P1_2 = 'U', P1_3 = 'G', P1_4 = 'J';
        const WORD P2_1 = VK_NUMPAD7, P2_2 = VK_NUMPAD9, P2_3 = VK_NUMPAD4, P2_4 = VK_NUMPAD6;
        const WORD P1_U = 'W', P1_D = 'S', P1_B = 'A', P1_F = 'D';
        const WORD P2_U = VK_UP, P2_D = VK_DOWN, P2_B = VK_RIGHT, P2_F = VK_LEFT;

        while (true) {
            if (CheckHotkeyDown(VK_F10)) {
                ToggleFullscreenWindowed();
            }
            if (CheckHotkeyDown(VK_OEM_5)) { // '\' key
                ToggleGamePause();
            }
            if (CheckHotkeyDown(VK_OEM_PLUS)) { // '=' key
                StepOneFrame();
            }
            if (CheckHotkeyDown(VK_F9)) {
                if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
                    g_DiagnosticMode++;
                    if (g_DiagnosticMode > 20) g_DiagnosticMode = 0;
                    PrintDiagnosticModeMenu();
                } else {
                    ToggleFrameDataVisualizer();
                    LogTrace("[FRAME DATA] Profiler Visualizer %s (F9)", g_FrameDataVisualizerEnabled ? "ENABLED" : "DISABLED");
                }
            } else if (g_DiagnosticMode == 1) {
                if (CheckHotkeyDown(VK_F1)) {
                    TriggerMacroBot(0, 'T', 0, 0, VK_NUMPAD7, 0, "Simultaneous FP (P1: 0f,  P2: 0f)");
                }
                if (CheckHotkeyDown(VK_F2)) {
                    TriggerMacroBot(0, 'T', 1, 0, VK_NUMPAD7, 0, "P1 Delayed FP (+1 frame delay on P1)");
                }
                if (CheckHotkeyDown(VK_F3)) {
                    TriggerMacroBot(0, 'T', 0, 0, VK_NUMPAD7, 1, "P2 Delayed FP (+1 frame delay on P2)");
                }
                if (CheckHotkeyDown(VK_F4)) {
                    TriggerMacroBot(0, 'T', 2, 0, VK_NUMPAD7, 0, "P1 Delayed FP (+2 frame delay on P1)");
                }
                if (CheckHotkeyDown(VK_F5)) {
                    TriggerMacroBot(0, 'T', 0, 0, VK_NUMPAD7, 2, "P2 Delayed FP (+2 frame delay on P2)");
                }
                if (CheckHotkeyDown(VK_F6)) {
                    TriggerSequenceMacro(MACRO_SEQ_P2_WALK_THEN_ATTACK, "P1 FP vs P2 Left(15f)+Wait(1f)+Num7");
                }
                if (CheckHotkeyDown(VK_F7)) {
                    TriggerSequenceMacro(MACRO_SEQ_P1_WALK_THEN_ATTACK, "P1 D(15f)+Wait(1f)+T vs P2 Num7");
                }
                if (CheckHotkeyDown(VK_F8)) {
                    TriggerMacroBot('S', 'G', 0, VK_DOWN, VK_NUMPAD4, 0, "Simultaneous D+FK (P1: S+G vs P2: Down+Num4)");
                }
            } else if (g_DiagnosticMode == 2) {
                if (CheckHotkeyDown(VK_F1)) {
                    TriggerMacroBot('A', 'T', 0, VK_RIGHT, VK_NUMPAD7, 0, "Simultaneous b+FP (P1: A+T, P2: Right+Num7)");
                }
                if (CheckHotkeyDown(VK_F2)) {
                    TriggerMacroBot('A', 'U', 0, VK_RIGHT, VK_NUMPAD9, 0, "Simultaneous b+BP (P1: A+U, P2: Right+Num9)");
                }
                if (CheckHotkeyDown(VK_F3)) {
                    TriggerMacroBot('A', 'U', 0, 0, VK_NUMPAD7, 1, "P1 b+BP (A+U) vs P2 Delayed FP (+1f Num7)");
                }
                if (CheckHotkeyDown(VK_F4)) {
                    TriggerMacroBot('A', 'T', 0, 0, VK_NUMPAD7, 1, "P1 b+FP (A+T) vs P2 Delayed FP (+1f Num7)");
                }
                if (CheckHotkeyDown(VK_F5)) {
                    TriggerMacroBot('A', 'U', 0, 0, VK_NUMPAD7, 2, "P1 b+BP (A+U) vs P2 Delayed FP (+2f Num7)");
                }
                if (CheckHotkeyDown(VK_F6)) {
                    TriggerMacroBot('A', 'T', 0, 0, VK_NUMPAD7, 2, "P1 b+FP (A+T) vs P2 Delayed FP (+2f Num7)");
                }
                if (CheckHotkeyDown(VK_F7)) {
                    TriggerMacroBot('D', 'T', 0, 0, VK_NUMPAD9, 0, "P1 f+FP (D+T) vs P2 BP (Num9) Simultaneous");
                }
                if (CheckHotkeyDown(VK_F8)) {
                    TriggerMacroBot('D', 'U', 0, 0, VK_NUMPAD7, 0, "P1 f+BP (D+U) vs P2 FP (Num7) Simultaneous");
                }
            } else if (g_DiagnosticMode == 3) {
                if (CheckHotkeyDown(VK_F1)) {
                    Trigger2StepMacro(P1_D, P1_B, P1_1, P2_D, P2_B, P2_1, "Both do D, B, 1 (P1: S, A+T vs P2: Down, Right+Num7)");
                }
                if (CheckHotkeyDown(VK_F2)) {
                    Trigger2StepMacro(P1_D, P1_B, P1_2, P2_D, P2_B, P2_2, "Both do D, B, 2 (P1: S, A+U vs P2: Down, Right+Num9)");
                }
                if (CheckHotkeyDown(VK_F3)) {
                    Trigger2StepMacro(P1_D, P1_B, P1_3, P2_D, P2_B, P2_3, "Both do D, B, 3 (P1: S, A+G vs P2: Down, Right+Num4)");
                }
                if (CheckHotkeyDown(VK_F4)) {
                    Trigger2StepMacro(P1_D, P1_B, P1_4, P2_D, P2_B, P2_4, "Both do D, B, 4 (P1: S, A+J vs P2: Down, Right+Num6)");
                }
                if (CheckHotkeyDown(VK_F5)) {
                    Trigger2StepMacro(P1_D, P1_F, P1_1, P2_D, P2_F, P2_1, "Both do D, F, 1 (P1: S, D+T vs P2: Down, Left+Num7)");
                }
                if (CheckHotkeyDown(VK_F6)) {
                    Trigger2StepMacro(P1_D, P1_F, P1_2, P2_D, P2_F, P2_2, "Both do D, F, 2 (P1: S, D+U vs P2: Down, Left+Num9)");
                }
                if (CheckHotkeyDown(VK_F7)) {
                    Trigger2StepMacro(P1_D, P1_F, P1_3, P2_D, P2_F, P2_3, "Both do D, F, 3 (P1: S, D+G vs P2: Down, Left+Num4)");
                }
                if (CheckHotkeyDown(VK_F8)) {
                    Trigger2StepMacro(P1_D, P1_U, P1_2, P2_D, P2_U, P2_2, "Both do D, U, 2 (P1: S, W+U vs P2: Down, Up+Num9)");
                }
            } else if (g_DiagnosticMode == 4) {
                if (CheckHotkeyDown(VK_F1)) {
                    Trigger2StepMacro(P1_B, P1_F, P1_1, P2_B, P2_F, P2_1, "Both do B, F, 1 (P1: A, D+T vs P2: Right, Left+Num7)");
                }
                if (CheckHotkeyDown(VK_F2)) {
                    Trigger2StepMacro(P1_B, P1_F, P1_2, P2_B, P2_F, P2_2, "Both do B, F, 2 (P1: A, D+U vs P2: Right, Left+Num9)");
                }
                if (CheckHotkeyDown(VK_F3)) {
                    Trigger2StepMacro(P1_B, P1_F, P1_3, P2_B, P2_F, P2_3, "Both do B, F, 3 (P1: A, D+G vs P2: Right, Left+Num4)");
                }
                if (CheckHotkeyDown(VK_F4)) {
                    Trigger2StepMacro(P1_B, P1_F, P1_4, P2_B, P2_F, P2_4, "Both do B, F, 4 (P1: A, D+J vs P2: Right, Left+Num6)");
                }
                if (CheckHotkeyDown(VK_F5)) {
                    Trigger2StepMacro(P1_B, P1_B, P1_1, P2_B, P2_B, P2_1, "Both do B, B, 1 (P1: A, A+T vs P2: Right, Right+Num7)");
                }
                if (CheckHotkeyDown(VK_F6)) {
                    Trigger2StepMacro(P1_B, P1_B, P1_2, P2_B, P2_B, P2_2, "Both do B, B, 2 (P1: A, A+U vs P2: Right, Right+Num9)");
                }
                if (CheckHotkeyDown(VK_F7)) {
                    Trigger2StepMacro(P1_B, P1_B, P1_3, P2_B, P2_B, P2_3, "Both do B, B, 3 (P1: A, A+G vs P2: Right, Right+Num4)");
                }
                if (CheckHotkeyDown(VK_F8)) {
                    Trigger2StepMacro(P1_B, P1_B, P1_4, P2_B, P2_B, P2_4, "Both do B, B, 4 (P1: A, A+J vs P2: Right, Right+Num6)");
                }
            } else if (g_DiagnosticMode == 5) {
                if (CheckHotkeyDown(VK_F1)) {
                    Trigger2StepMacro(P1_F, P1_F, P1_3, P2_F, P2_F, P2_3, "Both do F, F, 3 (P1: D, D+G vs P2: Left, Left+Num4)");
                }
                if (CheckHotkeyDown(VK_F2)) {
                    Trigger2StepMacro(P1_F, P1_F, P1_4, P2_F, P2_F, P2_4, "Both do F, F, 4 (P1: D, D+J vs P2: Left, Left+Num6)");
                }
                if (CheckHotkeyDown(VK_F3)) {
                    Trigger3StepMacro(P1_D, P1_F, P1_B, P1_2, P2_D, P2_F, P2_B, P2_2, "Both do D, F, B, 2 (P1: S, D, A+U vs P2: Down, Left, Right+Num9)");
                }
                if (CheckHotkeyDown(VK_F4)) {
                    Trigger3StepMacro(P1_D, P1_B, P1_F, P1_1, P2_D, P2_B, P2_F, P2_1, "Both do D, B, F, 1 (P1: S, A, D+T vs P2: Down, Right, Left+Num7)");
                }
                if (CheckHotkeyDown(VK_F5)) {
                    Trigger3StepMacro(P1_D, P1_B, P1_F, P1_2, P2_D, P2_B, P2_F, P2_2, "Both do D, B, F, 2 (P1: S, A, D+U vs P2: Down, Right, Left+Num9)");
                }
                if (CheckHotkeyDown(VK_F6)) {
                    Trigger3StepMacro(P1_D, P1_B, P1_F, P1_3, P2_D, P2_B, P2_F, P2_3, "Both do D, B, F, 3 (P1: S, A, D+G vs P2: Down, Right, Left+Num4)");
                }
                if (CheckHotkeyDown(VK_F7)) {
                    Trigger3StepMacro(P1_B, P1_B, P1_F, P1_1, P2_B, P2_B, P2_F, P2_1, "Both do B, B, F, 1 (P1: A, A, D+T vs P2: Right, Right, Left+Num7)");
                }
                if (CheckHotkeyDown(VK_F8)) {
                    Trigger3StepMacro(P1_B, P1_B, P1_F, P1_4, P2_B, P2_B, P2_F, P2_4, "Both do B, B, F, 4 (P1: A, A, D+J vs P2: Right, Right, Left+Num6)");
                }
            } else if (g_DiagnosticMode == 6) {
                if (CheckHotkeyDown(VK_F1)) {
                    Trigger2StepMacro(P1_U, P1_D, P1_4, P2_U, P2_D, P2_4, "Both do U, D, 4 (P1: W(7f)+Wait(3f)+S+J vs P2: Up(7f)+Wait(3f)+Down+Num6)", 7, 3);
                }
                if (CheckHotkeyDown(VK_F2)) {
                    Trigger3StepMacro(P1_U, P1_D, P1_F, P1_1, P2_U, P2_D, P2_F, P2_1, "Both do U, D, F, 1 (P1: W(7f)+Wait(3f)+S, D+T vs P2: Up(7f)+Wait(3f)+Down, Left+Num7)", 7, 3);
                }
                if (CheckHotkeyDown(VK_F3)) {
                    Trigger3StepMacro(P1_U, P1_D, P1_B, P1_1, P2_U, P2_D, P2_B, P2_1, "Both do U, D, B, 1 (P1: W(7f)+Wait(3f)+S, A+T vs P2: Up(7f)+Wait(3f)+Down, Right+Num7)", 7, 3);
                }
                if (CheckHotkeyDown(VK_F4)) {
                    Trigger3StepMacro(P1_U, P1_B, P1_F, P1_1, P2_U, P2_B, P2_F, P2_1, "Both do U, B, F, 1 (P1: W(7f)+Wait(3f)+A, D+T vs P2: Up(7f)+Wait(3f)+Right, Left+Num7)", 7, 3);
                }
                if (CheckHotkeyDown(VK_F5)) {
                    Trigger2StepMacro(P1_B, P1_B, P1_1, P2_D, P2_F, P2_3, "P1 b, b, 1 (A, A+T) vs P2 d, f, 3 (Down, Left+Num4)");
                }
                if (CheckHotkeyDown(VK_F6)) {
                    Trigger2StepMacro(P1_D, P1_F, P1_3, P2_D, P2_F, P2_2, "P1 d, f, 3 (S, D+G) vs P2 d, f, 2 (Down, Left+Num9)");
                }
                if (CheckHotkeyDown(VK_F7)) {
                    Trigger2StepMacro(P1_B, P1_F, P1_3, P2_B, P2_F, P2_1, "P1 b, f, 3 (A, D+G) vs P2 b, f, 1 (Right, Left+Num7)");
                }
                if (CheckHotkeyDown(VK_F8)) {
                    Trigger2StepMacro(P1_B, P1_F, P1_3, P2_B, P2_B, P2_1, "P1 b, f, 3 (A, D+G) vs P2 b, b, 1 (Right, Right+Num7)");
                }
            } else if (g_DiagnosticMode == 7) {
                if (CheckHotkeyDown(VK_F1)) {
                    TriggerP2SpecVsP1Norm(P2_D, P2_F, P2_3, P1_D, P1_1, 0, "P2 Sub-Zero D,F+3 vs P1 D+1 on STARTUP (Scorpion Interrupts)");
                }
                if (CheckHotkeyDown(VK_F2)) {
                    TriggerP2SpecVsP1Norm(P2_D, P2_F, P2_3, P1_D, P1_1, 5, "P2 Sub-Zero D,F+3 vs P1 D+1 on ACTIVE (Sub-Zero Wins)");
                }
                if (CheckHotkeyDown(VK_F3)) {
                    TriggerP1SpecVsP2Norm(P1_D, P1_B, P1_4, P2_D, P2_1, 0, "P1 Scorpion D,B+4 vs P2 D+1 on STARTUP (Sub-Zero Interrupts)");
                }
                if (CheckHotkeyDown(VK_F4)) {
                    TriggerP1SpecVsP2Norm(P1_D, P1_B, P1_4, P2_D, P2_1, 5, "P1 Scorpion D,B+4 vs P2 D+1 on ACTIVE (Scorpion Wins)");
                }
                if (CheckHotkeyDown(VK_F5)) {
                    TriggerP2SpecVsP1Norm(P2_D, P2_F, P2_3, P1_D, P1_2, 5, "P2 Sub-Zero D,F+3 vs P1 D+2 Uppercut (D+2 Uppercut Wins)");
                }
                if (CheckHotkeyDown(VK_F6)) {
                    TriggerJumpInVsGround(true, "P1 Jump-In vs P2 D+2 Anti-Air Uppercut (D+2 Wins)");
                }
                if (CheckHotkeyDown(VK_F7)) {
                    TriggerJumpInVsGround(false, "P1 Jump-In vs P2 Standing Normal 1 (Jump-In Wins)");
                }
                if (CheckHotkeyDown(VK_F8)) {
                    TriggerMacroBot(P1_D, P1_2, 0, P2_D, P2_2, 0, "Both do D+2 Uppercut (Later Inputted D+2 Wins by Default)");
                }
            } else if (g_DiagnosticMode == 8) {
                if (CheckHotkeyDown(VK_F1)) {
                    TriggerMode8F1();
                }
                if (CheckHotkeyDown(VK_F2)) {
                    TriggerMode8F2();
                }
                if (CheckHotkeyDown(VK_F3)) {
                    TriggerMode8F3();
                }
                if (CheckHotkeyDown(VK_F4)) {
                    TriggerMode8F4();
                }
                if (CheckHotkeyDown(VK_F5)) {
                    TriggerMode8F5();
                }
                if (CheckHotkeyDown(VK_F6)) {
                    TriggerMode8F6();
                }
                if (CheckHotkeyDown(VK_F7)) {
                    TriggerMode8F7();
                }
                if (CheckHotkeyDown(VK_F8)) {
                    TriggerMode8F8();
                }
            } else if (g_DiagnosticMode == 9) {
                if (CheckHotkeyDown(VK_F1)) {
                    TriggerMode9F1();
                }
                if (CheckHotkeyDown(VK_F2)) {
                    TriggerMode9F2();
                }
                if (CheckHotkeyDown(VK_F3)) {
                    TriggerMode9F3();
                }
                if (CheckHotkeyDown(VK_F4)) {
                    TriggerMode9F4();
                }
                if (CheckHotkeyDown(VK_F5)) {
                    TriggerMode9F5();
                }
                if (CheckHotkeyDown(VK_F6)) {
                    TriggerMode9F6();
                }
                if (CheckHotkeyDown(VK_F7)) {
                    TriggerMode9F7();
                }
                if (CheckHotkeyDown(VK_F8)) {
                    TriggerMode9F8();
                }
            } else if (g_DiagnosticMode == 10) {
                if (CheckHotkeyDown(VK_F1)) {
                    TriggerMode10F1();
                }
                if (CheckHotkeyDown(VK_F2)) {
                    TriggerMode10F2();
                }
                if (CheckHotkeyDown(VK_F3)) {
                    TriggerMode10F3();
                }
                if (CheckHotkeyDown(VK_F4)) {
                    TriggerMode10F4();
                }
                if (CheckHotkeyDown(VK_F5)) {
                    TriggerMode10F5();
                }
                if (CheckHotkeyDown(VK_F6)) {
                    TriggerMode10F6();
                }
                if (CheckHotkeyDown(VK_F7)) {
                    TriggerMode10F7();
                }
                if (CheckHotkeyDown(VK_F8)) {
                    TriggerMode10F8();
                }
            } else if (g_DiagnosticMode == 11) {
                if (CheckHotkeyDown(VK_F1)) {
                    TriggerMode11F1();
                }
                if (CheckHotkeyDown(VK_F2)) {
                    TriggerMode11F2();
                }
                if (CheckHotkeyDown(VK_F3)) {
                    TriggerMode11F3();
                }
                if (CheckHotkeyDown(VK_F4)) {
                    TriggerMode11F4();
                }
                if (CheckHotkeyDown(VK_F5)) {
                    TriggerMode11F5();
                }
                if (CheckHotkeyDown(VK_F6)) {
                    TriggerMode11F6();
                }
                if (CheckHotkeyDown(VK_F7)) {
                    TriggerMode11F7();
                }
                if (CheckHotkeyDown(VK_F8)) {
                    TriggerMode11F8();
                }
            } else if (g_DiagnosticMode == 12) {
                if (CheckHotkeyDown(VK_F1)) {
                    TriggerMode12F1();
                }
                if (CheckHotkeyDown(VK_F2)) {
                    TriggerMode12F2();
                }
                if (CheckHotkeyDown(VK_F3)) {
                    TriggerMode12F3();
                }
                if (CheckHotkeyDown(VK_F4)) {
                    TriggerMode12F4();
                }
                if (CheckHotkeyDown(VK_F5)) {
                    TriggerMode12F5();
                }
                if (CheckHotkeyDown(VK_F6)) {
                    TriggerMode12F6();
                }
            } else if (g_DiagnosticMode == 13) {
                if (CheckHotkeyDown(VK_F1)) TriggerMode13F1();
                if (CheckHotkeyDown(VK_F2)) TriggerMode13F2();
                if (CheckHotkeyDown(VK_F3)) TriggerMode13F3();
                if (CheckHotkeyDown(VK_F4)) TriggerMode13F4();
            } else if (g_DiagnosticMode == 14) {
                if (CheckHotkeyDown(VK_F1)) TriggerMode14F1();
                if (CheckHotkeyDown(VK_F2)) TriggerMode14F2();
                if (CheckHotkeyDown(VK_F3)) TriggerMode14F3();
                if (CheckHotkeyDown(VK_F4)) TriggerMode14F4();
                if (CheckHotkeyDown(VK_F5)) TriggerMode14F5();
                if (CheckHotkeyDown(VK_F6)) TriggerMode14F6();
                if (CheckHotkeyDown(VK_F7)) TriggerMode14F7();
                if (CheckHotkeyDown(VK_F8)) TriggerMode14F8();
            } else if (g_DiagnosticMode == 15) {
                if (CheckHotkeyDown(VK_F1)) TriggerMode15F1();
                if (CheckHotkeyDown(VK_F2)) TriggerMode15F2();
                if (CheckHotkeyDown(VK_F3)) TriggerMode15F3();
                if (CheckHotkeyDown(VK_F4)) TriggerMode15F4();
            } else if (g_DiagnosticMode == 16) {
                if (CheckHotkeyDown(VK_F1)) TriggerMode16F1();
                if (CheckHotkeyDown(VK_F2)) TriggerMode16F2();
                if (CheckHotkeyDown(VK_F3)) TriggerMode16F3();
                if (CheckHotkeyDown(VK_F4)) TriggerMode16F4();
                if (CheckHotkeyDown(VK_F5)) TriggerMode16F5();
                if (CheckHotkeyDown(VK_F6)) TriggerMode16F6();
                if (CheckHotkeyDown(VK_F7)) TriggerMode16F7();
                if (CheckHotkeyDown(VK_F8)) TriggerMode16F8();
            } else if (g_DiagnosticMode == 17) {
                if (CheckHotkeyDown(VK_F1)) TriggerMode17F1();
                if (CheckHotkeyDown(VK_F2)) TriggerMode17F2();
                if (CheckHotkeyDown(VK_F3)) TriggerMode17F3();
                if (CheckHotkeyDown(VK_F4)) TriggerMode17F4();
            } else if (g_DiagnosticMode == 18) {
                if (CheckHotkeyDown(VK_F1)) TriggerMode18F1();
                if (CheckHotkeyDown(VK_F2)) TriggerMode18F2();
                if (CheckHotkeyDown(VK_F3)) TriggerMode18F3();
                if (CheckHotkeyDown(VK_F4)) TriggerMode18F4();
                if (CheckHotkeyDown(VK_F5)) TriggerMode18F5();
            } else if (g_DiagnosticMode == 19) {
                if (CheckHotkeyDown(VK_F1)) TriggerMode19F1();
                if (CheckHotkeyDown(VK_F2)) TriggerMode19F2();
            } else if (g_DiagnosticMode == 20) {
                if (CheckHotkeyDown(VK_F1)) TriggerMode20F1();
                if (CheckHotkeyDown(VK_F2)) TriggerMode20F2();
                if (CheckHotkeyDown(VK_F3)) TriggerMode20F3();
                if (CheckHotkeyDown(VK_F4)) TriggerMode20F4();
            }

            Sleep(10);
        }
        return 0;
    }

    bool InstallSimulationHooks() {
        if (MH_CreateHook((void*)0x0080f850, (void*)hkTickFightFrame, (void**)&oTickFightFrame) == MH_OK) {
            MH_EnableHook((void*)0x0080f850);
            LogTrace("[+] Frame Simulation Tick Hooked (0x0080F850)");
        }

        if (MH_CreateHook((void*)0x008a06f0, (void*)hkMatchBufferedMove, (void**)&oMatchBufferedMove) == MH_OK) {
            MH_EnableHook((void*)0x008a06f0);
            LogTrace("[+] Match Buffered Move Hooked (0x008A06F0 - Diagnostic Logger)");
        }

        if (MH_CreateHook((void*)0x008a1d10, (void*)hkMatchAction, (void**)&oMatchAction) == MH_OK) {
            MH_EnableHook((void*)0x008a1d10);
            LogTrace("[+] Match Action Hooked (0x008A1D10 - Diagnostic Logger)");
        }

        CreateThread(nullptr, 0, HotkeyThread, nullptr, 0, nullptr);
        LogTrace("[+] Simulation Hotkey Worker Thread Started");
        return true;
    }
}

