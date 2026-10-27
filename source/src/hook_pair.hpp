#pragma once
#include "minhook/include/MinHook.h"

namespace MK9TradeFix {
    struct HookSpec { void* target; void* detour; void** original; };

    // Callbacks keep failure/rollback behavior testable without patching a process.
    template<class Create, class Enable, class Disable, class Remove, class Report>
    bool InstallHookPair(const HookSpec (&hooks)[2], Create create, Enable enable,
                         Disable disable, Remove remove, Report report) {
        bool created[2] = {false, false};
        auto rollback = [&]() {
            for (int i = 1; i >= 0; --i) {
                if (!created[i]) continue;
                const MH_STATUS disabled = disable(hooks[i].target);
                if (disabled != MH_OK && disabled != MH_ERROR_DISABLED)
                    report("rollback-disable", hooks[i].target, disabled);
                const MH_STATUS removed = remove(hooks[i].target);
                if (removed != MH_OK) report("rollback-remove", hooks[i].target, removed);
            }
        };
        for (int i = 0; i < 2; ++i) {
            const MH_STATUS status = create(hooks[i].target, hooks[i].detour, hooks[i].original);
            if (status != MH_OK) {
                report("create", hooks[i].target, status);
                rollback();
                return false;
            }
            created[i] = true;
        }
        for (int i = 0; i < 2; ++i) {
            const MH_STATUS status = enable(hooks[i].target);
            if (status != MH_OK) {
                report("enable", hooks[i].target, status);
                rollback();
                return false;
            }
        }
        return true;
    }
}
