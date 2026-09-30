# Testing

| What | Command | Passes when |
|---|---|---|
| DOS in 86Box | `make loopa CARD=g450` (or g200, g400) | `HX-DONE 0`: the frames ran, the snapshot was written |
| DOS against Mesa | a DOS `-test -fixed -frames 120 -shot 119:F0` run and `build/headless/fwheel-hl` with the same options, compared with the harness's `imgcmp.py` | within conformance tolerances (RGB565 quantisation, tolerance 24, 0.5%, edges excluded) |
| Sound | the DOS run with `-tone`, `--sound sb16 --wav`, and `wavcheck.py audio.wav --tone 440` | 440 Hz found |
| Linux | `xvfb-run build/linux/fwheel -test -fixed -frames 120 -shot 119:F0L -nosound` in the dev container | the frame equals the headless one |

| Autopilot | `-test -fixed -autopilot -laps 1 -hash` (DOS in Loop A with `--file build/data/YARD.PAK`, or headless) | a lap with no damage (`FW-LAP`, `HX-TEST laps`) |
| Replays | record with `-record FILE`, play with `-replay FILE` on the same platform | `HX-TEST replay PASS`: every second's state hash as recorded |

86Box's speed says nothing about real hardware: performance numbers come
from real silicon and the bench only.
