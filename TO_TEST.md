# Web port: manual test checklist

Nothing here can be covered automatically — it all needs a real browser, a GPU,
real game data, and someone clicking. Work through it and tick as you go.

Each dialog below was converted from a blocking `dialog::run()` to a
cooperative one. That means the code which used to follow `run()` now runs in a
completion callback, and anything it touched had to stop being a stack local —
so a converted dialog can look fine and still be reading freed memory. For each
one: **open it, accept it, and cancel it**, and confirm the setting actually
took effect.

If `[heartbeat N]` stops appearing in the page log, the tab is genuinely frozen
and that dialog is still blocking. If it keeps ticking, the page is alive
whatever else is wrong.

## Preferences → Graphics

- [ ] `Screen Size` popup
- [ ] `Brightness` popup
- [ ] `HUD Plugin` popup
- [ ] `HUD Size` popup
- [ ] `Terminal Size` popup

## Preferences → Graphics → Rendering Options

Two tabs. Open the dialog, exercise both tabs, then confirm OK persists across a
restart and CANCEL discards.

Basic tab:

- [ ] `Scripted Effects Quality` popup
- [ ] `Walls` texture quality popup
- [ ] `Landscapes` texture quality popup
- [ ] `Sprites` texture quality popup
- [ ] `Weapons in Hand` texture quality popup
- [ ] `HUD / Terminals` texture quality popup
- [ ] `3D Model Skins` quality popup

Advanced tab — one resolution and one colour-depth popup per texture type:

- [ ] `Walls` resolution + depth popups
- [ ] `Landscapes` resolution + depth popups
- [ ] `Sprites` resolution + depth popups
- [ ] `Weapons in Hand` resolution + depth popups
- [ ] `HUD / Terminals` resolution + depth popups

Whole dialog:

- [ ] OK applies every changed setting, and they survive a page reload
- [ ] CANCEL discards every changed setting
- [ ] Reopening after OK shows the saved values

## Preferences → Sound

- [ ] `Channels` popup

## Preferences → Controls

- [ ] `Mouse Feel` popup
- [ ] `Controller Feel` popup

## Preferences → Controls → Mouse (custom/advanced)

- [ ] `Mouse Feel` popup

## Preferences → Online

- [ ] First colour swatch: sliders live-update the swatch, OK keeps the colour
- [ ] First colour swatch: CANCEL restores the original colour
- [ ] Second colour swatch: OK keeps the colour
- [ ] Second colour swatch: CANCEL restores the original colour

## Preferences → Environment

Each row opens a file list. Test selecting an entry and cancelling.

- [ ] `Map`
- [ ] `Physics`
- [ ] `Shapes`
- [ ] `Sounds`
- [ ] `External Resources`
- [ ] `Script File` (enable `Use Solo Script` first)
- [ ] `Netscript File` (enable `Use Netscript in Films` first)

And the LOAD OTHER button inside those lists, which opens a file browser:

- [ ] LOAD OTHER → choose a file → path updates
- [ ] LOAD OTHER → cancel → path unchanged

## Main menu

- [ ] `About` — opens, both tabs render, OK returns to the menu
- [ ] `Save Last Film` — name and save writes the film
- [ ] `Save Last Film` — cancel returns to the menu cleanly
- [ ] `Save Last Film` — reusing an existing name prompts to overwrite; declining returns to the save dialog rather than giving up
- [ ] `Replay Saved Film` — picker opens, choosing one starts playback
- [ ] `Replay Saved Film` — cancel returns to the main menu
- [ ] `Begin New Game` while holding the cheat modifier — vidmaster level chooser appears, picking a level starts there, cancel returns to the menu, and the game does not start twice

## In game

- [ ] `Escape` — "cancel the game in progress?" appears; YES returns to the main menu, NO resumes
- [ ] Any error (e.g. a corrupt save) — the alert appears and dismisses without freezing

## Renderer

- [ ] Walls, floors and ceilings are textured, right way up, no smearing or tiling
- [ ] Landscape/sky renders
- [ ] Sprites (items, monsters, corpses) are cut out cleanly — no opaque rectangle around them
- [ ] Sprites are not repeated at the screen edges
- [ ] Looking through a doorway: sprites beyond it are clipped by the doorway, not painted over the wall in front
- [ ] An object underwater is drawn under the water surface, not over it
- [ ] Picking up an item gives a brief coloured tint, not a black screen
- [ ] Being hit gives a brief tint/static, not a black screen
- [ ] Fog thickens with distance rather than being flat or absent
- [ ] HUD and text are readable and correctly positioned
- [ ] Switching renderer between software and OpenGL in Preferences works, and the choice survives a restart

## Pointer lock

Three separate entry paths; they have behaved differently from each other
before, so test each. The log reports `[pointerlock] acquired` / `released` /
`request refused by the browser`, which distinguishes "never asked" from
"browser said no".

- [ ] After `Begin New Game` — mouse look works and does not stop at the canvas edge
- [ ] After `Continue Saved Game` from the in-page list
- [ ] After loading a save from disk
- [ ] All three in Safari (stricter than Chrome about what counts as a user gesture)
- [ ] All three in Chrome

## Persistence and recovery

- [ ] Save a game, reload the page, confirm the save is still listed
- [ ] Change a preference, reload the page, confirm it was retained
- [ ] Loading a saved game from the in-page list
- [ ] Loading a saved game from disk (the "click anywhere" overlay appears and is hard to miss)
- [ ] `?resetprefs` in the URL discards preferences and saved games, in Safari
- [ ] `?resetprefs` in the URL discards preferences and saved games, in Chrome
- [ ] `?nogl` in the URL starts the game with the software renderer, in Safari
- [ ] `?nogl` in the URL starts the game with the software renderer, in Chrome
- [ ] With many WebGL-using tabs open (or after exhausting contexts), the page logs "WebGL is not usable right now" and starts in software rather than aborting
- [ ] If the engine fails to start, both the "Reload with the software renderer" and "Reset preferences and reload" buttons appear and work
- [ ] Open Preferences → Graphics → Rendering Options, close it, and repeat ten times — the game keeps running (each screen-mode change used to leak a WebGL context)
- [ ] Change screen size or renderer several times in a row — same check

## Sound

- [ ] Sound effects play
- [ ] Music plays
- [ ] Music keeps playing while a dialog is open
- [ ] Audio resumes after the first click if the browser suspended it
