#include "anim_viewer.hpp"
#include "trade_fix.hpp"
#include "engine_common.hpp"
#include "imgui_overlay.hpp"
#include "imgui/imgui.h"
#include <vector>
#include <string>
#include <algorithm>
#include <cstdio>
#include <cstdarg>

namespace MK9TradeFix {

    static bool s_AnimViewerOpen = false;
    static int s_SelectedPlayer = 1; // 1 = P1, 2 = P2
    static int s_ActiveCategory = 0; // 0 = Anim Sequences, 1 = Character Script Moves
    static int s_ActivePreset = 0;   // 0 = All, 1 = Victory & Intros, 2 = Hurt & Reactions, 3 = Attacks & Specials, 4 = Movement & Defense, 5 = Fatalities & X-Ray

    static std::vector<AnimSequenceEntry> s_P1AnimSequences;
    static std::vector<AnimSequenceEntry> s_P2AnimSequences;
    static std::vector<AnimSequenceEntry> s_AllAnimSequences;

    static std::vector<ScriptMoveEntry> s_P1ScriptMoves;
    static std::vector<ScriptMoveEntry> s_P2ScriptMoves;

    static void* s_LastP1Char = nullptr;
    static void* s_LastP2Char = nullptr;

    static char s_FilterBuffer[128] = "";
    static int s_SelectedAnimIndex = -1;
    static int s_SelectedMoveIndex = -1;

    static float s_PlaybackSpeed = 1.0f;
    static bool s_LoopAnimation = false;
    static float s_BlendIn = 0.1f;
    static float s_BlendOut = 0.1f;

    static char s_StatusMessage[256] = "Ready. Select a player and animation to view.";
    static ImVec4 s_StatusColor = ImVec4(0.5f, 0.8f, 1.0f, 1.0f);

    struct AnimLogItem {
        std::string text;
        ImVec4 color;
        uint64_t frame;
    };
    static std::vector<AnimLogItem> s_AnimExecLogs;
    static bool s_AutoScrollLogs = true;
    static int s_CustomActionId = 1;
    typedef int (__thiscall *tPlayAnimSeq)(void* pChar, void* pAnimSeq, float startTime, float blendIn, int flags);
    static tPlayAnimSeq fnPlayAnimSeq = (tPlayAnimSeq)0x00446E20;

    typedef int (__thiscall *tSkelMesh_PlayAnim)(void* pSkelComp, int nameIndex, float rate, float blendIn, int bLoop, float blendOut);
    static tSkelMesh_PlayAnim fnSkelMeshPlayAnim = (tSkelMesh_PlayAnim)0x006BA740;

    struct QueuedMacro {
        int player;
        std::vector<QueuedInputStep> steps;
        size_t currentStep;
        int phase; // 0 = press, 1 = hold, 2 = delay
        int timer;
        std::string desc;
        bool active;
    };

    static QueuedMacro s_ActiveMacro;

    // =========================================================================
    // SEH Raw C Helper Functions (Avoid C2712 object unwinding conflict)
    // =========================================================================

