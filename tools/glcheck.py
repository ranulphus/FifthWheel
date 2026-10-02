#!/usr/bin/env python3
"""glcheck.py - every OpenGL function the game and the kit call must be one
DOS-GL implements (not one of its logged stubs): the GL subset, checked at
build time rather than found as a DGL-STUB line in a run.

  glcheck.py DOSGL_DIR [SOURCES...]   (default: game/src/*.c kit/src/*.c,
                                       less plat_headless.c, Linux only)

DOS-GL's implemented functions are those its sources define with APIENTRY;
the rest of GL 1.1 (src/gl/gl11.api) are stubs. Exit status 0 when every
call is implemented."""
import glob
import os
import re
import sys

CALL = re.compile(r"\b(gl[A-Z]\w*)\s*\(")
DEFINED = re.compile(r"APIENTRY\s+(gl[A-Z]\w*)\s*\(")
NOT_DOS = {"plat_headless.c"}            # OSMesa: the headless build only


def implemented(dosgl):
    names = set()
    for path in glob.glob(os.path.join(dosgl, "src", "**", "*.c"), recursive=True):
        names.update(DEFINED.findall(open(path, errors="replace").read()))
    return names


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    return re.sub(r"//[^\n]*", " ", text)


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    dosgl = sys.argv[1]
    sources = sys.argv[2:] or sorted(glob.glob("game/src/*.c") + glob.glob("kit/src/*.c"))
    have = implemented(dosgl)
    if not have:
        sys.exit("glcheck: no APIENTRY functions under %s/src" % dosgl)
    used, missing = {}, {}
    for path in sources:
        if os.path.basename(path) in NOT_DOS:
            continue
        for name in CALL.findall(strip_comments(open(path, errors="replace").read())):
            used.setdefault(name, set()).add(os.path.basename(path))
    for name, files in sorted(used.items()):
        if name not in have:
            missing[name] = files
    for name, files in sorted(missing.items()):
        print("glcheck: %s (in %s) is not implemented by DOS-GL" % (name, ", ".join(sorted(files))))
    print("glcheck: %d GL functions used, %d implemented by DOS-GL, %d not" % (len(used), len(used) - len(missing),
                                                                             len(missing)))
    sys.exit(1 if missing else 0)


if __name__ == "__main__":
    main()
