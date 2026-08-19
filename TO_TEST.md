# Web port: manual test checklist

The web port has no automated coverage for anything that needs a real browser,
a GPU, or a person clicking things. This file lists what to exercise by hand,
and why each item is here.

Everything below is grouped by the change that motivated it. Items are worth
re-running after any change to the dialog machinery, since they share it.

## Dialogs converted from blocking to cooperative

`dialog::run()` spins until the dialog is dismissed. In a browser that never
happens, because dismissing it needs input and input only arrives once control
returns to the browser's event loop — so a blocking dialog freezes the tab
outright. Each of these was converted to `run_dialog_cooperatively()`, which
means the code that used to follow `run()` now runs in a completion callback,
and anything it touched had to stop being a stack local.

That second part is where the risk is: a converted dialog can look fine and
still be reading freed memory. Test each by **opening it, accepting it, and
cancelling it**, and check the setting actually took effect.

| # | Where | What to check |
| --- | --- | --- |
| 1 | Preferences → any dropdown (e.g. Graphics → Renderer, Screen Size) | Opens, both accept and cancel work, chosen value sticks. This one path covers every popup menu in the game. |
| 2 | Preferences → Graphics → Rendering Options (with the OpenGL renderer selected) | Opens; toggles and sliders across **both** tabs; OK applies and persists across a restart; CANCEL discards. This dialog binds ~25 preferences at once and was the largest conversion. |
| 3 | Preferences → Online → the two colour swatches | Opens; dragging the RGB sliders live-updates the swatch; OK keeps the colour; CANCEL restores the original. |
| 4 | Preferences → Environment → any file row (Solo Script, Physics, Shapes, …) | The list appears; picking an entry sets it; CANCEL leaves it unchanged. |
| 5 | Preferences → Environment → a file row → LOAD OTHER | The file browser opens; choosing a file sets the path; cancelling leaves it unchanged. |
| 6 | Main menu → Save Last Film | The save dialog opens; naming and saving writes the film; cancelling returns to the menu cleanly. Overwriting an existing name should prompt, and declining that prompt should return to the save dialog rather than giving up. |
| 7 | Main menu → Replay Saved Film | The film picker opens; choosing one starts playback; cancelling returns to the main menu. |
| 8 | Main menu → About | Opens, both tabs render, OK returns to the main menu. |
| 9 | In game → Escape | The "cancel the game in progress?" prompt appears; YES returns to the main menu, NO resumes. |
| 10 | Any error condition (e.g. loading a corrupt save) | The alert appears and dismisses without freezing. |
| 11 | Begin New Game **while holding the cheat modifier** | The vidmaster level chooser appears; picking a level starts there; cancelling returns to the main menu. Re-entrant path — worth confirming it does not start twice. |

**Heartbeat is the tell.** The page logs `[heartbeat N]` every 3 seconds. If it
stops, the tab is genuinely frozen and the dialog in question is still
blocking. If it keeps going, the page is alive whatever else is wrong.

## Not converted, and why

| Where | Status |
| --- | --- |
| `ScenarioChooser::run()` (`shell.cpp`) | Not converted. It is not a `dialog` — it creates its own SDL window and runs its own event loop during `initialize_application()`, before the main loop exists, so converting it means restructuring startup rather than reusing the dialog machinery. Only reached when more than one scenario is present, which the current single-folder upload cannot produce. |

## Renderer (OpenGL/WebGL build only)

Built with `./web/build-engine.sh --opengl`, then selected in
Preferences → Graphics. These exercise the parts of the GL emulation this port
had to supply itself, none of which have automated coverage.

| # | What | What to check |
| --- | --- | --- |
| 12 | Walls, floors, ceilings, landscape | Textured, right way up, no smearing or tiling artefacts. |
| 13 | Sprites (items, monsters, corpses) | Cut out cleanly against the background — **no opaque rectangle** around them (alpha test), and not repeated at the screen edges (texture wrap). |
| 14 | Looking through a doorway into another room | Sprites beyond it are clipped by the doorway, not painted over the wall in front (clip planes 0/1). |
| 15 | An object underwater | Drawn *under* the water surface, not over it (clip plane 5). |
| 16 | Picking up an item, and being hit | A brief coloured tint — **not** a black screen (blend state across `glPushAttrib`/`glPopAttrib`). |
| 17 | Fog-heavy levels | Fog thickens with distance rather than being flat or absent (`gl_Fog.start` reconstruction). |
| 18 | HUD and text | Readable, correctly positioned. |
| 19 | Switching renderer software ↔ OpenGL in Preferences | Both work, and the choice survives a restart. |

## Browser and platform behaviour

| # | What | What to check |
| --- | --- | --- |
| 20 | Pointer lock, after **Begin New Game** | Mouse look works and does not stop at the canvas edge. |
| 21 | Pointer lock, after **Continue Saved Game** (from the in-page list) | Same. Entry path differs, and has behaved differently before. |
| 22 | Pointer lock, after **loading a save from disk** | Same. Known to have failed here when the other two worked; the log now reports `[pointerlock] acquired` / `released` / `request refused by the browser`, which distinguishes "never asked" from "browser said no". |
| 23 | Safari specifically | All of 20–22. Safari is stricter than Chrome about what counts as a user gesture, and has been the one to fail. |
| 24 | Saved games and preferences survive a reload | Save, reload the page, and confirm the save is listed and preferences are retained (IndexedDB). |
| 25 | `?resetprefs` | Append it to the URL: preferences and saved games are discarded and the game starts fresh. This is the recovery path if a bad preference ever prevents startup, so it needs to work in **both** Safari and Chrome — deleting the database is refused if anything still holds it open. |
| 26 | Startup failure recovery | If the engine fails to start, a "Reset preferences and reload" button appears and works. |
| 27 | Sound and music | Effects and music both play; music continues while a dialog is open; audio resumes after the first click if the browser suspended it. |
