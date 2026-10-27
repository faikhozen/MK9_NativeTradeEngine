#pragma once
#include <cstdint>
namespace MK9TradeFix {
inline bool IsFrameDataGetupAction(uint32_t action) {
    // Live trace: 0x6212 is a standing stance switch, not a getup/knockdown.
    return action>=0x6000 && action<=0x62FF && action!=0x6212;
}
inline bool IsFrameDataWakeupTransition(uint32_t previous,uint64_t lastKnockdown,uint64_t frame) {
    return IsFrameDataGetupAction(previous) || previous==0x460D ||
        (lastKnockdown>0 && frame>=lastKnockdown && frame-lastKnockdown<=20);
}
}
