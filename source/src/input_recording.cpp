#include "recording_device_policy.hpp"
#include "input_recording.hpp"
#include "recording_io.hpp"
#include "reversal_engine.hpp"
#include "input_isolation.hpp"
#include "extra_gameplay.hpp"
#include "imgui_overlay.hpp"
#include "toasty_training.hpp"
#include "minhook/include/MinHook.h"
#include <windows.h>
#include <cstdio>
#include <cstdlib>

namespace MK9TradeFix {

    struct NativeRecordingDevice { uintptr_t pointer=0; unsigned flags=0; int slot=-1, kind=0, port=-1; };
    static bool s_NativePlaybackReady=false;
    static bool ReadNativeRecordingDevice(int player,NativeRecordingDevice& out) {
        if ((player!=1 && player!=2) || !IsTrainingMode()) return false;
        __try {
            void* character=player==1 ? GetP1Char() : GetP2Char();
            if ((uintptr_t)character<0x01000000) return false;
            const uintptr_t info=*(uintptr_t*)((uintptr_t)character+0x7638);
            if (info<0x00400000) return false;
            out.pointer=*(uintptr_t*)(info+0xB40);
            out.slot=RecordingDevicePolicy::DeviceSlot(out.pointer);
            if (out.slot<0 || *(int*)(out.pointer+4)!=out.slot) return false;
            out.flags=*(unsigned*)(out.pointer+8);
            if (!(out.flags&0xC9)) return false;
            const uintptr_t config=0x00E0E1F0+out.slot*0x17C;
            if (*(int*)(config+0x154)) {
                out.kind=2; out.port=*(int*)(config+0x158);
            } else out.kind=*(int*)config==7 ? 1 : 3;
            return true;
        } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    bool InputRecordingManager::NativePlaybackReady() { return s_NativePlaybackReady; }
    const char* InputRecordingManager::GetNativeDeviceName(int player) const {
        NativeRecordingDevice d;
        if (!ReadNativeRecordingDevice(player,d)) return "Not assigned / outside practice";
        if (d.kind==1) return "Keyboard (game bindings)";
        if (d.kind==2) {
            static const char* ports[]={"XInput controller 1","XInput controller 2","XInput controller 3","XInput controller 4"};
            return d.port>=0 && d.port<4 ? ports[d.port] : "XInput controller";
        }
        return "DirectInput device (game bindings)";
    }
    bool InputRecordingManager::UsesNativePlayback() const {
        NativeRecordingDevice d;
        return s_NativePlaybackReady && IsPlaying() && ReadNativeRecordingDevice(m_PlaybackTargetPlayer,d);
    }
    bool InputRecordingManager::ApplyNativePlayback(void* device) {
        if (!s_NativePlaybackReady || !IsPlaying() || !IsTrainingMode() || IsNativeGamePaused()) return false;
        NativeRecordingDevice d;
        if (!ReadNativeRecordingDevice(m_PlaybackTargetPlayer,d) || d.pointer!=(uintptr_t)device) return false;
        if (m_PlaybackSlotIndex<0 || m_PlaybackSlotIndex>=MAX_SLOTS) return false;
        const auto& slot=m_Slots[m_PlaybackSlotIndex];
        unsigned mask=0;
        if (m_DelayRemainingFrames==0 && m_PlaybackFrameCursor<slot.frames.size()) {
            const auto& frame=slot.frames[m_PlaybackFrameCursor];
            mask=RecordingDevicePolicy::Encode(frame.directions,frame.buttons);
        }
        __try {
            *(unsigned*)(d.pointer+0x0C)=mask;
            // Normalization has already run. Override before native edge/history generation.
            for (int offset=0x38;offset<=0x44;offset+=4) *(float*)(d.pointer+offset)=0;
            return true;
        } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    typedef void (__cdecl *NormalizeDeviceFn)(void*,int);
    static NormalizeDeviceFn s_NormalizeKeyboard=nullptr,s_NormalizeXInput=nullptr,s_NormalizeDirectInput=nullptr;
    static void __cdecl NormalizeKeyboard(void* device,int slot) {
        s_NormalizeKeyboard(device,slot); InputRecordingManager::Get().ApplyNativePlayback(device);
    }
    static void __cdecl NormalizeXInput(void* device,int slot) {
        s_NormalizeXInput(device,slot); InputRecordingManager::Get().ApplyNativePlayback(device);
    }
    static void __cdecl NormalizeDirectInput(void* device,int slot) {
        s_NormalizeDirectInput(device,slot); InputRecordingManager::Get().ApplyNativePlayback(device);
    }
    static void InstallNativeRecordingHooks() {
        void* targets[]={(void*)0x00424910,(void*)0x00424310,(void*)0x00424500};
        void* hooks[]={(void*)NormalizeKeyboard,(void*)NormalizeXInput,(void*)NormalizeDirectInput};
        void** originals[]={(void**)&s_NormalizeKeyboard,(void**)&s_NormalizeXInput,(void**)&s_NormalizeDirectInput};
        const unsigned char expected[][6]={{0x55,0x8B,0xEC,0x83,0xEC,0x20},{0x55,0x8B,0xEC,0x83,0xEC,0x20},{0x55,0x8B,0xEC,0x83,0xEC,0x24}};
        bool created[3]={},enabled[3]={};
        for(int i=0;i<3;++i) if(memcmp(targets[i],expected[i],sizeof(expected[i]))!=0) return;
        bool ready=true;
        for(int i=0;i<3;++i) {
            created[i]=MH_CreateHook(targets[i],hooks[i],originals[i])==MH_OK;
            enabled[i]=created[i] && MH_EnableHook(targets[i])==MH_OK;
            ready=ready && enabled[i];
        }
        if(!ready) for(int i=0;i<3;++i) {
            if(enabled[i]) MH_DisableHook(targets[i]);
            if(created[i]) MH_RemoveHook(targets[i]);
        }
        s_NativePlaybackReady=ready;
        LogTrace("[RECORDING] Native assigned-device playback: %s",ready?"READY":"UNAVAILABLE");
    }

    typedef DWORD(WINAPI* tXInputGetState)(DWORD dwUserIndex, XINPUT_STATE* pState);
    typedef DWORD(WINAPI* tXInputGetCapabilities)(DWORD dwUserIndex, DWORD dwFlags, XINPUT_CAPABILITIES* pCapabilities);

    static tXInputGetState oXInputGetState = nullptr;
    static tXInputGetState oXInputGetStateEx = nullptr;
    static tXInputGetCapabilities oXInputGetCapabilities = nullptr;

    static DWORD WINAPI hkXInputGetCapabilities(DWORD dwUserIndex, DWORD dwFlags, XINPUT_CAPABILITIES* pCapabilities) {
        auto& rec = InputRecordingManager::Get();
        auto& rev = ReversalEngine::Get();

        if (pCapabilities) {
            bool overrideCaps = false;
            if (rec.IsPlaying() && !rec.UsesNativePlayback() && dwUserIndex == (DWORD)GetPlayerXInputPort(rec.GetPlaybackTargetPlayer())) {
                overrideCaps = true;
            } else if (HasActiveDiagnosticGamepad(dwUserIndex)) {
                overrideCaps = true;
            } else if (rev.HasActiveGamepadOverride(dwUserIndex)) {
                overrideCaps = true;
            }

            if (overrideCaps) {
                memset(pCapabilities, 0, sizeof(XINPUT_CAPABILITIES));
                pCapabilities->Type = XINPUT_DEVTYPE_GAMEPAD;
                pCapabilities->SubType = XINPUT_DEVSUBTYPE_GAMEPAD;
                pCapabilities->Flags = 0;
                pCapabilities->Gamepad.wButtons = 0xFFFF;
                pCapabilities->Gamepad.bLeftTrigger = 255;
                pCapabilities->Gamepad.bRightTrigger = 255;
                pCapabilities->Gamepad.sThumbLX = (SHORT)32767;
                pCapabilities->Gamepad.sThumbLY = (SHORT)32767;
                pCapabilities->Gamepad.sThumbRX = (SHORT)32767;
                pCapabilities->Gamepad.sThumbRY = (SHORT)32767;
                return ERROR_SUCCESS;
            }
        }
        if (oXInputGetCapabilities) {
            return oXInputGetCapabilities(dwUserIndex, dwFlags, pCapabilities);
        }
        return ERROR_DEVICE_NOT_CONNECTED;
    }

    static DWORD WINAPI hkXInputGetState(DWORD dwUserIndex, XINPUT_STATE* pState) {
        auto& rec = InputRecordingManager::Get();
        auto& rev = ReversalEngine::Get();

        // 1. Playback Overriding (Feeds recorded controller frame directly to the game engine)
        if (rec.IsPlaying() && pState) {
            DWORD targetIndex = (DWORD)GetPlayerXInputPort(rec.GetPlaybackTargetPlayer());
            if (dwUserIndex == targetIndex) {
                if (rec.GetPlaybackGamepad(dwUserIndex, pState)) {
                    return ERROR_SUCCESS;
                }
            }
        }

        // 2. Diagnostic Macros & Test Bot Input Injection (Modes 1..16)
        if (pState && GetActiveDiagnosticGamepad(dwUserIndex, pState)) {
            return ERROR_SUCCESS;
        }

        // 3. Reversal Engine & Practice Dummy Idle Stance Gamepad Injection
        if (pState && rev.GetActiveGamepad(dwUserIndex, pState)) {
            ExtraGameplay::FilterControllerInput(dwUserIndex, pState);
            return ERROR_SUCCESS;
        }

        DWORD res = oXInputGetState ? oXInputGetState(dwUserIndex, pState) : ERROR_DEVICE_NOT_CONNECTED;

        // Auto-Detect Controller Index when requested
        if (res == ERROR_SUCCESS && pState && MK9TradeFix::g_DetectingControllerForPlayer != 0) {
            bool hasInput = (pState->Gamepad.wButtons != 0 || 
                             pState->Gamepad.bLeftTrigger > 40 || 
                             pState->Gamepad.bRightTrigger > 40 || 
                             abs(pState->Gamepad.sThumbLX) > 16000 || 
                             abs(pState->Gamepad.sThumbLY) > 16000);
            if (hasInput) {
                if (MK9TradeFix::g_DetectingControllerForPlayer == 2) {
                    MK9TradeFix::g_P2_XInputPort = (int)dwUserIndex;
                    if (MK9TradeFix::g_P1_XInputPort == (int)dwUserIndex) {
                        MK9TradeFix::g_P1_XInputPort = (dwUserIndex == 0) ? 1 : 0;
                    }
                    MK9TradeFix::LogTrace("[CONTROLLER DETECT] Assigned Player 2 (Dummy) Controller to Port %d (Index %d)", dwUserIndex + 1, dwUserIndex);
                    MK9TradeFix::g_DetectingControllerForPlayer = 0;
                } else if (MK9TradeFix::g_DetectingControllerForPlayer == 1) {
                    MK9TradeFix::g_P1_XInputPort = (int)dwUserIndex;
                    if (MK9TradeFix::g_P2_XInputPort == (int)dwUserIndex) {
                        MK9TradeFix::g_P2_XInputPort = (dwUserIndex == 0) ? 1 : 0;
                    }
                    MK9TradeFix::LogTrace("[CONTROLLER DETECT] Assigned Player 1 Controller to Port %d (Index %d)", dwUserIndex + 1, dwUserIndex);
                    MK9TradeFix::g_DetectingControllerForPlayer = 0;
                }
            }
        }

        // 4. Recording Capture (Captures live controller button presses, stick moves, and triggers)
        if (res == ERROR_SUCCESS && pState && rec.IsRecording()) {
            DWORD targetIndex = (DWORD)GetPlayerXInputPort(rec.GetRecordedTargetPlayer());
            if (dwUserIndex == targetIndex) {
                rec.UpdateLiveGamepad(dwUserIndex, pState->Gamepad);
            }
        }

        if (res == ERROR_SUCCESS && pState) {
            ExtraGameplay::FilterControllerInput(dwUserIndex, pState);
        }

        return res;
    }

    static DWORD WINAPI hkXInputGetStateEx(DWORD dwUserIndex, XINPUT_STATE* pState) {
        auto& rec = InputRecordingManager::Get();
        auto& rev = ReversalEngine::Get();

        // 1. Playback Overriding
        if (rec.IsPlaying() && pState) {
            DWORD targetIndex = (DWORD)GetPlayerXInputPort(rec.GetPlaybackTargetPlayer());
            if (dwUserIndex == targetIndex) {
                if (rec.GetPlaybackGamepad(dwUserIndex, pState)) {
                    return ERROR_SUCCESS;
                }
            }
        }

        // 2. Diagnostic Macros & Test Bot Input Injection (Modes 1..16)
        if (pState && GetActiveDiagnosticGamepad(dwUserIndex, pState)) {
            return ERROR_SUCCESS;
        }

        // 3. Reversal Engine & Practice Dummy Idle Stance Gamepad Injection
        if (pState && rev.GetActiveGamepad(dwUserIndex, pState)) {
            ExtraGameplay::FilterControllerInput(dwUserIndex, pState);
            return ERROR_SUCCESS;
        }

        DWORD res = oXInputGetStateEx ? oXInputGetStateEx(dwUserIndex, pState) : ERROR_DEVICE_NOT_CONNECTED;

        // Auto-Detect Controller Index when requested
        if (res == ERROR_SUCCESS && pState && MK9TradeFix::g_DetectingControllerForPlayer != 0) {
            bool hasInput = (pState->Gamepad.wButtons != 0 || 
                             pState->Gamepad.bLeftTrigger > 40 || 
                             pState->Gamepad.bRightTrigger > 40 || 
                             abs(pState->Gamepad.sThumbLX) > 16000 || 
                             abs(pState->Gamepad.sThumbLY) > 16000);
            if (hasInput) {
                if (MK9TradeFix::g_DetectingControllerForPlayer == 2) {
                    MK9TradeFix::g_P2_XInputPort = (int)dwUserIndex;
                    if (MK9TradeFix::g_P1_XInputPort == (int)dwUserIndex) {
                        MK9TradeFix::g_P1_XInputPort = (dwUserIndex == 0) ? 1 : 0;
                    }
                    MK9TradeFix::LogTrace("[CONTROLLER DETECT] Assigned Player 2 (Dummy) Controller to Port %d (Index %d)", dwUserIndex + 1, dwUserIndex);
                    MK9TradeFix::g_DetectingControllerForPlayer = 0;
                } else if (MK9TradeFix::g_DetectingControllerForPlayer == 1) {
                    MK9TradeFix::g_P1_XInputPort = (int)dwUserIndex;
                    if (MK9TradeFix::g_P2_XInputPort == (int)dwUserIndex) {
                        MK9TradeFix::g_P2_XInputPort = (dwUserIndex == 0) ? 1 : 0;
                    }
                    MK9TradeFix::LogTrace("[CONTROLLER DETECT] Assigned Player 1 Controller to Port %d (Index %d)", dwUserIndex + 1, dwUserIndex);
                    MK9TradeFix::g_DetectingControllerForPlayer = 0;
                }
            }
        }

        // 3. Recording Capture
        if (res == ERROR_SUCCESS && pState && rec.IsRecording()) {
            DWORD targetIndex = (DWORD)GetPlayerXInputPort(rec.GetRecordedTargetPlayer());
            if (dwUserIndex == targetIndex) {
                rec.UpdateLiveGamepad(dwUserIndex, pState->Gamepad);
            }
        }

        if (res == ERROR_SUCCESS && pState) {
            ExtraGameplay::FilterControllerInput(dwUserIndex, pState);
        }

        return res;
    }

    void InputRecordingManager::InstallXInputHooks() {
        InstallNativeRecordingHooks();
        const char* dlls[] = { "xinput1_3.dll", "xinput1_4.dll", "xinput9_1_0.dll" };
        for (int i = 0; i < 3; ++i) {
            HMODULE h = GetModuleHandleA(dlls[i]);
            if (!h) h = LoadLibraryA(dlls[i]);
            if (h) {
                // Hook XInputGetState by name and ordinal 3
                void* pGetState = (void*)GetProcAddress(h, "XInputGetState");
                if (!pGetState) pGetState = (void*)GetProcAddress(h, (LPCSTR)3);
                if (pGetState && !oXInputGetState) {
                    if (MH_CreateHook(pGetState, (void*)hkXInputGetState, (void**)&oXInputGetState) == MH_OK) {
                        MH_EnableHook(pGetState);
                        LogTrace("[+] XInputGetState Hook Installed on %s (0x%p)", dlls[i], pGetState);
                    }
                }

                // Hook XInputGetStateEx (ordinal 100)
                void* pGetStateEx = (void*)GetProcAddress(h, (LPCSTR)100);
                if (pGetStateEx && !oXInputGetStateEx) {
                    if (MH_CreateHook(pGetStateEx, (void*)hkXInputGetStateEx, (void**)&oXInputGetStateEx) == MH_OK) {
                        MH_EnableHook(pGetStateEx);
                        LogTrace("[+] XInputGetStateEx (Ord 100) Hook Installed on %s (0x%p)", dlls[i], pGetStateEx);
                    }
                }

                // Hook XInputGetCapabilities by name and ordinal 2
                void* pGetCaps = (void*)GetProcAddress(h, "XInputGetCapabilities");
                if (!pGetCaps) pGetCaps = (void*)GetProcAddress(h, (LPCSTR)2);
                if (pGetCaps && !oXInputGetCapabilities) {
                    if (MH_CreateHook(pGetCaps, (void*)hkXInputGetCapabilities, (void**)&oXInputGetCapabilities) == MH_OK) {
                        MH_EnableHook(pGetCaps);
                        LogTrace("[+] XInputGetCapabilities Hook Installed on %s (0x%p)", dlls[i], pGetCaps);
                    }
                }
            }
        }
    }

    InputRecordingManager& InputRecordingManager::Get() {
        static InputRecordingManager instance;
        return instance;
    }

    InputRecordingManager::InputRecordingManager() {
        Initialize();
    }

    void InputRecordingManager::Initialize() {
        for (int i = 0; i < MAX_SLOTS; i++) {
            m_Slots[i].id = i + 1;
            snprintf(m_Slots[i].name, sizeof(m_Slots[i].name), "Slot %d", i + 1);
            m_Slots[i].characterName[0] = '\0';
            m_Slots[i].Clear();
        }
        m_ActiveSlotIndex = 0;
        m_State = RecordingState::IDLE;
        m_TriggerMode = RecordingTriggerMode::COUNTDOWN;
        m_PlaybackMode = PlaybackMode::PLAY_ONCE;
        m_CountdownSeconds = 3;
        m_RecordTargetPlayer = 2; // Default record P2 (Dummy) controller
        m_PlaybackTargetPlayer = 2;
        m_ShowHUD = false;
        m_RecordFrameCount = 0;
        m_PlaybackFrameCursor = 0;
        m_PlaybackSlotIndex = 0;
        m_DelayRemainingFrames = 0;
        m_CountdownRemainingFrames = 0;
        m_LastInjectedFrame = InputFrame();
        m_NativeP1Input = InputFrame();
        m_NativeP2Input = InputFrame();
        m_NativeP1LastFrame = 0;
        m_NativeP2LastFrame = 0;
        m_HasLiveGamepadP1 = false;
        m_HasLiveGamepadP2 = false;
    }

    void InputRecordingManager::ResetAll() {
        StopRecording();
        StopPlayback();
        for (int i = 0; i < MAX_SLOTS; i++) {
            m_Slots[i].Clear();
        }
    }

    RecordingSlot& InputRecordingManager::GetSlot(int slotIndex) {
        if (slotIndex < 0) slotIndex = 0;
        if (slotIndex >= MAX_SLOTS) slotIndex = MAX_SLOTS - 1;
        return m_Slots[slotIndex];
    }

    const RecordingSlot& InputRecordingManager::GetSlot(int slotIndex) const {
        if (slotIndex < 0) slotIndex = 0;
        if (slotIndex >= MAX_SLOTS) slotIndex = MAX_SLOTS - 1;
        return m_Slots[slotIndex];
    }

    void InputRecordingManager::ClearSlot(int slotIndex) {
        if (slotIndex >= 0 && slotIndex < MAX_SLOTS) {
            m_Slots[slotIndex].Clear();
            LogTrace("[RECORDING] Cleared Slot %d", slotIndex + 1);
        }
    }

    void InputRecordingManager::SetActiveSlotIndex(int idx) {
        if (idx >= 0 && idx < MAX_SLOTS) {
            m_ActiveSlotIndex = idx;
        }
    }

    bool InputRecordingManager::ExportSlotToFile(int slotIndex, const char* filepath) {
        if (slotIndex < 0 || slotIndex >= MAX_SLOTS) return false;
        const RecordingSlot& slot = m_Slots[slotIndex];
        if (slot.isEmpty || slot.totalFrames == 0) return false;

        char pathBuf[MAX_PATH];
        if (!filepath || filepath[0] == '\0') {
            CreateDirectoryA("recordings", NULL);
            snprintf(pathBuf, sizeof(pathBuf), "recordings\\slot_%d.mkr", slotIndex + 1);
            filepath = pathBuf;
        }

        if (!RecordingIO::Export(filepath, slot, MAX_RECORD_FRAMES)) {
            LogTrace("[RECORDING] Export failed: %s", filepath);
            return false;
        }
        const uint32_t count = slot.totalFrames;
        LogTrace("[RECORDING] Successfully exported Slot %d ('%s' [%s], %u frames) to %s", slotIndex + 1, slot.name, slot.characterName, count, filepath);
        return true;
    }

    bool InputRecordingManager::ImportSlotFromFile(int slotIndex, const char* filepath) {
        if (slotIndex < 0 || slotIndex >= MAX_SLOTS) return false;
        char pathBuf[MAX_PATH];
        if (!filepath || filepath[0] == '\0') {
            snprintf(pathBuf, sizeof(pathBuf), "recordings\\slot_%d.mkr", slotIndex + 1);
            filepath = pathBuf;
        }

        FILE* f = fopen(filepath, "rb");
        if (!f) return false;

        RecordingSlot& slot = m_Slots[slotIndex];
        const bool imported = RecordingIO::Read(f, slot, MAX_RECORD_FRAMES);
        fclose(f);
        if (!imported) return false;
        const uint32_t count = slot.totalFrames;
        LogTrace("[RECORDING] Successfully imported Slot %d ('%s' [%s], %u frames) from %s", slotIndex + 1, slot.name, slot.characterName, count, filepath);
        return true;
    }

    bool InputRecordingManager::ExportAllSlots(const char* directory) {
        CreateDirectoryA(directory ? directory : "recordings", NULL);
        bool anyExported = false;
        for (int i = 0; i < MAX_SLOTS; i++) {
            if (!m_Slots[i].isEmpty && m_Slots[i].totalFrames > 0) {
                char buf[MAX_PATH];
                snprintf(buf, sizeof(buf), "%s\\slot_%d.mkr", directory ? directory : "recordings", i + 1);
                if (ExportSlotToFile(i, buf)) anyExported = true;
            }
        }
        return anyExported;
    }

    bool InputRecordingManager::ImportAllSlots(const char* directory) {
        bool anyImported = false;
        for (int i = 0; i < MAX_SLOTS; i++) {
            char buf[MAX_PATH];
            snprintf(buf, sizeof(buf), "%s\\slot_%d.mkr", directory ? directory : "recordings", i + 1);
            if (ImportSlotFromFile(i, buf)) anyImported = true;
        }
        return anyImported;
    }

    float InputRecordingManager::GetCountdownProgressFraction() const {
        if (m_CountdownSeconds <= 0) return 1.0f;
        float total = (float)(m_CountdownSeconds * 60);
        float remain = (float)m_CountdownRemainingFrames;
        return 1.0f - (remain / total);
    }

    void InputRecordingManager::StartRecording(int slotIndex) {
        if (!IsTrainingMode()) {
            LogTrace("[RECORDING] Blocked: Practice Dummy Recording is restricted to Practice / Training Mode.");
            return;
        }
        if (!IsP2HumanControl()) {
            LogTrace("[RECORDING] Blocked: Player 2 Dummy must be set to [ Human ] in Practice Options.");
            return;
        }

        if (slotIndex < 0 || slotIndex >= MAX_SLOTS) slotIndex = m_ActiveSlotIndex;
        m_ActiveSlotIndex = slotIndex;

        // Stop any active playback
        StopPlayback();

        RecordingSlot& slot = m_Slots[slotIndex];
        slot.Clear();
        slot.frames.reserve(MAX_RECORD_FRAMES);
        m_RecordFrameCount = 0;

        // Query character name from target player
        void* pTargetChar = (m_RecordTargetPlayer == 2) ? GetP2Char() : GetP1Char();
        const char* charName = ToastyTraining::DetectCharacterName(pTargetChar);
        if (charName && *charName) {
            strncpy(slot.characterName, charName, sizeof(slot.characterName) - 1);
            slot.characterName[sizeof(slot.characterName) - 1] = '\0';
        } else {
            snprintf(slot.characterName, sizeof(slot.characterName), "Player %d", m_RecordTargetPlayer);
        }

        // Auto-collapse main menu so player can focus on the game screen
        MK9Overlay::SetConsoleOpen(false);
        m_ShowHUD = true;

        if (m_TriggerMode == RecordingTriggerMode::COUNTDOWN && m_CountdownSeconds > 0) {
            m_State = RecordingState::COUNTDOWN;
            m_CountdownRemainingFrames = m_CountdownSeconds * 60;
            LogTrace("[RECORDING] Arming Slot %d [%s] with %d second countdown...", slotIndex + 1, slot.characterName, m_CountdownSeconds);
        } else if (m_TriggerMode == RecordingTriggerMode::AUTO) {
            m_State = RecordingState::WAITING_FOR_INPUT;
            LogTrace("[RECORDING] Arming Slot %d [%s] (Waiting for first input)...", slotIndex + 1, slot.characterName);
        } else {
            m_State = RecordingState::RECORDING;
            LogTrace("[RECORDING] Started Recording Slot %d [%s] immediately!", slotIndex + 1, slot.characterName);
        }
    }

    void InputRecordingManager::StopRecording() {
        if (m_State == RecordingState::RECORDING || m_State == RecordingState::WAITING_FOR_INPUT || m_State == RecordingState::COUNTDOWN) {
            if (m_ActiveSlotIndex >= 0 && m_ActiveSlotIndex < MAX_SLOTS) {
                RecordingSlot& slot = m_Slots[m_ActiveSlotIndex];
                slot.totalFrames = (uint32_t)slot.frames.size();
                slot.isEmpty = (slot.totalFrames == 0);

                // Update character name if not resolved earlier
                if (slot.characterName[0] == '\0' || strncmp(slot.characterName, "Player", 6) == 0) {
                    void* pTargetChar = (m_RecordTargetPlayer == 2) ? GetP2Char() : GetP1Char();
                    const char* charName = ToastyTraining::DetectCharacterName(pTargetChar);
                    if (charName && *charName) {
                        strncpy(slot.characterName, charName, sizeof(slot.characterName) - 1);
                        slot.characterName[sizeof(slot.characterName) - 1] = '\0';
                    }
                }
                LogTrace("[RECORDING] Stopped Recording Slot %d [%s] (Total Frames: %uF)", m_ActiveSlotIndex + 1, slot.characterName, slot.totalFrames);
            }
            m_State = RecordingState::IDLE;
        }
    }

    void InputRecordingManager::StartPlayback(int slotIndex, int targetPlayer, int delayFrames) {
        if (!IsTrainingMode()) {
            LogTrace("[PLAYBACK] Blocked: Practice Dummy Playback is restricted to Practice / Training Mode.");
            return;
        }
        if (!IsP2HumanControl()) {
            LogTrace("[PLAYBACK] Blocked: Player 2 Dummy must be set to [ Human ] in Practice Options.");
            return;
        }

        if (slotIndex < 0 || slotIndex >= MAX_SLOTS) slotIndex = m_ActiveSlotIndex;
        RecordingSlot& slot = m_Slots[slotIndex];
        if (slot.isEmpty || slot.frames.empty()) {
            LogTrace("[PLAYBACK] Cannot start playback: Slot %d is empty.", slotIndex + 1);
            return;
        }

        StopRecording();
        StopPlayback();

        // Auto-collapse main menu
        MK9Overlay::SetConsoleOpen(false);
        m_ShowHUD = true;

        m_ActiveSlotIndex = slotIndex; // Synchronize HUD, status, highlights and subsequent slot actions.
        m_PlaybackSlotIndex = slotIndex;
        m_PlaybackTargetPlayer = targetPlayer;
        m_DelayRemainingFrames = (delayFrames > 0) ? delayFrames : 0;
        m_PlaybackFrameCursor = 0;
        m_State = RecordingState::PLAYING;

        LogTrace("[PLAYBACK] Started Slot %d Playback on P%d (Total: %uF, Delay: %dF)", 
                 slotIndex + 1, targetPlayer, slot.totalFrames, m_DelayRemainingFrames);
    }

    void InputRecordingManager::StartRandomPlayback(int targetPlayer, int delayFrames) {
        if (!IsTrainingMode()) {
            LogTrace("[PLAYBACK] Blocked: Practice Dummy Playback is restricted to Practice / Training Mode.");
            return;
        }
        if (!IsP2HumanControl()) {
            LogTrace("[PLAYBACK] Blocked: Player 2 Dummy must be set to [ Human ] in Practice Options.");
            return;
        }

        std::vector<int> validSlots;
        for (int i = 0; i < MAX_SLOTS; i++) {
            if (!m_Slots[i].isEmpty && !m_Slots[i].frames.empty()) {
                validSlots.push_back(i);
            }
        }

        if (validSlots.empty()) {
            LogTrace("[PLAYBACK] Cannot start random playback: No non-empty slots.");
            return;
        }

        int pick = validSlots[rand() % validSlots.size()];
        StartPlayback(pick, targetPlayer, delayFrames);
    }

    void InputRecordingManager::StopPlayback() {
        if (m_State == RecordingState::PLAYING) {
            ReleaseAllInjectedKeys(m_PlaybackTargetPlayer);
            m_State = RecordingState::IDLE;
            m_PlaybackFrameCursor = 0;
            m_DelayRemainingFrames = 0;
            m_LastInjectedFrame = InputFrame();
            LogTrace("[PLAYBACK] Playback Stopped.");
        }
    }

    // Hook Method B: Native Engine AppendInputHistory Event Dispatcher
    void InputRecordingManager::OnNativeAppendInput(void* pPlayerInfo, uint32_t entry) {
        if (!pPlayerInfo || (uintptr_t)pPlayerInfo < 0x01000000) return;

        void* p1 = GetP1Char();
        void* p2 = GetP2Char();
        void* p1Info = (p1 && (uintptr_t)p1 > 0x01000000) ? *(void**)((uintptr_t)p1 + 0x7638) : nullptr;
        void* p2Info = (p2 && (uintptr_t)p2 > 0x01000000) ? *(void**)((uintptr_t)p2 + 0x7638) : nullptr;

        InputFrame parsedFrame;
        uint16_t dirCode = (uint16_t)(entry >> 16);
        uint16_t btnMask = (uint16_t)(entry & 0xFFFF);

        parsedFrame.dirToken = ((uint32_t)dirCode << 16);
        parsedFrame.btnMask  = btnMask;

        // Direction mapping from MK9 token IDs
        if (dirCode == 0x8001 || dirCode == 0x8005 || dirCode == 0x8009) parsedFrame.directions |= IN_DIR_UP;
        if (dirCode == 0x8004 || dirCode == 0x8006 || dirCode == 0x800A) parsedFrame.directions |= IN_DIR_DOWN;
        if (dirCode == 0x8008 || dirCode == 0x8009 || dirCode == 0x800A) parsedFrame.directions |= IN_DIR_LEFT;
        if (dirCode == 0x8002 || dirCode == 0x8005 || dirCode == 0x8006) parsedFrame.directions |= IN_DIR_RIGHT;

        // Button mapping
        if (btnMask & 0x0001) parsedFrame.buttons |= IN_BTN_FP;
        if (btnMask & 0x0002) parsedFrame.buttons |= IN_BTN_BP;
        if (btnMask & 0x0004) parsedFrame.buttons |= IN_BTN_FK;
        if (btnMask & 0x0008) parsedFrame.buttons |= IN_BTN_BK;
        if (btnMask & 0x0010) parsedFrame.buttons |= IN_BTN_BLOCK;
        if (btnMask & 0x0020) parsedFrame.buttons |= IN_BTN_TAG;
        if (btnMask & 0x0040) parsedFrame.buttons |= IN_BTN_STANCE;

        if (pPlayerInfo == p2Info) {
            m_NativeP2Input = parsedFrame;
            m_NativeP2LastFrame = g_CurrentFrame;
        } else if (pPlayerInfo == p1Info) {
            m_NativeP1Input = parsedFrame;
            m_NativeP1LastFrame = g_CurrentFrame;
        }
    }

    void InputRecordingManager::UpdateLiveGamepad(DWORD dwUserIndex, const XINPUT_GAMEPAD& gp) {
        if ((int)dwUserIndex == g_P1_XInputPort) {
            m_LiveGamepadP1 = gp;
            m_HasLiveGamepadP1 = true;
        } else if ((int)dwUserIndex == g_P2_XInputPort) {
            m_LiveGamepadP2 = gp;
            m_HasLiveGamepadP2 = true;
        }
    }

    bool InputRecordingManager::GetPlaybackGamepad(DWORD dwUserIndex, XINPUT_STATE* pState) {
        if (UsesNativePlayback()) return false;
        if (!pState || m_PlaybackSlotIndex < 0 || m_PlaybackSlotIndex >= MAX_SLOTS) return false;
        const auto& slot = m_Slots[m_PlaybackSlotIndex];
        if (slot.isEmpty || slot.frames.empty()) return false;

        if (m_PlaybackFrameCursor < slot.frames.size()) {
            const auto& cur = slot.frames[m_PlaybackFrameCursor];
            memset(&pState->Gamepad, 0, sizeof(XINPUT_GAMEPAD));

            if (cur.hasGamepad) {
                pState->Gamepad = cur.gamepad;
                // Also ensure DPAD and stick match
                if (cur.directions & IN_DIR_UP)    { pState->Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_UP;    pState->Gamepad.sThumbLY = 32767; }
                if (cur.directions & IN_DIR_DOWN)  { pState->Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_DOWN;  pState->Gamepad.sThumbLY = -32767; }
                if (cur.directions & IN_DIR_LEFT)  { pState->Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_LEFT;  pState->Gamepad.sThumbLX = -32767; }
                if (cur.directions & IN_DIR_RIGHT) { pState->Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_RIGHT; pState->Gamepad.sThumbLX = 32767; }
            } else {
                if (cur.directions & IN_DIR_UP)    { pState->Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_UP;    pState->Gamepad.sThumbLY = 32767; }
                if (cur.directions & IN_DIR_DOWN)  { pState->Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_DOWN;  pState->Gamepad.sThumbLY = -32767; }
                if (cur.directions & IN_DIR_LEFT)  { pState->Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_LEFT;  pState->Gamepad.sThumbLX = -32767; }
                if (cur.directions & IN_DIR_RIGHT) { pState->Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_RIGHT; pState->Gamepad.sThumbLX = 32767; }

                if (cur.buttons & IN_BTN_FP)     pState->Gamepad.wButtons |= XINPUT_GAMEPAD_X;
                if (cur.buttons & IN_BTN_BP)     pState->Gamepad.wButtons |= XINPUT_GAMEPAD_Y;
                if (cur.buttons & IN_BTN_FK)     pState->Gamepad.wButtons |= XINPUT_GAMEPAD_A;
                if (cur.buttons & IN_BTN_BK)     pState->Gamepad.wButtons |= XINPUT_GAMEPAD_B;
                if (cur.buttons & IN_BTN_BLOCK)  pState->Gamepad.bRightTrigger = 255;
                if (cur.buttons & IN_BTN_STANCE) pState->Gamepad.bLeftTrigger = 255;
                if (cur.buttons & IN_BTN_TAG)    pState->Gamepad.wButtons |= XINPUT_GAMEPAD_LEFT_SHOULDER;
            }
            pState->dwPacketNumber = (DWORD)m_PlaybackFrameCursor + 1;
            return true;
        }
        return false;
    }

    InputFrame InputRecordingManager::CaptureLivePlayerInput(int playerNum) {
        InputFrame frame;
        NativeRecordingDevice device;
        if (!ReadNativeRecordingDevice(playerNum,device)) return frame;
        __try {
            const auto actions=RecordingDevicePolicy::Decode(*(unsigned*)(device.pointer+0x0C));
            frame.directions=actions.directions; frame.buttons=actions.buttons;
            // Keep the existing file format, storing resolved actions instead of guessed VKs.
            frame.btnMask=actions.buttons;
        } __except(EXCEPTION_EXECUTE_HANDLER) { return InputFrame(); }
        return frame;
    }

    void InputRecordingManager::InjectPlayerInput(int playerNum, const InputFrame& currentFrame, const InputFrame& prevFrame) {
        if (s_NativePlaybackReady) return;
        if (g_KeyboardAssignedPlayer != 0 && g_KeyboardAssignedPlayer != playerNum) return;

        auto UpdateKey = [](WORD vk, bool isNowDown, bool wasDown) {
            if (isNowDown && !wasDown) {
                SimulateKeyDown(vk);
            } else if (!isNowDown && wasDown) {
                SimulateKeyUp(vk);
            }
        };

        if (playerNum == 1) {
            // Directions
            UpdateKey('W', (currentFrame.directions & IN_DIR_UP) != 0,    (prevFrame.directions & IN_DIR_UP) != 0);
            UpdateKey('S', (currentFrame.directions & IN_DIR_DOWN) != 0,  (prevFrame.directions & IN_DIR_DOWN) != 0);
            UpdateKey('A', (currentFrame.directions & IN_DIR_LEFT) != 0,  (prevFrame.directions & IN_DIR_LEFT) != 0);
            UpdateKey('D', (currentFrame.directions & IN_DIR_RIGHT) != 0, (prevFrame.directions & IN_DIR_RIGHT) != 0);

            // Also update Arrow keys for P1
            UpdateKey(VK_UP,    (currentFrame.directions & IN_DIR_UP) != 0,    (prevFrame.directions & IN_DIR_UP) != 0);
            UpdateKey(VK_DOWN,  (currentFrame.directions & IN_DIR_DOWN) != 0,  (prevFrame.directions & IN_DIR_DOWN) != 0);
            UpdateKey(VK_LEFT,  (currentFrame.directions & IN_DIR_LEFT) != 0,  (prevFrame.directions & IN_DIR_LEFT) != 0);
            UpdateKey(VK_RIGHT, (currentFrame.directions & IN_DIR_RIGHT) != 0, (prevFrame.directions & IN_DIR_RIGHT) != 0);

            // Buttons: Send both standard PC and legacy bindings
            bool fpNow = (currentFrame.buttons & IN_BTN_FP) != 0, fpWas = (prevFrame.buttons & IN_BTN_FP) != 0;
            bool bpNow = (currentFrame.buttons & IN_BTN_BP) != 0, bpWas = (prevFrame.buttons & IN_BTN_BP) != 0;
            bool fkNow = (currentFrame.buttons & IN_BTN_FK) != 0, fkWas = (prevFrame.buttons & IN_BTN_FK) != 0;
            bool bkNow = (currentFrame.buttons & IN_BTN_BK) != 0, bkWas = (prevFrame.buttons & IN_BTN_BK) != 0;
            bool blkNow = (currentFrame.buttons & IN_BTN_BLOCK) != 0, blkWas = (prevFrame.buttons & IN_BTN_BLOCK) != 0;
            bool tagNow = (currentFrame.buttons & IN_BTN_TAG) != 0, tagWas = (prevFrame.buttons & IN_BTN_TAG) != 0;
            bool stNow = (currentFrame.buttons & IN_BTN_STANCE) != 0, stWas = (prevFrame.buttons & IN_BTN_STANCE) != 0;

            UpdateKey('U', fpNow, fpWas);
            UpdateKey('T', fpNow, fpWas);

            UpdateKey('I', bpNow, bpWas);
            UpdateKey('Y', bpNow, bpWas);

            UpdateKey('J', fkNow, fkWas);
            UpdateKey('G', fkNow, fkWas);

            UpdateKey('K', bkNow, bkWas);
            UpdateKey('H', bkNow, bkWas);
            UpdateKey('L', bkNow, bkWas);

            UpdateKey(VK_SPACE,    blkNow, blkWas);
            UpdateKey('O',         tagNow, tagWas);
            UpdateKey(VK_LSHIFT,   tagNow, tagWas);
            UpdateKey('P',         stNow,  stWas);
            UpdateKey(VK_LCONTROL, stNow,  stWas);
        } else {
            // P2: Pure authentic MK9 Player 2 PC mappings
            // Directions: Arrow Keys ONLY (Never send Numpad 4 or 6 as directions!)
            bool upNow   = (currentFrame.directions & IN_DIR_UP) != 0,   upWas   = (prevFrame.directions & IN_DIR_UP) != 0;
            bool downNow = (currentFrame.directions & IN_DIR_DOWN) != 0, downWas = (prevFrame.directions & IN_DIR_DOWN) != 0;
            bool leftNow = (currentFrame.directions & IN_DIR_LEFT) != 0, leftWas = (prevFrame.directions & IN_DIR_LEFT) != 0;
            bool rgtNow  = (currentFrame.directions & IN_DIR_RIGHT) != 0, rgtWas = (prevFrame.directions & IN_DIR_RIGHT) != 0;

            UpdateKey(VK_UP,    upNow,   upWas);
            UpdateKey(VK_DOWN,  downNow, downWas);
            UpdateKey(VK_LEFT,  leftNow, leftWas);
            UpdateKey(VK_RIGHT, rgtNow,  rgtWas);

            // Secondary Numpad movement (8/2 only, never 4/6)
            UpdateKey(VK_NUMPAD8, upNow,   upWas);
            UpdateKey(VK_NUMPAD2, downNow, downWas);

            // Buttons: Authentic MK9 Numpad mappings
            // FP (1) = Numpad 7
            UpdateKey(VK_NUMPAD7, (currentFrame.buttons & IN_BTN_FP) != 0,    (prevFrame.buttons & IN_BTN_FP) != 0);
            // BP (2) = Numpad 9
            UpdateKey(VK_NUMPAD9, (currentFrame.buttons & IN_BTN_BP) != 0,    (prevFrame.buttons & IN_BTN_BP) != 0);
            // FK (3) = Numpad 4 (Authentic MK9 default FK)
            UpdateKey(VK_NUMPAD4, (currentFrame.buttons & IN_BTN_FK) != 0,    (prevFrame.buttons & IN_BTN_FK) != 0);
            // BK (4) = Numpad 6 (Authentic MK9 default BK)
            UpdateKey(VK_NUMPAD6, (currentFrame.buttons & IN_BTN_BK) != 0,    (prevFrame.buttons & IN_BTN_BK) != 0);
            // Block = Numpad 0
            UpdateKey(VK_NUMPAD0, (currentFrame.buttons & IN_BTN_BLOCK) != 0, (prevFrame.buttons & IN_BTN_BLOCK) != 0);
            // Tag = Numpad Decimal (.)
            UpdateKey(VK_DECIMAL, (currentFrame.buttons & IN_BTN_TAG) != 0,   (prevFrame.buttons & IN_BTN_TAG) != 0);
            // Stance = Numpad Add (+)
            UpdateKey(VK_ADD,     (currentFrame.buttons & IN_BTN_STANCE) != 0,(prevFrame.buttons & IN_BTN_STANCE) != 0);
        }
    }

    void InputRecordingManager::ReleaseAllInjectedKeys(int playerNum) {
        if (s_NativePlaybackReady) { m_LastInjectedFrame=InputFrame(); return; }
        if (g_KeyboardAssignedPlayer != 0 && g_KeyboardAssignedPlayer != playerNum) return;

        if (playerNum == 1) {
            SimulateKeyUp('W');
            SimulateKeyUp('S');
            SimulateKeyUp('A');
            SimulateKeyUp('D');
            SimulateKeyUp(VK_UP);
            SimulateKeyUp(VK_DOWN);
            SimulateKeyUp(VK_LEFT);
            SimulateKeyUp(VK_RIGHT);
            SimulateKeyUp('U');
            SimulateKeyUp('T');
            SimulateKeyUp('I');
            SimulateKeyUp('Y');
            SimulateKeyUp('J');
            SimulateKeyUp('G');
            SimulateKeyUp('K');
            SimulateKeyUp('H');
            SimulateKeyUp('L');
            SimulateKeyUp(VK_SPACE);
            SimulateKeyUp('O');
            SimulateKeyUp(VK_LSHIFT);
            SimulateKeyUp('P');
            SimulateKeyUp(VK_LCONTROL);
        } else {
            SimulateKeyUp(VK_UP);
            SimulateKeyUp(VK_DOWN);
            SimulateKeyUp(VK_LEFT);
            SimulateKeyUp(VK_RIGHT);
            SimulateKeyUp(VK_NUMPAD8);
            SimulateKeyUp(VK_NUMPAD2);
            SimulateKeyUp(VK_NUMPAD7);
            SimulateKeyUp(VK_NUMPAD9);
            SimulateKeyUp(VK_NUMPAD4);
            SimulateKeyUp(VK_NUMPAD6);
            SimulateKeyUp(VK_NUMPAD0);
            SimulateKeyUp(VK_DECIMAL);
            SimulateKeyUp(VK_ADD);
            SimulateKeyUp(VK_NUMPAD1);
            SimulateKeyUp(VK_NUMPAD3);
        }

        m_LastInjectedFrame = InputFrame();
    }

    void InputRecordingManager::OnFrameTick(void* p1Char, void* p2Char) {
        // Enforce game mode and human dummy control restrictions (only required when targeting P2 dummy)
        bool targetNeedsHuman = (m_State == RecordingState::RECORDING || m_State == RecordingState::WAITING_FOR_INPUT || m_State == RecordingState::COUNTDOWN)
                                ? (m_RecordTargetPlayer == 2)
                                : (m_PlaybackTargetPlayer == 2);
        if (!IsTrainingMode() || (targetNeedsHuman && !IsP2HumanControl())) {
            if (IsRecording()) StopRecording();
            if (IsPlaying()) StopPlayback();
            m_ShowHUD = false;
            return;
        }

        // 1. Handle Countdown Phase
        if (m_State == RecordingState::COUNTDOWN) {
            if (m_CountdownRemainingFrames > 0) {
                m_CountdownRemainingFrames--;
            }
            if (m_CountdownRemainingFrames <= 0) {
                m_State = RecordingState::RECORDING;
                m_RecordFrameCount = 0;
                RecordingSlot& slot = m_Slots[m_ActiveSlotIndex];
                if (slot.characterName[0] == '\0' || strncmp(slot.characterName, "Player", 6) == 0) {
                    void* pTargetChar = (m_RecordTargetPlayer == 2) ? (p2Char ? p2Char : GetP2Char()) : (p1Char ? p1Char : GetP1Char());
                    const char* charName = ToastyTraining::DetectCharacterName(pTargetChar);
                    if (charName && *charName) {
                        strncpy(slot.characterName, charName, sizeof(slot.characterName) - 1);
                        slot.characterName[sizeof(slot.characterName) - 1] = '\0';
                    }
                }
                LogTrace("[RECORDING] Countdown reached 0. Recording Slot %d [%s] engaged!", m_ActiveSlotIndex + 1, slot.characterName);
            }
            return;
        }

        // 2. Handle Waiting for First Input (Auto Trigger)
        if (m_State == RecordingState::WAITING_FOR_INPUT) {
            InputFrame current = CaptureLivePlayerInput(m_RecordTargetPlayer);
            if (!current.IsEmpty()) {
                m_State = RecordingState::RECORDING;
                m_RecordFrameCount = 0;
                RecordingSlot& slot = m_Slots[m_ActiveSlotIndex];
                if (slot.characterName[0] == '\0' || strncmp(slot.characterName, "Player", 6) == 0) {
                    void* pTargetChar = (m_RecordTargetPlayer == 2) ? (p2Char ? p2Char : GetP2Char()) : (p1Char ? p1Char : GetP1Char());
                    const char* charName = ToastyTraining::DetectCharacterName(pTargetChar);
                    if (charName && *charName) {
                        strncpy(slot.characterName, charName, sizeof(slot.characterName) - 1);
                        slot.characterName[sizeof(slot.characterName) - 1] = '\0';
                    }
                }
                slot.frames.push_back(current);
                m_RecordFrameCount++;
                LogTrace("[RECORDING] Auto trigger activated! Recording Slot %d [%s] engaged!", m_ActiveSlotIndex + 1, slot.characterName);
            }
            return;
        }

        // 3. Handle Active Recording
        if (m_State == RecordingState::RECORDING) {
            InputFrame current = CaptureLivePlayerInput(m_RecordTargetPlayer);
            RecordingSlot& slot = m_Slots[m_ActiveSlotIndex];

            if (slot.frames.size() < MAX_RECORD_FRAMES) {
                slot.frames.push_back(current);
                m_RecordFrameCount++;
            } else {
                // Buffer capacity limit reached
                StopRecording();
            }
            return;
        }

        // 4. Handle Active Playback
        if (m_State == RecordingState::PLAYING) {
            NativeRecordingDevice assigned;
            if (s_NativePlaybackReady && !ReadNativeRecordingDevice(m_PlaybackTargetPlayer,assigned)) { StopPlayback(); return; }
            if (m_DelayRemainingFrames > 0) {
                m_DelayRemainingFrames--;
                return;
            }

            RecordingSlot& slot = m_Slots[m_PlaybackSlotIndex];
            if (slot.isEmpty || slot.frames.empty()) {
                StopPlayback();
                return;
            }

            if (m_PlaybackFrameCursor < slot.frames.size()) {
                InputFrame current = slot.frames[m_PlaybackFrameCursor];
                InjectPlayerInput(m_PlaybackTargetPlayer, current, m_LastInjectedFrame);
                m_LastInjectedFrame = current;
                m_PlaybackFrameCursor++;
            } else {
                // End of recorded slot reached
                ReleaseAllInjectedKeys(m_PlaybackTargetPlayer);
                if (m_PlaybackMode == PlaybackMode::REPEAT_LOOP) {
                    m_PlaybackFrameCursor = 0;
                } else if (m_PlaybackMode == PlaybackMode::RANDOM_SLOT) {
                    StartRandomPlayback(m_PlaybackTargetPlayer, 0);
                } else {
                    StopPlayback();
                }
            }
        }
    }

}


