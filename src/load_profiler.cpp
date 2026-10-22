#include "load_profiler.hpp"
#include "trade_fix.hpp"
#include "combat_state.hpp"
#include "imgui_overlay.hpp"
#include "minhook/include/MinHook.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <mutex>
#include <cstdio>
#include <cstdarg>
#include <mmsystem.h>

namespace MK9TradeFix {
    namespace LoadProfiler {

        bool g_FastLoadingEnabled = true;

        static uint64_t s_QpcFreq = 0;
        static uint64_t s_ProcessStartQpc = 0;
        static uint64_t s_D3D9InitQpc = 0;
        static uint64_t s_FirstFrameQpc = 0;
        static uint64_t s_MainMenuQpc = 0;

        static bool s_D3D9Recorded = false;
        static bool s_FirstFrameRecorded = false;
        static bool s_MainMenuRecorded = false;

        // Dedicated Logging to runtime.log and MK9_LOAD_PROFILER.LOG with immediate flush
        static void LogProfiler(const char* fmt, ...) {
            char buf[1024];
            va_list args;
            va_start(args, fmt);
            vsnprintf(buf, sizeof(buf), fmt, args);
            va_end(args);

            // Overlay Console & Debugger Output
            MK9Overlay::AppendLog(buf, 0);
            OutputDebugStringA(buf);
            OutputDebugStringA("\n");

            // Guaranteed Disk Write (flushed immediately)
            static FILE* s_LogRuntime = nullptr;
            static FILE* s_LogProfiler = nullptr;
            static bool s_FilesOpened = false;
            if (!s_FilesOpened) {
                s_FilesOpened = true;
                s_LogRuntime = _fsopen("runtime.log", "a+", _SH_DENYNO);
                s_LogProfiler = _fsopen("MK9_LOAD_PROFILER.LOG", "a+", _SH_DENYNO);
            }

            if (s_LogRuntime) {
                fprintf(s_LogRuntime, "%s\n", buf);
                fflush(s_LogRuntime);
            }
            if (s_LogProfiler) {
                fprintf(s_LogProfiler, "%s\n", buf);
                fflush(s_LogProfiler);
            }
        }

        // Match Loading State
        static bool     s_IsMatchLoading = false;
        static bool     s_IsVsScreenLoading = false;
        static uint64_t s_MatchLoadStartQpc = 0;
        static uint64_t s_MatchLoadEndQpc = 0;
        static uint64_t s_VsScreenStartQpc = 0;
        static uint64_t s_VsScreenEndQpc = 0;

        static double   s_LastMatchLoadDuration = 0.0;
        static double   s_LastVsScreenDuration = 0.0;
        static double   s_LastMatchDiskReadDuration = 0.0;
        static double   s_LastMatchSleepDuration = 0.0;
        static double   s_LastMatchTotalBytesMB = 0.0;
        static size_t   s_TotalPackagesLoaded = 0;

        static uint64_t s_LoadingSleepQpcDelta = 0;
        static uint32_t s_LoadingSleepCallCount = 0;
        static uint32_t s_SleepCalls0ms = 0;
        static uint32_t s_SleepCalls1ms = 0;
        static uint32_t s_SleepCalls5ms = 0;
        static uint32_t s_SleepCalls15ms = 0;
        static uint32_t s_SleepCallsOver15ms = 0;

        static uint64_t s_StartupSleepQpcDelta = 0;
        static uint32_t s_StartupSleepCallCount = 0;

        struct PackageRecord {
            std::wstring filename;
            uint64_t     fileSizeBytes;
            uint64_t     bytesRead;
            double       readDurationSec;
        };

        struct ActiveFileHandle {
            std::wstring filename;
            uint64_t     fileSizeBytes;
            uint64_t     bytesRead;
            double       readDurationSec;
            uint64_t     openedQpc;
        };

        static std::mutex s_FileLock;
        static std::unordered_map<HANDLE, ActiveFileHandle> s_ActiveHandles;
        static std::vector<PackageRecord> s_StartupPackages;
        static std::vector<PackageRecord> s_CurrentMatchPackages;

