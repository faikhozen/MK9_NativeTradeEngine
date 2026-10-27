#pragma once
#include <cstring>
namespace MK9TradeFix { namespace CleanFighters {
inline bool Package(const char* value,const char* base) {
    const size_t n=std::strlen(base);
    if(std::strncmp(value,base,n)) return false;
    if(!value[n]) return true;
    // Cooked packages append an eight-digit content hash, e.g. FightingArt-04b73c4a.
    if(value[n]!='-' || std::strlen(value+n+1)!=8) return false;
    for(size_t i=n+1;value[i];++i)
        if(!((value[i]>='0'&&value[i]<='9')||(value[i]>='a'&&value[i]<='f')||(value[i]>='A'&&value[i]<='F'))) return false;
    return true;
}
// Restrict interception to blood libraries and explicitly named fighter blood particles.
// Never classify a character package itself as blood (it also contains projectiles).
inline bool BloodTemplate(const char* package,const char* asset) {
    if(!package || !asset) return false;
    if(Package(package,"FX_Blood")) return true;
    if(!Package(package,"FightingArt") && strncmp(package,"CHAR_",5) && strncmp(package,"FX_CHAR_",8)) return false;
    return !strncmp(asset,"Blood_",6) || !strncmp(asset,"FX_Blood",8);
}
} }
