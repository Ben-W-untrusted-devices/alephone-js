# Aleph One Web Port — Plan

Tracking document for porting this fork of Aleph One to run in the browser
(WASM for the existing C++ engine, browser-native code where the platform
genuinely differs from desktop). Update this file's checklist as work lands;
keep the "Findings" section as a running log of things worth not
re-discovering later.

## Goals

- Run the existing Marathon engine (Source_Files/) in a browser via
  WebAssembly, reusing as much of the existing C++ as possible.
- Replace only the pieces that are genuinely platform-specific: game data
  loading (no real filesystem on the web) and anything that turns out not to
  be covered by SDL2's Emscripten backend.

## Non-goal (for now)

- Networked multiplayer (SDL_net/TCPMess) — deferred until the single-player
  path works end to end.

## Working conventions

- New code gets unit tests. Existing code does not need retrofitted tests
  just because it's nearby.
- Prefer not to modify existing (C++) code. When a change there really is
  necessary, leave a comment explaining why (existing files have none of this
  style today, so a web-port comment should be easy to spot).
- Prefer smaller, incremental changes over big-bang rewrites. Partial
  progress that leaves things in a working, tested state is fine — this
  doesn't need to be solved in one pass.
- WASM is the target for the existing C++; don't rewrite engine logic in JS.

## Hard constraint: Marathon 2 test data is not repo content

A copy of the real Marathon 2 game data (for manual/integration testing of
file loading) lives at `../Marathon 2` — a sibling of this git repo, **one
level outside it**. This is commercial game content whose copyright status
here is implicit, not explicit permission to redistribute.

- It must never be copied, moved, or committed into this repository.
- It must never be bundled into, or fetched/hosted by, the built web app.
- It is for local testing only: confirming the file-loading code actually
  works against real scenario data. Tests that use it must read it from
  outside the repo (e.g. via a path/env var) and must skip cleanly — not
  fail — when it isn't present, since it won't exist on other machines or CI.
- See also the root-level `CLAUDE.md` (one level up from this repo) and
  `alephone-js/CLAUDE.md`, which both restate this rule for future sessions.

## Findings so far

- **SDL2 has a real Emscripten backend.** Emscripten's `-sUSE_SDL=2` port
  translates DOM events into genuine SDL events: keyboard, mouse (including
  Pointer Lock for relative-mouse/mouselook), and Gamepad API → SDL
  GameController. This codebase's input layer
  ([Source_Files/Input/joystick_sdl.cpp](Source_Files/Input/joystick_sdl.cpp),
  [Source_Files/Input/mouse_sdl.cpp](Source_Files/Input/mouse_sdl.cpp), and
  ~47 files calling `SDL_PollEvent`/`SDL_GetKeyboardState`/etc.) already
  targets the SDL2 API, so it likely needs little to no rewriting — this is
  a recompile target, not a from-scratch JS input reimplementation.
- **File I/O is the real hard part.**
  [Source_Files/Files](Source_Files/Files) uses POSIX-style `fopen`/`opendir`
  plus `SDL_RWops`/zzip against a real disk
  ([find_files_sdl.cpp](Source_Files/Files/find_files_sdl.cpp),
  [wad_sdl.cpp](Source_Files/Files/wad_sdl.cpp)) to locate and load
  scenario/map/prefs/save data. Emscripten's MEMFS emulates POSIX file calls
  fine once data is *in* it, but populating it from user-supplied files
  (drag-drop/file picker) instead of scanning a real directory tree is a
  genuine, non-trivial redesign — this is where the actual new code lives.
