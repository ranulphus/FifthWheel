#!/usr/bin/env bash
# winvm.sh - a ready-to-boot 86Box machine with Fifth Wheel installed, for
# MGA-Glide's patched 86Box (its Windows kit, or `make 86box` on Linux).
#
#   tools/winvm.sh [CARD]      (default g450) -> dist/fwheel-CARD-vm.zip
#
# The machine is Loop A's (MGA-Glide tools/86box/mkwinvm.py): a Pentium II
# 350, 64 MB, the Matrox CARD, a Sound Blaster 16 (BLASTER set), FreeDOS.
# D:\FW holds the release ZIP's files (make release); D:\FW.BAT starts the
# game, so typing FW at the prompt plays. No retail data: the zip can be
# shared. Needs MGAHAL (make winvm sets it).
set -euo pipefail
: "${MGAHAL:?}"
card=${1:-g450}
id=$(git describe --tags --always --dirty)
zip=dist/fwheel-$id.zip
[ -f "$zip" ] || { echo "winvm: no $zip (make release)" >&2; exit 1; }
stage=$PWD/build/winvm
rm -rf "$stage"
mkdir -p "$stage/FW"
unzip -q "$zip" -d "$stage/FW"
printf '%s\r\n' "@ECHO OFF" "REM FW [options]: Fifth Wheel (options go to the game, e.g. -detail low)" \
    "D:" "CD \\FW" "FWHEEL %1 %2 %3 %4 %5 %6 %7 %8 %9" "C:" "CD \\" > "$stage/FW.BAT"
cat > "$stage/notes.txt" <<NOTES
Fifth Wheel ($id) on the emulated Matrox $(echo "$card" | tr a-z A-Z)

At the DOS prompt, type FW to play. D:\FW holds the game (FWHEEL.EXE, the
world, CWSDPMI) and its README.TXT, which lists the keys; the game keeps
FWHEEL.CFG (options, the joystick's set-up) and CAREER.DAT (money and the
garage's items) there too. Options go to the game: FW -detail low, say.

Sound comes from the emulated Sound Blaster 16 (BLASTER is set in
C:\RUN.BAT). A joystick or wheel: in 86Box's settings (Input devices),
pick a joystick type and map your controller to it, then press J in the
game to set it up.

86Box runs the Matrox 3D engine and the Pentium II much slower than the
real machines: the frame rate here says nothing about real hardware.
NOTES
"$MGAHAL/tools/dev" python3 "$MGAHAL/tools/86box/mkwinvm.py" --name "fwheel-$card" --card "$card" --no-voodoo \
    --mem 64 --d-cylinders 64 --readme "$stage/notes.txt" --out "$PWD/dist/fwheel-$card-vm.zip" \
    --d-dir "$stage/FW=/FW" --d-file "$stage/FW.BAT=/FW.BAT" \
    --run "ECHO Fifth Wheel: type FW to play (README.txt has the rest)."
echo "winvm: dist/fwheel-$card-vm.zip"
