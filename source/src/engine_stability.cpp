#include "engine_stability.hpp"
#include "minhook/include/MinHook.h"
#include <intrin.h>

namespace MK9TradeFix {

    // ========================================================================
    // R6025 Pure Virtual Function Call Diagnostic Logger & Trap
    // ========================================================================
    static void LogPureCallDiagnosis(void* pThis, void* pRetAddr) {
        FILE* fLog = nullptr;
        fopen_s(&fLog, "R6025_ANALYSIS.LOG", "a+");
        if (!fLog) {
            fopen_s(&fLog, "V:\\SteamLibrary\\steamapps\\common\\MortalKombat_KompleteEdition\\DiscContentPC\\R6025_ANALYSIS.LOG", "a+");
        }

        auto WriteDiag = [&](const char* fmt, ...) {
            char buf[1024];
            va_list args;
            va_start(args, fmt);
            vsnprintf(buf, sizeof(buf), fmt, args);
            va_end(args);

            printf("%s\n", buf);
            if (fLog) {
                fprintf(fLog, "%s\n", buf);
            }
        };

        WriteDiag("====================================================================");
        WriteDiag(" [!] CRITICAL: R6025 PURE VIRTUAL FUNCTION CALL INTERCEPTED!");
        WriteDiag("====================================================================");
        WriteDiag(" Simulation Frame:    %llu", g_CurrentFrame);
        WriteDiag(" Call Site (EIP):     0x%08X", (uintptr_t)pRetAddr);
        if ((uintptr_t)pRetAddr >= 0x00400000 && (uintptr_t)pRetAddr <= 0x01500000) {
            WriteDiag("                      MKKE.exe + 0x%08X", (uintptr_t)pRetAddr - 0x00400000);
        }

        WriteDiag(" 'this' Pointer (ECX):0x%08X", (uintptr_t)pThis);

        // Identify object
        const char* objIdentity = "Unknown Object";
        if (pThis == nullptr) {
            objIdentity = "NULL / Uninitialized (Static or CRT direct invocation)";
        } else if (pThis == GetP1Char()) {
            objIdentity = "Player 1 Character Actor (0x00E89D98)";
        } else if (pThis == GetP2Char()) {
            objIdentity = "Player 2 Character Actor (0x00E8AA24)";
        } else {
            void* p1 = GetP1Char();
            void* p2 = GetP2Char();
            if (p1 && (uintptr_t)p1 > 0x01000000) {
                if (pThis == *(void**)((uintptr_t)p1 + 0x7638)) objIdentity = "Player 1 Combat Controller (+0x7638)";
                else if (pThis == *(void**)((uintptr_t)p1 + 0x2F1C)) objIdentity = "Player 1 Weapon Prop List (+0x2F1C)";
                else if (pThis == *(void**)((uintptr_t)p1 + 0x78F0)) objIdentity = "Player 1 Attack State Block (+0x78F0)";
            }
            if (p2 && (uintptr_t)p2 > 0x01000000) {
                if (pThis == *(void**)((uintptr_t)p2 + 0x7638)) objIdentity = "Player 2 Combat Controller (+0x7638)";
                else if (pThis == *(void**)((uintptr_t)p2 + 0x2F1C)) objIdentity = "Player 2 Weapon Prop List (+0x2F1C)";
                else if (pThis == *(void**)((uintptr_t)p2 + 0x78F0)) objIdentity = "Player 2 Attack State Block (+0x78F0)";
            }
        }
        WriteDiag(" Object Identity:     %s", objIdentity);

        // VTable Inspection
        if (pThis && (uintptr_t)pThis > 0x00010000 && (uintptr_t)pThis < 0x7FFE0000) {
            __try {
                uintptr_t vtable = *(uintptr_t*)pThis;
                WriteDiag(" VTable Address:      0x%08X", vtable);
                if (vtable >= 0x00400000 && vtable <= 0x01500000) {
                    WriteDiag("                      MKKE.exe + 0x%08X", vtable - 0x00400000);
                }

                // Dump first 10 vtable slots
                uintptr_t* pSlots = (uintptr_t*)vtable;
                for (int i = 0; i < 10; i++) {
                    uintptr_t slotVal = pSlots[i];
                    const char* tag = "";
                    if (slotVal == 0x0098c5c6) tag = " <--- _purecall thunk!";
                    WriteDiag("   Slot[%02d] (+0x%02X): 0x%08X%s", i, i * 4, slotVal, tag);
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                WriteDiag(" VTable Address:      [Inaccessible / Access Violation]");
            }
        }

        // Stack Backtrace (16 frames)
        WriteDiag(" Call Stack (16 levels):");
        void* stackFrames[16] = { 0 };
        USHORT frameCount = RtlCaptureStackBackTrace(1, 16, stackFrames, NULL);
        for (USHORT i = 0; i < frameCount; i++) {
            uintptr_t addr = (uintptr_t)stackFrames[i];
            if (addr >= 0x00400000 && addr <= 0x01500000) {
                WriteDiag("   [#%02d] 0x%08X (MKKE.exe + 0x%08X)", i, addr, addr - 0x00400000);
            } else {
                HMODULE hMod = NULL;
                GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)addr, &hMod);
                char modName[MAX_PATH] = "Unknown";
                if (hMod) GetModuleFileNameA(hMod, modName, sizeof(modName));
                char* pBaseName = strrchr(modName, '\\');
                if (pBaseName) pBaseName++; else pBaseName = modName;
                WriteDiag("   [#%02d] 0x%08X (%s + 0x%08X)", i, addr, pBaseName, addr - (uintptr_t)hMod);
            }
        }

