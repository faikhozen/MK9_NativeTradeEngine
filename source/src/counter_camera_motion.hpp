#pragma once
#include <cmath>
#include <cstdint>
namespace MK9TradeFix { namespace CounterCamera {
inline float Clamp(float v,float lo,float hi) { return std::isfinite(v) ? (v<lo?lo:(v>hi?hi:v)) : 0.0f; }
inline float Ease(float t) { t=Clamp(t,0,1); return t*t*(3-2*t); }
struct Shot { float zoom,shift,yaw; };
inline Shot Sample(int mode,uint64_t age) {
    const float f=(float)(age>180?180:age);
    if(mode==1) {
        const float attack=Ease(f/5.0f);
        const float settle=Ease((f-5.0f)/13.0f);
        return {attack*(1.12f-0.26f*settle),0,0};
    }
    const float enter=Ease(f/12.0f);
    const float drift=Ease((f-12.0f)/30.0f);
    return {enter*(1.0f-0.12f*drift),enter*(0.82f+0.18f*drift),enter*(0.90f+0.16f*drift)};
}
inline float Zoom(float requested) { return Clamp(requested,-20,10); }
inline float Shift(float requested) { return Clamp(requested,0,80); }
inline float Yaw(float requested) { return Clamp(requested,0,4); }
struct Orbit { float x,y; };
inline Orbit OrbitOffset(float nativeYaw,float angle,float radius) {
    return {radius*(std::cos(nativeYaw)-std::cos(nativeYaw+angle)),
            radius*(std::sin(nativeYaw)-std::sin(nativeYaw+angle))};
}
inline bool Debounce(uint64_t now,uint64_t last,bool samePair) { return samePair && now>=last && now-last<10; }
} }
