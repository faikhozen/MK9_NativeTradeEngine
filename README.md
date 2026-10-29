# MK9 Native Physical Trade Engine

**Mortal Kombat Komplete Edition · Windows PC / Steam · v0.6710a**  
**By Fai Khozen** · [Support on Ko-fi](https://ko-fi.com/faikhozen) · [Reports / Contact](https://x.com/faikhozen)

A deep native gameplay mod, training lab suite, frame-data engine, and visual capture toolkit for *Mortal Kombat Komplete Edition* (PC). It features true physical attack trading, comprehensive practice dummy recording and reversal automation, interactive frame-data timelines, skeletal collision visualization, stage Void Mode / green-screen capture, customizable combat rules, and an in-game graphical overlay.

[Revision history](docs/CODEX_CHANGELOG.md) · [Release validation](docs/PUBLIC_RELEASE_v06710.md) · [Macro diagnostics](docs/MACRO_DIAGNOSTICS.md) · [Hotkey guide](docs/HOTKEYS.md)

---

## Architecture & Tools Used

The mod operates entirely in-process as a native DirectInput8 proxy library (`dinput8.dll`), requiring no external trainers, injectors, or modified game executables on disk:

* **[Dear ImGui](https://github.com/ocornut/imgui) (v1.90.4)**: Powers the in-game DirectX 9 graphical overlay console, multi-tab layout, interactive RGB color wheels, draggable floating HUD windows, live telemetry graphs, and real-time combat meters.
* **[MinHook](https://github.com/TsudaKageyu/minhook) (v1.3.3)**: Provides minimalist x86 API hooking and memory detours across Unreal Engine 3 / NetherRealm combat engine functions.
* **Hacker Disassembler Engine 32 C (HDE32)**: Embedded length-disassembly engine used by MinHook to safely decode and hook x86 function prologues.
* **DirectInput8 Proxy (`dinput8.dll`)**: Intercepts `DirectInput8Create` and chains calls to the Windows system `dinput8.dll`, allowing automatic initialization by `MKKE.exe` at game startup.
* **Direct3D 9 Pipeline Interception**: Hooks `IDirect3DDevice9::EndScene` (index 42), `Reset` (index 16), `Clear` (index 43), and `DrawIndexedPrimitive` (index 82). Handles UI rendering, custom background clearing for Chroma Keying, and runtime vertex declaration filtering for Void Mode stage suppression while preserving translucent VFX particles and combat projectiles.
* **Ghidra & Ghidra Bridge Server**: Reverse engineering, live disassembly tracing, and structure analysis of the *MKKE.exe* binary (communicating over port 7077).
* **XInput & Windows Raw Input**: Manages low-level controller port detection, gamepad swapping, P1/P2 input quarantine, and synthetic macro injection for the automated test bot suites.
* **Microsoft Visual Studio 2022 Community (MSVC x86 C++17)**: Native build toolchain.

---

## User Interface & Overlay Overview

Press **`~` (Tilde / Grave Accent)** at any time during gameplay or practice to toggle the in-game **Dear ImGui Console Overlay**. The console window is fully resizable, draggable, and features adjustable background opacity (**0.05** to **1.00**).

### Menu Bar
* **View**:
  * **Frame Data Visualizer (F9)**: Toggle the live bottom-center timeline bar.
  * **Auto-Scroll**: Toggle automatic log scrolling in the console.
  * **Unlock 4:3 & Non-16:9 Resolutions**: Removes the engine's hardcoded 0.15 aspect-ratio clamp (`0x005692F7`), enabling 4:3 (800x600, 1024x768, 1280x960, 1400x1050, 1600x1200), 5:4 (1280x1024), and 16:10 resolutions in the in-game Video Options menu.
  * **Clear Log**: Empties the current in-memory console log buffer.
  * **Close Console (`~`)**: Closes the overlay window.
* **Gameplay Fixes Menu**: Quick-access toggles for Ducking Bug Fix, Meter Drain Glitch Fix, Input Bleed Quarantine, Trade Priority Fix, and Jump Input Threshold Fix.
* **Display Mode Selector**: Dropdown to switch immediately between **BORDERLESS**, **WINDOWED**, and **FULLSCREEN** modes (or cycle with **F10**).
* **Header Telemetry**: Displays the live match simulation frame counter (`Frame: XXXXXX` or `Frame: In Menu`), `[PAUSED]` indicator, active Diagnostic Bot Mode index, and a quick-adjust opacity slider.

---

## Features by UI Console Tab

```
====================================================================================
                        IN-GAME CONSOLE MASTER NAVIGATION
====================================================================================
 [ Gameplay Fixes ] [ Recording Settings ] [ Reversal Settings ] [ 3D Visualizer & Display ]
 [ Extras ]         [ Performance & Telemetry ] [ Console Logs ]
====================================================================================
```

### Tab 1: Gameplay Fixes

* **Core Engine Fixes**:
  * **Physical Trade Priority Fix**: Disables vanilla Stage 5 one-sided attack interruption, restoring physical hitbox clashing and mutual hit execution on simultaneous strikes.
  * **Ducking Bug Fix**: Remaps expired neutral directional tokens (`0x80000000`) to native down tokens (`0x80000004`) when ducking or holding Down, preventing standing normals from coming out during crouch.
  * **Meter Drain Glitch Fix**: Drains EX meter at move launch to prevent losing meter upon interrupt before active frames without executing the move.
  * **Input Bleed & Cross-Talk Quarantine**: Quarantines device queries and disables global fallback scanning to prevent rapid button mashing on Player 2 (or Player 1) from bleeding inputs into the opponent.
  * **Jump Input Threshold Fix**: Eliminates the hardcoded 4-frame hold requirement for Up inputs, enabling immediate 1f, 2f, and 3f jump takeoffs while preserving the native 3-frame pre-jump squat (`0x200E`) and physics arcs.
  * **Runtime Console & Diagnostic Logging**: Toggle tick-by-tick combat tracing and log ingestion (turn OFF to maximize FPS during long sessions).
* **Release Check / Negative Edge Subsystem**:
  * Individual **Player 1 Negative Edge** and **Player 2 Negative Edge** checkboxes. When OFF, special moves require a true downstroke button press rather than executing on button release.
* **Dan Forden "Toasty!" Subsystem**:
  * **100% Uppercut "Toasty!" Cameo**: Guarantees Dan Forden's iconic cameo on every landed D+2 uppercut when no cameo is currently on screen. Inputting the character's secret TOASTY BOOST sequence (or Down) during the active cameo window (~40 frames) awards a full 100% Super Meter boost.
  * **"Trigger 'Toasty!' Cameo Now" Button**: Manually dispatches the native `0x008D6180` cameo trigger on demand.
  * **Appearance & Screen Placement**:
    * **Dan Forden Size Slider**: Adjust scale from 1.00x (vanilla) up to 3.80x (giant), with a **Random Size** option and reset button.
    * **Cameo Corner Dropdown**: Choose between Bottom-Right (Vanilla), Bottom-Left, Top-Right, Top-Left, or Random Corners.
    * **Auto-Mirror**: Automatically mirrors Dan Forden horizontally when appearing from left corners to face inward toward fighters.
    * **Auto-Flip**: Automatically flips Dan Forden upside-down when appearing from top corners so he peeks down from the ceiling.
    * *Safety Guard*: Custom sizing and corners lock safely to authentic vanilla behavior in 720p or 4:3 resolutions.
  * **Active Character Detection & Boost Reference**:
    * Dynamically displays both fighters' names, secret boost sequences, and input details.
    * Searchable 32-character **TOASTY BOOST Roster Reference Guide** table with live text filtering and active fighter highlight badges (`[P1]`, `[P2]`).

---

### Tab 2: Recording Settings (Practice Dummy Recorder)

* **Mode Prerequisites**: Requires Practice Mode with Dummy set to **Human** control. Real-time status banners alert if in Main Menu, non-practice game modes, or if the dummy is set to AI.
* **Operational Sub-Modes**:
  * **Record Mode (Capture P2)**: Capture P2 inputs (gamepad, stick, keyboard) into 5 dedicated slots. Extended capacity allows up to **4 minutes (14,400 frames)** of continuous recording per slot.
  * **Replay Mode (Playback P2)**: Have the dummy play back recorded slots with playback modes: **Play Once**, **Repeat / Loop**, or **Random Slot Selection**.
* **Recording Trigger Methods**:
  * **Auto (On First Input)**: Arms the recorder; capture starts the moment P2 presses any button or direction.
  * **Manual (Instant)**: Begins recording immediately upon clicking Record.
  * **Countdown (Timer)**: Configurable countdown delay (0 to 3 seconds) with on-screen visual progress bar before recording starts.
* **Super Meter Practice Mode**: Dropdown to toggle **NORMAL**, **FULL** (locks meter at 100%), or **REFILL** (automatically restores meter to 100% if neither player attacks for 4 seconds).
* **Slots Table**: 5 slots displaying assigned character, recorded duration/frame count, Record/Play toggle buttons, and slot Clear buttons.
* **Floating Recorder HUD**: Toggleable compact floating window showing recording countdown, live recording/playback progress bars, elapsed time/frames, and stop/start controls.
* **Recording File Management (`.mkr`)**:
  * Export/import active slot to `.mkr` file.
  * Batch export/import all slots to/from a folder (`slot1.mkr` .. `slot5.mkr`).
* **Controller Port Configuration**:
  * **Detect P2 (Dummy) Controller**: Press any button on the dummy pad to automatically bind it to P2.
  * **Detect P1 Controller**: Auto-binds P1 gamepad.
  * **Swap P1 <-> P2 Gamepad**: One-click controller port swap.
  * Keyboard notice: P2 keyboard control uses native default controls (Arrow keys + Numpad).

---

### Tab 3: Reversal Settings (Practice Dummy Reversal Engine)

* **Global Reversal Engine Toggle**: Arm or disarm the automated reversal system. Target can be assigned to Player 2 (dummy) or Player 1.
* **Dummy Posture & Idle Stance Controls**:
  * Instant posture buttons: **Standing**, **Crouch**, **Stand Block**, **Crouch Block**, **Auto Block (Stand)**, **Auto Block (Crouch)**.
  * **Auto Block Mechanics**: In Auto Block mode, the dummy allows the initial strike to connect (confirming combo initiation) and then automatically locks into continuous guard across all subsequent hits until the attacker stops attacking for 2 seconds (120F).
  * **Floating Dummy Stance HUD**: Toggleable compact floating window for rapid posture switching during training.
* **Reversal Categories**:
  1. **Invincible Wake-up**: True wakeup reversal special attack with 3-frame pre-buffer, native invulnerability armor, and the "WAKE UP ATTACK" banner.
  2. **Regular Wake-up**: Neutral getup reversal. Waits for neutral recovery after getup and adds 1 frame beyond the selected delay.
  3. **Block Reversal**: Executes on the exact first frame out of blockstun.
  4. **Reversal After Damage**: Executes on the exact first frame out of hitstun.
* **Per-Slot Customization**:
  * 5 configurable slots per category.
  * **On/Off Toggle**: Enable multiple slots simultaneously for mix-up testing.
  * **Weight (1 to 10)**: RNG selection frequency when multiple slots are enabled.
  * **Knockdown Recovery Options**: Normal Wakeup, Quick Roll, Delayed Wakeup, Delayed Wakeup Roll.
  * **Hold Duration Slider**: Sets committed grounded duration (30F to 96F) for Delayed Wakeup options.
  * **Delay Slider**: Configurable execution delay (0F to 30F).
* **Comprehensive Reversal Action Library**:
  * *Neutral*: Stand / Do Nothing.
  * *EX Special Moves*: All character-specific EX specials with armor and invulnerability (`EX DF1` through `EX DBF3`).
  * *Special Moves*: Standard specials (`DF1` through `DBF3`).
  * *Normal Attacks*: Stand 1, Stand 2, Stand 3, Stand 4, D+1, D+2 Uppercut, D+3, D+4, Throw.
  * *Movement & Defense*: Walk Forward, Walk Back, Backdash, Forward Dash, Neutral Jump, Forward Jump, Backward Jump, Crouch, Stand Block, Crouch Block.
  * *Super*: X-Ray Attack.
* **Live Wakeup State Telemetry**: Real-time phase monitor tracking: IDLE / READY, AIRBORNE KNOCKDOWN (Falling), GROUNDED KNOCKDOWN (Floor Settle), DELAYED WAKEUP (Ground Hold), FORCE ROLL MODE (Tech Roll), RISING GET-UP (Wakeup Window), REVERSAL EXECUTED.

---

### Tab 4: 3D Visualizer & Display

* **Collision Visualizer**:
  * **Attack Hitboxes (Red circles)**: Traverses live attack sphere records directly from `PLAYER_COL_INFO`.
  * **Body Hurtboxes (Green/Yellow circles)**: Traverses the native skeletal hurtbox array updated per frame.
  * **Pushboxes (Cyan circles)**: Displays fighter penetration volumes and character separation radii.
* **Clean Fighters**:
  * Suppresses all blood particles, sprays, smears, and ground pooling during matches.
  * Suppresses cosmetic battle damage, clothes tearing, and skin bruising. Health damage, hit reactions, and mechanics function normally.
  * Displays live telemetry: blood effects cleared, mesh resets, and fault counts.
* **Stage Background & Floor Grid (Void Mode)**:
  * **Disable Stage Background (Void Mode)**: D3D9 hook intercepts draw calls to suppress static scenery meshes, props, decals, and skyboxes. Active strictly inside 3D matches when health bars appear.
  * **Translucent VFX Preservation**: Employs destination blend checks (`destBlend > D3DBLEND_ZERO`) to ensure fireballs, energy beams, ice blasts, slime trails, and particles remain visible against the void.
  * **Stage Characters & Ambient FX Suppression**:
    * Checkbox to hide animated stage background characters and dynamic scenery props.
    * Checkbox to hide stage ambient FX and translucent environmental meshes (Cemetery fog, water planes, Pit torches, Hell lava).
  * **Interactive Chroma Keying Palette**:
    * Full RGB Color Picker Wheel to select any background color for video compositing in OBS, Premiere, or DaVinci Resolve.
    * Quick Presets: **Pure Black**, **Green Screen** (`#00FF00`), **Blue Screen** (`#0033FF`), **Magenta** (`#FF00FF`).
  * **3D Perspective Floor Grid**:
    * Perspective floor grid drawn on the stage plane with distance intervals.
    * Customizable Grid Line Color picker + Presets (Cyan, Green, Gold, Purple).
    * Center Division Lines Color picker ($u=0, v=0$) + Presets (White, Gold, Cyan, Red).
  * **Dynamic Stage Corner Wall Boundaries (Laser Walls)**:
    * Accurately extracts dynamic stage limits from internal Unreal Engine bounds registers (`0x00DF0BE4` / `0x00DF0C20`).
    * Renders vertical boundary walls and floor markings at true corner limits ($X = \pm\text{halfWidth}$), dynamically adapting to both standard and wide arenas (including The Street stage wall hotfix).
    * Customizable wall colors (Crimson Warning Red, Orange, Gold).
  * **Character Lighting Probe Warmup Engine**:
    * Prevents fighters from turning black or losing shaders upon position resets, teleports (Scorpion Teleport Punch), or morphs (Shang Tsung Soul Steal).
    * Provides a pause-guarded warmup delay allowing UE3 to bake character lighting probes before re-engaging stage suppression.
* **Display & Window Management**:
  * **720p Monitor Lock**: Automatically detects displays $\le 1280\times 720$ and locks to Fullscreen to prevent window clipping.
  * **Display Switching**: Borderless Windowed, Windowed (Bordered), Exclusive Fullscreen, and Cycle Display (**F10**).
  * **Unlock 4:3 & Non-16:9 Resolutions**: Unlocks 800x600, 1024x768, 1280x960, 1280x1024, 1600x1200, and 16:10 resolutions in the in-game Video Options menu.
* **Frame Data Timeline Bar Settings**:
  * Enable/Disable toggle (**F9**).
  * Frame Bar Opacity slider (0.00 to 1.00).
  * **Reset Frame Bar Position**: Re-centers the bar at bottom-center of the screen.
  * **Color Reference & Glossary Table**:
    * Startup: Pink (`RGB: 235, 55, 125`)
    * Active: Red (`RGB: 230, 35, 45`)
    * Recovery: Blue (`RGB: 25, 135, 225`)
    * Hitstun / Knockdown: Yellow (`RGB: 245, 225, 45`)
    * Blockstun: Vibrant Cyan (`RGB: 0, 215, 240`)
    * Dash / Cancel: Vibrant Orange (`RGB: 255, 140, 20`)
    * Quick Roll / Tech Roll: Emerald Green (`RGB: 40, 215, 105`)
    * Invincible Wake-up: Royal Electric Violet (`RGB: 180, 70, 255`)
    * Delayed Wakeup: Deep Sea Teal (`RGB: 18, 185, 165`)

---

### Tab 5: Extras (Tuning, Meter Economy & Combat Rules)

* **User Presets & Configuration (4 Slots)**:
  * 4 save/load slots with customizable names.
  * Export/import all presets to/from `mk9_extras_presets.ini`.
  * Quick Presets: **Apply Vanilla Preset (Default)**, **Dramatic Mode (Cam 1 FOV)**, and **Dramatic Mode (Cam 2 Angled)**.
* **Super Meter Economy & Combat Meter System**:
  * **Custom Expenditure**:
    * EX Special Cost slider (0.05 to 1.00; default 0.33).
    * Kombo Breaker Cost slider (0.05 to 1.00; default 0.67).
    * X-Ray Super Move Cost slider (0.10 to 1.00; default 1.00).
  * **Special & Throw Costs**:
    * Special Move Meter Cost slider (0.000 to 0.500 per special).
    * Throw Attack Meter Cost slider (0.000 to 0.500 per throw).
  * **Normal Attack Cancel Penalties**:
    * Normal $\rightarrow$ Special Move Cancel Cost (0.000 to 0.500).
    * Normal $\rightarrow$ EX Special Cancel Extra Cost (0.000 to 0.500).
    * Normal $\rightarrow$ X-Ray Super Cancel Extra Cost (0.000 to 0.500).
    * Normal $\rightarrow$ Dash Cancel Cost (0.000 to 0.500).
  * **Combat Meter Generation (Gains)**:
    * Attacker Hit Meter Gain: Separate reward sliders for normal strikes and specials.
    * Defender Block Meter Gain: Rewards super meter on blocking attacks (meter-on-block).
    * Defender Hit Meter Gain: Additional meter awarded upon taking damage.
  * **On-Screen Meter Percentage HUD**:
    * High-visibility 1-decimal readout (`XX.X%`) rendered beside each player's meter dial.
    * Color-coded readiness threshold: Cyan (X-Ray ready), Green (Breaker ready), Gold (EX ready), Gray (below threshold).
    * Customizable X/Y screen offset sliders and reset button.
* **General Hitstun, Blockstun & Pushback Modifiers**:
  * Target Scope: Both Players, P1 Only, P2 Only.
  * General Blockstun Adjustment slider (-12 to +12 frames).
  * General Hitstun Adjustment slider (-15 to +60 frames).
  * Block Pushback Multiplier slider (0.00x to 3.00x) with quick buttons (Point-Blank 0.00x, Tight 0.50x, Vanilla 1.00x, Heavy 2.00x).
  * Corner Pushback Reflection: Transfers pushback into the attacker when defender is backed into the stage wall.
  * Reset to Default (0F) button.
* **Attack Frame Data Modifier**:
  * Master toggle, target scope (Both, P1, P2), and Reset to Vanilla (0F) button.
  * Normal Attacks Tuning: Startup Delta (-12 to +12F), Active Delta (-12 to +12F), Recovery Delta (-12 to +12F).
  * Special Moves Tuning: Startup Delta (-12 to +12F), Active Delta (-12 to +12F), Recovery Delta (-12 to +12F).
  * Individual Standing Normal Overrides: Dedicated Startup, Active, and Recovery sliders for FP (Front Punch), BP (Back Punch), FK (Front Kick), and BK (Back Kick).
* **Counter Hit, Fatal Punish & Trade System**:
  * **Counter Hit Settings**: Extra hitstun slider (-15 to +60F), minimum startup threshold slider (0 to 60F), move filter (Both, Normals, Specials), camera mode, and meter reward.
  * **Fatal Punish Settings**: Extra hitstun slider (-15 to +60F), minimum startup threshold slider (0 to 60F), move filter, camera mode, and meter reward.
  * **Mutual Trade Settings**: Camera mode, FOV offset slider, hold duration slider (5 to 120 frames), and meter reward for both fighters.
  * **Dynamic Camera Modes**:
    * *Option 1*: Dynamic FOV Zoom on impact.
    * *Option 2*: Angled Over-the-Shoulder perspective (lateral Shift Y towards attacker + Yaw angle towards defender).
    * *Slow-Motion*: Toggleable slow-mo playback rate (0.10x to 1.00x speed) during camera impact transitions.
    * *Smoothing*: Independent Move-In and Move-Out camera speed sliders.
  * **Hit Outcome Pop-up Badges (HUD)**:
    * Displays stylized animated badges below the combo counter: `COUNTER HIT` (Amber Gold), `FATAL PUNISH` (Crimson Pulse), and `TRADE HIT` (Electric Cyan).
    * Test preview buttons to audition all badges and camera effects on P1 or P2.
    * Customizable X/Y screen position sliders and reset button.
* **On-Screen Move ID & Move Name Telemetry**:
  * Shows real-time move name and action state hex ID (e.g. `[0x5200]`) directly above each player's super meter dial.
  * Customizable X/Y positioning offset sliders.
* **Knockdown & Wakeup Recovery Options**:
  * Target Scope: Both Players, P1 Only, P2 Only.
  * **Disable Wakeup Attack on Wakeup Roll**: Suppresses wakeup attacks and the "WAKE UP ATTACK" banner when recovering from a tech roll; allows standard no-roll wakeup attacks. Displays live blocked roll wakeup check counters.
  * **Disable Delayed Wakeup**: Suppresses holding Down; characters always wake up on immediate timing (~32-38F).
  * **Delayed Wakeup Fixed Amount of Hold**: Locks the player into a committed ground duration (30F to 96F) rather than variable release timing.
  * **Disable Delayed Wakeup Roll**: Suppresses the backward roll when exiting delayed wakeup; characters always rise stationary in place.
* **Move Priority Hierarchy**:
  * Selectable combat priority rules:
    * Uppercut (D+2) Anti-Air Priority: D+2 cleanly beats incoming jump-in attacks during active frames.
    * Uppercut vs Special: D+2 cleanly beats grounded specials during clash frames.
    * Uppercut vs Uppercut: Later inputted D+2 wins by default.
    * Jump-In Attacks: Jump-ins trade or cleanly beat grounded standing normals.
    * X-Ray Absolute Priority: X-Rays cleanly overpower all incoming normal and special strikes.
    * Instant Projectile Impacts: Projectiles deal immediate contact reactions without holding or passthrough glitches.
  * Quick Presets: **All OFF (Default Standard)** and **All ON (Full Priority Overrides)**.
* **Juggle Gravity & Limits**:
  * Scope: Both, Player 1, Player 2 (applies to the fighter receiving the juggle; normal jumps retain native physics).
  * Custom Juggle Gravity: Floatiness slider (0.25x to 2.00x).
  * Deteriorate on Follow-up Hits: Lowers floatiness per additional air hit (delta slider: 0.01 to 0.30).
  * Bypass Native Reaction Limits: Bypasses native count-based juggle caps while preserving authentic combo counters.
  * Live Telemetry: Height Z, Juggle Hit Count, Motion Scale, State, Launches Modified, and Bypasses counters.
  * Quick Presets: **Native**, **Floaty 1.50x**, **Heavy 0.50x**, **Deterioration**.
* **Free Camera & Cinematic Controls (Hotkey: `-`)**:
  * Detaches the camera from the 2.5D combat plane into full 3D free-flight space.
  * Quick Presets: **Reset to Game View**, **P1 Close-Up**, **P2 Close-Up**, **Top-Down View**, **Cinematic Profile**.
  * Manual Controls: Drag sliders for Position (X, Y, Z) and Rotation (Pitch, Yaw, Roll) with live degree readouts.
  * Field of View (FOV) override slider (20° to 140°).
  * Sensitivity & Speed: Fly Speed, Shift Boost multiplier (1.5x to 8x), Alt Slow Crawl (0.05x to 0.5x), and Mouse Sensitivity.
  * Flight Options: Hold RMB to Look, Always Lock Mouse, Invert Pitch, Invert Yaw, Gamepad dual-stick flight support, Update while paused.

---

### Tab 6: Performance & Telemetry

* **Match Loading Landscape & File I/O Diagnostics**:
  * **Fast Match Loading Toggle**: Bypasses artificial 15ms worker thread sleep yields during loading screens, allowing package deserialization to proceed at maximum PC hardware speed.
  * Live timing breakdown: Actual VS screen duration, total select & load time, package count and observed MB read, synchronous disk throughput (MB/s), and aggregate worker sleep duration.
* **Live Rendering & Callback Timings**:
  * Render FPS and frame time in ms.
  * Sum of category averages in microseconds ($\mu$s) with color-coded performance budget.
* **Hardware Micro-Profiler**:
  * Real-time per-subsystem microsecond execution breakdown table.
  * Displays subsystem name, average execution time ($\mu$s), peak spike ($\mu$s), call count, and budget status badge (`[<100us]`, `[<400us]`, `[<1ms]`, `[>=1ms]`).
  * "Reset Peak Spikes" button.
* **Diagnostic Test Bot Suites (Modes 1 to 20)**:
  * Selectable via dropdown or cycled in-game using **Shift + F9**. Executes automated synthetic inputs via function keys (**F1** to **F8**):
    * *Mode 1*: Normal Timing (0f, +1f, +2f FP)
    * *Mode 2*: Weapons (`b+FP`, `b+BP`, `f+FP`, `f+BP`)
    * *Mode 3*: Projectiles 1 (`D, B, 1..4` / `D, F, 1..3` / `D, U, 2`)
    * *Mode 4*: Projectiles 2 (`B, F, 1..4` / `B, B, 1..4`)
    * *Mode 5*: Projectiles 3 (`F, F, 3..4` / `D, F, B, 2` / `D, B, F, 1..3` / `B, B, F, 1, 4`)
    * *Mode 6*: Projectiles 4 (`U, D, 1..4` / `D, U, 1, 3, 4`)
    * *Mode 7*: Scorpion vs Sub-Zero (Specials vs Normals / Jump-In vs D+2)
    * *Mode 8*: Release Check & Buffer (Negative Edge vs Downstroke / Hit-Confirm)
    * *Mode 9*: Dash & Projectiles (FP Recovery, Dash, Freeze, JC D+FP Buffers)
    * *Mode 10*: EX Forceball $\rightarrow$ F+3 vs P1 EX (Reversal Startup / Armor Priority)
    * *Mode 11*: EX Forceball $\rightarrow$ F+3 vs P1 D,F+1+BL (Unarmored EX Reversal)
    * *Mode 12*: Multi-Device Input Bleed & Mashing Isolation (Stress Test)
    * *Mode 13*: Quick Roll (Tech Roll Window Identification)
    * *Mode 14*: Delayed Wakeup (Floor `0x460D` Buffer Window Prober)
    * *Mode 15*: Blockstun Diagnostic (Priority Pinpoint F1 - F4)
    * *Mode 16*: Blockstun Distance & Pushback Measurement (F1 - F4)
    * *Mode 17*: Attack Frame Data Measurement (F1: FP, F2: BP, F3: FK, F4: BK)
    * *Mode 18*: Normal Cancel Diagnostic (F1: Spear, F2: EX Spear, F3: Dash Fwd, F4: Backdash, F5: X-Ray)
    * *Mode 19*: Kabal Bug Diagnostic (F1: F+4 $\rightarrow$ F+4 Loop, F2: 1,1,1 $\rightarrow$ NDC $\rightarrow$ 1 Link)
    * *Mode 20*: Crouching Normal Diagnostics (F1: D+1, F2: D+2 Uppercut, F3: D+3, F4: D+4)

---

### Tab 7: Console Logs

* Real-time color-coded combat logging (Crash: Red, Trade: Cyan, Hit/Damage: Orange, Display: Green, Diagnostic/Bot: Yellow, Subsystems: Purple).
* Search filter bar to quickly query specific frames, function names, or tags.
* Action buttons: **Clear**, **Copy All** (copies full log to Windows clipboard), **Auto-Scroll**, and **Enable Logging** toggle.

---

## Floating In-Game HUDs

| HUD | Description | How to Toggle |
|---|---|---|
| **Frame Data Timeline** | 75-frame visual block timeline for P1 & P2 showing startup, active, recovery, advantage, and hover tooltips. Draggable. | **F9** or View Menu |
| **Practice Dummy Recorder HUD** | Compact floating overlay showing recording/playback countdown, frame progress bars, and stop/start buttons. | Tab 2 button or auto on record |
| **Practice Dummy Stance HUD** | Floating palette for one-click dummy posture switching (Stand, Crouch, Stand Block, Crouch Block, Auto Block). | Tab 3 button |
| **Super Meter Percentage HUD** | High-visibility 1-decimal percentage readouts (`XX.X%`) with color-coded threshold readiness beside each meter dial. | Tab 5 checkbox |
| **Move ID & Move Name Telemetry HUD** | Real-time move name and action state hex ID badge (e.g. `[0x5200]`) above each player's super meter dial. | Tab 5 checkbox |
| **Hit Outcome Pop-up Badges** | Dynamic animated banners below combo counters displaying `COUNTER HIT`, `FATAL PUNISH`, and `TRADE HIT`. | Tab 5 checkbox |

---

## Quick Controls & Keybindings Reference

### Global Simulation & Interface Hotkeys

| Key | Action |
|---|---|
| **`~`** (Tilde) | Open / close Dear ImGui console overlay |
| **`\`** (Backslash) | Pause / resume game simulation (freezes combat tick while UI & camera stay interactive) |
| **`=`** (Equals) | Step advance exactly one simulation tick forward while simulation is paused |
| **`F9`** | Toggle Frame Data Timeline Bar |
| **`F10`** | Cycle window display mode (Borderless Windowed $\rightarrow$ Windowed $\rightarrow$ Fullscreen) |
| **`-`** (Main Keyboard Minus) | Toggle 3D Free Camera mode |
| **`Shift + F9`** | Cycle active Diagnostic Test Bot Mode (Modes 1 to 20) |
| **`F1` – `F8`** | Trigger active Diagnostic Test Bot macro injections |
| **`F11`** | Quick toggle Physical Trade Engine hooks (Enabled / Disabled) |

### Practice Mode Quick Directional Resets

Works on both gamepad (`Back` / `Select`) and keyboard (P1 Select: **`4`** on number row or `Backspace`; P2 Select: **`Numpad -`** / `VK_SUBTRACT`):

| Input Combination | Reset Placement |
|---|---|
| **Select Alone (Neutral)** | Standard Center (`P1: -120.0f, P2: +120.0f`) |
| **Left + Select** (`A` / Left Arrow) | Left Corner Wall (`P1: -1140.0f, P2: -1300.0f`) |
| **Down + Left + Select** (`S + A` / Down + Left Arrow) | Left Corner Swapped (`P1: -1300.0f, P2: -1140.0f`) |
| **Right + Select** (`D` / Right Arrow) | Right Corner Wall (`P1: +1140.0f, P2: +1300.0f`) |
| **Down + Right + Select** (`S + D` / Down + Right Arrow) | Right Corner Swapped (`P1: +1300.0f, P2: +1140.0f`) |
| **Down + Select** (`S` / Down Arrow) | Center Swapped (`P1: +120.0f, P2: -120.0f`) |

### Free Camera Flight Controls

| Control | Action |
|---|---|
| **`W` / `A` / `S` / `D`** | Fly Forward / Strafe Left / Fly Backward / Strafe Right |
| **`Space`** or **`Q`** | Ascend vertically |
| **`Ctrl`**, **`E`**, or **`C`** | Descend vertically |
| **Hold `RMB` + Mouse** | Look around (Pitch & Yaw) |
| **Hold `Shift`** | Speed Boost multiplier (1.5x to 8x) |
| **Hold `Alt`** | Precision Crawl multiplier (0.05x to 0.5x) |
| **`Ctrl` + Mouse Wheel** | Live Field of View (FOV) zoom |
| **`Shift` + Mouse Wheel** | Live flight speed adjustment |
| **Gamepad Dual Sticks** | Left Stick moves; Right Stick looks; `RT` / `LT` ascend / descend |

---

## What's New in v0.6710a

* **Stage Wall Placement Hotfix**: Corrects floor-grid wall placement on *The Street* by using native projected stage limits instead of assuming spawn offset equals half the arena width. Gameplay collision and boundaries remain authentic. [Hotfix details and validation](docs/STAGE_WALL_HOTFIX.md).

## Included v0.6710 Changes

* Consolidates Codex-assisted fixes for string and crouching frame data telemetry, eliminating false wakeup and counter/punish badges.
* Juggle gravity deterioration and count-based reaction limit bypass.
* Stage Void Mode D3D9 filtering with strict translucent VFX destination blend checks (`destBlend > D3DBLEND_ZERO`), preserving projectiles, energy beams, and fireballs.
* Clean Fighters mode suppressing blood, sprays, smears, pools, and mesh damage.
* Quick-roll wakeup restriction ("Disable Wakeup Attack on Wakeup Roll").
* Fixed Jade's Bojutsu staff lifetime bug, ensuring detached visual props are cleanly hidden upon hit interrupt.
* Automatic startup logo, legal, and vendor screen skip while keeping save loading intact.
* Dan Forden Toasty lifecycle fixes, installing patches cleanly once and eliminating false-positive triggers from special moves.
* Fast match loading option bypassing thread sleep yields.

---

## Installation & Uninstallation

1. Close Mortal Kombat Komplete Edition and back up any existing `dinput8.dll` in your game's `DiscContentPC` directory.
2. Download and extract the release ZIP archive.
3. Copy the extracted `dinput8.dll` directly beside `MKKE.exe` in:
   ```text
   ...\Mortal Kombat Komplete Edition\DiscContentPC\
   ```
4. Launch the game through Steam or `MKKE.exe`. Check that the console overlay opens with **`~`** and displays **v0.6710a**.

**Uninstallation**: Simply close the game and delete or rename `dinput8.dll` from `DiscContentPC`.

**Compatibility**: Requires Windows x86 *Mortal Kombat Komplete Edition*. Supported executable SHA-256:  
`D1C71E086DEAC26716E48F060E486679A36DDD4002AB5080D66DF9D11437D0E6`

---

## Building from Source

The complete C++ source code is included in the release package under `source/`.

* **Prerequisites**: Visual Studio 2022 Community with the **Desktop development with C++** workload (MSVC v143 x86 toolset).
* **Build Command**: Execute `build_x86.bat` from the root directory. Output binaries (`dinput8.dll`, `dinput8.pdb`) are compiled into `bin/`.
* **Regression Tests**: Run `tests/run_tests.bat "PATH_TO_MKKE.exe"` to execute the automated offline test suite.

---

## Credits & Third-Party Notices

* Created by **Fai Khozen**.
* Development and reverse engineering conducted with AI assistance, including Gemini and OpenAI Codex.
* **Dear ImGui**: Copyright (c) 2014-2024 Omar Cornut (MIT License).
* **MinHook**: Copyright (C) 2009-2017 Tsuda Kageyu (BSD 2-Clause License).
* **Hacker Disassembler Engine 32 C (HDE32)**: Copyright (c) 2008-2009 Vyacheslav Patkov.
