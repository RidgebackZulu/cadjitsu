# Cadly MCP tools

Generated from the app (`Cadly --mcp-tools`); do not edit by hand.
Lengths are millimetres, angles degrees. Numeric arguments also take expressions such as `"d1 * 2"` or
`"0.5 in"`. Every tool that changes the design is one undo step and shows in Cadly's timeline.

## `get_design`

The open design: timeline features (id, type, status, parameters), bodies (volume, area, bounding box, face and edge counts), sketches with their profiles, construction planes, what is visible (and the browser folders), parameters, and the history marker. Call this first and after edits.

No arguments.

## `list_faces`

Faces of a body with an index for other tools: type (plane, cylinder...), area, centre, outward normal of planes, radius and axis of cylinders. Filter with normal ("+z", "-x"...) or type; sorted by distance to near.

| Argument | Type | Description |
|---|---|---|
| `body` **(required)** | string | body id or name |
| `limit` | integer | at most this many (default 100) |
| `near` | [x, y, z] | sort by distance from this point |
| `normal` | string | only planar faces facing this way: +x, -x, +y, -y, +z, -z |
| `type` | `plane` / `cylinder` / `cone` / `sphere` / `torus` / `other` | only faces of this type |

## `list_edges`

Edges of a body with an index for fillet / chamfer: type (line, circle...), length, start, end, midpoint, direction of lines, centre and radius of circles. Filter with direction ("x", "y", "z": lines parallel to it), type, or sort by distance from near.

