#!/bin/bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
deps="${RENUT_DEPS_DIR:-$root/.deps}"
mkdir -p "$deps"
[[ "$(uname -s)" == Darwin && "$(uname -m)" == arm64 ]] || { echo 'Requires an Apple Silicon Mac.' >&2; exit 1; }
# Use a project-local SDK, extractor, Python environment and shader compiler.
download() {
    local url="$1" dest="$2" hash="$3"
    if [[ ! -f "$dest" ]]; then curl -fL --retry 3 "$url" -o "$dest.tmp"; mv "$dest.tmp" "$dest"; fi
    printf '%s  %s\n' "$hash" "$dest" | shasum -a 256 -c -
}
if [[ ! -x "$deps/sdk/mac-arm64/bin/rexglue" ]]; then
    download https://github.com/rexglue/rexglue-sdk/releases/download/v0.10.0/rexglue-sdk-0.10.0-mac-arm64.zip \
        "$deps/sdk.zip" 1192638b51a6963aa6a4f24b77ebcb90c16f7fd0b91b311e11a90100f5274326
    unzip -q "$deps/sdk.zip" -d "$deps/sdk"
fi
if [[ ! -x "$deps/dxc/bin/arm64/dxc-macos" ]]; then
    git clone https://github.com/renderbag/dxc-bin.git "$deps/dxc"
    git -C "$deps/dxc" checkout 12395e36295f950361ca842d8cc461492412facc
fi
git -C "$root" submodule update --init --recursive
if [[ ! -f "$root/assets/default.xex" ]]; then
    [[ $# -ge 1 && -f "$1" ]] || { echo 'Pass the path to your US retail ISO as the first argument.' >&2; exit 1; }
    download https://github.com/XboxDev/extract-xiso/releases/download/build-202609111233/extract-xiso_macOS.zip \
        "$deps/extract-xiso.zip" be9b0acc33413d7fc69d9151224ff1314128fb218654c6aac64fc5709e3930c2
    if [[ ! -x "$deps/extractor/extract-xiso" ]]; then unzip -q "$deps/extract-xiso.zip" -d "$deps/extractor"; fi
    "$deps/extractor/extract-xiso" -x -d "$root/assets" "$1"
fi
if [[ ! -x "$deps/python/bin/python" ]]; then python3 -m venv "$deps/python"; fi
"$deps/python/bin/python" -c 'import xxhash' 2>/dev/null || "$deps/python/bin/pip" install xxhash==4.0.2
# ReXGlue's headers require a modern LLVM. Override for another LLVM installation.
llvm="${RENUT_LLVM_PREFIX:-/opt/homebrew/opt/llvm@21}"
[[ -x "$llvm/bin/clang++" ]] || { echo 'Install LLVM 21, CMake and Ninja, or set RENUT_LLVM_PREFIX.' >&2; exit 1; }
xcrun -sdk macosx metal --version >/dev/null
export PATH="$llvm/bin:$PATH"
export CMAKE_BUILD_PARALLEL_LEVEL="${RENUT_BUILD_JOBS:-4}"
cd "$root"
"$deps/sdk/mac-arm64/bin/rexglue" codegen renut_manifest.toml
cmake --preset mac-arm64-release -DCMAKE_PREFIX_PATH="$deps/sdk/mac-arm64" \
    -DRENUT_DXC_ROOT="$deps/dxc" -DPython3_EXECUTABLE="$deps/python/bin/python"
cmake --build --preset mac-arm64-release --parallel "$CMAKE_BUILD_PARALLEL_LEVEL"
MTL_DEBUG_LAYER=1 out/build/mac-arm64-release/renut_metal_smoke
"$deps/python/bin/python" tools/package_macos.py --build out/build/mac-arm64-release
