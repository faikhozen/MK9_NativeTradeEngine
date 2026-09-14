# MK9 Native Physical Trade Engine Mod (v0.6704e)
**Author:** Fai Khozen  
**Target:** Mortal Kombat 9 (Komplete Edition) - PC (Steam / DiscContentPC)  
**Support / Donate:** [Ko-fi](https://ko-fi.com/faikhozen) (https://ko-fi.com/faikhozen)  
**Bug Reporting:** [X/Twitter](https://x.com/faikhozen) (https://x.com/faikhozen)

> [!NOTE]
> **AI Assistance Disclosure:** This project was developed and reverse-engineered with the assistance of AI pair-programming tools for low-level memory analysis, disassembly tracing, and native hook orchestration.

---

## What's New in v0.6704e (Changelog Since v0.6704d)

* **[PERFORMANCE] Negative Edge Hook Optimization & Slowdown Elimination:**
  * Completely resolved the gameplay slowdown / FPS drop when Negative Edge hooks were active.
  * Replaced repeated high-frequency SEH player resolution across input hooks (`CheckButtonReleasedTag`, `CheckBufferedMove`, `IsCommandInputSatisfied`, `GetSatisfiedInputButton`, `MatchAndExecuteMove`) with fast direct pointer indexing and zero-overhead early exits, restoring solid 60 FPS performance regardless of toggle state.

* **[FIX] Crouch Buffer State Cleanup (Ducking Bug Fix Fix):**
  * Fixed a stance-locking issue where the Crouch Buffer Fix intermittently caused characters to get stuck in a crouching position while performing attacks.
  * Properly synchronizes stance tokens and state transitions so standing attacks immediately execute standing animations and neutral recovery flows seamlessly without stuck crouch states.

* **[FEATURE] Modular Move Priority Hierarchy in Extras:**
  * Separated all move priority rules into individual checkboxes under `Extras > Move Priority` (MenuBar & Main Tab), all **OFF** by default:
    * `Uppercut (D+2) Anti-Air Priority`: D+2 cleanly beats incoming Jump-In attacks during active anti-air frames.
    * `Uppercut vs Special`: D+2 cleanly overpowers grounded special moves during active clash frames.
    * `Uppercut vs Uppercut`: Later inputted D+2 wins by default.
    * `Jump-In Attacks`: Jump-ins trade or cleanly beat grounded standing normals during active falling frames.
    * `X-Ray Absolute Priority`: Cinematic X-Ray super moves cleanly overpower all incoming normal and special strikes.
    * `Instant Projectile Impacts`: Projectiles deal immediate damage/reactions on contact without artificial holding or passthrough glitches.
  * When all priority options are OFF (default), the Physical Trade Priority Fix (F11) operates in 100% pure physical trade mode where any simultaneous hits trade symmetrically without artificial priority biases.

* **[FEATURE] 720p & Below Monitor Auto-Fullscreen Lock:**
  * Added automatic screen resolution detection for displays at or below 720p (`<= 1280x720` or height `<= 720`).
  * When enabled (`Lock to Fullscreen on 720p & Below Monitor`), windowed and borderless switching are disabled and the game is locked to Exclusive Fullscreen to prevent window clipping or resolution distortion.

* **[FIX] Fullscreen Direct3D Mouse Cursor Support:**
  * Enabled Direct3D software cursor rendering (`io.MouseDrawCursor = g_ConsoleOpen`) and added `WM_SETCURSOR` arrow cursor handling so the mouse cursor is 100% visible and interactive inside Exclusive Fullscreen.

## Previous Highlights (v0.6701a - v0.6704d)

* **[NEW] Frame Data Visualizer & Frame Bar Enhancements:**
  * **Dynamic Hitstun & Blockstun Timeline Segmentation:** The real-time frame bar timeline now dynamically displays state transitions with clean color coding: **Startup (Blue)**, **Active (Red)**, **Recovery (Yellow)**, **Hitstun / Blockstun (Purple)**, and **Dash / Movement (Orange)**.
  * **Precise Frame Timings & Active Digit Overlays:** Displays live duration numbers directly over active frame segments for instant visual measurement of startup windows and active hit frames.
  * **Real-Time Frame Advantage Readout (+/-):** Instant frame advantage calculation displayed on screen (+/- on hit or on block) as soon as moves resolve or connect with opponent guard.
  * **Projectile Advantage Support:** Expanded frame data timeline engine to track projectile travel, active projectile collision windows, and projectile advantage on block/hit.
  * **Visual HUD Polish & Placement Options:** Polished frame bar layout with options to adjust positioning and opacity directly through the in-game console. Toggleable on/off anytime via **F9**.

* **[NEW] Meter Drain Glitch Fix & Frame-1 EX Super Armor Protection:**
  * Fixed the notorious vanilla engine bug where getting struck on the early startup frames of an EX Special move (before native armor activates) causes the player to lose 1 bar of super meter while the move completely drops and fails to execute.
  * Grants immediate Frame-1 Super Armor, neutralizes incoming flinch reactions, and protects the attacker's action from premature abortion, allowing armored special moves to reliably absorb incoming hits without losing meter.
  * Toggleable via the in-game console under **Gameplay Fixes -> Meter Drain Glitch Fix** and header quick-toggle button.

* **[NEW] Crouch Input Buffer Lookback Timeout Fix:**
  * Fixed the vanilla engine bug where holding Down causes directional buffer tokens to expire after 8 frames, causing subsequent attack button presses (1, 2, 3, 4) to resolve as neutral standing attacks rather than low pokes (D+1, D+3, D+4) or uppercuts (D+2).
  * Maintains continuous crouch state for as long as Down is physically held, ensuring 100% reliable low pokes and anti-air uppercuts from extended crouching states.

* **[NEW] Negative Edge (Release Check) Subsystem:**
  * **Tournament Standard (Disabled by Default):** In competitive fighting games, specials executing on button release often cause accidental misfires. This mod defaults Negative Edge to **OFF** for both players.
  * **Independent Per-Player Toggles:** Easily turn Negative Edge on or off for P1 and P2 individually via hotkeys (`Shift + F7` / `Shift + F8`) or the console UI.
  * **Clean Execution:** Intercepts release queries directly without console log spam.
  * **Mode 8 Diagnostic Suite:** Built-in automated macro suite to test and verify press vs release behavior across standard, snap, stagger, and direction-held buffers.

* **[NEW] Toasty Training Mode & Meter Boost System:**
  * Added Toasty Training Mode that forces Dan Forden's "Toasty!" easter egg to trigger 100% of the time on qualifying D+2 uppercuts across all game modes (Versus, Arcade, Training) and stages.
  * Unlocks the authentic NetherRealm 85-frame reward window on uppercuts, enabling full super meter replenishment on secret `Down + Start` input for labbing and combo practice.
  * Includes real-time UI controls and a dynamic 32-character secret input guide in the ImGui console.

* **[NEW] Expanded Diagnostic Test Suites (Modes 8, 9 & 10):**
  * **Mode 8 (Release Check Diagnostics):** 8 automated macros testing raw negative edge, downstrokes, hit-confirm cancels, whiff cancels, and rapid string buffers.
  * **Mode 9 (Live Projectile & Frame Data Telemetry):** Automated test rig for evaluating projectile trajectories, hitbox duration, and frame data interactions.
  * **Mode 10 (EX Forceball -> F+3 Frame Trap vs EX Reversals):** 8-macro diagnostic suite (F1-F8) testing stand blocking vs crouch blocking into EX special reversals across precise frame intervals (Frames 94-103).

* **[NEW] Tabbed ImGui Console UI & Dedicated Gameplay Fixes Menu:**
  * Consolidated all gameplay fixes (Physical Trade Engine, Negative Edge P1/P2, Crouch Buffer Fix, Meter Drain Glitch Fix, and Toasty Training) into a unified, clean management tab in the ImGui console (~).
  * Enhanced stability guards for Havok animation worker threads and DirectX 9 device resets.

* **[FIXES] Visual Glitch when Prop Attack hit startup and recovery frame [prop vanish]**

* **[FIXES] Hang up on exact frame punish on props with limb normals**

* **[FIXES] Fatal crash on EX Special startup hit impact when absorbing attacks with Super Armor**

---

## Overview

This mod restores authentic physical strike trades, weapon clashing, and low-level priority resolution in Mortal Kombat 9 (Komplete Edition) on PC.

In vanilla MK9, when two players attack simultaneously, the engine's internal anti-trade system (Stage 5) arbitrarily kills the second player's attack thread on frame 1, making true trades impossible and causing one-sided interrupts.

This mod hooks the native combat pipeline directly to enable natural fighting game trading, full Type A/B weapon prop collision, authoritative move priority rules, per-player Negative Edge (Release Check) toggles, live frame data tools, and essential quality-of-life engine fixes.

---

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

### 4. Frame-1 EX Super Armor & Meter Drain Protection
* Prevents the vanilla meter drain bug where taking a hit on EX startup drains 1 bar of meter without performing the move.
* Injects Frame-1 Super Armor and guards attacker moves against interruption during early startup.

### 5. Persistent Crouch Input Buffer (No 8-Frame Timeout)
* Eliminates the 8-frame directional lookback decay while holding Down.
* Pokes and uppercuts come out 100% consistently from extended crouching states.

### 6. Negative Edge (Release Check) Subsystem
* **Tournament Standard (Disabled by Default):** In competitive fighting games, specials executing on button release often cause accidental misfires. This mod defaults Negative Edge to **OFF** for both players.
* **Independent Per-Player Toggles:** Easily turn Negative Edge on or off for P1 and P2 individually via hotkeys or the console UI.
* **Zero Log Spam:** Native engine hooks intercept release queries directly without console spam.
* **Mode 8 Diagnostic Suite:** Programmatic verification of press vs release behavior.

### 7. Live Frame Data Timeline & Advantage Display
* Toggleable in-game HUD via **F9** or the console menu.
* Real-time frame timeline breakdown: **Startup (Blue)**, **Active (Red)**, **Recovery (Yellow)**, **Hitstun / Blockstun (Purple)**, **Dash / Movement (Orange)**.
* Real-time frame advantage readout (+/- on hit or on block).
* Active duration numbers displayed directly on timeline segments.

### 8. 3D Hitbox & Collision Visualizer (DirectX 9 Hook)
* **Red Volumes:** Active physical striking hitboxes (limbs, Type A weapons, Type B props).
* **Green Volumes:** Full skeletal hurtboxes and body collision cylinders.
* **Cyan Volumes:** Ground and aerial positional pushboxes.
* Toggleable directly in the in-game console (~) with safe DX9 primitive flushing.

### 9. Simulation Controls (Pause & Frame-Step)
* **Pause / Freeze Game Simulation:** Press **Backslash (\)** to instantly freeze the simulation match state.
* **Step 1 Frame Forward:** Press **Equals (=)** while paused to advance the match exactly one tick at a time--ideal for inspecting hitboxes, startup frames, and trades.

### 10. Video Modes & Quality of Life
* **Video Modes (Borderless, Windowed, Fullscreen):**
  * **Borderless Windowed:** Runs at full desktop resolution without borders, allowing instant Alt+Tab multitasking without black screens or crashes.
  * **Windowed:** Standard resizable windowed mode.
  * **Fullscreen:** Traditional exclusive fullscreen mode.
  * *Switchable anytime via F10 or from the in-game console menu (~ -> Display Mode).*
* **Skip Intro / Instant Title Screen:** Bypasses WB and NetherRealm intro Bink movies directly to the Title Screen.
* **R6025 Pure Virtual Call Fix:** Neutralizes uninitialized audio vtables to prevent random R6025 runtime crashes.
* **Havok Stability Armor:** Validates memory page commit status before animation evaluations, eliminating Havok worker thread access violations.

---

## Controls & Hotkeys

### General & Overlay Controls
| Hotkey | Action |
| :--- | :--- |
| **~ (Tilde)** | Open / Close Live In-Game ImGui Console |
| **\ (Backslash)** | Pause / Freeze Game Simulation |
| **= (Equals)** | Step 1 Frame Forward (while paused) |
| **F9** | Toggle Real-Time Frame Data Visualizer HUD |
| **F10** | Cycle Video Display Mode (Borderless / Windowed / Fullscreen) |
| **F11** | Toggle Native Trade Engine Fix / Vanilla MK9 |
| **Shift + F7** | Toggle Player 1 Negative Edge (Release Check) ON / OFF |
| **Shift + F8** | Toggle Player 2 Negative Edge (Release Check) ON / OFF |

### Diagnostic Macro Test Suites (Practice Mode / Keyboard Controls)
* **Mode 1: Normal Timing Suite** (F1-F5: 0f, +1f, +2f FP trade intervals; F8: Low kick trade)
* **Mode 2: Weapon Clashing Suite** (F1-F8: b+FP, b+BP, f+FP weapon clashes vs normals)
* **Modes 3–6: Projectile Diagnostic Suites** (Simultaneous projectile clash testing)
* **Mode 7: Matchup Priority Suite (Scorpion vs Sub-Zero)** (D,F+3 vs D+1, D+2 vs Specials, Jump-in vs Anti-Air)
* **Mode 8: Release Check & Input Buffer Diagnostics** (Standard, Snap, Stagger, and Direction-Held Negative Edge checks)
* **Mode 9: Live Projectile & Frame Data Telemetry** (Projectile trajectory, hitbox duration, and frame data analysis)
* **Mode 10: EX Forceball -> F+3 Frame Trap vs EX Reversals** (Reptile Stand BL vs Crouch BL at Frames 94-103 into EX Flip Kick)

---

## In-Game Console & Menu Options (~ / Tilde)

Press **~ (Tilde)** at any time during gameplay or practice mode to open the Live ImGui Console:

1. **View Menu:** Toggle Frame Data HUD (F9), Auto-Scroll, Log Opacity slider, Clear Log.
2. **Display Mode Menu:** Cycle Borderless Windowed, Windowed, Fullscreen (F10).
3. **Gameplay Fixes Tab:**
   * Trade Engine Fix Toggle (F11)
   * Player 1 & Player 2 Negative Edge Toggles (Tournament Standard OFF / Vanilla ON)
   * Crouch Input Buffer Lookback Fix (Persistent Crouch)
   * Meter Drain Glitch Fix (Frame-1 Super Armor & Action Guard)
   * Toasty Training Mode & Secret Meter Boost Lab
4. **Collision Visualizer Tab:** Toggle Red Hitboxes, Green Hurtboxes, Cyan Pushboxes.
5. **Diagnostic Test Bot Tab:** Trigger Modes 1 through 10 with clickable UI buttons.
6. **Real-Time Combat Log:** Colored live telemetry feed showing hit registrations, trade decisions, damage percentages, reaction IDs, and frame timings.

---

## Installation

1. Copy `dinput8.dll` into your MK9 game directory:
   ```text
   Steam\steamapps\common\MortalKombat_KompleteEdition\DiscContentPC\
   ```
2. Launch Mortal Kombat Komplete Edition normally through Steam.
3. To uninstall, simply delete or rename `dinput8.dll`.

---

## TO DO / Roadmap

- [ ] More vanilla MK9 bug fixes & legacy crash remedies
- [ ] Accuracy in Hitboxes, Hurtboxes by default
- [x] Negative edge removal (Release Check toggle per player with tournament standard defaults)
- [x] Crouch Input Buffer lookback timeout fix
- [x] Meter Drain Glitch fix on EX move early startup
- [x] Toasty Meter Boost Training & Cameo Subsystem
- [x] Frame Data Visualizer timeline & advantage calculations
- [ ] In-game configuration & customization menu (hotkey remapping, visualizer styling)
- [ ] Frame meter gain calibration on attack hit/block
- [ ] EZ Toasty option meter gain
- [ ] Breaker meter allowance adjustments & tuning
