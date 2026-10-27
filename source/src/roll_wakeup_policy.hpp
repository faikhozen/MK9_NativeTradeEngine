#pragma once
#include <cstdint>
#include <cstring>
namespace MK9TradeFix { namespace RollWakeupPolicy {
inline bool IsWakeupMessage(const char* text) {
    return text && std::strcmp(text,"wakeup attack")==0;
}
inline bool IsRollAnimation(const char* name) {
    static const char prefix[]="Generic_RollGetup_";
    return name && std::strncmp(name,prefix,sizeof(prefix)-1)==0;
}
inline bool Neutral(uint32_t a) { return a==0 || a==0xF || a==0x100 || a==0x101 || a==0x102 || (a>=0x200 && a<=0x2FF) || (a>=0x900 && a<=0xA0F); }
struct Window {
    uintptr_t owner=0;
    uint64_t start=0;
    bool active=false;
    void Arm(uintptr_t character,uint64_t frame) { owner=character; start=frame; active=true; }
    void Observe(uintptr_t character,uint32_t action,uint64_t frame) {
        if(!active) return;
        if(character!=owner || frame<start || frame-start>180 || Neutral(action) ||
           (action>=0x4000 && action<=0x4FFF && action!=0x460D)) active=false;
    }
    bool Blocks(bool enabled,int scope,int player) const {
        return enabled && active && (player==1 || player==2) && (scope==0 || scope==player);
    }
};
} }
