# Checking ptrack in Nuke

This is the Phase 1 exit check: tracks from `ptrack` must paste into Nuke and
line up. It needs a machine with Nuke and some real plates, so it runs on your
workstation rather than in the cloud session. Each step says what "good" looks
like and what to send back if it isn't.

## 0. Build on the workstation

```sh
git clone https://github.com/freebzArt/claude_rnd.git && cd claude_rnd
git checkout claude/loving-ritchie-5goz2t
# Linux:  sudo apt-get install ninja-build pkg-config libeigen3-dev libopenexr-dev nlohmann-json3-dev \
#           doctest-dev libavformat-dev libavcodec-dev libswscale-dev ffmpeg libopencolorio-dev
# macOS:  brew install ninja pkg-config eigen openexr nlohmann-json doctest ffmpeg opencolorio
cmake --preset release && cmake --build --preset release && ctest --preset release
export PT=$PWD/build/release/cli/ptrack
```

On Rocky/RHEL-style Linux the package names differ; if a dependency is
missing, Claude can adapt the build when the session runs on that machine.

## 1. Synthetic shot through Nuke (5 minutes)

Proves the export format, frame numbering and y flip with known ground truth.

```sh
$PT synth --out /tmp/ptk --frames 40 --noise 0.01 --occluder
$PT track --seq /tmp/ptk/plate.####.exr --mask /tmp/ptk/mask.exr \
          --out /tmp/ptk/track.json --nuke /tmp/ptk/pin.nk
$PT eval --track /tmp/ptk/track.json --truth /tmp/ptk/truth.json   # expect well under 0.1 px
nuke -t tools/nuke/check_cornerpin.py /tmp/ptk/track.json /tmp/ptk/pin.nk \
        --plate "/tmp/ptk/plate.####.exr" --out /tmp/ptk/qc
```

Good:
- The script prints `PASS` with a largest difference under 0.001 px.
- `/tmp/ptk/qc/qc.####.jpg`: the checkerboard sits on the plane and doesn't swim.
- `/tmp/ptk/qc/stab.####.jpg`: the plane stands still.

## 2. Paste by hand (2 minutes)

```sh
$PT nuke --track /tmp/ptk/track.json --clipboard
```

In Nuke, read the plate, select the Read node and paste (Ctrl/Cmd+V) into the
node graph. A `PlanarTrack` CornerPin2D should appear connected under it.
Scrub: its corners in the viewer should follow the plane's corners.

## 3. Your real plates (the part that matters)

Pick 3-5 shots: one easy, then the kinds that hurt (grain, motion blur,
occluders, lighting changes, anamorphic, overscan). For each:

1. In Nuke, note the reference frame and the four corners of the area to track
   from the viewer (bottom-left, bottom-right, top-right, top-left).
2. Track:
   ```sh
   $PT track --seq /path/plate.%04d.exr --ref 1001 --range 1001-1100 \
             --quad X1,Y1,X2,Y2,X3,Y3,X4,Y4 --coords nuke \
             --out shotname.json --nuke shotname.nk
   ```
   Add `--overscan` for plates with data outside the format, `--ocio CONFIG
   --from scene_linear --to compositing_log` to use the show's config, and
   `--normalize local-contrast` for heavy lighting changes.
3. Run the check script with `--plate` and look at `qc` and `stab`.

Send back for each shot: the command line, what ptrack printed (frames tracked,
any pause and its reason), the PASS/FAIL line, and a sentence on how qc/stab
look (sticks, swims, drifts after frame N). The shot itself isn't needed unless
something fails; then a few frames around the failure help most.

## 4. Still to verify with Nuke

- **Tracker4 export.** Not written yet: its paste format needs a real Nuke to
  verify. The first session on your machine adds it.
- **Host capability matrix** (roadmap Phase 0): what Nuke's OFX support allows
  (temporal clip access, overlays, animated parameters, launching a process).
  Needs the OFX SDK and a small probe plugin built against your Nuke version.
- **Flame:** the same checks for Flame Action export, once that exporter exists.
