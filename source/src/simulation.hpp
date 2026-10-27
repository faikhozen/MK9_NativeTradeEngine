#pragma once
#include "engine_common.hpp"
#include <atomic>

namespace MK9TradeFix {

    extern std::atomic<bool> g_GamePaused;
    extern std::atomic<bool> g_StepOneFrame;

    void SetEnginePaused(bool pause);
    void ToggleGamePause();
    bool IsGamePaused();
    void StepOneFrame();

    int GetDiagnosticMode();
    void SetDiagnosticMode(int mode);
    void PrintDiagnosticModeMenu();
    extern int g_DiagnosticMode;

    extern uint64_t    g_ActiveMacroTriggerFrame;
    extern int         g_ActiveMacroP1Delay;
    extern int         g_ActiveMacroP2Delay;
    extern const char* g_ActiveMacroDesc;

    extern MacroSequenceType g_SimSeqType;
    extern uint64_t          g_SimTriggerFrame;

    void TriggerMacroBot(WORD p1Dir, WORD p1Key, int p1Delay, WORD p2Dir, WORD p2Key, int p2Delay, const char* desc);
    void TriggerSequenceMacro(MacroSequenceType seq, const char* desc);
    void Trigger2StepMacro(WORD p1_d1, WORD p1_d2, WORD p1_b, WORD p2_d1, WORD p2_d2, WORD p2_b, const char* desc, int step1Hold = 2, int step1Delay = 2);
    void Trigger3StepMacro(WORD p1_d1, WORD p1_d2, WORD p1_d3, WORD p1_b, WORD p2_d1, WORD p2_d2, WORD p2_d3, WORD p2_b, const char* desc, int step1Hold = 2, int step1Delay = 2);
    void TriggerP2SpecVsP1Norm(WORD p2_d1, WORD p2_d2, WORD p2_btn, WORD p1_dir, WORD p1_btn, int p1_delay, const char* desc);
    void TriggerP1SpecVsP2Norm(WORD p1_d1, WORD p1_d2, WORD p1_btn, WORD p2_dir, WORD p2_btn, int p2_delay, const char* desc);
    void TriggerJumpInVsGround(bool isAntiAirD2, const char* desc);
    void TriggerMode8F1();
    void TriggerMode8F2();
    void TriggerMode8F3();
    void TriggerMode8F4();
    void TriggerMode8F5();
    void TriggerMode8F6();
    void TriggerMode8F7();
    void TriggerMode8F8();
    void TriggerMode9F1();
    void TriggerMode9F2();
    void TriggerMode9F3();
    void TriggerMode9F4();
    void TriggerMode9F5();
    void TriggerMode9F6();
    void TriggerMode9F7();
    void TriggerMode9F8();
    void TriggerMode10F1();
    void TriggerMode10F2();
    void TriggerMode10F3();
    void TriggerMode10F4();
    void TriggerMode10F5();
    void TriggerMode10F6();
    void TriggerMode10F7();
    void TriggerMode10F8();
    void TriggerMode11F1();
    void TriggerMode11F2();
    void TriggerMode11F3();
    void TriggerMode11F4();
    void TriggerMode11F5();
    void TriggerMode11F6();
    void TriggerMode11F7();
    void TriggerMode11F8();
    void TriggerMode12F1();
    void TriggerMode12F2();
    void TriggerMode12F3();
    void TriggerMode12F4();
    void TriggerMode12F5();
    void TriggerMode12F6();
    void TriggerMode13F1();
    void TriggerMode13F2();
    void TriggerMode13F3();
    void TriggerMode13F4();
    void TriggerMode14FKey(int fNum);
    void TriggerMode14F1();
    void TriggerMode14F2();
    void TriggerMode14F3();
    void TriggerMode14F4();
    void TriggerMode14F5();
    void TriggerMode14F6();
    void TriggerMode14F7();
    void TriggerMode14F8();
    void TriggerMode14Custom(int relOffset, int holdDuration);
    void TriggerMode15FKey(int fNum);
    void TriggerMode15F1();
    void TriggerMode15F2();
    void TriggerMode15F3();
    void TriggerMode15F4();

    void TriggerMode16FKey(int fNum);
    void TriggerMode16F1();
    void TriggerMode16F2();
    void TriggerMode16F3();
    void TriggerMode16F4();
    void TriggerMode16F5();
    void TriggerMode16F6();
    void TriggerMode16F7();
    void TriggerMode16F8();

    void TriggerMode17FKey(int fNum);
    void TriggerMode17F1();
    void TriggerMode17F2();
    void TriggerMode17F3();
    void TriggerMode17F4();

    void TriggerMode18FKey(int fNum);
    void TriggerMode18F1();
    void TriggerMode18F2();
    void TriggerMode18F3();
    void TriggerMode18F4();
    void TriggerMode18F5();

    void TriggerMode19FKey(int fNum);
    void TriggerMode19F1();
    void TriggerMode19F2();

    void TriggerMode20FKey(int fNum);
    void TriggerMode20F1();
    void TriggerMode20F2();
    void TriggerMode20F3();
    void TriggerMode20F4();

    bool InstallSimulationHooks();
}

