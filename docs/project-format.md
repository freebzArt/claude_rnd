# Project File Format (draft, schema 0)

A project is one **JSON file** (`.ptrack`) plus an optional **cache folder**
(`<name>.ptrack.cache/`). The JSON file is the source of truth. The cache holds
derived data only (pyramids, per-frame diagnostics, heat-map buffers), and
deleting it loses nothing.

Design goals:

- **Human-readable and diff-able.** Artists and pipeline TDs can inspect a
  project, and projects work in git.
- **Versioned from day one.** Every file carries `schema` and `version`.
  Readers refuse newer major versions with a clear message and migrate older
  ones.
- **The node graph is the model.** The layer list in the UI is a view of the
  same graph (see the roadmap, section 5).
- **Embeddable.** The OFX plugin stores the same JSON, compressed, in a string
  parameter, so a Nuke script or Flame Batch setup stays self-contained.

All coordinates follow [conventions.md](conventions.md). All frames are host
frame numbers.

## Top level

```json
{
  "schema": "ptrack.project",
  "version": 0,
  "name": "sh010_screen",
  "created": "2026-10-26T23:00:00Z",
  "app": "ptrack 0.1.0",
  "sources": { ... },
  "nodes": { ... },
  "edges": [ ... ],
  "ui": { ... }
}
```

## Sources

Media the project reads. Paths are stored as written by the user and also
relative to the project file, so a moved show tree can be relinked.

```json
"sources": {
  "plate": {
    "kind": "exr-sequence",
    "path": "/shows/abc/sh010/plate/sh010_plate.%04d.exr",
    "relativePath": "../plate/sh010_plate.%04d.exr",
    "frames": [1001, 1120],
    "layer": "",
    "part": 0,
    "colour": { "input": "ACEScg", "trackingSpace": "ACEScct" }
  }
}
```

`kind` is `exr-sequence`, `movie` (adds `startFrame`, `timecode`, `fps`), or
`host-clip` (frames come from the OFX host).

## Nodes

Nodes are keyed by a stable id. Every node has `type`, `name`, `color` (used
consistently in the graph, layer list, viewer and timeline) and `params`.
Animated values use the keyframe form below.

| `type` | Purpose | Inputs | Outputs |
|---|---|---|---|
| `source` | Read media | none | `image` |
| `colour`, `bandpass`, `channel`, `denoise` | Preprocessing | `image` | `image` |
| `shape` | Bezier or X-spline matte | `transform` (optional) | `matte` |
| `key` | Luma, chroma or difference key | `image` | `matte` |
| `matte-merge`, `matte-subtract`, `matte-intersect`, `matte-invert`, `matte-grow` | Matte operations | `a`, `b` (subtract: `a` minus `b`) | `matte` |
| `track` | Planar track | `image`, `matte`, `prediction` (optional) | `transform` |
| `surface` | Insert corners | `transform` | `transform` |
| `export` | Nuke, Flame, JSON | `transform` | none |

### Shape example

```json
"shape_actor": {
  "type": "shape",
  "name": "Actor",
  "color": "#e0794a",
  "params": {
    "spline": "bezier",
    "closed": true,
    "points": {
      "keys": {
        "1001": [[[812.0, 403.5], [-12, 4], [12, -4], 3.0], ...],
        "1040": [[[901.2, 398.0], [-12, 4], [12, -4], 3.0], ...]
      },
      "interp": "smooth"
    },
    "feather": 2.0,
    "opacity": 1.0
  }
}
```

Each point is `[position, inTangent, outTangent, featherWidth]`. A shape linked
to a track (an edge into its `transform` input) stores its points in the
reference frame of that track. Its keys are then offsets on top of the track,
not absolute positions.

### Track example

```json
"track_wall": {
  "type": "track",
  "name": "Wall",
  "color": "#3fa7d6",
  "params": {
    "referenceFrame": 1001,
    "range": [1001, 1120],
    "model": "perspective",
    "strategy": "hybrid",
    "loss": "tukey",
    "photometric": "gain-bias",
    "levels": 0,
    "pause": { "zncc": 0.5, "znccDrop": 0.15, "inliers": 0.3, "visible": 0.2 }
  },
  "result": "track_wall.json"
}
```

`result` names a **track result** file in the cache folder, written in the
format below. When the OFX plugin embeds a project, it inlines results instead.

## Edges

```json
"edges": [
  { "from": "source_plate.image", "to": "colour_1.image" },
  { "from": "colour_1.image", "to": "track_wall.image" },
  { "from": "shape_wall.matte", "to": "sub_1.a" },
  { "from": "shape_actor.matte", "to": "sub_1.b" },
  { "from": "sub_1.matte", "to": "track_wall.matte" },
  { "from": "track_actor.transform", "to": "shape_actor.transform" },
  { "from": "track_wall.transform", "to": "surface_poster.transform" }
]
```

Holding out is always an edge into a `b` port of `matte-subtract`. This makes
it visible and never ambiguous, which is the point of the node graph.

## Keyframes

Every animatable parameter is either a plain value or:

```json
{ "keys": { "<frame>": <value>, ... }, "interp": "linear" | "smooth" | "step" }
```

## Track result (implemented: `ptrack track --out`)

Written by the CLI today, and by the workbench later.

```json
{
  "schema": "ptrack.track",
  "version": 1,
  "kind": "track",
  "source": "plate.%04d.exr",
  "format": { "width": 1920, "height": 1080, "pixelAspect": 1.0 },
  "referenceFrame": 1001,
  "surface": [[x, y], [x, y], [x, y], [x, y]],
  "settings": "model=perspective strategy=hybrid loss=tukey curve=acescct",
  "paused": { "frame": 1087, "reason": "match quality fell suddenly ..." },
  "frames": [
    {
      "frame": 1001,
      "h": [1, 0, 0, 0, 1, 0, 0, 0, 1],
      "corners": [[x, y], [x, y], [x, y], [x, y]],
      "gain": 1.0, "bias": 0.0, "refined": true,
      "diagnostics": {
        "rms": 0.0, "zncc": 1.0, "znccAll": 1.0, "inlierRatio": 1.0,
        "visibleFraction": 1.0, "conditionNumber": 12.3, "minEigenvalue": 0.04,
        "iterations": 4, "converged": true
      }
    }
  ]
}
```

- `h` maps reference-frame coordinates to that frame's coordinates, row-major,
  normalized so `h[8] = 1`.
- `corners` are the surface corners on that frame (BL, BR, TR, TL), included
  for convenience and for tools that don't want to apply `h`.
- Ground-truth files from `ptrack synth` use the same layout with
  `"kind": "truth"` and no diagnostics.
