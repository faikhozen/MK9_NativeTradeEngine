#include "../src/asi_loader.hpp"
#include <thread>
#include <atomic>
#include <cstdio>
#include <cwchar>
#include <cstdlib>
namespace MK9TradeFix { void LogTrace(const char*, ...) {} }
#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
int main() {
    wchar_t executable[MAX_PATH], original[MAX_PATH], expected[MAX_PATH];
    CHECK(GetModuleFileNameW(nullptr, executable, MAX_PATH));
    wchar_t* slash = wcsrchr(executable, L'\\'); CHECK(slash); *slash = 0;
    CHECK(GetCurrentDirectoryW(MAX_PATH, original));
    CHECK(SetCurrentDirectoryW(L"tests\\artifacts"));
    CHECK(GetCurrentDirectoryW(MAX_PATH, expected));
    CHECK(!ASILoader::IsInitialized() && !ASILoader::IsInitializing());
    std::atomic<bool> done{false}, changed{false}, sawLoading{false};
    std::thread observer([&]() {
        while (!done.load()) {
            wchar_t current[MAX_PATH];
            if (!GetCurrentDirectoryW(MAX_PATH, current) || wcscmp(current, expected)) changed = true;
            if (ASILoader::IsInitializing()) {
                sawLoading = true;
                ASILoader::Initialize(); // Reentrant/parallel initialization must not block or run twice.
            }
            Sleep(1);
        }
    });
    ASILoader::Initialize();
    done = true; observer.join();
    CHECK(!changed && sawLoading);
    CHECK(ASILoader::IsInitialized() && !ASILoader::IsInitializing());
    CHECK(ASILoader::GetLoadedCount() == 1);
    CHECK(ASILoader::GetFailedCount() == 1);
    ASILoader::Initialize(); CHECK(ASILoader::GetLoadedCount() == 1);
    wchar_t plugin[MAX_PATH]; swprintf_s(plugin, L"%ls\\scripts\\fixture.asi", executable);
    HMODULE module = GetModuleHandleW(plugin); CHECK(module);
    auto result = reinterpret_cast<int(*)()>(GetProcAddress(module, "GetFixtureResult"));
    CHECK(result && result() == 42);
    wchar_t current[MAX_PATH]; CHECK(GetCurrentDirectoryW(MAX_PATH, current));
    CHECK(wcscmp(current, expected) == 0);
    CHECK(SetCurrentDirectoryW(original));
    printf("PASS: ASI dependency loading, stable working directory during plugin initialization, readiness, failure reporting, and one-time initialization.\n");
}
