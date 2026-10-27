#pragma once
#include <cstdint>

namespace MK9TradeFix {
namespace MovePriority {

    // ------------------------------------------------------------------------
    // Move Priority Hierarchy Options (All default to OFF / false)
    // ------------------------------------------------------------------------

    // 1. Uppercut (D+2) Anti-Air Priority: D+2 cleanly beats incoming Jump-In attacks during active anti-air frames.
    extern bool g_UppercutAntiAir;

    // 2. Uppercut vs Special: D+2 cleanly overpowers grounded special moves during active clash frames.
    extern bool g_UppercutVsSpecial;

    // 3. Uppercut vs Uppercut: Later inputted D+2 wins by default.
    extern bool g_UppercutVsUppercut;

    // 4. Jump-In Attacks: Jump-ins trade or cleanly beat grounded standing normals during active falling frames.
    extern bool g_JumpInPriority;

    // 5. X-Ray Absolute Priority: Cinematic X-Ray super moves cleanly overpower all incoming normal and special strikes.
    extern bool g_XRayAbsolutePriority;

    // 6. Instant Projectile Impacts: Projectiles deal immediate damage/reactions on contact without artificial holding or passthrough glitches.
    extern bool g_InstantProjectiles;

    // Configuration utilities
    void SetAll(bool state);
    void ResetToDefaults();

} // namespace MovePriority
} // namespace MK9TradeFix