        static inline double QpcToSeconds(uint64_t delta) {
            if (s_QpcFreq == 0) return 0.0;
            return (double)delta / (double)s_QpcFreq;
        }

        static inline double GetElapsedSinceProcessStart() {
            LARGE_INTEGER now;
            QueryPerformanceCounter(&now);
            return QpcToSeconds((uint64_t)now.QuadPart - s_ProcessStartQpc);
        }

        void InitProcessStartTime() {
            if (s_ProcessStartQpc == 0) {
                // Set system timer interrupt resolution to 1ms (eliminates 15.6ms OS Sleep quantization)
                timeBeginPeriod(1);

                LARGE_INTEGER freq, now;
                QueryPerformanceFrequency(&freq);
                QueryPerformanceCounter(&now);
                s_QpcFreq = (uint64_t)freq.QuadPart;
                s_ProcessStartQpc = (uint64_t)now.QuadPart;
            }
        }

        void RecordD3D9Init() {
            if (s_D3D9Recorded) return;
            s_D3D9Recorded = true;
            LARGE_INTEGER now;
            QueryPerformanceCounter(&now);
            s_D3D9InitQpc = (uint64_t)now.QuadPart;

            double elapsed = QpcToSeconds(s_D3D9InitQpc - s_ProcessStartQpc);
            LogProfiler("[LOAD PROFILER] [+%6.3fs] Direct3D9 Graphics Device Initialized", elapsed);
        }

        void RecordFirstFrame() {
            if (s_FirstFrameRecorded) return;
            s_FirstFrameRecorded = true;
            LARGE_INTEGER now;
            QueryPerformanceCounter(&now);
            s_FirstFrameQpc = (uint64_t)now.QuadPart;

            double elapsed = QpcToSeconds(s_FirstFrameQpc - s_ProcessStartQpc);
            LogProfiler("[LOAD PROFILER] [+%6.3fs] First Visual Frame Rendered (hkEndScene)", elapsed);
        }

        void RecordMainMenu() {
            if (s_MainMenuRecorded) return;
            s_MainMenuRecorded = true;
            LARGE_INTEGER now;
            QueryPerformanceCounter(&now);
            s_MainMenuQpc = (uint64_t)now.QuadPart;

            double elapsed = QpcToSeconds(s_MainMenuQpc - s_ProcessStartQpc);

            double startupBytes = 0.0;
            double startupDiskReadSec = 0.0;
            std::vector<PackageRecord> pkgs;
            {
                std::lock_guard<std::mutex> lock(s_FileLock);
                pkgs = s_StartupPackages;
            }
            for (const auto& pkg : pkgs) {
                startupBytes += (double)pkg.bytesRead;
                startupDiskReadSec += pkg.readDurationSec;
            }
            double startupMB = startupBytes / (1024.0 * 1024.0);
            double startupSleepSec = QpcToSeconds(s_StartupSleepQpcDelta);

            LogProfiler("====================================================================");
            LogProfiler("[LOAD PROFILER] [+%6.3fs] MAIN MENU REACHED | App Startup Complete", elapsed);
            LogProfiler("  - Total Startup Packages:  %zu files (%5.1f MB total)", pkgs.size(), startupMB);
            LogProfiler("  - Startup Disk Read Time:  %6.3f s", startupDiskReadSec);
            LogProfiler("  - Startup Thread Sleeps:   %6.3f s (%u calls)", startupSleepSec, s_StartupSleepCallCount);
            LogProfiler("====================================================================");
        }

        // --------------------------------------------------------------------
        // Native Win32 API Detours for I/O & Thread Sleep Profiling
        // --------------------------------------------------------------------
        typedef HANDLE (WINAPI *tCreateFileW)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
        static tCreateFileW oCreateFileW = nullptr;

        typedef HANDLE (WINAPI *tCreateFileA)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
        static tCreateFileA oCreateFileA = nullptr;