| Argument | Type | Description |
|---|---|---|
| `body` **(required)** | string | body id or name |
| `direction` | `x` / `y` / `z` | only straight edges parallel to this axis |
| `limit` | integer | at most this many (default 100) |
| `near` | [x, y, z] | sort by distance from this point (to the edge's midpoint) |
| `type` | `line` / `circle` / `other` | only edges of this type |

## `screenshot`

A PNG picture of the canvas, optionally from a standard view and in a display style. Use it to check the result.

| Argument | Type | Description |
|---|---|---|
| `max_width` | integer | scale down to at most this width in pixels (default 1200) |
| `style` | `shaded_edges` / `shaded` / `wireframe` / `rendered` | display style |
| `view` | `home` / `front` / `back` / `left` / `right` / `top` / `bottom` | look from here first (and fit) |

## `create_sketch`

Creates a sketch (one timeline step) on an origin plane, a construction plane or a planar face, with its geometry. Returns the sketch id, its frame (for converting between sketch and world coordinates), its closed profiles (regions to extrude, each with an inside point) and the parameters of its dimensions.

| Argument | Type | Description |
|---|---|---|
| `entities` **(required)** | array of objects | Sketch geometry in the sketch's own 2D coordinates (mm). On XY these are world X, Y; on XZ, world X and Z; on YZ, world Y and Z; on a face or construction plane, see the frame this tool returns. |
| `name` | string | optional name, e.g. "Base outline" |
| `plane` **(required)** | string or object | Where: "XY", "XZ" or "YZ" (origin planes), {"plane": <construction plane feature id>}, or {"face": {"body": "b2", "index": 5}} for a planar face of a body. |

## `add_to_sketch`

Adds geometry to an existing sketch (one undo step). Bodies already made from the sketch keep their shape; the new regions can be extruded as new bodies.

| Argument | Type | Description |
|---|---|---|
| `entities` **(required)** | array of objects | Sketch geometry in the sketch's own 2D coordinates (mm). On XY these are world X, Y; on XZ, world X and Z; on YZ, world Y and Z; on a face or construction plane, see the frame this tool returns. |
| `sketch` **(required)** | integer | sketch feature id |

## `extrude`

Extrudes sketch profiles (regions) or planar faces into a solid. By default it makes a NEW BODY (combine bodies afterwards with combine). Profiles are picked by points inside them (sketch coordinates); without points every profile of the sketch is used.

| Argument | Type | Description |
|---|---|---|
| `direction` | `one_side` / `two_sides` / `symmetric` | default one_side |
| `distance` | number or string | length in mm (negative: the other way), default 10 |
| `distance2` | number or string | second side length for two_sides |
| `extent` | `distance` / `all` | all: through everything (default distance) |
| `faces` | array of objects | planar faces to extrude instead of / as well as profiles |
| `flip` | boolean | with extent all: go the other way |
| `operation` | `new_body` / `join` / `cut` / `intersect` | default new_body |
| `profile_points` | array of [x, y] | which regions of the sketch (sketch coordinates) |
| `sketch` | integer | sketch feature id (for profiles) |
| `taper` | number or string | taper angle in degrees (positive flares outwards) |

## `fillet`

Rounds edges (from list_edges) with a radius. Faces round all their edges.

| Argument | Type | Description |
|---|---|---|
| `edges` | array of objects | edges to round |
| `faces` | array of objects | faces whose edges to round |
| `radius` **(required)** | number or string | radius in mm |

## `chamfer`

Bevels edges (from list_edges): equal distance, two distances, or distance and angle. Chamfers on bottom edges print better than fillets (no overhang).

| Argument | Type | Description |
|---|---|---|
| `angle` | number or string | angle in degrees (distance_angle), default 45 |
| `distance` **(required)** | number or string | distance in mm |
| `distance2` | number or string | second distance (two_distances) |
| `edges` | array of objects | edges to chamfer |
| `faces` | array of objects | faces whose edges to chamfer |
| `flip` | boolean | swap the sides of distance / angle |
| `type` | `equal` / `two_distances` / `distance_angle` | default equal |

## `hole`

Drills holes into a planar face at world points on it (or at the points of a sketch): simple, counterbore (for socket head screws) or countersink (for flat head screws); to a depth or through all.

| Argument | Type | Description |
|---|---|---|
| `cbore_depth` | number or string | counterbore depth, default 3 |
| `cbore_diameter` | number or string | counterbore diameter, default 9 |
| `csink_angle` | number or string | countersink angle, default 90 |
| `csink_diameter` | number or string | countersink diameter, default 9 |
| `depth` | number or string | depth, default 10 (ignored with through_all) |
| `diameter` | number or string | hole diameter, default 5 |
| `face` | object |  |
| `flat_tip` | boolean | flat bottom instead of a drill point |
| `points` | array of [x, y, z] | hole centres (world coordinates, mm) |
| `sketch` | integer | instead of face + points: every point entity of this sketch (holes go along its normal) |
| `through_all` | boolean | go through everything |
| `tip_angle` | number or string | drill point angle, default 118 |
| `type` | `simple` / `counterbore` / `countersink` | default simple |

## `combine`

Joins, cuts or intersects bodies: the target body with the tool bodies. keep_tools leaves the tools in place (for example to cut a clearance pocket and keep the part that fits in it).

| Argument | Type | Description |
|---|---|---|
| `keep_tools` | boolean | keep the tool bodies (default false) |
| `operation` | `join` / `cut` / `intersect` | default join |
| `target` **(required)** | string | the body that is changed |
| `tools` **(required)** | array of string | tool bodies |

## `offset_plane`

A construction plane offset from an origin plane, another construction plane or a planar face, optionally turned by an angle about its local X or Y axis. Sketch on it with create_sketch {plane: {plane: id}}.

| Argument | Type | Description |
|---|---|---|
| `angle` | number or string | rotation in degrees (default 0) |
| `axis` | `x` / `y` | rotation axis (default x) |
| `base` **(required)** | string or object | Where: "XY", "XZ" or "YZ" (origin planes), {"plane": <construction plane feature id>}, or {"face": {"body": "b2", "index": 5}} for a planar face of a body. |
| `offset` | number or string | distance along the base normal, mm (default 0) |

## `edit_feature`

Changes values of an existing feature (one undo step), like Edit Feature: e.g. {"distance": 25} for an extrude, {"radius": "3 mm"} for a fillet, {"operation": "cut"}. Keys are those get_design shows in the feature data (see set_parameter for any parameter by name). Numbers are mm, or degrees for angles.

| Argument | Type | Description |
|---|---|---|
| `feature` **(required)** | integer or string | feature id or name |
| `name` | string | rename it |
| `set` | object | key -> new value |

## `set_parameter`

Sets a model parameter (a dimension, distance, radius...: names like "d3" from get_design) to a value or expression. Everything that depends on it recomputes, as in Fusion's Change Parameters.

| Argument | Type | Description |
|---|---|---|
| `expression` **(required)** | number or string | new value (number = mm) or expression |
| `name` **(required)** | string | parameter name, e.g. d3 |

## `suppress`

Suppresses (or unsuppresses) a feature: it is skipped, as if it were not there.

| Argument | Type | Description |
|---|---|---|
| `feature` **(required)** | integer or string |  |
| `suppressed` | boolean | default true |

## `set_visibility`

Shows or hides bodies, sketches and construction planes (like the eyes in the browser), or whole browser folders: a hidden folder hides everything in it but keeps each item's own setting for when it is shown again. Items are ids or names. Returns what is visible afterwards.

| Argument | Type | Description |
|---|---|---|
| `bodies` | array of string | bodies to show or hide |
| `folders` | array of `bodies` / `sketches` / `construction` / `origin` | folders to show or hide as a whole |
| `planes` | array of integer or string | construction plane ids or names |
| `sketches` | array of integer or string | sketch ids or names |
| `visible` **(required)** | boolean | true to show, false to hide |

## `delete_feature`

Deletes a feature from the timeline (one undo step).

| Argument | Type | Description |
|---|---|---|
| `feature` **(required)** | integer or string |  |

## `roll_to`

Moves the timeline's history marker: to a position (0 = before everything) or to just after a feature. New features are inserted at the marker.

| Argument | Type | Description |
|---|---|---|
| `after` | integer or string | feature id or name |
| `position` | integer | number of active features |

## `undo`

Undoes the last change (like Cmd+Z). Optional count.

| Argument | Type | Description |
|---|---|---|
| `count` | integer | how many steps, default 1 |

## `redo`

Redoes what was undone. Optional count.

| Argument | Type | Description |
|---|---|---|
| `count` | integer | how many steps, default 1 |

## `set_view`

Points the camera: home, front, back, left, right, top, bottom (and fits the model).

| Argument | Type | Description |
|---|---|---|
| `view` **(required)** | `home` / `front` / `back` / `left` / `right` / `top` / `bottom` |  |

## `set_display_style`

Display style of the canvas: shaded_edges, shaded, wireframe or rendered.

| Argument | Type | Description |
|---|---|---|
| `style` **(required)** | `shaded_edges` / `shaded` / `wireframe` / `rendered` |  |

## `section`

Section analysis: cuts the view (not the model) by a plane moved along its normal, to look inside. hide: true turns sections off.

| Argument | Type | Description |
|---|---|---|
| `flip` | boolean | keep the other side |
| `hide` | boolean | turn all sections off |
| `offset` | number | mm along the plane normal |
| `plane` | string or object | Where: "XY", "XZ" or "YZ" (origin planes), {"plane": <construction plane feature id>}, or {"face": {"body": "b2", "index": 5}} for a planar face of a body. |

## `new_design`

Starts a new, empty design (the current one is discarded; save it first).

No arguments.

## `open_design`

Opens a .cadly design.

| Argument | Type | Description |
|---|---|---|
| `path` **(required)** | string | absolute path of a .cadly file |

## `save_design`

Saves the design as a .cadly file (the whole history).

| Argument | Type | Description |
|---|---|---|
| `path` **(required)** | string | absolute path, ending .cadly |

## `export_stl`

Exports bodies as an STL for 3D printing. The mesh is checked first (watertight, manifold, outward, volume); a mesh that fails is not written unless write_invalid. Returns the printability report.

| Argument | Type | Description |
|---|---|---|
| `binary` | boolean | default true |
| `bodies` | array of string | default: every visible body |
| `merge` | boolean | unite touching bodies into one solid (default true) |
| `path` **(required)** | string | absolute path, ending .stl |
| `refinement` | `coarse` / `medium` / `fine` | default medium |
| `write_invalid` | boolean | write even if the check fails |

## `export_step`

Exports bodies as a STEP file (AP242 by default); it is read back to check the volume.

| Argument | Type | Description |
|---|---|---|
| `bodies` | array of string | default: every visible body |
| `path` **(required)** | string | absolute path, ending .step |
| `schema` | `ap242` / `ap214` | default ap242 |

## `batch`

Runs several tool calls in order, in one request: e.g. create_sketch, extrude, list_edges, fillet, get_design. Use it to build a part in a few turns instead of many. Each call is still its own undo step. Results come back per call, in order; by default the batch stops at the first failure (later calls are skipped) and reports it. Calls cannot use each other's results, so batch steps whose arguments you already know (sketch ids are predictable from get_design's features), then inspect and continue.

| Argument | Type | Description |
|---|---|---|
| `calls` **(required)** | array of objects | the calls, run in order |
| `stop_on_error` | boolean | stop at the first failed call (default true) |
