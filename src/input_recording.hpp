#pragma once
#include "engine_common.hpp"
#include <vector>
#include <string>
#include <windows.h>
#include <xinput.h>

namespace MK9TradeFix {

    // Input Bitmasks
    enum InputDirectionFlags : uint16_t {
        IN_DIR_NONE  = 0,
        IN_DIR_UP    = (1 << 0),
        IN_DIR_DOWN  = (1 << 1),
        IN_DIR_LEFT  = (1 << 2),
        IN_DIR_RIGHT = (1 << 3)
    };

    enum InputButtonFlags : uint16_t {
        IN_BTN_NONE   = 0,
        IN_BTN_FP     = (1 << 0), // Front Punch (1)
        IN_BTN_BP     = (1 << 1), // Back Punch (2)
        IN_BTN_FK     = (1 << 2), // Front Kick (3)
        IN_BTN_BK     = (1 << 3), // Back Kick (4)
        IN_BTN_BLOCK  = (1 << 4), // Block
        IN_BTN_TAG    = (1 << 5), // Tag
        IN_BTN_STANCE = (1 << 6), // Stance Flip
        IN_BTN_THROW  = (1 << 7)  // Throw (1+3)
    };

    struct InputFrame {
        uint32_t dirToken = 0x80000000; // Native MK9 direction token (0x80000000 Neutral)
        uint32_t btnMask = 0;           // Native MK9 button mask
        uint16_t directions = 0;
        uint16_t buttons = 0;
        XINPUT_GAMEPAD gamepad = { 0 };
        bool hasGamepad = false;

        bool IsEmpty() const {
            bool gpEmpty = (gamepad.wButtons == 0 && abs(gamepad.sThumbLX) < 10000 && abs(gamepad.sThumbLY) < 10000 &&
                            gamepad.bRightTrigger < 30 && gamepad.bLeftTrigger < 30);
            return (directions == 0 && buttons == 0 && btnMask == 0 && (dirToken == 0 || dirToken == 0x80000000) && (!hasGamepad || gpEmpty));
        }
    };

    struct RecordingSlot {
        int id = 0;
        char name[32] = { 0 };
        char characterName[32] = { 0 }; // Character used for this recording
        std::vector<InputFrame> frames;
        uint32_t totalFrames = 0;
        bool isEmpty = true;

        void Clear() {
            frames.clear();
            totalFrames = 0;
            isEmpty = true;
            characterName[0] = '\0';
        }
    };

    enum class RecordingTriggerMode {
        AUTO = 0,       // Starts immediately when first non-neutral input is pressed
        MANUAL = 1,     // Starts immediately on hotkey / button press
        COUNTDOWN = 2   // Starts after countdown (1s, 2s, 3s)
    };

    enum class PlaybackMode {
        PLAY_ONCE = 0,
        REPEAT_LOOP = 1,
        RANDOM_SLOT = 2
    };

    enum class RecordingState {
        IDLE = 0,
        WAITING_FOR_INPUT,
        COUNTDOWN,
        RECORDING,
        PLAYING
    };

    class InputRecordingManager {
    public:
        static const int MAX_SLOTS = 8;
        static const int MAX_RECORD_FRAMES = 14400; // 4 minutes at 60 FPS (formerly 1200 / 20s)

        static InputRecordingManager& Get();
        static void InstallXInputHooks();

        void Initialize();
        void ResetAll();

        // Slot Management
        RecordingSlot& GetSlot(int slotIndex);
        const RecordingSlot& GetSlot(int slotIndex) const;
        void ClearSlot(int slotIndex);
        int GetActiveSlotIndex() const { return m_ActiveSlotIndex; }
        void SetActiveSlotIndex(int idx);

        // Import & Export
        bool ExportSlotToFile(int slotIndex, const char* filepath = nullptr);
        bool ImportSlotFromFile(int slotIndex, const char* filepath = nullptr);
        bool ExportAllSlots(const char* directory = "recordings");
        bool ImportAllSlots(const char* directory = "recordings");

        // Recording Controls
        void StartRecording(int slotIndex);
        void StopRecording();
        bool IsRecording() const { return m_State == RecordingState::RECORDING || m_State == RecordingState::WAITING_FOR_INPUT || m_State == RecordingState::COUNTDOWN; }

