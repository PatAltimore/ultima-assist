#!/bin/bash
# Cross-compile Boron (embedded by xu4) to WASM with Emscripten.
#  - deps/out/libboron.a : static library linked into the web build
#  - deps/out/boron.js   : interpreter run under Node, used to pack xu4's
#                          .mod / render.pak game modules (no native compiler needed)
set -e
cd "$(dirname "$0")"
source tools/emenv.sh

cd deps/boron
[ -f config.opt ] || ./configure --no-readline --no-socket --no-execute --static
CONFIG="$(cat config.opt)"
CFLAGS="-O2 -std=gnu99 -Iinclude -Iinclude/boron -Iurlan -Ieval -Isupport -sUSE_ZLIB=1 -w $CONFIG"
OBJ_FN="env array binary block coord date path string context gc serialize tokenize vector parse_block parse_string str mem_util quickSortIndex fpconv os boron port_file wait hashmap well512 random"
mkdir -p ../out/obj
rm -f ../out/libboron.a
OBJS=()
for n in $OBJ_FN; do
  for d in urlan support eval unix; do
    [ -f "$d/$n.c" ] && { src="$d/$n.c"; break; }
  done
  echo "===> CC $src"
  emcc $CFLAGS -c "$src" -o "../out/obj/$n.o"
  OBJS+=("../out/obj/$n.o")
done
emar rc ../out/libboron.a "${OBJS[@]}"
echo "===> main (node interpreter)"
emcc $CFLAGS -c eval/main.c -o ../out/obj/main.o
emcc ../out/obj/main.o ../out/libboron.a -sUSE_ZLIB=1 -sNODERAWFS=1 -sENVIRONMENT=node -sEXIT_RUNTIME=1 -sALLOW_MEMORY_GROWTH -O2 -o ../out/boron.js
echo "Done: deps/out/libboron.a deps/out/boron.js"
