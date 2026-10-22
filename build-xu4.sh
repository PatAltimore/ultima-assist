#!/bin/bash
# Compile xu4 to WASM objects in $OUT (default build/): GLFW + WebGL2 backend,
# silent sound, Boron from deps/out.  Run build-deps.sh first, then link-xu4.sh.
# Override OUT and OPT (e.g. OUT=build-dbg OPT="-O0 -g") for a debug build.
set -e
cd "$(dirname "$0")"
source tools/emenv.sh

SRC=engine/src
BOR=deps/boron
OUT=${OUT:-build}
mkdir -p "$OUT"
COMMON="${OPT:--O2} -w -sUSE_GLFW=3 -sUSE_LIBPNG=1 -sUSE_ZLIB=1 -DUSE_GLES -DUSE_BORON -DCONF_MODULE -DNDEBUG -DVERSION=\"DR-1.0\" -I$SRC -I$SRC/support -I$SRC/lzw -I$BOR/include -I$BOR/include/boron"

CSRCS="lzw/hash.c lzw/lzw.c support/notify.c support/stringTable.c support/txf_draw.c support/unzip.c module.c support/cdi.c"
CXXSRCS="annotation aura camp cheat city codex combat controller context creature death debug direction discourse dungeon dungeonview error event filesystem game gamebrowser gui image imageloader imagemgr imageview intro item location map maploader menu menuitem names object party person portal progress_bar rle savegame scale screen screen_glfw settings shrine sound_null assist spell stats textview tile tileanim tileset tileview u4file view xu4 lzw/u4decode lzw/u6decode config_boron"

JOBS=$(nproc 2>/dev/null || echo 4)
fail=0
pids=()

# Run at most $JOBS compiles at a time; remember any failure.
run() {
  while [ "$(jobs -rp | wc -l)" -ge "$JOBS" ]; do sleep 0.2; done
  ( "$@" ) &
  pids+=($!)
}

for f in $CSRCS; do
  echo "CC  $f"
  run emcc $COMMON -c "$SRC/$f" -o "$OUT/$(echo $f | tr / _).o"
done
for f in $CXXSRCS; do
  echo "CXX $f"
  run em++ $COMMON -std=gnu++11 -c "$SRC/$f.cpp" -o "$OUT/$(echo $f | tr / _).o"
done

for p in "${pids[@]}"; do wait "$p" || fail=1; done
if [ "$fail" != 0 ]; then
  echo "A compile step failed." >&2
  exit 1
fi
echo "Compiled $(ls "$OUT"/*.o | wc -l) objects into $OUT/"
