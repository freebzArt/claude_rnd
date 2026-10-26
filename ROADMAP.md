# Planar Tracker for VFX: Roadmap

A high-end planar tracker for compositing work, delivered as an OFX plugin for
**Nuke** and **Flame**, with its own node-based workbench, project files, and
direct export to Nuke CornerPin and Flame Action.

Targets: NVIDIA GPU workstations on Windows and Linux, and Apple Silicon Macs.
Main inputs: OpenEXR sequences and QuickTime `.mov` files.

---

## 1. Guiding decisions

These shape every phase. Phase 0 should confirm or overturn them before much
code exists.

### 1.1 Engine first; the plugin and the app are both clients

```
                ┌──────────────────────────────────────────────┐
                │                 CORE ENGINE (C++)            │
                │  I/O · color · pyramids · solvers · mattes   │
                │  diagnostics · export · project model        │
                │  Compute backends: CUDA │ Metal │ CPU (SIMD) │
                └──────────────┬───────────────┬───────────────┘
                               │               │
          ┌────────────────────┴──┐     ┌──────┴────────────────────────┐
          │  WORKBENCH APP        │◄───►│  OFX PLUGIN (Nuke, Flame)     │
          │  (separate process)   │ IPC │  · fetches frames from host   │
          │  node graph, viewer,  │     │  · renders pin/stab/matte/grid│
          │  shapes, heat maps    │     │  · exposes corners as params  │
          └───────────────────────┘     └───────────────────────────────┘
                     ▲
                     │  same engine, no UI
          ┌──────────┴───────────┐
          │  CLI / Python API    │  farm batch tracking, pipeline tools
          └──────────────────────┘
```

**Why a separate workbench process instead of drawing everything inside the
host's OFX UI:**

- OFX parameter UIs can't hold a node graph, a layer manager, a curve editor,
  or heat map views. Overlay support also differs a lot between hosts, and
  Flame's OFX overlay support is more limited than Nuke's.
- Nuke is a Qt application. Loading a second Qt UI into Nuke's process risks
  Qt version and symbol clashes. Running the UI out of process avoids this.
- Mocha Pro and Silhouette already use this pattern inside Nuke and Flame, so
  artists know it: the plugin node lives in the comp, and a button opens the
  full tracker UI, which pulls frames from the host.
- The same app also runs standalone on EXR and MOV files with no host at all.

### 1.2 The core solver is a dense direct method, not a feature tracker

The main tracker is a **direct, area-based homography solver**. It aligns every
weighted pixel inside the matte, using Efficient Second-order Minimization (ESM)
parameterized on the SL(3) Lie algebra, inside a coarse-to-fine pyramid. It
uses robust M-estimators solved by IRLS and jointly estimates a photometric
model.

Reasons:

- **Sub-pixel accuracy and stability.** It uses all the texture in the region,
  including low-contrast surfaces where feature detectors find nothing. This
  is the class of tracker Mocha made the industry standard.
- **Built-in occlusion handling.** Robust weights automatically down-weight
  pixels that don't fit the plane, such as passing actors, reflections, and
  specular highlights.
- **It makes the heat map cheap (section 6, Phase 5).** A direct IRLS solver
  already computes a residual, a weight, and a Jacobian for every pixel. A
  per-pixel "who pushed the track which way" map is a by-product of data the
  solver already has, not a separate research project.

Feature-based matching (learned or classical keypoints with RANSAC) is a
**support system** layered on top. It handles initialization on large motion,
re-acquisition after occlusion or the plane leaving frame, and drift checks
against keyframes.

### 1.3 The node graph is the source of truth; a layer list is a view of it

Every track, shape, matte operation, and export is a node. Artists who just
want to track a sign never need to open the graph: a classic layer list
(Mocha-style) edits the same underlying graph. When things get complicated,
the graph shows exactly what holds out what (section 5).

### 1.4 Track in a perceptual space, never raw scene-linear

