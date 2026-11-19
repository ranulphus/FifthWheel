#!/bin/sh
# shots.sh - DOS against Mesa: the same frames drawn by FWHEEL.EXE in 86Box
# (Loop A) and by the headless build (OSMesa), compared with the harness's
# imgcmp.py (RGB565, tolerance 24, 0.5% of pixels, colour edges excluded).
# The frames: the world's ten poses (-tourshots 10: along the tour and at
# two depots, no HUD), the first scene with its HUD (the job board, the
# minimap), a trailer reversing into a bay (-dockpose: the reversing
# camera, its guide lines) and a job's haul 40 s in (-autojob: the route's
# chevrons, the shadows, the HUD on the road). Needs MGAHAL and DEV (make shots sets them).
#
#   tools/shots.sh [CARD]    (default g450; make shots CARD=...)
set -eu
card=${1:-g450}
: "${MGAHAL:?}" "${DEV:?}"
dos=out/shots-$card
hl=out/shots-headless
rm -rf $dos $hl
mkdir -p $dos $hl/out

# DOS, one boot per set of frames; the guest's C:\OUT comes back as files/.
# The runs log nothing while they draw (86Box draws the haul at about 10
# frames a second), so the harness waits 300 s of silence, not 60.
for name in tour hud dock haul; do
    case $name in
    tour) args="-tourshots 10" ;;
    hud) args="-frames 61 -shot 60:H0" ;;
    dock) args="-dockpose -frames 121 -shot 120:H1" ;;
    haul) args="-autojob -frames 2401 -shot 2400:H2" ;;
    esac
    "$DEV" python3 "$MGAHAL/tools/loopa/run.py" --name "fwshots-$name" --card "$card" \
        --exe build/dos/FWHEEL.EXE --file build/data/WORLD.PAK --args="-test -fixed -nosound $args" \
        --out "$PWD/$dos/$name" --timeout 900 --idle 300 > /dev/null || true
    # The haul is cut short, so its job's own check fails; its picture is what counts.
    status=$(cat $dos/$name/status 2>/dev/null || echo none)
    if [ $name = haul ] && grep -q 'HX-TEST snapshot PASS H2' $dos/$name/serial.log 2>/dev/null; then
        status="PASS (snapshot; the job is cut short)"
    fi
    echo "shots: DOS $name: $status"
    # The same frames headless.
    "$DEV" sh -c "cd $hl && LP_NUM_THREADS=2 ../../build/headless/fwheel-hl -world ../../build/data/WORLD.PAK \
        -test -fixed -nosound $args > $name.log 2>&1" || true
done

fail=0
for got in $dos/*/files/*.PPM; do
    [ -e "$got" ] || { echo "shots: no DOS frames"; exit 1; }
    base=$(basename "$got" .PPM)
    lower=$(echo "$base" | tr A-Z a-z)
    ref=$hl/out/$base.ppm
    if [ ! -e "$ref" ]; then
        echo "shots: $base: no headless frame"
        fail=1
        continue
    fi
    # imgcmp reads PNGs: Loop A made the DOS one; the harness's png module makes ours.
    "$DEV" python3 -c "import sys; sys.path.insert(0, '$MGAHAL/tools/loopa'); import png; \
w, h, rgb = png.read_ppm('$ref'); png.write_png('$hl/$lower.png', w, h, rgb)"
    if "$DEV" python3 "$MGAHAL/tools/imgcmp.py" "$hl/$lower.png" "$(dirname $(dirname $got))/$lower.png" \
        --diff "$dos/$lower-diff.png" > "$dos/$lower.cmp" 2>&1; then
        echo "shots: $base PASS $(tail -1 $dos/$lower.cmp)"
    else
        echo "shots: $base FAIL $(tail -1 $dos/$lower.cmp)"
        fail=1
    fi
done
# Every frame the sets ask for: a run that hung or failed early leaves its frames out.
for want in P0 P1 P2 P3 P4 P5 P6 P7 P8 P9 H0 H1 H2; do
    if ! ls $dos/*/files/$want.PPM > /dev/null 2>&1; then
        echo "shots: $want: no DOS frame"
        fail=1
    fi
done
exit $fail
