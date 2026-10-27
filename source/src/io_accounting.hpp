#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <cstddef>

namespace MK9TradeFix { namespace LoadProfiler {
    struct PackageRecord {
        std::wstring filename;
        uint64_t fileSizeBytes;
        uint64_t bytesRead;
        double readDurationSec;
    };
    struct ActiveFileHandle {
        std::wstring filename;
        uint64_t fileSizeBytes;
        uint64_t bytesRead;
        double readDurationSec;
        uint64_t openedQpc;
        size_t packageIndex = static_cast<size_t>(-1);
        uint64_t epoch = 0;
        bool startup = false;
        uint64_t fileIdentity = 0;
        uint32_t volume = 0;
        bool identityValid = false;
    };
    // Caller holds the accounting lock. Epochs prevent a stale handle from
    // attributing reads to a later match that reused the same package index.
    inline void AttributeRead(ActiveFileHandle& handle, uint64_t bytes, double seconds,
            uint64_t currentEpoch, std::vector<PackageRecord>& startup, std::vector<PackageRecord>& match) {
        handle.bytesRead += bytes;
        handle.readDurationSec += seconds;
        if (!handle.startup && handle.epoch != currentEpoch) return;
        auto& records = handle.startup ? startup : match;
        if (handle.packageIndex >= records.size()) return;
        auto& record = records[handle.packageIndex];
        record.bytesRead += bytes;
        record.readDurationSec += seconds;
    }
}}
