# MK9 Native Physical Trade Engine Mod (v0.6706a)
**Author:** Fai Khozen  
**Target:** Mortal Kombat 9 (Komplete Edition) - PC (Steam / DiscContentPC)  
**Support / Donate:** [Ko-fi](https://ko-fi.com/faikhozen) (https://ko-fi.com/faikhozen)  
**Bug Reporting:** [X/Twitter](https://x.com/faikhozen) (https://x.com/faikhozen)

> [!NOTE]
> **AI Assistance Disclosure:** This project was developed and reverse-engineered with the assistance of AI pair-programming tools for low-level memory analysis, disassembly tracing, and native hook orchestration.

> [!WARNING]
> **EXPERIMENTAL VISUALIZERS DISCLAIMER (ESTIMATES ONLY):**  
> Both the **3D Hitbox/Collision Visualizer** and the **Frame Data Timeline & Advantage Bar (F9)** are **experimental algorithmic estimates and approximations**; they are **NOT 100% frame-perfect or pixel-perfect accurate**.
> * **3D Collision Wireframes:** Approximated using skeletal bone transforms and heuristic bounding volumes (spheres, boxes, cylinders) rendered through native UE3 debug primitives.
> * **Frame Data Timeline & Advantage:** Dynamically calculated from live engine animation clocks, state flags, and memory timing heuristics rather than static developer frame tables. They serve as valuable real-time labbing references, but slight discrepancies can occur across complex cancels and multi-hit strings.

---

## What's New in v0.6706a (Changelog Since v0.6705c)

### 1. [FEATURE] Dramatic Mode / Dynamic Cinematic Camera System (Cam 1 & Cam 2)
Experience high-impact fighting game moments with broadcast-style dynamic camera angles on Counter Hits, Fatal Punishes, and Simultaneous Trades:
* **Preset Cam 1 (Dynamic FOV Close-Up):** Smooth cinematic FOV glide on counterhit / punish impact (e.g. -6.0° close-up), automatically restoring baseline FOV upon recovery.
* **Preset Cam 2 (Angled Dynamic Perspective):** Moves camera position laterally toward the attacker (`Shift Y`) while turning the rotation angle (`Yaw`) toward the defender, creating an over-the-shoulder dramatic broadcast clash perspective.
* **General Slow-Motion Integration:** Option to scale animation rates during camera adjustment (e.g. 0.80x slow-mo) with configurable speed-in and speed-out parameters.
* **Camera 2 Stabilization & Anti-Drift Engine:** Eliminated compounding yaw accumulation and wild camera spins across multi-pass rendering ticks by stripping previously applied offsets from raw engine pointers (`s_LastAppliedShiftY`, `s_LastAppliedYaw`) and gating interpolation strictly to once per frame.
* **Intelligent Recovery Tracking:** Camera holds through defender launches, knockdowns, and floor rolls, then smoothly releases back to neutral view the moment the defender gets up or recovers.

### 2. [FEATURE] Combat Meter System: Expenditure & Gains
Expanded the Extras menu with advanced super meter economy customization:
* **Special Move Expenditure:** Optional super meter cost deducted upon raw special move initiation (e.g. -25.0% / -0.250).
* **Normal to Special Cancel Cost:** Optional super meter cost deducted when canceling normal attacks into special moves (e.g. -10.0% / -0.100).
* **Normal to Dash Cancel Cost:** Optional super meter cost deducted when canceling normal attacks into forward or backward dashes (e.g. -10.0% / -0.100).
* **Combat Meter Generation (Gains):**
  * **Attacker Hit Meter Gain:** Configurable flat super meter rewarded to the attacker on landed strikes (with filter modes: All Moves, Normals Only, Specials Only).
  * **Defender Block Meter Gain:** Configurable flat super meter rewarded on blocking attacks (meter-on-block).
  * **Defender Hit Meter Gain:** Configurable super meter rewarded upon taking damage.
  * **Counter Hit & Fatal Punish Meter Rewards:** Configurable flat bonus super meter awarded to Defender, Attacker, or Both.

### 3. [FIX] Super Meter & EX Special Move Isolation
* **EX Special Move Isolation:** Fixed an issue where EX special moves fell through to normal attack classification in `OnTick`, which erroneously triggered normal attack hit gains and standard special move meter deductions.
* **Phantom Meter Refund Fix (29.7% -> 62.7%):** Fixed a bug where attempting an EX special move with insufficient meter armed the reversal refund tracker with 33.0% (despite 0 meter being deducted), resulting in an unexpected +33.0% meter bonus upon the next action.
* **Taxonomy Classification:** Integrated `IsEXMoveActive(plr)` into `ClassifySpecialOrSuper` in `combat_state.cpp` to guarantee that all EX specials (including unarmored projectiles like EX Fireball, EX Ice Ball) are properly classified as `MOVE_CLASS_EX_SPECIAL`.

### 4. [FEATURE] Juggle Deterioration & Gravity Deterioration
* Added dynamic combo juggle decay and gravity scaling per hit count to regulate juggle extensions and prevent infinite loops.

### 5. [UI & PRESETS] User Presets & Configuration
* 4 User Preset save/load slots (Slot 1 to Slot 4) with INI file import and export functionality.
* Quick Preset buttons: **Apply Vanilla Preset (Default)**, **Apply Dramatic Mode (Cam 1 FOV)**, and **Dramatic Mode (Cam 2 Angled)**.

---

## What's New in v0.6705b (Changelog Since v0.6705a)

### 1. [CALIBRATION] 0-Frame Reversal After Damage Execution
* **Instant First-Neutral-Frame Reversal:** Resolved an issue where configuring 0-frame delay on **Reversal After Damage** caused special moves (such as `D, B + 1`) to fail or drop into standard normal attacks (`0x5200`), requiring a 1-frame delay workaround.
* **Elimination of Premature Transition Ticks:** Removed an eager sequence tick that fired on the hurt release transition frame (`0x4600/0x4700 -> 0x0000`). Frame 0 now begins execution cleanly on the very first actionable neutral frame, guaranteeing 100% reliable 0-frame reversals on standing (e.g., Kratos 3) and crouching (e.g., Kratos D+1) hits.

### 2. [FIX] Delayed Wakeup & Delayed Wakeup Roll Recovery
* **Early Down Pre-buffering:** Integrated immediate Down pre-buffering upon knockdown hit registration, holding the character down smoothly without default tech-roll fallback.
* **Clean Neutral Disarm:** Speculative knockdown states automatically disarm when grounded hitstun finishes, preventing Down-holding from bleeding into standing/crouching hit recovery.
* **UI Table Cleanup:** Dedicated table views for Block Reversals and Reversals After Damage strictly display relevant columns (Status, Action, Weight, Delay) without cluttering wakeup parameters.

### 3. [FIX] Toasty 100% Uppercut in Scaled Combos
* **Multi-Hit Pop-up Launcher Detection:** Scans the active move record buffer for pop-up launcher sub-states (`sub == 12 || atkType == 10 || atkType == 12`) to detect uppercuts even when deep inside combo strings.
* **Scaled Damage Sensitivity:** Lowered the minimum damage evaluation threshold to `3%` (`0.03f`), ensuring Toasty easter egg boosts trigger reliably during heavily scaled combos.

---

## What's New in v0.6705a (Changelog Since v0.6704e)

### 1. [FEATURE] Custom Super Meter Expenditure (Extras Tab)
Take complete control over Mortal Kombat 9's super meter economics with fully configurable costs and activation requirements:
* **EX Special Move Cost:** Customize how much meter is required and burned for EX specials (configurable from `0.05` to `1.00`, Default: `0.25` / 25.0% — giving you 4 EX moves per full meter instead of the vanilla 3).
* **Kombo Breaker Cost:** Adjust the meter needed to break out of enemy kombos (configurable from `0.05` to `1.00`, Default: `0.50` / 50.0% — giving you 2 breakers per full meter instead of 1).
* **X-Ray Super Move Cost:** Configure the activation threshold and consumption for X-Ray super moves (configurable from `0.10` to `1.00`, Default: `1.00` / 100.0% — allowing X-Rays to be activated at less than full meter).
* **Live Dynamic Percentage Sliders:** Interactive sliders in the console menu feature real-time color-coded percentage readouts that dynamically update as you drag.
* **Precise Native Threshold Recalibration:** Replaces the engine's hardcoded `1.0` X-Ray requirements and native command buffer lookups (`0x008A9702`, `0x008A98AF`, `0x008A9E0E`) with an adaptive threshold and float precision epsilon tolerance (`-0.005f`), guaranteeing reliable activation at all custom cost levels.

### 2. [FEATURE] Real-Time On-Screen Meter Percentage HUD Indicator
* **High-Visibility Readout:** Renders each player's exact super meter level up to 1 decimal place (`XX.X%`) directly beside their respective meter dials at the bottom of the screen.
* **Dynamic Readiness Colors:**
  * **Glowing Cyan:** Meter $\ge$ X-Ray threshold (X-Ray Ready).
  * **Emerald Green:** Meter $\ge$ Breaker threshold (Breaker Ready).
  * **Amber Gold:** Meter $\ge$ EX threshold (EX Ready).
  * **Muted Gray:** Below threshold.
* **Startup Default:** Configured to **OFF by default on game launch**. Can be toggled on at any time under **Extras -> Super Meter System & Custom Expenditure** with customizable X/Y positioning sliders and a reset button.

### 3. [FEATURE & FIX] Practice Dummy Recording & Playback System (Tab 2)
* Added a full-featured frame-accurate input recording and playback engine accessible via Tab 2 ("Recording Settings").
* Record custom player or dummy input sequences, save across multiple slots, and play back on loop or as a one-shot to practice anti-airs, test punish windows, and lab defensive gaps.
* **Keyboard Recording & Playback Inconsistency Fix:**
  * Fixed directional vs attack key conflict where `VK_NUMPAD4` and `VK_NUMPAD6` were polled as Left/Right walk keys while in MK9 PC they bind to Front Kick (3) and Back Kick (4), which previously caused playback to kick whenever walking.
  * Properly mapped native Player 2 keyboard bindings (Arrow keys for movement; Numpad 7, 9, 4, 6 for 1, 2, 3, 4; Numpad 0 for Block; Numpad Decimal for Tag; Numpad Add for Stance).
  * Expanded Player 1 keyboard recording polling to support standard layout (`W, A, S, D`, `U, I, J, K, L`, `1, 2, 3, 4`, Space).
  * Dedicated directional key injection guaranteeing faithful, glitch-free keyboard playback.

### 4. [FEATURE & FIX] Practice Dummy Reversal Engine (Tab 3)
* Program the practice dummy to automatically execute immediate or delayed reversals out of:
  * **Blockstun:** Reversal attack on block.
  * **Hitstun (Reversal After Damage):** Reversal attack on hit.
  * **Wakeup:** Invincible Wakeup or Regular Wakeup.
* Supports customizable execution delays (0 to 30 frames) to test true blockstrings, frame traps, and meaty timing.
* Selectable knockdown recovery methods: Normal Wakeup, Tech Roll / Quick Roll, Delayed Wakeup, and Delayed Wakeup Roll.
* **Block Reversal & Crouch Block Calibration Fix:**
  * Fixed an issue where idle crouching block (`0x0A00`) and idle standing block (`0x0A01`) were treated as active blockstun, causing block reversals to stall until an internal 30-frame ceiling expired.
  * Differentiated active block reactions (`0x0A02..0x0A0F` or action lock) from idle blocking stances so block reversals trigger reliably on frame 1 of recovery.
  * Maintained crouch block stance continuously during reversal delay intervals (`delayFrames > 0`) so the dummy never prematurely drops block during multi-hit blockstrings.
* **Hit Reversal (After Damage) Fix:**
  * Fixed an issue where hit reversals never triggered when the dummy was in crouch block or hit by crouching/low attacks.
  * Removed premature hitstun cancellation on frame 1 caused by false block detection.
  * Broadened hit reaction tracking from only `0x4600` (stand hurt) to all hurt states (`0x4000..0x4FFF`, including `0x4700` crouch hurt and `0x4100` low hurt).
  * Excluded hurt states from `inAttackThread` so stun tracking is never erroneously wiped.

### 5. [FEATURE & FIX] Knockdown & Wakeup Recovery Customization (Extras Tab)
* **Disable Delayed Wakeup:** Suppresses holding Down on the floor; fighters always wake up on standard immediate timing (~32-38F).
* **Fixed Delayed Wakeup Hold Duration:** Eliminates arbitrary ground release timing by locking the player into a predetermined duration (30F to 96F) once initiated.
* **Disable Delayed Wakeup Roll:** Suppresses backward rolling when rising from delayed wakeup; character rises stationary in place.
  * **Fix:** Suppressed Down-Away (`0x8000001F` / `0x1F`) and Up-Away (`0x80000027` / `0x27`) tokens so rolling cannot be initiated while holding Down-Back.
  * Zeroed root motion velocity (`+0x2F74`) and anchored floor position upon roll remapping.
  * Routed gamepad injection through controller input filters so dummy controller overrides cannot bypass roll suppression.

### 6. [FEATURE] Free Camera & 3D Visualizer Subsystem (Tab 4)
* Detachable 3D free camera allowing full 360-degree rotation, pan, zoom, and cinematic perspective inspection during matches and practice.
* Native UE3 3D collision visualizer for real-time attack hitboxes, body hurtboxes, and character pushboxes *(experimental estimates)*.

### 7. [UI] Streamlined 5-Tab In-Game Console Menu
* Restructured the ImGui console menu (`~` key) into 5 focused master tabs:
  * **Tab 1: Gameplay Fixes** (Ducking Bug Fix, Meter Drain Glitch Fix, Physical Trade Priority Fix, Negative Edge controls, Toasty Training).
  * **Tab 2: Recording Settings** (Practice dummy recording & playback slots).
  * **Tab 3: Reversal Settings** (Practice dummy reversal rules, recovery modes, execution delays).
  * **Tab 4: 3D Visualizer & Display** (Native UE3 3D collision wireframes, 720p monitor lock, Free Camera, Frame Data timeline bar).
  * **Tab 5: Extras** (Super Meter Custom Expenditure & Percentage HUD, Knockdown & Wakeup options, Modular Move Priority Hierarchy, Juggle Value & Gravity Deterioration).

---

## Mod Features in General

### 1. Physical Trade Engine & Simultaneous Strike Clashes
In vanilla MK9, when two fighters attack simultaneously, the engine's internal anti-trade system (Stage 5) arbitrarily kills the second player's attack thread on frame 1, creating one-sided interrupts and preventing authentic fighting game trading. This mod intercepts the native combat pipeline to deliver:
* **Limb vs Limb Trades:** Simultaneous jabs, pokes, kicks, and normals connect and trade damage/reactions symmetrically on active frames.
* **50/50 Special Move Clashes:** When two physical specials collide during active frames (e.g., Sub-Zero slide vs Scorpion teleport punch), both connect with winner immunity protection to prevent broken double-hits.
* **Natural Counter Hits:** Authentic counter-hit advantages when striking an opponent during their startup windup.
* **Pure Physical Trade Mode:** When all move priority overrides are OFF (default), hits trade with 100% physical neutrality.

### 2. Weapon Prop Clashing (Type A & Type B)
* **Type A Props (Rigged Skeletal Weapons):** Full 3D collision clashing for direct bone-attached weapons (Scorpion swords, Kitana steel fans, Baraka arm blades, Sonya batons, Cyber Sub-Zero swords).
* **Type B Props (Spawned Child Entities):** Real-time tracking and hitbox clashing for detached/attached weapon entities (Jade bo staff, Nightwolf tomahawks, Kenshi spirit sword, Mileena sai).
* **Weapon vs Limb Clashing:** Authentic physical interactions when striking weapons against limbs or weapons against weapons.

### 3. Modular Move Priority Hierarchy (Extras Tab)
Configure independent priority rules under `Extras > Move Priority` (all OFF by default for pure trade mode):
* **Uppercut (D+2) Anti-Air Priority:** D+2 cleanly beats incoming Jump-In attacks during active anti-air frames.
* **Uppercut vs Special:** D+2 cleanly overpowers grounded special moves during active clash frames.
* **Uppercut vs Uppercut:** Later inputted D+2 wins by default.
* **Jump-In Attacks:** Jump-ins trade or cleanly beat grounded standing normals during active falling frames.
* **X-Ray Absolute Priority:** Cinematic X-Ray super moves cleanly overpower incoming normal and special strikes.
* **Instant Projectile Impacts:** Projectiles deal immediate damage/reactions on contact without artificial holding or passthrough glitches.

### 4. Frame-1 EX Super Armor & Meter Drain Glitch Protection
* Fixes the notorious vanilla engine bug where taking a hit during early startup frames of an EX Special move (before native armor activates) drains 1 bar of super meter while the move completely vanishes and drops.
* Grants immediate Frame-1 Super Armor, neutralizes incoming flinch reactions, and protects the attacker's action from premature abortion.

### 5. Persistent Crouch Input Buffer (Ducking Bug Fix)
* Eliminates the vanilla engine bug where holding Down causes directional buffer tokens to expire after 8 frames, causing subsequent attack button presses (1, 2, 3, 4) to resolve as neutral standing attacks rather than low pokes (D+1, D+3, D+4) or uppercuts (D+2).
* Maintains continuous crouch state for as long as Down is physically held, ensuring 100% reliable low pokes and anti-air uppercuts from extended crouching states.

### 6. Negative Edge (Release Check) Subsystem
* **Tournament Standard (Disabled by Default):** In competitive fighting games, specials executing on button release often cause accidental misfires. This mod defaults Negative Edge to **OFF** for both players.
* **Independent Per-Player Toggles:** Easily turn Negative Edge on or off for P1 and P2 individually via hotkeys (`Shift + F7` / `Shift + F8`) or the console UI.
* **Zero Performance Overhead:** Optimized with direct pointer indexing and zero-overhead early exits, ensuring solid 60 FPS performance.

### 7. Experimental 3D Hitbox & Collision Visualizer (DirectX 9 Hook)
> **Note:** Visualizers are *experimental estimates* based on skeletal bone tracking and heuristic bounding geometry, not 100% pixel/frame accurate.
* **Red Volumes:** Active physical striking hitboxes (limbs, Type A weapons, Type B props).
* **Green Volumes:** Full skeletal hurtboxes and body collision cylinders.
* **Cyan Volumes:** Ground and aerial positional pushboxes.
* Toggleable directly in the in-game console (Tab 4: 3D Visualizer & Display).

### 8. Experimental Frame Data Visualizer & Timeline Bar (F9)
> **Note:** Timings and frame advantage are *experimental estimates* based on live memory states and animation clocks, not 100% accurate.
* **Dynamic Timeline Segmentation:** **Startup (Pink)**, **Active (Red)**, **Recovery (Yellow)**, **Hitstun / Stumble (Orange)**, **Blockstun (Blue)**, **Dash / Cancel (Slate)**, **Quick Roll / Tech Roll (Green)**, **Invincible Wake-up (Violet)**, **Delayed Wakeup (Teal)**.
* **Real-Time Frame Advantage Readout (+/-):** Instant calculation of advantage on hit or block.
* **Active Duration Digit Overlays:** Live number labeling over phase blocks for visual measurement.
* **Custom Positioning & Opacity:** Right-click context menu and drag anywhere to reposition.

### 9. Practice Mode Lab Tools (Recording & Reversals)
* **Practice Dummy Recording & Playback (Tab 2):** Record and loop custom dummy attack strings, jump-ins, and mixups across multiple memory slots.
* **Practice Dummy Reversal Engine (Tab 3):** Set the dummy to auto-reversal out of blockstun, hitstun, or wakeup with configurable delay frames (0-30F) to test frame traps and meaty setups.
* **Knockdown Recovery Options:** Enforce or test Normal Wakeup, Tech Roll, Delayed Wakeup, and Delayed Roll.

### 10. Live Microsecond Hook Telemetry & Hardware Profiler
* Dedicated performance profiler backed by high-resolution hardware timers (`QueryPerformanceCounter`).
* Measures live execution times (in microseconds $\mu s$), peak latency spikes, and frame budget percentages across input hooks, trade engine, meter logic, and rendering.

### 11. Simulation Controls & Free Camera
* **Pause / Freeze Simulation:** Press **Backslash (\)** to instantly freeze the simulation match state.
* **Step 1 Frame Forward:** Press **Equals (=)** while paused to advance the match exactly one tick at a time.
* **Detachable 3D Free Camera:** Full 360-degree rotation, pan, zoom, and cinematic perspective inspection.

### 12. Display & Quality-of-Life Features
* **720p & Below Monitor Auto-Fullscreen Lock:** Detects displays $\le 720p$ and locks to Exclusive Fullscreen to prevent window clipping.
* **Fullscreen Direct3D Mouse Cursor:** Native arrow cursor rendered directly inside Exclusive Fullscreen.
* **Display Mode Switching:** Switch between Borderless Windowed, Windowed, and Fullscreen anytime via **F10** or the console menu.
* **Toasty Training Mode:** Forces Dan Forden's "Toasty!" cameo 100% of the time on D+2 uppercuts with the authentic 85-frame reward window (`Down + Start`) for full meter replenishment.
* **Stability Fixes:** Havok memory page commit guards and audio vtable initialization guards prevent random Havok and R6025 crashes.
* **Skip Intro:** Automatically skips opening logos directly to the title screen.

### 13. Juggle Value & Gravity Deterioration Subsystem (Extras Tab)
* **Custom Air Decay Rate:** Direct tuning of airborne physics velocity decay (`0.050x` to `3.000x`, Vanilla standard `0.700x`), allowing heavier gravity drops or floatier extended aerial juggling.
* **Per-Hit Juggle Deterioration:** Dynamic gravity scaling that deducts decay delta (`0.005x` to `0.700x`) per consecutive juggle hit, progressively pulling airborne opponents downward to naturally prevent infinite corner loops.
* **Bypass Move Juggle Limit (Infinite Pop-ups):** Clamps the engine's internal juggle counter check at `0x00895962` so moves never refuse to launch or lock into untechable falling states.
* **Strict Jump Isolation:** Normal neutral, forward, backward jumps, air normals, and landings (`action < 0x4000`) remain 100% vanilla and strictly unaffected by juggle decay.
* **Live In-Match Telemetry:** Real-time pushbox Z coordinates, juggle hit counters, and effective decay readouts for both P1 and P2.

---

## Controls & Hotkeys

### General & Overlay Controls
| Hotkey | Action |
| :--- | :--- |
| **~ (Tilde)** | Open / Close Live In-Game ImGui Console |
| **\ (Backslash)** | Pause / Freeze Game Simulation |
| **= (Equals)** | Step 1 Frame Forward (while paused) |
| **F9** | Toggle Real-Time Frame Data Visualizer HUD *(Experimental)* |
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

## In-Game Console Menu Structure (~ / Tilde)

Press **`~` (Tilde)** at any time during gameplay or practice mode to access the 5-tab control panel:

* **Tab 1: Gameplay Fixes:**
  * Trade Engine Fix Toggle (F11)
  * Player 1 & Player 2 Negative Edge Toggles (Tournament Standard OFF / Vanilla ON)
  * Crouch Input Buffer Lookback Fix (Persistent Crouch / Ducking Bug Fix)
  * Meter Drain Glitch Fix (Frame-1 Super Armor & Action Guard)
  * Toasty Training Mode & Dan Forden 85-Frame Reward Window
* **Tab 2: Recording Settings:**
  * Practice dummy frame-accurate input recording & playback engine
  * Multi-slot recording management (Loop playback, One-shot playback)
* **Tab 3: Reversal Settings:**
  * Auto-reversal attack triggers (On Blockstun, On Hitstun, On Wakeup)
  * Execution delay sliders (0 to 30 frames)
  * Knockdown recovery method selection (Normal, Tech Roll, Delayed Wakeup, Delayed Roll)
* **Tab 4: 3D Visualizer & Display:**
  * UE3 Native 3D Collision Visualizer *(Experimental Estimates)*: Red Hitboxes, Green Hurtboxes, Cyan Pushboxes
  * Display Management: Borderless, Windowed, Exclusive Fullscreen, 720p Auto-Lock
  * Frame Data Timeline Bar Settings *(Experimental Estimates)*: Opacity, Positioning, Color Glossary
  * Detachable 3D Free Camera controls
* **Tab 5: Extras:**
  * Super Meter System & Custom Expenditure (EX, Breaker, and X-Ray cost sliders + live % readouts)
  * Real-Time On-Screen Meter Percentage HUD (XX.X% readout, position sliders, reset)
  * Knockdown & Wakeup Recovery Customization (Disable Delayed Wakeup, Fixed Duration, Disable Roll)
  * Modular Move Priority Hierarchy (Uppercuts, Specials, Jump-Ins, X-Rays, Projectiles)
  * Extra Gameplay: Juggle Value & Gravity Deterioration (Air Decay Multiplier, Dynamic Per-Hit Deterioration Scaling, Bypass Move Juggle Limits, Live Telemetry)

---

## Installation

1. Copy `dinput8.dll` into your Mortal Kombat 9 installation folder (where `MKKE.exe` is located):
   * Example: `...\Steam\steamapps\common\MortalKombat_KompleteEdition\DiscContentPC\`
2. Launch the game normally through Steam.
3. Press **`~` (Tilde)** on your keyboard at any time in-game to toggle the ImGui console menu.
4. To uninstall, simply delete or rename `dinput8.dll`.

---

## Verification & Integrity

* **File:** `dinput8.dll` (x86 release binary)
* **SHA-256:** `32AC9A0D22C18584C18DB1D8A923E6E963791AB95FF7DFD73468CBC7F35802EA`

---

## TO DO / Roadmap

- [ ] More vanilla MK9 bug fixes & legacy crash remedies
- [ ] Accuracy in Hitboxes, Hurtboxes by default (transitioning from experimental estimates to exact native physics mesh volumes)
- [x] Negative edge removal (Release Check toggle per player with tournament standard defaults)
- [x] Crouch Input Buffer lookback timeout fix
- [x] Meter Drain Glitch fix on EX move early startup
- [x] Toasty Meter Boost Training & Cameo Subsystem
- [x] Frame Data Visualizer timeline & advantage calculations (experimental)
- [x] Practice dummy recording & auto-reversal engine
- [x] Custom super meter expenditure & on-screen percentage HUD
- [ ] In-game configuration & customization menu (hotkey remapping, visualizer styling)
- [ ] Frame meter gain calibration on attack hit/block
