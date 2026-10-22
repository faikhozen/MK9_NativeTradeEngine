#pragma once
#include <cstdint>

namespace MK9TradeFix {
    namespace CrouchBufferFix {
        // Subsystem state
        void SetEnabled(bool enabled);
        bool IsEnabled();
        void Toggle();

        // Resolves 8-frame directional buffer expiration (Mode 9 F8)
        // Remaps neutral attack tokens to native crouch normal tokens when crouched or holding Down
        uint32_t RemapCrouchAttackToken(void* pChar, uint32_t dirMotion, uint32_t button);
    }
}

