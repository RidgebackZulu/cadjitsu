# Cadly

Parametric CAD for designing 3D-printable parts. Cadly is a native app compiled for
Apple Silicon. It copies the parts of Autodesk Fusion 360 that matter for printed parts:
- sketch mode
- solid modeling
- mouse and keyboard handling
- the history timeline
- section analysis

It leaves out mesh, sheet metal, plastics and rendering workspaces.

> Status: under active development, built in milestones (see *Roadmap*).

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

Each CI run uploads an ad-hoc-signed `Cadly-macos-arm64` artifact.

1. Download the zip from the run's **Artifacts** section and unzip it.
2. Clear the quarantine flag once: `xattr -dr com.apple.quarantine Cadly.app`.
3. Open `Cadly.app`.

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
| Canvas menu | Right-click (Create Sketch, Edit Sketch, undo / redo, fit) |
| Undo / redo | Cmd+Z / Shift+Cmd+Z (inside a sketch: the sketch's own steps) |

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

## Self tests

`Cadly --selftest=<name> --out <dir>` runs a scripted scenario through the real UI and
command layer. It saves screenshots and exits with status 0 on success.
`Cadly --list-selftests` lists the scenarios.

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
- [ ] **M4** Extrude command, timeline (scrub, suppress, edit feature), browser, undo, files
- [ ] **M5** Fillet, chamfer, hole, combine (keep tools), offset and rotated construction planes
- [ ] **M6** Section analysis
- [ ] **M7** Export dialogs, rendered view, packaging
