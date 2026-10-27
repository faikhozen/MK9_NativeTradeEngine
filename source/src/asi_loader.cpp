#include "asi_loader.hpp"
#include "engine_common.hpp"
#include <vector>
#include <string>
#include <atomic>
#include <algorithm>

namespace ASILoader {

    static std::atomic<bool> s_Started{false};
    static std::atomic<bool> s_Complete{false};
    static std::atomic<size_t> s_FailedCount{0};
    static std::atomic<size_t> s_LoadedCount{0};

    typedef void (*InitializeASIFn)(void);

    static void LoadPluginsFromDir(const std::wstring& dirPath) {
        std::wstring searchPattern = dirPath + L"\\*.asi";
        WIN32_FIND_DATAW fd;
        HANDLE hFind = FindFirstFileW(searchPattern.c_str(), &fd);

        if (hFind == INVALID_HANDLE_VALUE) {
            return;
        }


        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                continue;
            }

            std::wstring fileName = fd.cFileName;
            std::wstring fullPath = dirPath + L"\\" + fileName;

            // Search this absolute plugin directory for its dependencies without
            // mutating the working directory shared by every game thread.
            HMODULE hPlugin = LoadLibraryExW(fullPath.c_str(), nullptr,
                LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
            if (hPlugin) {
                s_LoadedCount++;
                MK9TradeFix::LogTrace("[ASI Loader] Loaded plugin: %ls (Base: 0x%p)", fileName.c_str(), hPlugin);

                InitializeASIFn initFn = (InitializeASIFn)GetProcAddress(hPlugin, "InitializeASI");
                if (initFn) {
                    MK9TradeFix::LogTrace("[ASI Loader] Calling InitializeASI() for: %ls", fileName.c_str());
                    initFn();
                }
            } else {
                DWORD err = GetLastError();
                ++s_FailedCount;
                MK9TradeFix::LogTrace("[ASI Loader] FAILED to load plugin: %ls (Error: %u / 0x%08X)", fileName.c_str(), err, err);
            }

        } while (FindNextFileW(hFind, &fd));

        FindClose(hFind);
    }

    void Initialize() {
        bool expected = false;
        if (!s_Started.compare_exchange_strong(expected, true)) {
            return;
        }

        wchar_t exePath[MAX_PATH] = { 0 };
        const DWORD length = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        if (!length || length >= MAX_PATH) {
            ++s_FailedCount;
            s_Complete.store(true);
            MK9TradeFix::LogTrace("[ASI Loader] Invalid or truncated executable path; plugin scan aborted.");
            return;
        }

        wchar_t* lastSlash = wcsrchr(exePath, L'\\');
        if (lastSlash) {
            *lastSlash = L'\0';
        }

        std::wstring baseDir = exePath;

        MK9TradeFix::LogTrace("====================================================================");
        MK9TradeFix::LogTrace(" [ASI LOADER] Initializing Ultimate ASI Loader subsystem...");
        MK9TradeFix::LogTrace(" [ASI LOADER] Base Directory: %ls", baseDir.c_str());
        MK9TradeFix::LogTrace("====================================================================");

        // 1. Scan Base Directory (where KratosUnlock.asi lives in DiscContentPC)
        LoadPluginsFromDir(baseDir);

        // 2. Scan scripts\\ directory
        std::wstring scriptsDir = baseDir + L"\\scripts";
        if (GetFileAttributesW(scriptsDir.c_str()) != INVALID_FILE_ATTRIBUTES) {
            LoadPluginsFromDir(scriptsDir);
        }

        // 3. Scan plugins\\ directory
        std::wstring pluginsDir = baseDir + L"\\plugins";
        if (GetFileAttributesW(pluginsDir.c_str()) != INVALID_FILE_ATTRIBUTES) {
            LoadPluginsFromDir(pluginsDir);
        }

        s_Complete.store(true);
        MK9TradeFix::LogTrace("[ASI LOADER] Scan complete. %zu plugin(s) active; %zu load failure(s).", s_LoadedCount.load(), s_FailedCount.load());
        MK9TradeFix::LogTrace("====================================================================");
    }

    bool IsInitialized() { return s_Complete.load(); }
    bool IsInitializing() { return s_Started.load() && !s_Complete.load(); }
    size_t GetFailedCount() { return s_FailedCount.load(); }

    size_t GetLoadedCount() {
        return s_LoadedCount.load();
    }

} // namespace ASILoader