        WriteDiag("====================================================================\n");
        if (fLog) {
            fflush(fLog);
            fclose(fLog);
        }
        fflush(stdout);
    }

    typedef void(__cdecl* tPureCall)();
    static tPureCall oPureCall = nullptr;

    static void __declspec(naked) hkPureCall() {
        __asm {
            pushad                  // Preserve all general registers
            mov edx, [esp + 32]     // Caller return address
            push edx                // arg 2: pRetAddr
            push ecx                // arg 1: pThis (ECX)
            call LogPureCallDiagnosis
            add esp, 8
            popad
            xor eax, eax            // Neutralize return value (return 0/nullptr)
            ret
        }
    }

    static void __cdecl hkPureCallCRT() {
        void* retAddr = _ReturnAddress();
        LogPureCallDiagnosis(nullptr, retAddr);
    }

    static void __declspec(naked) SafeVTableNoOp() {
        __asm {
            xor eax, eax
            ret
        }
    }

    static void __declspec(naked) SafeVTableNoOp4() {
        __asm {
            xor eax, eax
            ret 4
        }
    }

    static void __declspec(naked) SafeVTableNoOp8() {
        __asm {
            xor eax, eax
            ret 8
        }
    }

    // ========================================================================
    // Havok Animation Crash Guards
    // ========================================================================
    static const uintptr_t g_HavokLoopResume  = 0x0071C09E;
    static const uintptr_t g_HavokLoopExit    = 0x0071C2AC;
    static const uintptr_t g_HavokChunkResume = 0x0071C5BE;
    static const uintptr_t g_HavokChunkExit   = 0x0071C5DB;

    // Guard 1: Havok Bone Track Extractor (0x0070E1F0)
    typedef void (__thiscall *tExtractBoneTracks)(void* pThis, void* pTrack, uint32_t mask, uint32_t flags);
    static tExtractBoneTracks oExtractBoneTracks = nullptr;

    static void __fastcall hkExtractBoneTracks(void* pThis, void* edx_unused, void* pTrack, uint32_t mask, uint32_t flags) {
        if (!pThis || !pTrack || (uintptr_t)pThis < 0x00400000 || (uintptr_t)pTrack < 0x00400000) {
            if (pThis && (uintptr_t)pThis >= 0x00400000) {
                *(uint32_t*)pThis = flags;
                *(uint32_t*)((uintptr_t)pThis + 4) = 0;
            }
            return;
        }

        __try {
            if (oExtractBoneTracks) {
                oExtractBoneTracks(pThis, pTrack, mask, flags);
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            if (pThis && (uintptr_t)pThis >= 0x00400000) {
                *(uint32_t*)pThis = flags;
                *(uint32_t*)((uintptr_t)pThis + 4) = 0;
            }
        }
    }

    // Guard 2: Havok Animation Blend Node Evaluator (0x0071EAA0)
    typedef void (__thiscall *tHavokEvaluateTracks)(void* pThis, void* param_1, void* param_2);
    static tHavokEvaluateTracks oHavokEvaluateTracks = nullptr;

    static void __fastcall hkHavokEvaluateTracks(void* pThis, void* edx_unused, void* param_1, void* param_2) {
        if (!pThis || !param_1 || !param_2 || (uintptr_t)pThis < 0x00400000 || (uintptr_t)param_1 < 0x00400000 || (uintptr_t)param_2 < 0x00400000) return;

        __try {
            if (oHavokEvaluateTracks) {
                oHavokEvaluateTracks(pThis, param_1, param_2);
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            if (param_1 && (uintptr_t)param_1 >= 0x00400000) {
                *(uint32_t*)((uintptr_t)param_1 + 0x50) = 0;
            }
        }
    }

    // Guard 3: Havok Animation Bytecode Opcode Range Guard (0x0071C090)
    static __declspec(naked) void hkHavokAnimLoopEntry() {
        __asm {
            mov edx, dword ptr [ebp + 0x08]
            mov ecx, dword ptr [ebx + edx + 0x40]
            
            // Validate opcode range: 1 <= ecx <= 11
            cmp ecx, 1
            jb anim_out_of_range
            cmp ecx, 11
            ja anim_out_of_range

            // Valid packet: read size table and resume native loop
            movzx esi, byte ptr ds:[ecx + 0x00DEDAE0]
            push g_HavokLoopResume
            ret

        anim_out_of_range:
            // Safely break loop and jump to epilogue
            push g_HavokLoopExit
            ret
        }
    }

    // Guard 4: Havok Chunk Evaluator NULL Pointer Guard (0x0071C5B6)
    static __declspec(naked) void hkHavokChunkIterEntry() {
        __asm {
            mov eax, dword ptr [eax + 0x04]
            mov dword ptr [esi + 0x04], eax

            // Validate chunk pointer before dereferencing [eax]
            test eax, eax
            je chunk_null
            cmp eax, 0x00010000
            jb chunk_null

            // Valid chunk: read [eax] and resume loop
            mov edx, dword ptr [eax]
            push g_HavokChunkResume
            ret

        chunk_null:
            // Safely exit chunk evaluator function
            push g_HavokChunkExit
            ret
        }
    }

    // Guard 5: Animation Track Virtual Method Invocation Guard (0x0079F0E0)
    typedef void (__cdecl *tAnimTrackUpdate)(void** ppContext);
    static tAnimTrackUpdate oAnimTrackUpdate = nullptr;

    static void __cdecl hkAnimTrackUpdate(void** ppContext) {
        if (!ppContext || (uintptr_t)ppContext < 0x00400000) return;

        __try {
            if (oAnimTrackUpdate) {
                oAnimTrackUpdate(ppContext);
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    // Guard 6: Resource Cache Key Comparator & Release Guard (0x00A66AEA)
    // Prevents access violation if ecx or [ecx] contains an invalid/corrupted pointer (< 0x00010000)
    static const uintptr_t g_KeyReleaseResume = 0x00A66AFE;
    static __declspec(naked) void hkSafeKeyRelease() {
        __asm {
            mov ecx, dword ptr [esp + 8]
            test ecx, ecx
            je skip_rel
            cmp ecx, 0x00010000
            jb skip_rel

            mov edx, dword ptr [ecx]
            test edx, edx
            je skip_rel
            cmp edx, 0x00010000
            jb skip_rel

            mov edx, dword ptr [edx + 8]
            test edx, edx
            je skip_rel
            cmp edx, 0x00010000
            jb skip_rel

            mov eax, dword ptr [esp + 0x0C]
            push eax
            call edx

        skip_rel:
            push g_KeyReleaseResume
            ret
        }
    }

    // Guard 6b: Resource Cache Key Lookup Guard (0x00A66AB0)
    // Prevents access violation or STATUS_ILLEGAL_INSTRUCTION if node or [node+4] has corrupted vtable
    typedef bool (__thiscall *tResourceCacheLookup)(void* thisPtr, void* pKey);
    static tResourceCacheLookup oResourceCacheLookup = nullptr;

    static bool __fastcall hkResourceCacheLookup(void* thisPtr, void* edx, void* pKey) {
        if (!thisPtr || (uintptr_t)thisPtr < 0x00010000 || IsBadReadPtr(thisPtr, 8)) {
            return false;
        }
        __try {
            uintptr_t mode = *(uintptr_t*)thisPtr;
            if (mode == 0) {
                uintptr_t inner = *(uintptr_t*)((uintptr_t)thisPtr + 4);
                if (!inner || inner < 0x00010000 || IsBadReadPtr((void*)inner, 8)) {
                    return false;
                }
                uintptr_t vtbl = *(uintptr_t*)inner;
                if (!vtbl || vtbl < 0x00400000 || IsBadReadPtr((void*)vtbl, 8)) {
                    return false;
                }
                uintptr_t fn = *(uintptr_t*)(vtbl + 4);
                // Reject invalid/misaligned code pointers (< 0x00400000 or odd address causing illegal instruction)
                if (!fn || fn < 0x00400000 || (fn & 1) != 0) {
                    return false;
                }
            }
            return oResourceCacheLookup(thisPtr, pKey);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    // ========================================================================
    // Guard 7: Character Select & Asset Package Async Listener / Refcount Guards
    // (Prevents NULL pointer access violation when hovering/selecting DLC characters)
    // ========================================================================
    typedef void* (__thiscall *tPackageRegisterCallback)(void* pThis, void* pCallback, void* pArg);
    static tPackageRegisterCallback oPackageRegisterCallback = nullptr;

    static void* __fastcall hkPackageRegisterCallback(void* pThis, void* edxUnused, void* pCallback, void* pArg) {
        if (!pThis || (uintptr_t)pThis < 0x00010000) {
            return (void*)0xFFFFFFFF;
        }
        return oPackageRegisterCallback(pThis, pCallback, pArg);
    }

    typedef void (__thiscall *tPackageDecRef)(void* pThis);
    static tPackageDecRef oPackageDecRef = nullptr;

    static void __fastcall hkPackageDecRef(void* pThis, void* edxUnused) {
        if (!pThis || (uintptr_t)pThis < 0x00010000) {
            return;
        }
        oPackageDecRef(pThis);
    }

    typedef void (__thiscall *tPackageUnregister)(void* pThis, uint32_t handle);
    static tPackageUnregister oPackageUnregister = nullptr;

    static void __fastcall hkPackageUnregister(void* pThis, void* edxUnused, uint32_t handle) {
        if (!pThis || (uintptr_t)pThis < 0x00010000) {
            return;
        }
        oPackageUnregister(pThis, handle);
    }

    typedef int (__thiscall *tPackageQueryStatus)(void* pThis);
    static tPackageQueryStatus oPackageQueryStatus = nullptr;

    static int __fastcall hkPackageQueryStatus(void* pThis, void* edxUnused) {
        if (!pThis || (uintptr_t)pThis < 0x00010000) {
            return 0;
        }
        return oPackageQueryStatus(pThis);
    }

    // ========================================================================
    // Guard 8: Kratos & DLC Character Select Thumbnail & Registry Resolver
    // (Ensures Kratos is recognized as a full DLC character with ID 6001
    // ONLY IF the requisite asset packages are present on disk)
    // ========================================================================
    static bool HasKratosFiles() {
        static int s_KratosPresent = -1;
        if (s_KratosPresent != -1) return (s_KratosPresent == 1);

        const char* candidatePaths[] = {
            "Asset\\Char_Kratos.xxx",
            "Asset\\Char_Kratos_Script.xxx",
            "Asset\\Kratos_ui.xxx",
            "..\\Asset\\Char_Kratos.xxx",
            "DiscContentPC\\Asset\\Char_Kratos.xxx",
            "CookedPC\\Char_Kratos.xxx",
            "Char_Kratos.xxx"
        };
        for (const char* p : candidatePaths) {
            DWORD attr = GetFileAttributesA(p);
            if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
                s_KratosPresent = 1;
                LogTrace("[DLC RESOLVER] Kratos assets found at '%s' -> Kratos is ENABLED and SELECTABLE.", p);
                return true;
            }
        }
        s_KratosPresent = 0;
        LogTrace("[DLC RESOLVER] Kratos asset packages (.xxx) not found on disk -> Kratos slot DISABLED / HIDDEN.");
        return false;
    }

    typedef int (__cdecl *tIsDLC_Char)(const char* charName);
    static tIsDLC_Char oIsDLC_Char = nullptr;

    static int __cdecl hkIsDLC_Char(const char* charName) {
        if (charName && (_stricmp(charName, "kratos") == 0 || _stricmp(charName, "char_kratos") == 0 || strstr(charName, "kratos") != nullptr)) {
            if (!HasKratosFiles()) {
                return 0; // Hide/disable Kratos if files are missing
            }
            return 1;
        }
        if (oIsDLC_Char) {
            return oIsDLC_Char(charName);
        }
        return 0;
    }

    typedef int (__cdecl *tIsDLC)(const char* charName);
    static tIsDLC oIsDLC = nullptr;

    static int __cdecl hkIsDLC(const char* charName) {
        if (charName && (_stricmp(charName, "kratos") == 0 || _stricmp(charName, "char_kratos") == 0 || strstr(charName, "kratos") != nullptr)) {
            if (!HasKratosFiles()) {
                return 0; // Hide/disable Kratos if files are missing
            }
            return 1;
        }
        if (oIsDLC) {
            return oIsDLC(charName);
        }
        return 0;
    }

    typedef int (__cdecl *tIsOnDiscDLC)(const char* charName);
    static tIsOnDiscDLC oIsOnDiscDLC = nullptr;

    static int __cdecl hkIsOnDiscDLC(const char* charName) {
        if (charName && (_stricmp(charName, "kratos") == 0 || _stricmp(charName, "char_kratos") == 0 || strstr(charName, "kratos") != nullptr)) {
            return 0; // Kratos is standalone package DLC, not on-disc atlas
        }
        if (oIsOnDiscDLC) {
            return oIsOnDiscDLC(charName);
        }
        return 0;
    }

    typedef int (__cdecl *tGetDLCID)(const char* charName);
    static tGetDLCID oGetDLCID = nullptr;

    static int __cdecl hkGetDLCID(const char* charName) {
        if (charName && (_stricmp(charName, "kratos") == 0 || _stricmp(charName, "char_kratos") == 0 || strstr(charName, "kratos") != nullptr)) {
            if (!HasKratosFiles()) {
                return -1;
            }
            return 6001; // Standard Kratos DLC ID
        }
        if (oGetDLCID) {
            return oGetDLCID(charName);
        }
        return -1;
    }

    typedef int (__cdecl *tGetDLCUnlocked)(const char* charName);
    static tGetDLCUnlocked oGetDLCUnlocked = nullptr;

    static int __cdecl hkGetDLCUnlocked(const char* charName) {
        if (charName && (_stricmp(charName, "kratos") == 0 || _stricmp(charName, "char_kratos") == 0 || strstr(charName, "kratos") != nullptr)) {
            if (!HasKratosFiles()) {
                return 0; // Locked / unselectable if assets missing
            }
            return 1; // Always unlocked if files present
        }
        if (oGetDLCUnlocked) {
            return oGetDLCUnlocked(charName);
        }
        return 0;
    }



    bool InstallEngineStabilityHooks() {
        // Set unhandled crash filter
        SetUnhandledExceptionFilter(MK9CrashHandler);

        // Hook Animation Track Virtual Method Invocation (0x0079F0E0)
        if (MH_CreateHook((void*)0x0079f0e0, (void*)hkAnimTrackUpdate, (void**)&oAnimTrackUpdate) == MH_OK) {
            MH_EnableHook((void*)0x0079f0e0);
            LogTrace("[+] Animation Track Virtual Method Guard Hooked (0x0079F0E0)");
        }

        // Havok Opcode Guard patch (0x0071C090 - 14 bytes)
        DWORD hProt1 = 0;
        uint8_t* pPatch71C090 = (uint8_t*)0x0071C090;
        if (VirtualProtect(pPatch71C090, 14, PAGE_EXECUTE_READWRITE, &hProt1)) {
            uintptr_t target = (uintptr_t)hkHavokAnimLoopEntry;
            uintptr_t rel = target - ((uintptr_t)pPatch71C090 + 5);
            pPatch71C090[0] = 0xE9;
            *(uint32_t*)(pPatch71C090 + 1) = (uint32_t)rel;
            memset(pPatch71C090 + 5, 0x90, 9);
            VirtualProtect(pPatch71C090, 14, hProt1, &hProt1);
            FlushInstructionCache(GetCurrentProcess(), pPatch71C090, 14);
            LogTrace("[+] Havok Animation Opcode Guard Patched (0x0071C090)");
        }

        // Havok Chunk Evaluator NULL Guard patch (0x0071C5B6 - 8 bytes)
        DWORD hProt2 = 0;
        uint8_t* pPatch71C5B6 = (uint8_t*)0x0071C5B6;
        if (VirtualProtect(pPatch71C5B6, 8, PAGE_EXECUTE_READWRITE, &hProt2)) {
            uintptr_t target = (uintptr_t)hkHavokChunkIterEntry;
            uintptr_t rel = target - ((uintptr_t)pPatch71C5B6 + 5);
            pPatch71C5B6[0] = 0xE9;
            *(uint32_t*)(pPatch71C5B6 + 1) = (uint32_t)rel;
            memset(pPatch71C5B6 + 5, 0x90, 3);
            VirtualProtect(pPatch71C5B6, 8, hProt2, &hProt2);
            FlushInstructionCache(GetCurrentProcess(), pPatch71C5B6, 8);
            LogTrace("[+] Havok Chunk Evaluator NULL Guard Patched (0x0071C5B6)");
        }

        // Hook Havok Bone Track Extractor (0x0070E1F0)
        if (MH_CreateHook((void*)0x0070e1f0, (void*)hkExtractBoneTracks, (void**)&oExtractBoneTracks) == MH_OK) {
            MH_EnableHook((void*)0x0070e1f0);
            LogTrace("[+] Havok Bone Track Extractor Hooked (0x0070E1F0)");
        }

        // Hook Havok Animation Blend Node Evaluator (0x0071EAA0)
        if (MH_CreateHook((void*)0x0071eaa0, (void*)hkHavokEvaluateTracks, (void**)&oHavokEvaluateTracks) == MH_OK) {
            MH_EnableHook((void*)0x0071eaa0);
            LogTrace("[+] Havok Animation Blend Node Evaluator Hooked (0x0071EAA0)");
        }

        // Hook R6025 Pure Virtual Call Engine Thunk (0x0098C5C6)
        if (MH_CreateHook((void*)0x0098c5c6, (void*)hkPureCall, (void**)&oPureCall) == MH_OK) {
            MH_EnableHook((void*)0x0098c5c6);
            LogTrace("[+] R6025 PureCall Thunk Hooked (0x0098C5C6)");
        }

        // Patch IAT Entry for MSVCR90!_purecall (0x00C5539C)
        DWORD oldProt = 0;
        if (VirtualProtect((void*)0x00c5539c, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProt)) {
            *(uintptr_t*)0x00c5539c = (uintptr_t)hkPureCall;
            VirtualProtect((void*)0x00c5539c, sizeof(void*), oldProt, &oldProt);
            LogTrace("[+] R6025 IAT Slot Patched (0x00C5539C)");
        }

        // Register CRT Purecall Handler with MSVCR90.dll
        HMODULE hMsvc = GetModuleHandleA("MSVCR90.dll");
        if (hMsvc) {
            typedef void (__cdecl *_purecall_handler)(void);
            typedef _purecall_handler (__cdecl *tSetPurecallHandler)(_purecall_handler);
            tSetPurecallHandler fnSet = (tSetPurecallHandler)GetProcAddress(hMsvc, "_set_purecall_handler");
            if (fnSet) {
                fnSet((_purecall_handler)hkPureCallCRT);
                LogTrace("[+] MSVCR90!_set_purecall_handler Registered");
            }
        }

        // Neutralize Slots 1 and 2 of Surface Audio VTable 0x00C660F8 (prevents R6025 on audio dispatch)
        DWORD vProt = 0;
        uintptr_t* pAudioVTable = (uintptr_t*)0x00C660F8;
        if (VirtualProtect(pAudioVTable, 0x40, PAGE_EXECUTE_READWRITE, &vProt)) {
            if (pAudioVTable[1] == 0x0098C5C6) pAudioVTable[1] = (uintptr_t)SafeVTableNoOp;
            if (pAudioVTable[2] == 0x0098C5C6) pAudioVTable[2] = (uintptr_t)SafeVTableNoOp;
            VirtualProtect(pAudioVTable, 0x40, vProt, &vProt);
            LogTrace("[+] Surface Audio VTable 0x00C660F8 neutralized (Slots 1 & 2)");
        }

        // Neutralize Slots 1 through 5 of Abstract Interface VTable 0x00D9249C (prevents R6025 on smart pointer refcount)
        uintptr_t* pInterfaceVTable = (uintptr_t*)0x00D9249C;
        if (VirtualProtect(pInterfaceVTable, 0x30, PAGE_EXECUTE_READWRITE, &vProt)) {
            for (int i = 1; i <= 4; i++) {
                if (pInterfaceVTable[i] == 0x0098C5C6) {
                    pInterfaceVTable[i] = (uintptr_t)SafeVTableNoOp4;
                }
            }
            if (pInterfaceVTable[5] == 0x0098C5C6) {
                pInterfaceVTable[5] = (uintptr_t)SafeVTableNoOp8;
            }
            VirtualProtect(pInterfaceVTable, 0x30, vProt, &vProt);
            LogTrace("[+] Abstract Interface VTable 0x00D9249C neutralized (Slots 1-4: ret 4, Slot 5: ret 8)");
        }

        // Guard 6: Resource Cache Key Comparator & Release Patch (0x00A66AEA - 20 bytes)
        DWORD hProt6 = 0;
        uint8_t* pPatchA66AEA = (uint8_t*)0x00A66AEA;
        if (VirtualProtect(pPatchA66AEA, 20, PAGE_EXECUTE_READWRITE, &hProt6)) {
            uintptr_t target = (uintptr_t)hkSafeKeyRelease;
            uintptr_t rel = target - ((uintptr_t)pPatchA66AEA + 5);
            pPatchA66AEA[0] = 0xE9;
            *(uint32_t*)(pPatchA66AEA + 1) = (uint32_t)rel;
            memset(pPatchA66AEA + 5, 0x90, 15);
            VirtualProtect(pPatchA66AEA, 20, hProt6, &hProt6);
            FlushInstructionCache(GetCurrentProcess(), pPatchA66AEA, 20);
            LogTrace("[+] Resource Cache Key Comparator & Release Guard Patched (0x00A66AEA)");
        }

        // Guard 6b: Resource Cache Key Lookup Guard Hook (0x00A66AB0)
        if (MH_CreateHook((void*)0x00A66AB0, (void*)hkResourceCacheLookup, (void**)&oResourceCacheLookup) == MH_OK) {
            MH_EnableHook((void*)0x00A66AB0);
            LogTrace("[+] Resource Cache Key Lookup Guard Hooked (0x00A66AB0)");
        }

        // Guard 7: DLC Character Select & Asset Package Async Listener / Refcount Hooks
        if (MH_CreateHook((void*)0x006a4d70, (void*)hkPackageRegisterCallback, (void**)&oPackageRegisterCallback) == MH_OK) {
            MH_EnableHook((void*)0x006a4d70);
            LogTrace("[+] Package Async Register Callback Guard Hooked (0x006A4D70)");
        }

        if (MH_CreateHook((void*)0x006a82a0, (void*)hkPackageDecRef, (void**)&oPackageDecRef) == MH_OK) {
            MH_EnableHook((void*)0x006a82a0);
            LogTrace("[+] Package DecRef Guard Hooked (0x006A82A0)");
        }

        if (MH_CreateHook((void*)0x006a83a0, (void*)hkPackageUnregister, (void**)&oPackageUnregister) == MH_OK) {
            MH_EnableHook((void*)0x006a83a0);
            LogTrace("[+] Package Unregister Guard Hooked (0x006A83A0)");
        }

        if (MH_CreateHook((void*)0x006a4eb0, (void*)hkPackageQueryStatus, (void**)&oPackageQueryStatus) == MH_OK) {
            MH_EnableHook((void*)0x006a4eb0);
            LogTrace("[+] Package Query Status Guard Hooked (0x006A4EB0)");
        }

        // Guard 8: Kratos & DLC Character Select Thumbnail & Registry Resolver Hooks
        if (MH_CreateHook((void*)0x00888000, (void*)hkIsDLC_Char, (void**)&oIsDLC_Char) == MH_OK) {
            MH_EnableHook((void*)0x00888000);
            LogTrace("[+] DLC Character Check Guard Hooked (0x00888000)");
        }

        if (MH_CreateHook((void*)0x00888630, (void*)hkIsDLC, (void**)&oIsDLC) == MH_OK) {
            MH_EnableHook((void*)0x00888630);
            LogTrace("[+] IsDLC Registry Guard Hooked (0x00888630)");
        }

        if (MH_CreateHook((void*)0x00888720, (void*)hkIsOnDiscDLC, (void**)&oIsOnDiscDLC) == MH_OK) {
            MH_EnableHook((void*)0x00888720);
            LogTrace("[+] IsOnDiscDLC Registry Guard Hooked (0x00888720)");
        }

        if (MH_CreateHook((void*)0x00887c30, (void*)hkGetDLCID, (void**)&oGetDLCID) == MH_OK) {
            MH_EnableHook((void*)0x00887c30);
            LogTrace("[+] GetDLCID Registry Guard Hooked (0x00887C30)");
        }

        if (MH_CreateHook((void*)0x00887c80, (void*)hkGetDLCUnlocked, (void**)&oGetDLCUnlocked) == MH_OK) {
            MH_EnableHook((void*)0x00887c80);
            LogTrace("[+] GetDLCUnlocked Registry Guard Hooked (0x00887C80)");
        }

        return true;
    }
}

