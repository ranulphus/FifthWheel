#!/usr/bin/env bash
# suite.sh - Fifth Wheel's Loop A suites on one card: each check's outcome on
# one line, exit status 0 when all pass. Needs MGAHAL and DEV (make suite
# sets them) and build/dos/FWHEEL.EXE.
#
#   tools/suite.sh [CARD] [CHECK...]
#       checks: sb16 sbpro joy shop fx menus exit dglexit crash ctrlc mem modes auto (default: all)
#
# Every run fails on a DGL-STUB (a GL call DOS-GL lacks) or DGL-GLERR line.
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
#                 the Big Air horn with $600, is left (Esc) and the game quit
#                 from the pause menu;
#                 a second run loads CAREER.DAT with them fitted and $50 left
#   fx            -fxtest: every flourish far past its pool, drawn through
#                 DOS-GL; the pools hold (the detail preset's caps)
#   menus         keys typed in 86Box: the title's options set the detail to
#                 high, the game is driven, paused and quit from the pause
#                 menu; a second run starts with FWHEEL.CFG's detail high
#
# Leaving the machine usable (each with the SB16 playing; afterwards SBCHK
# finds its DMA stopped, VECCHK the interrupt vectors as they were before,
# and KEYWAIT reads a key through the BIOS):
#   exit          QUIT on the title screen
#   dglexit       DOS-GL's DGL_EXIT_AFTER: exit() inside a swap
#   crash         FW_CRASH: a #UD at tick 90 (DGL-FAULT, text mode); a career
#                 saved before it loads afterwards as it was
#   ctrlc         Ctrl-C while driving: SDL makes it a quit, and the game
#                 ends as from the menu (the career saved)
#   ctrlbreak     the same with Ctrl-Break
#   mem           86Box with 32 MB: the tour runs, and FW-MEM's heap stays
#                 within 20 MB
#   modes         every screen size the game offers (FW-MODES): a frame each,
#                 compared with the headless build's at the same size
#   release       dist/fwheel-*.zip (the newest; make release) unpacked alone in
#                 C:\FW and run from there with C:\HX (the harness's
#                 CWSDPMI) off the PATH: the ZIP's own CWSDPMI starts it
#   auto          the tour's state hashes (FW-HASH, 1,800 ticks): the same on
#                 every card (compared with any card's out/suite-*/auto)
set -uo pipefail
: "${MGAHAL:?}" "${DEV:?}"
card=${1:-g450}; shift || true
checks=${*:-sb16 sbpro joy shop fx menus exit dglexit crash ctrlc ctrlbreak mem modes auto release}
out=$PWD/out/suite-$card
mkdir -p "$out"
fail=0

say() { printf '  %-9s %s\n' "$1" "$2"; }
bad() { say "$1" "FAIL $2"; fail=1; }
run() {  # NAME [run.py options...]: the status
    local name=$1; shift
    "$DEV" python3 "$MGAHAL/tools/loopa/run.py" --name "fw-$name" --card "$card" --exe build/dos/FWHEEL.EXE \
        --file build/data/WORLD.PAK --out "$out/$name" --idle 90 --timeout 900 "$@" > /dev/null 2>&1
    if grep -aq "DGL-STUB\|DGL-GLERR" "$out/$name/serial.log" 2>/dev/null; then
        echo "GL-ERROR($(grep -ao 'DGL-STUB [a-zA-Z0-9]*\|DGL-GLERR 0x[0-9a-f]*' "$out/$name/serial.log" | head -1))"
    else
        cat "$out/$name/status" 2>/dev/null || echo NO-STATUS
    fi
}
blaster="SET BLASTER=A220 I5 D1 H5 T6"
# The machine after a program: the SB silent, the vectors as saved, the keyboard the BIOS's.
after=(--cmd "SBCHK" --cmd "VECCHK check" --cmd "KEYWAIT 20")
after_ok() { log "$1" | grep -aq "HX-SBCHK .* stopped" && log "$1" | grep -aq "HX-VECCHK ok" &&
             log "$1" | grep -aq "HX-KEY scan=39"; }
after_why() { log "$1" | grep -ah "HX-SBCHK\|HX-VECCHK changed\|HX-KEY \|DGL-FAULT\|FW-CRASH" | tr '\n' ' '; }
# Ends a --cmd job: the harness's last word.
done_cmd=(--cmd "SERSAY HX-DONE 0")
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
    # 86Box scancodes: G 0x22, Enter 0x1c, Esc 0x01; extended Right 0x14d, Down 0x150, Up 0x148.
    # Esc leaves the garage; then Esc pauses, Up wraps to QUIT, Enter.
    keys="@FW-PLAY ready,1:0x22,2.5:0x14d,3.5:0x1c,4.5:0x150,5.5:0x14d,6.5:0x1c,7.5:0x01,9:0x01,10:0x148,11:0x1c"
    st=$(run shop --cmd "FWHEEL -test -nosound -career CAREER.DAT -money 600 -noexit" \
         --cmd "FWHEEL -test -nosound -career CAREER.DAT -frames 30" --keys "$keys")
    l=$(log shop | grep -a 'FW-CAREER loaded' | tail -1)
    if [ "$st" = PASS ] && log shop | grep -q "FW-MENU buy SKY BLUE" && log shop | grep -q "FW-MENU buy BIG AIR" \
       && [[ $l == *"money 50,"*"paint SKY BLUE, horn BIG AIR"* ]]; then
        say shop "PASS (bought two, saved, loaded: ${l#*: })"
    else bad shop "($st; $(log shop | grep -a 'FW-MENU buy\|FW-MENU short\|FW-CAREER' | tr '\n' ' '))"; fi
}

