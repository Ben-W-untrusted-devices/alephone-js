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
    **Resolved: `-pthread` benefits nothing in this codebase — it's purely
    an openal-soft implementation detail, not a functional need.** Before
    picking a workaround, checked whether the engine actually needs real
    threading: it doesn't. Once `Network/` is disabled, single-player
    reaches essentially zero real `SDL_CreateThread`/`pthread_*` calls (the
    one exception, `Misc/Statistics.cpp`'s HTTP telemetry-upload thread,
    isn't guarded by `DISABLE_NETWORKING` even though it's arguably
    network-adjacent — noted for later, not yet fixed). More importantly,
    **audio is already architected the web-friendly way**:
    `OpenALManager::OpenDevice()` calls `alcLoopbackOpenDeviceSOFT`, not a
    real device — audio is *pulled* via `alcRenderSamplesSOFT` from the SDL
    audio callback, exactly the model Web Audio wants (browser calls your
    callback to fill a buffer), not pushed by an internal mixer thread.
    openal-soft's `-pthread` requirement turned out to be unconditional
    regardless of any of this, though: its own `CMakeLists.txt` hard-fails
    ("PThreads is required for non-Windows builds!") because it uses POSIX
    semaphores for its internal command queue, independent of backend or
    rendering mode — so no build-option existed to avoid it while still
    linking real `libopenal.a`. (Aside: the vcpkg build for this platform
    only has the offline "Wave" file-writing backend enabled anyway — no
    real-time device backend exists yet regardless, so real audio output
    was always going to require the loopback+Web-Audio integration work as
    its own M5 task.)
    **Fix applied**: for the Emscripten target, `configure.ac` now keeps
    vcpkg's real `AL/alext.h` headers (needed for the
    `LPALCLOOPBACKOPENDEVICESOFT` typedef the code uses) but stops linking
    `libopenal.a` entirely — the code only ever loads SOFT-extension
    functions dynamically via `alcGetProcAddress`, never direct linkage, so
    Emscripten's own bundled AL/ALC stub (auto-linked by default,
    `AUTO_NATIVE_LIBRARIES`) satisfies linking instead. Extension lookups
    will return NULL (no audio) until real threaded openal-soft is
    revisited in M5, deliberately, alongside the loopback+WebAudio glue
    code and whatever SharedArrayBuffer/cross-origin-isolation deployment
    headers that ends up needing.
  - **The working `emconfigure` recipe, end to end** (from a clean
    `build-wasm/` dir, after `source ../../emsdk/emsdk_env.sh` and
    `sh ../vcpkg/install-wasm32-emscripten.sh`):
    ```
    emconfigure ../configure --with-boost=/opt/homebrew \
      --without-zzip --without-vpx --without-matroska --without-ebml \
      --without-vorbis --without-vorbisenc --without-nfd --without-catch2 \
      --without-curl --without-png \
      CPPFLAGS="-I/opt/homebrew/include" \
      PKG_CONFIG_PATH="<repo>/vcpkg/installed-wasm32-emscripten/wasm32-emscripten/lib/pkgconfig"
    emmake make
    ```
    Notably **no `-pthread` and no `-L/opt/homebrew/lib`** — both were
    needed transiently while debugging (see above) but neither belongs in
    the real recipe. `-I/opt/homebrew/include` is still needed (Boost/asio
    headers only, native `brew install boost asio`, never linked). Produces
    `build-wasm/Source_Files/alephone.wasm` — confirmed a real WebAssembly
    binary via `file`. No rendering yet (`Not found: OpenGL rendering`).
  - **Verified it actually runs, not just links (M3c).** The default
    `emmake` target links to a bare `alephone`/`alephone.wasm` with no
    `.html`/`.js` glue (Emscripten defaults to a Node-runnable script
    without an explicit `-o foo.html`). Quick free check: `node alephone
    --help` printed the real, correctly-formatted CLI usage banner — proves
    `main()`, argument parsing, and basic I/O work. Running it for real
    (`node alephone --nogl --nosound --nojoystick`, no args) got exactly as
    far as expected: crashed in `SDL_Init` → `Emscripten_VideoInit` →
    `emscripten_get_screen_size` with `ReferenceError: screen is not
    defined` — a genuine browser-only DOM API (`window.screen`), i.e. Node
    itself was the limitation, not the port. Relinking the same object
    files with `-o alephone.html` (a one-off manual relink for this test,
    not yet how the Makefile builds it) and loading that in a real browser
    tab got much further: **no crash**, a real black canvas rendered (SDL2
    actually created a browser window/canvas — genuine visual proof, even
    with zero rendering code enabled), console output streamed live to the
    page, and the game reached its own error handling: *"Please be sure
    the files 'Map', 'Shapes', 'Images' and 'Sounds' are correctly
    installed and try again."* That's the engine correctly reporting no
    scenario data is available — expected, since the M1 upload widget
    isn't wired into MEMFS yet (M4). **This is the real evidence for
    reprioritizing M4 ahead of M3b-iv/OpenGL**: the engine reaches its data-
    loading step, and therefore blocks on missing data, before it would
    ever reach a rendering code path — so the filesystem bridge is the
    actual next thing standing in the way, not GL.
  - **`network_dummy.cpp` was missing ~15 stub definitions** for
    declarations `network.h`/`network_games.h` had grown since this file
    was last touched — unsurprising, since (per the earlier finding)
    `DISABLE_NETWORKING` had never actually been wired to `config.h`
    before this session, so this dummy path had likely never actually been
    linked by anyone. Added matching stubs (`NetGetStats`,
    `NetUpdateUnconfirmedActionFlags`, `get_player_net_ranking`,
    `hub_get_minimum_send_period`, etc.) following the existing file's
    plain no-op/empty-default style.
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
- [x] **M3b-iii — pthread/shared-memory ABI mismatch, resolved.** Root-caused
      to real openal-soft's unconditional pthread requirement (see
      Findings) — decided to keep vcpkg's real AL headers (for the
      `LPALCLOOPBACKOPENDEVICESOFT` typedef) but stop linking real
      `libopenal.a`, relying on Emscripten's own bundled AL/ALC stub
      instead. No more `-pthread` anywhere in the build.
