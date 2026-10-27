#pragma once

#include <windows.h>
#include <string>
#include <vector>

namespace MK9TradeFix {

    struct AnimSequenceEntry {
        std::string name;
        std::string packageName; // Containing package (e.g. CHAR_SubZero, Expressions, etc.)
        int nameIndex;
        int nameNumber;
        float sequenceLength; // In seconds
        int numFrames;
        float rateScale;
        void* pSequence;      // Pointer to UAnimSequence
        int animSetIndex;
    };

    struct ScriptMoveEntry {
        std::string name;
        uint32_t scriptFunc;
        void* pFunction;
    };

    struct QueuedInputStep {
        WORD dir;
        WORD btn;
        WORD btn2;
        int holdFrames;
        int delayAfter;
    };

    class AnimViewer {
    public:
        static void Initialize();
        static void OnMatchStart();
        static void RefreshAll(bool force = false);
        static void RefreshPlayer(int playerIdx); // 1 = P1, 2 = P2
        static void RenderImGuiTab();

        static bool PlayAnimSequence(int playerIdx, const AnimSequenceEntry& entry, float speed, bool loop);
        static bool PlayStunAnimation(int playerIdx, const AnimSequenceEntry& entry, float speed = 1.0f, bool loop = true);
        static bool FindDizzyOrStumbleAnim(int playerIdx, AnimSequenceEntry& outEntry);
        static bool TriggerScriptMove(int playerIdx, const ScriptMoveEntry& entry);
        static bool TriggerAction(int playerIdx, int actionId, const char* label = nullptr);
        static bool QueueMacro(int playerIdx, const std::vector<QueuedInputStep>& steps, const char* label = nullptr, int fallbackActionId = 0);
        static void ResetToNeutralIdle(int playerIdx);
        static void OnFrameTick();
        static void LogAnim(const char* fmt, ...);
        static void ClearLogs();

        static bool IsOpen();
        static void SetOpen(bool open);
        static void ToggleOpen();

    private:
        static void ScanAllAnimSequences(void* p1Char, void* p2Char);
        static void ScanAnimSequences(int playerIdx, void* pChar);
        static void ScanScriptMoves(int playerIdx, void* pChar);
        static std::string ResolveFName(int nameIndex, int nameNumber);
    };

} // namespace MK9TradeFix
