# Unlike the other install-*.sh scripts here, this doesn't do a full
# manifest install of everything in ../vcpkg.json -- most of those ports
# (steamworks-sdk, nativefiledialog-extended, miniupnpc, ...) don't apply to
# a browser build at all (see ../../WEB_PORT_PLAN.md). Installs only what
# M3's first pass actually needs; add more ports here as later milestones
# require them.
`cat ~/.vcpkg/vcpkg.path.txt`/vcpkg --classic --triplet=wasm32-emscripten --x-install-root=installed-wasm32-emscripten --feature-flags="versions" install sdl2 sdl2-ttf "libsndfile[core,external-libs]" openal-soft
