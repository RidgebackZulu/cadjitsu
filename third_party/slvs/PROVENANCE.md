# libslvs provenance

- Upstream: SolveSpace 3.2 (https://solvespace.com), GPLv3 — see `COPYING.txt`.
- Obtained from the Debian/Ubuntu source archive:
  `https://archive.ubuntu.com/ubuntu/pool/universe/s/solvespace/solvespace_3.2+dfsg.orig.tar.xz`
- Files copied verbatim (no modifications), except the one patch below:
  - `include/slvs.h`
  - `src/{constrainteq,entity,expr,system,util}.cpp`
  - `src/platform/platformbase.cpp`, `src/slvs/lib.cpp`
  - the headers they include: `src/{defs,dsc,expr,handle,param,polygon,resource,sketch,solvespace,ttf,ui,util}.h`,
    `src/srf/surface.h`, `src/render/render.h`, `src/platform/{platform,gui}.h`
- Added by Cadjitsu: `shim/mimalloc.h` — a tiny stand-in for the three mimalloc heap
  functions used by SolveSpace's temporary arena (`mi_heap_new`, `mi_heap_zalloc`,
  `mi_heap_destroy`), because the `+dfsg` tarball strips the bundled mimalloc.

Note: libslvs keeps global solver state (`static System SYS`, `Sketch SK`), so Cadjitsu
serializes every call through a single mutex (`cad::SlvsSolver`).

## Patches

- `src/slvs/lib.cpp` (both `Slvs_Solve` and `Slvs_SolveSketch`): `PT_ON_LINE`'s
  extra parameter (the point's position along the line) is initialised with
  `ModifyToSatisfy()`, which for this constraint only projects the point onto
  the line, and this happens *before* the parameter is copied into the solver's
  system. Upstream never initialised it (`Slvs_CanInitiallySatisfy()` excludes
  `PT_ON_LINE`) and copied it first anyway, so it started at 0 and a point that
  was already on a line got pulled halfway towards the line's first point.
  Marked "Cadjitsu patch" in the source; covered by
  `tests/core/test_sketch_solver.cpp` ("points on lines stay where they are").