    static bool SafeGetFNameRaw(int nameIndex, char* outBuf, size_t maxLen) {
        __try {
            void*** pGNamesPtr = (void***)0x00E7C058;
            int* pGNamesCount = (int*)0x00E7C05C;

            if (pGNamesPtr && *pGNamesPtr && pGNamesCount && nameIndex >= 0 && nameIndex < *pGNamesCount) {
                void** gnames = *pGNamesPtr;
                void* entry = gnames[nameIndex];
                if (entry && (uintptr_t)entry >= 0x00400000 && (uintptr_t)entry < 0x7FFE0000) {
                    const char* rawName = (const char*)((uintptr_t)entry + 0x10);
                    if (rawName && (uintptr_t)rawName >= 0x00400000 && (uintptr_t)rawName < 0x7FFE0000 && rawName[0] != '\0') {
                        strncpy_s(outBuf, maxLen, rawName, _TRUNCATE);
                        return true;
                    }
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
        return false;
    }

    struct RawAnimSeqInfo {
        int nameIndex;
        int nameNumber;
        float sequenceLength;
        int numFrames;
        float rateScale;
        void* pSequence;
        int animSetIndex;
    };

    static bool SafeGetCharKeyword(void* pChar, char* outKeyword, size_t maxLen) {
        __try {
            if (!pChar || IsBadReadPtr(pChar, 0x300)) return false;
            uintptr_t pkg = *(uintptr_t*)((uintptr_t)pChar + 0x02A8);
            if (!pkg || IsBadReadPtr((void*)pkg, 0x20)) {
                pkg = *(uintptr_t*)((uintptr_t)pChar + 0x7624);
            }
            if (!pkg || IsBadReadPtr((void*)pkg, 0x20)) return false;

            // In MKScript package, offset +0x14 points to the script file name, e.g. "char_scorpion.mko"
            const char* pFileName = *(const char**)(pkg + 0x14);
            if (pFileName && !IsBadReadPtr(pFileName, 4)) {
                char buf[64];
                strncpy_s(buf, sizeof(buf), pFileName, _TRUNCATE);
                for (int i = 0; buf[i]; i++) buf[i] = (char)tolower((unsigned char)buf[i]);

                char* start = buf;
                if (strncmp(start, "char_", 5) == 0) {
                    start += 5;
                }
                char* dot = strstr(start, ".mko");
                if (dot) *dot = '\0';

                if (start[0] != '\0') {
                    strncpy_s(outKeyword, maxLen, start, _TRUNCATE);
                    return true;
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
        return false;
    }

    static bool SafeGetGObjectsInfo(void*** outGObjects, int* outCount, void** outAnimSetClass) {
        __try {
            void*** pGObjectsData = (void***)0x00E7C0EC;
            int* pGObjectsCount = (int*)0x00E7C0F0;
            void** pAnimSetClass = (void**)0x00E588CC;

            if (pGObjectsData && *pGObjectsData && pGObjectsCount && *pGObjectsCount > 0) {
                *outGObjects = *pGObjectsData;
                *outCount = *pGObjectsCount;
                *outAnimSetClass = pAnimSetClass ? *pAnimSetClass : nullptr;
                return true;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
        return false;
    }

    static bool SafeIsAnimSet(void* pObj, void* animSetClass, void** outOuter, void*** outSeqs, int* outSeqCount) {
        __try {
            if (!pObj || IsBadReadPtr(pObj, 0x60)) return false;
            void* objClass = *(void**)((uintptr_t)pObj + 0x30);
            if (!objClass) return false;

            bool match = false;
            if (animSetClass && objClass == animSetClass) {
                match = true;
            } else {
                int clsNameIdx = *(int*)((uintptr_t)objClass + 0x28);
                char clsName[32] = {0};
                if (SafeGetFNameRaw(clsNameIdx, clsName, sizeof(clsName))) {
                    if (strcmp(clsName, "AnimSet") == 0) {
                        match = true;
                    }
                }
            }

            if (match) {
                *outOuter = *(void**)((uintptr_t)pObj + 0x24);
                void** pSequences = *(void***)((uintptr_t)pObj + 0x54);
                int seqCount = *(int*)((uintptr_t)pObj + 0x58);
                if (pSequences && seqCount > 0 && seqCount <= 5000 && !IsBadReadPtr(pSequences, seqCount * sizeof(void*))) {
                    *outSeqs = pSequences;
                    *outSeqCount = seqCount;
                    return true;
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
        return false;
    }

    static bool SafeGetOuterPackageName(void* pOuter, char* outBuf, size_t maxLen) {
        __try {
            if (!pOuter || IsBadReadPtr(pOuter, 0x30)) return false;
            int nameIdx = *(int*)((uintptr_t)pOuter + 0x28);
            return SafeGetFNameRaw(nameIdx, outBuf, maxLen);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
        return false;
    }

    static bool SafeReadSequenceData(void* pSeq, int animSetIdx, RawAnimSeqInfo* outInfo) {
        __try {
            if (!pSeq || IsBadReadPtr(pSeq, 0x78)) return false;
            outInfo->nameIndex = *(int*)((uintptr_t)pSeq + 0x38);
            outInfo->nameNumber = *(int*)((uintptr_t)pSeq + 0x3C);
            outInfo->rateScale = *(float*)((uintptr_t)pSeq + 0x48);
            outInfo->numFrames = *(int*)((uintptr_t)pSeq + 0x70);
            outInfo->sequenceLength = (outInfo->numFrames > 0) ? (outInfo->numFrames / 60.0f) : 0.0f;
            outInfo->pSequence = pSeq;
            outInfo->animSetIndex = animSetIdx;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
        return false;
    }

    static bool SafeGetScriptPackage(void* pChar, uintptr_t* outPkg, int* outFnCount, uintptr_t* outFnTable) {
        __try {
            if (!pChar || IsBadReadPtr(pChar, 0x300)) return false;
            uintptr_t pkg = *(uintptr_t*)((uintptr_t)pChar + 0x02A8);
            if (!pkg || IsBadReadPtr((void*)pkg, 0x90)) {
                pkg = *(uintptr_t*)((uintptr_t)pChar + 0x7624);
            }
            if (!pkg || IsBadReadPtr((void*)pkg, 0x90)) return false;
            int fnCount = *(int*)(pkg + 0x20);
            uintptr_t pFnTable = *(uintptr_t*)(pkg + 0x8C);
            if (fnCount > 0 && fnCount < 5000 && pFnTable && !IsBadReadPtr((void*)pFnTable, fnCount * sizeof(uintptr_t))) {
                *outPkg = pkg;
                *outFnCount = fnCount;
                *outFnTable = pFnTable;
                return true;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
        return false;
    }

    static bool SafeReadScriptFunction(uintptr_t pFnTable, int i, char* outName, size_t maxLen, uint32_t* outSFunc, void** outFnPtr) {
        __try {
            uintptr_t pFn = *(uintptr_t*)(pFnTable - 4 + i * 4);
            if (!pFn || IsBadReadPtr((void*)pFn, 8)) return false;
            uintptr_t pSym = *(uintptr_t*)(pFn + 0x00);
            uint32_t sFunc = *(uint32_t*)(pFn + 0x04);
            if (pSym && !IsBadReadPtr((void*)pSym, 4)) {
                const char* str = (const char*)pSym;
                if (str && str[0] >= 32 && str[0] <= 126) {
                    strncpy_s(outName, maxLen, str, _TRUNCATE);
                    *outSFunc = sFunc;
                    *outFnPtr = (void*)pFn;
                    return true;
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
        return false;
    }

    static void* SafeGetFighterInfo(int playerIdx, void* pChar) {
        __try {
            if (pChar && !IsBadReadPtr(pChar, 0x7640)) {
                void* pInfo = *(void**)((uintptr_t)pChar + 0x7638);
                if (pInfo && !IsBadReadPtr(pInfo, 0x30)) return pInfo;
            }
            void* pInfo = (playerIdx == 1) ? (void*)0x00E89D7C : (void*)0x00E8AA08;
            if (pInfo && !IsBadReadPtr(pInfo, 0x30)) return pInfo;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
        return nullptr;
    }

    static bool SafeGetPlayerSwapped() {
        __try {
            void* p1 = GetP1Char();
            void* p2 = GetP2Char();
            if (p1 && p2 && (uintptr_t)p1 > 0x01000000 && (uintptr_t)p2 > 0x01000000) {
                float p1_x = *(float*)((uintptr_t)p1 + 0x1E8);
                float p2_x = *(float*)((uintptr_t)p2 + 0x1E8);
                return (p1_x > p2_x);
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
        return false;
    }

    static bool SafeInvokePlayAnimDirect(void* pChar, void* pAnimSeq, int nameIndex, float rate, float blendIn, float blendOut, int bLoop, int* outRes) {
        bool invoked = false;

        // Clear ActionState so the combat engine state machine doesn't immediately snap back to stance
        __try {
            if (pChar && !IsBadReadPtr(pChar, 0x1E0)) {
                *(int*)((uintptr_t)pChar + 0x1D4) = 0;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}

        // Priority 1: Direct MKCharacter Animation Function (0x00446E20 via vtable[0x6C] or direct)
        __try {
            if (pChar && !IsBadReadPtr(pChar, 0x120) && pAnimSeq && !IsBadReadPtr(pAnimSeq, 0x80)) {
                void* pVtable = *(void**)pChar;
                tPlayAnimSeq fn = (pVtable && !IsBadReadPtr(pVtable, 0x70)) ? (tPlayAnimSeq)*(void**)((uintptr_t)pVtable + 0x6C) : nullptr;
                if (!fn) fn = fnPlayAnimSeq;
                if (fn) {
                    // Set RateScale on the anim sequence object
                    if (rate > 0.01f && rate < 10.0f) {
                        *(float*)((uintptr_t)pAnimSeq + 0x48) = rate;
                    }
                    int res = fn(pChar, pAnimSeq, 0.0f, blendIn, bLoop ? 2 : 0);
                    if (outRes) *outRes = res;
                    invoked = true;
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}

        // Priority 2: Also trigger native UE3 USkeletalMeshComponent::PlayAnim (0x006BA740) on the mesh component
        __try {
            if (pChar && !IsBadReadPtr(pChar, 0x120)) {
                void* pSkelComp = *(void**)((uintptr_t)pChar + 0x010C);
                if (pSkelComp && !IsBadReadPtr(pSkelComp, 0x2A0)) {
                    fnSkelMeshPlayAnim(pSkelComp, nameIndex, rate, blendIn, bLoop, blendOut);
                    invoked = true;
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}

        return invoked;
    }

    static bool SafeInvokeTriggerMove(void* pInfo, int actionId, void* pChar, int* outRes) {
        __try {
            typedef int (*tTriggerMove)(void* pInfo, int actionId, int unk1, int priority, int unk3);
            tTriggerMove fnTrigger = (tTriggerMove)0x00889B90;
            if (fnTrigger && pInfo && !IsBadReadPtr(pInfo, 0x20)) {
                int res = fnTrigger(pInfo, actionId, 1, 1000, 2);
                if (outRes) *outRes = res;
                if (pChar && !IsBadReadPtr(pChar, 0x1E0)) {
                    *(int*)((uintptr_t)pChar + 0x1D4) = actionId;
                }
                return true;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
        return false;
    }

    static bool SafeResetAction(void* pChar, void* pInfo) {
        __try {
            if (pChar && !IsBadReadPtr(pChar, 0x1E0)) {
                *(int*)((uintptr_t)pChar + 0x1D4) = 0;
            }
            if (pInfo && !IsBadReadPtr(pInfo, 0xB50)) {
                void* pAnimPlayer = *(void**)((uintptr_t)pInfo + 0xB40);
                if (pAnimPlayer && !IsBadReadPtr(pAnimPlayer, 0x20)) {
                    *(uint32_t*)((uintptr_t)pAnimPlayer + 0xC) &= ~0x04;
                }
            }
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
        return false;
    }

    static void SafeSetScriptFunc(void* pChar, void* pFn) {
        __try {
            if (pChar && !IsBadReadPtr(pChar, 0x2FA0) && pFn && (uintptr_t)pFn > 0x00400000) {
                *(void**)((uintptr_t)pChar + 0x2F88) = pFn;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    void AnimViewer::LogAnim(const char* fmt, ...) {
        char buf[512];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);

        ImVec4 col = ImVec4(0.9f, 0.9f, 0.9f, 1.0f);
        if (strstr(buf, "[+]") || strstr(buf, "SUCCESS") || strstr(buf, "dispatched") || strstr(buf, "refreshed")) {
            col = ImVec4(0.3f, 1.0f, 0.4f, 1.0f);
        } else if (strstr(buf, "[-]") || strstr(buf, "Error") || strstr(buf, "Failed")) {
            col = ImVec4(1.0f, 0.4f, 0.3f, 1.0f);
        } else if (strstr(buf, "[EXECUTE]") || strstr(buf, "[TRIGGER]")) {
            col = ImVec4(1.0f, 0.85f, 0.3f, 1.0f);
        } else if (strstr(buf, "[INFO]") || strstr(buf, "[DEBUG]") || strstr(buf, "[#]")) {
            col = ImVec4(0.4f, 0.8f, 1.0f, 1.0f);
        }

        AnimLogItem item;
        item.text = buf;
        item.color = col;
        item.frame = g_CurrentFrame;
        s_AnimExecLogs.push_back(item);
        if (s_AnimExecLogs.size() > 100) {
            s_AnimExecLogs.erase(s_AnimExecLogs.begin());
        }

        strncpy_s(s_StatusMessage, sizeof(s_StatusMessage), buf, _TRUNCATE);
        s_StatusColor = col;

        MK9Overlay::AppendLog(buf, g_CurrentFrame);

        OutputDebugStringA(buf);
        OutputDebugStringA("\n");

        static FILE* s_AnimLogFile = nullptr;
        static bool s_AnimLogFileInit = false;
        if (!s_AnimLogFileInit) {
            s_AnimLogFileInit = true;
            s_AnimLogFile = _fsopen("MK9_ANIM_VIEWER.LOG", "a", _SH_DENYNO);
        }
        if (s_AnimLogFile) {
            fprintf(s_AnimLogFile, "[F%llu] %s\n", g_CurrentFrame, buf);
            fflush(s_AnimLogFile);
        }
    }

    void AnimViewer::ClearLogs() {
        s_AnimExecLogs.clear();
    }

    // =========================================================================
    // AnimViewer Class Methods
    // =========================================================================

    void AnimViewer::Initialize() {
        s_AnimViewerOpen = false;
        s_SelectedPlayer = 1;
        s_SelectedAnimIndex = -1;
        s_SelectedMoveIndex = -1;
        s_P1AnimSequences.clear();
        s_P2AnimSequences.clear();
        s_AllAnimSequences.clear();
        s_P1ScriptMoves.clear();
        s_P2ScriptMoves.clear();
        s_LastP1Char = nullptr;
        s_LastP2Char = nullptr;
    }

    void AnimViewer::OnMatchStart() {
        RefreshAll(true);
    }

    bool AnimViewer::IsOpen() {
        return s_AnimViewerOpen;
    }

    void AnimViewer::SetOpen(bool open) {
        s_AnimViewerOpen = open;
    }

    void AnimViewer::ToggleOpen() {
        s_AnimViewerOpen = !s_AnimViewerOpen;
        if (s_AnimViewerOpen) {
            RefreshAll(false);
        }
    }

    std::string AnimViewer::ResolveFName(int nameIndex, int nameNumber) {
        if (nameIndex < 0) return "None";

        char rawBuf[128];
        rawBuf[0] = '\0';

        if (SafeGetFNameRaw(nameIndex, rawBuf, sizeof(rawBuf))) {
            if (nameNumber > 0) {
                char buf[128];
                snprintf(buf, sizeof(buf), "%s_%d", rawBuf, nameNumber - 1);
                return std::string(buf);
            }
            return std::string(rawBuf);
        }

        return "Unknown";
    }

    void AnimViewer::ScanAllAnimSequences(void* p1Char, void* p2Char) {
        s_P1AnimSequences.clear();
        s_P2AnimSequences.clear();
        s_AllAnimSequences.clear();

        void** gobjects = nullptr;
        int gobjectsCount = 0;
        void* animSetClass = nullptr;

        if (!SafeGetGObjectsInfo(&gobjects, &gobjectsCount, &animSetClass)) {
            return;
        }

        char p1Keyword[64] = {0};
        char p2Keyword[64] = {0};
        bool hasP1Key = SafeGetCharKeyword(p1Char, p1Keyword, sizeof(p1Keyword));
        bool hasP2Key = SafeGetCharKeyword(p2Char, p2Keyword, sizeof(p2Keyword));

        int animSetIdx = 0;
        for (int i = 0; i < gobjectsCount; i++) {
            void* pObj = gobjects[i];
            if (!pObj) continue;

            void* pOuter = nullptr;
            void** pSequences = nullptr;
            int seqCount = 0;

            if (SafeIsAnimSet(pObj, animSetClass, &pOuter, &pSequences, &seqCount)) {
                char outerName[128] = {0};
                SafeGetOuterPackageName(pOuter, outerName, sizeof(outerName));
                std::string pkgStr = outerName;
                std::string pkgLower = pkgStr;
                for (char& c : pkgLower) c = (char)tolower((unsigned char)c);

                bool matchesP1 = hasP1Key && (pkgLower.find(p1Keyword) != std::string::npos);
                bool matchesP2 = hasP2Key && (pkgLower.find(p2Keyword) != std::string::npos);

                for (int j = 0; j < seqCount; j++) {
                    void* pSeq = pSequences[j];
                    RawAnimSeqInfo rawInfo;
                    if (SafeReadSequenceData(pSeq, animSetIdx, &rawInfo)) {
                        AnimSequenceEntry entry;
                        entry.name = ResolveFName(rawInfo.nameIndex, rawInfo.nameNumber);
                        entry.packageName = pkgStr;
                        entry.nameIndex = rawInfo.nameIndex;
                        entry.nameNumber = rawInfo.nameNumber;
                        entry.sequenceLength = rawInfo.sequenceLength;
                        entry.numFrames = rawInfo.numFrames;
                        entry.rateScale = rawInfo.rateScale;
                        entry.pSequence = rawInfo.pSequence;
                        entry.animSetIndex = animSetIdx;

                        s_AllAnimSequences.push_back(entry);
                        if (matchesP1) {
                            s_P1AnimSequences.push_back(entry);
                        }
                        if (matchesP2) {
                            s_P2AnimSequences.push_back(entry);
                        }
                    }
                }
                animSetIdx++;
            }
        }
    }

    void AnimViewer::ScanAnimSequences(int playerIdx, void* pChar) {
        // Main scanning is handled centrally across GObjects via ScanAllAnimSequences
    }

    void AnimViewer::ScanScriptMoves(int playerIdx, void* pChar) {
        auto& targetList = (playerIdx == 1) ? s_P1ScriptMoves : s_P2ScriptMoves;
        targetList.clear();

        if (!pChar) return;

        uintptr_t pkg = 0;
        int fnCount = 0;
        uintptr_t pFnTable = 0;

        if (SafeGetScriptPackage(pChar, &pkg, &fnCount, &pFnTable)) {
            for (int i = 0; i < fnCount; i++) {
                char fnName[128];
                fnName[0] = '\0';
                uint32_t sFunc = 0;
                void* fnPtr = nullptr;
                if (SafeReadScriptFunction(pFnTable, i, fnName, sizeof(fnName), &sFunc, &fnPtr)) {
                    ScriptMoveEntry entry;
                    entry.name = fnName;
                    entry.scriptFunc = sFunc;
                    entry.pFunction = fnPtr;
                    targetList.push_back(entry);
                }
            }
        }
    }

    void AnimViewer::RefreshPlayer(int playerIdx) {
        RefreshAll(true);
    }

    void AnimViewer::RefreshAll(bool force) {
        void* p1 = GetP1Char();
        void* p2 = GetP2Char();

        ScanAllAnimSequences(p1, p2);
        ScanScriptMoves(1, p1);
        ScanScriptMoves(2, p2);

        s_LastP1Char = p1;
        s_LastP2Char = p2;

        snprintf(s_StatusMessage, sizeof(s_StatusMessage), "Catalog refreshed: P1 (%zu anims, %zu moves) | P2 (%zu anims, %zu moves) | Global (%zu anims)",
                 s_P1AnimSequences.size(), s_P1ScriptMoves.size(),
                 s_P2AnimSequences.size(), s_P2ScriptMoves.size(),
                 s_AllAnimSequences.size());
        s_StatusColor = ImVec4(0.4f, 1.0f, 0.4f, 1.0f);
    }

    void AnimViewer::OnFrameTick() {
        if (!s_ActiveMacro.active) return;

        if (s_ActiveMacro.currentStep >= s_ActiveMacro.steps.size()) {
            ReleaseAllSimulationKeys();
            s_ActiveMacro.active = false;
            LogAnim("[+] P%d '%s' input sequence completed.",
                    s_ActiveMacro.player, s_ActiveMacro.desc.c_str());
            return;
        }

        auto& step = s_ActiveMacro.steps[s_ActiveMacro.currentStep];

        if (s_ActiveMacro.phase == 0) {
            // Press inputs
            if (step.dir)  SimulateKeyDown(step.dir);
            if (step.btn)  SimulateKeyDown(step.btn);
            if (step.btn2) SimulateKeyDown(step.btn2);
            s_ActiveMacro.phase = 1;
            s_ActiveMacro.timer = (step.holdFrames > 0) ? step.holdFrames : 3;
        } else if (s_ActiveMacro.phase == 1) {
            s_ActiveMacro.timer--;
            if (s_ActiveMacro.timer <= 0) {
                // Release inputs
                if (step.dir)  SimulateKeyUp(step.dir);
                if (step.btn)  SimulateKeyUp(step.btn);
                if (step.btn2) SimulateKeyUp(step.btn2);
                s_ActiveMacro.phase = 2;
                s_ActiveMacro.timer = (step.delayAfter > 0) ? step.delayAfter : 1;
            }
        } else if (s_ActiveMacro.phase == 2) {
            s_ActiveMacro.timer--;
            if (s_ActiveMacro.timer <= 0) {
                // Advance step
                s_ActiveMacro.currentStep++;
                s_ActiveMacro.phase = 0;
            }
        }
    }

    bool AnimViewer::QueueMacro(int playerIdx, const std::vector<QueuedInputStep>& steps, const char* label, int fallbackActionId) {
        void* pChar = (playerIdx == 1) ? GetP1Char() : GetP2Char();
        if (!pChar) {
            LogAnim("[-] Error: Player %d character is not currently active.", playerIdx);
            return false;
        }

        void* pInfo = SafeGetFighterInfo(playerIdx, pChar);
        const char* actLabel = label ? label : "Macro";

        // Cancel previous macro and release keys
        if (s_ActiveMacro.active) {
            ReleaseAllSimulationKeys();
        }

        s_ActiveMacro.player = playerIdx;
        s_ActiveMacro.desc = actLabel;
        s_ActiveMacro.steps = steps;
        s_ActiveMacro.currentStep = 0;
        s_ActiveMacro.phase = 0;
        s_ActiveMacro.timer = 0;
        s_ActiveMacro.active = !steps.empty();

        if (fallbackActionId > 0) {
            int res = 0;
            SafeInvokeTriggerMove(pInfo, fallbackActionId, pChar, &res);
        }

        bool swapped = SafeGetPlayerSwapped();
        LogAnim("[+] P%d '%s' queued! (%zu steps, Swapped: %s)",
                playerIdx, actLabel, steps.size(), swapped ? "YES" : "NO");

        return true;
    }

    bool AnimViewer::TriggerAction(int playerIdx, int actionId, const char* label) {
        void* pChar = (playerIdx == 1) ? GetP1Char() : GetP2Char();
        if (!pChar) {
            LogAnim("[-] Error: Player %d character is not currently active.", playerIdx);
            return false;
        }

        const char* actLabel = label ? label : "Move";
        LogAnim("[EXECUTE] P%d Action 0x%02X (%s) | Queuing native input injection...",
                playerIdx, actionId, actLabel);

        bool swapped = SafeGetPlayerSwapped();
        WORD fwd  = (playerIdx == 1) ? (swapped ? 'A' : 'D') : (swapped ? VK_RIGHT : VK_LEFT);
        WORD back = (playerIdx == 1) ? (swapped ? 'D' : 'A') : (swapped ? VK_LEFT : VK_RIGHT);
        WORD up   = (playerIdx == 1) ? 'W' : VK_UP;
        WORD down = (playerIdx == 1) ? 'S' : VK_DOWN;
        WORD fp   = (playerIdx == 1) ? 'T' : VK_NUMPAD7;
        WORD bp   = (playerIdx == 1) ? 'U' : VK_NUMPAD9;
        WORD fk   = (playerIdx == 1) ? 'G' : VK_NUMPAD4;
        WORD bk   = (playerIdx == 1) ? 'J' : VK_NUMPAD6;
        WORD blk  = (playerIdx == 1) ? VK_SPACE : VK_NUMPAD0;
        WORD thr  = (playerIdx == 1) ? VK_OEM_1 : VK_ADD;
        WORD stc  = (playerIdx == 1) ? 'Q' : VK_NUMPAD1;

        std::vector<QueuedInputStep> steps;

        switch (actionId) {
            case 0x01: // Attack 1 (Front Punch)
                steps.push_back({ 0, fp, 0, 3, 2 });
                break;
            case 0x02: // Attack 2 (Back Punch)
                steps.push_back({ 0, bp, 0, 3, 2 });
                break;
            case 0x03: // Attack 3 (Front Kick)
                steps.push_back({ 0, fk, 0, 3, 2 });
                break;
            case 0x04: // Attack 4 (Back Kick)
                steps.push_back({ 0, bk, 0, 3, 2 });
                break;
            case 0x05: // Crouch / Duck
                steps.push_back({ down, 0, 0, 16, 2 });
                break;
            case 0x06: // Jump
                steps.push_back({ up, 0, 0, 4, 2 });
                break;
            case 0x07: // Block
                steps.push_back({ 0, blk, 0, 16, 2 });
                break;
            case 0x0C: // Dash Forward
                steps.push_back({ fwd, 0, 0, 2, 2 });
                steps.push_back({ fwd, 0, 0, 3, 2 });
                break;
            case 0x0D: // Dash Backward
                steps.push_back({ back, 0, 0, 2, 2 });
                steps.push_back({ back, 0, 0, 3, 2 });
                break;
            case 0x10: // Special 1 (e.g. D, F + 1)
                steps.push_back({ down, 0, 0, 2, 2 });
                steps.push_back({ fwd, fp, 0, 3, 2 });
                break;
            case 0x11: // Special 2 (e.g. D, B + 1)
                steps.push_back({ down, 0, 0, 2, 2 });
                steps.push_back({ back, fp, 0, 3, 2 });
                break;
            case 0x12: // Special 3 (e.g. B, F + 2)
                steps.push_back({ back, 0, 0, 2, 2 });
                steps.push_back({ fwd, bp, 0, 3, 2 });
                break;
            case 0x13: // Special 4 (e.g. D, B + 3)
                steps.push_back({ down, 0, 0, 2, 2 });
                steps.push_back({ back, fk, 0, 3, 2 });
                break;
            case 0x18: // X-Ray
                steps.push_back({ 0, stc, blk, 8, 2 });
                break;
            case 0x1A: // Taunt / Stance Switch
                steps.push_back({ 0, stc, 0, 4, 2 });
                break;
            case 0x1B: // Throw / Grab
                steps.push_back({ 0, thr, 0, 4, 2 });
                break;
            case 0x20: // Uppercut (D+2)
                steps.push_back({ down, bp, 0, 4, 2 });
                break;
            case 0x21: // Sweep (D+4)
                steps.push_back({ down, bk, 0, 4, 2 });
                break;
            case 0x22: // Jump Forward + Attack 1
                steps.push_back({ up, fwd, 0, 6, 13 });
                steps.push_back({ 0, fp, 0, 3, 2 });
                break;
            case 0x23: // Jump Backward + Attack 1
                steps.push_back({ up, back, 0, 6, 13 });
                steps.push_back({ 0, fp, 0, 3, 2 });
                break;
            case 0x24: // Jump Forward + Attack 4
                steps.push_back({ up, fwd, 0, 6, 13 });
                steps.push_back({ 0, bk, 0, 3, 2 });
                break;
            default:
                steps.push_back({ 0, fp, 0, 3, 2 });
                break;
        }

        return QueueMacro(playerIdx, steps, actLabel, actionId);
    }

    bool AnimViewer::PlayAnimSequence(int playerIdx, const AnimSequenceEntry& entry, float speed, bool loop) {
        void* pChar = (playerIdx == 1) ? GetP1Char() : GetP2Char();
        if (!pChar) {
            LogAnim("[-] Error: Player %d character is not currently active.", playerIdx);
            return false;
        }

        if (entry.numFrames == 0) {
            LogAnim("[-] '%s' is a static 0-frame bind/cloth mesh pose (non-playable clip).", entry.name.c_str());
            return false;
        }

        LogAnim("[ANIM] P%d Direct Skeletal PlayAnim: '%s' (Frames: %d, Speed: %.2fx, Loop: %s)",
                playerIdx, entry.name.c_str(), entry.numFrames, speed, loop ? "YES" : "NO");

        int res = 0;
        bool success = SafeInvokePlayAnimDirect(pChar, entry.pSequence, entry.nameIndex, speed, s_BlendIn, s_BlendOut, loop ? 1 : 0, &res);

        if (success) {
            LogAnim("[+] P%d Playing animation '%s' directly on skeletal mesh! (pSeq=0x%08X, res=%d)",
                    playerIdx, entry.name.c_str(), (uintptr_t)entry.pSequence, res);
        } else {
            LogAnim("[-] Failed to invoke direct animation playback for '%s' on P%d.", entry.name.c_str(), playerIdx);
        }

        return success;
    }

    static AnimSequenceEntry s_CachedStunAnimP1;
    static AnimSequenceEntry s_CachedStunAnimP2;
    static void* s_LastStunP1Char = nullptr;
    static void* s_LastStunP2Char = nullptr;
    static bool s_CachedStunFoundP1 = false;
    static bool s_CachedStunFoundP2 = false;

    bool AnimViewer::PlayStunAnimation(int playerIdx, const AnimSequenceEntry& entry, float speed, bool loop) {
        void* pChar = (playerIdx == 1) ? GetP1Char() : GetP2Char();
        if (!pChar || !entry.pSequence || entry.numFrames <= 0) return false;

        bool invoked = false;

        // 1. Play directly via MKCharacter::PlayAnimSeq (0x00446E20)
        __try {
            void* pVtable = *(void**)pChar;
            tPlayAnimSeq fn = (pVtable && !IsBadReadPtr(pVtable, 0x70)) ? (tPlayAnimSeq)*(void**)((uintptr_t)pVtable + 0x6C) : nullptr;
            if (!fn) fn = fnPlayAnimSeq;
            if (fn) {
                if (speed > 0.01f && speed < 10.0f) {
                    *(float*)((uintptr_t)entry.pSequence + 0x48) = speed;
                }
                fn(pChar, entry.pSequence, 0.0f, 0.15f, loop ? 2 : 0);
                invoked = true;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}

        // 2. Play directly on USkeletalMeshComponent (0x006BA740)
        __try {
            void* pSkelComp = *(void**)((uintptr_t)pChar + 0x010C);
            if (pSkelComp && !IsBadReadPtr(pSkelComp, 0x2A0)) {
                fnSkelMeshPlayAnim(pSkelComp, entry.nameIndex, speed, 0.15f, loop ? 1 : 0, 0.15f);
                invoked = true;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}

        return invoked;
    }

    bool AnimViewer::FindDizzyOrStumbleAnim(int playerIdx, AnimSequenceEntry& outEntry) {
        void* pChar = (playerIdx == 1) ? GetP1Char() : GetP2Char();
        if (!pChar) return false;

        void*& lastChar = (playerIdx == 1) ? s_LastStunP1Char : s_LastStunP2Char;
        AnimSequenceEntry& cachedEntry = (playerIdx == 1) ? s_CachedStunAnimP1 : s_CachedStunAnimP2;
        bool& cachedFound = (playerIdx == 1) ? s_CachedStunFoundP1 : s_CachedStunFoundP2;

        if (pChar == lastChar && cachedFound) {
            outEntry = cachedEntry;
            return true;
        }

        // Rescan if empty or player changed
        if (s_P1AnimSequences.empty() && s_P2AnimSequences.empty()) {
            RefreshAll(false);
        }

        const auto& list = (playerIdx == 1) ? s_P1AnimSequences : s_P2AnimSequences;
        if (list.empty()) return false;

        static const char* s_StunKeywords[] = {
            "dizzy_loop",
            "dizzy",
            "stumble_loop",
            "stumble",
            "stagger",
            "react_dizzy",
            "react_heavy"
        };

        for (const char* kw : s_StunKeywords) {
            for (const auto& entry : list) {
                if (entry.numFrames <= 0) continue;
                std::string lowerName = entry.name;
                std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
                if (lowerName.find(kw) != std::string::npos) {
                    cachedEntry = entry;
                    lastChar = pChar;
                    cachedFound = true;
                    outEntry = entry;
                    return true;
                }
            }
        }

        return false;
    }

    bool AnimViewer::TriggerScriptMove(int playerIdx, const ScriptMoveEntry& entry) {
        void* pChar = (playerIdx == 1) ? GetP1Char() : GetP2Char();
        if (!pChar) {
            LogAnim("[-] Error: Player %d character is not currently active.", playerIdx);
            return false;
        }

        // Set active script function descriptor pointer in character state
        SafeSetScriptFunc(pChar, entry.pFunction);

        bool swapped = SafeGetPlayerSwapped();
        WORD fwd  = (playerIdx == 1) ? (swapped ? 'A' : 'D') : (swapped ? VK_RIGHT : VK_LEFT);
        WORD back = (playerIdx == 1) ? (swapped ? 'D' : 'A') : (swapped ? VK_LEFT : VK_RIGHT);
        WORD up   = (playerIdx == 1) ? 'W' : VK_UP;
        WORD down = (playerIdx == 1) ? 'S' : VK_DOWN;
        WORD fp   = (playerIdx == 1) ? 'T' : VK_NUMPAD7;
        WORD bp   = (playerIdx == 1) ? 'U' : VK_NUMPAD9;
        WORD fk   = (playerIdx == 1) ? 'G' : VK_NUMPAD4;
        WORD bk   = (playerIdx == 1) ? 'J' : VK_NUMPAD6;
        WORD blk  = (playerIdx == 1) ? VK_SPACE : VK_NUMPAD0;
        WORD thr  = (playerIdx == 1) ? VK_OEM_1 : VK_ADD;
        WORD tag  = (playerIdx == 1) ? 'E' : VK_DECIMAL;
        WORD stc  = (playerIdx == 1) ? 'Q' : VK_NUMPAD1;

        std::string lowerName = entry.name;
        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);

        char charKey[64] = {0};
        SafeGetCharKeyword(pChar, charKey, sizeof(charKey));

        std::vector<QueuedInputStep> steps;
        int fallbackActId = 0;

        // 1. Jump Attacks
        if (lowerName.find("jump") != std::string::npos) {
            WORD jumpDir = up;
            WORD jumpDir2 = 0;
            if (lowerName.find("backward") != std::string::npos || lowerName.find("back") != std::string::npos) {
                jumpDir2 = back;
            } else if (lowerName.find("forward") != std::string::npos || lowerName.find("forw") != std::string::npos) {
                jumpDir2 = fwd;
            }

            WORD jumpBtn = 0;
            if (lowerName.find("attack4") != std::string::npos || lowerName.find("kick2") != std::string::npos) {
                jumpBtn = bk;
            } else if (lowerName.find("attack3") != std::string::npos || lowerName.find("kick1") != std::string::npos || lowerName.find("kick") != std::string::npos) {
                jumpBtn = fk;
            } else if (lowerName.find("attack2") != std::string::npos || lowerName.find("punch2") != std::string::npos) {
                jumpBtn = bp;
            } else if (lowerName.find("attack1") != std::string::npos || lowerName.find("punch1") != std::string::npos || lowerName.find("punch") != std::string::npos) {
                jumpBtn = fp;
            }

            if (jumpBtn != 0) {
                steps.push_back({ jumpDir, jumpDir2, 0, 6, 13 });
                steps.push_back({ 0, jumpBtn, 0, 3, 2 });
            } else {
                steps.push_back({ jumpDir, jumpDir2, 0, 5, 2 });
            }
            fallbackActId = 0x06;
        }
        // 2. Throws and Breakers
        else if (lowerName.find("throw") != std::string::npos || lowerName.find("grab") != std::string::npos) {
            if (lowerName.find("towards") != std::string::npos || lowerName.find("forward") != std::string::npos) {
                steps.push_back({ fwd, thr, 0, 5, 2 });
            } else {
                steps.push_back({ 0, thr, 0, 5, 2 });
            }
            fallbackActId = 0x1B;
        }
        else if (lowerName.find("breaker") != std::string::npos) {
            steps.push_back({ fwd, blk, 0, 6, 2 });
            fallbackActId = 0x07;
        }
        // 3. Supermove / X-Ray
        else if (lowerName.find("supermove") != std::string::npos || lowerName.find("xray") != std::string::npos || lowerName.find("x_ray") != std::string::npos) {
            steps.push_back({ 0, stc, blk, 8, 2 });
            fallbackActId = 0x18;
        }
        // 4. Tag moves
        else if (lowerName.find("tagassist") != std::string::npos) {
            steps.push_back({ 0, tag, 0, 5, 2 });
        }
        else if (lowerName.find("tagswap") != std::string::npos || lowerName.find("changetag") != std::string::npos) {
            steps.push_back({ fwd, tag, 0, 5, 2 });
        }
        // 5. Dial-in Strings and Directional Normals: attack[1-4][towards|away|down](_[1-4])*
        else if (lowerName.find("attack") != std::string::npos) {
            size_t atkPos = lowerName.find("attack");
            if (atkPos + 6 < lowerName.size() && lowerName[atkPos + 6] >= '1' && lowerName[atkPos + 6] <= '4') {
                int firstBtnNum = lowerName[atkPos + 6] - '0';
                WORD firstBtn = (firstBtnNum == 1) ? fp : (firstBtnNum == 2) ? bp : (firstBtnNum == 3) ? fk : bk;

                WORD firstDir = 0;
                std::string sub = lowerName.substr(atkPos, 25);
                if (sub.find("towards") != std::string::npos || sub.find("toward") != std::string::npos) {
                    firstDir = fwd;
                } else if (sub.find("away") != std::string::npos || sub.find("back") != std::string::npos) {
                    firstDir = back;
                } else if (sub.find("down") != std::string::npos || sub.find("duck") != std::string::npos) {
                    firstDir = down;
                }

                // Check for subsequent combo buttons in string, e.g. attack1_1_1, attack2towards_1_4
                std::vector<WORD> subsequentBtns;
                for (size_t k = atkPos + 7; k < lowerName.size(); k++) {
                    if (lowerName[k] == '_' && k + 1 < lowerName.size() && lowerName[k+1] >= '1' && lowerName[k+1] <= '4') {
                        int bNum = lowerName[k+1] - '0';
                        WORD bKey = (bNum == 1) ? fp : (bNum == 2) ? bp : (bNum == 3) ? fk : bk;
                        subsequentBtns.push_back(bKey);
                    }
                }

                if (subsequentBtns.empty()) {
                    // Single directional or standing normal
                    steps.push_back({ firstDir, firstBtn, 0, 4, 2 });
                    if (firstDir == down && firstBtn == bp) fallbackActId = 0x20; // Uppercut
                    else if (firstDir == down && firstBtn == bk) fallbackActId = 0x21; // Sweep
                    else fallbackActId = firstBtnNum;
                } else {
                    // Dial-in combo sequence
                    steps.push_back({ firstDir, firstBtn, 0, 3, 3 });
                    for (WORD bKey : subsequentBtns) {
                        steps.push_back({ 0, bKey, 0, 3, 3 });
                    }
                    fallbackActId = firstBtnNum;
                }
            }
        }

        // 6. If no steps yet, test character specials by name keywords
        if (steps.empty()) {
            if (lowerName.find("uppercut") != std::string::npos) {
                steps.push_back({ down, bp, 0, 4, 2 });
                fallbackActId = 0x20;
            } else if (lowerName.find("sweep") != std::string::npos) {
                steps.push_back({ down, bk, 0, 4, 2 });
                fallbackActId = 0x21;
            } else if (lowerName.find("crouch") != std::string::npos || lowerName.find("duck") != std::string::npos) {
                steps.push_back({ down, 0, 0, 16, 2 });
                fallbackActId = 0x05;
            }
            // Character-specific specials:
            // Scorpion
            else if (lowerName.find("spear") != std::string::npos) {
                steps.push_back({ back, 0, 0, 2, 2 });
                steps.push_back({ back, fp, 0, 3, 2 });
                fallbackActId = 0x10;
            } else if (lowerName.find("teleport") != std::string::npos) {
                steps.push_back({ down, 0, 0, 2, 2 });
                steps.push_back({ back, fk, 0, 3, 2 });
                fallbackActId = 0x11;
            } else if (lowerName.find("takedown") != std::string::npos || lowerName.find("legtakedown") != std::string::npos) {
                steps.push_back({ down, 0, 0, 2, 2 });
                steps.push_back({ back, bk, 0, 3, 2 });
                fallbackActId = 0x13;
            } else if (lowerName.find("hellfire") != std::string::npos) {
                steps.push_back({ down, 0, 0, 2, 2 });
                steps.push_back({ back, bp, 0, 3, 2 });
                fallbackActId = 0x12;
            }
            // Sub-Zero
            else if (lowerName.find("freeze") != std::string::npos || lowerName.find("iceball") != std::string::npos) {
                steps.push_back({ down, 0, 0, 2, 2 });
                steps.push_back({ fwd, fp, 0, 3, 2 });
                fallbackActId = 0x10;
            } else if (lowerName.find("slide") != std::string::npos) {
                steps.push_back({ back, 0, 0, 2, 2 });
                steps.push_back({ fwd, bk, 0, 3, 2 });
                fallbackActId = 0x11;
            } else if (lowerName.find("clone") != std::string::npos || lowerName.find("iceclone") != std::string::npos) {
                steps.push_back({ down, 0, 0, 2, 2 });
                steps.push_back({ back, fp, 0, 3, 2 });
                fallbackActId = 0x12;
            } else if (lowerName.find("puddle") != std::string::npos || lowerName.find("icepuddle") != std::string::npos) {
                steps.push_back({ down, 0, 0, 2, 2 });
                steps.push_back({ back, fk, 0, 3, 2 });
                fallbackActId = 0x13;
            }
            // Johnny Cage
            else if (lowerName.find("shadowkick") != std::string::npos || lowerName.find("shadow_kick") != std::string::npos) {
                steps.push_back({ back, 0, 0, 2, 2 });
                steps.push_back({ fwd, bk, 0, 3, 2 });
                fallbackActId = 0x10;
            } else if (lowerName.find("forceball") != std::string::npos) {
                steps.push_back({ down, 0, 0, 2, 2 });
                steps.push_back({ fwd, bp, 0, 3, 2 });
                fallbackActId = 0x11;
            } else if (lowerName.find("nutpunch") != std::string::npos) {
                steps.push_back({ back, 0, 0, 2, 2 });
                steps.push_back({ down, fp, 0, 3, 2 });
                fallbackActId = 0x12;
            }
            // Liu Kang
            else if (lowerName.find("fireball") != std::string::npos) {
                steps.push_back({ back, 0, 0, 2, 2 });
                steps.push_back({ fwd, fp, 0, 3, 2 });
                fallbackActId = 0x10;
            } else if (lowerName.find("dragon") != std::string::npos || lowerName.find("flyingkick") != std::string::npos) {
                steps.push_back({ fwd, 0, 0, 2, 2 });
                steps.push_back({ fwd, bk, 0, 3, 2 });
                fallbackActId = 0x11;
            } else if (lowerName.find("bicycle") != std::string::npos) {
                steps.push_back({ back, 0, 0, 2, 2 });
                steps.push_back({ back, 0, 0, 2, 2 });
                steps.push_back({ fwd, bk, 0, 3, 2 });
                fallbackActId = 0x12;
            }
            // Raiden
            else if (lowerName.find("torpedo") != std::string::npos || lowerName.find("superman") != std::string::npos) {
                steps.push_back({ back, 0, 0, 2, 2 });
                steps.push_back({ fwd, fk, 0, 3, 2 });
                fallbackActId = 0x10;
            } else if (lowerName.find("lightning") != std::string::npos) {
                steps.push_back({ down, 0, 0, 2, 2 });
                steps.push_back({ back, fp, 0, 3, 2 });
                fallbackActId = 0x11;
            }
            // Ermac
            else if (lowerName.find("slam") != std::string::npos || lowerName.find("telehang") != std::string::npos) {
                steps.push_back({ down, 0, 0, 2, 2 });
                steps.push_back({ back, fp, 0, 3, 2 });
                fallbackActId = 0x10;
            } else if (lowerName.find("forcepush") != std::string::npos || lowerName.find("push") != std::string::npos) {
                steps.push_back({ down, 0, 0, 2, 2 });
                steps.push_back({ back, bp, 0, 3, 2 });
                fallbackActId = 0x11;
            }
            // Reptile
            else if (lowerName.find("spit") != std::string::npos || lowerName.find("acid") != std::string::npos) {
                steps.push_back({ down, 0, 0, 2, 2 });
                steps.push_back({ fwd, fp, 0, 3, 2 });
                fallbackActId = 0x10;
            }
            // Smoke
            else if (lowerName.find("smokebomb") != std::string::npos || lowerName.find("bomb") != std::string::npos) {
                steps.push_back({ down, 0, 0, 2, 2 });
                steps.push_back({ back, bp, 0, 3, 2 });
                fallbackActId = 0x10;
            }
            // Kung Lao
            else if (lowerName.find("hat") != std::string::npos) {
                steps.push_back({ back, 0, 0, 2, 2 });
                steps.push_back({ fwd, bp, 0, 3, 2 });
                fallbackActId = 0x10;
            } else if (lowerName.find("spin") != std::string::npos) {
                steps.push_back({ down, 0, 0, 2, 2 });
                steps.push_back({ fwd, fp, 0, 3, 2 });
                fallbackActId = 0x11;
            }
            // Kitana
            else if (lowerName.find("fan") != std::string::npos) {
                steps.push_back({ down, 0, 0, 2, 2 });
                steps.push_back({ fwd, fp, 0, 3, 2 });
                fallbackActId = 0x10;
            }
            // Mileena
            else if (lowerName.find("sai") != std::string::npos) {
                steps.push_back({ back, 0, 0, 2, 2 });
                steps.push_back({ fwd, fp, 0, 3, 2 });
                fallbackActId = 0x10;
            } else if (lowerName.find("roll") != std::string::npos) {
                steps.push_back({ back, 0, 0, 2, 2 });
                steps.push_back({ down, bk, 0, 3, 2 });
                fallbackActId = 0x11;
            }
            // Jax
            else if (lowerName.find("dashpunch") != std::string::npos || lowerName.find("dash_punch") != std::string::npos) {
                steps.push_back({ fwd, 0, 0, 2, 2 });
                steps.push_back({ fwd, bp, 0, 3, 2 });
                fallbackActId = 0x10;
            } else if (lowerName.find("groundpound") != std::string::npos || lowerName.find("quake") != std::string::npos) {
                steps.push_back({ down, 0, 0, 2, 2 });
                steps.push_back({ back, bk, 0, 3, 2 });
                fallbackActId = 0x11;
            }
            // Sonya
            else if (lowerName.find("energyring") != std::string::npos || lowerName.find("rings") != std::string::npos) {
                steps.push_back({ down, 0, 0, 2, 2 });
                steps.push_back({ fwd, bp, 0, 3, 2 });
                fallbackActId = 0x10;
            } else if (lowerName.find("leggrab") != std::string::npos || lowerName.find("leg_grab") != std::string::npos) {
                steps.push_back({ down, 0, 0, 2, 2 });
                steps.push_back({ back, bk, 0, 3, 2 });
                fallbackActId = 0x11;
            }
            // Noob Saibot
            else if (lowerName.find("shadow") != std::string::npos) {
                steps.push_back({ down, 0, 0, 2, 2 });
                steps.push_back({ fwd, bp, 0, 3, 2 });
                fallbackActId = 0x10;
            } else if (lowerName.find("blackhole") != std::string::npos) {
                steps.push_back({ down, 0, 0, 2, 2 });
                steps.push_back({ back, fp, 0, 3, 2 });
                fallbackActId = 0x11;
            }
            // Cyrax
            else if (lowerName.find("net") != std::string::npos) {
                steps.push_back({ back, 0, 0, 2, 2 });
                steps.push_back({ back, fp, 0, 3, 2 });
                fallbackActId = 0x10;
            }
            // Sektor
            else if (lowerName.find("flame") != std::string::npos) {
                steps.push_back({ back, 0, 0, 2, 2 });
                steps.push_back({ fwd, bp, 0, 3, 2 });
                fallbackActId = 0x10;
            }
            // Generic Special 1..4
            else if (lowerName.find("special1") != std::string::npos || lowerName.find("special_1") != std::string::npos) {
                steps.push_back({ down, 0, 0, 2, 2 });
                steps.push_back({ fwd, fp, 0, 3, 2 });
                fallbackActId = 0x10;
            } else if (lowerName.find("special2") != std::string::npos || lowerName.find("special_2") != std::string::npos) {
                steps.push_back({ down, 0, 0, 2, 2 });
                steps.push_back({ back, fp, 0, 3, 2 });
                fallbackActId = 0x11;
            } else if (lowerName.find("special3") != std::string::npos || lowerName.find("special_3") != std::string::npos) {
                steps.push_back({ back, 0, 0, 2, 2 });
                steps.push_back({ fwd, bp, 0, 3, 2 });
                fallbackActId = 0x12;
            } else if (lowerName.find("special4") != std::string::npos || lowerName.find("special_4") != std::string::npos) {
                steps.push_back({ down, 0, 0, 2, 2 });
                steps.push_back({ back, fk, 0, 3, 2 });
                fallbackActId = 0x13;
            }
            // Face buttons
            else if (lowerName.find("punch2") != std::string::npos || lowerName.find("attack2") != std::string::npos) {
                steps.push_back({ 0, bp, 0, 3, 2 });
                fallbackActId = 0x02;
            } else if (lowerName.find("kick1") != std::string::npos || lowerName.find("attack3") != std::string::npos) {
                steps.push_back({ 0, fk, 0, 3, 2 });
                fallbackActId = 0x03;
            } else if (lowerName.find("kick2") != std::string::npos || lowerName.find("attack4") != std::string::npos) {
                steps.push_back({ 0, bk, 0, 3, 2 });
                fallbackActId = 0x04;
            } else if (lowerName.find("punch1") != std::string::npos || lowerName.find("attack1") != std::string::npos) {
                steps.push_back({ 0, fp, 0, 3, 2 });
                fallbackActId = 0x01;
            } else {
                // Final fallback
                steps.push_back({ 0, fp, 0, 3, 2 });
                fallbackActId = 0x01;
            }
        }

        LogAnim("[TRIGGER] P%d Script Move '%s' (sFunc: %u, pFn: 0x%08X) -> %zu steps (Action 0x%02X)",
                playerIdx, entry.name.c_str(), entry.scriptFunc, (uintptr_t)entry.pFunction, steps.size(), fallbackActId);

        return QueueMacro(playerIdx, steps, entry.name.c_str(), fallbackActId);
    }

    static const AnimSequenceEntry* FindMatchingAnimForScriptMove(int playerIdx, const std::string& moveName) {
        const auto& animList = (playerIdx == 2) ? s_P2AnimSequences : s_P1AnimSequences;
        if (animList.empty()) return nullptr;

        std::string mLower = moveName;
        std::transform(mLower.begin(), mLower.end(), mLower.begin(), ::tolower);

        // Strip prefixes: "rxsounds_", "char_", "p_", "generic_", "rx_"
        if (mLower.find("rxsounds_") == 0) mLower = mLower.substr(9);
        if (mLower.find("char_") == 0) mLower = mLower.substr(5);
        if (mLower.find("generic_") == 0) mLower = mLower.substr(8);
        if (mLower.find("p_") == 0) mLower = mLower.substr(2);
        if (mLower.find("rx_") == 0) mLower = mLower.substr(3);

        // Special Semantic Keyword Matching:
        // 1. Victory / Win / Cinema
        if (mLower.find("victory") != std::string::npos || mLower.find("win") != std::string::npos) {
            for (const auto& anim : animList) {
                std::string aLower = anim.name;
                std::transform(aLower.begin(), aLower.end(), aLower.begin(), ::tolower);
                if (aLower.find("victory") != std::string::npos || aLower.find("roundover") != std::string::npos) return &anim;
            }
        }

        // 2. X-Ray / Supermove
        if (mLower.find("xray") != std::string::npos || mLower.find("x_ray") != std::string::npos || mLower.find("supermove") != std::string::npos) {
            bool isVictim = (mLower.find("victim") != std::string::npos || mLower.find("reaction") != std::string::npos);
            for (const auto& anim : animList) {
                std::string aLower = anim.name;
                std::transform(aLower.begin(), aLower.end(), aLower.begin(), ::tolower);
                if (aLower.find("xray") != std::string::npos) {
                    if (isVictim && (aLower.find("victim") != std::string::npos || aLower.find("reaction") != std::string::npos)) return &anim;
                    if (!isVictim && (aLower.find("attacker") != std::string::npos || aLower.find("out") != std::string::npos)) return &anim;
                }
            }
        }

        // 3. Special moves
        if (mLower.find("flame") != std::string::npos) {
            for (const auto& anim : animList) {
                std::string aLower = anim.name;
                std::transform(aLower.begin(), aLower.end(), aLower.begin(), ::tolower);
                if (aLower.find("flame") != std::string::npos) return &anim;
            }
        }
        if (mLower.find("spear") != std::string::npos) {
            for (const auto& anim : animList) {
                std::string aLower = anim.name;
                std::transform(aLower.begin(), aLower.end(), aLower.begin(), ::tolower);
                if (aLower.find("spear") != std::string::npos) return &anim;
            }
        }
        if (mLower.find("freeze") != std::string::npos) {
            for (const auto& anim : animList) {
                std::string aLower = anim.name;
                std::transform(aLower.begin(), aLower.end(), aLower.begin(), ::tolower);
                if (aLower.find("freeze") != std::string::npos) return &anim;
            }
        }
        if (mLower.find("teleport") != std::string::npos) {
            for (const auto& anim : animList) {
                std::string aLower = anim.name;
                std::transform(aLower.begin(), aLower.end(), aLower.begin(), ::tolower);
                if (aLower.find("teleport") != std::string::npos) return &anim;
            }
        }
        if (mLower.find("slide") != std::string::npos) {
            for (const auto& anim : animList) {
                std::string aLower = anim.name;
                std::transform(aLower.begin(), aLower.end(), aLower.begin(), ::tolower);
                if (aLower.find("slide") != std::string::npos) return &anim;
            }
        }
        if (mLower.find("throw") != std::string::npos || mLower.find("grab") != std::string::npos) {
            for (const auto& anim : animList) {
                std::string aLower = anim.name;
                std::transform(aLower.begin(), aLower.end(), aLower.begin(), ::tolower);
                if (aLower.find("throw") != std::string::npos) return &anim;
            }
        }
        if (mLower.find("taunt") != std::string::npos) {
            for (const auto& anim : animList) {
                std::string aLower = anim.name;
                std::transform(aLower.begin(), aLower.end(), aLower.begin(), ::tolower);
                if (aLower.find("taunt") != std::string::npos) return &anim;
            }
        }

        // Strip character name prefix (e.g. "scorpion_attack1_1_1" -> "attack1_1_1")
        size_t under = mLower.find('_');
        std::string cleaned = mLower;
        if (under != std::string::npos && under <= 12) {
            std::string prefix = mLower.substr(0, under);
            if (prefix == "scorpion" || prefix == "subzero" || prefix == "liukang" || 
                prefix == "johnnycage" || prefix == "cage" || prefix == "raiden" ||
                prefix == "jax" || prefix == "sonya" || prefix == "kano" || prefix == "kunglao" ||
                prefix == "kitana" || prefix == "mileena" || prefix == "jade" || prefix == "reptile" ||
                prefix == "smoke" || prefix == "noob" || prefix == "cyrax" || prefix == "sektor" ||
                prefix == "ermac" || prefix == "shangtsung" || prefix == "baraka" || prefix == "kabal") {
                cleaned = mLower.substr(under + 1);
            }
        }

        // 4. Exact match on cleaned or mLower
        for (const auto& anim : animList) {
            std::string aLower = anim.name;
            std::transform(aLower.begin(), aLower.end(), aLower.begin(), ::tolower);
            if (aLower == cleaned || aLower == mLower) return &anim;
        }

        // 5. Substring match
        for (const auto& anim : animList) {
            std::string aLower = anim.name;
            std::transform(aLower.begin(), aLower.end(), aLower.begin(), ::tolower);
            if (aLower.find(cleaned) != std::string::npos || cleaned.find(aLower) != std::string::npos) {
                return &anim;
            }
        }

        return nullptr;
    }

    static bool MatchesPreset(const std::string& name, int preset) {
        if (preset == 0) return true;
        std::string n = name;
        std::transform(n.begin(), n.end(), n.begin(), ::tolower);
        switch (preset) {
            case 1: // Victory & Intros
                return (n.find("vic") != std::string::npos || n.find("win") != std::string::npos || 
                        n.find("intro") != std::string::npos || n.find("taunt") != std::string::npos || 
                        n.find("roundover") != std::string::npos || n.find("pose") != std::string::npos ||
                        n.find("endofround") != std::string::npos);
            case 2: // Hurt & Reactions
                return (n.find("hit") != std::string::npos || n.find("hurt") != std::string::npos || 
                        n.find("react") != std::string::npos || n.find("damage") != std::string::npos || 
                        n.find("fall") != std::string::npos || n.find("dizzy") != std::string::npos || 
                        n.find("freeze") != std::string::npos || n.find("stumble") != std::string::npos ||
                        n.find("crumble") != std::string::npos || n.find("collapse") != std::string::npos ||
                        n.find("knock") != std::string::npos);
            case 3: // Attacks & Specials
                return (n.find("attack") != std::string::npos || n.find("punch") != std::string::npos || 
                        n.find("kick") != std::string::npos || n.find("sweep") != std::string::npos || 
                        n.find("uppercut") != std::string::npos || n.find("spear") != std::string::npos || 
                        n.find("teleport") != std::string::npos || n.find("flame") != std::string::npos || 
                        n.find("throw") != std::string::npos || n.find("slide") != std::string::npos);
            case 4: // Movement & Defense
                return (n.find("walk") != std::string::npos || n.find("dash") != std::string::npos || 
                        n.find("jump") != std::string::npos || n.find("duck") != std::string::npos || 
                        n.find("block") != std::string::npos || n.find("getup") != std::string::npos || 
                        n.find("turn") != std::string::npos || n.find("step") != std::string::npos || 
                        n.find("stance") != std::string::npos);
            case 5: // Fatalities & X-Ray
                return (n.find("fatality") != std::string::npos || n.find("babality") != std::string::npos || 
                        n.find("xray") != std::string::npos || n.find("x_ray") != std::string::npos || 
                        n.find("supermove") != std::string::npos || n.find("finish") != std::string::npos);
            default:
                return true;
        }
    }

    void AnimViewer::ResetToNeutralIdle(int playerIdx) {
        ReleaseAllSimulationKeys();
        s_ActiveMacro.active = false;

        void* pChar = (playerIdx == 1) ? GetP1Char() : GetP2Char();
        if (!pChar) return;
        void* pInfo = SafeGetFighterInfo(playerIdx, pChar);

        SafeResetAction(pChar, pInfo);

        // Restore default standing idle animation (Stance at pChar + 0x0528)
        __try {
            if (!IsBadReadPtr(pChar, 0x530)) {
                void* pStance = *(void**)((uintptr_t)pChar + 0x0528);
                if (pStance && !IsBadReadPtr(pStance, 0x80)) {
                    int res = 0;
                    SafeInvokePlayAnimDirect(pChar, pStance, -1, 1.0f, 0.15f, 0.15f, 1, &res);
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}

        LogAnim("[+] P%d Reset to Neutral Standing Idle (Keys released, Stance restored).", playerIdx);
    }

    void AnimViewer::RenderImGuiTab() {
        void* p1 = GetP1Char();
        void* p2 = GetP2Char();

        // Auto-refresh if fighter pointers changed
        if (p1 != s_LastP1Char || p2 != s_LastP2Char) {
            RefreshAll(false);
        }

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.4f, 0.9f, 1.0f, 1.0f), "In-Match Animation Viewer & Move Trigger Subsystem");
        ImGui::SameLine();
        ImGui::TextDisabled("| Enumerate all loaded skeletal mesh sequences and trigger in-game");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Top Controls: Player Selector, Category Selector, and Refresh Button
        ImGui::Text("Target Fighter:");
        ImGui::SameLine();
        if (ImGui::RadioButton("Player 1 (P1)", s_SelectedPlayer == 1)) {
            s_SelectedPlayer = 1;
            s_SelectedAnimIndex = -1;
            s_SelectedMoveIndex = -1;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Player 2 (P2)", s_SelectedPlayer == 2)) {
            s_SelectedPlayer = 2;
            s_SelectedAnimIndex = -1;
            s_SelectedMoveIndex = -1;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("All Loaded (Global)", s_SelectedPlayer == 0)) {
            s_SelectedPlayer = 0;
            s_SelectedAnimIndex = -1;
            s_SelectedMoveIndex = -1;
        }

        ImGui::SameLine(420.0f);
        if (ImGui::Button(" Refresh Animations (F5) ")) {
            RefreshAll(true);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Rescan GObjects UAnimSets and MKScript packages for loaded animations.");
        }

        ImGui::SameLine();
        if (ImGui::Button(" Reset to Neutral Idle ")) {
            int stopPlr = (s_SelectedPlayer == 0) ? 1 : s_SelectedPlayer;
            ResetToNeutralIdle(stopPlr);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Resets selected fighter's action state to standard neutral idle.");
        }

        ImGui::Spacing();

        // Mode Selector: Raw Skeletal Anim Sequences vs Character Move Scripts
        ImGui::Text("View Mode:");
        ImGui::SameLine();
        if (ImGui::RadioButton("Loaded Anim Sequences (Raw Mesh)", s_ActiveCategory == 0)) {
            s_ActiveCategory = 0;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Character Move Scripts (Combat Actions)", s_ActiveCategory == 1)) {
            s_ActiveCategory = 1;
        }

        // Quick Category Preset Buttons
        ImGui::Text("Animation Presets:");
        ImGui::SameLine();

        auto PresetBtn = [](const char* label, int preset, ImVec4 activeCol) {
            bool isActive = (s_ActivePreset == preset);
            if (isActive) ImGui::PushStyleColor(ImGuiCol_Button, activeCol);
            if (ImGui::Button(label)) {
                s_ActivePreset = preset;
                s_ActiveCategory = 0; // Automatically switch to Loaded Anim Sequences
                s_SelectedAnimIndex = -1;
            }
            if (isActive) ImGui::PopStyleColor();
        };

        PresetBtn(s_ActivePreset == 0 ? "[ ALL (470+) ]" : " All (470+) ", 0, ImVec4(0.2f, 0.45f, 0.7f, 1.0f));
        ImGui::SameLine();
        PresetBtn(s_ActivePreset == 1 ? "[ VICTORY & INTROS ]" : " Victory & Intros ", 1, ImVec4(0.85f, 0.65f, 0.15f, 1.0f));
        ImGui::SameLine();
        PresetBtn(s_ActivePreset == 2 ? "[ HURT & REACTIONS ]" : " Hurt & Reactions ", 2, ImVec4(0.8f, 0.25f, 0.25f, 1.0f));
        ImGui::SameLine();
        PresetBtn(s_ActivePreset == 3 ? "[ ATTACKS & SPECIALS ]" : " Attacks & Specials ", 3, ImVec4(0.2f, 0.65f, 0.35f, 1.0f));
        ImGui::SameLine();
        PresetBtn(s_ActivePreset == 4 ? "[ MOVEMENT & DEFENSE ]" : " Movement & Defense ", 4, ImVec4(0.25f, 0.55f, 0.75f, 1.0f));
        ImGui::SameLine();
        PresetBtn(s_ActivePreset == 5 ? "[ FATALITIES & X-RAY ]" : " Fatalities & X-Ray ", 5, ImVec4(0.75f, 0.2f, 0.75f, 1.0f));

        ImGui::Spacing();

        // Search / Filter Input
        ImGui::SetNextItemWidth(350.0f);
        ImGui::InputTextWithHint("##AnimFilter", "Filter animations by name or package (e.g. walk, attack, idle, hit, xray)...", s_FilterBuffer, sizeof(s_FilterBuffer));
        ImGui::SameLine();
        if (ImGui::Button("Clear Filter")) {
            s_FilterBuffer[0] = '\0';
            s_ActivePreset = 0;
        }

        ImGui::Spacing();

        // Playback Control Bar
        ImGui::Text("Playback Controls:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(140.0f);
        ImGui::SliderFloat("Speed", &s_PlaybackSpeed, 0.1f, 3.0f, "%.2fx");

        ImGui::SameLine();
        ImGui::Checkbox("Loop", &s_LoopAnimation);

        ImGui::SameLine();
        ImGui::SetNextItemWidth(80.0f);
        ImGui::SliderFloat("Blend In", &s_BlendIn, 0.0f, 0.5f, "%.2fs");

        ImGui::SameLine();
        ImGui::SetNextItemWidth(80.0f);
        ImGui::SliderFloat("Blend Out", &s_BlendOut, 0.0f, 0.5f, "%.2fs");

        ImGui::Spacing();

        // Status Feedback Bar
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08f, 0.08f, 0.12f, 1.0f));
        ImGui::BeginChild("AnimStatusChild", ImVec2(0, 32), true);
        ImGui::TextColored(s_StatusColor, "%s", s_StatusMessage);
        ImGui::EndChild();
        ImGui::PopStyleColor();

        ImGui::Spacing();

        // Quick Move Triggers (Fight Engine Action Dispatcher 0x00889B90)
        ImGui::Text("Quick Move Triggers:");
        ImGui::SameLine();
        ImGui::TextDisabled("(Triggers combat engine action directly with real-time diagnostic logging)");

        int targetPlr = (s_SelectedPlayer == 0) ? 1 : s_SelectedPlayer;
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.45f, 0.7f, 0.9f));
        if (ImGui::Button(" Attack 1 (0x01) ")) TriggerAction(targetPlr, 0x01, "Attack 1 (Front Punch)");
        ImGui::SameLine();
        if (ImGui::Button(" Attack 2 (0x02) ")) TriggerAction(targetPlr, 0x02, "Attack 2 (Back Punch)");
        ImGui::SameLine();
        if (ImGui::Button(" Attack 3 (0x03) ")) TriggerAction(targetPlr, 0x03, "Attack 3 (Front Kick)");
        ImGui::SameLine();
        if (ImGui::Button(" Attack 4 (0x04) ")) TriggerAction(targetPlr, 0x04, "Attack 4 (Back Kick)");
        ImGui::SameLine();
        if (ImGui::Button(" Uppercut (D+2) ")) TriggerAction(targetPlr, 0x20, "Uppercut (D+2)");
        ImGui::SameLine();
        if (ImGui::Button(" Sweep (D+4) ")) TriggerAction(targetPlr, 0x21, "Sweep (D+4)");
        ImGui::SameLine();
        if (ImGui::Button(" Crouch (0x05) ")) TriggerAction(targetPlr, 0x05, "Crouch / Duck");
        ImGui::SameLine();
        if (ImGui::Button(" Jump (0x06) ")) TriggerAction(targetPlr, 0x06, "Jump");
        ImGui::SameLine();
        if (ImGui::Button(" Dash Fwd (0x0C) ")) TriggerAction(targetPlr, 0x0C, "Dash Forward");
        ImGui::SameLine();
        if (ImGui::Button(" Dash Back (0x0D) ")) TriggerAction(targetPlr, 0x0D, "Dash Backward");
        ImGui::PopStyleColor();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.45f, 0.25f, 0.65f, 0.9f));
        if (ImGui::Button(" Special 1 (0x10) ")) TriggerAction(targetPlr, 0x10, "Special 1");
        ImGui::SameLine();
        if (ImGui::Button(" Special 2 (0x11) ")) TriggerAction(targetPlr, 0x11, "Special 2");
        ImGui::SameLine();
        if (ImGui::Button(" Special 3 (0x12) ")) TriggerAction(targetPlr, 0x12, "Special 3");
        ImGui::SameLine();
        if (ImGui::Button(" Special 4 (0x13) ")) TriggerAction(targetPlr, 0x13, "Special 4");
        ImGui::SameLine();
        if (ImGui::Button(" X-Ray (0x18) ")) TriggerAction(targetPlr, 0x18, "X-Ray Attack");
        ImGui::SameLine();
        if (ImGui::Button(" Taunt (0x1A) ")) TriggerAction(targetPlr, 0x1A, "Taunt / Stance Switch");
        ImGui::SameLine();
        if (ImGui::Button(" Throw (0x1B) ")) TriggerAction(targetPlr, 0x1B, "Throw / Grab");
        ImGui::PopStyleColor();

        ImGui::SameLine();
        ImGui::SetNextItemWidth(70.0f);
        ImGui::InputInt("##CustomActionId", &s_CustomActionId, 1, 5);
        ImGui::SameLine();
        if (ImGui::Button(" Trigger Custom ID ")) {
            TriggerAction(targetPlr, s_CustomActionId, "Custom Action ID");
        }

        ImGui::Spacing();

        // Helper string lowercase
        auto toLower = [](const std::string& str) {
            std::string s = str;
            std::transform(s.begin(), s.end(), s.begin(), ::tolower);
            return s;
        };
        std::string filterLower = toLower(s_FilterBuffer);

        // Main Content Area: Left = Listbox, Right = Detail Pane
        float contentHeight = 260.0f;

        if (s_ActiveCategory == 0) {
            // =============================================================
            // CATEGORY 0: Loaded Anim Sequences
            // =============================================================
            auto& animList = (s_SelectedPlayer == 1) ? s_P1AnimSequences :
                             (s_SelectedPlayer == 2) ? s_P2AnimSequences : s_AllAnimSequences;

            // Build filtered index list
            std::vector<int> filteredIndices;
            for (size_t i = 0; i < animList.size(); i++) {
                if (!MatchesPreset(animList[i].name, s_ActivePreset)) continue;
                if (filterLower.empty() || 
                    toLower(animList[i].name).find(filterLower) != std::string::npos ||
                    toLower(animList[i].packageName).find(filterLower) != std::string::npos) {
                    filteredIndices.push_back((int)i);
                }
            }

            ImGui::Text("Animations Found: %zu / %zu loaded (%s)", 
                        filteredIndices.size(), animList.size(),
                        (s_SelectedPlayer == 1) ? "P1 Fighter" :
                        (s_SelectedPlayer == 2) ? "P2 Fighter" : "All In-Memory");

            // Two-column layout
            ImGui::Columns(2, "AnimViewerColumns", true);
            ImGui::SetColumnWidth(0, 480.0f);

            // Left Column: Scrollable Listbox
            ImGui::BeginChild("AnimListChild", ImVec2(0, contentHeight), true);
            if (filteredIndices.empty()) {
                if (animList.empty()) {
                    ImGui::TextDisabled("No animations loaded. Enter a match or press Refresh.");
                } else {
                    ImGui::TextDisabled("No animations match current filter '%s'.", s_FilterBuffer);
                }
            } else {
                for (int idx : filteredIndices) {
                    const auto& anim = animList[idx];
                    char label[256];
                    if (s_SelectedPlayer == 0) {
                        snprintf(label, sizeof(label), "%-32s | %-16s | %3d F##anim%d",
                                 anim.name.c_str(), anim.packageName.c_str(), anim.numFrames, idx);
                    } else {
                        snprintf(label, sizeof(label), "%-36s | %3d F | %4.2fs##anim%d",
                                 anim.name.c_str(), anim.numFrames, anim.sequenceLength, idx);
                    }

                    bool isSelected = (s_SelectedAnimIndex == idx);
                    if (ImGui::Selectable(label, isSelected)) {
                        s_SelectedAnimIndex = idx;
                    }
                    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                        s_SelectedAnimIndex = idx;
                        int playPlr = (s_SelectedPlayer == 0) ? 1 : s_SelectedPlayer;
                        PlayAnimSequence(playPlr, anim, s_PlaybackSpeed, s_LoopAnimation);
                    }
                }
            }
            ImGui::EndChild();

            ImGui::NextColumn();

            // Right Column: Details & Action Pane
            ImGui::BeginChild("AnimDetailChild", ImVec2(0, contentHeight), true);
            if (s_SelectedAnimIndex >= 0 && s_SelectedAnimIndex < (int)animList.size()) {
                const auto& anim = animList[s_SelectedAnimIndex];
                ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "Animation Details:");
                ImGui::Separator();
                ImGui::Text("Name: %s", anim.name.c_str());
                ImGui::Text("Package: %s", anim.packageName.c_str());
                ImGui::Text("Frames: %d (%4.2f sec @ 60fps)", anim.numFrames, anim.numFrames / 60.0f);
                ImGui::Text("Duration: %4.3f seconds", anim.sequenceLength);
                ImGui::Text("Rate Scale: %4.2fx", anim.rateScale);
                ImGui::Text("FName Index: %d (Number: %d)", anim.nameIndex, anim.nameNumber);
                ImGui::Text("AnimSet Index: #%d", anim.animSetIndex);
                ImGui::Text("Address: 0x%08X", (uintptr_t)anim.pSequence);

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.25f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.8f, 0.35f, 1.0f));
                if (ImGui::Button(" [ PLAY ANIMATION DIRECTLY ] ", ImVec2(-1, 36))) {
                    int playPlr = (s_SelectedPlayer == 0) ? 1 : s_SelectedPlayer;
                    PlayAnimSequence(playPlr, anim, s_PlaybackSpeed, s_LoopAnimation);
                }
                ImGui::PopStyleColor(2);

                ImGui::Spacing();
                if (ImGui::Button("Stop / Neutral Stance", ImVec2(-1, 26))) {
                    int playPlr = (s_SelectedPlayer == 0) ? 1 : s_SelectedPlayer;
                    ResetToNeutralIdle(playPlr);
                }
            } else {
                ImGui::TextDisabled("Select an animation from the list on the left to view details and trigger playback.");
            }
            ImGui::EndChild();

            ImGui::Columns(1);

        } else {
            // =============================================================
            // CATEGORY 1: Character Move Scripts (Combat Actions)
            // =============================================================
            auto& moveList = (s_SelectedPlayer == 2) ? s_P2ScriptMoves : s_P1ScriptMoves;

            std::vector<int> filteredIndices;
            for (size_t i = 0; i < moveList.size(); i++) {
                if (!MatchesPreset(moveList[i].name, s_ActivePreset)) continue;
                if (filterLower.empty() || toLower(moveList[i].name).find(filterLower) != std::string::npos) {
                    filteredIndices.push_back((int)i);
                }
            }

            ImGui::Text("Script Moves Found: %zu / %zu registered (%s)", 
                        filteredIndices.size(), moveList.size(),
                        (s_SelectedPlayer == 2) ? "Player 2" : "Player 1");

            ImGui::Columns(2, "MoveViewerColumns", true);
            ImGui::SetColumnWidth(0, 480.0f);

            // Left Column: Moves List
            ImGui::BeginChild("MoveListChild", ImVec2(0, contentHeight), true);
            if (filteredIndices.empty()) {
                if (moveList.empty()) {
                    ImGui::TextDisabled("No script moves found. Enter a match or press Refresh.");
                } else {
                    ImGui::TextDisabled("No moves match current filter '%s'.", s_FilterBuffer);
                }
            } else {
                for (int idx : filteredIndices) {
                    const auto& move = moveList[idx];
                    char label[256];
                    snprintf(label, sizeof(label), "%-42s | ID: %5u##move%d",
                             move.name.c_str(), move.scriptFunc, idx);

                    bool isSelected = (s_SelectedMoveIndex == idx);
                    if (ImGui::Selectable(label, isSelected)) {
                        s_SelectedMoveIndex = idx;
                    }
                    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                        s_SelectedMoveIndex = idx;
                        int playPlr = (s_SelectedPlayer == 2) ? 2 : 1;
                        const AnimSequenceEntry* pMatch = FindMatchingAnimForScriptMove(playPlr, move.name);
                        if (pMatch) {
                            PlayAnimSequence(playPlr, *pMatch, s_PlaybackSpeed, s_LoopAnimation);
                        } else {
                            TriggerScriptMove(playPlr, move);
                        }
                    }
                }
            }
            ImGui::EndChild();

            ImGui::NextColumn();

            // Right Column: Move Details & Action
            ImGui::BeginChild("MoveDetailChild", ImVec2(0, contentHeight), true);
            if (s_SelectedMoveIndex >= 0 && s_SelectedMoveIndex < (int)moveList.size()) {
                const auto& move = moveList[s_SelectedMoveIndex];
                int playPlr = (s_SelectedPlayer == 2) ? 2 : 1;
                const AnimSequenceEntry* pMatchingAnim = FindMatchingAnimForScriptMove(playPlr, move.name);

                std::string mLower = move.name;
                std::transform(mLower.begin(), mLower.end(), mLower.begin(), ::tolower);
                bool isCombatCombo = (mLower.find("attack") != std::string::npos || mLower.find("punch") != std::string::npos ||
                                      mLower.find("kick") != std::string::npos || mLower.find("special") != std::string::npos ||
                                      mLower.find("spear") != std::string::npos || mLower.find("teleport") != std::string::npos ||
                                      mLower.find("slide") != std::string::npos || mLower.find("sweep") != std::string::npos ||
                                      mLower.find("uppercut") != std::string::npos || mLower.find("throw") != std::string::npos ||
                                      mLower.find("dash") != std::string::npos || mLower.find("jump") != std::string::npos);

                ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "Move Script Details:");
                ImGui::Separator();
                ImGui::Text("Name: %s", move.name.c_str());
                ImGui::Text("Script Function ID: %u", move.scriptFunc);
                ImGui::Text("Function Pointer: 0x%08X", (uintptr_t)move.pFunction);

                ImGui::Spacing();
                if (pMatchingAnim) {
                    ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f), "Direct Animation Clip: '%s' (%d frames)", 
                                       pMatchingAnim->name.c_str(), pMatchingAnim->numFrames);
                } else {
                    ImGui::TextDisabled("No 1:1 direct animation clip found for '%s' (script-driven move).", move.name.c_str());
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                // Button 1: Direct Animation Playback
                if (pMatchingAnim) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.25f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.8f, 0.35f, 1.0f));
                    if (ImGui::Button(" [ PLAY ANIMATION DIRECTLY ] ", ImVec2(-1, 32))) {
                        PlayAnimSequence(playPlr, *pMatchingAnim, s_PlaybackSpeed, s_LoopAnimation);
                    }
                    ImGui::PopStyleColor(2);
                    ImGui::Spacing();
                }

                // Button 2: Execute Combat Move (Only for actual combat attack inputs!)
                if (isCombatCombo) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.8f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.65f, 0.95f, 1.0f));
                    if (ImGui::Button(" [ EXECUTE COMBAT MOVE ] ", ImVec2(-1, 32))) {
                        TriggerScriptMove(playPlr, move);
                    }
                    ImGui::PopStyleColor(2);
                } else if (!pMatchingAnim) {
                    ImGui::TextDisabled("Note: Cinematic / State Script - Use 'Loaded Anim Sequences' or presets above.");
                }

                ImGui::Spacing();
                if (ImGui::Button("Stop / Neutral Stance", ImVec2(-1, 26))) {
                    int playPlr = (s_SelectedPlayer == 2) ? 2 : 1;
                    ResetToNeutralIdle(playPlr);
                }
            } else {
                ImGui::TextDisabled("Select a character script move from the list on the left to trigger.");
            }
            ImGui::EndChild();

            ImGui::Columns(1);
        }

        // Live Execution Diagnostics & Event Log
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextColored(ImVec4(0.4f, 0.9f, 1.0f, 1.0f), "Live Execution Diagnostics & Event Log:");
        ImGui::SameLine();
        if (ImGui::Button("Clear Logs")) {
            ClearLogs();
        }
        ImGui::SameLine();
        ImGui::Checkbox("Auto-scroll", &s_AutoScrollLogs);
        ImGui::SameLine();
        ImGui::TextDisabled("(Mirrored to ImGui Console Logs, OutputDebugString, & MK9_ANIM_VIEWER.LOG)");

        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.04f, 0.04f, 0.07f, 1.0f));
        ImGui::BeginChild("AnimExecLogChild", ImVec2(0, 120.0f), true, ImGuiWindowFlags_HorizontalScrollbar);
        if (s_AnimExecLogs.empty()) {
            ImGui::TextDisabled("No animation or move execution events recorded yet. Click a move or trigger button above to see live diagnostic logs.");
        } else {
            for (const auto& logEntry : s_AnimExecLogs) {
                ImGui::TextColored(logEntry.color, "%s", logEntry.text.c_str());
            }
            if (s_AutoScrollLogs && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
                ImGui::SetScrollHereY(1.0f);
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

} // namespace MK9TradeFix
