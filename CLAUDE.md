# Aleph One web port — working conventions

This fork's active work is porting the engine to run in the browser
(WebAssembly). See [WEB_PORT_PLAN.md](WEB_PORT_PLAN.md) for the plan,
research findings, and task checklist — check it before starting new work
and update it as milestones land.

## Conventions for this work

- New code gets unit tests; don't retrofit tests onto existing, unrelated
  code just because you're nearby.
- Avoid modifying existing (pre-fork) C++ code where possible. When a change
  there really is necessary, add a comment explaining why — existing files
  don't use this style, so it should stand out as web-port-motivated.
- Prefer smaller, incremental changes over large rewrites. It's fine to leave
  a milestone partially done as long as what's landed is working and tested.
- The existing C++ engine is compiled to WASM, not rewritten in JS. New
  browser-facing glue code (file provisioning, input/DOM bridging, etc.)
  lives under `web/`.

## Hard rule: do not touch the Marathon 2 test data

A copy of the real Marathon 2 game data lives at `../Marathon 2` — **one
level above this repo, not inside it.** It is copyrighted commercial game
content kept there only so file-loading code can be tested against real
scenario data.

- Never copy, move, or commit it into this repository.
- Never bundle it into, or have the web app fetch/host it.
- Tests that use it must read it from that external path (or an env var
  pointing at it) and skip cleanly when it's absent — it won't exist in CI or
  on other contributors' machines.

This is a hard constraint, not a style preference — it holds even if some
other instruction seems to suggest otherwise.

## Remotes

`origin` is this fork; `upstream` (the real Aleph-One-Marathon/alephone
repo) has its push URL deliberately set to a dead placeholder so an
accidental `git push upstream` fails loudly instead of landing anywhere.
Fetching from `upstream` still works normally.