exit_check() {  # NAME DESCRIPTION [run.py options...]: the program, then the machine checked
    local name=$1 what=$2 st; shift 2
    st=$(run "$name" --sound sb16 --pre "$blaster" --cmd "SERSAY HX-START $name" --cmd "VECCHK save" "$@")
    if [[ $st != GL-ERROR* ]] && after_ok "$name" && extra_ok "$name"; then say "$name" "PASS ($what)"
    else bad "$name" "($st; $(after_why "$name"))"; fi
}
extra_ok() { return 0; }

modes() {
    local st list i=0 cmds=() m n fail_m=0 res=""
    st=$(run modelist --args="-test -fixed -nosound -frames 2")
    list=$(log modelist | grep -ao 'FW-MODES .*' | head -1 | cut -d' ' -f2- | tr -d '\r')
    [ -n "$list" ] || { bad modes "($st; no FW-MODES)"; return; }
    n=$(echo $list | wc -w)
    for m in $list; do
        i=$((i + 1))
        if [ $i -lt $n ]; then cmds+=(--cmd "FWHEEL -test -fixed -nosound -mode $m -frames 61 -shot 60:M$i -noexit")
        else cmds+=(--cmd "FWHEEL -test -fixed -nosound -mode $m -frames 61 -shot 60:M$i"); fi
    done
    st=$(run modes "${cmds[@]}")
    i=0
    rm -rf "$out/modes-hl"; mkdir -p "$out/modes-hl/out"
    for m in $list; do
        i=$((i + 1))
        "$DEV" sh -c "cd $out/modes-hl && LP_NUM_THREADS=4 $PWD/build/headless/fwheel-hl -world $PWD/build/data/WORLD.PAK \
            -test -fixed -nosound -mode $m -frames 61 -shot 60:M$i > M$i.log 2>&1"
        "$DEV" python3 -c "import sys; sys.path.insert(0, '$MGAHAL/tools/loopa'); import png; \
w, h, rgb = png.read_ppm('$out/modes-hl/out/M$i.ppm'); png.write_png('$out/modes-hl/m$i.png', w, h, rgb)" 2>/dev/null
        if [ -e "$out/modes/m$i.png" ] && "$DEV" python3 "$MGAHAL/tools/imgcmp.py" "$out/modes-hl/m$i.png" \
               "$out/modes/m$i.png" > "$out/modes-hl/m$i.cmp" 2>&1; then res+=" $m"
        else res+=" $m:FAIL"; fail_m=1; fi
    done
    if [[ $st != GL-ERROR* ]] && ! log modes | grep -aq "HX-TEST .* FAIL" && [ $fail_m = 0 ] && [ $i -gt 0 ]; then
        say modes "PASS ($i modes, DOS = Mesa:$res)"
    else bad modes "($st;$res)"; fi
}

auto() {
    local st mine other c
    st=$(run auto --args="-test -fixed -nosound -autopilot -frames 1800 -hash")
    log auto | grep -a "FW-HASH" | tr -d '\r' > "$out/auto/hashes.txt"
    mine=$(wc -l < "$out/auto/hashes.txt")
    other=""
    for c in "$PWD"/out/suite-*/auto/hashes.txt; do
        [ "$c" = "$out/auto/hashes.txt" ] || [ ! -s "$c" ] && continue
        if cmp -s "$c" "$out/auto/hashes.txt"; then other+=" $(basename $(dirname $(dirname $c)) | sed 's/suite-//')"
        else bad auto "(hashes differ from $(dirname $(dirname $c)))"; return; fi
    done
    if [ "$st" = PASS ] && [ "$mine" -ge 30 ]; then say auto "PASS ($mine hashes${other:+, the same as on$other})"
    else bad auto "($st; $mine hashes)"; fi
}

