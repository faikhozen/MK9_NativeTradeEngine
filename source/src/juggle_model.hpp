#pragma once
#include <cmath>
#include <cstdint>
namespace MK9TradeFix { namespace JuggleDeterioration {
inline bool TargetsPlayer(int scope, int player) { return player >= 0 && player < 2 && (scope == 0 || scope == player + 1); }
inline bool IsJuggleReaction(uint32_t action) { return action >= 0x4000 && action < 0x5000 && action != 0x4205 && action != 0x4206; }
inline float EffectiveMotionScale(bool custom, float base, bool perHit, float delta, int hits) {
    float scale = custom && std::isfinite(base) ? base : 1.0f;
    if (perHit && std::isfinite(delta) && delta > 0 && hits > 0) scale -= delta * static_cast<float>(hits);
    return scale < 0.25f ? 0.25f : (scale > 2.0f ? 2.0f : scale);
}
struct BallisticParameters { float gravity, velocity, origin; };
inline float HeightAt(const BallisticParameters& p, float t) { return p.origin+p.velocity*t+0.5f*p.gravity*t*t; }
inline float VelocityAt(const BallisticParameters& p, float t) { return p.velocity+p.gravity*t; }
struct LaunchGravityState {
    bool active=false; float authored=0, applied=0;
    float Begin(float incoming, float scale) {
        const float source=active && incoming==applied ? authored : incoming;
        authored=source;
        return source/scale;
    }
    void Finish(float normalized, float scale) { applied=normalized; active=scale!=1; }
};
} }
