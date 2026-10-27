#pragma once
#include <cstring>
#include <cstdint>
namespace MK9TradeFix { namespace StageVisibility {
enum class Kind { Keep, Scenery, AmbientFX };
inline char Lower(char c) { return c>='A' && c<='Z' ? c+('a'-'A') : c; }
inline bool Prefix(const char* text,const char* prefix) {
    while(*prefix) { if(Lower(*text++)!=Lower(*prefix++)) return false; } return true;
}
inline bool Equal(const char* a,const char* b) { return Prefix(a,b) && std::strlen(a)==std::strlen(b); }
inline bool FighterAsset(const char* package) { return Prefix(package,"CHAR_") || Prefix(package,"FX_CHAR_"); }
inline bool StageMapAsset(const char* package) { return Prefix(package,"BGND_"); }
inline bool StageFXAsset(const char* package,const char* levelPackage) {
    return (Prefix(levelPackage,"BGND_") && Prefix(package,"FX_") && Equal(package+3,levelPackage+5)) ||
        (Equal(package,"FX_Pit") && Equal(levelPackage,"BGND_PitDay")); // Live PitDay eruption assets.
}
inline bool StageWorldFXAsset(const char* package,const char* world) {
    if(!Prefix(package,"FX_") || !package[3] || !Prefix(world,package+3)) return false;
    const char* tail=world+std::strlen(package+3);
    return Prefix(tail,"_Global_") || Prefix(tail,"_Cine_");
}
inline bool PlacedStageAmbient(bool persistentLevel,bool componentOnActor,uint32_t actorFlags,bool fighterOwner) {
    return persistentLevel && componentOnActor && (actorFlags&0x11)!=0 && !fighterOwner;
}
inline bool StageSceneActor(const char* type) {
    return Equal(type,"StaticMeshActor") || Equal(type,"InterpActor") ||
        Equal(type,"SkeletalMeshActor") || Equal(type,"DynamicSMActor") || Equal(type,"Emitter") || Equal(type,"EmitterSpawnable");
}
inline bool SameStageCinemaWorld(const char* confirmedWorld,const char* candidateWorld) {
    if(FighterAsset(confirmedWorld) || FighterAsset(candidateWorld)) return false;
    const char* cinema=std::strstr(candidateWorld,"_Cine_");
    const char* family=std::strstr(confirmedWorld,"_Global_");
    if(!family) family=std::strstr(confirmedWorld,"_Cine_");
    if(!cinema || !family || family==confirmedWorld ||
       family-confirmedWorld!=cinema-candidateWorld) return false;
    for(const char* a=confirmedWorld,*b=candidateWorld;a<family;++a,++b)
        if(Lower(*a)!=Lower(*b)) return false;
    return true;
}
inline Kind Classify(bool fighterBound,bool fighterAsset,bool skeletal,bool particle,
                     bool confirmedSceneAsset,bool confirmedStageFX,bool confirmedPlacedAmbient=false) {
    if(fighterBound || fighterAsset) return Kind::Keep;
    if(confirmedPlacedAmbient) return Kind::AmbientFX;
    if(skeletal && confirmedSceneAsset) return Kind::Scenery;
    if(particle && confirmedStageFX) return Kind::AmbientFX;
    return Kind::Keep; // Unknown/shared resources remain visible.
}
} }
