# Notices

**ultima-assist** is an unofficial browser build of the open-source *Ultima IV* engine, with an assist sidebar. It is not affiliated with or endorsed by Electronic Arts or Origin Systems. *Ultima* is a trademark of its owner.

Complete corresponding source for this site: @@SOURCE_URL@@

## Game data is not included

The site contains **no Ultima IV game data**. To play, you supply your own copy of the DOS version's `ultima4.zip` (Origin released Ultima IV as freeware). The page checks the file locally, stores it in your browser's storage, and never uploads it anywhere. "Forget game data" removes it.

## Optional download relay

The "Download it for me" button fetches the game file through a small relay (`relay/`, an Azure Function). The relay stores nothing, only serves one fixed URL, and refuses the file unless its SHA-256 matches a pinned value. Choosing the file yourself works without it.

## Components

| Part | What | Licence |
| --- | --- | --- |
| [xu4](https://github.com/xu4-engine/u4) | The game engine, commit `6a7ee3d0079cfdc1c8fb9ba7a3c710a957155a71`, in `engine/` | GPL-3.0-or-later (see `engine/COPYING`; some files carry GPL-2.0-or-later headers) |
| xu4 changes | WebGL2 and browser input/storage changes (`patches/xu4-emscripten.patch`), plus new `engine/src/assist.cpp` and `engine/src/sound_null.cpp` | GPL-3.0-or-later |
| [Boron](https://codeberg.org/wickedsmoke/boron) | Scripting language embedded in xu4, commit `b0c1305e7b4ff11a0ee2aef3b97a2a95f4afe4b2`, in `deps/boron/` | LGPL-3.0 (see `deps/boron/LICENSE`) |
| `web/`, `tools/`, `relay/`, build scripts | The page, the assist sidebar, build tooling | GPL-3.0-or-later |
| `web/guide-data.js` | Town, dungeon, shrine and item coordinates, generated from xu4's `module/Ultima-IV/maps.b` by `tools/gen-guide.py` | GPL-3.0-or-later (derived from xu4 data) |
| [Emscripten](https://emscripten.org/) | Compiler toolchain. Its GLFW, libpng and zlib ports are linked into the build | MIT / zlib / libpng licences |

The licence texts for xu4 and Boron are published next to the site as `LICENSE-xu4-GPL-3.0.txt` and `LICENSE-boron-LGPL.txt`.
