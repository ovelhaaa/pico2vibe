#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$ROOT/web/dist"
mkdir -p "$OUT"

emcc "$ROOT/web/wasm/vibe_wasm.cpp" \
  -I"$ROOT/src" \
  -O3 \
  -s MODULARIZE=1 \
  -s EXPORT_ES6=1 \
  -s ENVIRONMENT=web \
  -s ALLOW_MEMORY_GROWTH=1 \
  -s EXPORTED_FUNCTIONS='["_malloc","_free","_vibe_create","_vibe_destroy","_vibe_get_sample_rate","_vibe_get_block_size","_vibe_get_param_count","_vibe_get_param_name","_vibe_get_param_min","_vibe_get_param_max","_vibe_get_param_default","_vibe_get_param","_vibe_set_param","_vibe_get_voicing_count","_vibe_get_voicing_name","_vibe_set_voicing","_vibe_reset","_vibe_process_stereo"]' \
  -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap","UTF8ToString"]' \
  -o "$OUT/vibe_wasm.js"

cp "$ROOT/web/public/"* "$OUT/"
touch "$OUT/.nojekyll"
echo "Built web preview into $OUT"
