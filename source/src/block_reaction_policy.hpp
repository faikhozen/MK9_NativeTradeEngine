#pragma once
#include <cstdint>
#include <cstring>
namespace MK9TradeFix { namespace BlockReaction {
inline bool IsReactionFunction(const char* name) {
    return name && (std::strcmp(name, "block_common_reaction") == 0 ||
                    std::strncmp(name, "r_block_hit_", 12) == 0);
}
inline bool IsExplicitReactionAction(uint32_t action) {
    return action == 0x4F00 || (action >= 0x0A02 && action <= 0x0A0F);
}
inline bool IsLocked(bool scriptReaction, uint32_t action, uint64_t elapsed) {
    // Cover the impact-to-script handoff; idle guard itself is never evidence.
    if (elapsed == 1) return true;
    // Native exit helpers can remain nested under the reaction after neutral
    // or an accepted movement/attack has already begun.
    if (action == 0 || action == 0x0100 || action == 0x0101 || action == 0x0900 ||
        action == 0x0A00 || (action >= 0x2000 && action <= 0x201F) ||
        (action >= 0x5000 && action <= 0x5FFF)) return false;
    return scriptReaction || IsExplicitReactionAction(action);
}
} }

