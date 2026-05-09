# Super Mario Bros — Enhanced Edition

## Description

Super Mario Bros — Enhanced Edition is a polished 2D platformer for Windows, written in C with the Raylib library. It revives the classic side‑scrolling Mario experience with modern conveniences: three distinct themed worlds, a fully **procedural audio engine** (no external sound files needed), a robust save/load system with file‑integrity checks, animated toast notifications, particle effects, and a clean main menu / pause / level‑select flow. All gameplay rules follow classic Mario tradition — jump on enemies to defeat them, avoid spikes, collect coins, hit `?` blocks, and reach the goal flag to clear the level.

This project was developed as the **CSE1200 Structured Programming Project (Group 3)**.

## Technology Stack

- **Language:** C (C99)

- **Graphics & Input:** [Raylib](https://www.raylib.com/) (single-header style game library)

- **Audio:** Procedural PCM synthesis via Raylib's `Wave` / `Sound` APIs — *no external `.wav` or `.ogg` files required*

- **Build target:** Windows (Code::Blocks / MinGW‑w64), but cross‑compatible with any Raylib‑supported platform (Linux, macOS)

- **Architecture:**

  - Scene‑based state machine (Menu, Options, Level Select, Game, Pause, Level Complete, Game Over, Game Win)

  - AABB collision (player vs platforms, pipes, Q‑blocks, enemies, coins)

  - Frame‑based fixed-step physics (gravity = 0.55, jump impulse = −11.5, max fall speed = 13)

  - 80‑slot particle pool for jumps, coins, stomps, and deaths

  - Versioned binary save format with 4‑byte magic number `'MRBO'` + checksum for corruption detection

## Features

- **Modern menu UI**

  - Animated starfield background

  - Main menu, Level Select, Options, Pause, Level Complete, Game Over, and Game Win screens

  - Color‑coded level cards (green grasslands, orange desert, blue night)

  - Lock indicator for levels that haven’t been unlocked yet

- **Three distinct themed levels**

  1. **Grasslands** — classic overworld vibe with bright skies

  2. **Desert Sunset** — orange dunes and warm tones

  3. **Night Mountains** — minor‑key atmosphere with twinkling stars

  - Each level has its own background art, layout, and BGM track

- **Polished gameplay mechanics**

  - Smooth left/right movement with directional sprite flipping

  - Variable‑height jumping with proper ground detection

  - **Goombas** with patrolling AI, stomp‑defeat animation, and squash timer

  - **Spikes** and bottomless pits cause death

  - **Moving platforms** (horizontal and vertical) with sticky riding behavior

  - **`?` blocks** that pop coins on hit (visual bump animation)

  - **Coins** with collection particles and score increment

  - **Goal flag** ends the level

  - 5 starting lives, 300‑second per‑level timer, time bonus on level clear

- **Procedural Audio Engine**

  - **11 sound effects** synthesized at startup: jump, coin, Q‑block, stomp, die, level‑clear, game‑over, save‑chime, menu‑move, menu‑select, flag‑get

  - **5 background music tracks** with seamless looping: title theme, Level 1 (overworld), Level 2 (desert variation), Level 3 (minor‑key night), and a win fanfare

  - Tiny BGM state machine handles transitions and loop re‑triggering

  - Master volume honors Music / SFX toggles in Options

- **Robust Save / Load System**

  - Save file (`savegame.dat`) uses a **4‑byte magic number** (`'MRBO'`) + version field + UNIX timestamp + checksum

  - Corrupt or foreign files are rejected cleanly instead of silently breaking the menu

  - Pause menu has a **SAVE GAME** option with animated confirmation toast

  - **F5** quick‑saves anywhere during gameplay

  - **Load Game** menu entry shows `LOADED LEVEL X` toast on success or `NO SAVE FILE FOUND` on miss

- **Toast Notification System**

  - Animated, fading on‑screen messages

  - Slides in from the top, holds, fades out

  - Used for save/load feedback and gameplay hints

- **Options screen**

  - Music ON / OFF

  - SFX ON / OFF

  - Difficulty: EASY / NORMAL / HARD

## How to Play

### Starting the Game

JUST DOWNLOAD THE ENTIRE REPOSITORY & PLAY THE GAME BY LAUNCHING `super_mario.exe`.
[After downloading the source, build it with `make` from the `src/` directory — see [Setup & Build](#setup--build) below.]

### Main Menu

- **NEW GAME** — start a fresh run from Level 1 with 5 lives

- **LOAD GAME** — resume from your last saved level (shows toast feedback)

- **OPTIONS** — toggle music, SFX, and difficulty

- **QUIT** — exit the game

Press **`L`** from the main menu to jump straight to Level Select (any unlocked level).

### Controls

- **Movement:** `→` / `D` to move right, `←` / `A` to move left.

  All movement is responsive with directional sprite flipping.

- **Jump:** `Space`, `↑`, or `W` (only when grounded).

- **Pause:** `P` or `Esc`.

- **Quick‑save:** `F5` during gameplay.

- **Keyboard shortcuts:**

  `Enter` – confirm in any menu

  - `Esc` – go back to previous menu

  - `↑` / `↓` – navigate menu items

  - `←` / `→` – change values (Options screen)

  - `M` – return to main menu (on Level Complete screen)

### Gameplay Rules

When playing through a level:

- A pawn-like rule: jump **on top** of Goombas to defeat them and bounce away.

- Touching a Goomba from the side = lose a life.

- Spikes and bottomless pits = lose a life.

- Collect coins for **+200 score** each.

- Hit `?` blocks **from below** to pop a coin.

- Reach the **goal flag** to clear the level and earn a time bonus.

- Lose all 5 lives → Game Over. Clear all 3 levels → You Win!

### Pause Menu

While paused, you can:

- **RESUME** — return to gameplay

- **SAVE GAME** — write progress to `savegame.dat` (animated chime + toast confirmation)

- **MAIN MENU** — abandon current run and return to title

## Setup & Build

### Prerequisites

- **OS:** Windows 7 or later (also builds on Linux and macOS)

- **Compiler:** MinGW‑w64 / GCC (or any C99‑compatible toolchain)

- **Library:**

  - [Raylib](https://www.raylib.com/) 4.0 or newer

  Ensure Raylib’s headers and libraries are installed and visible to your compiler.

### Building from Source (MinGW / make)

From the `src` directory (where `makefile` lives):

```
make
```

This will compile `super_mario.c` into `super_mario.exe` (or `super_mario` on Linux/macOS).

To build and run in one step:

```
make run
```

To clean the build:

```
make clean
```

### Building manually (without make)

If you don’t have `make`, you can compile directly:

**Windows (MinGW):**
```
gcc super_mario.c -o super_mario.exe -O2 -Wall -lraylib -lopengl32 -lgdi32 -lwinmm
```

**Linux:**
```
gcc super_mario.c -o super_mario -O2 -Wall -lraylib -lGL -lm -lpthread -ldl -lrt -lX11
```

### Running

From the same directory as `super_mario.exe`:

```
./super_mario.exe
```

or just double‑click `super_mario.exe` in Explorer.

## Repository Structure (Key Files)

- `src/super_mario.c` – Entire game in a single, well‑commented translation unit:

  - **Audio Engine** — procedural PCM wave synthesis, BGM state machine, SFX dispatch

  - **File Handler** — versioned save/load with magic number and checksum validation

  - **Toast System** — animated on‑screen notifications

  - **Game Logic** — physics, AABB collision, enemy AI, scoring, level data

  - **Renderer** — pixel‑style player, enemies, particles, parallax backgrounds

  - **Scene Manager** — main menu, options, level select, pause, game over, win

- `src/makefile` – Cross‑platform build script (Windows / Linux / macOS)

- `README.md` – This file

- `LICENSE` – MIT license

- `PORTABLE_PACKAGE.txt` – Notes on portable distribution

- `.gitignore` – Files Git should never upload (build artifacts, save files, IDE clutter)

## Game Constants (Quick Reference)

| Constant | Value | Description |
|---|---|---|
| `SW` × `SH` | 960 × 540 | Window resolution |
| `PW` × `PH` | 26 × 38 | Player hitbox |
| `G` | 0.55 | Gravity per frame |
| `JMP` | −11.5 | Jump impulse |
| `BNC` | −7.5 | Bounce after stomp |
| `SPD` | 4.5 | Run speed |
| `MAXF` | 13.0 | Terminal fall speed |
| Lives (start) | 5 | |
| Time per level | 300 | seconds |
| Target FPS | 60 | |

## Credits

- **Course:** CSE1200 — Structured Programming

- **Group:** 3 Taufiq E Elahi(250221)
- **Dedicated to** Late Hasina(Grandmother of Taufiq E Elahi)

- **Engine library:** [Raylib](https://www.raylib.com/) by Ramon Santamaria

## License

This project is open source under the [MIT License](LICENSE). Feel free to modify and distribute.
