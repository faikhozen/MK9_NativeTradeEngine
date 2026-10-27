#pragma once
#include <windows.h>
#include <stdint.h>

enum ProfilerCategory {
    PROF_INPUT_HOOKS = 0,    // Negative Edge, Crouch Buffer & Input Hooks
    PROF_TRADE_ENGINE,       // Physical Trades, Move Priority & Clash Resolution
    PROF_METER_DRAIN_FIX,    // Frame-1 Armor, Action Guard & X-Ray Refund
    PROF_FRAME_DATA,         // Frame Data Timeline, Hitstun & Advantage HUD
    PROF_HITBOX_3D,          // 3D Collision & Hitbox Visualizer (DX9)
    PROF_IMGUI_RENDER,       // ImGui Overlay & Console Rendering
    PROF_TOTAL_COUNT
};

struct SubsystemMetric {
    const char* name;
    const char* description;
    double currentUs;
    double peakUs;
    double avgUs;
    uint64_t callCount;
};

namespace ModProfiler {
    int64_t GetFrequency();

    void Init();
    void ResetPeaks();
    SubsystemMetric* GetMetrics();
    int GetCount();
    double GetTotalAvgUs();
    double GetTotalPeakUs();
    bool IsEnabled();
    void SetEnabled(bool enabled);
    void RecordSample(int index, double elapsedUs);

    __forceinline uint64_t StartTimer() {
        if (!IsEnabled()) return 0;
        LARGE_INTEGER t;
        QueryPerformanceCounter(&t);
        return (uint64_t)t.QuadPart;
    }

    __forceinline double CalculateElapsedUs(uint64_t start) {
        if (start == 0) return 0.0;
        LARGE_INTEGER end;
        QueryPerformanceCounter(&end);
        const int64_t frequency = GetFrequency();
        if (frequency > 0) {
            return (double)((int64_t)end.QuadPart - (int64_t)start) * 1000000.0 / (double)frequency;
        }
        return 0.0;
    }

    __forceinline void EndTimer(int index, uint64_t start) {
        if (start == 0) return;
        RecordSample(index, CalculateElapsedUs(start));
    }
}

#define PROFILE_START() uint64_t _prof_t0 = ModProfiler::StartTimer()
#define PROFILE_END(idx) ModProfiler::EndTimer(idx, _prof_t0)

class ScopedProfiler {
private:
    uint64_t m_Start;
    int m_Index;

public:
    __forceinline ScopedProfiler(int profIndex) : m_Index(profIndex) {
        m_Start = ModProfiler::StartTimer();
    }

    __forceinline ~ScopedProfiler() {
        if (m_Start != 0) {
            ModProfiler::EndTimer(m_Index, m_Start);
        }
    }
};
