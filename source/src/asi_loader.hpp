#pragma once
#include <windows.h>
#include <cstddef>

namespace ASILoader {
    void Initialize();
    bool IsInitialized(); // Scan finished; inspect GetFailedCount for load errors.
    bool IsInitializing();
    size_t GetFailedCount();
    size_t GetLoadedCount();
}
