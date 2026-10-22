#include "move_priority.hpp"

namespace MK9TradeFix {
namespace MovePriority {

    // All priority options are OFF (false) by default
    bool g_UppercutAntiAir      = false;
    bool g_UppercutVsSpecial    = false;
    bool g_UppercutVsUppercut   = false;
    bool g_JumpInPriority       = false;
    bool g_XRayAbsolutePriority = false;
    bool g_InstantProjectiles   = false;

    void SetAll(bool state) {
        g_UppercutAntiAir      = state;
        g_UppercutVsSpecial    = state;
        g_UppercutVsUppercut   = state;
        g_JumpInPriority       = state;
        g_XRayAbsolutePriority = state;
        g_InstantProjectiles   = state;
    }

    void ResetToDefaults() {
        SetAll(false);
    }

} // namespace MovePriority
} // namespace MK9TradeFix
