#pragma once

namespace MK9TradeFix { namespace KeyboardResetPolicy {
// Physical Set-1 scan codes: top-row 4 is 05, keypad 4 is 4B.
// Codes returned are native position override + 1 (zero means no request).
struct Input {
    bool directions[2][3] = {}; // left, right, down
    bool held[2] = {};
    void Focus(bool p1Held, bool p2Held) {
        for (auto& player : directions) for (bool& key : player) key=false;
        held[0]=p1Held; held[1]=p2Held;
    }
    int Key(unsigned vk, unsigned scan, bool extended, bool down, bool repeat, bool allowed) {
        int player=-1, direction=-1;
        if (!extended) {
            if (scan==0x1E) { player=0; direction=0; }
            if (scan==0x20) { player=0; direction=1; }
            if (scan==0x1F) { player=0; direction=2; }
        } else {
            if (scan==0x4B) { player=1; direction=0; }
            if (scan==0x4D) { player=1; direction=1; }
            if (scan==0x50) { player=1; direction=2; }
        }
        if (player>=0) { directions[player][direction]=down; return 0; }
        player=(!extended && vk=='4' && scan==0x05) ? 0 :
               ((!extended && vk==0x6D && scan==0x4A) ? 1 : -1);
        if (player<0) return 0;
        const bool wasHeld=held[player]; held[player]=down;
        if (!down || wasHeld || repeat || !allowed) return 0;
        const auto& d=directions[player];
        if (d[0] && d[1]) return 0; // Ambiguous directions never choose a corner.
        if (d[0]) return d[2] ? 4 : 3;
        if (d[1]) return d[2] ? 6 : 5;
        return d[2] ? 2 : 1;
    }
};
} }
