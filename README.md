# Cadly

Parametric CAD for designing 3D-printable parts. Cadly is a native app compiled for
Apple Silicon. It copies the parts of Autodesk Fusion 360 that matter for printed parts:
- sketch mode
- solid modeling
- mouse and keyboard handling
- the history timeline
- section analysis

It leaves out mesh, sheet metal, plastics and rendering workspaces.

> Status: every planned milestone (M0 to M7) is done; see *Roadmap*.

## Stack

| Piece | What |
|---|---|
| Language | C++20 |
| UI | Qt 6 Widgets; 3D viewport on `QRhiWidget` (Metal on macOS, OpenGL on Linux) |
| Geometry kernel | [OpenCASCADE](https://dev.opencascade.org) (OCCT 7.9+) |
| Sketch solver | SolveSpace `libslvs` (vendored, GPLv3) |
| Export | STEP (AP242) and watertight, validated STL |

Cadly is licensed under the GPLv3 (see `LICENSE`) because it links SolveSpace's solver.

## Building on macOS (Apple Silicon)

```sh
brew install qtbase qtshadertools opencascade eigen ninja cmake
cmake --preset macos-brew
cmake --build --preset macos-brew
open build/macos/src/app/Cadly.app
```

Tests: `ctest --preset macos-brew`.

## Downloading a CI build

Each CI run uploads an ad-hoc-signed disk image as the `Cadly-macOS-dmg` artifact. GitHub always delivers artifacts zipped.

1. Download `Cadly-macOS-dmg` from the run's **Artifacts** section and unzip it: you get `Cadly.dmg`.
2. Open `Cadly.dmg` and drag Cadly onto Applications.
3. Clear the quarantine flag once: `xattr -dr com.apple.quarantine /Applications/Cadly.app`.
4. Open Cadly.

The bundle is built for Apple Silicon, draws with Metal and needs macOS 15 (Sequoia) or later. It is self-contained: after `macdeployqt`, `scripts/macos_bundle_fix.py` points every library reference into the bundle and fails the build if anything still leads outside it. Before the disk image is uploaded, CI hides Homebrew, runs the `acceptance` self test on the packaged app, and runs the app copied out of the finished disk image. On an older macOS, build locally (see above).

## Building on Linux

The Linux build uses a conda-forge environment: Qt 6.9, OCCT 7.9, Eigen, GCC, CMake and Ninja. Headless runs use Xvfb and Mesa.

```sh
scripts/bootstrap_env.sh          # one-time: micromamba env in /opt/cadly-tools/env
source scripts/env.sh
cmake --preset linux-conda
cmake --build --preset linux-conda
ctest --preset linux-conda        # core tests + app tests under Xvfb
scripts/run_xvfb.sh build/linux/src/app/Cadly --selftest=smoke --out out/
```

## Layout

```
src/core/     cadcore: UI-free kernel (document, timeline, features, sketch, naming, export)
src/app/      Cadly Qt application (viewport, sketch UI, panels, timeline, browser)
tests/core/   doctest unit tests for cadcore
tests/app/    QtTest tests driving the real UI
third_party/  vendored libslvs, doctest, nlohmann/json
```

## Mouse and keyboard (Fusion 360 bindings)

| Action | Input |
|---|---|
| Pan | Middle drag; trackpad two-finger drag |
| Orbit | Shift + middle drag; Shift + two-finger drag |
| Zoom | Scroll wheel (towards the cursor); pinch |
| Fit | Double-click middle button; F6 |
| Select | Click a face, edge or vertex; Shift / Cmd-click adds |
| Select bodies | Drag left-to-right (window) or right-to-left (crossing); double-click a face |
| Standard views | Click a ViewCube face, edge or corner; Home button |
| Marking menu | Right-click the canvas: a ring of commands (Repeat, Create Sketch, Extrude, undo / redo...) plus a short list; click one or press Esc |
| Extrude | E (with a profile or planar face selected, or inside a sketch to finish it and extrude its profile) |
| Fillet / Hole | F / H |
| 3D Print (export an STL) | Cmd+P |
| Undo / redo | Cmd+Z / Shift+Cmd+Z (inside a sketch: the sketch's own steps; while a command is open: cancel it) |

## Sketching

**Starting a sketch**
- Click **Create Sketch**, then click one of the orange origin planes or any planar face.
- The view turns to look straight at the sketch plane.
- **Finish Sketch** (ribbon or palette) puts the whole sketch in the timeline as one undo step.

**Drawing**

| Tool | Key | Heads-up input |
|---|---|---|
| Line (click-click chains; Esc or double-click ends) | L | length, angle |
| 2-point rectangle / center rectangle | R | width, height |
| Center diameter circle | C | diameter |
| 3-point arc | | |
| Point | | |
| Sketch dimension | D | the value box opens when the dimension is placed |

- Points snap to existing points, the origin, midpoints, quadrants, curves and the sketch axes.
- Lines within 3° of level or plumb become horizontal or vertical.
- While drawing, type a number to fill the active value box. Tab moves to the next box. Enter commits the shape, and each typed value becomes a dimension.

**Editing**
- **Dimensions:** double-click a dimension to change it. The value box accepts expressions such as `2*d1` or `1 in`. A dimension that would over-constrain the sketch becomes a driven (reference) dimension.
- **Constraints:** coincident, horizontal/vertical, parallel, perpendicular, tangent, equal, midpoint, concentric, fix and symmetric. Apply them to the current selection, or pick the entities after choosing the tool. A constraint that repeats or contradicts others is refused.
- **Selection:** drag geometry to move it, drag in empty space to box-select, and double-click a curve to select its chain.
- **Other keys:** Delete removes the selection, X toggles construction geometry, and Esc steps back to Select.

**Display**
- Fully constrained geometry is black, under-constrained geometry blue, and construction geometry orange dashed.
- Closed regions (profiles) are shaded.
- Selecting anything shows its length, radius, diameter, area and other stats at the bottom right.

## Modeling

**Extrude (E)**
- Select one or more profiles (shaded sketch regions) or planar faces, then press **E**. Inside a sketch, **E** finishes the sketch and extrudes its profile.
- The dialog opens at the right of the canvas with the distance box ready for typing. An arrow on the canvas can be dragged instead.
- **Direction:** One Side, Two Sides (a distance and taper for each) or Symmetric.
- **Extent:** Distance, To Object (click the face to stop at; faces at an angle work too), or All (through everything; Flip picks the side).
- **Taper Angle:** positive angles flare the sides outwards, negative angles draw them in.
- **Operation:** Join, Cut, Intersect or New Body. Until you choose one, it follows what you are doing: out of a body's face joins, into a body cuts, elsewhere makes a new body.
- Values take expressions and units (`20`, `d1 / 2`, `0.5 in`). The model previews live; **OK** or Enter adds one timeline step, Esc or **Cancel** leaves the design untouched.

**Command inputs**
- While a command's dialog is open, clicks in the canvas add and remove inputs (no modifier needed); what is picked is highlighted, and the × in an input clears it.
- Inputs are drawn on the model before the feature, so an edge a previewed fillet has rounded away stays visible where it was and can still be clicked to drop it.
- Cuts, holes and the tools a Combine cut removes are previewed in translucent red.
- A feature that fails to build (a fillet radius too large for its edges...) says why in the dialog, and OK stays disabled until it builds.

**Fillet (F) and Chamfer**
- Select edges, or faces to take all their edges; tangent edges are followed.
- Fillet takes a radius. Chamfer is Equal Distance, Two Distances, or Distance and Angle, with Flip for which side the first distance is on.

**Hole (H)**
- **On Face:** click a planar face where a hole goes; each further click on it adds a hole, and clicking a hole's centre removes it. X and Y place the last hole exactly (in the face's own coordinates, which follow the model axes where they can).
- **At Sketch Points:** pick points of a visible sketch (for example a sketch of dimensioned points on a face).
- Simple, Counterbore or Countersink holes; to a depth or through All; with a 118° drill point (or any angle) or a flat bottom.

**Combine**
- Click the target body, then the tool bodies; Join, Cut or Intersect; **Keep Tools** leaves the tools in the model.

**Offset Plane**
- Pick an origin plane (they are shown while the command runs), a construction plane or a planar face; type the distance or drag the arrow.
- **Angle** turns the plane by any angle about its own X or Y axis, or about a straight edge.
- Sketches can be created on construction planes, and planes follow the faces they are built on.

**Section Analysis** (Inspect)
- Pick an origin plane, a construction plane or a planar face, then type the depth or drag the arrow; **Flip** keeps the other side. Cut faces are closed with hatched caps, as in Fusion.
- The section is kept in the browser's **Analysis** folder, not the timeline. Its eye turns it off (back to the normal view) and on again; double-click it (or right-click > Edit Section Analysis) to change it; right-click > Delete removes it. One section shows at a time.
- While a section is shown its depth arrow stays on the canvas: drag it at any time, even in the middle of another command. Each drag is one undo step.
- Modelling carries on while sectioned. What is cut away cannot be picked (the caps block picks too), and a section built on a face follows that face when the model changes.

**Timeline** (bottom of the window)
- One icon per feature, in order, and the history marker after the last active one. Drag the marker to scrub through the history; the model follows immediately. The buttons at the left step to the start, back, forward and to the end.
- Right-click a feature: **Edit Feature** (or **Edit Sketch**), **Suppress / Unsuppress**, **Roll History Marker Here**, **Rename** and **Delete**. Double-click also edits.
- **Edit Feature** reopens the dialog the feature was made with, filled in with its values. The model is shown rolled back to that feature while you edit, and changes preview live. Everything after it recomputes when you click OK.
- Suppressed features are drawn struck through and skipped; failing features are drawn red, and features with warnings yellow.

**Browser** (left)
- Origin, Analysis, Bodies, Sketches and Construction folders. The eye next to each item shows or hides it; a sketch hides itself once a feature uses it, and its eye brings it back.
- Double-click a body's name (or press F2) to rename it; double-click a sketch to edit it.

**Recomputing**
- The model is recomputed on a background thread, so the window never waits for it: a *Computing…* note appears at the bottom right while it works.
- Every step's result is cached, so scrubbing, suppressing and unsuppressing, undo and Cancel are usually instant.

**Files:** New, Open, Save and Save As (`.cadly`, a JSON document with the whole history). On macOS, double-clicking a `.cadly` file in Finder (or dropping it on the Dock icon) opens it; `Cadly design.cadly` does the same from a shell.

## Display

**View > visual style** (also the display button in the navigation bar under the canvas):

| Style | What it shows |
|---|---|
| Shaded with Visible Edges | The default: shaded bodies with their edges drawn. |
| Shaded | Shaded bodies, no edges. |
| Wireframe | Edges only, including hidden ones. |
| Rendered | A studio look for checking a part: a softer material with rim light, a contact shadow on the ground, no edges or grid. |

The canvas can be used the same way in every style: picking, commands and section analysis all work.

## Export and 3D printing

**MAKE > 3D Print** (Cmd+P) and **File > Export…** open the same dialog: 3D Print starts on STL, Export on STEP.
- **Bodies:** every visible body, or only the selected ones (bodies, or the bodies of selected faces and edges). If something is selected when the dialog opens, it starts on the selection.
- **STL (3D printing):** Refinement is Coarse, Medium, Fine or Custom (chord and angle tolerances; each preset shows its own). **Merge bodies into one solid** unites bodies that touch or overlap, so the slicer gets one watertight solid. Binary or ASCII. **Open in my slicer afterwards** hands the file to the app your computer opens `.stl` files with (PrusaSlicer, Bambu Studio, Cura…); the choice and the last export folder are remembered.
- **Printability check:** the mesh is welded along the model's edges and checked before it is written:
  - every edge is shared by exactly two triangles, facing opposite ways (watertight, manifold);
  - no degenerate triangles;
  - outward-facing (positive volume);
  - the mesh volume matches the exact model volume.

  The dialog reports the triangle and shell counts and both volumes. A mesh that fails is not written unless you say so.
- **STEP:** AP242 (or AP214), in millimetres, one named solid per body. After writing, the file is read back and its volume shown next to the model's.
- Exports run in the background; the window stays usable. An open command is ended first, so what you export is the design, never a preview.

## Self tests

`Cadly --selftest=<name> --out <dir>` runs a scripted scenario through the real UI and
command layer. It saves screenshots and exits with status 0 on success.
`Cadly --list-selftests` lists the scenarios.

| Scenario | What it does |
|---|---|
| `smoke` | The window opens and the canvas renders. |
| `acceptance` | The M7 acceptance run, all through the UI: an L bracket (a 60 x 40 plate extruded 8 mm, an upright sketched on its top face and extruded 40 mm, two counterbored screw holes, a hole through the upright, an R4 fillet in the inside corner, chamfers), then Extrude1 edited from 8 to 10 mm with every later feature following. Screenshots in all four visual styles; 3D Print to an STL that passes the printability check and reads back as one manifold shell; File > Export to a STEP that reads back with the same volume; save, New, reopen. The macOS CI job also runs it on the packaged app. |
| `views` | A demo part in every display style; hover, selection statistics and the ViewCube. |
| `sketch` | Sketch mode end to end: typed dimensions, a hole, a slot, constraints, statistics. |
| `features` | Fillet, Chamfer, Hole, Offset Plane and Combine through their dialogs, with a screenshot of each live preview; Edit Feature; the design checked against a from-scratch evaluation. |
| `section` | The M6 acceptance run: a section through both holes of the demo bracket from the dialog, hatched caps, the depth arrow dragged afterwards, a fillet on an edge picked in the section view (the cut-away edge cannot be picked), the browser eye off and on, the cap seen head-on. |
| `plate` | The M4 acceptance run, all through the UI: a 60 x 40 rectangle extruded 20 mm; a circle sketched on its top face and cut through all by dragging the arrow; the volume at every history-marker position; suppress / unsuppress from the cache; Edit Feature reopening both extrudes with their values and previewing live; 16 more holes and a fillet so recomputing takes a while; the first dimension edited to 80 (checked against a from-scratch evaluation); undo back to an empty design. Throughout, the UI thread must never be kept from running for 50 ms while the model recomputes (canvas repaints excluded: they are timed separately). |

## Roadmap

- [x] **M0** Build infrastructure, vendored solver, app skeleton, CI (Linux + macOS arm64)
- [x] **M1** Headless kernel:
  - document, timeline, result cache and parameters
  - topological naming
  - sketch profiles
  - extrude, fillet, chamfer, hole, combine and construction planes (geometry)
  - STL and STEP export
- [x] **M2** Viewport:
  - Fusion-style navigation and ViewCube
  - shaded, wireframe and edge display
  - face, edge and vertex picking
  - window and crossing selection
  - selection statistics
- [x] **M3** Sketch mode:
  - SolveSpace constraint solving with black/blue constrained colouring
  - sketches on origin planes and planar faces
  - line, rectangle, circle, arc and point tools with snapping and heads-up input
  - constraints and direct dimension editing
  - profiles, selection stats, drag, local undo
- [x] **M4** Modeling slice:
  - command dialogs with live, background-computed previews
  - Extrude with its arrow, all extents, tapers and the automatic operation
  - timeline: scrub, suppress, edit feature, rename, delete
  - browser, marking menu, undo / redo, New / Open / Save
- [x] **M5** Fillet, chamfer, hole (on faces or at sketch points), combine with keep tools, offset and rotated construction planes, all editable from the timeline
- [x] **M6** Section analysis on any plane or face: hatched caps, a depth arrow to drag at any time, browser eye, modelling while sectioned
- [x] **M7** Export and packaging:
  - 3D Print / Export dialog: STL with the printability check, STEP read back
  - Rendered visual style; styles in the View menu and the navigation bar
  - app icon, macOS bundle that opens `.cadly` documents, acceptance run on the packaged app