        // Playback Controls
        void StartPlayback(int slotIndex, int targetPlayer = 2, int delayFrames = 0);
        void StartRandomPlayback(int targetPlayer = 2, int delayFrames = 0);
        void StopPlayback();
        bool IsPlaying() const { return m_State == RecordingState::PLAYING; }

        // Per-Frame Tick (Invoked from Combat_TickFightFrame)
        void OnFrameTick(void* p1Char, void* p2Char);

        // Native Engine Input Hook
        void OnNativeAppendInput(void* pPlayerInfo, uint32_t entry);

        // XInput Interception
        void UpdateLiveGamepad(DWORD dwUserIndex, const XINPUT_GAMEPAD& gp);
        bool GetPlaybackGamepad(DWORD dwUserIndex, XINPUT_STATE* pState);

        // UI & HUD State Accessors
        RecordingState GetState() const { return m_State; }
        RecordingTriggerMode GetTriggerMode() const { return m_TriggerMode; }
        void SetTriggerMode(RecordingTriggerMode mode) { m_TriggerMode = mode; }
        
        int GetCountdownSeconds() const { return m_CountdownSeconds; }
        void SetCountdownSeconds(int sec) { m_CountdownSeconds = sec; }

        PlaybackMode GetPlaybackMode() const { return m_PlaybackMode; }
        void SetPlaybackMode(PlaybackMode mode) { m_PlaybackMode = mode; }

        int GetRecordedTargetPlayer() const { return m_RecordTargetPlayer; }
        void SetRecordTargetPlayer(int p) { m_RecordTargetPlayer = p; }
        int GetPlaybackTargetPlayer() const { return m_PlaybackTargetPlayer; }
        void SetPlaybackTargetPlayer(int p) { m_PlaybackTargetPlayer = p; }

        uint32_t GetCurrentPlaybackProgress() const { return m_PlaybackFrameCursor; }
        uint32_t GetCurrentRecordProgress() const { return m_RecordFrameCount; }

        bool IsHUDActive() const { return m_ShowHUD; }
        void SetHUDActive(bool show) { m_ShowHUD = show; }
        int GetCountdownRemainingFrames() const { return m_CountdownRemainingFrames; }
        int GetCountdownDisplayNumber() const { return (m_CountdownRemainingFrames + 59) / 60; }
        float GetCountdownProgressFraction() const;

    private:
        InputRecordingManager();
        ~InputRecordingManager() = default;

        InputFrame CaptureLivePlayerInput(int playerNum);
        void InjectPlayerInput(int playerNum, const InputFrame& currentFrame, const InputFrame& prevFrame);
        void ReleaseAllInjectedKeys(int playerNum);

        RecordingSlot m_Slots[MAX_SLOTS];
        int m_ActiveSlotIndex = 0;
        RecordingState m_State = RecordingState::IDLE;
        RecordingTriggerMode m_TriggerMode = RecordingTriggerMode::COUNTDOWN;
        PlaybackMode m_PlaybackMode = PlaybackMode::PLAY_ONCE;
        int m_CountdownSeconds = 3; // Default 3 second countdown
        int m_RecordTargetPlayer = 2; // Default record P2 (Dummy) controller
        int m_PlaybackTargetPlayer = 2; // Default play back onto dummy (P2)
        bool m_ShowHUD = false;

        // Internal counters
        uint32_t m_RecordFrameCount = 0;
        uint32_t m_PlaybackFrameCursor = 0;
        int m_PlaybackSlotIndex = 0;
        int m_DelayRemainingFrames = 0;
        int m_CountdownRemainingFrames = 0;
        InputFrame m_LastInjectedFrame;

        // Live Gamepad Interception Buffer
        XINPUT_GAMEPAD m_LiveGamepadP1 = { 0 };
        XINPUT_GAMEPAD m_LiveGamepadP2 = { 0 };
        bool m_HasLiveGamepadP1 = false;
        bool m_HasLiveGamepadP2 = false;

        // Native Input Event State
        InputFrame m_NativeP1Input;
        InputFrame m_NativeP2Input;
        uint64_t m_NativeP1LastFrame = 0;
        uint64_t m_NativeP2LastFrame = 0;
    };

}

