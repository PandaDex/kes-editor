# KES Editor

Map editor for KES. Brush geometry, entities, materials, and the place you import and open
the rest of the project's assets from. Builds `.fmapc` maps the engine loads.

## Build

Needs Qt 6.4+ (Core, Gui, Widgets, OpenGL, OpenGLWidgets) and OpenGL 3.3.

Windows:

```
cmake -S . -B ../../out/build/forge -G "Visual Studio 18 2026" -A x64 -DCMAKE_PREFIX_PATH=<Qt>/msvc2022_64
cmake --build ../../out/build/forge --config Release
```

Linux:

```
sudo apt install build-essential cmake ninja-build qt6-base-dev libqt6opengl6-dev libgl1-mesa-dev
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

`entities/` and `content/` get copied next to the exe on every build.

Import needs the engine's `model-importer` target built too. The KES Editor finds it in the engine's
build folder.

## Getting started

On first start the KES Editor asks for the game's `.kproject` (here `game/kes.kproject`). It remembers
it; change it with File > Open Project. Everything is relative to the project's `AssetRoot`
(`content/`): maps in `maps/`, materials in `materials/`, textures in `textures/`.

Save the map into `content/maps/` and press F9 to build the `.fmapc` next to it. Without the UI:

```
kes-editor --compile in.fmap out.fmapc
```

This uses the nearest `.kproject` above the map.

## Controls

| | |
|---|---|
| 3D view | hold RMB to look, WASD to fly, E/Q up/down, wheel changes speed |
| 2D views | MMB, RMB or Space+LMB to pan, wheel to zoom |
| Tools | Shift+S select, Shift+E entity, Shift+B block, Shift+X clip, Shift+V vertex, Shift+A material |
| Grid | `[` `]` smaller/bigger, Shift+W snap on/off, Ctrl+B snap selection |
| Edit | Ctrl+D duplicate, Del delete, Ctrl+X/C/V cut/copy/paste (works between maps), arrows and PgUp/PgDn nudge |
| Brushes | Ctrl+Shift+C carve, Ctrl+H hollow, Ctrl+T tie to entity, Ctrl+Shift+W move to world, Ctrl+M transform, Ctrl+L/Ctrl+I flip |
| Visibility | H hide selected, Shift+H hide others, U unhide all |
| Views | Shift+Z maximize view, Ctrl+E center on selection |
| Map | Alt+P check for problems, F9 build |

Clicking a selected object again in a 2D view switches its handles between scale and rotate.
Esc cancels a drag.

- Block tool: pick a shape in Tool Options, drag it in a 2D view or on a surface in 3D.
- Clip tool: drag a line across the selection, Tab picks which side to keep, Enter clips.
- Vertex tool: drag corners. Brushes stay convex.
- Material tool: click a face to apply the current material, Shift+click for the whole object,
  Alt+click to pick, Ctrl+click to select faces. Tool Options edit scale, offset and rotation.

The map autosaves every 3 minutes to `<user data>/KES Editor/autosave/` without touching your file.

## Assets

The Asset Browser shows the content folder.

- Double-click opens an asset in its editor. Maps, materials (`.mat`), shader graphs
  (`.slangnodes`), models (`.kmodel`), animation graphs (`.ag`), input maps (`.imap`) and `.po`
  files open in an editor tab; UI and shader files in VS Code (with the game folder as workspace),
  images and sounds in the system default app.
- Drag a material onto a face, an object, or a material field in Properties. Click one to make it
  the current material, which new blocks use.
- Import (Ctrl+Shift+I, the Import button, or drop files on the window) converts models, textures
  and sounds into `meshes/`, `textures/` and `sounds/`. Animations from glTF/FBX come out as
  `<model>_<clip>.kanim` next to the model. If the browser has a subfolder open, imports go there.
- New creates a material, UI panel, menu, HUD, stylesheet or an empty map.

Extensions an editor module claims (`src/modules/BuiltinModules.cpp`) always open in the editor. To
change which program opens any other type, edit `kAssetEditors` in `src/assets/AssetTypeMap.cpp`:

```cpp
{ "rml", "code {root} {file}|notepad" },
{ "png", "@system" },
```

`@forge` opens in the KES Editor, `@system` uses the OS default, `a|b` takes the first program found.
`{file}`, `{root}`, `{content}` and `{project}` are filled in; the file goes last if `{file}` isn't
used. If a program can't be found the KES Editor asks for it once. Templates for New are in
`src/assets/AssetTemplates.cpp`.

## Entities

Entity classes come from `.dec` files in `entities/` (next to the exe),
`<user data>/KES Editor/entities/` and the project's folders: `EntityDefPaths` in the `.kproject`
(`;`-separated, relative to it), `editor/` next to the `.kproject` by default. Project classes
load last and override the others. They reload when saved. `kes.dec` has the engine's classes;
`base.dec` explains the syntax. Tools > New Entity Class writes a class to `user.dec` from a form.

```
@PointClass base(Targetname, Angles) color(255 0 200) size(-12 -12 -12, 12 12 12)
    = my_turret : "A turret."
[
    damage(integer) : "Damage" : 25
    target(target_destination) : "Target"
    input  Activate(void)
    output OnFire(void)
]
```

Wire entities together on the Outputs tab in Properties. Links point at the target's GUID, so
renaming an entity doesn't break them.

## Files

- `.fmap`: the editor's map, JSON.
- `.fmapc`: the compiled map. Layout is in `docs/file-formats.md` in the engine repo.

## Source

```
src/core/      map data, document + undo, brush geometry, editing operations, project and materials
src/entities/  .dec parser and class registry
src/render/    camera and viewports
src/tools/     the editing tools
src/ui/        main window and panels
src/assets/    import, templates, opening assets in other programs
src/compile/   .fmapc compiler
```

New tools subclass `Tool` and get registered in `EditorContext`'s constructor.

## Not done yet

- Textures are only projected world-aligned. No face-aligned mapping, fit or texture lock.
- No displacements or overlays.
- Viewport models are OBJ only.
- Building a map blocks the editor until it's done.
