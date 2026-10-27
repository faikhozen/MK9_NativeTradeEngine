#include "profiler.hpp"
#include <string.h>
#include <mutex>
#include <atomic>
#include <cmath>

static SubsystemMetric g_SubsystemMetrics[PROF_TOTAL_COUNT] = {
    { "Input & Buffer Hooks", "Negative edge interception, crouch buffer lookback & move eval", 0.0, 0.0, 0.0, 0 },
    { "Physical Trade Engine", "Inclusive hit callback time, including native engine work", 0.0, 0.0, 0.0, 0 },
    { "Meter Drain & Armor Fix", "Frame-1 EX armor injection, action protection & meter refund", 0.0, 0.0, 0.0, 0 },
    { "Frame Data Timeline HUD", "Hitstun, blockstun, active frame tracking & advantage math", 0.0, 0.0, 0.0, 0 },
    { "3D Hitbox Visualizer", "DirectX 9 primitive batching for hurtboxes, hitboxes & pushboxes", 0.0, 0.0, 0.0, 0 },
    { "ImGui Overlay & Console", "Direct3D 9 ImGui frame rendering, console log & diagnostics UI", 0.0, 0.0, 0.0, 0 }
};

namespace ModProfiler {
    static std::atomic<bool> g_ProfilerEnabled{false};
    static std::mutex s_MetricsMutex;

    int64_t GetFrequency() {
        static const int64_t frequency = []() {
            LARGE_INTEGER value = {};
            return QueryPerformanceFrequency(&value) ? value.QuadPart : 0;
        }();
        return frequency;
    }

    void Init() {
        GetFrequency();
    }

    void ResetPeaks() {
        std::lock_guard<std::mutex> lock(s_MetricsMutex);
        for (int i = 0; i < PROF_TOTAL_COUNT; ++i) {
            g_SubsystemMetrics[i].peakUs = 0.0;
            g_SubsystemMetrics[i].callCount = 0;
            g_SubsystemMetrics[i].currentUs = 0.0;
            g_SubsystemMetrics[i].avgUs = 0.0;
        }
    }

    SubsystemMetric* GetMetrics() {
        // Callers receive a per-thread snapshot, never the concurrently written table.
        thread_local SubsystemMetric snapshot[PROF_TOTAL_COUNT];
        std::lock_guard<std::mutex> lock(s_MetricsMutex);
        memcpy(snapshot, g_SubsystemMetrics, sizeof(snapshot));
        return snapshot;
    }

    int GetCount() {
        return PROF_TOTAL_COUNT;
    }

    double GetTotalAvgUs() {
        std::lock_guard<std::mutex> lock(s_MetricsMutex);
        double total = 0.0;
        for (int i = 0; i < PROF_TOTAL_COUNT; ++i) {
            total += g_SubsystemMetrics[i].avgUs;
        }
        return total;
    }

    double GetTotalPeakUs() {
        std::lock_guard<std::mutex> lock(s_MetricsMutex);
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
        if (!g_ProfilerEnabled || index < 0 || index >= PROF_TOTAL_COUNT || !std::isfinite(elapsedUs) || elapsedUs < 0) return;
        std::lock_guard<std::mutex> lock(s_MetricsMutex);
        
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


