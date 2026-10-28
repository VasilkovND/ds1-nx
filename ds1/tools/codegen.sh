#!/bin/bash
# Generate the recompiled C++ of Dead Space (2008) from your own default.xex, and its memory image. Nothing here is
# committed: the output is ds1/out/codegen/.
#
#   ds1/tools/codegen.sh <folder with default.xex>
#   ds1/tools/codegen.sh --host                     (only build the host rexglue tool)
#
# Steps (each skipped when already done):
#   1. ReXGlue SDK v0.10.0 for the host, built with the Mac's clang (only its `rexglue` tool is used), with Hell's
#      Gate's patch (https://github.com/florinp93/hells-gate-recomp, fetched at the tested commit, never copied: it has no
#      license file) for its codegen fixes, and ours (host-patches/: dump the loaded image), as in ds3-nx.
#   2. The manifest: default.xex as the entry point, with codegen/names.toml (hints for branch targets the analysis does
#      not find, and names for the functions the Switch code hooks).
#   3. rexglue codegen. The build stops if the analysis reports a branch to an address that is no function entry: it
#      prints the names.toml entries to add (ADD_UNRESOLVED=1 adds them), then run again. The traps left become gotos
#      and tail calls (patch_branch_traps.py).
#   4. The CPU passes from gears2-nx / ds3-nx: patch_stack_access.py, patch_inline_calls.py and, with LOCALREGS=1 (the
#      default), patch_local_regs.py.
set -euo pipefail

if [ $# -ne 1 ]; then
  sed -n '2,19p' "$0" | sed 's/^# \{0,1\}//'
  exit 1
fi
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$ROOT/ds1/out"
CG="$OUT/codegen"
JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN)}"
LOCALREGS="${LOCALREGS:-1}"

HELLS_GATE_COMMIT="3ce3993b0f03c77353f8f10fdcd982160c9a2d18"   # 2026-10-16, v0.8.4-beta
REXGLUE_TAG="v0.10.0"
# The executable this build is made and tested from (ds1/PORTING.md, "Files"): title 45410857, v0.0.0.2.
TESTED="2a0adbcaecaf5deaeb801ffbbaedcb813617b19f1043187f43fd5f82c15ec7e3"

sha() { shasum -a 256 "$1" | cut -d' ' -f1; }
mkdir -p "$OUT"

