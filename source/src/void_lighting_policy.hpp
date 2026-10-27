#pragma once
#include <cstdint>
#include <cstddef>
namespace MK9TradeFix { namespace VoidLighting {
inline char Lower(char c) { return c>='A' && c<='Z' ? c+32 : c; }
inline bool Contains(const char* text,const char* token) {
    if(!text) return false;
    for(;*text;++text) {
        const char* a=text; const char* b=token;
        while(*a && *b && Lower(*a)==Lower(*b)) { ++a; ++b; }
        if(!*b) return true;
    }
    return false;
}
inline bool SoulName(const char* text) { return Contains(text,"soul_steal"); }
inline bool ShangName(const char* text) { return Contains(text,"shangtsung") || Contains(text,"shang_tsung"); }
inline bool SoulStealLanded(bool shangMesh,const char* attack,const char* current,const char* script,
                            const char* victim,uint32_t attackerAction,uint32_t victimAction) {
    const bool namedScript=ShangName(script) && SoulName(script);
    if(!shangMesh && !namedScript) return false;
    if(Contains(current,"miss") || Contains(script,"miss")) return false;
    // Cached attack names alone can outlive a move. Require current authored
    // evidence, or the matching victim reaction, rather than shared numeric IDs.
    const bool victimSoul=SoulName(victim);
    const bool namedMove=namedScript || SoulName(current) || (SoulName(attack) && victimSoul);
    const bool active=attackerAction==0x5200 || attackerAction==0x5300 || attackerAction==0x5208 ||
        attackerAction==0x4203 || attackerAction==0x5400;
    return namedMove && active && (victimSoul || victimAction==0x4600 || victimAction==0x460D);
}
inline bool FormChanged(uintptr_t oldMesh,uintptr_t mesh,uintptr_t oldScript,uintptr_t script) {
    return (oldMesh && mesh && oldMesh!=mesh) || (oldScript && script && oldScript!=script);
}
} }
