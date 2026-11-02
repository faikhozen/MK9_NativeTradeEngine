#include "attack_restrictions.hpp"
#include "engine_common.hpp"
#include "minhook/include/MinHook.h"
#include "imgui/imgui.h"
#include <atomic>
#include <cstring>

namespace MK9TradeFix { namespace AttackRestrictions {
// One atomic snapshot includes the height in tenths of a native unit.
static std::atomic<unsigned> s_Options{500u << 16};
bool g_AirHeightGateReady = false;
volatile unsigned g_AirHeightBlocks = 0;
std::atomic<float> g_P1AirHeight{0}, g_P2AirHeight{0}, g_LastRejectedAirHeight{0};
bool g_NormalGateReady = false, g_SpecialGateReady = false;
bool g_XRayGateReady = false;


volatile unsigned g_XRayDirectBlocks = 0;
volatile unsigned g_SpecialCancelBlocks = 0, g_EnhancedCancelBlocks = 0, g_XRayCancelBlocks = 0;
bool g_CommandGateReady = false;
volatile unsigned g_CommandQueryBlocks = 0;
volatile unsigned g_AlternateXRayQueryBlocks = 0;
volatile unsigned g_NormalBlocks = 0, g_SpecialBlocks = 0, g_EnhancedBlocks = 0, g_XRayBlocks = 0;
Settings GetSettings() {
    const unsigned bits = s_Options.load(std::memory_order_relaxed);
    Settings s;
    s.disableStrings = (bits & 1) != 0;
    s.disableSpecials = (bits & 2) != 0;
    s.disableEnhanced = (bits & 4) != 0;
    s.disableXRay = (bits & 8) != 0;
    s.scope = (bits >> 4) & 3;
    s.disableNormalToSpecial = (bits & 0x40) != 0;
    s.disableNormalToEnhanced = (bits & 0x80) != 0;
    s.disableNormalToXRay = (bits & 0x100) != 0;
    s.limitAirSpecialHeight = (bits & 0x200) != 0;
    s.minimumAirSpecialHeight = float(bits >> 16) * 0.1f;
    return s;
}
void SetSettings(const Settings& s) {
    const unsigned scope = s.scope >= 0 && s.scope <= 2 ? (unsigned)s.scope : 0;
    s_Options.store((s.disableStrings ? 1u : 0u) | (s.disableSpecials ? 2u : 0u) |
        (s.disableEnhanced ? 4u : 0u) | (s.disableXRay ? 8u : 0u) | (scope << 4) |
        (s.disableNormalToSpecial ? 0x40u : 0u) | (s.disableNormalToEnhanced ? 0x80u : 0u) |
        (s.disableNormalToXRay ? 0x100u : 0u) | (s.limitAirSpecialHeight ? 0x200u : 0u) |
        ((unsigned)(ClampAirHeight(s.minimumAirSpecialHeight) * 10.0f + 0.5f) << 16),
        std::memory_order_relaxed);
}
static int PlayerForOwner(void* owner) {
    if (!owner) return 0;
    void* fighters[2] = { GetP1Char(), GetP2Char() };
    __try {
        for (int i = 0; i < 2; ++i) {
            void* f = fighters[i];
            if (f && (uintptr_t)f >= 0x01000000 &&
                (owner == f || owner == *(void**)((uintptr_t)f + 0x7638))) return i + 1;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return 0;
}
struct FloorState { void* character=nullptr; uintptr_t actor=0; uint32_t serial=0; float z=0; bool valid=false; float peak=0; bool elevated=false; float standing=0; };
static FloorState s_Floor[2]; // Simulation-thread state; UI reads atomic telemetry only.
static std::atomic<bool> s_HeightReady[2];
static std::atomic<float> s_PeakHeight[2], s_StandingHeight[2];
static bool NativeAirborne(void* character) {
    const uintptr_t table = *(uintptr_t*)character;
    typedef int (__thiscall *AirFn)(void*);
    return ((AirFn)*(uintptr_t*)(table + 0x1FC))(character) != 0;
}
static bool ReadActor(void* character, uintptr_t& actor, float& z, uint32_t& serial) {
    if (!character || (uintptr_t)character < 0x01000000) return false;
    actor = *(uintptr_t*)((uintptr_t)character + 8);
    if (actor < 0x01000000 || actor == 0xDEADBEEF) return false;
    z = *(float*)(actor + 0xE4);
    serial = *(uint32_t*)((uintptr_t)character + 4);
    return std::isfinite(z);
}
// Same native active hurt-circle array used by the collision visualizer.
// Sample only standing idle (action 0), never crouch, attacks or airborne poses.
static float ReadStandingHeight(void* character, float floor) {
    const uintptr_t col=*(uintptr_t*)((uintptr_t)character+0x2F10);
    const int handle=*(int*)((uintptr_t)character+0x2F14);
    if (col<0x01000000 || col==0xDEADBEEF || handle==-1 || *(int*)(col+4)!=handle) return 0;
    const uintptr_t array=*(uintptr_t*)(col+0x18);
    const uint32_t count=*(uint32_t*)(col+0x184);
    if (array<0x01000000 || count==0 || count>48) return 0;
    float top=0;
    for (uint32_t n=0;n<count;++n) {
        const uintptr_t entry=array+n*0x28;
        if (!*(uint32_t*)(entry+4)) continue;
        const float z=*(float*)(entry+0x10), radius=*(float*)(entry+0x14);
        if (!std::isfinite(z) || !std::isfinite(radius) || radius<=0 || radius>400) continue;
        const float height=z+radius-floor;
        if (height>top && height<1000) top=height;
    }
    return top;
}
void OnFrameTick(void* p1, void* p2) {
    void* fighters[] = {p1,p2};
    for (int i=0; i<2; ++i) {
        bool ready=false; float height=0;
        __try {
            uintptr_t actor=0; float z=0; uint32_t serial=0;
            auto& floor=s_Floor[i];
            if (ReadActor(fighters[i],actor,z,serial)) {
                if (floor.character!=fighters[i] || floor.actor!=actor || floor.serial!=serial) {
                    floor=FloorState{}; floor.character=fighters[i]; floor.actor=actor; floor.serial=serial;
                }
                const uint32_t action=*(uint32_t*)((uintptr_t)fighters[i]+0x1D4);
                const bool groundAction=action==0 || action==0x100 || action==0x101 || action==0x102 ||
                    action==0x2000 || action==0x2001 || action==0x2010 || action==0x2011;
                if (groundAction && !NativeAirborne(fighters[i])) { floor.z=z; floor.valid=true; }
                ready=floor.valid;
                if (ready && action==0 && !NativeAirborne(fighters[i])) {
                    const float standing=ReadStandingHeight(fighters[i],floor.z);
                    if (standing>0) floor.standing=standing;
                }
                if (ready) {
                    height=HeightAboveFloor(z,floor.z);
                    // Keep the last peak on landing, reset at the next excursion.
                    if (height > 0.1f) {
                        if (!floor.elevated) floor.peak=0;
                        floor.elevated=true;
                        if (height > floor.peak) floor.peak=height;
                    } else floor.elevated=false;
                }
            } else floor=FloorState{};
        } __except(EXCEPTION_EXECUTE_HANDLER) { s_Floor[i]=FloorState{}; }
        s_StandingHeight[i].store(ready ? s_Floor[i].standing : 0,std::memory_order_relaxed);
        s_PeakHeight[i].store(ready ? s_Floor[i].peak : 0,std::memory_order_relaxed);
        s_HeightReady[i].store(ready,std::memory_order_relaxed);
        (i==0 ? g_P1AirHeight : g_P2AirHeight).store(height,std::memory_order_relaxed);
    }
}
static bool RejectLowAirMove(const Settings& s, MoveKind kind, int player, bool airOnly) {
    if (!s.limitAirSpecialHeight || !Applies(s.scope,player) ||
        (kind!=MoveKind::Special && kind!=MoveKind::Enhanced)) return false;
    __try {
        void* character=player==1 ? GetP1Char() : GetP2Char();
        uintptr_t actor=0; float z=0; uint32_t serial=0;
        if (!ReadActor(character,actor,z,serial)) return false;
        const auto& floor=s_Floor[player-1];
        const bool valid=floor.valid && floor.character==character && floor.actor==actor && floor.serial==serial;
        const float height=valid ? HeightAboveFloor(z,floor.z) : 0;
        if (RejectAirHeight(s,kind,player,airOnly || NativeAirborne(character),height,valid)) {
            ++g_AirHeightBlocks;
            g_LastRejectedAirHeight.store(height,std::memory_order_relaxed);
            return true;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return false;
}
// Character+2FA4/+2FA8 is the main move VM and its handle (verified 0089FE60).
// Follow live native call frames just as 0045C380 unwinds them.
static bool HasNormalScript(void* character, bool& executingCancelHelper) {
    executingCancelHelper = false;
    __try {
        uintptr_t vm = *(uintptr_t*)((uintptr_t)character + 0x2FA4);
        if (vm < 0x01000000 || IsBadReadPtr((void*)vm, 0x50) ||
            *(uint32_t*)(vm + 4) != *(uint32_t*)((uintptr_t)character + 0x2FA8)) return false;
        const uint32_t state = *(uint32_t*)(vm + 0x2C);
        if (state != 1 && state != 2 && state != 4) return false;
        const uintptr_t stack = *(uintptr_t*)(vm + 0x0C);
        uintptr_t frame = *(uintptr_t*)(vm + 0x10), end = *(uintptr_t*)(vm + 0x14);
        uintptr_t desc = *(uintptr_t*)(vm + 0x30);
        bool normal = false, helper = false;
        for (int depth = 0; depth < 16; ++depth) {
            if (desc < 0x00400000 || IsBadReadPtr((void*)desc, 0x2C)) return false;
            const char* name = *(const char**)desc;
            if (name && !IsBadReadPtr(name, 64)) {
                char bounded[65] = {};
                std::memcpy(bounded, name, 64);
                normal = normal || IsNormalScriptName(bounded);
                helper = helper || std::strcmp(bounded, "special_move_combo") == 0;
            }
            if (frame == stack) {
                executingCancelHelper = helper && *(uintptr_t*)0x00E39DF4 == vm;
                return normal;
            }
            if (stack < 0x01000000 || frame < stack || end < frame + 16 ||
                end - stack > 0x100000 || IsBadReadPtr((void*)(end - 16), 16)) return false;
            desc = *(uintptr_t*)(end - 8);
            if (desc < 0x00400000 || IsBadReadPtr((void*)desc, 0x2C)) return false;
            const uint32_t slots = *(uint32_t*)(desc + 0x28), locals = *(uint32_t*)(desc + 0x14);
            if (slots > 2048 || locals > 16384) return false;
            const uintptr_t size = slots * 8 + 16 + locals;
            end = frame;
            if (size > end - stack) return false;
            frame = end - size;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return false;
}
static bool InNativeNormal(int player) {
    if (player != 1 && player != 2) return false;
    void* fighter = player == 1 ? GetP1Char() : GetP2Char();
    if (!fighter) return false;
    __try {
        const uint32_t action = *(uint32_t*)((uintptr_t)fighter + 0x1D4);
        const uint32_t flags = *(uint32_t*)((uintptr_t)fighter + 0x2EC);
        if (IsNativeNormal(action, flags)) return true;
        bool executingHelper = false;
        const bool normalScript = HasNormalScript(fighter, executingHelper);
        return IsNativeNormal(action, flags, normalScript, executingHelper);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static void CountCancel(MoveKind kind) {
    if (kind == MoveKind::Special) ++g_SpecialCancelBlocks;
    else if (kind == MoveKind::Enhanced) ++g_EnhancedCancelBlocks;
    else if (kind == MoveKind::XRay) ++g_XRayCancelBlocks;
}
// Shared scripted command-pattern query, cdecl void(), native ret0.
// generic_*_2in1_check branches on VM+1C before reset (008D70E0) and
// transfer (008D99C0 / 008D9A00). A false query leaves the normal intact.
typedef void (__cdecl *CommandQueryFn)();
static CommandQueryFn s_CommandQuery = nullptr;
static CommandQueryFn s_AlternateXRayQuery = nullptr;
static bool RejectCommandQuery() {
    const Settings s = GetSettings();
    if (!s.disableSpecials && !s.disableEnhanced && !s.disableXRay &&
        !s.disableNormalToSpecial && !s.disableNormalToEnhanced && !s.disableNormalToXRay && !s.limitAirSpecialHeight) return false;
    __try {
        const uintptr_t vm = *(uintptr_t*)0x00E39DF4;
        if (vm < 0x01000000) return false;
        const uintptr_t desc = *(uintptr_t*)(vm + 0x30);
        if (desc < 0x00400000) return false;
        const char* name = *(const char**)desc;
        if (!name || IsBadReadPtr(name, 64)) return false;
        char bounded[65] = {};
        std::memcpy(bounded, name, 64);
        const MoveKind kind = KindFromCommandFunction(bounded);
        if (kind == MoveKind::Other) return false;
        typedef void* (__cdecl *ResolveActorFn)(uint32_t, uint32_t, uint32_t);
        void* owner = ((ResolveActorFn)0x00458930)(0, *(uint32_t*)0x00C6876C, *(uint32_t*)0x00C68760);
        const int player = PlayerForOwner(owner);
        const bool normal = InNativeNormal(player);
        const bool lowAir=RejectLowAirMove(s,kind,player,IsAirCommandFunction(bounded));
        if (!Reject(s, kind, player, normal) && !lowAir) return false;
        *(uint32_t*)(vm + 0x1C) = 0;
        ++g_CommandQueryBlocks;
        if (RejectCancel(s, kind, player, normal)) CountCancel(kind);
        if (kind == MoveKind::Special) ++g_SpecialBlocks;
        else if (kind == MoveKind::Enhanced) ++g_EnhancedBlocks;
        else if (kind == MoveKind::XRay) ++g_XRayBlocks;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static void __cdecl HookCommandQuery() {
    if (!RejectCommandQuery() && s_CommandQuery) s_CommandQuery();
}
static void __cdecl HookAlternateXRayQuery() {
    // generic_Xray_2in1_check ORs this result with the pattern query.
    // Preserve its normal false-result contract before reset/transfer.
    if (RejectCommandQuery()) { ++g_AlternateXRayQueryBlocks; return; }
    if (s_AlternateXRayQuery) s_AlternateXRayQuery();
}
static void InitializeCommandQuery() {
    void* target = (void*)0x008CBD10;
    void* alternate = (void*)0x008D2330;
    static const unsigned char expected[] = {0xA1,0x60,0x87,0xC6,0x00,0x8B,0x0D,0x6C,0x87,0xC6,0x00};
    if (std::memcmp(target, expected, sizeof(expected)) != 0 ||
        std::memcmp(alternate, expected, sizeof(expected)) != 0) return;
    const bool created = MH_CreateHook(target, (void*)HookCommandQuery, (void**)&s_CommandQuery) == MH_OK;
    const bool altCreated = MH_CreateHook(alternate, (void*)HookAlternateXRayQuery, (void**)&s_AlternateXRayQuery) == MH_OK;
    const bool enabled = created && MH_EnableHook(target) == MH_OK;
    const bool altEnabled = altCreated && MH_EnableHook(alternate) == MH_OK;
    g_CommandGateReady = enabled && altEnabled;
    if (!g_CommandGateReady) {
        if (enabled) MH_DisableHook(target);
        if (altEnabled) MH_DisableHook(alternate);
        if (created) MH_RemoveHook(target);
        if (altCreated) MH_RemoveHook(alternate);
        s_CommandQuery = nullptr; s_AlternateXRayQuery = nullptr;
    }
}
// Character combo_test enumerates follow-ups here, outside the initial matcher.
// Thiscall on Character, three stack arguments, callee pops12 (00896770).
typedef uintptr_t (__fastcall *ComboFn)(void*, void*, uint32_t, uint32_t*, uint32_t*);
static ComboFn s_Combo = nullptr;
static uintptr_t __fastcall HookCombo(void* character, void* edx, uint32_t skip,
                                     uint32_t* outDirection, uint32_t* outButton) {
    const Settings s = GetSettings();
    if (s.disableStrings) {
        __try {
            const int player = PlayerForOwner(character);
            if (player) {
                const uintptr_t info = *(uintptr_t*)((uintptr_t)character + 0x7638);
                if (info && RejectComboQuery(s, player, *(uint32_t*)(info + 0xB48))) {
                    ++g_NormalBlocks;
                    return 0; // Same failure contract: output arguments are left untouched.
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    return s_Combo ? s_Combo(character, edx, skip, outDirection, outButton) : 0;
}
// Thiscall on PlayerInfo, one stack argument, callee pops 4 (verified 008A1EF0).
// Reject before motion matching, transfer, meter deduction or victim setup.
typedef int (__fastcall *EligibilityFn)(void*, void*, void*);
static EligibilityFn s_Eligibility = nullptr;
static bool RejectMove(void* owner, void* move) {
    const Settings s = GetSettings();
    if (s.disableSpecials || s.disableEnhanced || s.disableXRay || s.disableNormalToSpecial ||
        s.disableNormalToEnhanced || s.disableNormalToXRay || s.limitAirSpecialHeight) {
        __try {
            if (move && (uintptr_t)move >= 0x00400000) {
                const uint32_t type=*(uint32_t*)((uintptr_t)move + 0x70);
                const MoveKind kind = KindFromNativeType(type);
                const int player = PlayerForOwner(owner);
                const bool inNormal = InNativeNormal(player);
                const bool lowAir=RejectLowAirMove(s,kind,player,IsNativeAirSpecial(type));
                if (Reject(s, kind, player, inNormal) || lowAir) {
                    if (RejectCancel(s, kind, player, inNormal)) CountCancel(kind);
                    if (kind == MoveKind::Special) ++g_SpecialBlocks;
                    else if (kind == MoveKind::Enhanced) ++g_EnhancedBlocks;
                    else if (kind == MoveKind::XRay) ++g_XRayBlocks;
                    return true;
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    return false;
}
static int __fastcall HookEligibility(void* owner, void* edx, void* move) {
    if (RejectMove(owner, move)) return 0;
    return s_Eligibility ? s_Eligibility(owner, edx, move) : 0;
}
// Alternate airborne eligibility, including native type 9: thiscall, ret8.
typedef int (__fastcall *AirEligibilityFn)(void*, void*, void*, float);
static AirEligibilityFn s_AirEligibility = nullptr;
static int __fastcall HookAirEligibility(void* owner, void* edx, void* move, float window) {
    if (RejectMove(owner, move)) return 0;
    return s_AirEligibility ? s_AirEligibility(owner, edx, move, window) : 0;
}
// Native type-2 air/distance eligibility: thiscall with three stack arguments,
// ret0C. The first argument remains the same native move descriptor.
typedef int (__fastcall *AirDistanceFn)(void*, void*, void*, float, float);
static AirDistanceFn s_AirDistance = nullptr;
static int __fastcall HookAirDistance(void* owner, void* edx, void* move, float window, float distance) {
    if (RejectMove(owner,move)) return 0;
    return s_AirDistance ? s_AirDistance(owner,edx,move,window,distance) : 0;
}
// Script callbacks also use these dedicated type-8/type-9 checks, without
// calling either general eligibility function. Reject before input acceptance,
// reset/XferToEXSpecialMove, meter spending, and native event publication.
// Both thiscall on PlayerInfo: 008A9680 ret0; 008A9800(float) ret4.
typedef void (__fastcall *XRayCheckFn)(void*, void*);
typedef void (__fastcall *AirXRayCheckFn)(void*, void*, float);
static XRayCheckFn s_XRayCheck = nullptr;
static AirXRayCheckFn s_AirXRayCheck = nullptr;
static bool RejectDirectXRay(void* owner) {
    const Settings s = GetSettings();
    if (!s.disableXRay && !s.disableNormalToXRay) return false;
    const int player = PlayerForOwner(owner);
    const bool inNormal = InNativeNormal(player);
    if (!Reject(s, MoveKind::XRay, player, inNormal)) return false;
    if (RejectCancel(s, MoveKind::XRay, player, inNormal)) CountCancel(MoveKind::XRay);
    ++g_XRayBlocks;
    ++g_XRayDirectBlocks;
    return true;
}
static void __fastcall HookXRayCheck(void* owner, void* edx) {
    if (!RejectDirectXRay(owner) && s_XRayCheck) s_XRayCheck(owner, edx);
}
static void __fastcall HookAirXRayCheck(void* owner, void* edx, float window) {
    if (!RejectDirectXRay(owner) && s_AirXRayCheck) s_AirXRayCheck(owner, edx, window);
}
static void InitializeXRayChecks() {
    void* ground = (void*)0x008A9680;
    void* air = (void*)0x008A9800;
    static const unsigned char groundBytes[] = {0x55,0x8B,0xEC,0x83,0xEC,0x08,0x53,0x6A,0x01};
    static const unsigned char airBytes[] = {0x55,0x8B,0xEC,0x83,0xEC,0x10,0x53,0x6A,0x01};
    if (std::memcmp(ground, groundBytes, sizeof(groundBytes)) != 0 ||
        std::memcmp(air, airBytes, sizeof(airBytes)) != 0) return;
    bool groundCreated = MH_CreateHook(ground, (void*)HookXRayCheck, (void**)&s_XRayCheck) == MH_OK;
    bool airCreated = MH_CreateHook(air, (void*)HookAirXRayCheck, (void**)&s_AirXRayCheck) == MH_OK;
    bool groundEnabled = groundCreated && MH_EnableHook(ground) == MH_OK;
    bool airEnabled = airCreated && MH_EnableHook(air) == MH_OK;
    g_XRayGateReady = groundEnabled && airEnabled;
    if (!g_XRayGateReady) {
        if (groundEnabled) MH_DisableHook(ground);
        if (airEnabled) MH_DisableHook(air);
        if (groundCreated) MH_RemoveHook(ground);
        if (airCreated) MH_RemoveHook(air);
        s_XRayCheck = nullptr; s_AirXRayCheck = nullptr;
    }
}
void Initialize() {
    InitializeXRayChecks();
    InitializeCommandQuery();
    static const unsigned char comboExpected[] = {0x55,0x8B,0xEC,0x56,0x8B,0xF1,0x8B,0x06};
    void* comboTarget = (void*)0x00896770;
    if (std::memcmp(comboTarget, comboExpected, sizeof(comboExpected)) == 0 &&
        MH_CreateHook(comboTarget, (void*)HookCombo, (void**)&s_Combo) == MH_OK) {
        g_NormalGateReady = MH_EnableHook(comboTarget) == MH_OK;
        if (!g_NormalGateReady) { MH_RemoveHook(comboTarget); s_Combo = nullptr; }
    }
    static const unsigned char expected[] = {0x55,0x8B,0xEC,0x56,0x6A,0x01,0x8B,0xF1};
    void* target = (void*)0x008A1EF0;
    if (std::memcmp(target, expected, sizeof(expected)) != 0) {
        LogTrace("[ATTACK RESTRICTIONS] Native eligibility prologue mismatch; gate disabled.");
        return;
    }
    if (MH_CreateHook(target, (void*)HookEligibility, (void**)&s_Eligibility) == MH_OK) {
        g_SpecialGateReady = MH_EnableHook(target) == MH_OK;
        if (!g_SpecialGateReady) { MH_RemoveHook(target); s_Eligibility = nullptr; }
    }
    static const unsigned char airExpected[] = {0x55,0x8B,0xEC,0x83,0xEC,0x0C,0x56,0x6A,0x01};
    void* airTarget = (void*)0x008A1F80;
    bool airReady = false;
    if (std::memcmp(airTarget, airExpected, sizeof(airExpected)) == 0 &&
        MH_CreateHook(airTarget, (void*)HookAirEligibility, (void**)&s_AirEligibility) == MH_OK) {
        airReady = MH_EnableHook(airTarget) == MH_OK;
        if (!airReady) { MH_RemoveHook(airTarget); s_AirEligibility = nullptr; }
    }
    if (!airReady && g_SpecialGateReady) {
        MH_DisableHook(target); MH_RemoveHook(target); s_Eligibility = nullptr;
    }
    if (!g_SpecialGateReady && airReady) {
        MH_DisableHook(airTarget); MH_RemoveHook(airTarget); s_AirEligibility = nullptr;
    }
    g_SpecialGateReady = g_SpecialGateReady && airReady;
    static const unsigned char distanceExpected[] = {0x55,0x8B,0xEC,0x83,0xEC,0x10,0x53,0x8B,0x5D,0x08};
    void* distanceTarget=(void*)0x008A2050;
    bool distanceReady=false;
    if (std::memcmp(distanceTarget,distanceExpected,sizeof(distanceExpected))==0 &&
        MH_CreateHook(distanceTarget,(void*)HookAirDistance,(void**)&s_AirDistance)==MH_OK) {
        distanceReady=MH_EnableHook(distanceTarget)==MH_OK;
        if (!distanceReady) { MH_RemoveHook(distanceTarget); s_AirDistance=nullptr; }
    }
    g_AirHeightGateReady=g_SpecialGateReady && g_CommandGateReady && distanceReady;
    LogTrace("[ATTACK RESTRICTIONS] Normal gate: %s | Special eligibility gate: %s | Dedicated X-Ray gates: %s",
             g_NormalGateReady ? "READY" : "OFF", g_SpecialGateReady ? "READY" : "OFF",
             g_XRayGateReady ? "READY" : "OFF");
    LogTrace("[MOVE RESTRICTIONS] Shared scripted command query: %s", g_CommandGateReady ? "READY" : "OFF");
    LogTrace("[MOVE RESTRICTIONS] Minimum air-special height: %s",g_AirHeightGateReady ? "READY" : "OFF");

}
void RenderUI() {
    Settings s = GetSettings();
    bool changed = false;
    ImGui::TextUnformatted("Target Scope:"); ImGui::SameLine();
    changed |= ImGui::RadioButton("Both Players##attack_scope", &s.scope, 0); ImGui::SameLine();
    changed |= ImGui::RadioButton("Player 1 Only##attack_scope", &s.scope, 1); ImGui::SameLine();
    changed |= ImGui::RadioButton("Player 2 Only##attack_scope", &s.scope, 2);
    ImGui::Spacing();
    ImGui::BeginDisabled(!g_NormalGateReady);
    changed |= ImGui::Checkbox("Disable Dial-A-Combos", &s.disableStrings);
    ImGui::TextDisabled("Allow opening normals; block additional normal string inputs.");
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!g_SpecialGateReady);
    changed |= ImGui::Checkbox("Disable Special Moves", &s.disableSpecials);
    ImGui::TextDisabled("Regular specials only. Enhanced specials have their own toggle.");
    changed |= ImGui::Checkbox("Disable EX Special Moves", &s.disableEnhanced);
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!g_SpecialGateReady || !g_XRayGateReady);
    changed |= ImGui::Checkbox("Disable X-Ray Moves", &s.disableXRay);
    ImGui::EndDisabled();
    ImGui::Spacing();
    ImGui::TextUnformatted("Normal Cancel Restrictions:");
    ImGui::TextDisabled("Allow these moves from neutral; prevent cancelling normals into them.");
    ImGui::BeginDisabled(!g_SpecialGateReady || !g_CommandGateReady);
    changed |= ImGui::Checkbox("Disable Normal Cancels into Special Moves", &s.disableNormalToSpecial);
    changed |= ImGui::Checkbox("Disable Normal Cancels into EX Special Moves", &s.disableNormalToEnhanced);
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!g_SpecialGateReady || !g_XRayGateReady || !g_CommandGateReady);
    changed |= ImGui::Checkbox("Disable Normal Cancels into X-Ray Moves", &s.disableNormalToXRay);
    ImGui::EndDisabled();
    ImGui::Spacing();
    ImGui::BeginDisabled(!g_AirHeightGateReady);
    changed |= ImGui::Checkbox("Restrict Air Specials Below Minimum Height", &s.limitAirSpecialHeight);
    changed |= ImGui::SliderFloat("Minimum Air Special Height", &s.minimumAirSpecialHeight,0.0f,300.0f,"%.1f native units above floor");
    ImGui::TextDisabled("Regular and EX air specials only. Grounded specials and X-Rays keep their existing rules.");
    ImGui::TextDisabled("Checked before move startup; buffered inputs may execute once high enough.");
    for (int i=0; i<2; ++i) {
        if (s_HeightReady[i].load(std::memory_order_relaxed))
            ImGui::Text("P%d height: %.1f | Peak (current / last jump): %.1f native units",i+1,
                (i==0 ? g_P1AirHeight : g_P2AirHeight).load(std::memory_order_relaxed),
                s_PeakHeight[i].load(std::memory_order_relaxed));
        else ImGui::TextDisabled("P%d height / peak: waiting for grounded reference",i+1);
        const float standing=s_StandingHeight[i].load(std::memory_order_relaxed);
        if (standing>0) ImGui::Text("P%d standing hurtbox height: %.1f native units",i+1,standing);
        else ImGui::TextDisabled("P%d standing hurtbox height: waiting for standing idle",i+1);
    }
    ImGui::Text("Low air-move evaluations rejected: %u | Last height: %.1f",g_AirHeightBlocks,g_LastRejectedAirHeight.load());
    ImGui::EndDisabled();
    if (!g_NormalGateReady || !g_SpecialGateReady || !g_XRayGateReady) ImGui::TextDisabled("Some restrictions are unavailable: native gate not ready.");
    if (changed) SetSettings(s);
}
} }