# 1. Host ReXGlue
HOST="$OUT/rexglue-host"
BIN="$HOST/out/$(uname -s | tr '[:upper:]' '[:lower:]' | sed 's/darwin/mac/')-$(uname -m | sed 's/x86_64/amd64/')"
REXGLUE="$BIN/rexglue"
if [ ! -x "$REXGLUE" ]; then
  HG="$OUT/hells-gate-recomp"
  if [ "$(git -C "$HG" rev-parse HEAD 2>/dev/null)" != "$HELLS_GATE_COMMIT" ]; then
    echo "== Hell's Gate $HELLS_GATE_COMMIT (its ReXGlue patch)"
    rm -rf "$HG"
    mkdir -p "$HG"
    git -C "$HG" init -q
    git -C "$HG" remote add origin https://github.com/florinp93/hells-gate-recomp.git
    git -C "$HG" fetch -q --depth 1 origin "$HELLS_GATE_COMMIT"
    git -C "$HG" checkout -q FETCH_HEAD
  fi
  SRC="$OUT/rexglue-host-src"
  if [ ! -d "$SRC/.git" ]; then
    echo "== ReXGlue $REXGLUE_TAG (host)"
    git clone -q --branch "$REXGLUE_TAG" --depth 1 https://github.com/rexglue/rexglue-sdk.git "$SRC"
    git -C "$SRC" submodule update --init --recursive --depth 1 -j 8
  fi
  for p in "$HG/patches/sdk/rexglue-sdk-v0.10.0.patch" "$ROOT"/ds1/host-patches/*.patch; do
    if git -C "$SRC" apply --reverse --check "$p" 2>/dev/null; then continue; fi
    echo "== host patch: $(basename "$p")"
    git -C "$SRC" apply "$p"
  done
  echo "== build rexglue (host, a few minutes)"
  cmake -S "$SRC" -B "$SRC/out/host" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DREXGLUE_USE_VULKAN=ON > "$OUT/rexglue-host.log"
  cmake --build "$SRC/out/host" --target rexglue -j "$JOBS" >> "$OUT/rexglue-host.log"
  BUILT="$SRC/out/$(basename "$BIN")"
  [ -x "$BUILT/rexglue" ] || { echo "rexglue was not built (see $OUT/rexglue-host.log)"; exit 1; }
  # Keep the tool and the libraries beside it; the sources and the build tree (~1 GB) go.
  rm -rf "$HOST"
  mkdir -p "$(dirname "$BIN")"
  mv "$BUILT" "$BIN"
  rm -rf "$SRC" "$HG"
  if [ "$(uname -s)" = Darwin ]; then
    # Its libraries sit beside it: find them there, so the folder can move.
    for p in $(otool -l "$REXGLUE" | awk '/LC_RPATH/{getline; getline; print $2}'); do
      install_name_tool -rpath "$p" @executable_path "$REXGLUE" 2>/dev/null || true
    done
    codesign -f -s - "$REXGLUE" 2>/dev/null || true
  fi
fi
[ -x "$REXGLUE" ] || { echo "rexglue was not built (see $OUT/rexglue-host.log)"; exit 1; }
if [ "$1" = --host ]; then
  echo "rexglue: $REXGLUE"
  exit 0
fi

GAME=$(cd "$1" && pwd)
[ -f "$GAME/default.xex" ] || { echo "$GAME/default.xex not found"; exit 1; }
if ! printf '%s
' "$TESTED" | grep -qx "$(sha "$GAME/default.xex")"; then
  echo "warning: default.xex is not the one this build was tested with (see ds1/PORTING.md); trying anyway"
fi

# 2. Project folder: the executable (copied: codegen refuses a link out of its game root) and the manifest.
mkdir -p "$CG/game"
cmp -s "$GAME/default.xex" "$CG/game/default.xex" || cp "$GAME/default.xex" "$CG/game/default.xex"
python3 - "$ROOT/ds1/codegen/names.toml" "$CG/ds1_manifest.toml" <<'PY'
import sys
names, out = open(sys.argv[1]).read(), sys.argv[2]
new = ('[project]\nname = "ds1"\nsdk_version = "0.10.0"\ngame_root = "game"\n\n'
       '[entrypoint]\nfile_path = "game/default.xex"\nout_directory_path = "generated/default"\nincludes = []\n\n'
       + names.rstrip() + '\n')
try:
    if open(out).read() == new:
        sys.exit(0)
except OSError:
    pass
open(out, 'w').write(new)
PY
# A name the SDK already defines as __imp__<name> (its export stubs) would replace the game's function with that stub.
for n in $(sed -n 's/^name = "\(.*\)"$/\1/p' "$ROOT/ds1/codegen/names.toml"); do
  if grep -rqE "__imp__$n\b" "$ROOT/sdk/src" "$ROOT/sdk/include"; then
    echo "codegen/names.toml: the SDK already defines __imp__$n; pick another name"; exit 1
  fi
done

# 3. Codegen (re-run only when an input changed) and 4. the passes.
STAMP="$CG/inputs.sha"
INPUTS=$( (cat "$CG"/game/* "$CG/ds1_manifest.toml" "$REXGLUE" \
  "$ROOT"/ds1/tools/patch_branch_traps.py "$ROOT"/ds1/tools/patch_stack_access.py "$ROOT"/ds1/tools/patch_inline_calls.py "$ROOT"/ds1/tools/patch_local_regs.py
  echo "localregs=$LOCALREGS check=${PLR_CHECK:-0}"
  # The generated functions the sources name (hooks, native callers) change what the passes do: those names.
  NAMES=$(sed -n 's/^name = "\(.*\)"$/\1/p' "$CG/ds1_manifest.toml" | paste -sd'|' -)
  cat $(find "$ROOT/ds1/src" "$ROOT/sdk/src" "$ROOT/sdk/include" -type f \( -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \
    -o -name '*.cc' -o -name '*.inc' \) -not -path '*/codegen/*' | sort) | grep -oE "\b(sub_[0-9A-F]{8}|${NAMES:-sub_})\b" | sort -u) \
  | shasum -a 256 | cut -d' ' -f1)
if [ "$(cat "$STAMP" 2>/dev/null)" != "$INPUTS" ] || [ ! -f "$CG/generated/default/sources.cmake" ]; then
  # Generated and patched in a staging folder, then only the files whose text changed are copied over generated/: the
  # others keep their dates, so after a small change (a name in names.toml) Ninja rebuilds a few units.
  echo "== codegen"
  ST="$CG/staging"
  rm -rf "$ST"
  mkdir -p "$ST"
  cp "$CG/ds1_manifest.toml" "$ST/"
  ln -s ../game "$ST/game"
  (cd "$ST" && REX_DUMP_IMAGE="$CG/image.bin" "$REXGLUE" --force codegen ds1_manifest.toml > "$CG/codegen.log" 2>&1) || {
    tail -20 "$CG/codegen.log"; exit 1; }
  # Branches into no function: each one becomes a trap in the generated code. A target that is no function entry yet
  # is named in names.toml (ADD_UNRESOLVED=1 does
  # it) and the code generated again; the traps left, to targets that are entries already, are what
  # patch_branch_traps.py turns into gotos and tail calls (below).
  UNRESOLVED=$(grep -oE 'Unresolved (b target|function|conditional branch to) 0x[0-9A-Fa-f]{8}' "$CG/codegen.log" | grep -oE '0x[0-9A-Fa-f]{8}' | sort -u || true)
  : > "$CG/unresolved.toml"
  NEW=0
  for a in $UNRESOLVED; do
    A=$(echo "${a#0x}" | tr '[:lower:]' '[:upper:]')
    NAMES_FILE="$ROOT/ds1/codegen/names.toml"
    grep -q "^\[entrypoint.functions.0x$A\]" "$NAMES_FILE" && continue
    NEW=$((NEW + 1))
    printf '# %s\n[entrypoint.functions.0x%s]\nname = "unresolved_target_%s"\n' "$(basename "$NAMES_FILE")" "$A" "$A" >> "$CG/unresolved.toml"
    if [ "${ADD_UNRESOLVED:-0}" = 1 ]; then
      printf '\n[entrypoint.functions.0x%s]\nname = "unresolved_target_%s"\n' "$A" "$A" >> "$NAMES_FILE"
    fi
  done
  if [ "$NEW" != 0 ]; then
    echo "codegen: $NEW branch targets that are no function (see $CG/codegen.log, $CG/unresolved.toml)."
    if [ "${ADD_UNRESOLVED:-0}" = 1 ]; then
      echo "added to ds1/codegen/names.toml: run again (new ones can appear)"
    else
      echo "add them to ds1/codegen/names.toml (ADD_UNRESOLVED=1 does it) and run again"
    fi
    exit 1
  fi
  for M in default; do
    echo "== $M: unresolved-branch traps"
    (cd "$ST" && python3 "$ROOT/ds1/tools/patch_branch_traps.py" generated/$M/*.cpp) > "$CG/patch_branch_traps.$M.log" || {
      cat "$CG/patch_branch_traps.$M.log"; exit 1; }
    head -1 "$CG/patch_branch_traps.$M.log"
    echo "== $M: generated-code passes for the CPU"
    # The guest stack's accesses without volatile (make_nx_pch.py, point 4).
    (cd "$ST" && python3 "$ROOT/ds1/tools/patch_stack_access.py" generated/$M/*.cpp) > "$CG/patch_stack_access.$M.log"
    tail -1 "$CG/patch_stack_access.$M.log"
    # Calls the compiler may inline: same-file functions no hook replaces, and the register save/restore helpers.
    (cd "$ST" && python3 "$ROOT/ds1/tools/patch_inline_calls.py" "$ROOT/ds1/src" "$ROOT/sdk/src" "$ROOT/sdk/include" \
      -- generated/$M/ds1_recomp.*.cpp) > "$CG/patch_inline_calls.$M.log"
    tail -1 "$CG/patch_inline_calls.$M.log" | cut -c1-160
    # The guest registers in host locals inside each function, with ctx exact only where a call or a return needs it.
    if [ "$LOCALREGS" = 1 ]; then
      (cd "$ST" && PLR_MAP="$CG/patch_local_regs.$M.check.map" python3 "$ROOT/ds1/tools/patch_local_regs.py" \
        generated/$M/ds1_inline_helpers.h "$ROOT/ds1/src" "$ROOT/sdk/src" "$ROOT/sdk/include" \
        -- generated/$M/ds1_recomp.*.cpp) > "$CG/patch_local_regs.$M.log" 2>&1 || {
        tail -20 "$CG/patch_local_regs.$M.log"; exit 1; }
      tail -1 "$CG/patch_local_regs.$M.log"
    fi
  done
  for M in default; do
    python3 - "$ST/generated/$M" "$CG/generated/$M" <<'PY'
import filecmp, os, shutil, sys
new, old = sys.argv[1:]
os.makedirs(old, exist_ok=True)
names = set(os.listdir(new))
changed = 0
for n in sorted(names):
    a, b = os.path.join(new, n), os.path.join(old, n)
    if not os.path.isfile(b) or not filecmp.cmp(a, b, shallow=False):
        shutil.copy2(a, b)
        changed += 1
for n in set(os.listdir(old)) - names:
    os.remove(os.path.join(old, n))
print(f"generated/{os.path.basename(old)}: {changed} of {len(names)} files changed")
PY
  done
  rm -rf "$ST"
  echo "$INPUTS" > "$STAMP"
fi
for M in default; do
  echo "codegen: $(ls "$CG"/generated/$M/*.cpp | wc -l | tr -d ' ') files in $CG/generated/$M"
done
echo "default.xex: $(sha "$GAME/default.xex")"
