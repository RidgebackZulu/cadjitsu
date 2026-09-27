---
name: cadly-cad
description: Design 3D-printable parts in the Cadly parametric CAD app through its MCP server - sketch, extrude, fillet, chamfer, drill holes, combine bodies, change parameters, and export a checked watertight STL or a STEP file. Use when the user wants to model, modify, inspect or export a part in Cadly, or asks for a 3D-printable part and Cadly's MCP tools (get_design, create_sketch, extrude...) are available.
version: 1.0.0
author: Cadly
license: GPL-3.0-or-later
metadata:
  category: design
  tags: "cad, 3d-printing, parametric, mcp, design, stl"
---

# Designing printable parts with Cadly

Cadly is a native, Fusion 360-style parametric CAD app for 3D-printable parts. Its MCP server lets you drive the
open design: every tool call edits the same timeline the user sees, is one undo step, and recomputes the model
before answering. Work like a careful CAD user: small steps, check the result after each one, keep the design
parametric.

## 1. Connect

Cadly must be running with its MCP server switched on: click the **MCP** button at the top right of Cadly's
toolbar (or File > MCP Server...), tick **Enable the MCP server**, click **Apply**. The same dialog shows the port
(default 7823), the auth token (Generate / Copy) and ready-made snippets for each client:

- **Claude Code plugin** (this repository): `/plugin marketplace add RidgebackZulu/cadly`, then
  `/plugin install cadly@cadly`, and start Claude Code with `CADLY_MCP_TOKEN=<token>` in the environment
  (`CADLY_MCP_URL` too if the port is not 7823).
- **Claude Code without the plugin:**
  `claude mcp add --transport http cadly http://127.0.0.1:7823/mcp --header "Authorization: Bearer <token>"`
- **Hermes Agent:** `hermes plugins install RidgebackZulu/cadly/plugins/cadly --enable` (the plugin folder,
  not the whole repository) for this skill, then `hermes mcp add cadly --url http://127.0.0.1:7823/mcp --auth
  header` and paste the token when asked.
- **Claude Desktop:** use the "Claude Desktop (config)" snippet from the dialog (it runs `npx mcp-remote`).

When an agent is connected the MCP button glows green, and every call appears in the dialog's **Event log**.
If a tool call fails with 401, the token is wrong; if the connection is refused, the server is off or on another
port. Start every session with `get_design`.

## 2. Coordinates and units

- Millimetres and degrees. Numbers are taken as mm (or degrees for angles); strings are expressions:
  `"12.5 mm"`, `"0.5 in"`, `"d3 * 2"` (parameters by name).
- **Z is up**, the print direction. The XY plane (z = 0) is the build plate. Design parts sitting on XY,
  growing in +Z.
- Sketch coordinates are 2D in the sketch's own frame. On **XY** they are world (x, y); on **XZ** world (x, z);
  on **YZ** world (y, z). The XZ sketch's normal is -Y and YZ's is +X, so check extrude directions with the
  returned bounding box. On a face or construction plane, use the `frame` returned by `create_sketch`:
  world = origin + x·x_axis + y·y_axis, and sketch (x, y) = ((P - origin)·x_axis, (P - origin)·y_axis).

## 3. The workflow

1. **Inspect** - `get_design`: features (with status and parameters), bodies (volume, bounding box), sketches
   (profiles with an inside point), construction planes.
2. **Sketch** - `create_sketch` on XY / XZ / YZ, `{"plane": id}` or `{"face": {"body": "b2", "index": 5}}` with
   rectangles, circles, polygons, lines, arcs, points. Rectangles and circles get driving dimensions
   (parameters such as `d1`, `d2`) and are anchored (rectangle at corner1, circle at its centre), so later
   parameter changes grow them predictably.
   For an angled or raised sketch plane, make a construction plane first:
   `offset_plane {"base": "XY", "offset": 20, "tilt_x": 30, "tilt_y": 15}` tilts it about its own X axis, then
   its (tilted) Y axis, turning about its centre; `get_design` gives each plane's `center` and `normal`.
3. **Extrude** - `extrude` the sketch's profiles. It makes a **new body** by default. Pick regions with
   `profile_points` (sketch coordinates inside the region); without them every profile is extruded (a circle
   inside a rectangle then fills the hole, so give a point in the ring to keep the hole).
