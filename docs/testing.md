# Testing

| What | Command | Passes when |
|---|---|---|
| DOS in 86Box | `make loopa CARD=g450` (or g200, g400) | `HX-DONE 0`: the frames ran, the snapshot was written |
| DOS against Mesa | a DOS `-test -fixed -frames 120 -shot 119:F0` run and `build/headless/fwheel-hl` with the same options, compared with the harness's `imgcmp.py` | within conformance tolerances (RGB565 quantisation, tolerance 24, 0.5%, edges excluded) |
| Sound | the DOS run with `-tone`, `--sound sb16 --wav`, and `wavcheck.py audio.wav --tone 440` | 440 Hz found |
| Linux | `xvfb-run build/linux/fwheel -test -fixed -frames 120 -shot 119:F0L -nosound` in the dev container | the frame equals the headless one |
| Autopilot | `-test -fixed -autopilot -laps 1 -hash` (DOS in Loop A with `--file build/data/YARD.PAK`, or headless) | a lap with no damage (`FW-LAP`, `HX-TEST laps`) |
| World | `make data-check` | seed 1's pack hash equals `data/golden/world.sha`, built at -O0 and -O2 |
| World pictures | `make shots CARD=g450` (`tools/shots.sh`): `-tourshots 10` and a frame with the HUD (`-frames 61 -shot 60:H0`) on DOS in Loop A and headless, compared with `imgcmp.py` | the ten poses along the tour and at two depots, and the HUD frame, within conformance tolerances |
| Jobs | `-test -fixed -autojob` (headless, or DOS in Loop A: `make loopa ARGS="-test -fixed -autojob -hash" LOOPA_TIMEOUT=1800`); `-job D:T:B` picks the job (depots and bay from 0) | `HX-TEST job PASS`: coupled, hauled and docked at grade OK or better (`FW-JOB done grade=...`) |
| Every job | `make jobsweep` (headless, every depot's trailer to every other depot's three bays; `out/jobsweep/results.txt`) | every job passes |
| Replays | record with `-record FILE`, play with `-replay FILE` on the same platform | `HX-TEST replay PASS`: every second's state hash as recorded |

86Box's speed says nothing about real hardware: performance numbers come
from real silicon and the bench only.
