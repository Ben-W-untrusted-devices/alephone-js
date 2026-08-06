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

## Non-goal

- Networked multiplayer (SDL_net/TCPMess/asio) — not just deferred, actually
  impossible from a browser tab as this codebase's networking is designed:
  browsers have no raw TCP/UDP socket API at all (only WebSocket/WebRTC),
  and no native peer (client, dedicated server, metaserver) speaks either of
  those. Real cross-play would need a purpose-built relay/gateway server as
  its own separate, later project — not a byproduct of getting asio to
  compile. Networking is compiled out entirely for the Emscripten build via
  `DISABLE_NETWORKING` (see Findings) rather than reimplemented.

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
- **How `emconfigure ../configure` was actually gotten to complete (M3a),
  and what to reuse next time:**
  - **Boost::Filesystem was eliminated, not cross-built.** Added
    [Source_Files/CSeries/portable_filesystem.h](Source_Files/CSeries/portable_filesystem.h),
    which aliases `aone_fs`/`aone_sys` to `std::filesystem`/`std` under
    `__EMSCRIPTEN__` and to `boost::filesystem`/`boost::system` otherwise.
    The only reason this codebase used `boost::filesystem` at all was pre-
    macOS-10.15 `std::filesystem` support (see the comment in
    `Source_Files/XML/Plugins.cpp`), which doesn't apply to Emscripten's
    libc++. Repointed the 4 call sites
    (`Files/FileHandler.cpp`, `XML/Plugins.{h,cpp}`, `Misc/preferences.cpp`).
    `configure.ac` now does an `AC_COMPILE_IFELSE` check for `__EMSCRIPTEN__`
    (`$host` stays the native triplet under `emconfigure` — it doesn't set
    `--host`, so `$host`-based detection doesn't work; a real compile check
    does) and skips the hard `AX_BOOST_FILESYSTEM` link-check when true.
    `AX_BOOST_BASE` (headers + version, no link step) still runs for both.
  - **`asio.hpp` doesn't compile under Emscripten as shipped** — its
    platform detection only recognizes Windows/POSIX and fails in
    `asio/detail/tss_ptr.hpp` ("Only Windows and POSIX are supported!").
    Since networked multiplayer is already a stated non-goal for now, the
    `AC_CHECK_HEADER([asio.hpp])` check is skipped for Emscripten in
    `configure.ac` too, same pattern as Boost::Filesystem. Revisit if/when
    networking actually comes into scope.
  - **vcpkg's community `wasm32-emscripten` triplet (ships in vcpkg itself,
    `triplets/community/wasm32-emscripten.cmake`) reliably cross-builds
    CMake-based ports** — no custom triplet needed, it auto-detects `$EMSDK`.
    Used it to build real SDL2, SDL2_ttf, libsndfile, and openal-soft for
    wasm32, all via `vcpkg/install-wasm32-emscripten.sh` (repo-tracked,
    mirrors the existing `install-<triplet>.sh` scripts, but installs an
    explicit minimal port list in `--classic` mode rather than the full
    manifest — most of `vcpkg.json` doesn't apply to a browser build).
    Each of these produced a correct `.pc` pkg-config file, so `configure.ac`'s
    *existing* `PKG_CHECK_MODULES`-based detection worked completely
    unmodified — this ended up being simpler than routing through
    Emscripten's own SDL2/OpenAL *ports* (`-sUSE_SDL=2`/`-sUSE_OPENAL=1`),
    since it reuses the autotools detection this project already has. Real
    upstream SDL2 already contains a genuine Emscripten backend
    (`src/video/emscripten/`), so this isn't a lesser substitute.
  - **`libsndfile`'s default vcpkg features pull in `mp3lame`, which fails
    to cross-build** (its own autotools script doesn't like the
    `wasm32-unknown-emscripten` host triplet). Not needed — installed as
    `libsndfile[core,external-libs]` (FLAC/Vorbis/Opus only, no MP3) instead
    of chasing mp3lame's build.
  - **Real `openal-soft` (not Emscripten's minimal built-in port) built
    cleanly for wasm32.** This likely resolves the `ALC_SOFT_loopback`/EFX
    risk flagged earlier (Emscripten's built-in OpenAL port lacks both) —
    worth confirming at runtime once there's something to run (M5), but the
    build-time risk is gone.
  - **Toolchain pieces now needed to reproduce this build**, none of them
    in the repo (same reasoning as `../emsdk`, `../Marathon 2`): a
    bootstrapped vcpkg *tool* checkout (this session used
    `../vcpkg-tool`, separate from this repo's tracked `vcpkg/`
    triplets-and-scripts directory) referenced via `~/.vcpkg/vcpkg.path.txt`
    per this project's existing convention; Homebrew `boost` and `asio`
    (their *headers* satisfy `AX_BOOST_BASE`/the asio check for the *native*
    macOS host build — architecture-independent, no cross-compilation
    needed for headers-only checks).
  - **Not found: OpenGL rendering** — configure didn't error (it's an
    optional `AC_ARG_ENABLE`), just silently disabled it, since nothing
    provides GL headers/libs for the wasm32 target yet. This is the next
    wall for M3b, and it's a real one: `RenderMain`/`RenderOther` use
    legacy compatibility-profile GL (fixed-function matrix/vertex-array
    state via `glMatrixMode`/`glEnableClientState`/`GL_DOUBLE` vertex
    arrays, plus an ARB-extension shader path —
    `glShaderSourceARB`/`glCreateProgramObjectARB` in `OGL_Shader.cpp`), not
    GLES2/3-shaped code. Emscripten's GL→WebGL translation targets GLES2/3;
    getting this rendering means real adaptation work (rewrite to VBOs +
    core GLSL), not just wiring up `-sUSE_WEBGL2=1` and recompiling.
  - **libyuv is entangled in movie *playback*, not just recording** —
    `Misc/interface.cpp` uses it for color conversion
    (`I420ToRGBA`/`I420Scale`) when playing intro/cutscene movies (decoded
    via the bundled `pl_mpeg`, unrelated to libvpx/matroska). The
    recording-only path (`Movie.cpp`, VP8/MKV export via libvpx/matroska)
    already has a working `#ifndef FILM_EXPORT` stub and was cleanly
    deferred via `--without-vpx --without-matroska --without-ebml`, but
    movie playback will need libyuv built for wasm32 eventually (not
    attempted yet — playback itself isn't in scope until later).
  - **`portable_filesystem.h` needed more than a namespace alias** once real
    compilation (not just `emconfigure`'s header-existence checks) exercised
    it. `boost::filesystem` and `std::filesystem` aren't perfectly drop-in
    compatible: `unique_path()` has no `std::filesystem` equivalent at all;
    the `file_type` enumerators are named differently (`regular_file`/
    `directory_file` vs `regular`/`directory`); and `last_write_time()`
    returns a plain `time_t` in Boost but a strongly-typed
    `chrono::time_point` in `std::filesystem`, incompatible with the
    `int`/`TimeType` comparisons the surrounding code did. Added
    `aone_fs_regular_file`/`aone_fs_directory_file`/`aone_fs_unique_path()`/
    `aone_fs_file_time_to_time_t()` wrappers to `portable_filesystem.h` to
    cover this — small, all in one place, all covered by compiling both
    branches directly (native g++ against Homebrew Boost, `em++` against
    Emscripten's libc++) rather than only trusting `emconfigure`. One real
    bug caught this way before it ever reached a build: my first draft of
    `aone_fs_file_time_to_time_t` took an `aone_fs::file_time_type`
    parameter, but `boost::filesystem` has no such type at all (its
    `last_write_time()` just returns `time_t` directly) — fixed by giving
    each branch its own correctly-typed signature instead of assuming one
    would work for both. **Lesson**: `emconfigure` succeeding only proves
    headers exist and link-time libraries are found — it doesn't compile
    any real code, so API-shape mismatches like this only show up once
    `emmake make` actually runs.
  - **`asio.hpp` doesn't just fail its own configure-time header check — it
    transitively breaks compilation of 43 object files that have nothing to
    do with networking**, including `shell.cpp` and `marathon2.cpp` (the
    actual entry point/main loop), `ChaseCam.cpp`, `Console.cpp`,
    `game_wad.cpp`, `interface.cpp`, `player.cpp`, `screen.cpp`, several
    `lua_*.cpp` files, and more. The chain: these files include
    `Misc/sdl_widgets.h` (general dialog/widget UI code) →
    `Network/Metaserver/metaserver_messages.h` → `Network/network.h` →
    `TCPMess/CommunicationsChannel.h` → `Network/NetworkInterface.h` →
    `<asio.hpp>`, which fails to compile under Emscripten regardless of
    whether the configure-time check ran. This is now the single biggest
    open blocker for M3b — bigger than GL, since GL only affects rendering
    files, not the entry point. Not yet decided how to resolve; options,
    roughly in order of how invasive they are:
    1. Make `asio.hpp` itself compile under Emscripten (e.g. patch/define
       around its Windows-or-POSIX-only platform detection). Keeps all
       existing code untouched but means fighting a library not designed
       for this target — unclear how deep that rabbit hole goes.
    2. Break the transitive leak: `sdl_widgets.h` (general UI) pulling in
       `metaserver_messages.h` (network-specific) is arguably a pre-existing
       modularity smell independent of the web port — worth understanding
       *why* before touching it.
    3. Stub `NetworkInterface.h`'s asio-dependent declarations under
       `#ifdef __EMSCRIPTEN__` so dependent code still compiles against a
       no-op interface, deferring real networking implementation entirely
       (consistent with the existing "networking is a non-goal for now"
       stance) — most invasive to existing code, but most clearly scoped.
    Needs a decision before M3b can make much further progress, similar to
    the GL rendering question.
  - **Decision on the asio leak: compile networking out entirely
    (option 3 above), because browsers can't do real networking regardless.**
    Browsers have no raw TCP/UDP socket API at all — only WebSocket (needs
    the peer to speak the WebSocket handshake) and WebRTC DataChannels
    (needs ICE/DTLS/SCTP negotiation on both ends). This codebase's
    networking (`NetworkInterface`/`TCPMess`/metaserver) uses plain
    asio sockets, same as every native peer (client, dedicated server,
    metaserver) it talks to — a browser tab can't open a socket to any of
    them no matter how the code compiles. Getting `asio.hpp` to compile
    (option 1) would've been wasted effort: even a working compile wouldn't
    produce a socket that reaches an existing native peer without a
    purpose-built WebSocket/WebRTC relay, which is a separate, later,
    deliberately-designed project, not a build-flag fix.
  - **The codebase already had a `DISABLE_NETWORKING` convention, just never
    finished/wired up** — worth reusing rather than inventing a new scheme.
    `configure.ac` now has a real `--disable-networking`/`--enable-networking`
    flag (`AX_ARG_ENABLE([networking], ...)`, matching the existing `opengl`
    pattern), forced off automatically for the Emscripten target regardless
    of what's passed (real networking is impossible there, not just
    undesired), `AC_DEFINE([DISABLE_NETWORKING], [1], ...)`-ing it into
    `config.h`. `NetworkInterface.h` gained a full `#else` branch: trivial,
    inline, no-op stand-ins for `IPaddress`/`UDPsocket`/`TCPsocket`/
    `TCPlistener`/`NetworkInterface` with the same public shape as the real
    classes (so code that merely references these types keeps compiling),
    and `NetworkInterface.cpp` became a no-op translation unit in that case.
    `network.cpp` already had a `#if defined(DISABLE_NETWORKING) #include
    "network_dummy.cpp" #else ... #endif` pattern from a previous, never-
    completed attempt at this — `network_games.cpp` has the equivalent
    whole-file-guarded pattern too. A handful of call sites
    (`network_dialogs.cpp`, `network_metaserver.cpp`, `network_star_spoke.cpp`)
    called `NetGetPinger()` — the one function `network.h` actually guards —
    unconditionally; wrapped those in `#if !defined(DISABLE_NETWORKING)` too.
  - **Widespread latent bug found while wiring this up: many of these
    `#if !defined(DISABLE_NETWORKING)` checks ran before `config.h` was ever
    included**, so the macro was never visible and the check silently always
    took the "networking enabled" branch — meaning this existing convention
    had likely never actually been exercised/tested before. Root cause:
    `DISABLE_NETWORKING` only exists inside the generated `config.h`, which
    is only pulled in via `#ifdef HAVE_CONFIG_H #include "config.h" #endif`
    in `cseries.h` — and roughly 15 files
    (`NetworkInterface.{h,cpp}`, `network.cpp`, `network_games.cpp`,
    `network_dialogs.cpp`, `network_metaserver.cpp`, `StarGameProtocol.cpp`,
    `network_star_spoke.cpp`, `network_star_hub.cpp`, `Pinger.{h,cpp}`,
    `network_dialog_widgets_sdl.cpp`, `network_messages.cpp`,
    `network_udp.cpp`, `metaserver_dialogs.cpp`, `metaserver_messages.cpp`,
    `SdlMetaserverClientUi.cpp`, `CommunicationsChannel.cpp`, `Message.cpp`,
    `MessageInflater.cpp`, `main.cpp`) had their `#if !defined(DISABLE_NETWORKING)`
    check as the very first thing after the license header, before any
    include at all. Fixed each with an explicit
    `#ifdef HAVE_CONFIG_H #include "config.h" #endif` immediately before the
    check. (Ran a heuristic check across every `DISABLE_NETWORKING` call
    site first to see which were actually at risk — most other usages, deep
    inside otherwise-normal files, were fine because those files already
    pull in `cseries.h` transitively via other early includes.)
  - **Result: `emmake make` now gets all the way to the real final link step
    for `alephone.wasm`** — genuine progress, not just per-file compilation.
    The 43-object-file compile-error wall from the asio leak, and the
    `network_dummy.cpp` incompleteness (duplicate `network_join`/
    `display_net_game_stats` symbols once `network_games.cpp`'s real
    implementation and the dummy's stub both compiled in) are both fully
    resolved — verified via a clean rebuild (`rm -rf build-wasm`), not just
    incremental, to rule out stale-object-file explanations.
  - **New next wall, found only once the real link was reached: a
    pthread/shared-memory ABI mismatch — now fully root-caused, in three
    layers.**
    1. **Where `-pthread` actually comes from**: `openal.pc`'s `Libs:` line
       bakes in `-pthread` (real openal-soft needs it for its mixer thread).
       That reaches the *linker* via `PKG_CHECK_MODULES`, but nothing adds
       `-pthread` to *compilation* — so wasm-ld correctly complains that our
       own object files (`shell.o` etc.) weren't compiled with atomics/
       bulk-memory support while the link wants `--shared-memory`. Fix:
       pass `-pthread` in `CFLAGS`/`CXXFLAGS` too, not just accept it at
       link time (compile and link must agree).
    2. **A second, unrelated bug this uncovered**: with `LDFLAGS=
       "-L/opt/homebrew/lib"` still set (left over from the Boost/asio
       *header*-only native build, from before M3a — never actually needed
       at link time), `wasm-ld` found the **native macOS**
       `/opt/homebrew/lib/libSDL2.a` before the correct vcpkg wasm32
       archive, silently discarded all its (incompatible-format) members,
       and left every SDL2 symbol undefined
       (`SDL_RWseek`/`SDL_RWread`/`SDL_EventState`/etc.). Dropping that
       stray `-L` from `LDFLAGS` fixed it completely — a good reminder to
       keep native-only flags scoped to what they're actually needed for.
    3. **The real remaining wall, after both of the above were fixed**:
       `wasm-ld: --shared-memory is disallowed by SDL_spinlock.c.o because
       it was not compiled with 'atomics' or 'bulk-memory' features` — this
       time from Emscripten's *own* bundled system libraries, not our code
       or vcpkg's. `libal` (Emscripten's built-in OpenAL JS-bridge, unused
       by us but linked in by default via the `AUTO_NATIVE_LIBRARIES`
       setting) and `libhtml5` (SDL2's real Emscripten backend genuinely
       needs this one, for `emscripten_set_keydown_callback_on_thread` and
       friends) are both declared as a plain `Library` rather than
       `MTLibrary` in this emsdk's `tools/system_libs.py` — meaning this
       Emscripten build (6.0.6) never produces a pthread-aware variant of
       either, and `libhtml5`'s is required, not optional. Confirmed with a
       full `emcc --clear-cache` + rebuild-from-scratch that this isn't
       cache staleness: freshly compiled with `-pthread` requested from the
       start, `libal.a`/`libhtml5.a` still embed a non-atomics
       `SDL_spinlock.c.o`. This looks like a genuine upstream limitation
       (possibly a bug) in this specific emsdk version, not something fixable
       via our own flags — `-sAUTO_NATIVE_LIBRARIES=0` removes `libal`, but
       `libhtml5` (which we can't drop) hits the identical wall on its own.
    **Undecided next step** — two real options: (a) try pinning a different
    (older/stable, not tip-of-tree) emsdk version and see if this library/
    pthread combination is better supported there; or (b) sidestep for this
    milestone by using Emscripten's own built-in non-threaded OpenAL port
    (`-sUSE_OPENAL=1`) instead of real openal-soft, deferring the
    `ALC_SOFT_loopback`/EFX-capable real audio backend to M5 (Audio, which
    was already its own separate milestone) — M3b's actual goal is just
    proving the full toolchain reaches a real link, not shipping audio.
  - A native (non-Emscripten) `./configure` sanity check hit an unrelated,
    pre-existing environment issue on this machine (Apple clang 17 fails
    this project's C++17-support autoconf check) — confirmed unrelated to
    this session's changes, since it fails at an earlier, independent check
    before the code touched here even runs. Not investigated further; not
    a regression.

## Milestones / Task list

- [x] **M1 — Data-provisioning upload widget** (`web/`, pure JS/TS, no WASM
      dependency, can be built and tested right now)
  - [x] Multi-file / folder drag-and-drop + `<input>` fallback
  - [x] Light, friendly recognition summary (spot known scenario file types;
        don't hard-block unrecognized files)
  - [x] In-memory file collection abstraction, shaped so it can be wired into
        an Emscripten MEMFS/IDBFS bridge later without redesign
  - [x] Unit tests (Vitest + jsdom)
  - [x] Integration test against the real Marathon 2 data (reads from
        outside the repo, skips if absent)
- [x] **M2 — Emscripten toolchain**
  - [x] Install/document emsdk setup (`../emsdk`, see Findings)
  - [x] Prove the toolchain works against the real build: `emconfigure
        ../configure` correctly drives `emcc`/`em++` through the standard
        autoconf checks (from a gitignored `build-wasm/` dir)
- [x] **M3a — Get `emconfigure ../configure` to complete successfully**
      (reduced feature set: core engine + SDL2, no zip/video/networking) —
      see Findings for exactly how each dependency was resolved.
- [x] **M3b-i — Fix the `portable_filesystem.h` gaps `emmake make` surfaced**
      (see Findings): `boost::filesystem`/`std::filesystem` aren't as
      drop-in-compatible as the M3a shim assumed.
- [x] **M3b-ii — Compile networking out entirely via `DISABLE_NETWORKING`**
      (see Findings). This was the decision from the asio-leak finding:
      browsers can't do real networking at all, so there's nothing to
      preserve by fighting asio's platform detection. `emmake make` now gets
      all the way to the final `alephone.wasm` link step.
- [ ] **M3b-iii — pthread/shared-memory ABI mismatch at the final link step.**
      Root-caused (see Findings): a real Emscripten 6.0.6 toolchain
      limitation, not fixable via our own flags. Needs a strategy decision
      (try a different emsdk version, or defer real threaded openal-soft to
      M5 and use Emscripten's built-in non-threaded OpenAL port for now) —
      not decided yet.
- [ ] **M3b-iv — OpenGL detection** (`Not found: OpenGL rendering` —
      configure didn't error, just silently disabled it), and then the real
      compile errors from the legacy-GL renderer (see Findings).
  - [ ] Confirm input (keyboard/mouse/gamepad) via SDL2's Emscripten backend
  - [ ] Rendering: **deferred as its own separately-scoped effort, not part
        of M3b.** GL rewrite (legacy compatibility-profile → GLES2/3-shaped,
        see Findings) is a big enough chunk of work to plan on its own once
        we're past compiling the rest of the engine. User has their own
        ideas for the rendering approach here (possibly significant changes,
        not a like-for-like port) — hardware-accelerated GL is explicitly
        *not* required for a first working build; a software renderer or
        other simplified path for the initial pass is fine. Don't assume
        WebGL is the target until that's actually decided.
- [ ] **M4 — Filesystem bridge**
  - [ ] Feed files collected by the M1 widget into MEMFS at the paths
        `find_files_sdl.cpp`/`FileHandler` expect
  - [ ] IDBFS persistence so re-upload isn't required every session
- [ ] **M5 — Audio**
- [ ] **M6 — Save games / prefs persistence**
- [ ] **M7 (stretch, likely deferred) — Networking** (SDL_net/TCPMess)

## Status

M1 (upload widget), M2 (toolchain), M3a (`emconfigure` completes
successfully), M3b-i (`portable_filesystem.h` gaps fixed), and M3b-ii
(networking compiled out via `DISABLE_NETWORKING`) are done. `emmake make`
reaches the actual final link step for `alephone.wasm` — real progress, not
just per-file compilation. M3b-iii (pthread/shared-memory ABI mismatch) is
now fully root-caused (three separate layers — see Findings) down to a
genuine Emscripten 6.0.6 toolchain limitation (no pthread-aware `libal`/
`libhtml5` variant), not something fixable via our own flags. Two fixed
bugs from this investigation (compiling with `-pthread`, dropping a stray
native `-L/opt/homebrew/lib` that was shadowing the correct wasm32 SDL2
archive) aren't committed yet — they were verified via manual one-off
relinks, not yet folded into a permanent `configure.ac` default, since
they're entangled with an undecided strategy call: pin a different emsdk
version, or use Emscripten's non-threaded built-in OpenAL port for now and
defer real threaded openal-soft to M5. GL rendering (M3b-iv) comes after
that, and is its own separately-scoped effort (see the rendering note
above).
