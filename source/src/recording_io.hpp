#pragma once
#include <cstdio>
#include <cstdint>
#include <utility>
#include <windows.h>
#include <cstring>

namespace MK9TradeFix { namespace RecordingIO {
    // Parse into a temporary slot. A failed import must preserve the destination.
    template<class Slot>
    bool Read(FILE* file, Slot& destination, uint32_t maxFrames) {
        uint32_t magic = 0, count = 0;
        if (!file || fread(&magic, sizeof(magic), 1, file) != 1 ||
            (magic != 0x31524B4D && magic != 0x32524B4D)) return false;
        Slot next;
        next.id = destination.id;
        if (fread(next.name, 1, 32, file) != 32) return false;
        next.name[31] = '\0';
        if (magic == 0x32524B4D && fread(next.characterName, 1, 32, file) != 32) return false;
        next.characterName[31] = '\0';
        if (fread(&count, sizeof(count), 1, file) != 1 || count > maxFrames) return false;
        next.frames.resize(count);
        if (count && fread(next.frames.data(), sizeof(next.frames[0]), count, file) != count) return false;
        if (ferror(file)) return false;
        next.totalFrames = count;
        next.isEmpty = (count == 0);
        destination = std::move(next);
        return true;
    }

    template<class Slot>
    bool Write(FILE* file, const Slot& slot, uint32_t maxFrames) {
        if (!file || slot.isEmpty || !slot.totalFrames || slot.totalFrames > maxFrames ||
            slot.totalFrames != slot.frames.size()) return false;
        const uint32_t magic = 0x32524B4D;
        return fwrite(&magic, sizeof(magic), 1, file) == 1 &&
            fwrite(slot.name, 1, 32, file) == 32 &&
            fwrite(slot.characterName, 1, 32, file) == 32 &&
            fwrite(&slot.totalFrames, sizeof(slot.totalFrames), 1, file) == 1 &&
            fwrite(slot.frames.data(), sizeof(slot.frames[0]), slot.totalFrames, file) == slot.totalFrames &&
            fflush(file) == 0 && !ferror(file);
    }

    template<class Slot>
    bool Export(const char* path, const Slot& slot, uint32_t maxFrames) {
        if (!path || slot.isEmpty || !slot.totalFrames || slot.totalFrames > maxFrames ||
            slot.totalFrames != slot.frames.size()) return false;
        char fullPath[MAX_PATH], parent[MAX_PATH], temporary[MAX_PATH];
        char* name = nullptr;
        const DWORD length = GetFullPathNameA(path, MAX_PATH, fullPath, &name);
        if (!length || length >= MAX_PATH || !name || name == fullPath) return false;
        const size_t parentLength = static_cast<size_t>(name - fullPath);
        memcpy(parent, fullPath, parentLength);
        parent[parentLength] = '\0';
        if (!GetTempFileNameA(parent, "mkr", 0, temporary)) return false;
        FILE* file = nullptr;
        bool ok = fopen_s(&file, temporary, "wb") == 0 && file;
        if (ok) {
            ok = Write(file, slot, maxFrames);
            if (fclose(file) != 0) ok = false;
        }
        if (ok) ok = MoveFileExA(temporary, fullPath, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
        if (!ok) DeleteFileA(temporary);
        return ok;
    }
}}