4. **Combine** - join / cut / intersect bodies with `combine` (target + tools, optional `keep_tools`). Prefer
   "new body, then combine" to extruding with `operation: "cut"`, because it keeps each shape editable and visible.
5. **Refine** - `fillet`, `chamfer`, `hole` using face / edge indexes from `list_faces` / `list_edges`.
6. **Verify** - `get_design` (volumes, bounding boxes, every feature `ok`), `measure` (distances, wall
   thicknesses, hole spacing, angles), `screenshot` (view `home`, `top`, `front`...), `section` to look inside.
7. **Export** - `export_stl` (report must say `printable: true`, `shells: 1` for a single part) and/or
   `export_step`. `save_design` keeps the editable history as a `.cadly` file.

**Batch the steps.** Agents often have a limited number of tool calls per turn, so use `batch` to run several
calls in one: `{"calls": [{"tool": "create_sketch", "arguments": {...}}, {"tool": "extrude", "arguments":
{"sketch": 1, "distance": 8}}, {"tool": "get_design"}]}`. Calls run in order, each is its own undo step, and the
batch stops at the first failure (the rest are reported as skipped). Calls cannot read each other's results, so
batch what you can predict (a new sketch's id is the next feature id, see `get_design`), then inspect and
batch the next stage (e.g. `list_edges`, then the fillets and holes, then `export_stl`).

## 4. Picking faces and edges

- Indexes belong to the current model: **list again after every feature**; fillets, holes and combines renumber.
- `list_faces {"body": "Body1", "normal": "+z"}` gives the top faces; `"near": [x, y, z]` sorts by distance.
- `list_edges {"body": "Body1", "direction": "z"}` gives vertical edges; filter corners by their `midpoint`.
  Cylinders have a straight seam edge - skip edges whose midpoint lies on a hole.
- Faces can be given to `fillet` / `chamfer` to treat all their edges.

## 5. Holes and fasteners

`hole` drills along the face normal at world points on a planar face (`through_all` or `depth`), or at the
stand-alone point entities of a sketch. Printed holes shrink: add ~0.2 mm, or use these clearances:

| Screw | Clearance hole | Counterbore (socket head) | Heat-set insert hole |
|---|---|---|---|
| M2 | 2.4 | 4.4 x 2.2 deep | 3.2 |
| M2.5 | 2.9 | 5.0 x 2.7 deep | 3.6 |
| M3 | 3.4 | 6.5 x 3.2 deep | 4.0-4.2 |
| M4 | 4.5 | 8.0 x 4.3 deep | 5.6 |
| M5 | 5.5 | 9.5 x 5.3 deep | 6.4 |

Keep at least 2 wall thicknesses (~2.5 mm) between a hole and an outside face.

## 6. Designing for FDM printing

- **Walls:** at least 1.2 mm (3 perimeters of a 0.4 mm nozzle); 2-3 mm for parts that carry load.
- **Overhangs:** faces sloping more than 45 degrees from vertical need support; `overhangs` lists them per body
  (with face indexes) so you can chamfer them or reorient the part. Prefer chamfers (45 degrees) to
  fillets on edges touching the build plate; fillets are fine on vertical and top edges.
- **Bridges:** keep unsupported horizontal spans under ~20 mm. Horizontal holes print better as teardrops or
  small (<8 mm).
- **Fits:** 0.2-0.3 mm clearance per side between mating printed parts; 0.1-0.15 mm for press fits.
- **Strength:** layers are weakest in Z; lay parts so loads run along the layers. Inside corners carrying
  load get fillets (R >= wall thickness).
- **Orientation:** the largest flat face goes on XY; avoid tiny first-layer contact.
- **Too big for the bed:** `split_body` with a plane and `pins: true` cuts it into pieces with alignment pin holes;
  export each piece.
- **Export check:** `export_stl` must report watertight and printable. More than one shell means separate
  pieces - combine touching bodies (or keep `merge: true`).

## 7. Changing a design

- `set_parameter {"name": "d2", "expression": 50}` - change any dimension or feature value; everything after
  it recomputes (like Fusion's Change Parameters). If the new value breaks the model, the change is undone and
  the error returned.
- `edit_feature {"feature": "Extrude1", "set": {"distance": 12, "operation": "join"}}` - change a feature's
  own values (keys as in its data; lengths in mm, angles in degrees).
- `suppress`, `delete_feature`, `roll_to` (insert features earlier in history), `undo` / `redo`.
- `set_visibility {"planes": ["Plane1"], "folders": ["sketches"], "visible": false}` - show or hide bodies,
  sketches, construction planes, or whole browser folders (`bodies`, `sketches`, `construction`, `origin`); a
  hidden folder keeps each item's own setting. Tidy up before a `screenshot`.
- `add_to_sketch` adds geometry to an existing sketch. Bodies already made from it keep their shape; the new
  regions can be extruded as new bodies.

## 8. When a tool fails

Failures come back as tool errors with the reason (a fillet radius too large for its edges, a lost
reference, a profile point outside every region, an index out of range). **Nothing is added when a feature
fails** - fix the arguments and call it again. Common fixes:

- "radius too large": use a smaller radius, or fillet fewer edges at a time.
- "no profile contains [x, y]": check the sketch's `profiles[].inside_point` and use sketch coordinates.
- Index out of range: call `list_faces` / `list_edges` again (the model changed).
- A feature later shows `warning` / `error` in `get_design` after an upstream edit: inspect it, adjust
  with `edit_feature`, or `undo`.

## 9. Worked example: an L-bracket with screw holes

```text
create_sketch {"plane": "XY", "entities": [{"type": "rectangle", "corner1": [0, 0], "corner2": [60, 40]}]}
    -> sketch 1, parameters d1 = 60 (width), d2 = 40 (depth)
extrude {"sketch": 1, "distance": 6}                                  -> Body1, the base plate
create_sketch {"plane": "XZ", "entities": [{"type": "rectangle", "corner1": [0, 6], "corner2": [60, 50]}]}
extrude {"sketch": 3, "distance": -6}           -> Body2, the upright at y 0..6 (the XZ sketch's normal is
                                                   -Y, so a negative distance grows towards +Y)
combine {"target": "Body1", "tools": ["Body2"], "operation": "join"}
list_edges {"body": "Body1", "direction": "x", "near": [30, 6, 6]}   -> the inside corner edge
fillet {"edges": [{"body": "Body1", "index": <inside edge>}], "radius": 4}
list_faces {"body": "Body1", "normal": "+z"}   -> two faces: the base top (centre z = 6) and the upright's top
hole {"face": {"body": "Body1", "index": <top face>}, "points": [[12, 25, 6], [48, 25, 6]],
      "type": "counterbore", "diameter": 3.4, "cbore_diameter": 6.5, "cbore_depth": 3.2, "through_all": true}
list_edges {"body": "Body1", "direction": "x", "near": [30, 40, 0]}  -> the front bottom edge
chamfer {"edges": [{"body": "Body1", "index": <front bottom edge>}], "distance": 1}
get_design; screenshot {"view": "home"}
export_stl {"path": "/Users/me/Desktop/bracket.stl", "refinement": "fine"}   -> printable: true, shells: 1
```

## 10. Worked example: a box with a lid pocket

```text
create_sketch {"plane": "XY", "entities": [{"type": "center_rectangle", "center": [0, 0], "width": 80, "height": 50}]}
extrude {"sketch": 1, "distance": 30}                                  -> Body1 (outer shell)
offset_plane {"base": "XY", "offset": 2}                               -> plane 3 (the floor thickness)
create_sketch {"plane": {"plane": 3}, "entities": [{"type": "center_rectangle", "center": [0, 0], "width": 76, "height": 46}]}
extrude {"sketch": 4, "distance": 40}                                  -> Body2 (the cavity tool)
combine {"target": "Body1", "tools": ["Body2"], "operation": "cut"}   -> 2 mm walls and floor
list_edges {"body": "Body1", "direction": "z"} -> the 4 outer vertical corners (midpoint x = +-40, y = +-25)
fillet {"edges": [...4 corners...], "radius": 5}
section {"plane": "XZ", "offset": 0}; screenshot {"view": "front"}; section {"hide": true}
```

A matching lid: sketch the outer rectangle again on XY, extrude 2 mm as a new body, then a 75.4 x 45.4
(0.3 mm clearance) lip 3 mm tall on its top face, and position it beside the box before export.

## 11. Tool reference

See [references/tools.md](references/tools.md) for every tool and argument (generated from the app).
