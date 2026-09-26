# libslvs provenance

- Upstream: SolveSpace 3.2 (https://solvespace.com), GPLv3 — see `COPYING.txt`.
- Obtained from the Debian/Ubuntu source archive:
  `https://archive.ubuntu.com/ubuntu/pool/universe/s/solvespace/solvespace_3.2+dfsg.orig.tar.xz`
- Files copied verbatim (no modifications):
  - `include/slvs.h`
  - `src/{constrainteq,entity,expr,system,util}.cpp`
  - `src/platform/platformbase.cpp`, `src/slvs/lib.cpp`
  - the headers they include: `src/{defs,dsc,expr,handle,param,polygon,resource,sketch,solvespace,ttf,ui,util}.h`,
    `src/srf/surface.h`, `src/render/render.h`, `src/platform/{platform,gui}.h`
- Added by Cadly: `shim/mimalloc.h` — a tiny stand-in for the three mimalloc heap
  functions used by SolveSpace's temporary arena (`mi_heap_new`, `mi_heap_zalloc`,
  `mi_heap_destroy`), because the `+dfsg` tarball strips the bundled mimalloc.

Note: libslvs keeps global solver state (`static System SYS`, `Sketch SK`), so Cadly
serializes every call through a single mutex (`cad::SlvsSolver`).
