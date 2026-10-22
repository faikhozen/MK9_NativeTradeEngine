#include <windows.h>
#include "asi_loader.hpp"

typedef HRESULT(WINAPI* DirectInput8Create_t)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
static DirectInput8Create_t oDirectInput8Create = nullptr;

static void LoadOriginalDInput8() {
    if (oDirectInput8Create) return;

    // Check for chained wrapper DLLs first (standard ASI Loader convention)
    HMODULE hReal = LoadLibraryA("dinput8Hooked.dll");
    if (!hReal) {
        hReal = LoadLibraryA("dinput8_chain.dll");
    }

    if (!hReal) {
        char sysPath[MAX_PATH];
        GetSystemDirectoryA(sysPath, MAX_PATH);
        strcat_s(sysPath, "\\dinput8.dll");
        hReal = LoadLibraryA(sysPath);
    }

    if (hReal) {
        oDirectInput8Create = (DirectInput8Create_t)GetProcAddress(hReal, "DirectInput8Create");
    }
}

extern "C" HRESULT WINAPI DirectInput8Create(HINSTANCE hinst, DWORD dwVersion, REFIID riidltf, LPVOID* ppvOut, LPUNKNOWN punkOuter) {
    ASILoader::Initialize();
    LoadOriginalDInput8();
    if (oDirectInput8Create) {
        return oDirectInput8Create(hinst, dwVersion, riidltf, ppvOut, punkOuter);
    }
    return E_FAIL;
}

