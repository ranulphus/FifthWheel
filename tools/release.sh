#!/bin/sh
# release.sh - the DOS release: dist/fwheel-ID.zip with the game, the world,
# the presets, CWSDPMI and the text files (8.3 names, CRLF line ends).
# Refuses a tree with uncommitted changes (its build would say "-dirty")
# unless RELEASE_DIRTY=1. Needs DOSGL, DJGPP_PREFIX (make release sets
# them) and CWSDPMI_ZIP for CWSDPMI.DOC (else only the EXE from DJGPP).
#
#   tools/release.sh
set -eu
: "${DOSGL:?}" "${DJGPP_PREFIX:?}"
if [ -n "$(git status --porcelain --untracked-files=no)" ] && [ "${RELEASE_DIRTY:-0}" != 1 ]; then
    echo "release: uncommitted changes (RELEASE_DIRTY=1 to build anyway)" >&2
    exit 1
fi
id=$(git describe --tags --always --dirty)
stage=build/release/FWHEEL
rm -rf build/release
mkdir -p "$stage" dist

cp build/dos/FWHEEL.EXE build/data/WORLD.PAK "$stage/"
sed 's/\r$//; s/$/\r/' data/budget.cfg > "$stage/BUDGET.CFG"
cp "$DJGPP_PREFIX/dos/CWSDPMI.EXE" "$stage/"
if [ -n "${CWSDPMI_ZIP:-}" ] && [ -f "$CWSDPMI_ZIP" ]; then
    unzip -p "$CWSDPMI_ZIP" bin/cwsdpmi.doc > "$stage/CWSDPMI.DOC"
else
    echo "release: no CWSDPMI_ZIP: CWSDPMI.DOC left out (THIRDPTY.TXT says where it is)" >&2
fi

crlf() { sed 's/\r$//; s/$/\r/' "$1"; }
crlf data/release/README.TXT > "$stage/README.TXT"
crlf LICENSE > "$stage/LICENSE.TXT"
{
    crlf data/release/THIRDPTY.TXT
    for l in "DOS-GL:$DOSGL/LICENSE" "The MGA-Glide HAL:$DOSGL/third_party/mgahal/LICENSE" \
             "SDL 3:$DOSGL/third_party/sdl/LICENSE.txt"; do
        printf -- '----------------------------------------------------------------------\r\n%s\r\n\r\n' "${l%%:*}"
        crlf "${l#*:}"
        printf '\r\n'
    done
} > "$stage/THIRDPTY.TXT"

# Every name 8.3 and upper case, as DOS shows them.
for f in "$stage"/*; do
    n=$(basename "$f")
    echo "$n" | grep -Eq '^[A-Z0-9_]{1,8}(\.[A-Z0-9]{1,3})?$' || { echo "release: $n is not 8.3" >&2; exit 1; }
done
zip -q -X -j "dist/fwheel-$id.zip" "$stage"/*
echo "release: dist/fwheel-$id.zip"
unzip -l "dist/fwheel-$id.zip" | tail -n +2
