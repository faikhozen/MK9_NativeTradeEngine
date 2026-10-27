# MK9 Native Physical Trade Engine

**Mortal Kombat Komplete Edition · Windows PC / Steam · v0.6710a**  
**By Fai Khozen** · [Support on Ko-fi](https://ko-fi.com/faikhozen) · [Reports / Contact](https://x.com/faikhozen)

A gameplay mod and practice toolkit for Mortal Kombat Komplete Edition. It combines physical attack trades, configurable combat rules, dummy recording and reversals, frame-data displays, collision visualization, and stage capture tools in an in-game overlay.

[Revision history](docs/CODEX_CHANGELOG.md) · [Release validation](docs/PUBLIC_RELEASE_v06710.md)

## Features

### Gameplay fixes and combat customization

- **Physical Trade Priority Fix:** enables physical normal-attack trades rather than resolving every clash through one-sided priority.
- **Input fixes:** ducking-normal buffer fix, jump-input threshold fix, P1/P2 input isolation, and separate negative-edge toggles.
- **Meter controls:** meter-drain fix; configurable EX, Breaker and X-Ray costs; optional special, throw and cancel costs; attacker/defender meter gains and on-screen meter percentages.
- **Combat rules:** configurable hitstun, blockstun, block pushback and corner reflection; attack startup/active/recovery modifiers; move-priority options.
- **Counter Hit / Fatal Punish / Trade:** optional hitstun extensions, meter rewards and outcome badges. Counter hits identify attack interrupts; fatal punishes identify recovery punishes. Startup thresholds and other selected rules determine eligibility.
- **Dramatic Mode:** quick presets for combat modifiers and impact camera effects, including Cam 1 FOV and Cam 2 angled camera settings. The later experimental camera redesigns were shelved; v0.6710a retains the earlier camera behavior.
- **Juggle Gravity & Limits:** receiving-player scope, Native / Floaty / Heavy settings, per-hit deterioration and count-based juggle-limit bypass. Normal jumps retain native behavior.
- **Knockdown & wakeup options:** quick-roll and delayed-wakeup controls, fixed delayed-wakeup hold, and **Disable Wakeup Attack on Wakeup Roll**. The roll restriction suppresses both the restricted attack and its native Wakeup Attack banner while allowing eligible no-roll wakeup attacks.

### Practice recording and reversals

- Record and replay Player 2 inputs using practice dummy slots; import/export recordings as `.mkr` files.
- Recording/playback requires an active Practice match with the native dummy set to **Human**. Keyboard capture uses P2's default arrow/numpad controls.
- Assign the reversal target to P1 or P2; configure **Invincible Wake-up**, **Regular Wake-up**, **Block Reversal**, and **Reversal After Damage** actions.
- Configure reversal actions, weights, hold times and delays, with quick-roll and delayed-wakeup recovery options. Regular Wake-up executes after neutral recovery with one additional frame beyond the selected delay.
- Dummy standing/crouching, guard and auto-block controls; Normal / Full / Refill super-meter modes.
- Directional practice position resets, simulation pause/frame advance, and diagnostic test macros for repeatable labbing.

### Frame data and collision display

- **Frame-data timeline (F9):** live startup, active and recovery phases, total frames and advantage, including follow-up string segments and observed weapon activity.
- **Attack hitboxes:** red circles. **Body hurtboxes:** green/yellow circles. **Pushboxes:** cyan circles.
- Optional move-name/action-ID telemetry and hit-outcome popups.

These displays use live engine observations and fallback estimates. They are practice references; complex cancels, strings and character-specific behavior can still produce discrepancies.

### Visuals, capture and display

- **Void Mode:** hide stage geometry and choose a background color, including black, green-screen, blue-screen and magenta presets.
- Separate options hide **stage characters/animated props** and **stage ambient FX/translucent meshes**. Enable both alongside Void Mode for the broader stage cleanup. Reported fixes cover Cemetery, beach water, Courtyard, Hell, Desert, Pit and Temple scenery/effects while preserving the tested fighter projectiles and FX.
- Colored perspective floor grid, center lines and stage-corner boundaries.
- **Clean fighters:** suppress blood and cosmetic battle damage during the match. Health damage and reactions still apply.
- Character lighting preservation through practice entry/reset, Scorpion Teleport Punch and Shang Tsung Soul Steal while using Void Mode.
- Free camera, display-mode switching, and optional 4:3/non-16:9 resolution unlocks.

### Toasty, character fixes and utilities

- **100% Uppercut Toasty:** qualifying D+2 hits trigger Toasty when no cameo is already visible. The current cameo finishes its native slide-out before another starts; it does not restart on every rapid hit.
- Toasty corner/size controls, optional random sizing, and a character Toasty Boost input reference. The cameo retains its native reward-input window.
- **Jade staff fix:** interrupted staff attacks no longer permanently lose the staff; detached temporary staff/glow visuals are hidden promptly.
- Startup logo/legal/vendor screens are skipped while save loading and its notice remain intact.
- Four user preset slots with save/load and INI import/export, ASI plugin loading, console logging, loading diagnostics and an optional hardware micro-profiler. Profiling starts **OFF**.

## Source code

The release ZIP includes the matching C++ source, tests, build script, and SHA-256 source manifest in source/.

## Installation

1. Close MKKE and back up any existing `dinput8.dll` in `DiscContentPC`.
2. Download and extract the release ZIP.
3. Copy its `dinput8.dll` beside `MKKE.exe` in the game's `DiscContentPC` folder.
4. Launch the game and check that the overlay shows **v0.6710a**.

The package does not replace saves, recordings or existing configuration. Existing settings can affect which options are enabled. If another mod uses `dinput8.dll`, resolve the loader conflict before replacing it. To uninstall, close the game and restore the previous DLL, or remove this DLL if none was previously installed.

**Compatibility:** Windows x86 Mortal Kombat Komplete Edition. Supported executable SHA-256: `D1C71E086DEAC26716E48F060E486679A36DDD4002AB5080D66DF9D11437D0E6`. Other executable versions and overlapping patches may prevent initialization. Hot unloading is unsupported.

## Quick controls

| Key | Action |
|---|---|
| `~` | Open / close overlay |
| `\` | Pause / resume simulation |
| `=` | Advance one simulation tick while paused |
| `F9` | Toggle frame-data timeline |
| `F10` | Cycle display mode |
| Main keyboard `-` | Toggle free camera |

Diagnostic function-key actions depend on the selected test mode. Use the overlay's labels for the current bindings.

## What's new in v0.6710a

Corrects floor-grid wall placement on The Street by using the native projected stage limits instead of assuming that the spawn offset is half the arena width. Gameplay collision is unchanged. [Hotfix details and validation](docs/STAGE_WALL_HOTFIX.md).

## Included v0.6710 changes

This release consolidates the Codex-assisted fixes for string/crouching frame data, false wakeup and counter/punish labels, juggle controls, stage cleanup and lighting, clean fighters, quick-roll wakeup restrictions, Jade's staff, startup skips and Toasty's lifecycle.

The release optimization pass removes temporary Jade diagnostics, installs Toasty training patches once, and leaves optional profiling off until requested. No comparative FPS improvement is claimed.

The complete **Dev1–Dev19 history**, including superseded experiments, is in the [Codex changelog](docs/CODEX_CHANGELOG.md). Older feature notes are retained in the [historical README](docs/LEGACY_README_v06709.md); current behavior is described above.

## Validation and bug reports

The v0.6710a hotfix passed **952 automated checks** and a final user-driven practice smoke test. See the [verification record](docs/PUBLIC_RELEASE_v06710.md) and linked revision notes for scope. Coverage is not exhaustive across every character, costume, stage, plugin or match mode.

When reporting an issue, include the version, characters, stage, enabled options, input sequence, and whether it follows a reset or interruption.

## Building

Use VS2022 Community's x86 C++ toolchain and run `build_x86.bat`. Output is written to `bin`; deployment is separate. Run `tests/run_tests.bat "FULL_PATH_TO_MKKE.exe"` for the regression checks. Tests read the executable without launching it.

## Credits

Created by **Fai Khozen**. Development and reverse engineering used AI assistance, including Gemini and OpenAI Codex. Third-party notices accompany the release.



