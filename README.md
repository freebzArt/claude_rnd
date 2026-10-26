# Planar Tracker (R&D)

A high-end planar tracker for VFX, planned as an OFX plugin for Nuke and Flame,
with a node-based standalone workbench and GPU acceleration (CUDA / Metal).
The working name in code is **ptrack**.

- [ROADMAP.md](ROADMAP.md): the phased plan.
- [Live roadmap with progress](https://claude.ai/artifact/Li6b9g6rpskSkaV7uBQngZ)
- [docs/conventions.md](docs/conventions.md): coordinates, corners, time and colour.
- [docs/project-format.md](docs/project-format.md): project and track file formats.

## Status

Phase 0 (foundations) and the start of Phase 1 (core engine). What works today:

- **ESM planar solver** on a coarse-to-fine pyramid with translation, similarity,
  affine and perspective models, robust IRLS (Tukey with Huber warm-up by default),
  and joint gain/bias for lighting changes.
- **Tracking strategies:** keyframe, frame-to-frame and hybrid. In
  frame-to-frame and hybrid, pixels rejected as occluded on one frame stay out of
  the next frame's template.
- **Auto-pause with a reason** (match quality floor, sudden quality drop,
  rejected matte, region leaving frame), plus per-frame diagnostics.
- **Per-pixel contributions** to each solver step, the basis of the Phase 5 heat map.
- **OpenEXR** reading (layers, parts, data/display windows) with an ACEScct
  tracking curve for scene-linear plates.
- **Nuke CornerPin2D** export (track and stabilize) to a file, stdout or the clipboard.
- **Synthetic ground-truth shots** and an accuracy harness.

Accuracy on the synthetic harness (640×360, mean surface-corner error):

| Shot | Hybrid |
|---|---|
| Clean | 0.004 px |
| Grain (σ = 0.02) | 0.033 px |
| Lighting flicker (±20%) | 0.003 px |
| Occluder crossing the plane | 0.003 px |
| Stress: occluder already in the reference, grain and flicker, no holdout | 0.08 px (worst frame 1.8 px) |

The stress shot's worst frame is the first one after the reference, where the
occluder is part of the template. A holdout removes it. Keyframe-only mode
pauses on that shot instead of slipping.

## Build

Dependencies: CMake 3.24+, a C++20 compiler, Eigen 3.4, OpenEXR 3, nlohmann-json
and doctest.

```sh
# Ubuntu
sudo apt-get install ninja-build libeigen3-dev libopenexr-dev nlohmann-json3-dev doctest-dev
# macOS
brew install ninja eigen openexr nlohmann-json doctest

cmake --preset release
cmake --build --preset release
ctest --preset release
```

On Windows, use the `windows-vcpkg` preset (dependencies come from `vcpkg.json`).

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
io/     file formats: OpenEXR, sequences, track JSON (MOV next)
cli/    ptrack command-line tool
tests/  unit, accuracy and end-to-end tests
docs/   conventions, file formats, roadmap page
```
