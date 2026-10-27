#pragma once
#include <cstring>
namespace MK9TradeFix {
inline bool CanStartToasty(bool enabled,bool force,bool nativeBusy) {
    return (enabled || force) && !nativeBusy;
}
inline bool IsToastyCleanupWait(const char* function,unsigned line,unsigned frames) {
    return function && !std::strcmp(function,"p_toasty_display") && line==209 && frames==200;
}
}