        typedef BOOL (WINAPI *tReadFile)(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
        static tReadFile oReadFile = nullptr;

        typedef void (WINAPI *tSleep)(DWORD);
        static tSleep oSleep = nullptr;

        static bool IsInterestingPackage(LPCWSTR path) {
            if (!path) return false;
            std::wstring wpath(path);
            std::transform(wpath.begin(), wpath.end(), wpath.begin(), ::tolower);
            return (wpath.find(L".xxx") != std::wstring::npos ||
                    wpath.find(L".mko") != std::wstring::npos ||
                    wpath.find(L".mp4") != std::wstring::npos ||
                    wpath.find(L".bik") != std::wstring::npos ||
                    wpath.find(L".ini") != std::wstring::npos ||
                    wpath.find(L".upk") != std::wstring::npos);
        }

        static std::wstring ExtractFilename(LPCWSTR path) {
            if (!path) return L"";
            std::wstring wpath(path);
            size_t slash = wpath.find_last_of(L"\\/");
            if (slash != std::wstring::npos) {
                return wpath.substr(slash + 1);
            }
            return wpath;
        }

        static HANDLE WINAPI hkCreateFileW(
            LPCWSTR lpFileName,
            DWORD dwDesiredAccess,
            DWORD dwShareMode,
            LPSECURITY_ATTRIBUTES lpSecurityAttributes,
            DWORD dwCreationDisposition,
            DWORD dwFlagsAndAttributes,
            HANDLE hTemplateFile
        ) {
            if (!oCreateFileW) return INVALID_HANDLE_VALUE;
            HANDLE hFile = oCreateFileW(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
            if (hFile != INVALID_HANDLE_VALUE && IsInterestingPackage(lpFileName)) {
                LARGE_INTEGER size;
                size.QuadPart = 0;
                GetFileSizeEx(hFile, &size);

                LARGE_INTEGER now;
                QueryPerformanceCounter(&now);

                ActiveFileHandle rec;
                rec.filename = ExtractFilename(lpFileName);
                rec.fileSizeBytes = (uint64_t)size.QuadPart;
                rec.bytesRead = 0;
                rec.readDurationSec = 0.0;
                rec.openedQpc = (uint64_t)now.QuadPart;

                std::wstring lowerName = rec.filename;
                std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);

                // Check for VS loading screen entry
                if (lowerName.find(L"loadscreen") != std::wstring::npos || lowerName.find(L"versuskodes") != std::wstring::npos) {
                    if (!s_IsVsScreenLoading) {
                        s_IsVsScreenLoading = true;
                        s_IsMatchLoading = true;
                        s_VsScreenStartQpc = (uint64_t)now.QuadPart;
                        s_MatchLoadStartQpc = (uint64_t)now.QuadPart;
                        s_LoadingSleepQpcDelta = 0;
                        s_LoadingSleepCallCount = 0;
                        s_SleepCalls0ms = 0;
                        s_SleepCalls1ms = 0;
                        s_SleepCalls5ms = 0;
                        s_SleepCalls15ms = 0;
                        s_SleepCallsOver15ms = 0;
                        s_CurrentMatchPackages.clear();
                        LogProfiler("====================================================================");
                        LogProfiler("[LOAD PROFILER] >>> VS LOADING SCREEN TRIGGERED (%S) <<<", rec.filename.c_str());
                        LogProfiler("====================================================================");
                    }
                }

                std::lock_guard<std::mutex> lock(s_FileLock);
                if (s_ActiveHandles.size() > 64) {
                    s_ActiveHandles.erase(s_ActiveHandles.begin());
                }
                s_ActiveHandles[hFile] = rec;

                double sizeMB = (double)rec.fileSizeBytes / (1024.0 * 1024.0);
                PackageRecord pkg{ rec.filename, rec.fileSizeBytes, rec.fileSizeBytes, 0.0 };
                if (s_IsVsScreenLoading) {
                    s_CurrentMatchPackages.push_back(pkg);
                    s_TotalPackagesLoaded++;
                    LogProfiler("[LOAD I/O] Package: %-26S | Size: %5.1f MB", rec.filename.c_str(), sizeMB);
                } else if (!s_MainMenuRecorded) {
                    s_StartupPackages.push_back(pkg);
                    LogProfiler("[STARTUP I/O] Package: %-26S | Size: %5.1f MB", rec.filename.c_str(), sizeMB);
                }
            }
            return hFile;
        }

        static HANDLE WINAPI hkCreateFileA(
            LPCSTR lpFileName,
            DWORD dwDesiredAccess,
            DWORD dwShareMode,
            LPSECURITY_ATTRIBUTES lpSecurityAttributes,
            DWORD dwCreationDisposition,
            DWORD dwFlagsAndAttributes,
            HANDLE hTemplateFile
        ) {
            if (!oCreateFileA) return INVALID_HANDLE_VALUE;
            HANDLE hFile = oCreateFileA(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
            if (hFile != INVALID_HANDLE_VALUE && lpFileName) {
                int wlen = MultiByteToWideChar(CP_ACP, 0, lpFileName, -1, nullptr, 0);
                if (wlen > 0) {
                    std::wstring wpath(wlen, 0);
                    MultiByteToWideChar(CP_ACP, 0, lpFileName, -1, &wpath[0], wlen);
                    if (IsInterestingPackage(wpath.c_str())) {
                        LARGE_INTEGER size;
                        size.QuadPart = 0;
                        GetFileSizeEx(hFile, &size);

                        LARGE_INTEGER now;
                        QueryPerformanceCounter(&now);

                        ActiveFileHandle rec;
                        rec.filename = ExtractFilename(wpath.c_str());
                        rec.fileSizeBytes = (uint64_t)size.QuadPart;
                        rec.bytesRead = 0;
                        rec.readDurationSec = 0.0;
                        rec.openedQpc = (uint64_t)now.QuadPart;

                        std::wstring lowerName = rec.filename;
                        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);

                        if (lowerName.find(L"loadscreen") != std::wstring::npos || lowerName.find(L"versuskodes") != std::wstring::npos) {
                            if (!s_IsVsScreenLoading) {
                                s_IsVsScreenLoading = true;
                                s_IsMatchLoading = true;
                                s_VsScreenStartQpc = (uint64_t)now.QuadPart;
                                s_MatchLoadStartQpc = (uint64_t)now.QuadPart;
                                s_LoadingSleepQpcDelta = 0;
                                s_LoadingSleepCallCount = 0;
                                s_SleepCalls0ms = 0;
                                s_SleepCalls1ms = 0;
                                s_SleepCalls5ms = 0;
                                s_SleepCalls15ms = 0;
                                s_SleepCallsOver15ms = 0;
                                s_CurrentMatchPackages.clear();
                                LogProfiler("====================================================================");
                                LogProfiler("[LOAD PROFILER] >>> VS LOADING SCREEN TRIGGERED (%S) <<<", rec.filename.c_str());
                                LogProfiler("====================================================================");
                            }
                        }

                        std::lock_guard<std::mutex> lock(s_FileLock);
                        if (s_ActiveHandles.size() > 64) {
                            s_ActiveHandles.erase(s_ActiveHandles.begin());
                        }
                        s_ActiveHandles[hFile] = rec;

                        double sizeMB = (double)rec.fileSizeBytes / (1024.0 * 1024.0);
                        PackageRecord pkg{ rec.filename, rec.fileSizeBytes, rec.fileSizeBytes, 0.0 };
                        if (s_IsVsScreenLoading) {
                            s_CurrentMatchPackages.push_back(pkg);
                            s_TotalPackagesLoaded++;
                            LogProfiler("[LOAD I/O] Package: %-26S | Size: %5.1f MB", rec.filename.c_str(), sizeMB);
                        } else if (!s_MainMenuRecorded) {
                            s_StartupPackages.push_back(pkg);
                            LogProfiler("[STARTUP I/O] Package: %-26S | Size: %5.1f MB", rec.filename.c_str(), sizeMB);
                        }
                    }
                }
            }
            return hFile;
        }

        static BOOL WINAPI hkReadFile(
            HANDLE hFile,
            LPVOID lpBuffer,
            DWORD nNumberOfBytesToRead,
            LPDWORD lpNumberOfBytesRead,
            LPOVERLAPPED lpOverlapped
        ) {
            if (!oReadFile) return FALSE;
            LARGE_INTEGER t0;
            QueryPerformanceCounter(&t0);

            BOOL result = oReadFile(hFile, lpBuffer, nNumberOfBytesToRead, lpNumberOfBytesRead, lpOverlapped);

            LARGE_INTEGER t1;
            QueryPerformanceCounter(&t1);

            if (result && lpNumberOfBytesRead && *lpNumberOfBytesRead > 0) {
                std::lock_guard<std::mutex> lock(s_FileLock);
                auto it = s_ActiveHandles.find(hFile);
                if (it != s_ActiveHandles.end()) {
                    it->second.bytesRead += *lpNumberOfBytesRead;
                    it->second.readDurationSec += QpcToSeconds((uint64_t)t1.QuadPart - (uint64_t)t0.QuadPart);
                }
            }
            return result;
        }

        static void WINAPI hkSleep(DWORD dwMilliseconds) {
            if (!oSleep) {
                Sleep(dwMilliseconds);
                return;
            }

            // Only accelerate sleep during actual VS loading screen transition, NEVER during character select
            if (s_IsVsScreenLoading && dwMilliseconds > 0) {
                LARGE_INTEGER t0, t1;
                QueryPerformanceCounter(&t0);

                s_LoadingSleepCallCount++;
                if (dwMilliseconds <= 1) s_SleepCalls1ms++;
                else if (dwMilliseconds <= 5) s_SleepCalls5ms++;
                else if (dwMilliseconds <= 16) s_SleepCalls15ms++;
                else s_SleepCallsOver15ms++;

                if (g_FastLoadingEnabled) {
                    // With timeBeginPeriod(1), Sleep(1) accurately sleeps 1ms instead of 16ms (16x faster)
                    // without spinning 100% CPU or starving async worker threads.
                    DWORD target = (dwMilliseconds > 1) ? 1 : dwMilliseconds;
                    oSleep(target);
                } else {
                    oSleep(dwMilliseconds);
                }

                QueryPerformanceCounter(&t1);
                uint64_t delta = (uint64_t)t1.QuadPart - (uint64_t)t0.QuadPart;
                s_LoadingSleepQpcDelta += delta;
                return;
            } else if (!s_MainMenuRecorded && dwMilliseconds > 0) {
                LARGE_INTEGER t0, t1;
                QueryPerformanceCounter(&t0);

                if (g_FastLoadingEnabled) {
                    DWORD target = (dwMilliseconds > 1) ? 1 : dwMilliseconds;
                    oSleep(target);
                } else {
                    oSleep(dwMilliseconds);
                }

                QueryPerformanceCounter(&t1);
                uint64_t delta = (uint64_t)t1.QuadPart - (uint64_t)t0.QuadPart;
                s_StartupSleepQpcDelta += delta;
                s_StartupSleepCallCount++;
                return;
            }
            oSleep(dwMilliseconds);
        }

        void InstallHooks() {
            InitProcessStartTime();

            MH_STATUS mhStatus = MH_Initialize();
            LogProfiler("====================================================================");
            LogProfiler(" Mortal Kombat 9 Load Profiler & Fast Loading Optimizer Active");
            LogProfiler(" Process Start: T=0.000s | MinHook status: %d | Timer Resolution: 1ms", (int)mhStatus);
            LogProfiler(" Fast Loading Optimization: %s", g_FastLoadingEnabled ? "ENABLED [1ms Sleep Slice Active]" : "DISABLED");
            LogProfiler("====================================================================");

            MH_STATUS s1 = MH_CreateHookApi(L"kernel32", "CreateFileW", (void*)hkCreateFileW, (void**)&oCreateFileW);
            if (s1 == MH_OK) {
                MH_EnableHook((void*)CreateFileW);
                LogProfiler("[+] Load Profiler CreateFileW Hook Installed");
            } else {
                LogProfiler("[-] Load Profiler CreateFileW Hook Failed (%d)", (int)s1);
            }

            MH_STATUS s1a = MH_CreateHookApi(L"kernel32", "CreateFileA", (void*)hkCreateFileA, (void**)&oCreateFileA);
            if (s1a == MH_OK) {
                MH_EnableHook((void*)CreateFileA);
                LogProfiler("[+] Load Profiler CreateFileA Hook Installed");
            } else {
                LogProfiler("[-] Load Profiler CreateFileA Hook Failed (%d)", (int)s1a);
            }

            MH_STATUS s2 = MH_CreateHookApi(L"kernel32", "ReadFile", (void*)hkReadFile, (void**)&oReadFile);
            if (s2 == MH_OK) {
                MH_EnableHook((void*)ReadFile);
                LogProfiler("[+] Load Profiler ReadFile Hook Installed");
            } else {
                LogProfiler("[-] Load Profiler ReadFile Hook Failed (%d)", (int)s2);
            }

            MH_STATUS s4 = MH_CreateHookApi(L"kernel32", "Sleep", (void*)hkSleep, (void**)&oSleep);
            if (s4 == MH_OK) {
                MH_EnableHook((void*)Sleep);
                LogProfiler("[+] Load Profiler Sleep Hook Installed");
            } else {
                LogProfiler("[-] Load Profiler Sleep Hook Failed (%d)", (int)s4);
            }
        }

        // --------------------------------------------------------------------
        // Frame-by-Frame State Machine Monitor
        // --------------------------------------------------------------------
        void OnFrameTick() {
            void* p1 = GetP1Char();
            void* p2 = GetP2Char();
            uint32_t matchFrame = *(uint32_t*)0x00E89BE0;

            static bool s_HadValidFighters = false;
            static uint32_t s_IdleFrameCount = 0;

            // Detect Main Menu (frame tick count exceeds initial video intro threshold)
            if (!s_MainMenuRecorded && !p1 && !p2) {
                s_IdleFrameCount++;
                if (s_IdleFrameCount > 300) { // ~5 seconds after first frame
                    RecordMainMenu();
                }
            }

            // Detect when match loading begins
            // (Previous fighters become null and packages start loading)
            if (!s_IsMatchLoading && !p1 && !p2 && s_HadValidFighters) {
                s_IsMatchLoading = true;
                s_IsVsScreenLoading = false;
                s_HadValidFighters = false;

                LARGE_INTEGER now;
                QueryPerformanceCounter(&now);
                s_MatchLoadStartQpc = (uint64_t)now.QuadPart;
                s_VsScreenStartQpc = 0;

                s_LoadingSleepQpcDelta = 0;
                s_LoadingSleepCallCount = 0;
                s_SleepCalls0ms = 0;
                s_SleepCalls1ms = 0;
                s_SleepCalls5ms = 0;
                s_SleepCalls15ms = 0;
                s_SleepCallsOver15ms = 0;

                {
                    std::lock_guard<std::mutex> lock(s_FileLock);
                    s_CurrentMatchPackages.clear();
                }

                LogProfiler("====================================================================");
                LogProfiler("[LOAD PROFILER] >>> MATCH TRANSITION STARTED (Rematch / Next Match) <<<");
                LogProfiler("====================================================================");
            }

            // Detect when match load FINISHES and simulation begins
            if ((s_IsMatchLoading || s_IsVsScreenLoading) && p1 && p2 && matchFrame > 0) {
                s_IsMatchLoading = false;
                s_IsVsScreenLoading = false;
                s_HadValidFighters = true;

                LARGE_INTEGER now;
                QueryPerformanceCounter(&now);
                s_MatchLoadEndQpc = (uint64_t)now.QuadPart;
                s_VsScreenEndQpc = (uint64_t)now.QuadPart;

                double totalLoadSec = QpcToSeconds(s_MatchLoadEndQpc - s_MatchLoadStartQpc);
                double vsScreenSec = (s_VsScreenStartQpc > 0) ? QpcToSeconds(s_VsScreenEndQpc - s_VsScreenStartQpc) : totalLoadSec;
                double totalSleepSec = QpcToSeconds(s_LoadingSleepQpcDelta);

                double totalBytes = 0.0;
                double totalDiskReadSec = 0.0;
                std::vector<PackageRecord> pkgs;
                {
                    std::lock_guard<std::mutex> lock(s_FileLock);
                    pkgs = s_CurrentMatchPackages;
                }

                for (const auto& pkg : pkgs) {
                    totalBytes += (double)pkg.bytesRead;
                    totalDiskReadSec += pkg.readDurationSec;
                }

                double totalMB = totalBytes / (1024.0 * 1024.0);
                double avgSpeedMBs = (totalDiskReadSec > 0.001) ? (totalMB / totalDiskReadSec) : 0.0;
                double cpuProcessingSec = totalLoadSec - totalDiskReadSec - totalSleepSec;
                if (cpuProcessingSec < 0.0) cpuProcessingSec = 0.0;

                s_LastMatchLoadDuration = totalLoadSec;
                s_LastVsScreenDuration = vsScreenSec;
                s_LastMatchDiskReadDuration = totalDiskReadSec;
                s_LastMatchSleepDuration = totalSleepSec;
                s_LastMatchTotalBytesMB = totalMB;

                // Sort packages by read time descending
                std::sort(pkgs.begin(), pkgs.end(), [](const PackageRecord& a, const PackageRecord& b) {
                    return a.readDurationSec > b.readDurationSec;
                });

                LogProfiler("====================================================================");
                LogProfiler("[LOAD PROFILER] >>> MATCH LOADING COMPLETE: LANDSCAPE BREAKDOWN <<<");
                LogProfiler("====================================================================");
                LogProfiler("  TOTAL SELECT & LOAD TIME:  %6.3f seconds", totalLoadSec);
                LogProfiler("  >>> ACTUAL VS LOAD SCREEN: %6.3f seconds <<<", vsScreenSec);
                LogProfiler("  - Total Packages Read:     %zu files (%5.1f MB total)", pkgs.size(), totalMB);
                LogProfiler("  - Disk I/O Read Time:      %6.3f s (%4.1f%%) @ %5.1f MB/s", 
                            totalDiskReadSec, (totalLoadSec > 0 ? (totalDiskReadSec / totalLoadSec) * 100.0 : 0.0), avgSpeedMBs);
                LogProfiler("  - Thread Sleep Throttles:  %6.3f s (%4.1f%%) [%u calls]", 
                            totalSleepSec, (totalLoadSec > 0 ? (totalSleepSec / totalLoadSec) * 100.0 : 0.0), s_LoadingSleepCallCount);
                LogProfiler("    [Breakdown: %u x <=1ms, %u x 2-5ms, %u x 6-16ms, %u x >16ms]",
                            s_SleepCalls1ms, s_SleepCalls5ms, s_SleepCalls15ms, s_SleepCallsOver15ms);
                LogProfiler("  - Fast Loading Optimizer:  %s", g_FastLoadingEnabled ? "ACTIVE (Bypassed Sleep Delays)" : "MUTED");
                LogProfiler("  - CPU Decompression/Parse: %6.3f s (%4.1f%%)", 
                            cpuProcessingSec, (totalLoadSec > 0 ? (cpuProcessingSec / totalLoadSec) * 100.0 : 0.0));

                if (!pkgs.empty()) {
                    LogProfiler("--------------------------------------------------------------------");
                    LogProfiler("  TOP LOADED PACKAGES BY READ TIME:");
                    size_t limit = (pkgs.size() > 8) ? 8 : pkgs.size();
                    for (size_t i = 0; i < limit; ++i) {
                        double pMB = (double)pkgs[i].bytesRead / (1024.0 * 1024.0);
                        LogProfiler("   [%zu] %-26S | %5.1f MB | %5.1f ms", 
                                    i + 1, pkgs[i].filename.c_str(), pMB, pkgs[i].readDurationSec * 1000.0);
                    }
                }
                LogProfiler("====================================================================");
            }
        }

        bool IsMatchLoading() {
            return s_IsMatchLoading || s_IsVsScreenLoading;
        }

        double GetLastMatchLoadDuration() {
            return s_LastMatchLoadDuration;
        }

        double GetLastVsScreenDuration() {
            return s_LastVsScreenDuration;
        }

        double GetLastMatchDiskReadDuration() {
            return s_LastMatchDiskReadDuration;
        }

        double GetLastMatchSleepDuration() {
            return s_LastMatchSleepDuration;
        }

        double GetLastMatchTotalBytesMB() {
            return s_LastMatchTotalBytesMB;
        }

        size_t GetTotalPackagesLoaded() {
            return s_TotalPackagesLoaded;
        }

    }
}
