# Cadjitsu MCP tools

Generated from the app (`Cadjitsu --mcp-tools`); do not edit by hand.
Lengths are millimetres, angles degrees. Numeric arguments also take expressions such as `"d1 * 2"` or
`"0.5 in"`. Every tool that changes the design is one undo step and shows in Cadjitsu's timeline.

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

## `offset_sketch`

Offset (like Fusion's sketch Offset): copies sketch curves a distance to one side, joined up at the corners, held there by ONE new offset dimension (a parameter you can change later). Pick curves by entity id (`curves`, e.g. from add_to_sketch) or by points on or near them (`near`, sketch coordinates); with `chain` (default) each picks everything joined to it end to end, so one point on a rectangle takes the whole outline. Shell a part: offset its outline inwards by the wall thickness, then extrude the ring between the two. Clearances for lids and fits work the same way (e.g. 0.2 mm).

| Argument | Type | Description |
|---|---|---|
| `chain` | boolean | pick whole chains of joined curves (default true) |
| `curves` | array of integer | the curves to offset |
| `distance` **(required)** | number | mm. Positive: closed outlines grow outwards (open curves go to the side of `side_point`, else to the left of the first curve's direction); negative: the other way |
| `near` | array of [x, y] | the curves nearest these points |
| `side_point` | [x, y] | optional: a point on the side to offset to (the sign of distance is then ignored) |
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

Drills holes into a planar face at world points on it (or at the points of a sketch): simple, counterbore (for socket head screws), countersink (for flat head screws) or tapped (a threaded hole: set thread, e.g. "M5"; M5 and up are modelled, smaller ones left at the tap drill unless thread_mode is "modeled"); to a depth or through all.

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
| `thread` | string | tapped: the thread size, e.g. "M3", "M5", "M8x1", "1/4-20 UNC" (see the thread tool) |
| `thread_clearance` | number or string | tapped: radial print clearance, mm (default 0.15) |
| `thread_mode` | `auto` / `modeled` / `tap_drill` | tapped: auto (default) models M5 / #10 and up |
| `through_all` | boolean | go through everything |
| `tip_angle` | number or string | drill point angle, default 118 |
| `type` | `simple` / `counterbore` / `countersink` / `tapped` | default simple |

## `emboss_text`

Engraves text into a face (cut in) or embosses it (raised) on any face of a body; on a curved face the letters follow the surface (they wrap exactly around cylinders and cones). The text's middle goes at `at` (a world point on or near the face) or at `position` in the face's own frame, else at the middle of the face. On a flat face the text reads from outside with y up on upright faces (on a top face, x and y are world X and Y); on a curved face y runs up along the surface. Returns the feature, the volume change and where the text is.

| Argument | Type | Description |
|---|---|---|
| `at` | [x, y, z] | where the middle of the text goes (world, mm): the nearest point of the face |
| `bold` | boolean | bold letters |
| `depth` | number or string | engrave depth or emboss height, mm, default 0.6 |
| `direction` | `engrave` / `emboss` | engrave (cut in, default) or emboss (raised) |
| `face` **(required)** | object |  |
| `font` | string | font family, default "DejaVu Sans" (bundled: DejaVu Sans, DejaVu Serif, DejaVu Sans Mono) |
| `italic` | boolean | italic letters |
| `letter_spacing` | number or string | extra space between letters, mm, default 0 |
| `line_spacing` | number or string | line pitch as a multiple of the size, default 1.2 |
| `mirror` | boolean | reverse the letters, to read from the other side (stamps, moulds) |
| `position` | [x, y] | instead of at: the middle of the text in the face's frame (mm) |
| `rotation` | number or string | turn about the face normal, degrees anticlockwise seen from outside, default 0 |
| `size` | number or string | letter height (the font's size), mm, default 5 |
| `text` **(required)** | string | the text; \n for more lines |

## `mirror`

Mirrors bodies, or holes / extrudes, across a plane (XY, XZ, YZ, a construction plane or a planar face). Symmetric parts: model half, mirror it with join.

| Argument | Type | Description |
|---|---|---|
| `bodies` | array of string | bodies to copy |
| `features` | array of integer or string | holes or extrudes to repeat (their copies cut / join like the original) |
| `join` | boolean | bodies: join copies that touch the original (default true) |
| `plane` **(required)** | string or object | Where: "XY", "XZ" or "YZ" (origin planes), {"plane": <construction plane feature id>}, or {"face": {"body": "b2", "index": 5}} for a planar face of a body. |
| `skip` | array of integer | copies to leave out |

## `pattern`

Repeats bodies, or holes / extrudes, in rows (rectangular: count and spacing along one or two directions) or around an axis (circular: count over a total angle) - bolt circles, rows of holes, grids of pegs.

| Argument | Type | Description |
|---|---|---|
| `angle` | number or string | circular: total degrees (default 360: evenly all round) |
| `axis` | any | circular: "x", "y", "z" (through the origin), {"edge": ...} (a straight or round edge) or {"face": ...} (a cylinder: a hole's axis) |
| `bodies` | array of string | bodies to copy |
| `count` | number or string | instances including the original (rectangular default 3, circular 6) |
| `count2` | number or string | instances along direction2 (default 2) |
| `direction` | any | rectangular: "x", "y", "z" or {"edge": {body, index}} |
| `direction2` | any | rectangular: an optional second direction |
| `features` | array of integer or string | holes or extrudes to repeat (their copies cut / join like the original) |
| `join` | boolean | bodies: join copies that touch the original (default true) |
| `skip` | array of integer | copies to leave out |
| `spacing` | number or string | rectangular: mm between instances (negative: the other way) |
| `spacing2` | number or string | mm along direction2 |
| `symmetric` | boolean | circular: spread both ways from the original |
| `type` **(required)** | `rectangular` / `circular` | rectangular (rows along directions) or circular (around an axis) |

## `thread`

Threads round faces: a hole's wall (internal thread, to take a screw) or a boss (external, a bolt). The size comes from the diameter (a hole drilled at a tap drill or minor diameter, a boss at the major diameter) unless given. Small threads print poorly, so by default M4 / #8 and smaller are only opened to the tap drill (tap them after printing, or use self-tapping screws or heat-set inserts); mode "modeled" forces real threads. clearance (radial) makes printed threads fit. Sizes: ISO M2-M24 coarse, M8x1..M24x2 fine, UNC and UNF #4 to 1".

| Argument | Type | Description |
|---|---|---|
| `clearance` | number or string | radial print clearance, mm (default 0.15) |
| `faces` **(required)** | array of objects | round faces (list_faces with type cylinder) |
| `left_hand` | boolean | left-handed thread |
| `length` | number or string | thread length from the open end, mm (default the whole face) |
| `mode` | `auto` / `modeled` / `tap_drill` | default auto |
| `size` | string | e.g. "M6", "M8x1", "1/4-20 UNC"; default: from the diameter |

## `draft`

Tilts flat faces about a hinge edge (a draft). The hinge is a straight edge of one of the faces (list_edges); the face on its other side sets the pull direction and stays put. A positive angle leans the faces in over the body - a taper that prints without support; lean_out tilts them out instead. Give all the walls of a box with one bottom edge to taper the whole box.

| Argument | Type | Description |
|---|---|---|
| `angle` | number or string | degrees (default 5) |
| `faces` **(required)** | array of objects | flat faces to tilt |
| `hinge` **(required)** | object |  |
| `lean_out` | boolean | tilt out over the hinge instead of in (default false) |

## `split_body`

Splits bodies in two (or more) with a plane - "XY"/"XZ"/"YZ", a construction plane or a planar face, all unbounded - or with the curves of a sketch swept both ways along its normal. Every piece becomes a body (the biggest keeps the name). For parts too big for the printer: keep "both" and set pins to drill matching alignment pin holes into both halves of a plane cut.

| Argument | Type | Description |
|---|---|---|
| `bodies` | array of string | bodies to split (default every body the tool crosses) |
| `curves` | array of integer | only these curves of the sketch (default all non-construction) |
| `keep` | `both` / `front` / `back` | plane splits: keep both sides (default) or only the side the plane normal points to (front) or the other |
| `pin_depth` | number or string | pin hole depth into each half, mm (default 6) |
| `pin_diameter` | number or string | pin hole diameter, mm (default 3.2) |
| `pins` | boolean | drill alignment pin holes into both halves (plane splits keeping both) |
| `plane` | string or object | Where: "XY", "XZ" or "YZ" (origin planes), {"plane": <construction plane feature id>}, or {"face": {"body": "b2", "index": 5}} for a planar face of a body. |
| `sketch` | integer | split with this sketch's curves instead of a plane |

## `combine`

Joins, cuts or intersects bodies: the target body with the tool bodies. keep_tools leaves the tools in place (for example to cut a clearance pocket and keep the part that fits in it).

| Argument | Type | Description |
|---|---|---|
| `keep_tools` | boolean | keep the tool bodies (default false) |
| `operation` | `join` / `cut` / `intersect` | default join |
| `target` **(required)** | string | the body that is changed |
| `tools` **(required)** | array of string | tool bodies |

## `offset_plane`

A construction plane offset from an origin plane, another construction plane or a planar face, optionally tilted about its own X axis (tilt_x), its Y axis (tilt_y) or both - X first, then the tilted Y - turning about the plane's centre. Sketch on it with create_sketch {plane: {plane: id}}.

| Argument | Type | Description |
|---|---|---|
| `angle` | number or string | older form: a single tilt in degrees about `axis` |
| `axis` | `x` / `y` | older form: the axis `angle` turns about (default x) |
| `base` **(required)** | string or object | Where: "XY", "XZ" or "YZ" (origin planes), {"plane": <construction plane feature id>}, or {"face": {"body": "b2", "index": 5}} for a planar face of a body. |
| `offset` | number or string | distance along the base normal, mm (default 0) |
| `tilt_x` | number or string | tilt about the plane's X axis, degrees (default 0) |
| `tilt_y` | number or string | tilt about the plane's Y axis (after tilt_x), degrees (default 0) |

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

## `set_material`

Sets what bodies are printed in: the filament (PLA, PETG or TPU), its finish (matte, silk or semitransparent) and colour (a named filament colour such as "Signal Red", "Silk Gold", "Ice Blue", or "#rrggbb"). One undo step. The Rendered style and render_image show it (layer lines, silk sheen, light through semitransparent parts); default: grey matte PLA. Returns the bodies with their materials.

| Argument | Type | Description |
|---|---|---|
| `bodies` | array of string | the bodies (all of them if left out) |
| `color` | string | a named filament colour or #rrggbb (default: keep, or one that suits the finish) |
| `finish` | `matte` / `silk` / `semitransparent` | finish (default: keep, or matte) |
| `material` | `PLA` / `PETG` / `TPU` | filament (default: keep, or PLA) |
| `reset` | boolean | back to the default grey matte PLA (ignores the rest) |

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

## `measure`

Measures between two things, like Inspect > Measure: the minimum distance and its closest points, the X/Y/Z components, the distance between centres (holes, circles, cylinder axes) and the angle between flat faces or straight edges. With only `a`, gives its own size (area, length, radius, volume). Changes nothing.

| Argument | Type | Description |
|---|---|---|
| `a` **(required)** | object | the first thing: {"body": "Body1"} (the whole body), {"body": ..., "face": i} / "edge" / "vertex" (indexes from list_faces / list_edges), or {"point": [x, y, z]} |
| `b` | object | the second thing (optional): {"body": "Body1"} (the whole body), {"body": ..., "face": i} / "edge" / "vertex" (indexes from list_faces / list_edges), or {"point": [x, y, z]} |

## `overhangs`

Overhang check for printing upwards (+Z): for each body, the area of downward faces leaning further from vertical than `threshold` (they need support), flat bridges, and faces near the limit, with the worst faces (index for list_faces). The build plate is the lowest body's bottom. Fix with chamfers (45 deg) or by reorienting.

| Argument | Type | Description |
|---|---|---|
| `threshold` | number | degrees from vertical a face may lean (default 45) |

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

Opens a .cadjitsu design (or an older .cadly one).

| Argument | Type | Description |
|---|---|---|
| `path` **(required)** | string | absolute path of a .cadjitsu or .cadly file |

## `save_design`

Saves the design as a .cadjitsu file (the whole history).

| Argument | Type | Description |
|---|---|---|
| `path` **(required)** | string | absolute path, ending .cadjitsu |

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
