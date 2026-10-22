# Source this from the build scripts: puts Emscripten (emcc/em++/emar) and
# Node on PATH.  If emcc is already available (e.g. after `source
# tools/emsdk/emsdk_env.sh`, as in CI) nothing is changed.  Otherwise it
# finds the SDK in tools/emsdk, which also works from Git Bash on Windows
# where emsdk_env.sh can't be sourced.

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if ! command -v emcc >/dev/null 2>&1; then
  EMSDK="$ROOT/tools/emsdk"
  if [ ! -d "$EMSDK/upstream/emscripten" ]; then
    echo "Emscripten not found. Install it into tools/emsdk first (see README.md)." >&2
    exit 1
  fi
  export PATH="$EMSDK:$EMSDK/upstream/emscripten:$(echo "$EMSDK"/node/*/bin):$(echo "$EMSDK"/node/*):$PATH"
  EMSDK_NODE="$(ls "$EMSDK"/node/*/node.exe "$EMSDK"/node/*/bin/node 2>/dev/null | head -1)"
  EMSDK_PYTHON="$(ls "$EMSDK"/python/*/python.exe "$EMSDK"/python/*/bin/python3 2>/dev/null | head -1)"
  [ -n "$EMSDK_NODE" ] && export EMSDK_NODE
  [ -n "$EMSDK_PYTHON" ] && export EMSDK_PYTHON
fi

# Node for running the WASM build of Boron.
NODE="${EMSDK_NODE:-node}"
