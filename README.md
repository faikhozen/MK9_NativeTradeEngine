# Mortal Kombat 9 (2011) — Native Physical Trade Engine Mod

**Version:** 0.67 (Release Candidate)  
**Author:** Fai Khozen  
**Platform:** PC (Steam / Mortal Kombat: Komplete Edition)


Donations are pretty much appreciated  
https://www.paypal.com/paypalme/faikhozen



---

## 🥊 What Is This Mod? (In Plain English / Layman's Terms)

### ❓ What is a "Trade" in Fighting Games?
In most fighting games (like *Street Fighter*, *Tekken*, or *Virtua Fighter*), if two players attack each other at the same time, **both hits connect**. Player 1 punches Player 2, and Player 2 kicks Player 1 — **both characters take damage, both get knocked back, and both play their proper hit animations**. This is called a **Trade**.

---

### ❌ The Problem in Original Vanilla MK9:
In vanilla *Mortal Kombat 9* (2011), **trades were virtually impossible**.

The game had a hidden code rule: **the exact split-second a hit lands, it instantly deletes the opponent's attack from memory.**

* **The "Ghost Hit" Problem:** If Player 2 threw a giant, slow heavy kick, and Player 1 threw a quick jab at the same time, Player 1's jab would connect. The moment Player 1's jab connected, **Player 2's kick instantly vanished into thin air** — even if Player 2's foot was visibly passing straight through Player 1's face on screen!
* **Port Priority / Unfair Advantages:** In close clashes, the game would arbitrarily pick one player (often Player 1) to win the exchange 100% of the time, while the other player's attack simply stopped existing without doing any damage.

---

### ✅ What This Mod Does:
This mod **removes that deletion rule** and brings true fighting-game trade physics to Mortal Kombat 9:

1. **Both Attacks Connect Naturally:** If both fighters have attacks out in the air that touch each other, **both hits land**.
2. **Accurate Asymmetrical Damage & Reactions:**
   * If you hit your opponent with a **Light Punch (3% damage)**, they will take a Light Punch reaction.
   * If they hit you with a **Heavy Kick (5% damage)** at the same time, you will take a Heavy Kick reaction.
   * No more mirrored/glitched reactions.
3. **Clean Counter-Hits Are Preserved:** If you catch an opponent during their *startup* (before their attack has actually extended its hitboxes), you will still cleanly interrupt them with a Counter-Hit, just like in standard fighting games.
4. **Works for ALL 300+ Moves:** Automatically works for every character, normal attack, special move (fireballs, spears, freezes), EX attack, kombo, sweep, and X-Ray with **zero hardcoded lists**.
5. **Zero Lag & Zero Overhead:** Runs seamlessly in memory with no extra load times and zero disk lag.

---

## 🎮 Gameplay Examples

| Scenario | Vanilla MK9 (Original) | With This Mod (v0.67) |
| :--- | :--- | :--- |
| **Simultaneous Light Punches** | One player wins randomly; other player takes full damage for nothing | **Both players punch each other, take 3% damage, and stagger** |
| **Delayed Spacing Trade (Jab vs. Heavy Kick)** | Light punch connects; heavy kick disappears into thin air ("Ghost Whiff") | **Both hits land! P2 takes 3% damage (Jab), P1 takes 5% damage (Heavy Kick)** |
| **Fast Move vs. Slow Move (During Startup)** | Fast move interrupts slow move | **Fast move cleanly interrupts slow move (Counter Hit)** |
| **Special Move Clashes (e.g., Slides / Charges)** | One player completely ignores the other | **Both players clash and hit each other simultaneously** |

---

## 📥 Installation Guide (Super Easy 1-Step)

### Step 1: Copy the File
Copy the file **`dinput8.dll`** into your Mortal Kombat Komplete Edition game folder:

> **Default Steam Path:**  
> `V:\SteamLibrary\steamapps\common\MortalKombat_KompleteEdition\DiscContentPC\`  
> *(or wherever your Steam library is located: `C:\Program Files (x86)\Steam\steamapps\common\...`)*

### Step 2: Play the Game!
Launch Mortal Kombat 9 through Steam as usual. That's it!

---

## ⌨️ In-Game Controls & Features

* **Live Status Window:** A clean black console window will open alongside the game confirming the mod is active:
  ```text
  ====================================================================
   Mortal Kombat 9 - Native Physical Trade Engine v0.67               
   Author: Fai Khozen                                                 
   100% Dynamic Native Move Resolution across ALL 300+ Moves          
   Hotkey: [F11] Toggle Native Trade Fix (Status: ENABLED)            
  ====================================================================
  [TRADE #1] Physical Clash! Preserved active attack for P2
  [HIT #1] P1 hit P2 (Damage: 3.0%)
  [HIT #2] P2 hit P1 (Damage: 5.0%)
  ```
* **Toggle Key (`F11`):** Press **`F11`** on your keyboard anytime during gameplay to toggle the trade engine **ON** or **OFF** in real time.

---

## 🗑️ How to Uninstall
Simply delete `dinput8.dll` from your `DiscContentPC` folder. Your game will return 100% to its original vanilla state.

---

## 📜 Credits & Reverse Engineering
* **Author:** Fai Khozen
* **Engine Architecture:** For full technical disassembly and reverse engineering documentation, see [`docs/MK9_TRADE_ENGINE_DECONSTRUCTION.md`](file:///c:/Users/casan/Documents/github/MK9_RE/docs/MK9_TRADE_ENGINE_DECONSTRUCTION.md).
