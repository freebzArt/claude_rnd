"""Check a ptrack CornerPin2D export inside Nuke.

Run with Nuke's terminal mode (no UI needed):

    nuke -t tools/nuke/check_cornerpin.py TRACK.json PIN.nk
    nuke -t tools/nuke/check_cornerpin.py TRACK.json PIN.nk --plate "shot/plate.####.exr" --out qc/

What it checks:

1. Paste: PIN.nk (from `ptrack track --nuke FILE`) pastes into a script and
   creates exactly one CornerPin2D.
2. Numbers: on every tracked frame, Nuke's to1..to4 values equal the track's
   corners converted to Nuke coordinates (y up from the bottom of the format),
   and from1..from4 equal the reference-frame surface. This proves the curve
   syntax, frame numbering and y flip are right.
3. Pictures (with --plate): renders two JPEG sequences to look at:
   - qc.####.jpg: a checkerboard pinned onto the surface, half-transparent
     over the plate. It should stick to the surface with no swimming.
   - stab.####.jpg: the plate stabilised by the inverted pin. The surface
     should stand still; any drift shows up as motion.

Exit code 0 when the numeric check passes, 1 otherwise.
"""

import json
import os
import sys

import nuke  # noqa: E402  (only available inside Nuke)


TOLERANCE = 1e-3  # pixels; the export writes 4 decimals


def parse_args(argv):
    if len(argv) < 2:
        print(__doc__)
        sys.exit(2)
    args = {"track": argv[0], "pin": argv[1], "plate": None, "out": "ptrack_qc"}
    rest = argv[2:]
    while rest:
        key = rest.pop(0)
        if key in ("--plate", "--out") and rest:
            args[key[2:]] = rest.pop(0)
        else:
            print("unknown argument: %s" % key)
            sys.exit(2)
    return args


def set_format(width, height, pixel_aspect):
    name = "ptrack_check_%dx%d" % (width, height)
    nuke.addFormat("%d %d %g %s" % (width, height, pixel_aspect, name))
    nuke.root()["format"].setValue(name)


def paste_pin(path):
    before = set(n.name() for n in nuke.allNodes())
    nuke.nodePaste(path)
    pins = [n for n in nuke.allNodes("CornerPin2D") if n.name() not in before]
    if len(pins) != 1:
        raise RuntimeError("expected one CornerPin2D from %s, got %d" % (path, len(pins)))
    return pins[0]


def check_numbers(pin, track):
    height = track["format"]["height"]
    worst = 0.0
    worst_where = ""
    for f in track["frames"]:
        frame = f["frame"]
        for i, (x, y) in enumerate(f["corners"]):
            knob = pin["to%d" % (i + 1)]
            nx, ny = knob.valueAt(frame, 0), knob.valueAt(frame, 1)
            err = max(abs(nx - x), abs(ny - (height - y)))
            if err > worst:
                worst, worst_where = err, "to%d at frame %d" % (i + 1, frame)
    for i, (x, y) in enumerate(track["surface"]):
        knob = pin["from%d" % (i + 1)]
        err = max(abs(knob.value(0) - x), abs(knob.value(1) - (height - y)))
        if err > worst:
            worst, worst_where = err, "from%d" % (i + 1)
    return worst, worst_where


def render_qc(pin, track, plate, out_dir):
    first = track["frames"][0]["frame"]
    last = track["frames"][-1]["frame"]
    w, h = track["format"]["width"], track["format"]["height"]
    if not os.path.isdir(out_dir):
        os.makedirs(out_dir)

    read = nuke.nodes.Read(file=plate, first=first, last=last, origfirst=first, origlast=last)
    read["raw"].setValue(True)  # no colour conversion: this is a geometry check

    # Checkerboard squeezed onto the reference-frame surface, then tracked.
    checker = nuke.nodes.CheckerBoard2()
    to_surface = nuke.nodes.CornerPin2D(inputs=[checker])
    rect = [(0, 0), (w, 0), (w, h), (0, h)]
    for i in range(4):
        to_surface["from%d" % (i + 1)].setValue(list(rect[i]))
        to_surface["to%d" % (i + 1)].setValue(list(pin["from%d" % (i + 1)].value()))
    if "black_outside" in to_surface.knobs():
        to_surface["black_outside"].setValue(True)
    pin.setInput(0, to_surface)
    over = nuke.nodes.Merge2(inputs=[read, pin], operation="over", mix=0.5)
    qc = nuke.nodes.Write(inputs=[over], file=os.path.join(out_dir, "qc.####.jpg"), file_type="jpeg")
    qc["raw"].setValue(True)

    # Stabilise: the same pin inverted, applied to the plate.
    stab_pin = nuke.nodes.CornerPin2D(inputs=[read])
    for knob in ["to1", "to2", "to3", "to4", "from1", "from2", "from3", "from4"]:
        stab_pin[knob].fromScript(pin[knob].toScript())
    stab_pin["invert"].setValue(True)
    stab = nuke.nodes.Write(inputs=[stab_pin], file=os.path.join(out_dir, "stab.####.jpg"), file_type="jpeg")
    stab["raw"].setValue(True)

    nuke.execute(qc, first, last)
    nuke.execute(stab, first, last)
    nuke.scriptSaveAs(os.path.join(out_dir, "ptrack_qc.nk"), overwrite=1)
    print("Rendered %s and %s (frames %d-%d), script ptrack_qc.nk" % (
        os.path.join(out_dir, "qc.####.jpg"), os.path.join(out_dir, "stab.####.jpg"), first, last))


def main():
    args = parse_args(sys.argv[1:])
    with open(args["track"]) as fh:
        track = json.load(fh)
    fmt = track["format"]
    set_format(fmt["width"], fmt["height"], fmt.get("pixelAspect", 1.0))

    pin = paste_pin(args["pin"])
    worst, where = check_numbers(pin, track)
    ok = worst <= TOLERANCE
    print("Nuke %s: CornerPin2D '%s', %d frames, largest difference %.5f px (%s): %s" % (
        nuke.NUKE_VERSION_STRING, pin.name(), len(track["frames"]), worst, where or "-",
        "PASS" if ok else "FAIL"))

    if args["plate"]:
        render_qc(pin, track, args["plate"], args["out"])
    sys.exit(0 if ok else 1)


main()
