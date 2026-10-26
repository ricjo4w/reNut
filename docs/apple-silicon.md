# Apple Silicon port

This branch provides an ARM64 macOS release test build with a native Metal
renderer. It has been played through the opening and into Showdown Town on an
M3 MacBook Air. It remains a test candidate; complete-game compatibility and
steady 60 FPS have not been established.

## Architecture and required work

reNut recompiles PowerPC game code to C++ through ReXGlue. ReXGlue 0.10.0 already
provides an ARM64 macOS runtime, SDL3 window/input support and audio. The project
had no Mac build presets and its Plume gitlink lacked a `.gitmodules` mapping.
Its native graphics hooks bypass the SDK's Xenos renderer, so the SDK's existing
MoltenVK support does not by itself port this game.

Plume's pinned revision includes Metal. This port selects that backend directly,
creates an SDL Metal view and uses a Cocoa window/CAMetalLayer swapchain. It
retains the renderer's frame queues, asynchronous pipeline prebuild, upload
rings, resource retirement and texture/vertex caches.

The shaders require additional work beyond selecting a backend:

- DXC must run natively on ARM64 macOS, both as a tool and as a library inside
  XenosRecomp. Linux DXC binaries cannot be used here.
- SPIRV-Cross converts the recompiled shaders to Metal 3.1 with tier 2 argument
  buffers. Descriptor set indices remain Metal buffer indices 0–4, push constants
  use buffer 8, textures have 8192 slots and samplers have 256 slots. Physical
  buffer addresses remain direct GPU pointers. No Vulkan translation layer is
  used by reNut's renderer.
- Metal has no geometry shaders. Extra vertex shader variants pull vertices from
  the guest buffers and expand rectangle lists or point sprites into triangles.
  Rectangle expansion preserves arbitrary corner order and all interpolators;
  particles preserve size limits, viewport scaling and sprite coordinates.
- `metal` and `metallib` compile libraries during the build. Libraries are embedded
  in a compressed cache, avoiding shader-source compilation during gameplay.
  Individual library builds have content fingerprints so unchanged ones are reused.
  XenosRecomp also caches each shader's metadata and SPIR-V, keyed by its own
  executable, the loaded DXC library and the shared shader header. Runtime captures
  can therefore be added without recompiling every existing shader.
- A build-directory patch to Plume adds texture-to-buffer copies required for
  readback. The pinned submodule stays unmodified; patch application is checked.

The macOS build bundles SDL at commit
`d0183f0ee3b2497f49c6322c62354d114c22af29` and libusb 1.0.30. This includes
upstream wired Xbox controller support absent from the SDK snapshot. Before SDL
initialization, the entry point selects the bundled library through SDL
`SDL3_DYNAMIC_API`. Both static SDL callers then use the same runtime. The newer
SDL Objective-C classes are prefixed to avoid collisions with the SDK copy.
libusb is built for macOS 14, linked to SDL and bundled alongside it. No system
driver installation or administrator privileges are required. This enables input
inside reNut; it does not install system-wide GameController support for other
apps. Licenses and source URLs accompany the test build.

Platform integration includes macOS writable data/config/log paths, memory
reporting, Metal command-buffer error reporting, optional Mac EOS linkage,
app packaging and a compatibility adapter for the released SDK's logging setup.
The macOS audio import adapter also serializes driver registration, unregistration
and submission, dropping late submissions after driver teardown. In SDK 0.10.0,
the worker snapshots a callback before releasing its lock; unregister can then
clear the driver before that callback submits. A null driver fault was observed
during boot. Full-game startup, including the boot audio driver handover, is the
regression seam used here; an isolated callback mock would not cover this race.

Online play still requires the separately supplied EOS SDK and product credentials.

The Mac build registers functions by iterating the existing generated mapping
table instead of compiling a single enormous block of 39,937 virtual calls.
Every build verifies that the table and generated calls have identical ordered
bindings before using the substitution. Gameplay functions retain release
optimization.

## Build

Requirements: Apple Silicon, macOS 14 or newer, full Xcode with its Metal
toolchain, LLVM 21, CMake, Ninja, Git and Python 3. The Metal toolchain can be
installed with `xcodebuild -downloadComponent MetalToolchain` when missing.

```sh
tools/build_macos.sh '/absolute/path/to/Banjo-Kazooie Nuts & Bolts.iso'
```

The script downloads pinned SDK/extractor artifacts, verifies their SHA-256,
checks out the DXC distribution at a pinned commit, initializes Plume, extracts
the retail US ISO to ignored `assets/`, generates C++, builds, runs Metal API
validation and creates `out/build/mac-arm64-release/reNut.app` plus
`renut-macos-arm64.zip`. Once assets exist the ISO argument is optional. No game
data is included in the app archive. Keep `assets/` available for the game.