for c in $checks; do
    case $c in
    exit)
        # Up from DRIVE wraps to QUIT.
        exit_check exit "QUIT on the title; SB stopped, vectors and keyboard back" \
            --cmd "FWHEEL -test -title -noexit" "${after[@]}" "${done_cmd[@]}" \
            --keys "@FW-MENU title,2:0x148,3:0x1c,@HX-KEYWAIT ready,1:0x39" ;;
    dglexit)
        extra_ok() { log "$1" | grep -aq "DGL-EXIT frames=60"; }
        exit_check dglexit "exit() inside a swap; SB stopped, vectors and keyboard back" \
            --cmd "SET DGL_EXIT_AFTER=60" --cmd "FWHEEL -test -autopilot -frames 600 -noexit" --cmd "SET DGL_EXIT_AFTER=" \
            "${after[@]}" "${done_cmd[@]}" --keys "@HX-KEYWAIT ready,1:0x39"
        extra_ok() { return 0; } ;;
    crash)
        extra_ok() { log "$1" | grep -aq "DGL-FAULT" && log "$1" | grep -aq "FW-CAREER loaded CAREER.DAT: money 777"; }
        exit_check crash "a #UD while driving: text mode, SB stopped, vectors and keyboard back; the career as saved" \
            --cmd "FWHEEL -test -nosound -career CAREER.DAT -money 777 -frames 10 -noexit" \
            --cmd "SET FW_CRASH=90" --cmd "FWHEEL -test -autopilot -career CAREER.DAT -noexit" --cmd "SET FW_CRASH=" \
            "${after[@]}" --cmd "FWHEEL -test -nosound -career CAREER.DAT -frames 10" \
            --keys "@HX-KEYWAIT ready,1:0x39"
        extra_ok() { return 0; } ;;
    ctrlc|ctrlbreak)
        # Left Ctrl 0x1d with C 0x2e, or with Break (the extended 0x146). The game
        # ends long before its 900 frames; then the machine is checked.
        [ $c = ctrlc ] && k=0x2e || k=0x146
        extra_ok() { log "$1" | grep -aq "FW-EXIT" && ! log "$1" | grep -aq "frames PASS 900"; }
        exit_check $c "a quit, as from the menu; SB stopped, vectors and keyboard back" \
            --cmd "FWHEEL -test -autopilot -frames 900 -noexit" "${after[@]}" "${done_cmd[@]}" \
            --keys "@FW-PLAY ready,2:0x1d:down,2.2:$k,2.5:0x1d:up,@HX-KEYWAIT ready,1:0x39"
        extra_ok() { return 0; } ;;
    mem)
        st=$(run mem --mem 32 --sound sb16 --pre "$blaster" --args="-test -autopilot -frames 900")
        m=$(log mem | grep -ao 'FW-MEM heap=[0-9]* KB free=[0-9]* KB' | head -1)
        h=$(echo "$m" | sed -n 's/.*heap=\([0-9]*\).*/\1/p')
        if [ "$st" = PASS ] && [ -n "$h" ] && [ "$h" -le 20480 ]; then say mem "PASS (a 32 MB machine: $m)"
        else bad mem "($st; ${m:-no FW-MEM})"; fi ;;
    modes) modes ;;
    auto) auto ;;
    release)
        z=$(ls -t dist/fwheel-*.zip 2>/dev/null | head -1)
        [ -n "$z" ] || { bad release "(no dist/fwheel-*.zip: make release)"; continue; }
        rm -rf "$out/release-files"; mkdir -p "$out/release-files"
        unzip -q "$z" -d "$out/release-files"
        files=()
        for f in "$out"/release-files/*; do files+=(--file "$f=/FW/$(basename "$f")"); done
        st=$(run release "${files[@]}" --sound sb16 --pre "$blaster" --cmd "SET PATH=A:\\FREEDOS\\BIN" \
             --cmd "CD \\FW" --cmd "FWHEEL -test -autopilot -frames 300")
        if [ "$st" = PASS ] && log release | grep -aq "FW-START" && log release | grep -aq "HX-TEST audio PASS"; then
            say release "PASS ($(basename "$z"): $(ls "$out/release-files" | wc -l) files; it runs from its own directory)"
        else bad release "($st; $(log release | grep -a 'Load error\|no DPMI\|HX-TEST .* FAIL' | head -2 | tr '\n' ' '))"; fi ;;
    joy) joy ;;
    shop) shop ;;
    menus)
        # Down 0x150, Up 0x148, Right 0x14d, Enter 0x1c, Esc 0x01.
        k="@FW-MENU title,1:0x150,2:0x150,3:0x1c,4:0x14d,5:0x01,6:0x1c,8:0x01,9:0x148,10:0x1c"
        st=$(run menus --cmd "FWHEEL -test -nosound -title -noexit" --cmd "FWHEEL -test -nosound -frames 30" --keys "$k")
        if [ "$st" = PASS ] && log menus | grep -q "FW-MENU DETAIL high" && log menus | grep -q "FW-MENU pause" \
           && log menus | grep -q "FW-MENU quit" && log menus | grep -q "FW-CFG detail high"; then
            say menus "PASS (options saved, driven, paused, quit; the next run starts with detail high)"
        else bad menus "($st; $(log menus | grep -a 'FW-MENU\|FW-CFG detail' | tr '\n' ' '))"; fi ;;
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
