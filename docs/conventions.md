# Coordinate and Time Conventions

Every module and exporter follows this document. Exporters convert from these
internal conventions to each host's conventions, and each conversion has a
golden-file test.

## 1. Image coordinates (internal)

- **Origin:** the top-left corner of the **display window** (EXR) or of the
  frame (MOV).
- **Axes:** x to the right, **y down**.
- **Units:** pixels of the stored image, *not* squeezed or un-squeezed for
  pixel aspect ratio.
- **Pixel footprint:** pixel `(i, j)` covers `[i, i+1) × [j, j+1)`. Its centre
  is at `(i + 0.5, j + 0.5)`.

Why pixel edges and not pixel centres at integers: with edge-origin
coordinates, a 2× pyramid downsample is an exact scale by 0.5
(`x_level = x * 2^-level`) with no half-pixel offsets. This matches Nuke's and
OpenEXR's notion of a pixel's area.

### Data window vs display window (EXR)

Tracking images are always placed in display-window coordinates:

- pixels of the data window outside the display window (overscan) are
  currently cropped. Overscan support is planned: those pixels will keep their
  coordinates, which go negative or beyond the display size;
- areas of the display window not covered by the data window are filled with
  zero and marked invalid for sampling.

### Pixel aspect ratio

Coordinates stay in stored pixels. Pixel aspect (for example 2.0 for
anamorphic) is carried as metadata and used by:

- the solver's regularization and smoothing, so that "isotropic" means
  isotropic on screen;
- the 3D exports (homography decomposition needs square-pixel coordinates).

2D corner pins are exported in stored pixels, which is what Nuke's CornerPin2D
expects on an anamorphic format.

## 2. Homographies

- A track stores, per frame `t`, a 3×3 matrix `H_t` that maps **reference-frame
  image coordinates to frame-`t` image coordinates**: `p_t ~ H_t · p_ref`.
- `H_ref` is the identity.
- Stored normalized so that `H[2][2] = 1`, row-major in files.
- Solvers update homographies by right-composition with a local parameter
  exponential: `H ← H · N⁻¹ · exp(Σ θ_k B_k) · N`, where `N` normalizes the
  template region (centroid at the origin, unit spread) for conditioning.
- Pyramid level `l` uses `H_l = S · H · S⁻¹`, `S = diag(2^-l, 2^-l, 1)`.

## 3. Surface corners

The surface (the insert rectangle) has four corners, ordered as Nuke's
CornerPin2D orders them, **as seen upright on screen**:

| Index | Corner | Nuke knob |
|---|---|---|
| 1 | bottom-left | `to1` / `from1` |
| 2 | bottom-right | `to2` / `from2` |
| 3 | top-right | `to3` / `from3` |
| 4 | top-left | `to4` / `from4` |

Internally, with y down, "bottom" has the larger y.

## 4. Host conversions

| Target | Origin | y axis | Conversion from internal `(x, y)` |
|---|---|---|---|
| Nuke (CornerPin2D, Tracker4, Roto) | bottom-left of format | up | `(x, H − y)`, `H` = display height |
| Flame Action (2D) | centre of frame | up | `(x − W/2, H/2 − y)`; *to verify in the Phase 0 host matrix* |
| Mocha-style / OpenCV | top-left, pixel centres at integers | down | `(x − 0.5, y − 0.5)` |
| JSON (our own) | internal | down | none |

## 5. Time

- Internally a frame is identified by its **host frame number** (an integer).
- **EXR sequences:** the number in the file name (`plate.1001.exr` is frame
  1001). Gaps in a sequence are missing frames, not renumbered.
- **MOV:** decoded frame index `i` (0-based) maps to `start + i`. `start`
  defaults to **1**, matching Nuke's default for movie files. It can be set
  explicitly, or derived from the file's start timecode at its frame rate.
- The reference frame and every keyframe are stored as host frame numbers.
  When the plugin runs inside a host, the host's time is authoritative and the
  workbench shows host frame numbers.
- Frame rate and timecode are carried as metadata, never used to renumber
  frames silently.

## 6. Tracking colour space

Tracking runs on a single-channel image in a **tracking space**:

- Default for scene-linear sources (EXR): Rec.709 luminance, then the ACEScct
  curve. This stops highlights from dominating the solve.
- Default for display-referred sources (most MOV): Rec.709 luma, no curve.
- OCIO transforms replace these built-in curves once OCIO is integrated; the
  built-in curves remain as the fallback when no config is available.