- **Recognized Aleph One data file extensions**
  ([Source_Files/Files/FileHandler.cpp:571](Source_Files/Files/FileHandler.cpp#L571)):
  `.sceA` (scenario/map), `.shpA` (shapes), `.sndA` (sounds), `.phyA`
  (physics), `.sgaA` (savegame), `.filA` (film/replay); images (`.imgA`) are
  recognized by [ScenarioChooser.cpp](Source_Files/Misc/ScenarioChooser.cpp)
  rather than the main extension table. Useful for light, non-blocking
  recognition in the upload widget — not for hard validation, since plugins
  and scripts don't follow this table.
- **Emscripten toolchain is installed and proven against this codebase's
  real build system.** `emsdk` (pinned at 6.0.6) lives at `../emsdk` —
  a sibling of this repo, like `../Marathon 2`, since it's toolchain, not
  project source, and shouldn't bloat the repo. It's on `PATH` in every
  shell via `~/.zprofile` sourcing `emsdk_env.sh`. `emconfigure ../configure`
  (run from a `build-wasm/` directory, gitignored) correctly detects `emcc`/
  `em++` as the compiler and gets all the way through the standard autoconf
  checks — it only stops at Boost detection, because Boost isn't built for
  the `wasm32-unknown-emscripten` target yet. That's expected, not a
  toolchain problem.
- **Dependency landscape for M3**, from
  [vcpkg.json](vcpkg.json): Boost (algorithm/uuid/property_tree/iostreams/
  filesystem/lockfree/dll), SDL2 + SDL2_image + SDL2_ttf, asio, zziplib,
  miniupnpc, nativefiledialog-extended, libsndfile, openal-soft, catch2,
  libyuv, steamworks-sdk, matroska, libvpx. Rough triage:
  - SDL2 (+ SDL2_image) ships as an Emscripten *port*
    (`-sUSE_SDL=2`/`-sUSE_SDL_IMAGE=2`) — no separate build needed, this is
    the point of M3's SDL findings above.
  - openal-soft has an Emscripten port too (`-sUSE_OPENAL=1`) — candidate
    for M5 (audio) instead of building the real library.
  - Boost: mostly header-only for what this project uses, except
    Boost.Filesystem, which needs an actual compiled library for the wasm32
    target (build it with `emconfigure`/`b2`, or reduce reliance on it in
    favor of Emscripten's MEMFS-backed POSIX calls where the code allows).
  - nativefiledialog-extended, miniupnpc, steamworks-sdk: not applicable on
    the web (native file dialogs → our upload widget; NAT traversal/Steam →
    not relevant to a browser build) — should be `#ifdef`'d out for the web
    target rather than ported.
  - zziplib, libsndfile, matroska, libvpx, asio: still need a real decision
    (build for wasm32, replace with a browser-native equivalent, or defer
    the feature) — not yet investigated.

## Milestones / Task list

- [ ] **M1 — Data-provisioning upload widget** (`web/`, pure JS/TS, no WASM
      dependency, can be built and tested right now)
  - [ ] Multi-file / folder drag-and-drop + `<input>` fallback
  - [ ] Light, friendly recognition summary (spot known scenario file types;
        don't hard-block unrecognized files)
  - [ ] In-memory file collection abstraction, shaped so it can be wired into
        an Emscripten MEMFS/IDBFS bridge later without redesign
  - [ ] Unit tests (Vitest + jsdom)
  - [ ] Integration test against the real Marathon 2 data (reads from
        outside the repo, skips if absent)
- [x] **M2 — Emscripten toolchain**
  - [x] Install/document emsdk setup (`../emsdk`, see Findings)
  - [x] Prove the toolchain works against the real build: `emconfigure
        ../configure` correctly drives `emcc`/`em++` through the standard
        autoconf checks (from a gitignored `build-wasm/` dir)
- [ ] **M3 — Engine build against Emscripten's SDL2 port**
  - [ ] Get each vcpkg dependency building for wasm32 (or replaced/deferred
        per the triage in Findings) so `emconfigure`/`emmake` can get past
        Boost/SDL2/etc. detection and actually build objects
  - [ ] Confirm input (keyboard/mouse/gamepad) via SDL2's Emscripten backend
  - [ ] Confirm rendering (RenderMain/RenderOther GL calls) via Emscripten's
        GL→WebGL translation
- [ ] **M4 — Filesystem bridge**
  - [ ] Feed files collected by the M1 widget into MEMFS at the paths
        `find_files_sdl.cpp`/`FileHandler` expect
  - [ ] IDBFS persistence so re-upload isn't required every session
- [ ] **M5 — Audio**
- [ ] **M6 — Save games / prefs persistence**
- [ ] **M7 (stretch, likely deferred) — Networking** (SDL_net/TCPMess)

## Status

M1 (upload widget) and M2 (toolchain) are done. Next up: M3, starting with
the dependency triage above — Boost.Filesystem is the first concrete thing
blocking `emconfigure` from getting past the detection phase.
