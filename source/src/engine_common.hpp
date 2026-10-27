#pragma once
#include <windows.h>
#include <xinput.h>
#include <cstdint>
#include <cstdio>

namespace MK9TradeFix {

    // Pointer validation
    inline bool IsValidUserPtr(const void* ptr) {
        uintptr_t addr = (uintptr_t)ptr;
        return (addr >= 0x00400000 && addr < 0x7FFE0000);
    }

    inline bool IsCommittedReadablePtr(const void* ptr) {
        if (!IsValidUserPtr(ptr)) return false;
        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQuery(ptr, &mbi, sizeof(mbi)) == sizeof(mbi)) {
            return (mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)));
        }
        return false;
    }

    inline bool IsCommittedExecutablePtr(const void* ptr) {
        if (!IsValidUserPtr(ptr)) return false;
        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQuery(ptr, &mbi, sizeof(mbi)) == sizeof(mbi)) {
            if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) {
                return false;
            }
            return (mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
        }
        return false;
    }

    // Move Classification
    enum MoveClass {
        MOVE_CLASS_NORMAL_LIMB = 0,
        MOVE_CLASS_NORMAL_WEAPON,
        MOVE_CLASS_SPECIAL,
        MOVE_CLASS_EX_SPECIAL,
        MOVE_CLASS_XRAY
    };

    // Unified Character Combat State Machine
    enum CharacterCombatState {
        STATE_NEUTRAL = 0,         // Idle, walking, crouching, jumping
        STATE_HITSTUN,             // Receiving damage / stumble (0x4600)
        STATE_KNOCKDOWN,           // Airborne knockdown (0x4201), floor reaction (0x6200..0x620B)
        STATE_CINEMATIC_XRAY,      // Cinematic X-Ray mode (0x4203)
        STATE_SPECIAL_OR_SUPER,    // Special move, EX Special, or X-Ray trigger (EXCLUDED from trades)
        STATE_NORMAL_STARTUP,      // Normal attack in windup phase (Clean Interrupt)
        STATE_NORMAL_ACTIVE,       // Normal attack in active striking hit window (Trade Eligible)
        STATE_NORMAL_RECOVERY      // Normal attack past active window (Whiff Punish)
    };

    // Video Display Mode
    enum DisplayMode {
        DISPLAY_MODE_BORDERLESS = 0,
        DISPLAY_MODE_WINDOWED   = 1,
        DISPLAY_MODE_FULLSCREEN = 2
    };

    // Diagnostic Macro Sequences
    enum MacroSequenceType {
        MACRO_SEQ_STANDARD = 0,
        MACRO_SEQ_P1_WALK_THEN_ATTACK, // Mode 1 F7: P1 D (2f), wait 1f, T
        MACRO_SEQ_P2_WALK_THEN_ATTACK, // Mode 1 F6: P2 Left (2f), wait 1f, Num7
        MACRO_SEQ_GENERIC_SPECIAL,     // Multi-Step Special Move Macro (Modes 3 - 5)
        MACRO_SEQ_P1_SPEC_VS_P2_NORM,  // Mode 6: P1 Special vs P2 Normal (Startup/Active)
        MACRO_SEQ_P2_SPEC_VS_P1_NORM,  // Mode 6: P2 Special vs P1 Normal (Startup/Active)
        MACRO_SEQ_JUMP_IN_VS_GROUND,   // Mode 6: Jump-In vs Ground Normal / D+2 Uppercut
        MACRO_SEQ_MODE8_F1_RAW_NEG_EDGE,         // Mode 8 F1: P1 Hold 1 -> B, B -> Release 1 (Raw Neutral)
        MACRO_SEQ_MODE8_F2_NORMAL_SPECIAL,       // Mode 8 F2: P1 B, B -> Press 1 (Normal Special Baseline)
        MACRO_SEQ_MODE8_F3_HIT_CONFIRM_CANCEL,   // Mode 8 F3: P1 Close Contact Jab -> Cancel on Hit
        MACRO_SEQ_MODE8_F4_WHIFF_NEG_EDGE,       // Mode 8 F4: P1 Whiff Jab -> Neg Edge (Whiff Rule Check)
        MACRO_SEQ_MODE8_F5_P2_RAW_NEG_EDGE,      // Mode 8 F5: P2 Hold 1 -> B, B -> Release 1 (Mirrored)
        MACRO_SEQ_MODE8_F6_P2_NORMAL_SPECIAL,    // Mode 8 F6: P2 B, B -> Press 1 (Mirrored Baseline)
        MACRO_SEQ_MODE8_F7_P2_HIT_CONFIRM_CANCEL,// Mode 8 F7: P2 Close Contact Jab -> Cancel on Hit
        MACRO_SEQ_MODE8_F8_STRING_BUFFER,        // Mode 8 F8: P1 String 1, 1 -> Backdash Buffer Check
        MACRO_SEQ_MODE9_F1_FP,                   // Mode 9 F1: P1 Front Punch (Baseline)
        MACRO_SEQ_MODE9_F2_FP_DASH,              // Mode 9 F2: P1 Front Punch -> Immediate Dash
        MACRO_SEQ_MODE9_F3_PROJECTILE_NORMAL,    // Mode 9 F3: P1 D, B + 3 (Normal Projectile)
        MACRO_SEQ_MODE9_F4_PROJECTILE_EX,        // Mode 9 F4: P1 D, B + 3 + Block (EX Projectile)
        MACRO_SEQ_MODE9_F5_JC_FP_HOLD_DOWN_FP,   // Mode 9 F5: Johnny Cage FP -> Dash -> FP -> D+FP (Frame 53)
        MACRO_SEQ_MODE9_F6_JC_FP_HOLD_DOWN_FP,   // Mode 9 F6: Johnny Cage FP -> Dash -> FP -> D+FP (Frame 55)
        MACRO_SEQ_MODE9_F7_JC_FP_HOLD_DOWN_FP,   // Mode 9 F7: Johnny Cage FP -> Dash -> FP -> D+FP (Frame 56)
        MACRO_SEQ_MODE9_F8_JC_FP_HOLD_DOWN_FP,   // Mode 9 F8: Johnny Cage FP -> Dash -> FP -> D+FP (Frame 58)
        MACRO_SEQ_MODE10_F1_JC_EX_FORCEBALL_SOLO,          // Mode 10 F1: P2 Left(30f) -> Neutral(5f) -> D,F+2+BL -> F+3 (Solo Baseline)
        MACRO_SEQ_MODE10_F2_JC_EX_FORCEBALL_VS_P1_EX,      // Mode 10 F2: P2 F+3 vs P1 Stand BL -> D,B+3+BL (+0f Delay: Frame 94)
        MACRO_SEQ_MODE10_F3_JC_EX_FORCEBALL_VS_P1_EX,      // Mode 10 F3: P2 F+3 vs P1 Stand BL -> D,B+3+BL (+5f Delay: Frame 99)
        MACRO_SEQ_MODE10_F4_JC_EX_FORCEBALL_VS_P1_EX,      // Mode 10 F4: P2 F+3 vs P1 Stand BL -> D,B+3+BL (+6f Delay: Frame 100)
        MACRO_SEQ_MODE10_F5_JC_EX_FORCEBALL_VS_P1_CROUCH,  // Mode 10 F5: P2 F+3 vs P1 Crouch BL -> D,B+3+BL (+8f Delay: Frame 102) [Former F8]
        MACRO_SEQ_MODE10_F6_JC_EX_FORCEBALL_VS_P1_CROUCH,  // Mode 10 F6: P2 F+3 vs P1 Crouch BL -> D,B+3+BL (+9f Delay: Frame 103)
        MACRO_SEQ_MODE10_F7_JC_EX_FORCEBALL_VS_P1_STAND,   // Mode 10 F7: P2 F+3 vs P1 Stand BL -> D,B+3+BL (+8f Delay: Frame 102) [No Crouch]
        MACRO_SEQ_MODE10_F8_JC_EX_FORCEBALL_VS_P1_STAND,   // Mode 10 F8: P2 F+3 vs P1 Stand BL -> D,B+3+BL (+9f Delay: Frame 103) [No Crouch]
        MACRO_SEQ_MODE11_F1_JC_EX_FORCEBALL_SOLO,          // Mode 11 F1: P2 Left(30f) -> Neutral(5f) -> D,F+2+BL -> F+3 (Solo Baseline)
        MACRO_SEQ_MODE11_F2_JC_EX_FORCEBALL_VS_P1_DF1_EX,  // Mode 11 F2: P2 F+3 vs P1 Stand BL -> D,F+1+BL (+0f Delay: Frame 94)
        MACRO_SEQ_MODE11_F3_JC_EX_FORCEBALL_VS_P1_DF1_EX,  // Mode 11 F3: P2 F+3 vs P1 Stand BL -> D,F+1+BL (+5f Delay: Frame 99)
        MACRO_SEQ_MODE11_F4_JC_EX_FORCEBALL_VS_P1_DF1_EX,  // Mode 11 F4: P2 F+3 vs P1 Stand BL -> D,F+1+BL (+6f Delay: Frame 100)
        MACRO_SEQ_MODE11_F5_JC_EX_FORCEBALL_VS_P1_DF1_CROUCH, // Mode 11 F5: P2 F+3 vs P1 Crouch BL -> D,F+1+BL (+8f Delay: Frame 102)
        MACRO_SEQ_MODE11_F6_JC_EX_FORCEBALL_VS_P1_DF1_CROUCH, // Mode 11 F6: P2 F+3 vs P1 Crouch BL -> D,F+1+BL (+9f Delay: Frame 103)
        MACRO_SEQ_MODE11_F7_JC_EX_FORCEBALL_VS_P1_DF1_STAND,  // Mode 11 F7: P2 F+3 vs P1 Stand BL -> D,F+1+BL (+8f Delay: Frame 102) [No Crouch]
        MACRO_SEQ_MODE11_F8_JC_EX_FORCEBALL_VS_P1_DF1_STAND,  // Mode 11 F8: P2 F+3 vs P1 Stand BL -> D,F+1+BL (+9f Delay: Frame 103) [No Crouch]
        MACRO_SEQ_MODE12_F1_P2_MAX_MASH,       // Mode 12 F1: P2 Max Wild Mashing (Gamepads + Keyboard)
        MACRO_SEQ_MODE12_F2_P2_MODERATE_MASH,  // Mode 12 F2: P2 Moderate Mashing (Gamepads + Keyboard)
        MACRO_SEQ_MODE12_F3_P1_MAX_MASH,       // Mode 12 F3: P1 Max Wild Mashing (Gamepads + Keyboard)
        MACRO_SEQ_MODE12_F4_P1_MODERATE_MASH,  // Mode 12 F4: P1 Moderate Mashing (Gamepads + Keyboard)
        MACRO_SEQ_MODE12_F5_P2_MASH_P1_WALK_4S_BACK_4S_FWD, // Mode 12 F5: P2 Mash (6s) + P1 Walk 4s Back / 4s Fwd
        MACRO_SEQ_MODE12_F6_P1_MASH_P2_WALK_4S_BACK_4S_FWD, // Mode 12 F6: P1 Mash (6s) + P2 Walk 4s Back / 4s Fwd
        MACRO_SEQ_MODE13_F1_QUICK_ROLL,                      // Mode 13 F1: P1 Sweep (B+4) vs P2 Tech Roll (+42f offset)
        MACRO_SEQ_MODE13_F2_QUICK_ROLL,                      // Mode 13 F2: P1 Sweep (B+4) vs P2 Tech Roll (+44f offset)
        MACRO_SEQ_MODE13_F3_QUICK_ROLL,                      // Mode 13 F3: P1 Uppercut (D+2) vs P2 Tech Roll (+65f offset)
        MACRO_SEQ_MODE13_F4_QUICK_ROLL,                      // Mode 13 F4: P1 Uppercut (D+2) vs P2 Tech Roll (+66f offset)
        MACRO_SEQ_MODE14_DELAYED_WAKEUP,                     // Mode 14: Knockdown vs P2 Down Hold (Delayed Wakeup Buffer Prober)
        MACRO_SEQ_MODE15_BLOCKSTUN_DIAGNOSTIC,               // Mode 15: F1 (BK), F2 (FP), F3 (BP), F4 (FK) vs P2 Block
        MACRO_SEQ_MODE16_UP_TAP_DIAGNOSTIC,                  // Mode 16: P1 Tap Up 1f..8f Jump Startup Pinpoint
        MACRO_SEQ_MODE17_ATTACK_MEASUREMENT,                 // Mode 17: P1 Attack Frame Data Measurement (F1: FP, F2: BP, F3: FK, F4: BK)
        MACRO_SEQ_MODE18_NORMAL_CANCEL,                      // Mode 18: P1 Normal Cancel Diagnostic (F1: Spear, F2: EX Spear, F3: Dash Fwd, F4: Backdash, F5: X-Ray)
        MACRO_SEQ_MODE19_KABAL_BUG,                          // Mode 19: Kabal Bug Diagnostic (F1: F+4 -> F+4 Loop, F2: 1,1,1 -> NDC -> 1 Glitch Link)
        MACRO_SEQ_MODE20_CROUCHING_NORMAL                    // Mode 20: Crouching Normal Diagnostics (F1: D+1, F2: D+2, F3: D+3, F4: D+4)
    };
    #define MACRO_SEQ_MODE15_F1_BLOCKSTUN_DIAGNOSTIC MACRO_SEQ_MODE15_BLOCKSTUN_DIAGNOSTIC

    // Mode 15 Blockstun Pinpoint Diagnostic Registers
    extern int g_Mode15_ActiveFNum;

    // Mode 16 Up Tap & Jump Startup Diagnostic Registers
    extern int g_Mode16_ActiveFNum;

    // Mode 17 Attack Frame Data Measurement Registers
    extern int g_Mode17_ActiveFNum;

    // Mode 18 Normal Cancel Diagnostic Registers
    extern int         g_Mode18_ActiveFNum;
    extern bool        g_Mode18_DefenderBlocks;
    extern bool        g_Mode18_SpecialTriggered;
    extern uint64_t    g_Mode18_SpecialTriggerFrame;
    extern void*       g_Mode18_SpecialMoveInfo;
    extern bool        g_Mode18_SpecialIsEX;
    extern bool        g_Mode18_SpecialIsXRay;
    extern bool        g_Mode18_FollowUpHitDetected;
    extern uint64_t    g_Mode18_FollowUpHitFrame;
    extern float       g_Mode18_FollowUpDamage;
    extern int         g_Mode18_FollowUpRxID;
    extern const char* g_Mode18_FollowUpMoveClass;

    // Mode 19 Kabal Bug Diagnostic Registers
    extern int         g_Mode19_ActiveFNum;
    extern int         g_Mode19_DefenderBlockMode; // 0: Clean Hit, 1: Auto Block (Block on Gap), 2: Constant Block
    extern int         g_Mode19_HitCount;
    extern uint64_t    g_Mode19_Hit1Frame;
    extern uint64_t    g_Mode19_Hit2Frame;
    extern uint64_t    g_Mode19_Hit3Frame;
    extern uint64_t    g_Mode19_Hit4Frame;
    extern float       g_Mode19_TotalDamage;
    extern bool        g_Mode19_NDCCancelDetected;
    extern uint64_t    g_Mode19_NDCCancelFrame;

    // Mode 20 Crouching Normal Diagnostic Registers
    extern int         g_Mode20_ActiveFNum;

    // Mode 13 Quick Roll Calibration Registers
    extern int g_Mode13_BaseOffset;
    extern int g_Mode13_OffsetStep;
    extern int g_Mode13_RollDirection;
    extern int g_Mode13_RollPattern;

    // Mode 14 Delayed Wakeup Window Identification Registers
    extern int g_Mode14_KnockdownMove;    // 0 = Sweep (B+4), 1 = Uppercut (D+2)
    extern int g_Mode14_FloorRelOffset;   // Injection offset relative to floor contact 0x460D (-25 to +30)
    extern int g_Mode14_HoldFrames;       // Down hold duration (30 to 96, default: 90)
    extern int g_Mode14_BaseOffset;       // Sweep offset (default: 38)
    extern int g_Mode14_UppercutOffset;   // Uppercut offset (default: 63)
    extern int g_Mode14_ActiveFNum;
    int GetMode14RelativeOffset(int fNum);
    int GetMode14HoldDuration(int fNum);

    // Global Mod Status & Counters
    extern bool     g_FixEnabled;
    extern bool     g_LoggingEnabled;
    extern bool     g_P1_NegativeEdgeEnabled;
    extern bool     g_P2_NegativeEdgeEnabled;
    extern uint64_t g_CurrentFrame;
    inline uint64_t GetCurrentSimulationFrame() { return g_CurrentFrame; }
    extern uint64_t g_LastHitFrame;
    extern int      g_LastHitAttacker;
    extern int      g_HitEventCount;
    extern int      g_TradeEventCount;

    // Predictive Attack Timing Registers
    extern uint64_t g_P1_AttackStartFrame;
    extern uint64_t g_P2_AttackStartFrame;
    extern uint64_t g_P1_LastAttackStartFrame;
    extern uint64_t g_P1_LastAttackEndFrame;
    extern uint64_t g_P2_LastAttackStartFrame;
    extern uint64_t g_P2_LastAttackEndFrame;
    extern int      g_P1_LastScriptFunc;
    extern int      g_P2_LastScriptFunc;

    // 50/50 Special Clash Resolution
    extern int      g_5050_WinnerPlr;
    extern int      g_5050_LoserPlr;
    extern uint64_t g_5050_ExpireFrame;
    bool IsActive5050Winner(void* pChar);

    // Logging & Crash Handler
    void LogTrace(const char* fmt, ...);
    LONG WINAPI MK9CrashHandler(PEXCEPTION_POINTERS pExceptionInfo);

    // Authoritative NetherRealm Global Player Queries
    void* GetP1Char();
    void* GetP2Char();
    inline int GetPlayerNumber(void* pChar) {
        return (pChar == GetP2Char()) ? 2 : 1;
    }
    inline void* GetOpponentChar(void* pChar) {
        return (pChar == GetP2Char()) ? GetP1Char() : GetP2Char();
    }
    bool IsCurrentMatchTag();
    bool IsMatchActive();
    bool IsMatchFightActive();
    bool IsMatchLoadedUp();
    bool IsNativeGamePaused();
    bool IsGameOrSimulationPaused();
    bool IsAttachedChildActor(void* pParent, void* pCandidate);
    const char* GetCurrentStageName();
    bool IsTrainingMode();
    bool IsP2HumanControl();
    bool IsNativeSuperMeterFull();
    bool IsD2SymbolName(const char* sym);
    bool IsExecutingD2Script(void* pChar);
    void ApplyLateralPosition(void* pChar, float lateralY);
    void TriggerLightingProbeWarmup();

    // Keyboard & Gamepad Simulation Helpers
    HWND GetTargetWindow();
    bool IsGameWindowFocused();
    void SimulateKeyDown(WORD vk);
    void SimulateKeyUp(WORD vk);
    void ReleaseAllSimulationKeys();
    bool IsSimulatedKeyHeld(WORD vk);
    bool HasActiveDiagnosticGamepad(DWORD dwUserIndex);
    bool GetActiveDiagnosticGamepad(DWORD dwUserIndex, XINPUT_STATE* pState);

    // XInput Controller Mapping (Port Assignment & Auto-Detection)
    extern int g_P1_XInputPort; // Default 0 (Port 1)
    extern int g_P2_XInputPort; // Default 1 (Port 2)
    extern int g_DetectingControllerForPlayer; // 0 = Idle, 1 = Listening for P1, 2 = Listening for P2
    extern uint64_t g_DetectionStartFrame;

    inline int GetPlayerXInputPort(int playerNum) {
        return (playerNum == 1) ? g_P1_XInputPort : g_P2_XInputPort;
    }
    inline int GetXInputPortPlayer(int dwUserIndex) {
        if (dwUserIndex == g_P1_XInputPort) return 1;
        if (dwUserIndex == g_P2_XInputPort) return 2;
        return (dwUserIndex + 1);
    }
    inline void SwapControllerPorts() {
        int tmp = g_P1_XInputPort;
        g_P1_XInputPort = g_P2_XInputPort;
        g_P2_XInputPort = tmp;
    }
    inline void StartControllerDetection(int playerNum) {
        g_DetectingControllerForPlayer = playerNum;
        g_DetectionStartFrame = g_CurrentFrame;
    }
    inline void CancelControllerDetection() {
        g_DetectingControllerForPlayer = 0;
    }

    // Keyboard Mapping & Player Routing
    extern int g_KeyboardAssignedPlayer; // 0 = Auto/Both, 1 = Player 1, 2 = Player 2 (Dummy)
    inline void SwapKeyboardAssignment() {
        if (g_KeyboardAssignedPlayer == 1) g_KeyboardAssignedPlayer = 2;
        else if (g_KeyboardAssignedPlayer == 2) g_KeyboardAssignedPlayer = 1;
        else g_KeyboardAssignedPlayer = 2;
    }
}