Scene-linear EXR values are dominated by highlights: one specular hit at 40.0
outweighs the whole wall at 0.18 in a least-squares solve. Tracking input goes
through an OCIO transform into a log or perceptual tracking space by default
(for example ACEScct or the show's log space). Most artists never think about
this, and it quietly ruins tracks.

---

## 2. Phase overview

| Phase | Name | Outcome | Rough size* |
|---|---|---|---|
| 0 | Foundations and de-risking | Host capability matrix, GPU approach chosen, IPC prototype | 3–4 wks |
| 1 | Core engine (headless) | Accurate CPU tracker, EXR/MOV I/O, CLI that outputs a pasteable Nuke CornerPin | 8–10 wks |
| 2 | Workbench MVP | Standalone app: viewer, shapes and keyframes, node graph v1, grid preview, save/load, clipboard export | 10–12 wks |
| 3 | GPU acceleration | CUDA and Metal backends, GPU decode, interactive tracking speed | 4–6 wks (can overlap 2) |
| 4 | OFX plugin and host integration | Nuke and Flame plugin, frame bridge, Flame export, installers | 8–10 wks |
| 5 | Diagnostics: heat map and "why did it slip" | Per-pixel contribution maps, slip analyzer, trackability preview | 4–6 wks |
| 6 | Robustness and lighting-agnostic tracking | Bandpass and other preprocessing, ZNCC/MI costs, keyers, re-acquisition, global refinement, lens and blur handling | 10–14 wks |
| 7 | Advanced and ML-assisted | SAM 2 mattes, mesh tracking, 3D plane and camera export, roto export | open-ended, pick à la carte |
| 8 | Pipeline and polish | Python API, farm CLI, keymaps, docs, licensing | ongoing |

\*Rough sizes assume 2–3 experienced engineers. They are for sequencing, not
commitments.

**Milestones**

- **M1, "It tracks"** (end of Phase 1): CLI tracks a real EXR shot with a mask
  and puts a CornerPin2D on the clipboard that pastes cleanly into Nuke.
- **M2, "Artist-usable standalone"** (end of Phases 2 and 3): draw, track,
  correct, preview, and export, at interactive speed.
- **M3, "v1.0 in the comp"** (end of Phases 4 and 5): runs in Nuke and Flame,
  with heat map diagnostics.
- **M4, "Tough-shot toolkit"** (end of Phase 6).

---

## 3. Phases in detail

### Phase 0: Foundations and de-risking

The goal is to remove the unknowns that could force a re-architecture later.
Each spike ends with a short written decision.

1. **Host capability matrix (Nuke 15 and 16, Flame 2026 and 2027, on every
   supported OS).** Verify:
   - temporal clip access (fetching arbitrary frames during render or
     interaction);
   - overlay interacts (drawing corners and grid in the host viewer);
   - animated 2D parameters and keyframe write-back from the plugin;
   - custom or string parameters big enough to embed a compressed project;
   - launching a child process from the plugin;
   - multiple input clips (Source, Insert, Matte);
   - any OFX GPU render suites the host supports (CUDA or Metal);
   - **Flame specifically:** whether Batch or Action accepts pasted setup
     text, and which Action surface mode gives a perspective-correct corner
     pin rather than a bilinear one.
2. **GPU kernel strategy spike.** Implement one hot path (warp, gradients,
   Jacobian, and the JᵀWJ / JᵀWr reduction) on CUDA and on Metal, then pick
   the approach:
   - *Recommended:* write kernels once in **Slang** (single source that
     compiles to CUDA, Metal, and CPU), with a thin native runtime per
     backend (CUDA Driver API, metal-cpp).
   - *Alternative:* **Halide**, which is strong for image pipelines and
     scheduling, though its reductions are less direct.
   - CPU fallback path using SIMD (Highway or ISPC). It also serves as the
     bit-tolerance reference for tests.
3. **Out-of-process UI bridge prototype.** Plugin to app over a local socket
   for commands, plus shared memory for frame buffers. Measure the cost of
   sending 4K half-float frames.
4. **Project file format.** Human-readable JSON for the graph, shapes,
   keyframes, and params, so it diffs cleanly and works with git. A binary
   sidecar holds caches (pyramids, per-frame diagnostics), and losing it is
   safe. Version the schema from day one.
5. **Coordinate and time conventions doc.** Pick one internal convention and
   test every exporter against it:
   - pixel-center convention, y-down internally;
   - data window vs display window (EXR overscan);
   - pixel aspect ratio (anamorphic 2× squeeze);
   - frame-number mapping (EXR `1001`-based sequences vs MOV frame 0, plus
     timecode).
6. **Third-party license review**: FFmpeg (LGPL build), Qt, OCIO, OIIO,
   OpenEXR, and the ML model weights (see Risks).

**Exit criteria:** written decisions on GPU approach, UI process model, file
format, and a per-host feature matrix.

---

### Phase 1: Core tracking engine (headless, CPU reference)

**I/O and color**
- OpenEXR 3.x: multi-part and multi-layer, any channel or AOV, half and float,
  data and display windows, PAR, all common compressions (PIZ, ZIP, DWAA/B).
- MOV via FFmpeg: ProRes (422, 4444, RAW-adjacent workflows via external
  debayer), DNxHR, H.264, HEVC. Honor color tags and full vs legal range.
- OCIO v2 for input, tracking-space, and display transforms. Auto-pick the
  tracking space from file metadata, with a manual override.
- Threaded prefetch and a frame cache that stores precomputed pyramid levels.

**Solver**
- Motion models: translation; translation + rotation + scale; affine;
  **perspective (8-DOF homography)**. Models are selectable, and switching
  model partway through a shot carries the solution over.
- ESM on SL(3) in a coarse-to-fine pyramid, with inverse-compositional as an
  alternative path for speed.
- Robust IRLS: Huber, Tukey, or Cauchy, with automatic scale from the MAD.
- Joint photometric model: global gain and bias per frame.
- Weighted mask input: the matte from the graph becomes per-pixel weights,
  with feathered edges.
- Motion prediction: constant velocity, with an optional Kalman filter.
  Configurable search range and maximum motion.
- **Reference strategy:** track frame-to-frame for robustness, then refine
  against a reference keyframe template to cancel drift. If the warped
  reference no longer matches, re-anchor to a new keyframe (template-update
  strategy).
- Forward and backward tracking from any frame.
- Pixel selection for speed: optionally use only pixels with strong gradient.
- Track at proxy resolution, then refine at full resolution.

**Diagnostics recorded from day one.** This is what makes Phase 5 cheap. For
every frame, store:
- final residual, ZNCC to the reference, inlier ratio (pixels with robust
  weight above a threshold);
- Hessian condition number and smallest eigenvalue (detects degenerate or
  aperture-problem texture);
- iterations used, whether it converged, and the pyramid level where cost
  jumped;
- visible fraction of the region (to detect the plane leaving frame);
- area or scale change and photometric gain and bias.

**Export v1**
- Nuke `CornerPin2D` text on the clipboard: animated `to1–to4`, static
  `from1–from4` at the reference frame, correct y-flip and format origin.
- Nuke `CornerPin2D` stabilize variant (the inverse pin), and Nuke `Tracker4`
  with 4 tracks.
- JSON with the full per-frame homography and diagnostics.

**Validation harness** (built now, used forever)
- A synthetic ground-truth generator: textures warped by known homography
  paths, with added grain, motion blur, lighting flicker, occluders, lens
  distortion, and planes partly leaving frame.
- Public planar benchmarks: POT-210 and UCSB/TMT-style sequences. The metric
  is mean corner alignment error in pixels.
- Regression suite in CI, with accuracy tracked per commit.

**Exit:** under 0.1 px mean corner error on clean synthetic shots. The CLI
tracks real plates and produces a CornerPin that pastes and lines up in Nuke.

---

### Phase 2: Workbench MVP (standalone app)

**Viewer**
- GPU viewer with OCIO display transform, exposure and gamma, channel
  isolation, and zoom/pan with a sub-pixel loupe.
- Timeline with cached-frame bar, in/out points, and keyframe markers.
- **Confidence lane:** a colored strip (green, amber, red) under the timeline
  built from the Phase 1 diagnostics, so weak frames are visible before
  anyone renders.

**Shapes**
- Bezier and X-spline shapes, per-point feather, and open/closed toggle.
- Keyframed shape animation, with per-point keys if needed.
- **Link to track:** a shape follows any track's transform, with an extra
  offset keyed on top for hand corrections.
- GPU rasterization with feather coverage.

**Node graph v1 and layer list**
- Nodes: `Source`, `Shape`, `Matte Merge` (union), `Matte Subtract`
  (holdout), `Matte Intersect`, `Invert`, `Grow/Shrink/Feather`, `Track`,
  `Surface` (the insert corners, kept separate from the tracked region),
  `Export`.
- The layer list is a simplified view of the same graph. Making a shape a
  holdout in the list creates a visible Subtract edge in the graph.

**Tracking controls**
- Track forward, back, step, or range.
- **Auto-pause** when confidence drops below a threshold, instead of silently
  continuing or silently stopping. The pause is resumable.

**Correction**
- **AdjustTrack:** nudge surface corners on chosen frames with the loupe. The
  corrections interpolate between those frames as a separate layer on top of
  the raw track.
- Curve editor for corner positions.
- Smoothing that removes jitter but keeps intended motion, applied in plane
  space.

**Preview and QC**
- **Grid mode:** a perspective grid drawn over the surface (density and color
  adjustable) to judge the track over the footage.
- **Insert preview:** a checkerboard, a numbered test card, or a chosen image
  pinned to the surface.
- **Stabilized view:** shows the plate warped so the plane is locked. Any
  drift appears as visible motion, which is the fastest QC available.
- **Reference flicker:** A/B or difference between the current frame warped
  to the reference and the reference keyframe.

**Project files**
- Save and load, autosave, crash recovery, unlimited undo, and relative media
  paths with relinking.

**Exit:** an artist can take a real shot from EXR or MOV through drawing,
tracking, correcting, and QC, then paste into Nuke, without the CLI.

---

### Phase 3: GPU acceleration (can run alongside Phase 2)

- Port the hot paths to CUDA and Metal using the approach chosen in Phase 0:
  pyramid build, warp, gradients, Jacobian, weighted reductions, matte
  rasterization, preprocessing filters, and viewer display transforms.
- **GPU-resident frame cache:** upload once, keep pyramids on the GPU, and
  evict by LRU.
- Hardware video decode: NVDEC (via FFmpeg `hwaccel cuda`) on NVIDIA, and
  VideoToolbox on Apple Silicon (hardware ProRes on M-series Pro, Max, and
  Ultra chips). EXR decode stays on the CPU, in parallel threads, with
  read-ahead.
- Overlap work asynchronously: decode frame N+2, upload N+1, and solve N.
- Several independent tracks solve at once on separate streams or queues.
- **Parity tests:** every GPU kernel is checked against the CPU reference
  within a set tolerance, in CI on both vendors.
- Targets (to confirm in Phase 0): 4K perspective track at or above
  real-time playback rate on a current RTX workstation and on M-series Max.

---

### Phase 4: OFX plugin and host integration

**Plugin node (Nuke and Flame)**
- Inputs: `Source` (required), `Insert` (optional), and one or more `Matte`
  inputs. Host roto from Nuke Roto or Flame GMask can feed straight into the
  tracker graph as a Matte node.
- Output modes:
  - corner-pin the Insert onto Source, with optional motion blur;
  - stabilize;
  - the tracked matte (rasterized shapes);
  - grid overlay;
  - pass-through.
- Surface corners are exposed as **animated host parameters**, so artists can
  link expressions to them in Nuke, or reference them from other Flame nodes.
- An "Open Tracker" button launches the workbench, which pulls frames from the
  host through temporal clip access and the shared-memory bridge.
- The project is stored inside the host script as an embedded, compressed
  parameter, so the comp is self-contained. A sidecar file is an option for
  very large projects.
- Viewer overlay in Nuke shows corners and grid, and Flame gets the same
  wherever its OFX implementation allows.

**Flame export.** Exact mechanism depends on the Phase 0 findings.
- Action **3D axis + camera**: homography decomposition using known focal
  length and film back, with continuity used to resolve the two-solution
  ambiguity.
- Action **surface corner pin**, using the perspective-correct mode.
- Flame-importable `.ascii` axis and corner files.
- A Flame Python hook that builds the Action nodes from the clipboard, if
  Flame doesn't accept pasted setup text natively.

**Packaging**
- `.ofx.bundle` layout for Windows, Linux, and macOS (universal or arm64).
- Code signing and macOS notarization.
- Installers, and a license check suited to render farms (no UI, floating
  licenses).

**Exit:** validated against the Phase 0 host matrix. Round-trip golden tests
confirm that pasted or exported corner pins line up to within 0.05 px of the
in-app result in both hosts.

---

### Phase 5: Diagnostics, the heat map, and "why did it slip?"

**Feasibility verdict: feasible, not a rabbit hole.** This holds because the
core is a direct IRLS solver (decision 1.2).

At the converged solution, the parameter update is

```
Δp = H⁻¹ · Σᵢ wᵢ Jᵢᵀ rᵢ,      H = Σᵢ wᵢ Jᵢᵀ Jᵢ
```

So each pixel's contribution vector is simply `cᵢ = H⁻¹ wᵢ Jᵢᵀ rᵢ`, and the
contributions sum exactly to the update. Projecting `cᵢ` onto a direction of
interest turns it into a signed per-pixel value that can be drawn as a
diverging heat map. Cost: one extra GPU pass per frame, run only when the map
is turned on.

**Heat map views (toggle in the viewer)**
1. **Contribution map, projected onto a chosen direction:**
   - the unexpected motion on this frame (solved motion minus predicted
     motion);
   - a single DOF: x, y, rotation, scale, or perspective;
   - **"this corner"**: click the corner that slipped, and the map shows
     which pixels pushed that corner in that direction (projection through
     the corner's Jacobian).
2. **Weight map:** which pixels the robust estimator ignored. These are
   effectively the occlusions it detected automatically.
3. **Residual map:** where the warped reference disagrees with the current
   frame.
4. **Coarse-level map:** the same contribution maps at the coarsest pyramid
   level, for gross failures where the solver fell into the wrong basin
   early.
5. **Accumulated mode:** contributions summed over the last N frames before
   the slip, to catch slow drift as well as sudden jumps.

Note on limits: these are first-order (linearized) explanations. They are
reliable for "what pulled it" near the solution. Gross convergence failures
are covered by the coarse-level map plus the slip analyzer.

**Slip analyzer**

When confidence drops, jump to the last good frame and show a ranked
diagnosis with one-click fixes:

| Detected cause | Signal | Suggested fix |
|---|---|---|
| Occluder entering | Cluster of low-weight pixels growing from an edge of the region | Proposes a holdout shape fitted to the cluster, optionally linked to its own track |
| Lighting change or flicker | Global residual up, gain/bias jumping, structure unchanged | Switch to bandpass or ZNCC mode (Phase 6) |
| Degenerate texture | Hessian condition number spike | Enlarge the region, add texture, or lower the motion model's DOF |
| Motion blur | High-frequency energy drops sharply | Enable blur-aware mode, or skip the frame and interpolate |
| Plane leaving frame | Visible fraction below threshold | Continue on the remaining area, or switch to feature-based re-acquisition |
| Motion too large | Not converged at coarse level | Increase pyramid levels or search range, or use feature initialization |
| Reflection or parallax | Contributions split into opposing groups | Hold out the offending area; points to a non-planar surface |

**Trackability preview (before tracking)**
- A structure-tensor texture map over the plate shows where there is
  trackable detail and where the aperture problem lurks (texture running in
  one direction only).
- **Best start frame suggestion:** the sharpest, most front-facing, least
  blurred frame within the selected range.

---

### Phase 6: Robustness and lighting-agnostic tracking

**Preprocessing nodes** (they chain in the graph before a Track node)
- **Bandpass:** Difference of Gaussians or a Laplacian-pyramid band, with an
  interactive frequency range.
- Local contrast normalization (per-neighborhood zero mean and unit
  variance).
- Gradient-orientation image and census/rank transform: strongly invariant
  to lighting.
- Channel, AOV, and custom-mix selection (for example, track only green, or
  a chroma channel).
- Temporal and spatial denoise for heavy grain.
- Clamp and highlight roll-off.

**Cost functions and photometric models** (selectable per Track node)
- SSD with gain and bias (default); spatially varying gain (low-order
  polynomial field) for shadows sweeping across a plane.
- **ZNCC / ECC** (Enhanced Correlation Coefficient), invariant to affine
  lighting changes.
- **Mutual information**, for extreme lighting or exposure changes and flashes.
- **Descriptor fields:** dense gradient-channel descriptors, robust to
  lighting while keeping direct-method accuracy.

**Keyer and matte nodes**
- Luma, chroma, and difference keys produce mattes automatically, for
  example to hold out a greenscreen area, skin, or anything that changes.
- **Matte from robust weights:** turn the solver's own occlusion detection
  into an editable matte.

**Re-acquisition and global accuracy**
- Feature-based initialization and recovery: ALIKED or DISK keypoints with
  LightGlue matching, verified with MAGSAC++. SIFT and ORB are the CPU
  fallback. This handles occlusion gaps, the plane leaving and re-entering
  frame, and whip pans.
- **Bidirectional merge:** track forward and backward, then blend the two
  solutions weighted by per-frame confidence.
- **Multi-keyframe anchoring**, plus a **global planar refinement pass**: a
  pose-graph or bundle-style optimization over all frame homographies on
  SL(3), constrained by every keyframe and smoothness priors. It removes the
  drift that accumulates over long shots.

**Optics**
- **Lens-distortion-aware tracking.** Undistort before tracking using
  ST-maps, 3DE4 lens models via `ldpk`, or Nuke LensDistortion data. Export
  pins in distorted or undistorted space. A planar track through an
  undistorted lens slips at the frame edges no matter how good the solver is.
- **Blur-aware tracking (ESM-blur style):** model motion blur from frame
  velocity so heavily blurred frames still align, and export matching blur
  parameters for the insert.
- Rolling shutter compensation: research item, gated on demand.

---

### Phase 7: Advanced and ML-assisted (pick à la carte)

- **SAM 2 segmentation nodes:** click the surface or the occluder, and the
  matte propagates through the shot. It becomes a Matte node that can be
  refined by hand. Inference via TensorRT on NVIDIA and Core ML on Mac.
- **Mesh (deformable) tracking:** a regularized mesh warp layered on the
  planar solution, for cloth, flags, curved panels, and skin.
- **Dense-flow priors** (RAFT-class optical flow) as a prediction for
  violent motion.
- **3D plane and camera export:** homography decomposition to a Nuke
  `Camera` + `Card` or a Flame 3D axis and camera, plus FBX and Alembic.
- **Roto export:** tracked shapes become Nuke Roto shapes or Flame GMask with
  keyframes. The tracker doubles as a roto accelerator.
- **Lighting curve export:** the solver's per-frame gain and bias drive a
  Nuke `Grade` or Flame color node, so the insert flickers with the plate.
- **Multi-plane fusion:** combine several co-planar tracks weighted by
  confidence. Supports parent/child tracks, where a child uses the parent as
  its prediction.
- Blender export (corner pin via a Python script), if there's demand.

---

### Phase 8: Pipeline and polish (ongoing from Phase 4)

- Python API (pybind11) covering the full graph: build, track, and export
  from scripts.
- Headless CLI for farm tracking and batch export, with exit codes and
  quality thresholds for automated QC.
- Keymap presets matching Nuke, Flame, and Mocha.
- Preferences, project templates (for example "screen replacement" or
  "wall insert with actor holdout"), and an onboarding sample project.
- Documentation and short task-based tutorials.
- Crash reporting (opt-in) and performance HUD.

---

## 4. Track node: exposed parameters

| Group | Parameter | Notes |
|---|---|---|
| Model | Motion model | Translation, Trans+Rot+Scale, Affine, **Perspective**, (Phase 7: Mesh) |
| | Reference strategy | Fixed keyframe, frame-to-frame, or **hybrid with re-anchor threshold** (default) |
| Search | Pyramid levels, max motion, prediction | Constant velocity, Kalman, or a parent track |
| Cost | Cost function | SSD+gain/bias, ZNCC/ECC, MI, descriptor fields |
| | Robust estimator and scale | Huber, Tukey, or Cauchy; auto (MAD) or manual |
| | Photometric model | None, global gain/bias, or spatial gain field |
| Input | Channel / preprocessing | Usually set by upstream nodes; inline shortcuts for common cases |
| | Pixel selection | All, strong-gradient only, or every Nth pixel |
| | Matte edge handling | Erode and feather the region edge to drop boundary pixels |
| Solve | Iterations, tolerance | Plus proxy-then-full-res refinement toggle |
| Safety | Confidence stop threshold | Auto-pause behavior; minimum visible fraction |
| Output | Bidirectional merge, smoothing | Smoothing applied in plane space |

Each parameter has a tooltip. A "Simple / Advanced" toggle hides most of this
by default.

---

## 5. The UX problem: which matte holds out what

**Problem:** in most trackers it isn't clear which matte holds out which
track, how to hold out more than one thing, or how a matte is tied to a track.

**Approach: explicit graph, visible provenance**

```
 [Read plate.%04d.exr] ─► [OCIO → ACEScct] ─► [Bandpass] ─────────────┐ image
                                                                      ▼
 [Shape: Wall] ─────────────────► [Subtract] ──────────────────► [Track: Wall] ─► [Surface: Poster] ─► [Export: Nuke CornerPin]
                                     ▲  ▲                          matte
 [Shape: Actor] ──(holdout)──────────┘  │
     ▲ transform                        │
 [Track: Actor]                         │
                                        │
 [Key: Greenscreen] ──(holdout)─────────┘
```

- **Holdouts are edges, not hidden checkboxes.** Holding out several things
  just means more inputs into Subtract. Order and logic are visible.
- **"Shape follows track" is an edge** from a Track's transform output into a
  Shape's transform input, so linking is never hidden.
- **Matte provenance view:** a viewer mode that colors each pixel by the node
  that included or excluded it, matching each node's color in the graph.
  Hovering a pixel reports, for example, "excluded by *Shape: Actor* via
  *Subtract* → feeds *Track: Wall*".
- **Per-track effective matte:** select any Track node to see the final
  weighted matte it actually uses, after all holdouts and feathering, on the
  current frame.
- **Consistent color per node** across graph, layer list, viewer outlines,
  and timeline lanes.
- **Layer list for speed:** quick setups never need the graph, and anything
  done in the list appears in the graph automatically.

---

## 6. Additional features artists may not ask for, but that save tracks

1. **Perceptual tracking space for linear EXRs** (decision 1.4). This
   prevents highlight-dominated solves.
2. **Stabilized view and reference flicker** as the main QC tools. Drift is
   obvious in a stabilized view long before it shows on a grid.
3. **Auto-pause with a reason**, instead of a silent stop (Phases 2 and 5).
4. **Trackability preview** and **best start frame suggestion**, so the track
   starts in the right place.
5. **Separate tracking region and insert surface:** track the whole wall, pin
   to the small poster. Larger regions give better solves.
6. **Global refinement pass**, which removes the slow drift that frame-to-frame
   tracking accumulates over long shots.
7. **Lens distortion awareness:** a planar track through a distorted lens is
   wrong at the edges no matter how good the solver is.
8. **EXR data window, overscan, and anamorphic PAR** handled correctly in
   every export. These are a classic source of 1–2 px offsets and squeezed
   pins.
9. **Frame numbering and timecode mapping** between MOV, EXR, and host time,
   so keys don't land one frame off.
10. **Partial visibility:** keep tracking on what remains when the plane
    leaves frame, and extrapolate corners from the solved homography.
11. **Lighting curve export:** reuse the solver's gain/bias estimate to match
    plate flicker on the insert.
12. **Roto from tracks:** the same shapes become keyframed roto in the host.
13. **Auto-holdout suggestions** generated from the robust weight map.
14. **Farm-friendly CLI with quality thresholds**, so batch tracking can flag
    weak shots automatically.

---

## 7. Cross-cutting engineering

- **Language and stack:** C++20 core; Qt 6 for the workbench (running in its
  own process, so no clash with Nuke's Qt); OFX SDK; OpenEXR, OIIO, OCIO v2,
  FFmpeg; Eigen for the small dense linear algebra; pybind11.
- **Build:** CMake with presets per platform; CI on Windows, Linux (Rocky,
  matching VFX Reference Platform), and macOS arm64. GPU CI runners for CUDA
  and Metal parity tests.
- **VFX Reference Platform alignment:** match the library versions each target
  Nuke and Flame release uses, to avoid symbol clashes inside the host.
- **Testing:** unit tests; the synthetic ground-truth suite; public
  benchmarks; golden-file tests for every exporter; and scripted host tests
  (Nuke `-t` Python, Flame Python) that paste and check alignment.
- **Determinism:** the same inputs and settings give the same track on the
  same backend; differences between CPU and GPU stay within a documented
  tolerance.

---

## 8. Risks and mitigations

| Risk | Impact | Mitigation |
|---|---|---|
| Flame OFX limits (overlays, temporal access, paste) | Weaker in-Flame experience | Phase 0 matrix; out-of-process UI; file-based export plus Python hook fallback |
| Nuke OFX limits for deep integration (native roto output, viewer handles) | Some features feel bolted on | Optional native Nuke NDK adapter later, sharing the same engine |
| Two GPU backends doubling kernel work | Slower delivery | Single-source kernels (Slang) and a CPU reference with parity tests |
| ML weight licenses | Commercial shipping blocked | Use permissive models only (SAM 2: Apache-2.0; LightGlue: Apache-2.0; ALIKED: BSD-3; DISK: Apache-2.0; RAFT: BSD-3). **Avoid** SuperPoint/SuperGlue weights and CC-BY-NC models such as CoTracker. Re-verify every license at integration time |
| FFmpeg licensing | Distribution constraints | LGPL build, dynamically linked, no GPL components |
| Scope creep in Phases 6 and 7 | v1 delayed | v1.0 = Phases 0–5; Phases 6 and 7 are prioritized using Phase 5 slip-analyzer data from real shots |
| Heat map turns out less intuitive than hoped | Feature underused | Cheap to build (solver data already exists); test early with artists on known-bad shots; the weight and residual maps are useful even alone |

---

## 9. Suggested immediate next steps

1. Run the Phase 0 spikes, starting with the **host capability matrix**,
   since the most decisions depend on it.
2. Collect a **test-shot library** of 15–30 real shots of the kinds that hurt:
   - lighting flicker, heavy grain, motion blur;
   - occluders crossing the plane, planes leaving frame;
   - low-texture walls, reflective screens;
   - anamorphic, overscan, and distorted-lens plates.

   Every phase is measured against it.
3. Write the **coordinate and time conventions** document and the project
   file schema before Phase 1 code.
4. Set up the repo skeleton (CMake, CI, core library and CLI targets) and the
   synthetic ground-truth generator.