- [x] **M3b-v — Finish `network_dummy.cpp`.** Added the ~15 stub
      definitions `network.h`/`network_games.h` had grown since this dummy
      file was last touched (never actually linked before — see Findings).
- [x] **M3b — `emmake make` produces a real `alephone.wasm`.** 🎉 First
      successful end-to-end build: `emconfigure`/`emmake` through the whole
      tree, zero errors, real WebAssembly binary
      (`build-wasm/Source_Files/alephone.wasm`, confirmed via `file`). See
      Findings for the exact working `emconfigure` recipe.
- [x] **M3c — Verified `alephone.wasm` actually runs**, in both Node (quick
      free sanity check) and a real browser tab (the actual target). See
      Findings — this is real, not just "it links."
- [ ] **M3b-iv — OpenGL.** `Not found: OpenGL rendering` — configure didn't
      error, just silently disabled it, so the current build has no
      rendering at all yet. Then the real compile errors from the legacy-GL
      renderer (see Findings). **Not the actual next blocker** — M3c showed
      the engine reaches its own "can't find game data" error before it
      would ever reach rendering, so **M4 (filesystem bridge) is next**,
      not this.
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

**M1 through M3b are done. `emmake make` produces a real, working
`alephone.wasm`** — the first successful end-to-end build of this port,
zero errors. That covers: the upload widget (M1), the toolchain (M2),
`emconfigure` completing (M3a), the `portable_filesystem.h` gaps (M3b-i),
networking compiled out via `DISABLE_NETWORKING` (M3b-ii), the
pthread/shared-memory ABI mismatch (M3b-iii, root-caused to openal-soft's
unconditional internal requirement and resolved by not linking real
`libopenal.a` — see Findings), and finishing `network_dummy.cpp`'s missing
stubs (M3b-v). The exact working `emconfigure` recipe is in Findings.

**M3c is also done: verified `alephone.wasm` actually runs**, not just
links — in a real browser tab it reaches `main()`, initializes SDL2 (a
real canvas renders), and gets to the game's own "can't find Map/Shapes/
Images/Sounds" error, since no game data has been fed in yet (see
Findings for the full run, including a Node-only run that usefully failed
earlier at a genuine browser-only API, `window.screen`, confirming Node
was the limiting factor there, not the port).

That result reprioritizes what's next: **M4 (filesystem bridge) is next,
not M3b-iv/OpenGL.** The engine reaches its data-loading step — and
therefore blocks on missing data — before it would ever reach a rendering
code path, so wiring the M1 upload widget into MEMFS is the actual next
thing standing in the way of seeing more of the engine run, not GL. OpenGL
remains its own separately-scoped effort for whenever rendering is
tackled (see the rendering note above) — the user has their own ideas for
that approach, and hardware-accelerated GL is explicitly not required for
a first working build.
