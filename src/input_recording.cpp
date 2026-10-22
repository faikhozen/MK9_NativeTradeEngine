#include "input_recording.hpp"
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
            if (rec.IsPlaying() && dwUserIndex == (DWORD)GetPlayerXInputPort(rec.GetPlaybackTargetPlayer())) {
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

        FILE* f = fopen(filepath, "wb");
        if (!f) return false;

        uint32_t magic = 0x32524B4D; // "MKR2" (Supports character name)
        fwrite(&magic, sizeof(uint32_t), 1, f);
        fwrite(slot.name, sizeof(char), 32, f);
        fwrite(slot.characterName, sizeof(char), 32, f);
        uint32_t count = slot.totalFrames;
        fwrite(&count, sizeof(uint32_t), 1, f);
        if (count > 0 && !slot.frames.empty()) {
            fwrite(slot.frames.data(), sizeof(InputFrame), count, f);
        }
        fclose(f);
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

        uint32_t magic = 0;
        if (fread(&magic, sizeof(uint32_t), 1, f) != 1) {
            fclose(f);
            return false;
        }

        RecordingSlot& slot = m_Slots[slotIndex];
        slot.Clear();

        if (magic == 0x31524B4D) { // "MKR1" Legacy Format
            fread(slot.name, sizeof(char), 32, f);
            slot.name[31] = '\0';
            slot.characterName[0] = '\0';
        } else if (magic == 0x32524B4D) { // "MKR2" New Format
            fread(slot.name, sizeof(char), 32, f);
            slot.name[31] = '\0';
            fread(slot.characterName, sizeof(char), 32, f);
            slot.characterName[31] = '\0';
        } else {
            fclose(f);
            return false;
        }

        uint32_t count = 0;
        if (fread(&count, sizeof(uint32_t), 1, f) != 1 || count > MAX_RECORD_FRAMES) {
            fclose(f);
            return false;
        }

        slot.frames.resize(count);
        if (count > 0) {
            if (fread(slot.frames.data(), sizeof(InputFrame), count, f) != count) {
                slot.Clear();
                fclose(f);
                return false;
            }
        }
        slot.totalFrames = count;
        slot.isEmpty = (count == 0);
        fclose(f);
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

        if (playerNum == 2) {
            // First merge native engine input tokens if captured this frame
            if (m_NativeP2Input.directions != 0 || m_NativeP2Input.buttons != 0) {
                frame.directions |= m_NativeP2Input.directions;
                frame.buttons    |= m_NativeP2Input.buttons;
                frame.dirToken    = m_NativeP2Input.dirToken;
                frame.btnMask     = m_NativeP2Input.btnMask;
            }

            // Player 2 / Dummy Controller Capture (XInput)
            if (m_HasLiveGamepadP2) {
                frame.gamepad = m_LiveGamepadP2;
                frame.hasGamepad = true;
                m_HasLiveGamepadP2 = false;

                if ((frame.gamepad.wButtons & XINPUT_GAMEPAD_DPAD_UP) || frame.gamepad.sThumbLY > 15000)    frame.directions |= IN_DIR_UP;
                if ((frame.gamepad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN) || frame.gamepad.sThumbLY < -15000)  frame.directions |= IN_DIR_DOWN;
                if ((frame.gamepad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT) || frame.gamepad.sThumbLX < -15000) frame.directions |= IN_DIR_LEFT;
                if ((frame.gamepad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) || frame.gamepad.sThumbLX > 15000) frame.directions |= IN_DIR_RIGHT;

                if (frame.gamepad.wButtons & XINPUT_GAMEPAD_X) frame.buttons |= IN_BTN_FP;
                if (frame.gamepad.wButtons & XINPUT_GAMEPAD_Y) frame.buttons |= IN_BTN_BP;
                if (frame.gamepad.wButtons & XINPUT_GAMEPAD_A) frame.buttons |= IN_BTN_FK;
                if (frame.gamepad.wButtons & XINPUT_GAMEPAD_B) frame.buttons |= IN_BTN_BK;
                if (frame.gamepad.bRightTrigger > 30)          frame.buttons |= IN_BTN_BLOCK;
                if (frame.gamepad.bLeftTrigger > 30)           frame.buttons |= IN_BTN_STANCE;
                if (frame.gamepad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) frame.buttons |= IN_BTN_TAG;
            }

            // P2 Keyboard Polling (Standard MK9 P2 bindings):
            // Directions: Arrow keys ONLY (Never treat Numpad 4 or 6 as directions, they are attack kicks!)
            if (GetAsyncKeyState(VK_UP) & 0x8000)    frame.directions |= IN_DIR_UP;
            if (GetAsyncKeyState(VK_DOWN) & 0x8000)  frame.directions |= IN_DIR_DOWN;
            if (GetAsyncKeyState(VK_LEFT) & 0x8000)  frame.directions |= IN_DIR_LEFT;
            if (GetAsyncKeyState(VK_RIGHT) & 0x8000) frame.directions |= IN_DIR_RIGHT;

            // Optional secondary direction keys: Numpad 8 (Up) and Numpad 2 (Down) only
            if (GetAsyncKeyState(VK_NUMPAD8) & 0x8000) frame.directions |= IN_DIR_UP;
            if (GetAsyncKeyState(VK_NUMPAD2) & 0x8000) frame.directions |= IN_DIR_DOWN;

            // Attack buttons (Strictly authentic MK9 PC P2 Numpad layout):
            // FP (1): Numpad 7
            if (GetAsyncKeyState(VK_NUMPAD7) & 0x8000) frame.buttons |= IN_BTN_FP;
            // BP (2): Numpad 9
            if (GetAsyncKeyState(VK_NUMPAD9) & 0x8000) frame.buttons |= IN_BTN_BP;
            // FK (3): Numpad 4 (MK9 standard) or Numpad 1
            if ((GetAsyncKeyState(VK_NUMPAD4) & 0x8000) || (GetAsyncKeyState(VK_NUMPAD1) & 0x8000)) frame.buttons |= IN_BTN_FK;
            // BK (4): Numpad 6 (MK9 standard) or Numpad 3
            if ((GetAsyncKeyState(VK_NUMPAD6) & 0x8000) || (GetAsyncKeyState(VK_NUMPAD3) & 0x8000)) frame.buttons |= IN_BTN_BK;
            // Block: Numpad 0, Enter, RControl
            if ((GetAsyncKeyState(VK_NUMPAD0) & 0x8000) || (GetAsyncKeyState(VK_RETURN) & 0x8000) || (GetAsyncKeyState(VK_RCONTROL) & 0x8000)) frame.buttons |= IN_BTN_BLOCK;
            // Tag: Numpad Decimal (.), RShift
            if ((GetAsyncKeyState(VK_DECIMAL) & 0x8000) || (GetAsyncKeyState(VK_RSHIFT) & 0x8000)) frame.buttons |= IN_BTN_TAG;
            // Stance: Numpad Add (+) (VK_SUBTRACT is reserved for P2 Select)
            if (GetAsyncKeyState(VK_ADD) & 0x8000) frame.buttons |= IN_BTN_STANCE;

            // Fallback: If no arrow keys/numpad used, check left-hand WASD / JIKL / UIJK for P2:
            if (!(frame.directions & (IN_DIR_UP | IN_DIR_DOWN | IN_DIR_LEFT | IN_DIR_RIGHT))) {
                if (GetAsyncKeyState('W') & 0x8000) frame.directions |= IN_DIR_UP;
                if (GetAsyncKeyState('S') & 0x8000) frame.directions |= IN_DIR_DOWN;
                if (GetAsyncKeyState('A') & 0x8000) frame.directions |= IN_DIR_LEFT;
                if (GetAsyncKeyState('D') & 0x8000) frame.directions |= IN_DIR_RIGHT;
            }
            if (!(frame.buttons & (IN_BTN_FP | IN_BTN_BP | IN_BTN_FK | IN_BTN_BK))) {
                if ((GetAsyncKeyState('U') & 0x8000) || (GetAsyncKeyState('T') & 0x8000)) frame.buttons |= IN_BTN_FP;
                if ((GetAsyncKeyState('I') & 0x8000) || (GetAsyncKeyState('Y') & 0x8000)) frame.buttons |= IN_BTN_BP;
                if ((GetAsyncKeyState('J') & 0x8000) || (GetAsyncKeyState('G') & 0x8000)) frame.buttons |= IN_BTN_FK;
                if ((GetAsyncKeyState('K') & 0x8000) || (GetAsyncKeyState('H') & 0x8000)) frame.buttons |= IN_BTN_BK;
                if (GetAsyncKeyState(VK_SPACE) & 0x8000) frame.buttons |= IN_BTN_BLOCK;
            }
        } else {
            // First merge native engine input tokens if captured this frame
            if (m_NativeP1Input.directions != 0 || m_NativeP1Input.buttons != 0) {
                frame.directions |= m_NativeP1Input.directions;
                frame.buttons    |= m_NativeP1Input.buttons;
                frame.dirToken    = m_NativeP1Input.dirToken;
                frame.btnMask     = m_NativeP1Input.btnMask;
            }

            // Player 1 Controller Capture (XInput)
            if (m_HasLiveGamepadP1) {
                frame.gamepad = m_LiveGamepadP1;
                frame.hasGamepad = true;
                m_HasLiveGamepadP1 = false;

                if ((frame.gamepad.wButtons & XINPUT_GAMEPAD_DPAD_UP) || frame.gamepad.sThumbLY > 15000)    frame.directions |= IN_DIR_UP;
                if ((frame.gamepad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN) || frame.gamepad.sThumbLY < -15000)  frame.directions |= IN_DIR_DOWN;
                if ((frame.gamepad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT) || frame.gamepad.sThumbLX < -15000) frame.directions |= IN_DIR_LEFT;
                if ((frame.gamepad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) || frame.gamepad.sThumbLX > 15000) frame.directions |= IN_DIR_RIGHT;

                if (frame.gamepad.wButtons & XINPUT_GAMEPAD_X) frame.buttons |= IN_BTN_FP;
                if (frame.gamepad.wButtons & XINPUT_GAMEPAD_Y) frame.buttons |= IN_BTN_BP;
                if (frame.gamepad.wButtons & XINPUT_GAMEPAD_A) frame.buttons |= IN_BTN_FK;
                if (frame.gamepad.wButtons & XINPUT_GAMEPAD_B) frame.buttons |= IN_BTN_BK;
                if (frame.gamepad.bRightTrigger > 30)          frame.buttons |= IN_BTN_BLOCK;
                if (frame.gamepad.bLeftTrigger > 30)           frame.buttons |= IN_BTN_STANCE;
                if (frame.gamepad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) frame.buttons |= IN_BTN_TAG;
            }

            // P1 Keyboard Polling:
            // Directions: WASD or Arrow Keys
            if ((GetAsyncKeyState('W') & 0x8000) || (GetAsyncKeyState(VK_UP) & 0x8000))    frame.directions |= IN_DIR_UP;
            if ((GetAsyncKeyState('S') & 0x8000) || (GetAsyncKeyState(VK_DOWN) & 0x8000))  frame.directions |= IN_DIR_DOWN;
            if ((GetAsyncKeyState('A') & 0x8000) || (GetAsyncKeyState(VK_LEFT) & 0x8000))  frame.directions |= IN_DIR_LEFT;
            if ((GetAsyncKeyState('D') & 0x8000) || (GetAsyncKeyState(VK_RIGHT) & 0x8000)) frame.directions |= IN_DIR_RIGHT;

            // Attack buttons (Supports authentic MK9 PC P1 keyboard and legacy layouts):
            // FP (1): U, T, 1, or Numpad 7
            if ((GetAsyncKeyState('U') & 0x8000) || (GetAsyncKeyState('T') & 0x8000) || (GetAsyncKeyState('1') & 0x8000) || (GetAsyncKeyState(VK_NUMPAD7) & 0x8000)) frame.buttons |= IN_BTN_FP;
            // BP (2): I, Y, 2, or Numpad 9
            if ((GetAsyncKeyState('I') & 0x8000) || (GetAsyncKeyState('Y') & 0x8000) || (GetAsyncKeyState('2') & 0x8000) || (GetAsyncKeyState(VK_NUMPAD9) & 0x8000)) frame.buttons |= IN_BTN_BP;
            // FK (3): J, G, 3, or Numpad 4
            if ((GetAsyncKeyState('J') & 0x8000) || (GetAsyncKeyState('G') & 0x8000) || (GetAsyncKeyState('3') & 0x8000) || (GetAsyncKeyState(VK_NUMPAD4) & 0x8000)) frame.buttons |= IN_BTN_FK;
            // BK (4): K, H, L, or Numpad 6 (Note: VK_4 is P1 Select, NOT Back Kick!)
            if ((GetAsyncKeyState('K') & 0x8000) || (GetAsyncKeyState('H') & 0x8000) || (GetAsyncKeyState('L') & 0x8000) || (GetAsyncKeyState(VK_NUMPAD6) & 0x8000)) frame.buttons |= IN_BTN_BK;
            // Block: Space, Numpad 0, Enter
            if ((GetAsyncKeyState(VK_SPACE) & 0x8000) || (GetAsyncKeyState(VK_NUMPAD0) & 0x8000) || (GetAsyncKeyState(VK_RETURN) & 0x8000)) frame.buttons |= IN_BTN_BLOCK;
            // Tag: O, LShift, RShift, Numpad Decimal
            if ((GetAsyncKeyState('O') & 0x8000) || (GetAsyncKeyState(VK_LSHIFT) & 0x8000) || (GetAsyncKeyState(VK_RSHIFT) & 0x8000) || (GetAsyncKeyState(VK_DECIMAL) & 0x8000)) frame.buttons |= IN_BTN_TAG;
            // Stance: P, LControl, Numpad Add
            if ((GetAsyncKeyState('P') & 0x8000) || (GetAsyncKeyState(VK_LCONTROL) & 0x8000) || (GetAsyncKeyState(VK_ADD) & 0x8000)) frame.buttons |= IN_BTN_STANCE;
        }

        return frame;
    }

    void InputRecordingManager::InjectPlayerInput(int playerNum, const InputFrame& currentFrame, const InputFrame& prevFrame) {
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
