#pragma once
// Included only by extra_gameplay.cpp. Native calls retain component initialization/cleanup.
#include "clean_fighters_policy.hpp"
#include "hook_pair.hpp"
namespace MK9TradeFix { namespace CleanFighters {
static std::atomic<bool> s_Enabled{false},s_Ready{false};
static std::atomic<unsigned> s_BloodTemplates{0},s_BloodActivations{0},s_DamageResets{0},s_Faults{0};
using SetTemplateFn=void(__thiscall*)(void*,void*);
using ActivateFn=void(__thiscall*)(void*,int);
static SetTemplateFn s_SetTemplate=nullptr;
static ActivateFn s_Activate=nullptr;
static bool IsBlood(uintptr_t asset) {
    __try {
        if(asset<0x01000000 || asset==0xDEADBEEF) return false;
        char type[64],package[128],name[128];
        return StageVisibility::Name(*(uintptr_t*)(asset+0x30),type,sizeof(type)) &&
            !strcmp(type,"ParticleSystem") && StageVisibility::Name(asset,name,sizeof(name)) &&
            StageVisibility::Name(StageVisibility::Root(asset),package,sizeof(package)) && BloodTemplate(package,name);
    } __except(EXCEPTION_EXECUTE_HANDLER) { ++s_Faults; return false; }
}
static bool ClearParticle(void* component) {
    __try {
        if(!IsBlood(*(uintptr_t*)((uintptr_t)component+0x258))) return false;
        s_SetTemplate(component,nullptr); // Native SetTemplate(NULL) destroys old emitter instances.
        ++s_BloodTemplates; return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { ++s_Faults; return false; }
}
static void __fastcall SetTemplateHook(void* component,void*,void* asset) {
    StageVisibility::BeforeParticleTemplateChange(component);
    if(s_Enabled.load() && IsBlood((uintptr_t)asset)) { asset=nullptr; ++s_BloodTemplates; }
    s_SetTemplate(component,asset);
    StageVisibility::ApplyParticleVisibility(component);
}
static void __fastcall ActivateHook(void* component,void*,int attached) {
    if(s_Enabled.load() && ClearParticle(component)) ++s_BloodActivations;
    StageVisibility::ApplyParticleVisibility(component);
    s_Activate(component,attached);
    StageVisibility::ApplyParticleVisibility(component);
}
static void Initialize() {
    static const BYTE signature[]={0x53,0x8B,0xDC,0x83,0xEC,0x08,0x83,0xE4,0xF0,0x83,0xC4,0x04,0x55,0x8B,0x6B,0x04};
    static const BYTE damageSignature[]={0x55,0x8B,0xEC,0x56,0xE8,0x17,0xB4,0xB9,0xFF};
    if(memcmp((void*)0x007D0500,signature,sizeof(signature)) ||
       memcmp((void*)0x007CE610,signature,sizeof(signature)) ||
       memcmp((void*)0x00894960,damageSignature,sizeof(damageSignature))) {
        LogTrace("[CLEAN FIGHTERS] Signature mismatch; disabled"); return;
    }
    const HookSpec pair[2]={{(void*)0x007D0500,(void*)SetTemplateHook,(void**)&s_SetTemplate},
                            {(void*)0x007CE610,(void*)ActivateHook,(void**)&s_Activate}};
    s_Ready=InstallHookPair(pair,MH_CreateHook,MH_EnableHook,MH_DisableHook,MH_RemoveHook,
        [](const char* phase,void* target,MH_STATUS status){LogTrace("[CLEAN FIGHTERS] %s %p failed: %d",phase,target,(int)status);});
    LogTrace("[CLEAN FIGHTERS] Native particle hooks %s",s_Ready.load()?"active":"unavailable");
}
static bool DirtyMesh(uintptr_t mesh) {
    __try {
        const int count=*(int*)(mesh+0x574);
        const uintptr_t regions=*(uintptr_t*)(mesh+0x570);
        if(count<0 || count>64 || !regions) return false;
        for(int r=0;r<count;++r) {
            const uintptr_t region=regions+r*16;
            const int slots=*(int*)(region+8);
            const uintptr_t data=*(uintptr_t*)(region+4);
            if(slots<0 || slots>128 || (slots && !data)) return false;
            for(int i=0;i<slots;++i) if(*(int*)(data+i*20+4)) return true;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { ++s_Faults; }
    return false;
}
static void CleanMesh(void* character) {
    __try {
        if(!character || (uintptr_t)character<0x01000000) return;
        const uintptr_t mesh=*(uintptr_t*)((uintptr_t)character+0x10C);
        char type[64];
        if(!StageVisibility::Name(*(uintptr_t*)(mesh+0x30),type,sizeof(type)) || strcmp(type,"SkeletalMeshComponent")) return;
        if(!DirtyMesh(mesh)) return;
        // Same native operation as the NODAMAGE debug command: copy damage regions,
        // select CDRPRESET_NoDamage (14), and invalidate the render state. No health writes.
        ((void(__thiscall*)(void*,int))0x00894960)(character,0); ++s_DamageResets;
    } __except(EXCEPTION_EXECUTE_HANDLER) { ++s_Faults; }
}
static void Tick(void* p1,void* p2,bool requested) {
    const bool enabled=requested && s_Ready.load();
    const bool changed=enabled!=s_Enabled.exchange(enabled);
    if(!enabled) return;
    CleanMesh(p1); CleanMesh(p2);
    if(!changed) return;
    // Clear already-running blood when enabled mid-match. New effects are intercepted
    // synchronously in SetTemplate/ActivateSystem, before they can emit particles/decals.
    uintptr_t objects=0; int count=0;
    if(!StageVisibility::ObjectArray(objects,count)) return;
    for(int i=0;i<count;++i) {
        const uintptr_t object=StageVisibility::ObjectAt(objects,i);
        __try {
            char type[64];
            if(object>=0x01000000 && StageVisibility::Name(*(uintptr_t*)(object+0x30),type,sizeof(type)) &&
               !strcmp(type,"ParticleSystemComponent") && *(uintptr_t*)(object+0x48)>=0x01000000 &&
               !(*(uint32_t*)(object+8)&0x600)) ClearParticle((void*)object);
        } __except(EXCEPTION_EXECUTE_HANDLER) { ++s_Faults; }
    }
}
} }
