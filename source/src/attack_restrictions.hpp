#pragma once
#include <cstdint>
#include <cstring>
#include <cmath>
#include <atomic>
#include <limits>
namespace MK9TradeFix { namespace AttackRestrictions {
enum class MoveKind { Other, Special, Enhanced, XRay };
struct Settings {
    bool disableStrings = false;
    bool disableSpecials = false;
    bool disableEnhanced = false;
    bool disableXRay = false;
    bool disableNormalToSpecial = false;
    bool disableNormalToEnhanced = false;
    bool disableNormalToXRay = false;
    bool limitAirSpecialHeight = false;
    float minimumAirSpecialHeight = 50.0f;
    int scope = 0; // Both, P1, P2.
};
// Native EvaluateSpecialMoves dispatches descriptor+70 with these categories.
// Live script identities: 8=p_*_supermove; 3=fatality, 10=babality.
// Tag/assists (6/7) and finishers are outside these restrictions.
inline MoveKind KindFromNativeType(uint32_t type) {
    switch (type) {
    case 0: case 1: case 2: return MoveKind::Special;
    case 4: case 5: return MoveKind::Enhanced;
    case 8: case 9: return MoveKind::XRay;
    default: return MoveKind::Other;
    }
}
inline MoveKind KindFromCommandFunction(const char* name) {
    if (!name) return MoveKind::Other;
    if (std::strcmp(name,"generic_2in1_check")==0 ||
        std::strcmp(name,"generic_air_2in1_check")==0 ||
        std::strcmp(name,"generic_wakeup_2in1_check")==0) return MoveKind::Special;
    if (std::strcmp(name,"generic_EX_2in1_check")==0 ||
        std::strcmp(name,"generic_air_EX_2in1_check")==0 ||
        std::strcmp(name,"generic_wakeup_EX_2in1_check")==0) return MoveKind::Enhanced;
    if (std::strcmp(name,"generic_Xray_2in1_check")==0 ||
        std::strcmp(name,"generic_air_Xray_2in1_check")==0) return MoveKind::XRay;
    return MoveKind::Other;
}
inline bool Applies(int scope, int player) {
    return (player == 1 || player == 2) && (scope == 0 || scope == player);
}
inline float ClampAirHeight(float height) {
    if (!std::isfinite(height)) return 50.0f;
    return height < 0 ? 0 : (height > 300 ? 300 : height);
}
inline float HeightAboveFloor(float z, float floor) {
    if (!std::isfinite(z) || !std::isfinite(floor)) return std::numeric_limits<float>::quiet_NaN();
    return z > floor ? z-floor : 0;
}
inline bool IsAirCommandFunction(const char* name) {
    return name && (std::strcmp(name,"generic_air_2in1_check")==0 ||
                    std::strcmp(name,"generic_air_EX_2in1_check")==0);
}
inline bool IsNativeAirSpecial(uint32_t type) { return type == 1 || type == 2 || type == 5; }
inline bool RejectAirHeight(const Settings& s, MoveKind kind, int player,
                            bool airMove, float height, bool validHeight) {
    return s.limitAirSpecialHeight && Applies(s.scope, player) && airMove && validHeight &&
        (kind == MoveKind::Special || kind == MoveKind::Enhanced) && std::isfinite(height) &&
        height < ClampAirHeight(s.minimumAirSpecialHeight);
}
inline bool IsNativeNormal(uint32_t action, uint32_t flags, bool normalScript = false,
                           bool executingCancelHelper = false) {
    // Native neutral/hurt/movement state wins over retained normal flags.
    if ((action >= 0x4000 && action <= 0x4FFF) ||
        (action >= 0x6000 && action <= 0x62FF) || action == 0x5208) return false;
    return (normalScript && executingCancelHelper) ||
        (action >= 0x5000 && action <= 0x5FFF && ((flags & 0x400) != 0 || normalScript));
}
inline bool IsNormalScriptName(const char* name) {
    return name && name[0]=='a' && name[1]=='t' && name[2]=='t' && name[3]=='a' &&
        name[4]=='c' && name[5]=='k' && name[6]>='1' && name[6]<='4';
}
inline bool RejectCancel(const Settings& s, MoveKind kind, int player, bool inNormal) {
    return inNormal && Applies(s.scope, player) &&
        ((kind == MoveKind::Special && s.disableNormalToSpecial) ||
         (kind == MoveKind::Enhanced && s.disableNormalToEnhanced) ||
         (kind == MoveKind::XRay && s.disableNormalToXRay));
}
inline bool Reject(const Settings& s, MoveKind kind, int player, bool inNormal = false) {
    if (!Applies(s.scope, player)) return false;
    return (kind == MoveKind::Special && s.disableSpecials) ||
           (kind == MoveKind::Enhanced && s.disableEnhanced) ||
           (kind == MoveKind::XRay && s.disableXRay) || RejectCancel(s, kind, player, inNormal);
}
inline bool RejectComboQuery(const Settings& s, int player, uint32_t prefixLength) {
    return s.disableStrings && Applies(s.scope, player) && prefixLength > 0 && prefixLength <= 5;
}
Settings GetSettings();
void SetSettings(const Settings& settings);
void Initialize();
void RenderUI();
void OnFrameTick(void* p1, void* p2);
extern bool g_AirHeightGateReady;
extern volatile unsigned g_AirHeightBlocks;
extern std::atomic<float> g_P1AirHeight, g_P2AirHeight, g_LastRejectedAirHeight;
extern bool g_CommandGateReady;
extern volatile unsigned g_CommandQueryBlocks;
extern volatile unsigned g_AlternateXRayQueryBlocks;
extern bool g_NormalGateReady, g_SpecialGateReady, g_XRayGateReady;
extern volatile unsigned g_XRayDirectBlocks;
extern volatile unsigned g_SpecialCancelBlocks, g_EnhancedCancelBlocks, g_XRayCancelBlocks;
extern volatile unsigned g_NormalBlocks, g_SpecialBlocks, g_EnhancedBlocks, g_XRayBlocks;
} }
