#pragma once

#include "engine_common.hpp"
#include "engine_stability.hpp"
#include "display_window.hpp"
#include "frame_data.hpp"
#include "collision_visualizer.hpp"
#include "combat_state.hpp"
#include "trade_engine.hpp"
#include "simulation.hpp"
#include "crouch_buffer_fix.hpp"

namespace MK9TradeFix {
    void Initialize();
    void Shutdown();
    void ToggleFix();
    bool IsFixEnabled();
}
