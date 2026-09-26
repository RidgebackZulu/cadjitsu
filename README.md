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

## Self tests

`Cadly --selftest=<name> --out <dir>` runs a scripted scenario through the real UI and
command layer. It saves screenshots and exits with status 0 on success.
`Cadly --list-selftests` lists the scenarios.

## Roadmap

- [x] **M0** Build infrastructure, vendored solver, app skeleton, CI (Linux + macOS arm64)
- [ ] **M1** Headless kernel:
  - document, timeline, result cache and parameters
  - topological naming
  - sketch profiles
  - extrude and fillet
  - STL and STEP export
- [ ] **M2** Viewport:
  - Fusion-style navigation and ViewCube
  - shaded, wireframe and edge display
  - face and edge picking
- [ ] **M3** Sketch mode:
  - line, rectangle, circle and arc tools
  - constraints and direct dimension editing
  - selection stats
- [ ] **M4** Extrude command, timeline (scrub, suppress, edit feature), browser, undo, files
- [ ] **M5** Fillet, chamfer, hole, combine (keep tools), offset and rotated construction planes
- [ ] **M6** Section analysis
- [ ] **M7** Export dialogs, rendered view, packaging
