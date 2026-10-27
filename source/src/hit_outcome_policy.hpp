#pragma once
#include <cstdint>
namespace MK9TradeFix { namespace HitOutcomePolicy {
enum class Outcome { Clean, Combo, Counter, Punish };
inline bool Neutral(uint32_t a) {
    return a==0 || a==0xF || a==0x100 || a==0x101 || a==0x102 ||
        (a>=0x200 && a<=0x4FF) || (a>=0x900 && a<=0xA0F) ||
        (a>=0x2000 && a<=0x20FF) || (a>=0x6000 && a<=0x62FF);
}
template<class Tracker>
Outcome Classify(uint32_t action,uint32_t package,int32_t function,const Tracker& t,uint64_t frame) {
    // Current native non-attack state wins over retained script IDs and move history.
    if(Neutral(action)) return Outcome::Clean;
    if(action>=0x4000 && action<=0x4FFF) return Outcome::Combo;
    const bool attackAction=action>=0x5000 && action<=0x5FFF;
    const bool thread=package!=0 && function!=-1;
    const bool current=t.inMove && t.startFrame<=frame && thread &&
        t.scriptPackage==package && t.scriptFunc==function;
    if(!attackAction || !thread) return Outcome::Clean;
    if(current && t.recoveryStartFrame && frame>=t.recoveryStartFrame) return Outcome::Punish;
    // A fresh native attack can be hit before the per-frame tracker observes it.
    return Outcome::Counter;
}
} }
