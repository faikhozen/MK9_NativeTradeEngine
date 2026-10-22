#pragma once
#include "engine_common.hpp"
#include <atomic>

namespace MK9TradeFix {

    extern std::atomic<bool> g_DrawDebugHitboxes;
    extern std::atomic<bool> g_DrawDebugHurtboxes;
    extern std::atomic<bool> g_DrawDebugPushboxes;

    bool IsDrawDebugHitboxes();
    void SetDrawDebugHitboxes(bool enable);
    bool IsDrawDebugHurtboxes();
    void SetDrawDebugHurtboxes(bool enable);
    bool IsDrawDebugPushboxes();
    void SetDrawDebugPushboxes(bool enable);

    void SafeFlushDebugPrimitives();
    void EnsureLineBatcherVisible();
    bool IsCharacterInVisualActiveFrames(void* pChar);
    void RenderDebugCollisionPrimitives(void* pChar, int playerIdx);
}