The initial build recompiles thousands of unique game shaders. Later builds reuse
the Metal library cache. `RENUT_BUILD_JOBS` controls C++ build parallelism (default
4); `RENUT_LLVM_PREFIX` selects another LLVM installation; `RENUT_DEPS_DIR`
relocates the project-local dependencies. The CMake presets are
`mac-arm64-release` and `mac-arm64-debug`.

For an existing SDK/toolchain, configure with `CMAKE_PREFIX_PATH` pointing at its
`mac-arm64` folder and `RENUT_DXC_ROOT` pointing at a distribution containing
`bin/arm64/dxc-macos` and `lib/arm64/libdxcompiler.dylib`.

Packaging uses an ad-hoc development signature. Public distribution needs the
maintainer's Developer ID signing and notarization process.

## Data and launcher

On macOS, settings, path configuration, logs and renderer caches live in
`~/Library/Application Support/renut`. Saves use the `user_data_root` in
`renut.cfg`. NutStaller writes the same directory and defaults to a separate
`~/Library/Application Support/NutStaller/reNut` install folder.

The NutStaller fork accepts `renut-macos-arm64.zip` containing `reNut.app`. It uses
the `ricjo4w/reNut` release feed on Mac; override with `NUTSTALLER_RENUT_REPO`.
A release must actually be published before download/update can install it.
Windows and Linux keep their existing upstream feed.

## Validation and performance criteria

On the development M3 MacBook Air (8 CPU cores, 10 GPU cores, 16 GB RAM), recorded
checks so far:

- ARM64 generated game C++ compiled.
- All host shaders compiled to Metal libraries; sample game shaders, including
  rectangle and sprite variants, compiled.
- `MTL_DEBUG_LAYER=1 renut_metal_smoke` passed: bindless textures/samplers, push
  constants, offscreen draw, texture readback, direct GPU-address load/store and
  depth/stencil sampling/resolve.
- Incremental cache migration reproduced the full shader cache byte for byte.
  Fresh compilation, cache reuse and recovery from a corrupted cache produced
  identical output.
- NutStaller built, was packaged and opened on macOS. Platform selection and
  app-bundle extraction regression checks passed.

Additional checks completed on October 20–21, 2026:

- The packaged ARM64 game boots, displays menus and the opening, and supports
  on-foot gameplay in Spiral Mountain and Showdown Town. Textures, lighting,
  bloom, shadows, depth, transparency, particle expansion and HUD were checked.
- Save and reload work using the macOS user folder. Windowed and fullscreen
  startup both work. Interactive resize/fullscreen transitions remain unverified.
- Audio initializes at 48 kHz with six channels. The user confirmed audible
  output and controller A navigating the game menu in the release build.
- A per-command autorelease pool fixed runaway Metal object retention. In a
  release Showdown Town session, private memory was about 2.36 GB and resident
  memory about 1.79 GB, with GPU allocations around 891 MB.
- Measured moving gameplay varied roughly 44–54 FPS across one-minute samples
  on the fanless M3 Air, with some warm views reaching 60 FPS. New pipeline
  creation still causes visible hitches. A 60 FPS overlay while paused is not
  evidence of a sustained gameplay frame rate.
- PowerA wired controller `20d6:2062` is detected and opened through SDL/libusb.
  Physical A/B/X/Y/Menu presses, both sticks and both analog triggers were read
  successfully. The app includes `Contents/MacOS/renut_controller_check`, which
  listens for 30 seconds and prints connection, button and axis events.

Vehicle building/driving, later worlds, extended thermal behavior,
rumble and USB unplug/replug remain acceptance checks. Do not describe this as a
fully validated or locked-60-FPS final port. All recorded performance figures use
release builds with Metal validation disabled.

Set `renut_frame_csv = "benchmark/session.csv"` before starting to record completed
game-frame times. CPU/GPU/render-thread values in that CSV are delayed samples;
frame times themselves are measured at each guest swap. Recording is disabled by
default. Run the cache regression check with:

```sh
python3 tools/check_shader_cache.py --compiler out/build/mac-arm64-release/xenos_recomp/build/XenosRecomp/XenosRecomp
```

Start at the existing 720p resolution and VSync, and compare CPU/GPU timing and
memory reports before increasing resolution. A fanless MacBook Air needs a
sustained run to reveal thermal throttling. Do not infer a frame rate from the
offscreen smoke test or from the title screen.
