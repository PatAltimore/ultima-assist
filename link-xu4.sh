#!/bin/bash
# Link the objects into a web build.  Usage: link-xu4.sh <outdir> [path/to/ultima4.zip]
# Passing the zip embeds it (local testing only -- never publish such a build).
# BUILD and LDOPT select the object directory and optimisation flags (see build-xu4.sh).
set -e
cd "$(dirname "$0")"
source tools/emenv.sh
OUT="${1:?usage: link-xu4.sh <outdir> [ultima4.zip]}"; ZIP="$2"
mkdir -p "$OUT"
EXTRA=()
[ -n "$ZIP" ] && EXTRA+=(--preload-file "$ZIP@/ultima4.zip")
em++ ${BUILD:-build}/*.o deps/out/libboron.a -O2 -sUSE_GLFW=3 -sUSE_LIBPNG=1 -sUSE_ZLIB=1 \
  -sFULL_ES3=1 -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 -sASYNCIFY -sASYNCIFY_STACK_SIZE=262144 -sSTACK_SIZE=1MB \
  -sINITIAL_MEMORY=128MB -sALLOW_MEMORY_GROWTH -sFORCE_FILESYSTEM -lidbfs.js ${LDOPT:--O2} \
  --shell-file web/shell.html -sEXPORTED_RUNTIME_METHODS=callMain,FS,ENV,UTF8ToString \
  --preload-file engine/Ultima-IV.mod@/Ultima-IV.mod \
  --preload-file engine/render.pak@/render.pak "${EXTRA[@]}" -o "$OUT/index.html"
cp web/guide-data.js "$OUT/guide-data.js"
