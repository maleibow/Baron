# The Adventures of Baron Munchausen (Genesis / Mega Drive)

SGDK project. Rescue companions, fight the Sultan's guards, reach the exit. 4 rounds.

## Build
1. Install SGDK 2.x (github.com/Stephane-D/SGDK) and Java. Set the `GDK` environment variable to the SGDK folder.
2. In this folder run `build.bat` (Windows) or `./build.sh` (Mac/Linux).
3. Open `out/rom.bin` in an emulator (BlastEm recommended).

## Build online (no install): GitHub Actions
1. Create a free GitHub account and a new repository (private is fine).
2. Put the contents of this folder at the repository root, so `.github/workflows/build.yml`, `src/`, `res/` and `inc/` sit at the top level. The `.github` folder is hidden, so if drag-and-drop skips it, use Add file > Create new file, type `.github/workflows/build.yml` as the name and paste the workflow in.
3. Open the Actions tab. The build starts on every push, or press Run workflow.
4. When the run finishes (a few minutes), open it and download `baron-munchausen-rom` under Artifacts. Unzip it to get `rom.bin`.
5. Load `rom.bin` in an emulator. A red run means a compile error: open the failed step and read the log.

## Controls
D-pad move - B or C attack - A swap companion - START begin/continue.

## Companions
Baron: balanced pistol. Albrecht: slow, heavy melee. Berthold: very fast, short-range shots.
Adolphus: long-range piercing shot. Gustavus: gust that knocks enemies back and clears bullets.

## Project layout
- `src/main.c`      all game code
- `inc/palettes.h`  generated palette data (colours in the PNG order)
- `res/`            sprite/tile PNGs + `resources.res`
- `preview.png`     what the art looks like

## Palette use
PAL0 round tileset - PAL1 active party member (swapped when you press A) - PAL2 enemies, captives and effects - PAL3 text.

## Not yet tested on a real build
This was written without access to the SGDK compiler. If the build stops on an error, the likely spots are:
- `SPR_setAutoAnimation(...)` in `mk()`: delete the line if your SGDK version lacks it.
- `main(bool hardReset)`: use `int main()` on older SGDK.
- `TILESET ... NONE NONE` in `res/resources.res`: the final NONE must keep tiles in image order (no de-duplication) because the map code indexes them by position.
- `BG_A`/`BG_B` names (older SGDK used `PLAN_A`/`PLAN_B`).
No sound or music yet; add XGM tracks via `XGM` entries in resources.res if you want them.
