# ultima-assist

A browser build of *Ultima IV* (the open-source [xu4](https://github.com/xu4-engine/u4) engine compiled to WebAssembly) with an "assist mode" sidebar: live party and supplies, per-town guides, a quest checklist with next steps, and an automap of the places you've visited.

## What's here

- `engine/` — [xu4](https://github.com/xu4-engine/u4) at commit `6a7ee3d0079cfdc1c8fb9ba7a3c710a957155a71` (5 Jun 2025), vendored. `patches/xu4-emscripten.patch` is the diff against that commit for the files that already existed. Changes:
  - `src/gpu_opengl.h`, `src/gpu_opengl.cpp`: use `<GLES3/gl3.h>` and `#version 300 es` under `__EMSCRIPTEN__` (WebGL2).
  - `src/support/getTicks.c`, `src/screen_glfw.cpp`: `emscripten_sleep` in `msecSleep()` and the GLFW event poll, so the blocking game loop yields to the browser (without it the tab freezes).
  - `src/screen_glfw.cpp`: GLFW callbacks only queue events under Emscripten; `handleInputEvents()` replays them on the main stack. xu4's handlers start nested blocking loops (e.g. the new-game name prompt), and `emscripten_sleep()` from a browser-driven callback while `main()` is already suspended by Asyncify corrupted memory. That was the "memory access out of bounds" crash.
  - `src/settings.cpp`: under Emscripten the save/settings directory is `/xu4home/`, which the page mounts as persistent browser storage.
  - New files: `src/assist.cpp` (`assist_*` exports that read the live savegame and location, plus heal/food/gold actions) and `src/sound_null.cpp` (silent sound backend; the real one needs Faun/Vorbis).
  - The legacy SDL backend (`screen_sdl.cpp`) is stale and not used.
- `deps/boron/` — [Boron](https://codeberg.org/wickedsmoke/boron) at commit `b0c1305e7b4ff11a0ee2aef3b97a2a95f4afe4b2` (LGPL), the scripting language xu4 embeds, vendored.
- `web/shell.html` — the page: `ultima4.zip` upload (validated, kept in IndexedDB), persistent saves, auto-start on later visits, the assist sidebar, "Forget game data".
- `web/guide-data.js` — town, dungeon, shrine and item coordinates, generated from the game's own `maps.b` by `python tools/gen-guide.py`.
- `build.sh` — the whole build (see below). `build-deps.sh`, `build-xu4.sh`, `link-xu4.sh` are its steps; `tools/emenv.sh` finds Emscripten.
- `NOTICE.md` — licences and credits; published with the site as `NOTICE.txt`.
- `.github/workflows/pages.yml` — GitHub Pages deployment.

## Building

```bash
# One-time: Emscripten SDK (~2 GB, not checked in)
git clone https://github.com/emscripten-core/emsdk.git tools/emsdk
cd tools/emsdk && python emsdk.py install 6.0.6 && python emsdk.py activate 6.0.6 && cd ../..

bash build.sh          # Boron -> game modules -> xu4 -> dist/  (about a minute)
cd dist && python -m http.server 8090   # open http://localhost:8090/index.html
```

`build.sh` runs Boron under Node to pack the game modules, so no native compiler is needed. `dist/` contains no game data. For quick local testing you can embed a zip with `bash link-xu4.sh <outdir> path/to/ultima4.zip`; never publish such a build. A debug build: `OUT=build-dbg OPT="-O0 -g" bash build-xu4.sh`, then `BUILD=build-dbg LDOPT="-O0 -g -sASSERTIONS=2" bash link-xu4.sh <outdir>`.

## Deployment (GitHub Pages)

Players supply their own `ultima4.zip` on first visit, either by choosing it or, if the relay is set up, with the opt-in "Download it for me" button. Nothing about the game data is stored on the server. The relay deploys with `.github/workflows/relay.yml`; set the repository variable `RELAY_URL` so the Pages build shows the button (steps in `relay/README.md`).

## Page behavior

First visit shows the `ultima4.zip` picker; bad files are rejected (not a zip, or no `AVATAR.EXE` inside). After that the zip and saves persist across reloads, the game starts straight away, and "Journey Onward" resumes the saved party.

## Assist sidebar

- **Where you are / Party / Supplies / Hints / Virtues / Quest items:** live from the `assist_*` exports.
- **Town guide:** in a town, castle or village it lists the services (healer, inn, shops, companion), and any rune or quest item marked there, with the direction from where you stand (for example "Rune of Honesty at (8, 6): 9 north, 7 east"; confirmed in-game by walking there and pressing S). Dungeons show their stone; shrines show the mantra.
- **Quest checklist:** runes, virtues (raise to 99, then meditate 3 cycles with the mantra), stones, the bell/book/candle/horn/wheel/skull, the three key parts and the Abyss steps, with the next three things to do. The rules come from the engine source (`item.cpp`, `shrine.cpp`, `party.cpp`).
- **Automap:** the real overworld terrain (read from the engine), revealed as you explore (radius 5), with towns, dungeons, shrines and moongates marked, filled in once visited. Hover for names; a checkbox shows the whole map. Progress is kept in the browser (`localStorage`) and cleared by "Forget game data".
- Buttons: Heal party, +10 food, +500 gold.

## Known gaps

- No dungeon automap, no NPC dialogue hints, and the black stone, skull and reagent spots only get a note about the new moons.
- The shrine of Spirituality has no overworld entrance in the map data, so its location isn't listed.
- The "assist" buttons only edit the savegame, so the game's own status area lags until the next move.
- The first keypress after the title screen is sometimes ignored.
- No sound (Faun/Vorbis), no optional `u4upgrad.zip` graphics, no mouse or touch input.
