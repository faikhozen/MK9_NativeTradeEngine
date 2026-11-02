#pragma once
#include <cstdint>
namespace MK9TradeFix { namespace RecordingDevicePolicy {
struct Actions { uint16_t directions=0, buttons=0; };
// Native normalized action masks, verified against the game's command queries
// and live FP/BP/FK/BK/block/stance/throw presses. Not XInput button masks.
inline Actions Decode(uint32_t mask) {
    Actions a; a.directions=uint16_t(mask&15);
    const uint32_t native[]={0x40,0x10,0x20,0x80,0x8000,0x1000,0x4000,0x2000};
    for (unsigned i=0;i<8;++i) if(mask&native[i]) a.buttons|=uint16_t(1u<<i);
    return a;
}
inline uint32_t Encode(uint16_t directions,uint16_t buttons) {
    uint32_t mask=directions&15;
    const uint32_t native[]={0x40,0x10,0x20,0x80,0x8000,0x1000,0x4000,0x2000};
    for (unsigned i=0;i<8;++i) if(buttons&(1u<<i)) mask|=native[i];
    return mask;
}
inline int DeviceSlot(uintptr_t pointer) {
    constexpr uintptr_t base=0x00E221A8, stride=0x60C;
    if(pointer<base || pointer>=base+18*stride || (pointer-base)%stride) return -1;
    return int((pointer-base)/stride);
}
} }
