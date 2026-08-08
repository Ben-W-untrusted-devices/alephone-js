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
    `build-wasm/` dir, after `source ../../emsdk/emsdk_env.sh`,
    `sh ../vcpkg/install-wasm32-emscripten.sh`, and — see M4d —
    `sh ../vcpkg/install-wasm32-emscripten-exc.sh`):
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
    headers only, native `brew install boost asio`, never linked). As of
    M4d, `-fwasm-exceptions -sSUPPORT_LONGJMP=wasm` no longer needs to be
    passed manually either — `configure.ac` adds it automatically for the
    Emscripten target (see M4d in Findings below for why, and for two real
    gotchas worth knowing about before hand-editing `configure.ac` or
    `CXXFLAGS` again). Produces `build-wasm/Source_Files/alephone.wasm` —
    confirmed a real WebAssembly binary via `file`. No rendering yet
    (`Not found: OpenGL rendering`).
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
  - **M4: the filesystem bridge from the M1 upload widget to MEMFS.** Added
    [web/src/fs/EmscriptenFS.ts](web/src/fs/EmscriptenFS.ts) (a minimal
    `mkdirTree`/`writeFile` interface, kept separate from the real, much
    larger Emscripten `FS` type so this stays unit-testable against a plain
    fake) and
    [web/src/fs/mountUploadedFiles.ts](web/src/fs/mountUploadedFiles.ts)
    (writes every `UploadableFile`'s bytes into that FS under a mount root,
    recreating the dropped folder's directory structure). Reads file bytes
    via `FileReader` rather than the newer `Blob.arrayBuffer()` — the latter
    isn't implemented by jsdom (this project's unit test environment) as of
    the version in use here, even though real browsers support it.
  - **A newly-discovered bug blocked `emmake make` from being reused
    reliably: automake was using the native macOS `ar`/`ranlib`, not
    Emscripten's `emar`/`emranlib`.** `configure.ac`/`Makefile.am` never
    override `AR`/`RANLIB`, and `emconfigure` doesn't set them either (only
    `CC`/`CXX`). This went unnoticed through M3b/M3c because those archives
    were only ever *created* once; the bug only surfaces when an archive is
    *rewritten* (e.g. after touching one source file and rebuilding) — the
    native tools don't understand WASM object files, and silently produced
    a corrupt `.a` (`ranlib: ... the table of contents is empty` was the
    tell). Symptom at final link time: `wasm-ld: LLVM ERROR: malformed
    uleb128, extends past end`. Fix: always build with `AR=emar
    RANLIB=emranlib` (now baked into `web/build-engine.sh`) — a real
    root-caused fix, not a workaround. Worth adding to the `emconfigure`
    recipe above for anyone reproducing this build from scratch, though the
    configure-time recipe itself doesn't need to change (only the `make`
    invocation).
  - **Formalized the M3c one-off manual relink into
    [web/build-engine.sh](web/build-engine.sh).** Automake's link rule (see
    `Source_Files/Makefile`'s `CXXLINK`) places `-o alephone` right after
    the compiler flags, followed by the entire object/library list — not at
    the end of the line — so there's no `LDFLAGS` override that redirects
    output to `.html`/`.js`: anything passed in `LDFLAGS` lands *before*
    automake's own `-o alephone`, which then wins. The script instead
    captures the real link command (the correct object/library list,
    computed by automake) via a forced dry run (`make -n -B V=1 alephone`,
    grepping for the line containing literal ` -o alephone `, since that
    substring is unique to the link step — compile steps only ever produce
    `-o foo.o`), then re-runs it with extra flags appended, including a
    fresh `-o` — since emcc (like clang) honors the *last* `-o` flag, that's
    enough to redirect the output without editing the original command.
    Added flags: `-sMODULARIZE=1 -sEXPORT_ES6=1
    -sEXPORT_NAME=createAlephOneModule` (an importable ES module default-
    exporting an async factory function, rather than a global), `-
    sEXPORTED_RUNTIME_METHODS=FS,callMain` (both needed by the JS-side
    bridge), `-sFORCE_FILESYSTEM=1` (keeps FS fully linked in even though
    nothing calls it before `main()`), `-sINVOKE_RUN=0` (so `main()` isn't
    auto-invoked — the bridge mounts files into FS first, then calls
    `Module.callMain(["/data"])` itself), `-sENVIRONMENT=web`.
  - **Output deliberately lives at `web/engine/`, not `web/public/`.** Vite
    refuses to `import()` (even a dynamic one, even with `/* @vite-ignore
    */`) any `.js` file that resolves inside its configured `publicDir`
    (default `public/`) — by design, files there are meant to be referenced
    only via `<script src>`/`<link href>`, copied as-is on build. Since
    `alephone.js` needs to be `import()`ed (it's the whole point of
    `-sEXPORT_ES6=1` + `MODULARIZE`), the build output goes to its own
    plain static directory instead, which Vite's dev server still serves
    correctly (any real file under the project root is served, not just
    `publicDir`'s contents) without the import restriction applying.
  - **Manually driving the compiled engine from a plain `<script
    type="module">` (no bundler) is real, verified progress: `main()` now
    runs against browser-supplied data**, not just a canvas + a "no data"
    error (M3c). Added [web/game.html](web/game.html), a manual test
    harness (upload widget + "Start engine" button + canvas), and drove it
    via the Browser pane tool. Two real, non-obvious things were found this
    way that no amount of reading the C++ source would have surfaced
    quickly:
    1. **⚠️ Superseded by M4g below — kept for the reasoning history, but
       the rename this led to was wrong and has been reverted.** The claim
       was that the engine looks up its core scenario files by exact,
       extensionless name (`"Map"`, `"Shapes"`, `"Images"`, `"Sounds"` —
       see `Source_Files/Misc/DefaultStringSets.cpp`'s `"Filenames"` string
       set and `Source_Files/Files/preprocess_map_sdl.cpp`'s
       `have_default_files()`/`get_default_spec()`, which does a plain
       `access(GetPath(), R_OK)` against that literal name — no extension
       matching, no cleverness). The `.sceA`/`.shpA`/`.sndA`/`.imgA`
       extensions (used by this project's own recognized-extension table,
       `knownFileTypes.ts`, mirroring `FileHandler.cpp`'s) are for this
       widget's own "looks like a scenario" recognition and, separately,
       for file-browser/chooser UI — *not* for how the engine finds its
       default files. Confirmed empirically: mounting synthetic files named
       `Map.sceA`/`Shapes.shpA`/etc. still produced the "please be sure the
       files ... are correctly installed" error; renaming the same files to
       exactly `Map`/`Shapes`/etc. made that check pass and the engine
       proceeded into real initialization. **`mountUploadedFiles` now
       renames the four recognized top-level scenario files to their
       canonical extensionless names automatically** (a small fixed table,
       `CANONICAL_TOP_LEVEL_NAMES`) — without this, a normal user dropping
       a real, normally-named scenario folder would silently hit this
       error forever, so it's a correctness fix, not polish. Saved games,
       films, and physics models aren't looked up this way (the physics
       model isn't even required — `get_default_physics_spec` "doesn't care
       if it does not exist") and keep their real names.
    2. **`mountUploadedFiles` also strips a single shared leading folder
       across all uploaded files** (e.g. `"Marathon 2/Map.sceA"` mounts as
       `/data/Map`, not `/data/Marathon 2/Map`), only when every file
       shares the same first path segment. Native Aleph One expects
       scenario data (`Map`, `Plugins/`, `Scripts/`, ...) directly inside
       the one directory it's pointed at
       (`data_search_path`/`shell_options.directory` — see `shell.cpp`);
       the folder-picker/drag-drop widget naturally includes the dropped
       folder's own name as the first path segment
       (`webkitRelativePath`/`FileSystemEntry` walk), which would otherwise
       nest everything one level too deep — breaking not just the four
       canonical files but also `Plugins`/`Scripts` discovery (which scans
       for those subdirectory names directly under each `data_search_path`
       entry). Both fixes were verified together against the real
       Emscripten `FS` (not just the unit tests' fake): a synthetic upload
       shaped like the real Marathon 2 folder layout (`Marathon
       2/Map.sceA`, `.../Shapes.shpA`, `.../Images.imgA`,
       `.../Sounds.sndA`, `Marathon 2/Plugins/MyMod/SubMap.sceA`) produced
       exactly `/data/Map`, `/data/Shapes`, `/data/Images`, `/data/Sounds`,
       `/data/Plugins/MyMod/SubMap.sceA` — canonical names at the top,
       original names/nesting preserved underneath. (Verified with
       synthetic placeholder bytes under names matching the real Marathon 2
       layout, never with the real copyrighted file contents — no need to
       touch the actual data to prove the *path* logic is correct, and
       serving that real data over even a local, ephemeral HTTP port for
       test purposes was correctly refused by this session's safety
       tooling, which is the right call per the hard constraint above.)
  - **M4g — the extensionless-name rename above was wrong, and broke real
    scenarios specifically. Corrected, and verified against real Marathon 2
    data.** After M4d/M4e/M4f landed, a user retry with the real Marathon 2
    folder kept hitting the "please install the files" alert (`error -1`)
    even though a diagnostic build confirmed `Map`/`Shapes`/`Images`/
    `Sounds` were genuinely present under exactly the renamed canonical
    names. Root-caused by finally testing against the real data directly —
    not the browser (which can't run a native file picker via automation,
    and serving the data over HTTP was correctly refused, as above) but
    locally via **Node + Emscripten's NODEFS** (a real host-filesystem
    passthrough into the WASM virtual FS; the real data is only ever read
    from disk into this Node process's own memory, via `FS.readFile`, and
    is never written back, transmitted, copied into the repo, or bundled —
    same constraint the existing `realMarathon2Data.test.ts` already
    operates under, just exercising the compiled engine instead of just
    the TS layer). This gave a fast, reliable local iteration loop — no
    browser flakiness, immediate real stack traces — that the browser
    round-trips this whole investigation had been relying on couldn't
    match.
    - Real cause: `../Marathon 2/Scripts/Filenames.mml` — a real, ordinary
      part of the actual Marathon 2 scenario, not a corruption or edge
      case — contains a `<stringset index="129">` block explicitly
      overriding the engine's default filename lookups to the scenario's
      *actual* on-disk names: `Shapes.shpA`, `Sounds.sndA`, `Map.sceA`,
      even `Physics Models/Standard.phyA` (a subdirectory path). This is
      loaded automatically by the ordinary MML-loading machinery
      (`LoadBaseMMLScripts`, `shell.cpp`), which already runs against
      whatever's mounted at `data_search_path` before any of these lookups
      happen — exactly the same mechanism a native install already relies
      on. **The M4a rename directly defeated this**: stripping the
      extension to produce `"Map"` left nothing at the path the
      scenario's own script was, correctly, asking for (`"Map.sceA"`).
      Confirmed directly: relinking with the diagnostic restored and
      running via the Node+NODEFS harness showed `get_default_spec`
      searching for `Map.sceA`/`Shapes.shpA`/`Sounds.sndA`/`Images.imgA`
      specifically (not the bundled engine defaults) — and, after removing
      the rename, every one of those lookups succeeding
      (`Exists()=1`) against the real, unmodified files.
    - **Fix**: removed `mountUploadedFiles`'s canonicalization step
      entirely (`CANONICAL_TOP_LEVEL_NAMES` and its use — see the current
      `web/src/fs/mountUploadedFiles.ts`). Files now keep their real names
      throughout, exactly as dropped/picked — only the shared leading
      wrapper folder is still stripped (that part was correct, and
      independently re-verified against the real data below). This isn't
      a narrower fix for one scenario: it's the *general* case, since any
      scenario can define its own `Filenames.mml` (or omit one, falling
      back to the engine's own bundled defaults, `"Map"` etc. — which is
      presumably why the M4a testing, using synthetic data with no
      `Filenames.mml` of its own, never caught this). Updated
      `web/test/mountUploadedFiles.test.ts` to match (real names preserved
      throughout, not renamed).
    - **Verified end-to-end against the real Marathon 2 data**, via the
      Node+NODEFS harness: mounted the real folder unmodified, ran
      `callMain`, and confirmed every one of `have_default_files()`'s
      lookups now succeeds (`Map.sceA`, `Images.imgA`, `Shapes.shpA` all
      `Exists()=1`), and execution proceeds past all data-loading and
      preferences logic into actual window/screen creation
      (`SDL_CreateWindow` → `Emscripten_CreateWindow`), where it hits
      `ReferenceError: document is not defined` — expected and correct:
      Node has no DOM, the same category of Node-only limitation M3c
      already found with `window.screen`. This is real proof the fix
      works against the actual data; the remaining boundary is purely
      "Node isn't a browser," not anything left to fix here. Also
      re-verified the corrected, no-rename `mountUploadedFiles` in the
      browser harness with synthetic data to confirm nothing else
      regressed.
    - **A safety note on the Node+NODEFS approach**: NODEFS mounts are a
      real, writable passthrough to the host filesystem — mounting it
      directly at the path the engine operates on would risk the engine
      writing back to the real Marathon 2 folder (e.g. any incidental
      lock/cache file). The harness instead mounts NODEFS at a separate
      staging path used *only* for `FS.readFile`, then copies those bytes
      into MEMFS (in-process memory only) for the engine to actually run
      against — the real directory is only ever opened for reading.
  - **New, more significant blocker found only by getting main() to
    actually run against present data: `main_event_loop()` (`shell.cpp`) is
    a classic blocking `while` loop, incompatible with a browser tab's
    single-threaded, cooperative execution model.** Confirmed directly:
    once `have_default_files()` passes (see above), `Module.callMain(["/
    data"])` never returns and the entire tab stops compositing/responding
    (had to be force-closed) — consistent with a synchronous loop that
    never yields back to the browser's own event loop, which is also why
    nothing under Emscripten can block-and-wait the way native code can.
    This is architecturally the same *category* of problem as the deferred
    GL rendering work (M3b-iv): a real, scoped chunk of adaptation work,
    not a flag flip. The two standard fixes are `emscripten_set_main_loop`
    (restructure the loop into a per-frame callback the browser drives —
    more invasive to existing code, conflicts somewhat with this project's
    "avoid touching existing C++" convention, though this would be a
    "genuinely necessary" case) or Asyncify (`-sASYNCIFY=1`, lets the
    existing blocking-loop shape keep working by transparently
    yielding/resuming around specific calls — much less invasive to the
    C++, but has real binary-size/performance cost and needs the
    async-safe call graph marked up). **Not started or decided this
    session** — deliberately left as an open, explicitly-flagged decision
    for the same reason GL rendering was deferred (see M3b-iv note): it's a
    real architectural choice, not obviously not the user's to make
    unilaterally, and better to surface clearly than to guess.
  - **Decision: `emscripten_set_main_loop`, not Asyncify.** Before deciding,
    measured Asyncify's actual cost on this build rather than quoting
    generic numbers: relinking with `-sASYNCIFY=1` (whole reachable call
    graph, no `ASYNCIFY_ONLY` restriction) produced a working
    `alephone.wasm` at +9% size (47.98MB → 52.28MB) on this `-g -O2` debug
    build — smaller than the commonly-quoted 50-100%+, though a stripped
    `-O3` release build would likely show a higher *relative* overhead
    (smaller baseline diluting less). Runtime perf cost wasn't measured
    (would need real gameplay through a hang-prone harness). Investigated
    scope for the alternative first: `main_event_loop()` isn't the only
    blocking loop — `dialog::run()` (`Misc/sdl_dialogs.cpp:2232`) is a
    second, structurally identical one, called *synchronously from inside*
    `main_event_loop()`'s call tree (e.g. `iQuitGame` →
    `quit_without_saving()` → `d.run()`, `interface.cpp:1357`;
    `iPreferences` → `handle_preferences()` → `d.run()`,
    `interface.cpp:1422`), at ~28 live call sites (`grep -rn "\.run("`,
    minus ones compiled out by `DISABLE_NETWORKING`). Given the loop isn't
    waiting on any genuinely async browser API (no `fetch`/`await` — it's
    self-imposed blocking from polling SDL events), this is the textbook
    case Emscripten's own docs point to `emscripten_set_main_loop` for, not
    Asyncify's actual strength (arbitrary-depth resumable yields onto a
    real async operation). Chose to accept the wider blast radius (many
    call sites, not one) over Asyncify's size/perf tax and toolchain
    complexity.
  - **M4c-i (done): `main_event_loop()` converted, verified end-to-end —
    no more hang.** `shell.cpp`'s `main_event_loop()` now branches on
    `__EMSCRIPTEN__`: the loop body was factored out, unchanged, into
    `main_event_loop_iteration()` (native builds still just call it from
    the original `while` loop, so this is a pure refactor there — verified
    by diffing the extracted body against the original), and the
    Emscripten branch instead calls `emscripten_set_main_loop(callback, 0,
    1)`. `last_event_poll` became a function-local `static` rather than a
    loop-local, which is behavior-preserving in both branches since
    `main_event_loop()` itself is only ever entered once per program
    lifetime (`main.cpp`) either way. Because
    `simulate_infinite_loop=1` means the call never returns to its caller
    (Emscripten unwinds the C++ stack internally), nothing after it in
    `main_event_loop()`/`main()` runs anymore to notice `_quit_game` and
    call `shutdown_application()` — the per-frame callback does that job
    itself now instead (checks for `_quit_game` first, cancels the loop,
    and shuts down, rather than running an iteration). Verified for real,
    not just "it compiles": mounted synthetic scenario files, called
    `Module.callMain(["/data"])`, and confirmed the tab stayed fully
    responsive throughout — `screenshot` succeeded promptly (previously it
    timed out with "the page is not compositing frames"), a real in-canvas
    dialog rendered correctly, a live `computer` click on its button was
    processed, and the engine shut down cleanly in response
    (`shutdown_application()` ran, confirmed by the page's visible state
    changing). Also caught and worked around an unrelated environment
    issue while testing: port 5173 was occupied by a stray, unrelated dev
    server from something else on this machine (confirmed by loading a
    completely different app's UI) — `curl`'s 200 status code alone wasn't
    enough to catch this, only checking actual page content was; fixed by
    pointing `web/build-engine.sh`'s dev server at port 5180 instead (see
    `.claude/launch.json`, one directory above this repo).
  - **M4c-ii (not done, and not proven safe by the above test): every
    `dialog::run()` call site.** The one verified above (`alert_user`'s
    fatal-error dialog, `CSeries/csalerts_sdl.cpp:145`) happened to work in
    that manual test — the tab responded to a click on it and quit
    cleanly — but this should *not* be read as evidence that
    `dialog::run()` is now safe in general. `dialog::run()`'s own loop
    calls `yield()` (`CSeries/csmisc_sdl.cpp:71`), which is just
    `std::this_thread::yield()` — a genuine no-op on a single-threaded,
    non-Asyncify Emscripten build, since there's no other OS thread to
    yield to. The manual test's click and the follow-up check were two
    separate tool calls with real (if small) wall-clock latency between
    them, which is plausibly why it happened to work — not proof that a
    still-synchronous `dialog::run()` call reliably processes input without
    stalling the tab under less forgiving timing (rapid interaction,
    heavier dialogs like Preferences' nested sub-dialogs, slower devices).
    Treat `dialog::run()` as still needing the same
    `emscripten_set_main_loop`-shaped treatment as `main_event_loop()` got
    — converting call sites like `quit_without_saving()`/
    `handle_preferences()` from "call a function, block, get a result,
    keep going" into an explicit trigger-and-resume-next-frame shape — as
    a real, separate follow-up task, not yet started.
  - **First real-scenario-data test (by the user, with their own local copy
    of Marathon 2 — never touched by this session, per the hard rule)
    aborted right after the startup banner: `Aborted(). Build with
    -sASSERTIONS for more info.`** Root cause suspected before confirming:
    `build-engine.sh` never set `-sALLOW_MEMORY_GROWTH`, so the WASM heap
    was a fixed size decided at link time — real Marathon 2 data is ~48MB
    across just `Map.sceA`/`Shapes.shpA`/`Sounds.sndA`/`Images.imgA` (the
    144-file mount count the user reported also includes `Plugins/`,
    `Scripts/`, `Physics Models/`, `Demos/`), which a small default heap
    has no chance of holding once parsed into the engine's own structures.
    Added `-sALLOW_MEMORY_GROWTH=1` (the fix) and `-sASSERTIONS=1`
    (diagnostic — turns Emscripten's generic "Aborted()" into a specific
    reason) to `build-engine.sh` and rebuilt.
  - **Attempting to verify this by simulating the real data's size (never
    its content) surfaced a second, genuinely different bug: zero-filled
    placeholder bytes at the same sizes hang the tab outright** — even a
    trivial `1+1` eval never returned, confirming a real main-thread
    deadlock, not a display glitch. This is almost certainly the map/WAD
    parser looping on structurally-invalid header data (an all-zeros
    buffer is not a valid WAD, unlike a real `Map.sceA`), not the same bug
    the user hit (a clean `Aborted()`, not a hang) — so this specific
    repro is a false lead for *that* bug, but it's a real, separate
    robustness gap worth noting: malformed/corrupt scenario data can hang
    the tab under Emscripten (unrecoverable without reloading), where
    native Aleph One would at worst error out or the user could force-quit.
    Not root-caused or fixed — parked here rather than chased further,
    since it needs either a deliberately-crafted malformed-but-parseable
    WAD (real reverse-engineering effort) or, more practically, just
    seeing whether it recurs with real data once the memory fix is in.
    **Next step is on the user**: retry with the real Marathon 2 folder
    now that `-sALLOW_MEMORY_GROWTH`/`-sASSERTIONS` are in the rebuilt
    engine — if it still aborts, the message should now name the actual
    failure instead of a bare `Aborted()`.
  - **The retry (still by the user, real data) did abort again, and
    `-sASSERTIONS=1` did its job: the real failure is a thrown C++
    exception with no exception-catching support linked in** —
    `Aborted(Assertion failed: Exception thrown, but exception catching is
    not enabled. Compile with -sNO_DISABLE_EXCEPTION_CATCHING or
    -sEXCEPTION_CATCHING_ALLOWED=[..] to catch.)`. Emscripten disables
    C++ exception *catching* by default (a size/perf tradeoff) — `throw`
    still compiles, but nothing unwinds to a `catch`, so any throw is a
    hard abort regardless of how many `try`/`catch` blocks exist in the
    source (and this codebase has many, including right around the
    suspected call site — see below). The original stack trace (before
    `-sASSERTIONS`) named `std::__2::locale::use_facet`, which is exactly
    what `std::use_facet` does per the standard when the requested facet
    isn't installed: throws `std::bad_cast`.
  - **Root-caused, without touching real Marathon 2 data, to
    `PluginLoader::ParsePlugin` (`XML/Plugins.cpp:349`) parsing a real
    scenario's `Plugins/*/Plugin.xml`.** This code path only runs when a
    `Plugins/` folder is present — absent from every earlier synthetic
    test in this session, present in the real Marathon 2 folder (part of
    why the very first 4-file smoke test never hit this). Suspected the
    numeric `<checksum>` extraction inside `<map_patch>` handling
    (`cs_tree.get_value(static_cast<uint32_t>(0))`, line ~504) —
    `InfoTree` is a `boost::property_tree` tree, and its numeric
    `get_value<T>()` goes through a stream-based translator
    (`stringstream >> value`), which is locale-sensitive. Confirmed by
    reproduction: mounted a **synthetic** `Plugin.xml` using Aleph One's
    own public, open plugin-manifest schema (documented directly in
    `Plugins.cpp` — not copyrighted Marathon 2 content) containing a
    `<map_patch><checksum>305419896</checksum>...` element alongside tiny
    placeholder `Map`/`Shapes`/`Images`/`Sounds` files, and got the
    identical `Aborted(Assertion failed: Exception thrown...)` message.
  - **Two fix attempts, both real, both currently blocked — this needs
    dedicated follow-up, not a quick patch:**
    1. **Legacy JS-based exception catching** (`-sDISABLE_EXCEPTION_CATCHING=0`,
       what the abort message itself suggests): confirmed it's a link-time-
       only flag — a manual relink (reusing the existing, unmodified `.o`
       files, no recompile) picked up a different, exception-aware system
       library variant (`libc++-debug.a`/`libc++abi-debug.a` instead of the
       `-noexcept` variants) and linked successfully. But running the same
       synthetic-plugin repro against that build failed differently, with
       `null function` — some ABI/runtime mismatch between our already-
       compiled `.o` files and the newly-linked exception-aware runtime.
       Not resolved; may need our own object files recompiled too (with
       `-fexceptions` or equivalent), not just relinked.
    2. **Modern WASM-native exceptions** (`-fwasm-exceptions`, Emscripten's
       currently-recommended approach — better runtime characteristics
       than the JS-based emulation): requires the flag at *both* compile
       and link time, so re-ran `emconfigure` with
       `CXXFLAGS=CFLAGS="-fwasm-exceptions"` and did a full rebuild. That
       surfaced a *third* problem: `-fwasm-exceptions` also changes the
       underlying `setjmp`/`longjmp` codegen model (Emscripten's own
       `-mllvm -wasm-enable-sjlj`), and vcpkg's prebuilt `libfreetype.a`/
       `libpng16.a` (which use `setjmp`/`longjmp` internally for their own
       error handling, as does this project's bundled Lua's `ldo.o`) were
       built without it — link failed with `undefined symbol:
       emscripten_longjmp` across all of them. Fixing this route means
       rebuilding those vcpkg dependencies with matching flags too, not
       just this project's own code — a real, but bigger, undertaking
       (`vcpkg/install-wasm32-emscripten.sh` would need the matching
       compiler flags threaded through, and Lua's `ldo.o` reconsidered).
    Reverted the `-fwasm-exceptions` `emconfigure` run and rebuilt clean
    (confirmed via the browser harness that the baseline 4-file smoke test
    still passes) rather than leave `build-wasm/`/`web/engine/` in a
    broken state — `build-engine.sh` currently ships neither fix.
  - **This needs to land eventually — this codebase relies on real,
    working C++ exception handling throughout** (not just `Plugins.cpp`:
    `main.cpp`'s own `main()` wraps everything in `catch
    (std::exception&)`/`catch (...)`, expecting it to actually work), so
    "just don't hit code paths that throw" isn't a real long-term option
    — some real scenario data (evidently including plausible, ordinary
    plugin manifests with `map_patch`/`checksum` elements) will always hit
    this.
  - **Resolved: went with modern WASM-native exceptions
    (`-fwasm-exceptions`/`-sSUPPORT_LONGJMP=wasm`), not the legacy JS-based
    route.** Asked the user directly given both fixes were genuinely
    blocked and involved real tradeoffs (see the pros/cons above) rather
    than picking unilaterally; the user chose the modern route knowing it
    needed vcpkg dependency work.
    - **`Lua/liba1lua.a(ldo.o)`'s `emscripten_longjmp` turned out to need
      one more flag, not vcpkg work**: `-fwasm-exceptions` alone only
      covers C++ exception codegen; plain-C `setjmp`/`longjmp` (which
      Lua's `ldo.o` uses for its own error handling) needs the *separate*
      `-sSUPPORT_LONGJMP=wasm` setting to match. Adding it to
      `CFLAGS`/`CXXFLAGS` resolved Lua on its own, with zero vcpkg
      changes — narrowing the real remaining problem to just
      `libfreetype.a`/`libpng16.a` (vcpkg-prebuilt, pulled in transitively
      by `sdl2-ttf`; nothing else in the dependency graph uses
      `setjmp`/`longjmp`/C++ exceptions internally, so nothing else needed
      touching).
    - **Added a custom vcpkg triplet**,
      [vcpkg/custom-triplets/wasm32-emscripten-exc.cmake](vcpkg/custom-triplets/wasm32-emscripten-exc.cmake)
      — identical to vcpkg's own community `wasm32-emscripten` triplet,
      plus `VCPKG_C_FLAGS`/`VCPKG_CXX_FLAGS` set to the same two flags —
      and [vcpkg/install-wasm32-emscripten-exc.sh](vcpkg/install-wasm32-emscripten-exc.sh),
      which builds just `freetype`+`libpng` under it (into a separate
      `installed-wasm32-emscripten-exc/` root, gitignored like the other
      `installed-*` dirs) and copies the resulting `.a` files **over** the
      incompatible ones already installed under the plain
      `wasm32-emscripten` triplet. Nothing else (`sdl2`, `sdl2-ttf`
      itself, `openal-soft`, `libsndfile`) needed rebuilding — static
      libraries don't bake in their dependencies' code, so swapping just
      the two `.a` files this way is enough; run this script *after*
      `install-wasm32-emscripten.sh`.
    - **Made `configure.ac` add both flags automatically for the
      Emscripten target** (appended to `CXXFLAGS`/`CFLAGS`, not assigned —
      see the comment in `configure.ac` right after the `ax_target_emscripten`
      check), rather than requiring every future `emconfigure` invocation
      to remember to pass them by hand. This turned out to matter for a
      real reason, not just tidiness (see the next two findings).
    - **Real gotcha #1 — a naive `CXXFLAGS="-fwasm-exceptions
      -sSUPPORT_LONGJMP=wasm"` *assignment* (not append) during manual
      testing silently dropped the project's usual `-g -O2`**: autoconf
      only supplies its own `-g -O2` default when `CXXFLAGS` is *unset* in
      the environment; setting it to anything at all — even just to add
      one flag — replaces the default outright, autoconf does not merge
      the two. Symptom was subtle and easy to misattribute: the resulting
      `alephone.wasm` was suspiciously *smaller* (48MB → ~9.75MB, no
      debug info) and *ran*, but crashed differently — `memory access out
      of bounds` / `Stack overflow detected. You can try increasing
      -sSTACK_SIZE (currently set to 65536)` — from unoptimized code's
      much larger per-call stack frames blowing the default 64KB stack
      during boost property_tree's recursive XML parsing. Fixed by
      appending (`CXXFLAGS="$CXXFLAGS ..."`) instead of assigning, both in
      the final `configure.ac` change and in manual testing from then on.
    - **Real gotcha #2 — editing `configure.ac` doesn't do anything by
      itself**: `configure.ac` is a template; the actual `configure` shell
      script `emconfigure` runs is a *generated* file (gitignored, not
      tracked) that only gets regenerated by running `autoreconf -i` (or
      `autoconf`) again. Spent one full reconfigure+rebuild+test cycle not
      noticing this — the edited `configure.ac` silently had zero effect
      until `autoreconf -i` was run. Worth remembering for any future
      `configure.ac` edit, not just this one.
    - **Verified end-to-end, twice** (once with the flags passed
      manually, again after moving them into `configure.ac` and
      confirming a plain, undecorated `emconfigure` invocation now
      produces `CXXFLAGS: -g -O2 -fwasm-exceptions -sSUPPORT_LONGJMP=wasm`
      on its own): the exact synthetic-plugin repro that previously
      aborted with `Exception thrown, but exception catching is not
      enabled` now runs `callMain` to completion with no error, no crash,
      and the tab stays fully responsive (screenshot succeeds
      immediately) — confirmed via a from-scratch clean rebuild (all
      `.o`/`.a` deleted first, per the lesson from M4c-i) to rule out any
      stale-object contamination from the earlier failed attempts.
  - **A user retry with real Marathon 2 data hit a second, different real
    abort: `Stack overflow! Stack cookie has been overwritten...`** —
    Emscripten's default WASM stack (`-sSTACK_SIZE`, 64KB) is small even
    by WASM standards, and the minimal one-element synthetic plugin repro
    above never exercised enough recursion depth (through boost
    property_tree's parser, or Lua/XML script loading) to hit it. Added
    `-sSTACK_SIZE=4MB` to `web/build-engine.sh` (link-time only, no
    recompile needed) — a generous, standard-order-of-magnitude bump
    (roughly matching typical native OS thread stack defaults), not a
    tuned minimum. Verified against a deliberately heavier synthetic
    repro this time (5 plugin manifests × 20 nested `map_patch`/
    `resource`/`mml` entries each, still Aleph One's own public schema,
    still no real Marathon 2 content) — no crash, no abort, tab stayed
    responsive, and the engine correctly reached its own "please install
    the files" check (since the dummy content still isn't a real WAD).
  - **A third real bug: a genuinely correctly-mounted `/data` (verified —
    `Map`/`Shapes`/`Images`/`Sounds` all present under exactly the right
    names) still failed the "please install the files" check** — the same
    message as if zero files were mounted, right after the two fixes
    above. Root-caused by adding temporary diagnostic `fprintf`s to
    `shell_options.cpp`/`shell.cpp`/`preprocess_map_sdl.cpp` (reverted once
    the real cause was found and fixed — see below) rather than continuing
    to guess from outside: `have_default_files()` genuinely returned
    `false` for a synthetic repro shaped exactly like the real Marathon 2
    top level (`Map`/`Shapes`/`Images`/`Sounds` plus `Demos/`, `"Physics
    Models/"`, `Plugins/`, `Scripts/`), even though the identical
    canonical-names-only repro (no `Demos`/`Physics Models`/`Scripts`)
    passed cleanly. The actual bug: `ScenarioChooserScenario::load()`
    (`Misc/ScenarioChooser.cpp:91`, called unconditionally from
    `initialize_application()` before the `have_default_files()` check
    even runs) calls `boost::property_tree::read_xml()` directly on every
    non-directory, non-`.lua`, non-`~`-suffixed file inside a scenario's
    `Scripts/` folder — but that call sat *outside* the `try`/`catch` that
    only wrapped the next line (`tree.get<...>()`). Any file in `Scripts/`
    that isn't well-formed XML throws uncaught; before M4d that was a
    generic hard abort, and after M4d (real exception catching) it's
    instead correctly caught by `main()`'s own top-level handler, logged
    as `"Unhandled exception"`, and exits with status 1 — silently, from
    the browser's perspective, well before `have_default_files()` is ever
    reached. **This is a genuine, pre-existing robustness gap in native
    code** (not something the web port introduced) that real-world testing
    happened to surface — matches the "program exited (with status: 1)"
    message the user saw. Fixed by moving `read_xml()` inside the existing
    `try`/`catch`, broadened to `catch (const std::exception&)` rather
    than just `ptree_error` (an XML parse failure isn't guaranteed to
    share that exact hierarchy across boost versions) — the existing code
    already treats "couldn't extract a name from this script" as a
    skippable, non-fatal case; a parse failure is just another instance of
    that, not a reason to crash the whole program. Verified against the
    same "shaped like real Marathon 2" synthetic repro that reproduced it
    (still no real Marathon 2 content — a deliberately-malformed synthetic
    `Scripts/script1.mml`, not a valid one): `have_default_files()` now
    correctly returns `map=1 images=1 shapes=1`, and the engine proceeds
    past it cleanly. **The diagnostic `fprintf`s themselves were removed
    again** once the real fix landed — only the `ScenarioChooser.cpp` fix
    remains.
  - **A methodology note worth remembering**: mid-investigation, a
    seemingly-clean revert-and-retest (via `git stash`) appeared to show
    even the *diagnostic-free* baseline hanging on a previously-solid
    repro — a real moment of doubt about whether something environmental
    had broken. It hadn't: the Browser pane's "first `javascript_tool`
    call after `callMain` times out" is a known, benign, recurring pattern
    in this session (seen even in verified-working runs) — a follow-up
    trivial eval (`1+1`) always confirmed the tab was actually fine. Two
    consecutive timeouts without that follow-up check briefly looked like
    a real regression and cost a redundant revert/rebuild cycle. Always
    check responsiveness directly before concluding a hang, especially
    right after a `callMain` call.

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
      renderer (see Findings). **Still not the actual next blocker** — see
      the main-loop item below M4a for what is.
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
- [x] **M4a — Feed files collected by the M1 widget into MEMFS at the paths
      `find_files_sdl.cpp`/`FileHandler` expect.** See Findings:
      `mountUploadedFiles.ts`, the `emar`/`emranlib` build fix,
      `build-engine.sh` (a real, browser-loadable `alephone.js`/`.wasm`),
      and `game.html` (a manual harness). Files keep their real names and
      structure (only a shared leading wrapper folder is stripped) — see
      M4g: an earlier version of this milestone also renamed the four
      canonical scenario files, which turned out to be wrong and was
      reverted after testing against real Marathon 2 data.
  - [ ] **M4b — IDBFS persistence** so re-upload isn't required every
        session. Not started.
- [x] **M4c-i — `main_event_loop()` converted to `emscripten_set_main_loop`,
      verified end-to-end (no more hang).** Decision made after measuring
      Asyncify's real cost on this build rather than guessing — see
      Findings for the numbers and the call-site-count reasoning. Verified
      via the browser harness: module load, file mount, `callMain`, a real
      in-canvas dialog rendering, a live click, and a clean shutdown, all
      with the tab staying responsive throughout.
  - [ ] **M4c-ii — every `dialog::run()` call site** (Preferences,
        Quit-confirm, alerts, Load/Save, ~28 live call sites) still needs
        the same treatment. One happened to work in manual testing, but
        `dialog::run()`'s own `yield()` is a genuine no-op on this
        single-threaded build — see Findings for why that one test isn't
        evidence this is generally safe. Not started.
- [x] **M4d — Enable real C++ exception catching.** Real scenario data
      (confirmed via a synthetic repro of a plugin manifest, not real
      Marathon 2 content) throws during `Plugins.cpp`'s XML/property_tree
      parsing, and Emscripten's default build has no exception-catching
      support linked in at all, so any throw anywhere is a hard abort —
      not specific to plugins, just the first place real data reaches it.
      Resolved with modern WASM-native exceptions (user's explicit choice
      over the legacy JS-based route — see Findings for the pros/cons and
      why): `configure.ac` now adds `-fwasm-exceptions
      -sSUPPORT_LONGJMP=wasm` automatically for the Emscripten target, and
      a new custom vcpkg triplet + install script rebuild just
      `freetype`/`libpng` (the only dependencies needing it) to match.
      Verified end-to-end, twice, including a from-scratch clean rebuild:
      the exact repro that used to abort now runs cleanly.
  - [x] **STACK_SIZE bump** — Emscripten's 64KB default was too small for
        real scenario data's recursion depth; `-sSTACK_SIZE=4MB` in
        `web/build-engine.sh`.
  - [x] **`ScenarioChooser.cpp` XML-parse robustness fix (M4e)** — a
        genuine, pre-existing bug (uncaught `read_xml()` exception on any
        non-well-formed file in a scenario's `Scripts/` folder) that only
        started surfacing loudly once M4d's real exception catching landed
        (previously a generic hard abort either way). Root-caused via
        temporary diagnostic instrumentation rather than continued
        guessing.
  - [x] **`main.cpp`'s top-level catch now prints the real exception
        message (M4f)** — `logFatal()` alone was silently dropping it
        whenever the log file failed to open, which is likely on this
        target.
  - [x] **M4g — corrected a wrong fix from M4a, verified against real
        Marathon 2 data.** `mountUploadedFiles` no longer renames
        `Map.sceA`/`Shapes.shpA`/etc. to extensionless "canonical" names —
        real scenarios (confirmed with the actual Marathon 2 data) ship
        their own `Scripts/Filenames.mml` telling the engine their real
        on-disk names, and the rename broke exactly that mechanism. Found
        and verified using a new local Node + NODEFS test harness (real
        data read directly from disk into Node's memory, never written
        back, transmitted, or committed) — see Findings for the full
        story and the harness's safety design.
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

That result reprioritized what came next: **M4a (filesystem bridge), not
M3b-iv/OpenGL** — and M4a is now done. The M1 upload widget's files get
written into the real Emscripten `FS` under `/data`, with two real
correctness bugs found and fixed along the way (not just "should work" —
verified against the real `FS`, not only the fake used in unit tests):
core scenario files (`Map`/`Shapes`/`Images`/`Sounds`) are looked up by
exact extensionless name, so `mountUploadedFiles` renames the four
recognized top-level files to match; and a single shared leading folder
(the dropped folder's own name) is stripped so nested `Plugins`/`Scripts`
discovery isn't broken. Also formalized what M3c did as a one-off manual
relink into a real, reusable build step
([web/build-engine.sh](web/build-engine.sh)), fixing a genuine, previously-
unnoticed `ar`/`ranlib` bug along the way (see Findings) — and added a
manual test harness, [web/game.html](web/game.html).

**Driving the real engine this way surfaced the actual next blocker:**
once scenario data is present and `have_default_files()` passes,
`Module.callMain()` reaches `main_event_loop()` (`shell.cpp`) — a classic
blocking `while` loop — which hangs the entire browser tab, since nothing
in that loop ever yields back to the browser's own (single-threaded,
cooperative) event loop.

**That blocker is now resolved for `main_event_loop()` itself (M4c-i),
decided in favor of `emscripten_set_main_loop` over Asyncify** after
measuring Asyncify's real cost on this build (+9% wasm size on a debug
build — see Findings) and investigating scope (`dialog::run()` is a second
blocking loop, called from *inside* `main_event_loop()`'s call tree at ~28
live sites, not a separate independent one) rather than guessing. Verified
end-to-end: mounted data, called `callMain`, and the tab stayed fully
responsive — a real in-canvas dialog rendered, a live click was processed,
and the engine shut down cleanly. **Not yet done: `dialog::run()` itself
(M4c-ii)** — needs the same treatment at each of its ~28 call sites
(Preferences, Quit-confirm, alerts, Load/Save); one alert dialog happened
to work in manual testing, but its `yield()` is a genuine no-op on this
build, so that isn't evidence the general case is safe (see Findings).

**A user-run test with real Marathon 2 data (M4a/M4c-i's fixes applied)
found the next real blocker: real scenario data throws a C++ exception
during plugin manifest parsing, and Emscripten's default build has no
exception-catching support linked in at all — any throw anywhere is a
hard abort.** Root-caused without touching the real data (a synthetic
plugin manifest using Aleph One's own public schema reproduced it
exactly). **Now resolved (M4d)**: `configure.ac` adds
`-fwasm-exceptions -sSUPPORT_LONGJMP=wasm` automatically for the
Emscripten target (the user's explicit choice over the legacy JS-based
route, after weighing real tradeoffs — see Findings), and a new custom
vcpkg triplet rebuilds just `freetype`/`libpng` (the only dependencies
that needed it) to match. Verified end-to-end, twice, including a
from-scratch clean rebuild: the exact repro that used to abort now runs
`callMain` to completion with no error and a fully responsive tab.

**Real-data testing continued to surface real bugs, one per retry (M4e,
M4f, M4g)** — a genuine, pre-existing `ScenarioChooser.cpp` crash on any
non-well-formed file in `Scripts/` (only surfaced loudly once M4d's real
exception catching landed), a logging gap that was silently swallowing the
actual exception message on every crash, and — the big one — **M4a's
extensionless-name rename was itself wrong**, discovered only by finally
testing against the real Marathon 2 data directly rather than synthetic
approximations of it. Built a local **Node + NODEFS test harness**
specifically to make that possible (the browser can't drive a native file
picker via automation, and serving the real data over even a local HTTP
port was correctly refused) — real data read directly from disk into a
Node process's own memory, never written back, transmitted, or committed,
matching the constraints the existing `realMarathon2Data.test.ts` already
operates under. That harness showed real Marathon 2's own
`Scripts/Filenames.mml` overriding the engine's default filenames to its
actual on-disk names (`Map.sceA`, etc.) — exactly what M4a's rename
broke. Fixed by removing the rename entirely; `mountUploadedFiles` now
preserves real filenames throughout. **Verified end-to-end against the
real data**: every `have_default_files()` lookup now succeeds, and
execution proceeds past all data-loading and preferences logic into
actual window/screen creation, where it hits Node's expected "no DOM"
limitation (`document is not defined`) rather than any remaining bug in
this code.

M4b (IDBFS persistence), M4c-ii (`dialog::run()`'s own blocking loop —
see above), and M3b-iv (OpenGL) all remain not-yet-started. The next
real-browser retry (not Node) is the natural next step — nothing in this
session's testing has gotten further than the point Node's own DOM
limitation now marks.
