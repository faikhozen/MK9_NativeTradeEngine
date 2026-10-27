#pragma once
// Included by extra_gameplay.cpp only. All engine-object changes run on the game tick.
#include "stage_visibility_policy.hpp"
#include "hook_pair.hpp"
#include "imgui/imgui.h"
#include <atomic>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <string>
#include <mutex>
namespace MK9TradeFix { namespace StageVisibility {
static std::atomic<bool> s_HideScenery{false},s_HideAmbient{false};
static std::atomic<int> s_SceneryCount{0},s_AmbientCount{0},s_KeptCount{0};
static std::atomic<bool> s_Available{true};
struct Component {
    uintptr_t object=0,owner=0,asset=0,objectClass=0,outer=0,root=0;
    uint32_t index=0,name=0,number=0,assetOffset=0;
    bool skeletal=false,particle=false,staticMesh=false,sceneActor=false,placedAmbient=false,placedScene=false,fighterBound=false,fighterAsset=false,stageFX=false;
};
struct Hidden { Component component; Kind kind; };
static std::vector<Hidden> s_Hidden;
static std::vector<Hidden> s_Wanted;
static int s_LastObjectCount=0;
static std::recursive_mutex s_Lock;
static std::unordered_set<uintptr_t> s_SceneAssets,s_StageRoots,s_FXPackages;
static std::vector<std::string> s_WorldNames;
static uintptr_t s_Actor1=0,s_Actor2=0;
static char s_Level[128]={};
using SetHiddenFn=void(__thiscall*)(void*,uint32_t);
static SetHiddenFn s_SetHidden=nullptr;
using AttachFn=void(__thiscall*)(void*,void*,void*,void*);
static AttachFn s_Attach=nullptr;
static std::atomic<unsigned> s_BlockedUnhides{0};
static std::atomic<unsigned> s_AttachGuards{0};
static uintptr_t s_PreviousLevel=0;
static bool s_PreviousActive=false,s_PreviousScenery=false,s_PreviousAmbient=false;
static uint64_t s_LastScan=0;
static bool Name(uintptr_t object,char* result,size_t size) {
    result[0]=0;
    __try {
        if(object<0x01000000 || object==0xDEADBEEF) return false;
        const uint32_t index=*(uint32_t*)(object+0x28);
        if(index>1000000) return false;
        const uintptr_t names=*(uintptr_t*)0x00E7C058;
        const uintptr_t entry=*(uintptr_t*)(names+index*4);
        if(entry<0x01000000 || entry==0xDEADBEEF) return false;
        strncpy_s(result,size,(char*)(entry+0x10),_TRUNCATE); return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { result[0]=0; return false; }
}
static uintptr_t Root(uintptr_t object) {
    __try {
        for(int i=0;i<16 && object>=0x01000000 && object!=0xDEADBEEF;++i) {
            const uintptr_t outer=*(uintptr_t*)(object+0x24);
            if(!outer) return object;
            if(outer==object) return 0;
            object=outer;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return 0;
}
static uintptr_t Actor(void* character) {
    __try { return character ? *(uintptr_t*)((uintptr_t)character+8) : 0; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
static bool ReadComponent(uintptr_t object,uintptr_t p1,uintptr_t p2,const char* level,Component& out) {
    __try {
        if(object<0x01000000 || object==0xDEADBEEF) return false;
        char type[64];
        out.objectClass=*(uintptr_t*)(object+0x30);
        if(!Name(out.objectClass,type,sizeof(type))) return false;
        out.skeletal=Equal(type,"SkeletalMeshComponent");
        out.particle=Equal(type,"ParticleSystemComponent");
        out.staticMesh=Equal(type,"StaticMeshComponent");
        if(!out.skeletal && !out.particle && !out.staticMesh) return false;
        out.owner=*(uintptr_t*)(object+0x48);
        if(out.owner<0x01000000 || out.owner==0xDEADBEEF) return false; // Skip templates/defaults.
        out.object=object; out.index=*(uint32_t*)(object+4);
        out.name=*(uint32_t*)(object+0x28); out.number=*(uint32_t*)(object+0x2C);
        out.outer=*(uintptr_t*)(object+0x24); out.root=Root(out.owner);
        out.fighterBound=out.owner==p1 || out.owner==p2;
        // Native SetSkeletalMesh (0x7840F0) writes the actual mesh at +0x280.
        // +0x2C4 can mirror it later, but is not authoritative during attachment.
        out.assetOffset=out.skeletal ? 0x280 : (out.staticMesh ? 0x288 : 0x258);
        out.asset=*(uintptr_t*)(object+out.assetOffset);
        char assetType[64],assetPackage[128],ownerPackage[128],ownerType[64],outerName[64];
        if(!Name(*(uintptr_t*)(out.asset+0x30),assetType,sizeof(assetType)) ||
           !Equal(assetType,out.skeletal ? "SkeletalMesh" : (out.staticMesh ? "StaticMesh" : "ParticleSystem")) ||
           !Name(Root(out.asset),assetPackage,sizeof(assetPackage))) return false;
        out.fighterAsset=FighterAsset(assetPackage);
        out.stageFX=StageFXAsset(assetPackage,level);
        Name(out.root,ownerPackage,sizeof(ownerPackage));
        Name(*(uintptr_t*)(out.owner+0x30),ownerType,sizeof(ownerType));
        Name(*(uintptr_t*)(out.owner+0x24),outerName,sizeof(outerName));
        // Placed static meshes include translucent flame/water/vortex materials.
        // Native Actor::bStatic (+0x80 bit 0), or bNoDelete for level fixtures,
        // distinguishes stage fixtures from dynamically spawned combat effects.
        // PersistentLevel ownership also covers streamed stage sublevels whose
        // package names differ from the main BGND_* map.
        const uint32_t actorFlags=*(uint32_t*)(out.owner+0x80);
        out.placedAmbient=(out.staticMesh || out.particle) && ownerPackage[0] &&
            PlacedStageAmbient(Equal(outerName,"PersistentLevel"),out.outer==out.owner,
                actorFlags,FighterAsset(ownerPackage));
        out.sceneActor=out.outer==out.owner && Equal(outerName,"PersistentLevel") &&
            ownerPackage[0] && !FighterAsset(ownerPackage) && StageSceneActor(ownerType);
        // Positive evidence: a placed skeletal actor in this stage or a stage cinema sublevel.
        out.placedScene=out.skeletal && (StageMapAsset(assetPackage) ||
            (out.outer==out.owner && Equal(ownerType,"SkeletalMeshActor") &&
            Equal(outerName,"PersistentLevel") && (Equal(ownerPackage,level) || strstr(ownerPackage,"_Cine_"))));
        // DynamicSMActor is also used for Desert's SandCube. Its BGND asset,
        // rather than its dynamic actor flag, identifies it as stage scenery.
        if(out.staticMesh && StageMapAsset(assetPackage)) out.placedAmbient=true;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static bool StillSame(const Component& c) {
    __try {
        const int count=*(int*)0x00E7C0F0;
        const uintptr_t objects=*(uintptr_t*)0x00E7C0EC;
        return c.index<(uint32_t)count && *(uintptr_t*)(objects+c.index*4)==c.object &&
            *(uintptr_t*)(c.object+0x30)==c.objectClass && *(uint32_t*)(c.object+0x28)==c.name &&
            *(uint32_t*)(c.object+0x2C)==c.number && *(uintptr_t*)(c.object+0x24)==c.outer &&
            *(uintptr_t*)(c.object+c.assetOffset)==c.asset;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static bool HiddenNow(const Component& c) {
    __try { return (*(uint32_t*)(c.object+0x120)&4)!=0; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return true; }
}
static bool SetHidden(const Component& c,bool hidden) {
    __try {
        if(!StillSame(c) || *(uintptr_t*)(*(uintptr_t*)c.object+0x1DC)!=0x00631A10) return false;
        (s_SetHidden ? s_SetHidden : (SetHiddenFn)0x00631A10)((void*)c.object,hidden ? 1u : 0u);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static void Restore() {
    // Only objects changed from visible -> hidden are retained. Originally hidden objects stay hidden.
    for(const auto& item:s_Hidden) SetHidden(item.component,false);
    s_Hidden.clear(); s_Wanted.clear(); s_SceneAssets.clear(); s_StageRoots.clear();
    s_FXPackages.clear(); s_WorldNames.clear(); s_SceneryCount=0; s_AmbientCount=0;
}
static Kind Decision(const Component& c) {
    bool auxiliary=c.sceneActor && s_StageRoots.count(c.root)!=0;
    if(c.sceneActor && c.root!=s_PreviousLevel && !auxiliary) {
        char world[128];
        if(Name(c.root,world,sizeof(world))) for(const auto& known:s_WorldNames)
            if(SameStageCinemaWorld(known.c_str(),world)) { auxiliary=true; break; }
    }
    return Classify(c.fighterBound,c.fighterAsset,c.skeletal,c.particle,
        s_SceneAssets.count(c.asset)!=0,c.stageFX || (c.particle && s_FXPackages.count(Root(c.asset))!=0),
        c.placedAmbient || auxiliary);
}
static bool WantedNow(uintptr_t object,Component& c,Kind& kind) {
    if(!s_PreviousActive || !ExtraGameplay::g_DisableBackground || !s_Available.load() ||
       Root(Actor(GetP1Char()))!=s_PreviousLevel ||
       !ReadComponent(object,s_Actor1,s_Actor2,s_Level,c)) return false;
    kind=Decision(c);
    return (kind==Kind::Scenery && s_HideScenery.load()) || (kind==Kind::AmbientFX && s_HideAmbient.load());
}
static void Record(const Component& c,Kind kind) {
    for(const auto& old:s_Hidden) if(old.component.object==c.object && StillSame(old.component)) return;
    s_Hidden.push_back({c,kind});
}
static void __fastcall SetHiddenHook(void* object,void*,uint32_t hidden) {
    std::lock_guard<std::recursive_mutex> guard(s_Lock);
    Component c; Kind kind=Kind::Keep;
    if(!hidden && WantedNow((uintptr_t)object,c,kind)) {
        // Record native intent to show it, even if the mod already hid it.
        Record(c,kind); hidden=1; ++s_BlockedUnhides;
    }
    s_SetHidden(object,hidden);
}
static void ApplyParticleVisibility(void* object) {
    std::lock_guard<std::recursive_mutex> guard(s_Lock);
    Component c; Kind kind=Kind::Keep;
    if(WantedNow((uintptr_t)object,c,kind) && !HiddenNow(c) && SetHidden(c,true)) Record(c,kind);
}
static void BeforeParticleTemplateChange(void* object) {
    std::lock_guard<std::recursive_mutex> guard(s_Lock);
    // Pool reuse must not carry a stage template's mod-owned hidden bit into
    // the next fighter template. Restore while the old identity still matches.
    for(auto it=s_Hidden.begin();it!=s_Hidden.end();) {
        if(it->component.object==(uintptr_t)object) { SetHidden(it->component,false); it=s_Hidden.erase(it); }
        else ++it;
    }
}
static bool HideBeforeAttach(const Component& c) {
    __try {
        if(!StillSame(c)) return false;
        // Attach is about to construct the render state. Set the same native
        // bHiddenGame bit here without recursively requesting another attach.
        *(uint32_t*)(c.object+0x120)|=4;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static void __fastcall AttachHook(void* object,void*,void* scene,void* actor,void* transform) {
    std::lock_guard<std::recursive_mutex> guard(s_Lock);
    Component c; Kind kind=Kind::Keep;
    if(WantedNow((uintptr_t)object,c,kind)) {
        if(!HiddenNow(c) && HideBeforeAttach(c)) Record(c,kind);
        ++s_AttachGuards;
    }
    s_Attach(object,scene,actor,transform);
}
static void Initialize() {
    static const BYTE signature[]={0x55,0x8B,0xEC,0x8B,0x81,0x20,1,0,0,0x8B,0x55,8};
    static const BYTE attachSignature[]={0x55,0x8B,0xEC,0x56,0x8B,0xF1,0xF6,0x86,0x58,1,0,0,1};
    if(memcmp((void*)0x00631A10,signature,sizeof(signature)) ||
       memcmp((void*)0x00634E30,attachSignature,sizeof(attachSignature))) { s_Available=false; return; }
    const HookSpec pair[2]={{(void*)0x00631A10,(void*)SetHiddenHook,(void**)&s_SetHidden},
                            {(void*)0x00634E30,(void*)AttachHook,(void**)&s_Attach}};
    s_Available=InstallHookPair(pair,MH_CreateHook,MH_EnableHook,MH_DisableHook,MH_RemoveHook,
        [](const char* phase,void* target,MH_STATUS status){LogTrace("[STAGE VISIBILITY] %s %p failed: %d",phase,target,(int)status);});
    if(!s_Available.load()) { s_SetHidden=nullptr; s_Attach=nullptr; }
    LogTrace("[STAGE VISIBILITY] Native unhide/attach guards %s",s_Available.load()?"active":"unavailable");
}
static void Enforce() {
    // Runs after native simulation: cinema scripts may unhide an existing
    // component without changing its asset. Keep originally-hidden objects out
    // of the restoration ledger until the game actually makes them visible.
    for(const auto& item:s_Wanted) {
        const auto& c=item.component;
        if(!StillSame(c) || HiddenNow(c)) continue;
        if(!SetHidden(c,true)) continue;
        bool recorded=false;
        for(const auto& old:s_Hidden) if(old.component.object==c.object && StillSame(old.component)) { recorded=true; break; }
        if(!recorded) s_Hidden.push_back(item);
    }
    int scenes=0,effects=0;
    for(const auto& item:s_Hidden) { if(item.kind==Kind::Scenery) ++scenes; else ++effects; }
    s_SceneryCount=scenes; s_AmbientCount=effects;
}
static bool ObjectArray(uintptr_t& objects,int& count) {
    __try { objects=*(uintptr_t*)0x00E7C0EC; count=*(int*)0x00E7C0F0; return objects && count>0 && count<=500000; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static uintptr_t ObjectAt(uintptr_t objects,int index) {
    __try { return *(uintptr_t*)(objects+index*4); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
static void Tick(void* p1,void* p2,bool voidActive) {
    std::lock_guard<std::recursive_mutex> guard(s_Lock);
    const bool scenery=s_HideScenery.load(),ambient=s_HideAmbient.load();
    const uintptr_t actor1=Actor(p1),actor2=Actor(p2),levelRoot=Root(actor1);
    char level[128]; Name(levelRoot,level,sizeof(level));
    const bool active=voidActive && (scenery || ambient) && Prefix(level,"BGND_");
    const bool changed=active!=s_PreviousActive || scenery!=s_PreviousScenery || ambient!=s_PreviousAmbient || levelRoot!=s_PreviousLevel;
    if(changed) Restore();
    s_PreviousActive=active; s_PreviousScenery=scenery; s_PreviousAmbient=ambient; s_PreviousLevel=levelRoot;
    s_Actor1=actor1; s_Actor2=actor2; strncpy_s(s_Level,sizeof(s_Level),level,_TRUNCATE);
    if(!active || !s_Available.load()) return;
    uintptr_t objects=0; int count=0;
    if(!ObjectArray(objects,count)) return;
    if(!changed && count==s_LastObjectCount && g_CurrentFrame-s_LastScan<30) { Enforce(); return; }
    s_LastObjectCount=count;
    s_LastScan=g_CurrentFrame;
    std::vector<Component> components;
    std::unordered_set<uintptr_t> sceneAssets;
    std::unordered_set<uintptr_t> stageSublevels;
    std::unordered_set<uintptr_t> stageFXPackages;
    for(int i=0;i<count;++i) {
        const uintptr_t object=ObjectAt(objects,i);
        Component c;
        if(ReadComponent(object,actor1,actor2,level,c)) {
            if(c.placedScene && !c.fighterBound && !c.fighterAsset) sceneAssets.insert(c.asset);
            // A fixed stage fixture positively identifies its auxiliary world as
            // scenery. Include animated actors in that same world (e.g. the
            // soulnado's InterpActors/SkeletalMeshActor in Grave_Global_FX).
            // Never promote the main fight world: it also owns combat spawns.
            if(c.placedAmbient && !c.fighterBound && !c.fighterAsset && c.root!=levelRoot)
                stageSublevels.insert(c.root);
            // A particle package used by a placed stage fixture also identifies
            // cinematic spawns from that package (FX_RoofDay != RooftopDay).
            if(c.particle && c.placedAmbient && !c.fighterBound && !c.fighterAsset) {
                char package[128],world[128];
                if(Name(Root(c.asset),package,sizeof(package)) && Name(c.root,world,sizeof(world)) &&
                   StageWorldFXAsset(package,world)) stageFXPackages.insert(Root(c.asset));
            }
            components.push_back(c);
        }
    }
    std::unordered_map<uintptr_t,Kind> wanted;
    std::vector<std::string> stageWorldNames;
    for(const auto root:stageSublevels) {
        char name[128];
        if(Name(root,name,sizeof(name))) stageWorldNames.emplace_back(name);
    }
    s_SceneAssets=sceneAssets; s_StageRoots=stageSublevels; s_FXPackages=stageFXPackages; s_WorldNames=stageWorldNames;
    int kept=0;
    for(const auto& c:components) {
        bool auxiliaryScene=c.sceneActor && stageSublevels.count(c.root)!=0;
        if(c.sceneActor && c.root!=levelRoot && !auxiliaryScene) {
            char world[128];
            if(Name(c.root,world,sizeof(world))) {
                for(const auto& confirmed:stageWorldNames) {
                    if(SameStageCinemaWorld(confirmed.c_str(),world)) { auxiliaryScene=true; break; }
                }
            }
        }
        const Kind kind=Classify(c.fighterBound,c.fighterAsset,c.skeletal,c.particle,
            sceneAssets.count(c.asset)!=0,c.stageFX || (c.particle && stageFXPackages.count(Root(c.asset))!=0),
            c.placedAmbient || auxiliaryScene);
        if((kind==Kind::Scenery && scenery) || (kind==Kind::AmbientFX && ambient)) wanted[c.object]=kind;
        else ++kept;
    }
    for(auto it=s_Hidden.begin();it!=s_Hidden.end();) {
        if(!StillSame(it->component) || !wanted.count(it->component.object)) {
            SetHidden(it->component,false); it=s_Hidden.erase(it);
        } else { if(!HiddenNow(it->component)) SetHidden(it->component,true); ++it; }
    }
    s_Wanted.clear();
    for(const auto& c:components) {
        const auto decision=wanted.find(c.object);
        if(decision!=wanted.end()) s_Wanted.push_back({c,decision->second});
    }
    Enforce(); s_KeptCount=kept;
}
static void RenderUI() {
    bool scenery=s_HideScenery.load(),ambient=s_HideAmbient.load();
    if(ImGui::Checkbox("Hide stage characters and animated props##stage_scene",&scenery)) s_HideScenery=scenery;
    if(ImGui::Checkbox("Hide stage ambient FX and translucent meshes##stage_fx",&ambient)) s_HideAmbient=ambient;
    ImGui::TextWrapped("Preserves fighter assets and unidentified/shared effects. Changes visibility only; stage objects keep running.");
    if(!s_Available.load()) ImGui::TextDisabled("Object visibility unavailable: native signature mismatch.");
    else ImGui::Text("Hidden: %d animated components, %d ambient/static components | Kept: %d",s_SceneryCount.load(),s_AmbientCount.load(),s_KeptCount.load());
    ImGui::Text("Scripted unhide attempts blocked: %u",s_BlockedUnhides.load());
    ImGui::Text("Stage render attachments guarded: %u",s_AttachGuards.load());
}
} }
