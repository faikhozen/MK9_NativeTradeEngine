#pragma once
#include <cstring>
namespace MK9TradeFix {
// Only verified display delays in the supported attract script. In particular,
// saveWarning (line532,15..180F) and save-completion polling are not eligible.
inline int StartupScreenWaitKind(const char* caller,const char* callee,unsigned line,
                                 unsigned minimum,unsigned maximum) {
    if(!caller || !callee || std::strcmp(caller,"AttractModeLegalScreen") ||
       std::strcmp(callee,"WaitForScreenDisplay")) return 0;
    if(line==528 && minimum==60 && maximum==240) return 1;
    if(line==580 && minimum==20 && maximum==180) return 2;
    return 0;
}
}
