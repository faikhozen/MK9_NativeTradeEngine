#pragma once
namespace MK9TradeFix {
// The sequence runner consumes delay=1 and runs frame zero in that same tick.
// delay=0 also runs on the first tick, so zero needs 2 to add one actual tick.
inline int RegularWakeupInternalDelay(int configured) {
    return configured>0 ? configured+1 : 2;
}
}
