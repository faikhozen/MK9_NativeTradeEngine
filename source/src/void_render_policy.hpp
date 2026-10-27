#pragma once
#include <cstdint>
namespace MK9TradeFix { namespace VoidRender {
inline bool SceneTarget(uint32_t width,uint32_t height,uint32_t backWidth,uint32_t backHeight,
                        bool cube,bool colorFormat) {
    return colorFormat && !cube && width>0 && height>0 && width==backWidth && height==backHeight;
}
// Lighting/morph state does not revoke a user's active Void Mode selection.
inline bool Active(bool enabled,bool matchLoaded) { return enabled && matchLoaded; }
} }
