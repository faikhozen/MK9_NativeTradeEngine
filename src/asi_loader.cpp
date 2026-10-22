#include "asi_loader.hpp"
#include "engine_common.hpp"
#include <vector>
#include <string>
#include <atomic>
#include <algorithm>

namespace ASILoader {

    static std::atomic<bool> s_Initialized{false};
    static std::atomic<size_t> s_LoadedCount{0};

    typedef void (*InitializeASIFn)(void);

    static void LoadPluginsFromDir(const std::wstring& dirPath) {
        std::wstring searchPattern = dirPath + L"\\*.asi";
        WIN32_FIND_DATAW fd;
        HANDLE hFind = FindFirstFileW(searchPattern.c_str(), &fd);

        if (hFind == INVALID_HANDLE_VALUE) {
            return;
        }

        wchar_t prevCurDir[MAX_PATH] = { 0 };
        GetCurrentDirectoryW(MAX_PATH, prevCurDir);

        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                continue;
            }

            std::wstring fileName = fd.cFileName;
            std::wstring fullPath = dirPath + L"\\" + fileName;

            // Switch current directory to the plugin folder so any dependencies are found
            SetCurrentDirectoryW(dirPath.c_str());

            HMODULE hPlugin = LoadLibraryExW(fullPath.c_str(), NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
            if (!hPlugin) {
                hPlugin = LoadLibraryW(fullPath.c_str());
            }

            // Restore original working directory
            SetCurrentDirectoryW(prevCurDir);

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
                MK9TradeFix::LogTrace("[ASI Loader] FAILED to load plugin: %ls (Error: %u / 0x%08X)", fileName.c_str(), err, err);
            }

        } while (FindNextFileW(hFind, &fd));

        FindClose(hFind);
    }

    void Initialize() {
        bool expected = false;
        if (!s_Initialized.compare_exchange_strong(expected, true)) {
            return;
        }

        wchar_t exePath[MAX_PATH] = { 0 };
        GetModuleFileNameW(NULL, exePath, MAX_PATH);

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

        MK9TradeFix::LogTrace("[ASI LOADER] Initialization complete. %zu plugin(s) active.", s_LoadedCount.load());
        MK9TradeFix::LogTrace("====================================================================");
    }

    size_t GetLoadedCount() {
        return s_LoadedCount.load();
    }

} // namespace ASILoader
