# Planar Tracker (R&D)

A high-end planar tracker for VFX, planned as an OFX plugin for Nuke and Flame,
with a node-based standalone workbench and GPU acceleration (CUDA / Metal).
The working name in code is **ptrack**.

- [ROADMAP.md](ROADMAP.md): the phased plan.
- [Live roadmap with progress](https://claude.ai/artifact/Li6b9g6rpskSkaV7uBQngZ)
- [docs/conventions.md](docs/conventions.md): coordinates, corners, time and colour.
- [docs/project-format.md](docs/project-format.md): project and track file formats.

## Status

Phase 1 (core engine) is done apart from checks that need Nuke; see
[docs/nuke-check.md](docs/nuke-check.md). What works:

- **ESM planar solver** on a coarse-to-fine pyramid with translation,
  similarity, affine and perspective models. Uses robust IRLS (Tukey with Huber
  warm-up) and joint gain/bias for lighting.
- **Tracking strategies:** keyframe, frame-to-frame and hybrid (default).
  - Hybrid re-anchors to new keyframes when the reference stops matching.
  - It falls back to a lighting-normalised reference check when shadows or
    exposure shifts break the plain match.
  - Pixels rejected as occluded stay out of the next frame's template.
- **Auto-pause with a reason** instead of silent slips, with per-frame
  diagnostics. Per-pixel contributions to each solver step are recorded, the
  basis of the Phase 5 heat map.
- **Lighting-agnostic modes:** band-pass and local-contrast normalisation,
  applied per pyramid level (`--normalize`).
- **Input:**
  - OpenEXR sequences: layers, parts, overscan (`--overscan`), pixel aspect.
  - Movies through FFmpeg (ProRes, DNxHR, H.264, HEVC), numbered from
    `--start` or timecode.
  - Read-ahead frame cache.
- **Colour:** OCIO tracking-space conversion (`--ocio --from --to`), or the
  built-in ACEScct curve for scene-linear plates.
- **Export:** Nuke CornerPin2D (track and stabilize) to a file, stdout or the
  clipboard, plus JSON.
- **Testing:** synthetic ground-truth shots and an accuracy harness. CI on
  Linux, macOS arm64 and Windows.

Accuracy on the synthetic harness (640×360, mean surface-corner error, default
settings):

| Shot | Error |
|---|---|
| Clean | 0.004 px |
| Grain (σ = 0.02) | 0.03 px |
| Lighting flicker (±20%) | 0.003 px |
| Occluder crossing the plane | 0.003 px |
| Shadow edge sweeping across the whole plane | 0.14 px |
| 2× anamorphic, similarity model | 0.002 px |
| ProRes 422 HQ / H.264 versions of a clean shot | 0.02 / 0.04 px |
| Stress: occluder already in the reference, grain and flicker, no holdout | 0.09 px (worst frame 1.9 px) |

Known hard cases:
- **Surface content that changes,** such as a screen playing video:
  0.7 px mean, 5 px worst. Hold the content out.
- **An occluder baked into the reference frame:** about 2 px on the first
  frame after it. A holdout fixes it.

## Build

Dependencies: CMake 3.24+, a C++20 compiler, Eigen 3.4+, OpenEXR 3, nlohmann-json
and doctest; FFmpeg and OpenColorIO 2 for movies and OCIO (both optional:
`-DPTRACK_WITH_FFMPEG=OFF`, `-DPTRACK_WITH_OCIO=OFF`).

```sh
# Ubuntu
sudo apt-get install ninja-build pkg-config libeigen3-dev libopenexr-dev nlohmann-json3-dev doctest-dev \
    libavformat-dev libavcodec-dev libswscale-dev ffmpeg libopencolorio-dev
# macOS
brew install ninja pkg-config eigen openexr nlohmann-json doctest ffmpeg opencolorio

cmake --preset release
cmake --build --preset release
ctest --preset release
```

On Windows, use the `windows-vcpkg` preset (dependencies come from `vcpkg.json`;
movie and OCIO support are off there for now).

## Try it

```sh
B=build/release/cli/ptrack
$B synth --out /tmp/shot --frames 40 --noise 0.01 --occluder   # prints the track command
$B track --seq /tmp/shot/plate.####.exr --ref 1001 --mask /tmp/shot/mask.exr \
         --out /tmp/shot/track.json --nuke - --clipboard
$B eval --track /tmp/shot/track.json --truth /tmp/shot/truth.json
```

On your own plates, give the region as a quad instead of a mask. Corners go
bottom-left, bottom-right, top-right, top-left, copied from Nuke's viewer with
`--coords nuke`:

```sh
$B track --seq plate.%04d.exr --ref 1001 --range 1001-1100 \
         --quad 812,240,1300,250,1290,610,820,600 --coords nuke --clipboard
```

## Layout

```
core/   tracking engine (no I/O or UI dependencies)
io/     file formats: OpenEXR, movies (FFmpeg), OCIO, sequences, track JSON
cli/    ptrack command-line tool
tests/  unit, accuracy and end-to-end tests
docs/   conventions, file formats, Nuke check, roadmap page
tools/  host-side scripts (Nuke check)
```
