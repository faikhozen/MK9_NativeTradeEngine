#pragma once
#include <cstdint>

namespace MK9TradeFix {
    inline bool IsDetachedProjectileRecord(uint32_t type) { return type == 19; }
    template<class Tracker>
    bool StartsAttackSegment(const Tracker& tracker, uint32_t package, int32_t function, uint32_t record) {
        return !tracker.inMove || tracker.scriptFunc != function || tracker.scriptPackage != package ||
            (record != 0 && tracker.nativeRecord != 0 && record != tracker.nativeRecord);
    }

    template<class Tracker>
    void ObserveAttackActivity(Tracker& tracker, uint64_t frame, bool active) {
        if (active) {
            if (!tracker.firstActiveFrame) tracker.firstActiveFrame = frame;
            if (!tracker.activeBurstStartFrame || tracker.recoveryStartFrame) tracker.activeBurstStartFrame = frame;
            tracker.recoveryStartFrame = 0; // A later hitbox window supersedes provisional recovery.
            tracker.lastActiveFrame = frame;
            if (tracker.lastCountedActiveFrame != frame) {
                ++tracker.observedActiveFrames;
                tracker.lastCountedActiveFrame = frame;
            }
        } else if (tracker.firstActiveFrame && !tracker.recoveryStartFrame && frame > tracker.lastActiveFrame) {
            tracker.recoveryStartFrame = frame;
        }
    }

    template<class Tracker>
    void FinalizeAttackActivity(Tracker& tracker, uint64_t frame) {
        if (tracker.lastCountedActiveFrame != frame) ObserveAttackActivity(tracker, frame, false);
    }

    template<class Tracker>
    int ObservedActiveFrames(const Tracker& tracker) {
        if (tracker.observedActiveFrames) return static_cast<int>(tracker.observedActiveFrames);
        // Preserve legacy synthetic projectile launch anchors until that path is replaced.
        return tracker.firstActiveFrame && tracker.lastActiveFrame >= tracker.firstActiveFrame ?
            static_cast<int>(tracker.lastActiveFrame - tracker.firstActiveFrame + 1) : 0;
    }
}
