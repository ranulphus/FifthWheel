#!/usr/bin/env bash
# suite.sh - Fifth Wheel's Loop A suites on one card: each check's outcome on
# one line, exit status 0 when all pass. Needs MGAHAL and DEV (make suite
# sets them) and build/dos/FWHEEL.EXE.
#
#   tools/suite.sh [CARD] [CHECK...]    checks: sb16 sbpro (default: all)
#
#   sb16, sbpro   -soundtest through the Sound Blaster 16 and the Sound
#                 Blaster Pro 2 (8-bit): every sound's tones are in the
#                 recording, and the driver played no chunk as silence (SDL
#                 patch 0005; a recording's gaps cannot show that, since
#                 86Box does not keep pace with the recorder)
set -uo pipefail
: "${MGAHAL:?}" "${DEV:?}"
card=${1:-g450}; shift || true
checks=${*:-sb16 sbpro}
out=$PWD/out/suite-$card
mkdir -p "$out"
fail=0

say() { printf '  %-7s %s\n' "$1" "$2"; }
bad() { say "$1" "FAIL $2"; fail=1; }
run() {  # NAME [run.py options...]: the status
    local name=$1; shift
    "$DEV" python3 "$MGAHAL/tools/loopa/run.py" --name "fw-$name" --card "$card" --exe build/dos/FWHEEL.EXE \
        --file build/data/WORLD.PAK --out "$out/$name" --idle 90 --timeout 900 "$@" > /dev/null 2>&1
    cat "$out/$name/status" 2>/dev/null || echo NO-STATUS
}
log() { cat "$out/$1/serial.log" 2>/dev/null; }

sound() {  # NAME SNDCARD BLASTER
    local st w u
    st=$(run "$1" --args="-test -fixed -soundtest" --sound "$2" --pre "SET BLASTER=$3" --wav)
    w=$("$DEV" python3 "$MGAHAL/tools/loopa/wavcheck.py" "$out/$1/audio.wav" --tone 440 --tone 329 --tone 416 \
        --tone 1002 --tone 784 --tone 1900 2>&1)
    local wst=$?
    u=$(log "$1" | grep -o 'HX-TEST audio [A-Z]* .*' | cut -d' ' -f3-)
    if [ "$st" = PASS ] && [ $wst = 0 ]; then say "$1" "PASS (tones found; audio $u)"
    else bad "$1" "($st; audio ${u:-not reported}; $(echo "$w" | grep -c missing) tones missing)"; fi
}

for c in $checks; do
    case $c in
    sb16) sound sb16 sb16 "A220 I5 D1 H5 T6" ;;
    sbpro) sound sbpro sbprov2 "A220 I7 D1 T4" ;;   # 86Box's SB Pro 2 is on IRQ 7
    *) bad "$c" "(no such check)" ;;
    esac
done
exit $fail
