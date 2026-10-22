#include "profiler.hpp"
#include <string.h>

static SubsystemMetric g_SubsystemMetrics[PROF_TOTAL_COUNT] = {
    { "Input & Buffer Hooks", "Negative edge interception, crouch buffer lookback & move eval", 0.0, 0.0, 0.0, 0 },
    { "Physical Trade Engine", "Physical collision clash resolution & move priority hierarchy", 0.0, 0.0, 0.0, 0 },
    { "Meter Drain & Armor Fix", "Frame-1 EX armor injection, action protection & meter refund", 0.0, 0.0, 0.0, 0 },
    { "Frame Data Timeline HUD", "Hitstun, blockstun, active frame tracking & advantage math", 0.0, 0.0, 0.0, 0 },
    { "3D Hitbox Visualizer", "DirectX 9 primitive batching for hurtboxes, hitboxes & pushboxes", 0.0, 0.0, 0.0, 0 },
    { "ImGui Overlay & Console", "Direct3D 9 ImGui frame rendering, console log & diagnostics UI", 0.0, 0.0, 0.0, 0 }
};

namespace ModProfiler {
    bool g_ProfilerEnabled = true;
    LARGE_INTEGER g_PerfFrequency = { 0 };

    void Init() {
        if (g_PerfFrequency.QuadPart == 0) {
            QueryPerformanceFrequency(&g_PerfFrequency);
        }
    }

    void ResetPeaks() {
        for (int i = 0; i < PROF_TOTAL_COUNT; ++i) {
            g_SubsystemMetrics[i].peakUs = 0.0;
            g_SubsystemMetrics[i].callCount = 0;
            g_SubsystemMetrics[i].currentUs = 0.0;
        }
    }

    SubsystemMetric* GetMetrics() {
        return g_SubsystemMetrics;
    }

    int GetCount() {
        return PROF_TOTAL_COUNT;
    }

    double GetTotalAvgUs() {
        double total = 0.0;
        for (int i = 0; i < PROF_TOTAL_COUNT; ++i) {
            total += g_SubsystemMetrics[i].avgUs;
        }
        return total;
    }

    double GetTotalPeakUs() {
        double total = 0.0;
        for (int i = 0; i < PROF_TOTAL_COUNT; ++i) {
            total += g_SubsystemMetrics[i].peakUs;
        }
        return total;
    }

    bool IsEnabled() {
        return g_ProfilerEnabled;
    }

    void SetEnabled(bool enabled) {
        g_ProfilerEnabled = enabled;
    }

    void RecordSample(int index, double elapsedUs) {
        if (!g_ProfilerEnabled || index < 0 || index >= PROF_TOTAL_COUNT) return;
        
        SubsystemMetric& m = g_SubsystemMetrics[index];
        m.currentUs = elapsedUs;
        if (elapsedUs > m.peakUs) {
            m.peakUs = elapsedUs;
        }
        // Smooth exponential moving average (97% weight on history, 3% on current)
        if (m.callCount == 0) {
            m.avgUs = elapsedUs;
        } else {
            m.avgUs = (m.avgUs * 0.97) + (elapsedUs * 0.03);
        }
        m.callCount++;
    }
}
