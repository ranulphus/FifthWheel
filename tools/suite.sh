#!/usr/bin/env bash
# suite.sh - Fifth Wheel's Loop A suites on one card: each check's outcome on
# one line, exit status 0 when all pass. Needs MGAHAL and DEV (make suite
# sets them) and build/dos/FWHEEL.EXE.
#
#   tools/suite.sh [CARD] [CHECK...]    checks: sb16 sbpro joy shop fx (default: all)
#
#   sb16, sbpro   -soundtest through the Sound Blaster 16 and the Sound
#                 Blaster Pro 2 (8-bit): every sound's tones are in the
#                 recording, and the driver played no chunk as silence (SDL
#                 patch 0005; a recording's gaps cannot show that, since
#                 86Box does not keep pace with the recorder)
#   joy           86Box's virtual joystick (MGA-Glide's patch 0105): -calibrate
#                 sees it steered left and right, accelerated (forward) and
#                 braked (back), and saves FWHEEL.CFG; then a run that loads
#                 it maps half left, full forward and button 1 to steering
#                 0.4-0.5, the accelerator and the handbrake (-joylog)
#   shop          keys typed in 86Box: the garage (G) buys Sky Blue paint and
#                 the Big Air horn with $600, is left (Esc) and the game quit;
#                 a second run loads CAREER.DAT with them fitted and $50 left
#   fx            -fxtest: every flourish far past its pool, drawn through
#                 DOS-GL; the pools hold (16 coins, 64 confetti, 32 dust)
set -uo pipefail
: "${MGAHAL:?}" "${DEV:?}"
card=${1:-g450}; shift || true
checks=${*:-sb16 sbpro joy shop fx}
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

joy() {
    local st j keys
    keys="@FW-CAL left,0.5:joy:axis:0:-32767,1.5:joy:axis:0:0,@FW-CAL right,0.5:joy:axis:0:32767,1.5:joy:axis:0:0"
    keys+=",@FW-CAL accel,0.5:joy:axis:1:-32767,1.5:joy:axis:1:0,@FW-CAL brake,0.5:joy:axis:1:32767,1.5:joy:axis:1:0"
    # SDL's gameport driver learns each axis's range as it moves: full left first, then half.
    keys+=",@FW-JOYLOG ready,0.5:joy:axis:0:-32767,1.5:joy:axis:0:0,2.5:joy:axis:0:-16000,2.5:joy:axis:1:-32767"
    keys+=",2.5:joy:button:0:1"
    st=$(run joy --cmd "FWHEEL -test -nosound -calibrate -noexit" --cmd "FWHEEL -test -nosound -joylog -frames 600" \
         --joystick 4axis_4button --keys "$keys")
    j=$(log joy | grep -a 'FW-JOY axes' | tail -1)
    if [ "$st" = PASS ] && log joy | grep -q "HX-TEST calibrate PASS" && log joy | grep -q "FW-CFG loaded" \
       && [[ $j =~ buttons\ 1.*steer\ 0\.4[0-9]\ accel\ 1\.00\ brake\ 0\.00 ]]; then
        say joy "PASS (calibrated, saved, loaded; ${j#*buttons })"
    else bad joy "($st; $(log joy | grep -a 'HX-TEST calibrate\|FW-CAL saved\|FW-CFG loaded' | tr '\n' ' ') last: $j)"; fi
}

shop() {
    local st keys l
    # 86Box scancodes: G 0x22, Enter 0x1c, Esc 0x01; extended Right 0x14d, Down 0x150.
    keys="@FW-PLAY ready,1:0x22,2.5:0x14d,3.5:0x1c,4.5:0x150,5.5:0x14d,6.5:0x1c,7.5:0x01,9:0x01"
    st=$(run shop --cmd "FWHEEL -test -nosound -career CAREER.DAT -money 600 -noexit" \
         --cmd "FWHEEL -test -nosound -career CAREER.DAT -frames 30" --keys "$keys")
    l=$(log shop | grep -a 'FW-CAREER loaded' | tail -1)
    if [ "$st" = PASS ] && log shop | grep -q "FW-MENU buy SKY BLUE" && log shop | grep -q "FW-MENU buy BIG AIR" \
       && [[ $l == *"money 50,"*"paint SKY BLUE, horn BIG AIR"* ]]; then
        say shop "PASS (bought two, saved, loaded: ${l#*: })"
    else bad shop "($st; $(log shop | grep -a 'FW-MENU buy\|FW-MENU short\|FW-CAREER' | tr '\n' ' '))"; fi
}

for c in $checks; do
    case $c in
    joy) joy ;;
    shop) shop ;;
    fx)
        st=$(run fx --args="-test -fixed -nosound -fxtest")
        r=$(log fx | grep -ao 'HX-TEST fx-caps [A-Z]* .*' | cut -d' ' -f3-)
        if [ "$st" = PASS ] && [[ $r == PASS* ]]; then say fx "PASS (${r#PASS })"; else bad fx "($st; $r)"; fi ;;
    sb16) sound sb16 sb16 "A220 I5 D1 H5 T6" ;;
    sbpro) sound sbpro sbprov2 "A220 I7 D1 T4" ;;   # 86Box's SB Pro 2 is on IRQ 7
    *) bad "$c" "(no such check)" ;;
    esac
done
exit $fail
