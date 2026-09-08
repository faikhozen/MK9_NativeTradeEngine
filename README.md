# MK9 Native Physical Trade Engine Mod (v0.6701a)
**Author:** Fai Khozen  
**Target:** Mortal Kombat 9 (Komplete Edition) - PC (Steam / DiscContentPC)  
**Support / Donate:** [Ko-fi](https://ko-fi.com/faikhozen) (https://ko-fi.com/faikhozen)

> [!NOTE]
> **AI Assistance Disclosure:** This project was developed and reverse-engineered with the assistance of AI pair-programming tools for low-level memory analysis, disassembly tracing, and native hook orchestration.


<<<<<<< Updated upstream
Donations are pretty much appreciated  
https://www.paypal.com/paypalme/faikhozen

=======
## Overview

This mod restores authentic physical strike trades, weapon clashing, and low-level priority resolution in Mortal Kombat 9 (Komplete Edition) on PC.

In vanilla MK9, when two players attack simultaneously, the engine's internal anti-trade system (Stage 5) arbitrarily kills the second player's attack thread on frame 1, making true trades impossible and causing one-sided interrupts.

This mod hooks the native combat pipeline directly to enable natural fighting game trading, full Type A/B weapon prop collision, authoritative move priority rules, live frame data tools, and essential quality-of-life engine fixes.
>>>>>>> Stashed changes


<<<<<<< Updated upstream

someone complained that I used AI for this and suggested that I should "PRETEND" to at least to not use AI.

why should i hide that fact? its already obvious
=======
## Core Features & Combat Mechanics

### 1. Physical Trade Engine & Clash Resolution
* **Limb vs Limb Trades:** Simultaneous jabs, pokes, kicks, and normals connect and trade damage/reactions on active frames.
* **50/50 Special Move Clashes:** When two physical specials collide during active frames (e.g., Sub-Zero slide vs Scorpion teleport punch), both connect with winner immunity protection to prevent broken double-hits.
* **Trade-Ins & Counter Hits:** Natural counter-hit advantages when striking an opponent during their startup windup.

### 2. Weapon Prop Clashing (Type A & Type B)
* **Type A Props (Rigged Skeletal Weapons):** Full 3D collision clashing for direct bone-attached weapons (Scorpion swords, Kitana steel fans, Baraka arm blades, Sonya batons, Cyber Sub-Zero swords).
* **Type B Props (Spawned Child Entities):** Real-time tracking and hitbox clashing for detached/attached weapon entities (Jade bo staff, Nightwolf tomahawks, Kenshi spirit sword, Mileena sai).
* **Weapon vs Limb Clashing:** Authentic physical interactions when striking weapons against limbs or weapons against weapons.

### 3. Move Priority Hierarchy
* **Uppercut (D+2) Anti-Air Priority:** D+2 cleanly beats incoming Jump-In attacks during active anti-air frames.
* **Uppercut vs Special:** D+2 cleanly overpowers grounded special moves during active clash frames.
* **Uppercut vs Uppercut:** Later inputted D+2 wins by default.
* **Jump-In Attacks:** Jump-ins trade or cleanly beat grounded standing normals during active falling frames.
* **X-Ray Absolute Priority:** Cinematic X-Ray super moves cleanly overpower all incoming normal and special strikes.
* **Instant Projectile Impacts:** Projectiles deal immediate damage/reactions on contact without artificial holding or passthrough glitches.

### 4. 3D Hitbox & Collision Visualizer (DirectX 9 Hook)
* **Red Volumes:** Active physical striking hitboxes (limbs, Type A weapons, Type B props).
* **Green Volumes:** Full skeletal hurtboxes and body collision cylinders.
* **Cyan Volumes:** Ground and aerial positional pushboxes.
* Toggleable in-game via **F10** with safe DX9 primitive flushing.

### 5. Live Frame Data Timeline & Advantage Display
* Toggleable in-game HUD via **F9**.
* Real-time frame timeline breakdown: **Startup (Blue)**, **Active (Red)**, **Recovery (Yellow)**, **Hitstun (Purple)**.
* Real-time frame advantage readout (+/- on hit or on block).

### 6. Simulation Controls (Pause & Frame-Step)
* **Pause / Freeze Game Simulation:** Press **Backslash (\)** to instantly freeze the simulation match state.
* **Step 1 Frame Forward:** Press **Equals (=)** while paused to advance the match exactly one tick at a time--ideal for inspecting hitboxes, startup frames, and trades.

### 7. Video Modes & Quality of Life
* **Video Modes (Borderless, Windowed, Fullscreen):**
  * **Borderless Windowed:** Runs at full desktop resolution without borders, allowing instant Alt+Tab multitasking without black screens or crashes.
  * **Windowed:** Standard resizable windowed mode.
  * **Fullscreen:** Traditional exclusive fullscreen mode.
  * *Switchable anytime in real-time from the in-game console menu (~ / F12 -> Display Mode).*
* **Skip Intro / Instant Title Screen:** Bypasses WB and NetherRealm intro Bink movies directly to the Title Screen.
* **R6025 Pure Virtual Call Fix:** Neutralizes uninitialized audio vtables to prevent random R6025 runtime crashes.
* **Havok Stability Armor:** Validates memory page commit status before animation evaluations, eliminating Havok worker thread access violations.
>>>>>>> Stashed changes


<<<<<<< Updated upstream
TODO:

Props Trade
=======
## Controls & Hotkeys

### General & Overlay Controls
| Hotkey | Action |
| :--- | :--- |
| **~ (Tilde)** or **F12** | Open / Close Live In-Game ImGui Console |
| **\ (Backslash)** | Pause / Freeze Game Simulation |
| **= (Equals)** | Step 1 Frame Forward (while paused) |
| **F9** | Toggle Real-Time Frame Data Visualizer HUD |
| **F10** | Toggle 3D Hitbox / Hurtbox / Pushbox Visualizers |
| **F11** | Cycle Diagnostic Test Modes (Mode 1 to Mode 7) |

### Diagnostic Macro Test Suite (In Practice Mode)
* **Mode 1: Normal Timing Suite**
  * F1: Simultaneous FP (0f vs 0f trade)
  * F2: P1 Delayed FP (+1 frame delay on P1)
  * F3: P2 Delayed FP (+1 frame delay on P2)
  * F4: P1 Delayed FP (+2 frame delay on P1)
  * F5: P2 Delayed FP (+2 frame delay on P2)
  * F8: Simultaneous D+FK (S+G vs Down+Num4)
* **Mode 2: Weapon Clashing Suite**
  * F1: Simultaneous b+FP (A+T vs Right+Num7)
  * F2: Simultaneous b+BP (A+U vs Right+Num9)
  * F3: P1 b+BP vs P2 Delayed FP (+1f)
  * F4: P1 b+FP vs P2 Delayed FP (+1f)
  * F7: P1 f+FP vs P2 BP Simultaneous
  * F8: P1 f+BP vs P2 FP Simultaneous
* **Modes 3–6: Projectile Diagnostic Suites**
  * Test simultaneous projectile releases across D,B,1..4, D,F,1..4, and U,D,1..4.
* **Mode 7: Matchup Priority Suite (Scorpion vs Sub-Zero)**
  * F1: Sub-Zero D,F+3 vs Scorpion D+1 on Startup (Scorpion interrupts)
  * F2: Sub-Zero D,F+3 vs Scorpion D+1 on Active (Sub-Zero wins)
  * F3: Scorpion D,B+4 vs Sub-Zero D+1 on Startup (Sub-Zero interrupts)
  * F4: Scorpion D,B+4 vs Sub-Zero D+1 on Active (Scorpion wins)
  * F5: Sub-Zero D,F+3 vs Scorpion D+2 Uppercut (D+2 Uppercut wins)
  * F6: Jump-In vs D+2 Anti-Air Uppercut (D+2 wins)
  * F7: Jump-In vs Standing Normal 1 (Jump-in wins)
  * F8: Both do D+2 Uppercut (Later inputted D+2 wins)
>>>>>>> Stashed changes

-right now mod is new and buggy trade on props will have them float critically on  Jade, will disable her Staff if not acted upon quickly during match with this mod

<<<<<<< Updated upstream
--will try to find a solution for this but because of Jade fall back to no trade if props are in the context of things

--startup interrupt limb normal vs prop (prop win)
=======
## Installation

1. Copy dinput8.dll into your MK9 game directory:
   `	ext
   Steam\steamapps\common\MortalKombat_KompleteEdition\DiscContentPC\
   `
2. Launch Mortal Kombat Komplete Edition normally through Steam.
3. To uninstall, simply delete or rename dinput8.dll.
>>>>>>> Stashed changes

--prop vs prop same  matching startup (50/50)

<<<<<<< Updated upstream
--prop vs  prop startup delayed (delayed win)



I just want to fix the bugs and nuances of vanilla mk9


TODO but hassle:
Frame Data
HurtBox and Hitbox detection


## 📥 Installation Guide 

### Step 1: Copy the File
Copy the file **`dinput8.dll`** into your Mortal Kombat Komplete Edition game folder:

> **Default Steam Path:**  
> `V:\SteamLibrary\steamapps\common\MortalKombat_KompleteEdition\DiscContentPC\`  
> *(or wherever your Steam library is located: `C:\Program Files (x86)\Steam\steamapps\common\...`)*



---

## 🗑️ How to Uninstall
Simply delete `dinput8.dll` from your `DiscContentPC` folder. Your game will return 100% to its original vanilla state.

---

## 📜 Credits 
* **Author:** Fai Khozen and lord savior Chet Jeepeet
* **Engine Architecture:** For full technical disassembly and reverse engineering documentation, see [`docs/MK9_TRADE_ENGINE_DECONSTRUCTION.md`](file:///c:/Users/casan/Documents/github/MK9_RE/docs/MK9_TRADE_ENGINE_DECONSTRUCTION.md).
=======
## TO DO / Roadmap

- [ ] More vanilla MK9 bug fixes & legacy crash remedies
- [ ] negative edge removal
- [ ] In-game configuration & customization menu (hotkey remapping, visualizer styling)
- [ ] Frame meter gain calibration on attack hit/block
- [ ] EZ Toasty option meter gain
- [ ] Breaker meter allowance adjustments & tuning
>>>>>>> Stashed changes
