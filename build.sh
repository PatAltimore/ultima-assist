#!/bin/bash
# Full build: Boron -> game modules -> xu4 -> site folder.  Used by the GitHub
# Pages workflow and works the same locally.  Output: dist/ (no game data).
#   Needs Emscripten (tools/emsdk, or emcc already on PATH) and a bash shell.
set -e
cd "$(dirname "$0")"
source tools/emenv.sh

bash build-deps.sh

# Pack the game modules with the WASM build of Boron running under Node.
(
  cd engine
  "$NODE" ../deps/out/boron.js -s tools/pack-xu4.b module/Ultima-IV
  "$NODE" ../deps/out/boron.js -s tools/pack-xu4.b -f module/render -o render.pak
)

bash build-xu4.sh
rm -rf dist
bash link-xu4.sh dist

# GPL: ship the licence texts next to the build.
cp NOTICE.md dist/NOTICE.txt
cp engine/COPYING dist/LICENSE-xu4-GPL-3.0.txt
cp deps/boron/LICENSE dist/LICENSE-boron-LGPL.txt
sed -i "s|@@RELAY_URL@@|${RELAY_URL:-}|g" dist/index.html
# CI passes SOURCE_URL (the repository this site is built from).
sed -i "s|@@SOURCE_URL@@|${SOURCE_URL:-the repository this site was built from}|g" dist/NOTICE.txt

# Make sure nothing game-data-shaped slipped in, and that the build is complete.
for f in index.html index.js index.wasm index.data guide-data.js; do
  test -s "dist/$f" || { echo "Missing dist/$f" >&2; exit 1; }
done
if ls dist | grep -qi '\.zip$'; then echo "dist/ must not contain game data" >&2; exit 1; fi
echo "Built dist/:"; ls -la dist
